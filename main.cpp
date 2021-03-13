#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstdlib>
#include <cstdio>
#include <string>

#include "volrend/renderer.hpp"
#include "volrend/n3tree.hpp"

#include "volrend/internal/opts.hpp"
#include "volrend/internal/imwrite.hpp"

#include "imgui_impl_opengl3.h"
#include "imgui_impl_glfw.h"
#include "imfilebrowser.h"

#ifdef VOLREND_CUDA
#include "volrend/cuda/common.cuh"
#endif

namespace volrend {

// Starting CUDA/OpenGL interop code from
// https://gist.github.com/allanmac/4ff11985c3562830989f

namespace {

#define GET_RENDERER(window) \
    (*((VolumeRenderer*)glfwGetWindowUserPointer(window)))

void glfw_update_title(GLFWwindow* window) {
    // static fps counters
    // Source: http://antongerdelan.net/opengl/glcontext2.html
    static double stamp_prev = 0.0;
    static int frame_count = 0;

    const double stamp_curr = glfwGetTime();
    const double elapsed = stamp_curr - stamp_prev;

    if (elapsed > 0.5) {
        stamp_prev = stamp_curr;

        const double fps = (double)frame_count / elapsed;

        char tmp[128];
        sprintf(tmp, "volrend viewer - FPS: %.2f", fps);
        glfwSetWindowTitle(window, tmp);
        frame_count = 0;
    }

    frame_count++;
}

void draw_imgui(VolumeRenderer& rend, N3Tree& tree) {
    auto& cam = rend.camera;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(20.f, 20.f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(340.f, 400.f), ImGuiCond_Once);

    static char title[128] = {0};
    if (title[0] == 0) {
        sprintf(title, "volrend backend: %s", rend.get_backend());
    }

    static ImGui::FileBrowser open_obj_mesh_dialog, open_tree_dialog,
        save_screenshot_dialog(ImGuiFileBrowserFlags_EnterNewFilename);
    if (open_obj_mesh_dialog.GetTitle().empty()) {
        open_obj_mesh_dialog.SetTypeFilters({".obj"});
        open_obj_mesh_dialog.SetTitle("Load basic triangle OBJ");
    }
    if (open_tree_dialog.GetTitle().empty()) {
        open_tree_dialog.SetTypeFilters({".npz"});
        open_tree_dialog.SetTitle("Load N3Tree npz from svox");
    }
    if (save_screenshot_dialog.GetTitle().empty()) {
        save_screenshot_dialog.SetTypeFilters({".png"});
        save_screenshot_dialog.SetTitle("Save screenshot (png)");
    }

    // Begin window
    ImGui::Begin(title);

    if (ImGui::Button("Open tree")) {
        open_tree_dialog.Open();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save screenshot")) {
        save_screenshot_dialog.Open();
    }

    open_tree_dialog.Display();
    if (open_tree_dialog.HasSelected()) {
        // Load octree
        std::string path = open_tree_dialog.GetSelected().string();
        Mesh tmp;
        std::cout << "Load N3Tree npz: " << path << "\n";
        tree.open(path);
        rend.set(tree);
        open_tree_dialog.ClearSelected();
    }

    save_screenshot_dialog.Display();
    if (save_screenshot_dialog.HasSelected()) {
        // Save screenshot
        std::string path = save_screenshot_dialog.GetSelected().string();
        save_screenshot_dialog.ClearSelected();
        int width = rend.camera.width, height = rend.camera.height;
        std::vector<unsigned char> windowPixels(4 * width * height);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                     &windowPixels[0]);

        std::vector<unsigned char> flippedPixels(4 * width * height);
        for (int row = 0; row < height; ++row)
            memcpy(&flippedPixels[row * width * 4],
                   &windowPixels[(height - row - 1) * width * 4], 4 * width);

        if (path.size() < 4 ||
            path.compare(path.size() - 4, 4, ".png", 0, 4) != 0) {
            path.append(".png");
        }
        if (internal::write_png_file(path, flippedPixels.data(), width,
                                     height)) {
            std::cout << "Wrote " << path << "\n";
        } else {
            std::cout << "Failed to save screenshot\n";
        }
    }

    ImGui::SetNextTreeNodeOpen(false, ImGuiCond_Once);
    if (ImGui::TreeNode("Camera")) {
        // Update vectors indirectly since we need to normalize on change
        // (press update button) and it would be too confusing to keep
        // normalizing
        static glm::vec3 world_up_tmp = rend.camera.v_world_up;
        static glm::vec3 world_down_prev = rend.camera.v_world_up;
        static glm::vec3 back_tmp = rend.camera.v_back;
        static glm::vec3 forward_prev = rend.camera.v_back;
        if (cam.v_world_up != world_down_prev)
            world_up_tmp = world_down_prev = cam.v_world_up;
        if (cam.v_back != forward_prev) back_tmp = forward_prev = cam.v_back;

        ImGui::InputFloat3("center", glm::value_ptr(cam.center));
        ImGui::InputFloat3("origin", glm::value_ptr(cam.origin));
        ImGui::SliderFloat("fx", &cam.fx, 300.f, 7000.f);
        ImGui::SliderFloat("fy", &cam.fy, 300.f, 7000.f);
        if (ImGui::TreeNode("Directions")) {
            ImGui::InputFloat3("world_up", glm::value_ptr(world_up_tmp));
            ImGui::InputFloat3("back", glm::value_ptr(back_tmp));
            if (ImGui::Button("normalize & update dirs")) {
                cam.v_world_up = glm::normalize(world_up_tmp);
                cam.v_back = glm::normalize(back_tmp);
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }  // End camera node

    ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Render")) {
        static float inv_step_size = 1.0f / rend.options.step_size;
        if (ImGui::SliderFloat("1/eps", &inv_step_size, 128.f, 10000.f)) {
            rend.options.step_size = 1.f / inv_step_size;
        }
        ImGui::SliderFloat("sigma_thresh", &rend.options.sigma_thresh, 0.f,
                           100.0f);
        ImGui::SliderFloat("stop_thresh", &rend.options.stop_thresh, 0.001f,
                           0.4f);
        ImGui::SliderFloat("bg_brightness", &rend.options.background_brightness,
                           0.f, 1.0f);

        ImGui::TreePop();
    }  // End render node
#ifdef VOLREND_CUDA
    ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Visualization")) {
        ImGui::PushItemWidth(230);
        ImGui::SliderFloat3("bb_min", rend.options.render_bbox, 0.0, 1.0);
        ImGui::SliderFloat3("bb_max", rend.options.render_bbox + 3, 0.0, 1.0);
        ImGui::SliderInt("decomp", &rend.options.basis_id, -1,
                         tree.data_format.basis_dim - 1);
        ImGui::SliderFloat3("vdir shift", rend.options.rot_dirs, -M_PI / 4,
                            M_PI / 4);
        ImGui::PopItemWidth();
        if (ImGui::Button("reset vdir shift")) {
            for (int i = 0; i < 3; ++i) rend.options.rot_dirs[i] = 0.f;
        }
        ImGui::Checkbox("show grid", &rend.options.show_grid);
        ImGui::SameLine();
        ImGui::Checkbox("render depth", &rend.options.render_depth);
        ImGui::TreePop();
    }

    ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Manipulation")) {
        for (auto& mesh : rend.meshes) {
            if (ImGui::TreeNode(mesh.name.c_str())) {
                ImGui::PushItemWidth(230);
                ImGui::SliderFloat3("trans", glm::value_ptr(mesh.translation),
                                    -2.0f, 2.0f);
                ImGui::SliderFloat3("rot", glm::value_ptr(mesh.rotation), -M_PI,
                                    M_PI);
                ImGui::SliderFloat("scale", &mesh.scale, 0.01f, 10.0f);
                ImGui::PopItemWidth();
                ImGui::Checkbox("unlit", &mesh.unlit);
                ImGui::TreePop();
            }
        }
        if (ImGui::Button("Add Sphere")) {
            static int sphereid = 0;
            {
                Mesh sph = Mesh::Sphere();
                sph.scale = 0.1f;
                sph.translation[2] = 1.0f;
                sph.update();
                if (sphereid) sph.name = sph.name + std::to_string(sphereid);
                ++sphereid;
                rend.meshes.push_back(std::move(sph));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cube")) {
            static int cubeid = 0;
            {
                Mesh cube = Mesh::Cube();
                cube.scale = 0.2f;
                cube.translation[2] = 1.0f;
                cube.update();
                if (cubeid) cube.name = cube.name + std::to_string(cubeid);
                ++cubeid;
                rend.meshes.push_back(std::move(cube));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Tri OBJ")) {
            open_obj_mesh_dialog.Open();
        }
        ImGui::TreePop();
    }
#endif
    ImGui::End();

    open_obj_mesh_dialog.Display();
    if (open_obj_mesh_dialog.HasSelected()) {
        // Load mesh
        std::string path = open_obj_mesh_dialog.GetSelected().string();
        Mesh tmp;
        std::cout << "Load OBJ: " << path << "\n";
        tmp.load_basic_obj(path);
        if (tmp.vert.size()) {
            tmp.update();
            rend.meshes.push_back(std::move(tmp));
            std::cout << "Load success\n";
        } else {
            std::cout << "Load failed\n";
        }
        open_obj_mesh_dialog.ClearSelected();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void glfw_error_callback(int error, const char* description) {
    fputs(description, stderr);
}

void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action,
                       int mods) {
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        auto& rend = GET_RENDERER(window);
        auto& cam = rend.camera;
        switch (key) {
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, GL_TRUE);
                break;
            case GLFW_KEY_W:
            case GLFW_KEY_S:
            case GLFW_KEY_A:
            case GLFW_KEY_D:
            case GLFW_KEY_E:
            case GLFW_KEY_Q: {
                float speed = 0.002f;
                if (mods & GLFW_MOD_SHIFT) speed *= 5.f;
                if (key == GLFW_KEY_S || key == GLFW_KEY_A || key == GLFW_KEY_E)
                    speed = -speed;
                const auto& vec = (key == GLFW_KEY_A || key == GLFW_KEY_D)
                                      ? cam.v_right
                                      : (key == GLFW_KEY_W || key == GLFW_KEY_S)
                                            ? -cam.v_back
                                            : -cam.v_up;
                cam.move(vec * speed);
            } break;

            case GLFW_KEY_MINUS:
                cam.fx *= 0.99f;
                cam.fy *= 0.99f;
                break;

            case GLFW_KEY_EQUAL:
                cam.fx *= 1.01f;
                cam.fy *= 1.01f;
                break;

            case GLFW_KEY_0:
                cam.fx = CAMERA_DEFAULT_FOCAL_LENGTH;
                cam.fy = CAMERA_DEFAULT_FOCAL_LENGTH;
                break;

            case GLFW_KEY_1:
                cam.v_world_up = glm::vec3(0.f, 0.f, 1.f);
                break;

            case GLFW_KEY_2:
                cam.v_world_up = glm::vec3(0.f, 0.f, -1.f);
                break;

            case GLFW_KEY_3:
                cam.v_world_up = glm::vec3(0.f, 1.f, 0.f);
                break;

            case GLFW_KEY_4:
                cam.v_world_up = glm::vec3(0.f, -1.f, 0.f);
                break;

            case GLFW_KEY_5:
                cam.v_world_up = glm::vec3(1.f, 0.f, 0.f);
                break;

            case GLFW_KEY_6:
                cam.v_world_up = glm::vec3(-1.f, 0.f, 0.f);
                break;
        }
    }
}

void glfw_mouse_button_callback(GLFWwindow* window, int button, int action,
                                int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    if (ImGui::GetIO().WantCaptureMouse) return;

    auto& rend = GET_RENDERER(window);
    auto& cam = rend.camera;
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    if (action == GLFW_PRESS) {
        cam.begin_drag(
            x, y, (mods & GLFW_MOD_SHIFT) || button == GLFW_MOUSE_BUTTON_MIDDLE,
            button == GLFW_MOUSE_BUTTON_RIGHT ||
                button == GLFW_MOUSE_BUTTON_MIDDLE);
    } else if (action == GLFW_RELEASE) {
        cam.end_drag();
    }
}

void glfw_cursor_pos_callback(GLFWwindow* window, double x, double y) {
    GET_RENDERER(window).camera.drag_update(x, y);
}

void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto& cam = GET_RENDERER(window).camera;
    // Focal length adjusting was very annoying so changed it to movement in z
    // cam.focal *= (yoffset > 0.f) ? 1.01f : 0.99f;
    const float speed_fact = 1e-1f;
    cam.move(cam.v_back * ((yoffset < 0.f) ? speed_fact : -speed_fact));
}

GLFWwindow* glfw_init(const int width, const int height) {
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) std::exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_DEPTH_BITS, GL_TRUE);
    // glfwWindowHint(GLFW_DEPTH_BITS, GL_FALSE);
    // glfwWindowHint(GLFW_STENCIL_BITS, GL_FALSE);

    // glfwWindowHint(GLFW_SRGB_CAPABLE, GL_TRUE);
    // glEnable(GL_FRAMEBUFFER_SRGB);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window =
        glfwCreateWindow(width, height, "volrend viewer", NULL, NULL);

    glClearDepth(1.0);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (window == nullptr) {
        glfwTerminate();
        std::exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fputs("GLEW init failed\n", stderr);
        getchar();
        glfwTerminate();
        std::exit(EXIT_FAILURE);
    }

    // ignore vsync for now
    glfwSwapInterval(0);

    // only copy r/g/b
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplGlfw_InitForOpenGL(window, false);
    char* glsl_version = NULL;
    ImGui_ImplOpenGL3_Init(glsl_version);
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;
    glfwSetCharCallback(window, ImGui_ImplGlfw_CharCallback);

    return window;
}

void glfw_window_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    GET_RENDERER(window).resize(width, height);
}

}  // namespace
}  // namespace volrend

int main(int argc, char* argv[]) {
    using namespace volrend;

    cxxopts::Options cxxoptions(
        "volrend",
        "OpenGL octree volume rendering (c) VOLREND contributors 2021");

    internal::add_common_opts(cxxoptions);
    // clang-format off
    cxxoptions.add_options()
        ("nogui", "disable imgui", cxxopts::value<bool>())
        ("center", "camera center position (world); ignored for NDC",
                cxxopts::value<std::vector<float>>()->default_value(
                                                        "-2.2,0,2.2"))
        ("back", "camera's back direction unit vector (world) for orientation; ignored for NDC",
                cxxopts::value<std::vector<float>>()->default_value("-0.7071068,0,0.7071068"))
        ("origin", "origin for right click rotation controls; ignored for NDC",
                cxxopts::value<std::vector<float>>()->default_value("0,0,0"))
        ("world_up", "world up direction for rotating controls e.g. "
                     "0,0,1=blender; ignored for NDC",
                cxxopts::value<std::vector<float>>()->default_value("0,0,1"))
        ;
    // clang-format on

    cxxoptions.positional_help("npz_file");

    cxxopts::ParseResult args = internal::parse_options(cxxoptions, argc, argv);

#ifdef VOLREND_CUDA
    const int device_id = args["gpu"].as<int>();
    if (~device_id) {
        cuda(SetDevice(device_id));
    }
#endif

    N3Tree tree(args["file"].as<std::string>());
    int width = args["width"].as<int>(), height = args["height"].as<int>();
    float fx = args["fx"].as<float>();
    float fy = args["fy"].as<float>();
    bool nogui = args["nogui"].as<bool>();

    GLFWwindow* window = glfw_init(width, height);

    {
        VolumeRenderer rend;
        if (fx > 0.f) {
            rend.camera.fx = fx;
        }

        rend.options = internal::render_options_from_args(args);
        if (tree.use_ndc) {
            // Special inital coordinates for NDC
            // (pick average camera)
            rend.camera.center = glm::vec3(0);
            rend.camera.origin = glm::vec3(0, 0, -3);
            rend.camera.v_back = glm::vec3(0, 0, 1);
            rend.camera.v_world_up = glm::vec3(0, 1, 0);
            if (fx <= 0) {
                rend.camera.fx = rend.camera.fy = tree.ndc_focal * 0.25f;
            }
            rend.camera.movement_speed = 0.1f;
        } else {
            auto cen = args["center"].as<std::vector<float>>();
            rend.camera.center = glm::vec3(cen[0], cen[1], cen[2]);
            auto origin = args["origin"].as<std::vector<float>>();
            rend.camera.origin = glm::vec3(origin[0], origin[1], origin[2]);
            auto world_up = args["world_up"].as<std::vector<float>>();
            rend.camera.v_world_up =
                glm::vec3(world_up[0], world_up[1], world_up[2]);
            auto back = args["back"].as<std::vector<float>>();
            rend.camera.v_back = glm::vec3(back[0], back[1], back[2]);
        }
        if (fy <= 0.f) {
            rend.camera.fy = rend.camera.fx;
        }
        rend.set(tree);

        // Get initial width/height
        {
            glfwGetFramebufferSize(window, &width, &height);
            rend.resize(width, height);
        }

        // Set user pointer and callbacks
        glfwSetWindowUserPointer(window, &rend);
        glfwSetKeyCallback(window, glfw_key_callback);
        glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
        glfwSetCursorPosCallback(window, glfw_cursor_pos_callback);
        glfwSetScrollCallback(window, glfw_scroll_callback);
        glfwSetFramebufferSizeCallback(window, glfw_window_size_callback);

        while (!glfwWindowShouldClose(window)) {
            glEnable(GL_DEPTH_TEST);
            glfw_update_title(window);

            rend.render();

            if (!nogui) draw_imgui(rend, tree);

            glfwSwapBuffers(window);
            glFinish();
            glfwPollEvents();
            // glfwWaitEvents();
            // break;
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}
