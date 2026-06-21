#include "mt_gpu.h"

#include <atomic>
#include <thread>

#include <switch.h>

#include <EGL/egl.h>
#include <glad/glad.h>

namespace {

    const char *SH_FILL =
        "#version 430\n"
        "layout(std430, binding = 0) buffer srcBuffer { volatile uint src[]; };\n"
        "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;\n"
        "uniform uint var;\n"
        "void main() {\n"
        "    src[gl_GlobalInvocationID.x] = var + gl_GlobalInvocationID.x;\n"
        "}\n";

    const char *SH_VERIFY_FILL =
        "#version 430\n"
        "layout(std430, binding = 0) buffer srcBuffer { volatile uint src[]; };\n"
        "layout(std430, binding = 2) buffer resBuffer { uint res; };\n"
        "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;\n"
        "uniform uint var;\n"
        "void main() {\n"
        "    if (src[gl_GlobalInvocationID.x] != (var + gl_GlobalInvocationID.x)) {\n"
        "        res = 0x55555555u;\n"
        "    }\n"
        "}\n";

    const char *SH_COPY =
        "#version 430\n"
        "layout(std430, binding = 0) buffer srcBuffer { volatile uint src[]; };\n"
        "layout(std430, binding = 1) buffer dstBuffer { volatile uint dst[]; };\n"
        "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;\n"
        "void main() {\n"
        "    dst[gl_GlobalInvocationID.x] = src[gl_GlobalInvocationID.x];\n"
        "}\n";

    const char *SH_VERIFY_COPY =
        "#version 430\n"
        "layout(std430, binding = 0) buffer srcBuffer { volatile uint src[]; };\n"
        "layout(std430, binding = 1) buffer dstBuffer { volatile uint dst[]; };\n"
        "layout(std430, binding = 2) buffer resBuffer { uint res; };\n"
        "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;\n"
        "void main() {\n"
        "    if (src[gl_GlobalInvocationID.x] != dst[gl_GlobalInvocationID.x]) {\n"
        "        res = 0x55555555u;\n"
        "    }\n"
        "}\n";

    std::thread g_thread;
    std::atomic<bool> g_stop{ false };
    std::atomic<bool> g_running{ false };
    std::atomic<bool> g_error{ false };
    std::atomic<uint64_t> g_loop{ 0 };
    std::atomic<uint64_t> g_mismatches{ 0 };
    std::atomic<uint64_t> g_size_mb{ 0 };
    const char *volatile g_status = "Stopped";

    EGLDisplay s_dpy = EGL_NO_DISPLAY;
    EGLContext s_ctx = EGL_NO_CONTEXT;

    uint32_t s_rng_a = 0x12345, s_rng_b = 0x34211, s_rng_c = 0x57a3f, s_rng_d = 0x9e3779;

    uint32_t nextRandom() {
        s_rng_a = (s_rng_a & 0x3ffe) << 18 | (s_rng_a ^ s_rng_a << 6) >> 13;
        s_rng_b = (s_rng_b << 2 & 0xffffffe0) | (s_rng_b << 2 ^ s_rng_b) >> 27;
        s_rng_c = (s_rng_c & 0x1fffff0) << 7 | (s_rng_c ^ s_rng_c << 13) >> 21;
        s_rng_d = (s_rng_d & 0x7ff80) << 13 | (s_rng_d ^ s_rng_d << 3) >> 12;
        return s_rng_a ^ s_rng_b ^ s_rng_c ^ s_rng_d;
    }

    bool eglUp() {
        s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s_dpy == EGL_NO_DISPLAY)
            return false;
        if (!eglInitialize(s_dpy, nullptr, nullptr))
            return false;
        if (!eglBindAPI(EGL_OPENGL_API))
            return false;

        const EGLint cfgAttr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                   EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_NONE };
        EGLConfig cfg;
        EGLint n = 0;
        if (!eglChooseConfig(s_dpy, cfgAttr, &cfg, 1, &n) || n == 0)
            return false;

        const EGLint ctxAttr[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE };
        s_ctx = eglCreateContext(s_dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
        if (s_ctx == EGL_NO_CONTEXT)
            return false;

        if (eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, s_ctx) != EGL_TRUE)
            return false;
        return true;
    }

    void eglDown() {
        if (s_dpy) {
            eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (s_ctx)
                eglDestroyContext(s_dpy, s_ctx);
            eglTerminate(s_dpy);
        }
        s_ctx = EGL_NO_CONTEXT;
        s_dpy = EGL_NO_DISPLAY;
        eglReleaseThread();
    }

    GLuint buildProgram(const char *src) {
        GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glDeleteShader(sh);
            return 0;
        }
        GLuint prog = glCreateProgram();
        glAttachShader(prog, sh);
        glLinkProgram(prog);
        glDeleteShader(sh);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            glDeleteProgram(prog);
            return 0;
        }
        return prog;
    }

    void worker(bool full) {
        if (!eglUp()) {
            g_status = "EGL init failed";
            g_error = true;
            eglDown();
            g_running.store(false);
            return;
        }
        gladLoadGLLoader((GLADloadproc)eglGetProcAddress);

        GLint maxSsbo = 0;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxSsbo);
        if (maxSsbo <= 0) {
            g_status = "Failed to query SSBO size";
            g_error = true;
            eglDown();
            g_running.store(false);
            return;
        }

        size_t cap = full ? (size_t)0x20000000 : (size_t)0x8000000;
        size_t want = full ? ((size_t)maxSsbo << 2) : (size_t)maxSsbo;
        size_t bytes = (want < cap + 1) ? (want & ~(size_t)0x3ff) : cap;
        if (bytes < 0x400) {
            g_status = "GPU memtester buffer too small";
            g_error = true;
            eglDown();
            g_running.store(false);
            return;
        }

        g_size_mb.store((uint64_t)(bytes >> 20));
        GLuint groups = (GLuint)(bytes >> 10);

        GLuint fill = buildProgram(SH_FILL);
        GLuint verifyFill = buildProgram(SH_VERIFY_FILL);
        GLuint copy = buildProgram(SH_COPY);
        GLuint verifyCopy = buildProgram(SH_VERIFY_COPY);
        if (!fill || !verifyFill || !copy || !verifyCopy) {
            g_status = "Compute program build failed";
            g_error = true;
            if (fill) glDeleteProgram(fill);
            if (verifyFill) glDeleteProgram(verifyFill);
            if (copy) glDeleteProgram(copy);
            if (verifyCopy) glDeleteProgram(verifyCopy);
            eglDown();
            g_running.store(false);
            return;
        }

        GLint fillVarLoc = glGetUniformLocation(fill, "var");
        GLint verifyVarLoc = glGetUniformLocation(verifyFill, "var");

        GLuint bufs[3] = { 0, 0, 0 };
        glGenBuffers(3, bufs);
        const GLuint resInit = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[1]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), &resInit, GL_DYNAMIC_COPY);

        if (glGetError() != GL_NO_ERROR) {
            g_status = "Failed to allocate GPU memtester buffers";
            g_error = true;
            glDeleteBuffers(3, bufs);
            glDeleteProgram(fill);
            glDeleteProgram(verifyFill);
            glDeleteProgram(copy);
            glDeleteProgram(verifyCopy);
            eglDown();
            g_running.store(false);
            return;
        }

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufs[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bufs[1]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bufs[2]);

        g_status = "Running";

        while (!g_stop.load()) {
            uint32_t var = nextRandom();

            glUseProgram(fill);
            if (fillVarLoc >= 0)
                glUniform1ui(fillVarLoc, var);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            if (glGetError() != GL_NO_ERROR) {
                g_status = "GPU error during GPU memtester";
                g_error = true;
                break;
            }

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &resInit);
            glUseProgram(verifyFill);
            if (verifyVarLoc >= 0)
                glUniform1ui(verifyVarLoc, var);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            {
                GLuint res = 0;
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
                glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &res);
                if (res == 0x55555555u)
                    g_mismatches.fetch_add(1);
            }
            if (g_stop.load())
                break;

            glUseProgram(copy);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            if (glGetError() != GL_NO_ERROR) {
                g_status = "GPU error during GPU memtester";
                g_error = true;
                break;
            }

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &resInit);
            glUseProgram(verifyCopy);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            {
                GLuint res = 0;
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[2]);
                glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &res);
                if (res == 0x55555555u)
                    g_mismatches.fetch_add(1);
            }

            g_loop.fetch_add(1);
        }

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
        glUseProgram(0);
        glDeleteBuffers(3, bufs);
        glDeleteProgram(copy);
        glDeleteProgram(verifyCopy);
        glDeleteProgram(verifyFill);
        glDeleteProgram(fill);
        eglDown();

        if (!g_error.load())
            g_status = "Stopped";
        g_running.store(false);
    }

}  // namespace

extern "C" void mt_gpu_start(int full) {
    if (g_running.load())
        return;
    if (g_thread.joinable())
        g_thread.join();
    g_stop.store(false);
    g_error.store(false);
    g_loop.store(0);
    g_mismatches.store(0);
    g_size_mb.store(0);
    g_status = "Preparing GPU memtester...";
    g_running.store(true);
    appletSetAutoSleepDisabled(true);
    g_thread = std::thread(worker, full != 0);
}

extern "C" void mt_gpu_stop(void) {
    g_stop.store(true);
    if (g_thread.joinable())
        g_thread.join();
    g_running.store(false);
    appletSetAutoSleepDisabled(false);
}

extern "C" int mt_gpu_running(void) {
    return g_running.load() ? 1 : 0;
}

extern "C" void mt_gpu_get(mt_gpu_status_t *out) {
    if (!out)
        return;
    out->loop = g_loop.load();
    out->mismatches = g_mismatches.load();
    out->size_mb = g_size_mb.load();
    out->running = g_running.load() ? 1 : 0;
    out->error = g_error.load() ? 1 : 0;
    out->status = g_status;
}
