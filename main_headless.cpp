#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "volrend/common.hpp"
#include "volrend/n3tree.hpp"

#include "volrend/internal/opts.hpp"

#include "volrend/cuda/common.cuh"
#include "volrend/cuda/renderer_kernel.hpp"
#include "volrend/internal/imwrite.hpp"

namespace {
std::string path_basename(const std::string &str) {
    for (size_t i = str.size() - 1; ~i; --i) {
        const char c = str[i];
        if (c == '/' || c == '\\') {
            return str.substr(i);
        }
    }
    return str;
}

std::string remove_ext(const std::string &str) {
    for (size_t i = str.size() - 1; ~i; --i) {
        const char c = str[i];
        if (c == '.') {
            return str.substr(0, i);
        }
    }
    return str;
}

glm::mat4x3 read_transform_matrix(const std::string &path) {
    glm::mat4x3 tmp;
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "ERROR: '" << path << "' does not exist\n";
        std::exit(1);
    }
    // Recall GL is column major
    ifs >> tmp[0][0] >> tmp[1][0] >> tmp[2][0] >> tmp[3][0];
    ifs >> tmp[0][1] >> tmp[1][1] >> tmp[2][1] >> tmp[3][1];
    ifs >> tmp[0][2] >> tmp[1][2] >> tmp[2][2] >> tmp[3][2];
    return tmp;
}

void read_intrins(const std::string &path, float &fx, float &fy) {
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "ERROR: intrin '" << path << "' does not exist\n";
        std::exit(1);
    }
    float _;  // garbage
    ifs >> fx >> _ >> _ >> _;
    ifs >> _ >> fy;
}
}  // namespace

int main(int argc, char *argv[]) {
    using namespace volrend;
    cxxopts::Options cxxoptions(
        "volrend_headless",
        "Headless octree volume rendering (c) VOLREND contributors 2021");
    internal::add_common_opts(cxxoptions);

    // clang-format off
    cxxoptions.add_options()
        ("o,write_images", "output directory of images; "
         "if empty, DOES NOT save (for timing only)",
                cxxopts::value<std::string>()->default_value(""))
        ("i,intrin", "intrinsics matrix 4x4; if set, overrides the fx/fy",
                cxxopts::value<std::string>()->default_value(""))
        ;
    // clang-format on

    cxxoptions.allow_unrecognised_options();

    // Pass a list of camera pose *.txt files after npz file
    // each file should have 4x4 c2w pose matrix
    cxxoptions.positional_help("npz_file [c2w_txt_4x4...]");

    cxxopts::ParseResult args = internal::parse_options(cxxoptions, argc, argv);

    const int device_id = args["gpu"].as<int>();
    if (~device_id) {
        cuda(SetDevice(device_id));
    }

    // Load all transform matrices
    std::vector<glm::mat4x3> trans;
    std::vector<std::string> basenames;
    for (auto path : args.unmatched()) {
        trans.push_back(read_transform_matrix(path));
        basenames.push_back(remove_ext(path_basename(path)));
    }

    if (trans.size() == 0) {
        std::cerr << "WARNING: No camera poses specified, quitting\n";
        return 1;
    }
    std::string out_dir = args["write_images"].as<std::string>();

    N3Tree tree(args["file"].as<std::string>());
    int width = args["width"].as<int>(), height = args["height"].as<int>();
    float fx = args["fx"].as<float>();
    if (fx < 0) fx = 1111.11f;
    float fy = args["fy"].as<float>();

    {
        // Load intrin matrix
        std::string intrin_path = args["intrin"].as<std::string>();
        if (intrin_path.size()) {
            read_intrins(intrin_path, fx, fy);
        }
    }

    Camera camera(width, height, fx, fy);
    cudaArray_t array;
    cudaStream_t stream;

    cudaChannelFormatDesc channelDesc =
        cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsigned);

    std::vector<uint8_t> buf;
    if (out_dir.size()) buf.resize(4 * width * height);

    cuda(MallocArray(&array, &channelDesc, width, height));
    cuda(StreamCreateWithFlags(&stream, cudaStreamDefault));
    cudaArray_t depth_arr = nullptr;  // Not using depth buffer

    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    cudaEventRecord(start);
    for (size_t i = 0; i < trans.size(); ++i) {
        camera.transform = trans[i];
        camera._update(false);

        RenderOptions options = internal::render_options_from_args(args);

        launch_renderer(tree, camera, options, array, depth_arr, stream, true);

        if (out_dir.size()) {
            cuda(Memcpy2DFromArrayAsync(buf.data(), 4 * width, array, 0, 0,
                                        4 * width, height,
                                        cudaMemcpyDeviceToHost, stream));
            std::string fpath = out_dir + "/" + basenames[i] + ".png";
            internal::write_png_file(fpath, buf.data(), width, height);
        }
    }
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);
    float milliseconds = 0;
    cudaEventElapsedTime(&milliseconds, start, stop);
    milliseconds = milliseconds / trans.size();

    std::cout << std::fixed << std::setprecision(10);
    std::cout << milliseconds << " ms per frame\n";
    std::cout << 1000.f / milliseconds << " fps\n";

    cuda(FreeArray(array));
    cuda(StreamDestroy(stream));
}