#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "gpu_bw.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>

static EGLDisplay s_display = EGL_NO_DISPLAY;
static EGLContext s_context = EGL_NO_CONTEXT;

static bool egl_init(void) {
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_display == EGL_NO_DISPLAY) {
        printf("EGL: no display\n");
        consoleUpdate(NULL);
        return false;
    }
    if (!eglInitialize(s_display, NULL, NULL)) {
        printf("EGL: initialize failed (0x%x)\n", eglGetError());
        consoleUpdate(NULL);
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        printf("EGL: bindAPI(OPENGL) failed (0x%x)\n", eglGetError());
        consoleUpdate(NULL);
        return false;
    }

    const char *exts = eglQueryString(s_display, EGL_EXTENSIONS);
    if (!exts || !strstr(exts, "EGL_KHR_surfaceless_context")) {
        printf("EGL: no surfaceless_context\n");
        consoleUpdate(NULL);
        return false;
    }

    static const EGLint cfg_attribs[] = {
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_DEPTH_SIZE,
        24,
        EGL_STENCIL_SIZE,
        8,
        EGL_NONE,
    };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(s_display, cfg_attribs, &cfg, 1, &n) || !n) {
        printf("EGL: chooseConfig failed n=%d (0x%x)\n", (int)n, eglGetError());
        consoleUpdate(NULL);
        return false;
    }

    static const EGLint ctx_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4, EGL_CONTEXT_MINOR_VERSION_KHR, 3, EGL_NONE,
    };
    s_context = eglCreateContext(s_display, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (s_context == EGL_NO_CONTEXT) {
        printf("EGL: createContext failed (0x%x)\n", eglGetError());
        consoleUpdate(NULL);
        return false;
    }

    if (eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, s_context) != EGL_TRUE) {
        printf("EGL: makeCurrent failed (0x%x)\n", eglGetError());
        consoleUpdate(NULL);
        return false;
    }
    return true;
}

static void egl_exit(void) {
    eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s_context != EGL_NO_CONTEXT) {
        eglDestroyContext(s_display, s_context);
        s_context = EGL_NO_CONTEXT;
    }
    if (s_display != EGL_NO_DISPLAY) {
        eglTerminate(s_display);
        s_display = EGL_NO_DISPLAY;
    }
    eglReleaseThread();
}

static GLuint compile_compute(const char *src, const char *name) {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[256] = { 0 };
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("Compute shader compile failed: %s\n", log[0] ? log : name);
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

static double run_pass(GLuint prog, GLuint ssbo_src, GLuint ssbo_dst, size_t buf_bytes, int loops) {
    GLuint groups = (GLuint)(buf_bytes >> 10);

    glUseProgram(prog);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_src);
    if (ssbo_dst)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo_dst);

    glDispatchCompute(groups, 1, 1);
    glFinish();
    if (glGetError() != GL_NO_ERROR)
        return 0.0;

    uint64_t t0, t1;
    asm volatile("mrs %0, cntpct_el0" : "=r"(t0));
    for (int i = 0; i < loops; i++)
        glDispatchCompute(groups, 1, 1);
    glFinish();
    asm volatile("mrs %0, cntpct_el0" : "=r"(t1));

    if (glGetError() != GL_NO_ERROR)
        return 0.0;

    double elapsed = (double)(t1 - t0) * 625.0 / 12.0 / 1000000000.0;
    if (elapsed <= 0.0)
        return 0.0;
    return ((double)buf_bytes * (double)loops) / elapsed / 1000000.0;
}

bool gpu_bw_run(bool is_4gb, double *copy_out, double *read_out, double *write_out) {

    static const char *src_copy = "\n#version 430\n"
                                  "layout(std430, binding = 0) buffer srcBuffer { volatile uint src[]; };\n"
                                  "layout(std430, binding = 1) buffer dstBuffer { volatile uint dst[]; };\n"
                                  "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;\n"
                                  "void main() {\n"
                                  "    dst[gl_GlobalInvocationID.x] = src[gl_GlobalInvocationID.x];\n"
                                  "}\n";

    static const char *src_read = "\n#version 430\n"
                                  "layout(std430, binding = 0) buffer srcBuffer { volatile uint src[]; };\n"
                                  "shared uint tmp;\n"
                                  "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;\n"
                                  "void main() {\n"
                                  "    tmp |= src[gl_GlobalInvocationID.x];\n"
                                  "}\n";

    static const char *src_write = "\n#version 430\n"
                                   "layout(std430, binding = 0) buffer srcBuffer { volatile uint src[]; };\n"
                                   "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;\n"
                                   "void main() {\n"
                                   "    src[gl_GlobalInvocationID.x] = 0xAAAAAAAAu;\n"
                                   "}\n";

    if (!egl_init()) {
        printf("Failed to initialize EGL/GL for GPU benchmark.\n");
        consoleUpdate(NULL);
        return false;
    }

    GLint max_ssbo = 0;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &max_ssbo);
    if (max_ssbo < 1) {
        printf("Failed to query GPU SSBO size.\n");
        consoleUpdate(NULL);
        egl_exit();
        return false;
    }

    size_t buf_bytes = (size_t)max_ssbo;
    if (buf_bytes > 0x8000000)
        buf_bytes = 0x8000000;
    if (!is_4gb)
        buf_bytes >>= 1;
    buf_bytes &= ~(size_t)0x3FF;

    if (buf_bytes < 0x400) {
        printf("GPU benchmark buffer is too small.\n");
        consoleUpdate(NULL);
        egl_exit();
        return false;
    }

    int loops = is_4gb ? 400 : 800;
    int wloops = loops / 10;

    GLuint prog_copy = compile_compute(src_copy, "GPU Copy");
    GLuint prog_read = compile_compute(src_read, "GPU Read");
    GLuint prog_write = compile_compute(src_write, "GPU Write");
    if (!prog_copy || !prog_read || !prog_write) {
        if (prog_copy)
            glDeleteProgram(prog_copy);
        if (prog_read)
            glDeleteProgram(prog_read);
        if (prog_write)
            glDeleteProgram(prog_write);
        egl_exit();
        return false;
    }

    GLuint ssbo[2] = { 0, 0 };
    glGenBuffers(2, ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)buf_bytes, NULL, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)buf_bytes, NULL, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    if (glGetError() != GL_NO_ERROR) {
        printf("Failed to allocate GPU buffers!\n");
        consoleUpdate(NULL);
        glDeleteBuffers(2, ssbo);
        glDeleteProgram(prog_copy);
        glDeleteProgram(prog_read);
        glDeleteProgram(prog_write);
        egl_exit();
        return false;
    }

    run_pass(prog_write, ssbo[0], 0, buf_bytes, wloops);
    run_pass(prog_read, ssbo[0], 0, buf_bytes, wloops);

    *copy_out = run_pass(prog_copy, ssbo[0], ssbo[1], buf_bytes, loops);
    *read_out = run_pass(prog_read, ssbo[0], 0, buf_bytes, loops);
    *write_out = run_pass(prog_write, ssbo[0], 0, buf_bytes, loops);

    glDeleteBuffers(2, ssbo);
    glDeleteProgram(prog_copy);
    glDeleteProgram(prog_read);
    glDeleteProgram(prog_write);
    egl_exit();
    return true;
}
