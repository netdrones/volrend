#include "volrend/common.hpp"

// CUDA backend only enabled when VOLREND_USE_CUDA=ON
#ifdef VOLREND_CUDA
#include "volrend/renderer.hpp"
#include "volrend/mesh.hpp"

#include <ctime>
#include <GL/glew.h>
#include <cuda_gl_interop.h>
#include <array>

#include "volrend/cuda/common.cuh"
#include "volrend/cuda/renderer_kernel.hpp"

namespace volrend {

struct VolumeRenderer::Impl {
    Impl(Camera& camera, RenderOptions& options, std::vector<Mesh>& meshes)
        : camera(camera), options(options), meshes(meshes), buf_index(0) {}

    ~Impl() {
        // Unregister CUDA resources
        for (int index = 0; index < cgr.size(); index++) {
            if (cgr[index] != nullptr)
                cuda(GraphicsUnregisterResource(cgr[index]));
        }
        glDeleteRenderbuffers(2, rb.data());
        glDeleteRenderbuffers(2, depth_rb.data());
        glDeleteRenderbuffers(2, depth_buf_rb.data());
        glDeleteFramebuffers(2, fb.data());
        cuda(StreamDestroy(stream));
    }

    void start() {
        if (started_) return;
        cuda(StreamCreateWithFlags(&stream, cudaStreamDefault));

        glCreateRenderbuffers(2, rb.data());
        // Depth buffer cannot be read in CUDA,
        // have to write fake depth buffer manually..
        glCreateRenderbuffers(2, depth_rb.data());
        glCreateRenderbuffers(2, depth_buf_rb.data());
        glCreateFramebuffers(2, fb.data());

        // Attach rbo to fbo
        for (int index = 0; index < 2; index++) {
            glNamedFramebufferRenderbuffer(fb[index], GL_COLOR_ATTACHMENT0,
                                           GL_RENDERBUFFER, rb[index]);
            glNamedFramebufferRenderbuffer(fb[index], GL_COLOR_ATTACHMENT1,
                                           GL_RENDERBUFFER, depth_rb[index]);
            glNamedFramebufferRenderbuffer(fb[index], GL_DEPTH_ATTACHMENT,
                                           GL_RENDERBUFFER,
                                           depth_buf_rb[index]);
            const GLenum attach_buffers[]{GL_COLOR_ATTACHMENT0,
                                          GL_COLOR_ATTACHMENT1};
            glNamedFramebufferDrawBuffers(fb[index], 2, attach_buffers);
        }
        started_ = true;
    }

    void render() {
        GLfloat clear_color[] = {options.background_brightness,
                                 options.background_brightness,
                                 options.background_brightness, 1.f};
        GLfloat depth_inf = 1e9, zero = 0;
        glClearDepth(1.f);
        glClearNamedFramebufferfv(fb[buf_index], GL_COLOR, 0, clear_color);
        glClearNamedFramebufferfv(fb[buf_index], GL_COLOR, 1, &depth_inf);
        glClearNamedFramebufferfv(fb[buf_index], GL_DEPTH, 0, &depth_inf);
        if (tree == nullptr || !started_) return;

        camera._update();

        glDepthMask(GL_TRUE);
        glBindFramebuffer(GL_FRAMEBUFFER, fb[buf_index]);
        for (const Mesh& mesh : meshes) {
            mesh.draw(camera.w2c, camera.K);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        cuda(GraphicsMapResources(2, &cgr[buf_index * 2], stream));
        launch_renderer(*tree, camera, options, ca[buf_index * 2],
                        ca[buf_index * 2 + 1], stream);
        cuda(GraphicsUnmapResources(2, &cgr[buf_index * 2], stream));

        glNamedFramebufferReadBuffer(fb[buf_index], GL_COLOR_ATTACHMENT0);
        glBlitNamedFramebuffer(fb[buf_index], 0, 0, 0, camera.width,
                               camera.height, 0, camera.height, camera.width, 0,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
        buf_index ^= 1;
    }

    void resize(const int width, const int height) {
        if (camera.width == width && camera.height == height) return;
        // save new size
        camera.width = width;
        camera.height = height;

        // unregister resource
        for (int index = 0; index < cgr.size(); index++) {
            if (cgr[index] != nullptr)
                cuda(GraphicsUnregisterResource(cgr[index]));
        }

        // resize color buffer
        for (int index = 0; index < 2; index++) {
            // resize rbo
            glNamedRenderbufferStorage(rb[index], GL_RGBA8, width, height);
            glNamedRenderbufferStorage(depth_rb[index], GL_R32F, width, height);
            glNamedRenderbufferStorage(depth_buf_rb[index],
                                       GL_DEPTH_COMPONENT32F, width, height);
            const GLenum attach_buffers[]{GL_COLOR_ATTACHMENT0,
                                          GL_COLOR_ATTACHMENT1};
            glNamedFramebufferDrawBuffers(fb[index], 2, attach_buffers);

            // register rbo
            cuda(GraphicsGLRegisterImage(
                &cgr[index * 2], rb[index], GL_RENDERBUFFER,
                cudaGraphicsRegisterFlagsSurfaceLoadStore |
                    cudaGraphicsRegisterFlagsWriteDiscard));
            cuda(GraphicsGLRegisterImage(
                &cgr[index * 2 + 1], depth_rb[index], GL_RENDERBUFFER,
                cudaGraphicsRegisterFlagsSurfaceLoadStore |
                    cudaGraphicsRegisterFlagsWriteDiscard));
        }

        cuda(GraphicsMapResources(cgr.size(), cgr.data(), 0));
        for (int index = 0; index < cgr.size(); index++) {
            cuda(GraphicsSubResourceGetMappedArray(&ca[index], cgr[index], 0,
                                                   0));
        }
        cuda(GraphicsUnmapResources(cgr.size(), cgr.data(), 0));
    }

    const N3Tree* tree = nullptr;

   private:
    Camera& camera;
    RenderOptions& options;
    int buf_index;

    // GL buffers
    std::array<GLuint, 2> fb, rb, depth_rb, depth_buf_rb;

    // CUDA resources
    std::array<cudaGraphicsResource_t, 4> cgr = {{0}};
    std::array<cudaArray_t, 4> ca;

    std::vector<Mesh>& meshes;
    cudaStream_t stream;
    bool started_ = false;
};

VolumeRenderer::VolumeRenderer()
    : impl_(std::make_unique<Impl>(camera, options, meshes)) {}

VolumeRenderer::~VolumeRenderer() {}

void VolumeRenderer::render() { impl_->render(); }
void VolumeRenderer::set(N3Tree& tree) {
    impl_->start();
    impl_->tree = &tree;
}
void VolumeRenderer::clear() { impl_->tree = nullptr; }

void VolumeRenderer::resize(int width, int height) {
    impl_->resize(width, height);
}
const char* VolumeRenderer::get_backend() { return "CUDA"; }

}  // namespace volrend
#endif
