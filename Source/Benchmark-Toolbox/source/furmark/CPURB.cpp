#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <thread>
#include <vector>

#include "sates.h"
#include "vec23.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <arm_neon.h>
#include <condition_variable>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#ifndef ENABLE_NXLINK
    #define TRACE(fmt, ...) ((void)0)
#else
    #include <unistd.h>
    #define TRACE(fmt, ...) printf("%s: " fmt "\n", __PRETTY_FUNCTION__, ##__VA_ARGS__)

static int s_nxlinkSock = -1;

static void initNxLink() {
    if (R_FAILED(socketInitializeDefault()))
        return;

    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock >= 0)
        TRACE("printf output now goes to nxlink server");
    else
        socketExit();
}

static void deinitNxLink() {
    if (s_nxlinkSock >= 0) {
        close(s_nxlinkSock);
        socketExit();
        s_nxlinkSock = -1;
    }
}

extern "C" void userAppInit() {
    initNxLink();
}

extern "C" void userAppExit() {
    deinitNxLink();
}

#endif

static EGLDisplay s_display;
static EGLContext s_context;
static EGLSurface s_surface;

static bool initEgl(NWindow *win) {

    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!s_display) {
        TRACE("Could not connect to display! error: %d", eglGetError());
        goto _fail0;
    }

    eglInitialize(s_display, nullptr, nullptr);

    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
        TRACE("Could not set API! error: %d", eglGetError());
        goto _fail1;
    }

    EGLConfig config;
    EGLint numConfigs;
    static const EGLint framebufferAttributeList[] = { EGL_RENDERABLE_TYPE,
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
                                                       EGL_NONE };
    eglChooseConfig(s_display, framebufferAttributeList, &config, 1, &numConfigs);
    if (numConfigs == 0) {
        TRACE("No config found! error: %d", eglGetError());
        goto _fail1;
    }

    s_surface = eglCreateWindowSurface(s_display, config, win, nullptr);
    if (!s_surface) {
        TRACE("Surface creation failed! error: %d", eglGetError());
        goto _fail1;
    }

    static const EGLint contextAttributeList[] = { EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                                                   EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                                   EGL_CONTEXT_MAJOR_VERSION_KHR,
                                                   4,
                                                   EGL_CONTEXT_MINOR_VERSION_KHR,
                                                   3,
                                                   EGL_NONE };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttributeList);
    if (!s_context) {
        TRACE("Context creation failed! error: %d", eglGetError());
        goto _fail2;
    }

    eglMakeCurrent(s_display, s_surface, s_surface, s_context);
    return true;

_fail2:
    eglDestroySurface(s_display, s_surface);
    s_surface = nullptr;
_fail1:
    eglTerminate(s_display);
    s_display = nullptr;
_fail0:
    return false;
}

static void deinitEgl() {
    if (s_display) {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context) {
            eglDestroyContext(s_display, s_context);
            s_context = nullptr;
        }
        if (s_surface) {
            eglDestroySurface(s_display, s_surface);
            s_surface = nullptr;
        }
        eglTerminate(s_display);
        s_display = nullptr;
    }
}

static const char *const text_vs = R"text(
    #version 330 core
    layout(location=0) in vec2 inPos;
    layout(location=1) in vec3 inColor;
    out vec3 color;
    void main() {
        color = inColor;
        gl_Position = vec4(inPos, 0.0, 1.0);
    }
)text";

static const char *const text_fs = R"text(
    #version 330 core
    in vec3 color;
    out vec4 fragColor;
    void main() {
        fragColor = vec4(color, 1.0);
    }
)text";

static GLuint s_textProgram = 0;
static GLuint s_textVao = 0;
static GLuint s_textVbo = 0;

static float *s_textVertexData = nullptr;
static const int TEXT_BUFFER_FLOATS = 100 * 64 * 6 * 5;

static const unsigned char font8x8[11][8] = { { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00 }, { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00 },
                                              { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00 }, { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00 },
                                              { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00 }, { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00 },
                                              { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00 }, { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00 },
                                              { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00 }, { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00 },
                                              { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 } };

static GLuint createAndCompileShader(GLenum type, const char *source);

static void initTextRenderer() {
    GLuint vsh = createAndCompileShader(GL_VERTEX_SHADER, text_vs);
    GLuint fsh = createAndCompileShader(GL_FRAGMENT_SHADER, text_fs);

    s_textProgram = glCreateProgram();
    glAttachShader(s_textProgram, vsh);
    glAttachShader(s_textProgram, fsh);
    glLinkProgram(s_textProgram);
    glDeleteShader(vsh);
    glDeleteShader(fsh);

    glGenVertexArrays(1, &s_textVao);
    glGenBuffers(1, &s_textVbo);

    s_textVertexData = (float *)aligned_alloc(64, TEXT_BUFFER_FLOATS * sizeof(float));
}

static void drawTextPixel(float x, float y, float size, float r, float g, float b, float *vertexData, int *offset) {
    float verts[] = { x, y, r, g, b, x + size, y,        r, g, b, x + size, y + size, r, g, b,
                      x, y, r, g, b, x + size, y + size, r, g, b, x,        y + size, r, g, b };
    memcpy(&vertexData[*offset], verts, sizeof(verts));
    *offset += 30;
}

static void drawChar(char c, float x, float y, float scale, float r, float g, float b, float *vertexData, int *offset) {
    int idx = -1;
    if (c >= '0' && c <= '9')
        idx = c - '0';
    else if (c == '.')
        idx = 10;
    else
        return;

    const unsigned char *glyph = font8x8[idx];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << col)) {
                float px = x + col * scale;
                float py = y - row * scale;
                drawTextPixel(px, py, scale, r, g, b, vertexData, offset);
            }
        }
    }
}

static void drawText(const char *text, float x, float y, float scale, float r, float g, float b) {
    float *vertexData = s_textVertexData;
    int offset = 0;
    float cx = x;

    while (*text) {
        drawChar(*text, cx, y, scale, r, g, b, vertexData, &offset);
        cx += 8 * scale;
        text++;
    }

    if (offset > 0) {
        glUseProgram(s_textProgram);
        glBindVertexArray(s_textVao);
        glBindBuffer(GL_ARRAY_BUFFER, s_textVbo);
        glBufferData(GL_ARRAY_BUFFER, offset * sizeof(float), vertexData, GL_DYNAMIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLES, 0, offset / 5);
    }
}

static void cleanupTextRenderer() {
    if (s_textVbo) {
        glDeleteBuffers(1, &s_textVbo);
        s_textVbo = 0;
    }
    if (s_textVao) {
        glDeleteVertexArrays(1, &s_textVao);
        s_textVao = 0;
    }
    if (s_textProgram) {
        glDeleteProgram(s_textProgram);
        s_textProgram = 0;
    }
    if (s_textVertexData) {
        free(s_textVertexData);
        s_textVertexData = nullptr;
    }
}

static void setMesaConfig() {

    setenv("MESA_NO_ERROR", "1", 1);
}

static u64 s_startTicks = 0;
static u64 s_lastFrameTime = 0;
static float s_fps = 0.0f;
static int s_frameCount = 0;
static u64 s_fpsUpdateTime = 0;

static int frame = 0;

static const char *const rt_vs = R"text(
    #version 330 core
    out vec2 uv;

    void main()
    {
        vec2 pos = vec2(
            (gl_VertexID == 2) ? 3.0 : -1.0,
            (gl_VertexID == 1) ? 3.0 : -1.0
        );

        uv = 0.5 * (pos + 1.0);
        gl_Position = vec4(pos, 0.0, 1.0);
    }
)text";

static const char *const rt_fs = R"text(
#version 330 core

in vec2 uv;
out vec4 fragColor;

uniform sampler2D screenTex;

void main(){
    fragColor = texture(screenTex, uv);
}
)text";

static GLuint s_program;
static GLuint s_vao, s_vbo;

static GLint resolutionLoc;

static GLint loc_mdlvMtx, loc_projMtx;
static GLint loc_time;

static GLuint tex[2], fbo[2];

static GLuint createAndCompileShader(GLenum type, const char *source) {
    GLint success;
    GLchar msg[512];

    GLuint handle = glCreateShader(type);
    if (!handle) {
        TRACE("%u: cannot create shader", type);
        return 0;
    }
    glShaderSource(handle, 1, &source, nullptr);
    glCompileShader(handle);
    glGetShaderiv(handle, GL_COMPILE_STATUS, &success);

    if (!success) {
        glGetShaderInfoLog(handle, sizeof(msg), nullptr, msg);
        TRACE("%u: %s\n", type, msg);
        glDeleteShader(handle);
        return 0;
    }

    return handle;
}

static int width = 1280;
static int height = 720;

static int RW = 640;
static int RH = 360;

static GLuint screenTex;

// static float u_time;

static const int THREAD_COUNT = 2;
alignas(64) static std::atomic<int> nextTile(0);

static std::thread workers[THREAD_COUNT];
alignas(64) static std::atomic<bool> running(true);
alignas(64) static std::atomic<int> tilesDone(0);

static std::mutex workMutex;
static std::condition_variable workCV;

alignas(64) static bool workReady = false;
alignas(64) static int currentFrame = 0;

alignas(64) static std::atomic<bool> cpuRenderRunning(false);

enum Material { WHITE, RED, GREEN, LIGHT, MIRROR };

struct alignas(16) PixelData {
    float r, g, b, a;
};

static const int FB_COUNT = 4;
PixelData *frameBuffers[FB_COUNT];
alignas(64) static int currentRenderBuf = 0;
alignas(64) static float currentScale = 1.0f;

static inline void intersectScene4(const vec3x4 &ro, const vec3x4 &rd, float32x4_t &t, uint32x4_t &mat, vec3x4 &normal) {
    const float32x4_t INF = vdupq_n_f32(1e30f);
    const float32x4_t ZERO = vdupq_n_f32(0.0f);
    const float32x4_t EPS = vdupq_n_f32(1e-4f);

    t = INF;
    mat = vdupq_n_u32(WHITE);
    normal.x = normal.y = normal.z = ZERO;

    {
        const float32x4_t cx = vdupq_n_f32(0.0f);
        const float32x4_t cy = vdupq_n_f32(1.0f);
        const float32x4_t cz = vdupq_n_f32(-0.5f);

        vec3x4 oc;
        oc.x = vsubq_f32(ro.x, cx);
        oc.y = vsubq_f32(ro.y, cy);
        oc.z = vsubq_f32(ro.z, cz);

        float32x4_t b = dot(oc, rd);
        float32x4_t c = vsubq_f32(dot(oc, oc), vdupq_n_f32(1.0f));
        float32x4_t h = vsubq_f32(vmulq_f32(b, b), c);

        uint32x4_t hasHit = vcgtq_f32(h, ZERO);
        float32x4_t sqH = vsqrtq_f32(vmaxq_f32(h, ZERO));

        float32x4_t t0 = vsubq_f32(vnegq_f32(b), sqH);
        float32x4_t t1 = vaddq_f32(vnegq_f32(b), sqH);

        uint32x4_t useNear = vcgtq_f32(t0, EPS);
        float32x4_t tS = vbslq_f32(useNear, t0, t1);

        uint32x4_t valid = vandq_u32(hasHit, vcgtq_f32(tS, EPS));
        uint32x4_t closer = vcltq_f32(tS, t);
        uint32x4_t mask = vandq_u32(valid, closer);

        vec3x4 hp;
        hp.x = vmlaq_f32(ro.x, rd.x, tS);
        hp.y = vmlaq_f32(ro.y, rd.y, tS);
        hp.z = vmlaq_f32(ro.z, rd.z, tS);

        vec3x4 n;
        n.x = vsubq_f32(hp.x, cx);
        n.y = vsubq_f32(hp.y, cy);
        n.z = vsubq_f32(hp.z, cz);
        n = normalizeFast(n);

        t = vbslq_f32(mask, tS, t);
        mat = vbslq_u32(mask, vdupq_n_u32(MIRROR), mat);
        normal.x = vbslq_f32(mask, n.x, normal.x);
        normal.y = vbslq_f32(mask, n.y, normal.y);
        normal.z = vbslq_f32(mask, n.z, normal.z);
    }

    auto testPlane = [&](float32x4_t roAxis, float32x4_t rdAxis, float target, float nx, float ny, float nz, uint32_t material) {
        float32x4_t denom = rdAxis;
        float32x4_t num = vsubq_f32(vdupq_n_f32(target), roAxis);
        float32x4_t tP = vmulq_f32(num, fastRecip(denom));

        uint32x4_t notParallel = vcgtq_f32(vabsq_f32(denom), EPS);
        uint32x4_t ahead = vcgtq_f32(tP, EPS);
        uint32x4_t closer = vcltq_f32(tP, t);
        uint32x4_t mask = vandq_u32(notParallel, vandq_u32(ahead, closer));

        t = vbslq_f32(mask, tP, t);
        mat = vbslq_u32(mask, vdupq_n_u32(material), mat);
        normal.x = vbslq_f32(mask, vdupq_n_f32(nx), normal.x);
        normal.y = vbslq_f32(mask, vdupq_n_f32(ny), normal.y);
        normal.z = vbslq_f32(mask, vdupq_n_f32(nz), normal.z);
    };

    testPlane(ro.y, rd.y, 0.0f, 0, 1, 0, WHITE);

    {
        float32x4_t denom = rd.y;
        float32x4_t num = vsubq_f32(vdupq_n_f32(4.0f), ro.y);
        float32x4_t tP = vmulq_f32(num, fastRecip(denom));

        uint32x4_t notParallel = vcgtq_f32(vabsq_f32(denom), EPS);
        uint32x4_t ahead = vcgtq_f32(tP, EPS);
        uint32x4_t closer = vcltq_f32(tP, t);
        uint32x4_t mask = vandq_u32(notParallel, vandq_u32(ahead, closer));

        float32x4_t hx = vmlaq_f32(ro.x, rd.x, tP);
        float32x4_t hz = vmlaq_f32(ro.z, rd.z, tP);
        uint32x4_t inX = vcltq_f32(vabsq_f32(hx), vdupq_n_f32(1.0f));
        uint32x4_t inZ = vcltq_f32(vabsq_f32(hz), vdupq_n_f32(1.0f));
        uint32x4_t isLightPanel = vandq_u32(inX, inZ);
        uint32x4_t ceilMat = vbslq_u32(isLightPanel, vdupq_n_u32(LIGHT), vdupq_n_u32(WHITE));

        t = vbslq_f32(mask, tP, t);
        mat = vbslq_u32(mask, ceilMat, mat);
        normal.x = vbslq_f32(mask, vdupq_n_f32(0.0f), normal.x);
        normal.y = vbslq_f32(mask, vdupq_n_f32(-1.0f), normal.y);
        normal.z = vbslq_f32(mask, vdupq_n_f32(0.0f), normal.z);
    }

    testPlane(ro.x, rd.x, -2.0f, +1, 0, 0, RED);

    testPlane(ro.x, rd.x, +2.0f, -1, 0, 0, GREEN);

    testPlane(ro.z, rd.z, +2.0f, 0, 0, -1, WHITE);
}

static inline void trace8(vec3x4 ro0, vec3x4 rd0, vec3x4 ro1, vec3x4 rd1, vec3f out0[4], vec3f out1[4], uint32x4_t rng0, uint32x4_t rng1) {
    vec3x4 color0, color1;
    color0.x = color0.y = color0.z = vdupq_n_f32(0.0f);
    color1.x = color1.y = color1.z = vdupq_n_f32(0.0f);

    vec3x4 tput0, tput1;
    tput0.x = tput0.y = tput0.z = vdupq_n_f32(1.0f);
    tput1.x = tput1.y = tput1.z = vdupq_n_f32(1.0f);

    uint32x4_t alive0 = vdupq_n_u32(0xFFFFFFFF);
    uint32x4_t alive1 = vdupq_n_u32(0xFFFFFFFF);

    const float32x4_t BIAS = vdupq_n_f32(0.001f);

    for (int bounce = 0; bounce < 3; bounce++) {
        if (vaddvq_u32(vorrq_u32(alive0, alive1)) == 0)
            break;

        float32x4_t t0, t1;
        uint32x4_t mat0, mat1;
        vec3x4 n0, n1;

        intersectScene4(ro0, rd0, t0, mat0, n0);
        intersectScene4(ro1, rd1, t1, mat1, n1);

        uint32x4_t hit0 = vcltq_f32(t0, vdupq_n_f32(1e29f));
        uint32x4_t hit1 = vcltq_f32(t1, vdupq_n_f32(1e29f));
        uint32x4_t miss0 = vandq_u32(alive0, vmvnq_u32(hit0));
        uint32x4_t miss1 = vandq_u32(alive1, vmvnq_u32(hit1));

        color0.x = vbslq_f32(miss0, vaddq_f32(color0.x, vmulq_f32(tput0.x, vdupq_n_f32(0.7f))), color0.x);
        color1.x = vbslq_f32(miss1, vaddq_f32(color1.x, vmulq_f32(tput1.x, vdupq_n_f32(0.7f))), color1.x);
        color0.y = vbslq_f32(miss0, vaddq_f32(color0.y, vmulq_f32(tput0.y, vdupq_n_f32(0.8f))), color0.y);
        color1.y = vbslq_f32(miss1, vaddq_f32(color1.y, vmulq_f32(tput1.y, vdupq_n_f32(0.8f))), color1.y);
        color0.z = vbslq_f32(miss0, vaddq_f32(color0.z, vmulq_f32(tput0.z, vdupq_n_f32(1.0f))), color0.z);
        color1.z = vbslq_f32(miss1, vaddq_f32(color1.z, vmulq_f32(tput1.z, vdupq_n_f32(1.0f))), color1.z);

        alive0 = vandq_u32(alive0, hit0);
        alive1 = vandq_u32(alive1, hit1);

        vec3x4 pos0, pos1;
        pos0.x = vmlaq_f32(ro0.x, rd0.x, t0);
        pos0.y = vmlaq_f32(ro0.y, rd0.y, t0);
        pos0.z = vmlaq_f32(ro0.z, rd0.z, t0);
        pos1.x = vmlaq_f32(ro1.x, rd1.x, t1);
        pos1.y = vmlaq_f32(ro1.y, rd1.y, t1);
        pos1.z = vmlaq_f32(ro1.z, rd1.z, t1);

        uint32x4_t isLight0 = vandq_u32(alive0, vceqq_u32(mat0, vdupq_n_u32(LIGHT)));
        uint32x4_t isLight1 = vandq_u32(alive1, vceqq_u32(mat1, vdupq_n_u32(LIGHT)));
        uint32x4_t isMirror0 = vandq_u32(alive0, vceqq_u32(mat0, vdupq_n_u32(MIRROR)));
        uint32x4_t isMirror1 = vandq_u32(alive1, vceqq_u32(mat1, vdupq_n_u32(MIRROR)));

        uint32x4_t notLM0 = vmvnq_u32(vorrq_u32(vceqq_u32(mat0, vdupq_n_u32(LIGHT)), vceqq_u32(mat0, vdupq_n_u32(MIRROR))));
        uint32x4_t notLM1 = vmvnq_u32(vorrq_u32(vceqq_u32(mat1, vdupq_n_u32(LIGHT)), vceqq_u32(mat1, vdupq_n_u32(MIRROR))));
        uint32x4_t isDiff0 = vandq_u32(alive0, notLM0);
        uint32x4_t isDiff1 = vandq_u32(alive1, notLM1);

        color0.x = vbslq_f32(isLight0, vaddq_f32(color0.x, vmulq_f32(tput0.x, vdupq_n_f32(6.0f))), color0.x);
        color1.x = vbslq_f32(isLight1, vaddq_f32(color1.x, vmulq_f32(tput1.x, vdupq_n_f32(6.0f))), color1.x);
        color0.y = vbslq_f32(isLight0, vaddq_f32(color0.y, vmulq_f32(tput0.y, vdupq_n_f32(6.0f))), color0.y);
        color1.y = vbslq_f32(isLight1, vaddq_f32(color1.y, vmulq_f32(tput1.y, vdupq_n_f32(6.0f))), color1.y);
        color0.z = vbslq_f32(isLight0, vaddq_f32(color0.z, vmulq_f32(tput0.z, vdupq_n_f32(6.0f))), color0.z);
        color1.z = vbslq_f32(isLight1, vaddq_f32(color1.z, vmulq_f32(tput1.z, vdupq_n_f32(6.0f))), color1.z);

        alive0 = vandq_u32(alive0, vmvnq_u32(isLight0));
        alive1 = vandq_u32(alive1, vmvnq_u32(isLight1));

        vec3x4 refl0 = reflect(rd0, n0);
        vec3x4 refl1 = reflect(rd1, n1);
        rd0.x = vbslq_f32(isMirror0, refl0.x, rd0.x);
        rd1.x = vbslq_f32(isMirror1, refl1.x, rd1.x);
        rd0.y = vbslq_f32(isMirror0, refl0.y, rd0.y);
        rd1.y = vbslq_f32(isMirror1, refl1.y, rd1.y);
        rd0.z = vbslq_f32(isMirror0, refl0.z, rd0.z);
        rd1.z = vbslq_f32(isMirror1, refl1.z, rd1.z);

        uint32x4_t isWhite0 = vceqq_u32(mat0, vdupq_n_u32(WHITE));
        uint32x4_t isWhite1 = vceqq_u32(mat1, vdupq_n_u32(WHITE));
        uint32x4_t isRed0 = vceqq_u32(mat0, vdupq_n_u32(RED));
        uint32x4_t isRed1 = vceqq_u32(mat1, vdupq_n_u32(RED));

        float32x4_t w = vdupq_n_f32(0.9f), hi = vdupq_n_f32(1.0f), lo = vdupq_n_f32(0.2f);
        float32x4_t aR0 = vbslq_f32(isWhite0, w, vbslq_f32(isRed0, hi, lo));
        float32x4_t aR1 = vbslq_f32(isWhite1, w, vbslq_f32(isRed1, hi, lo));
        float32x4_t aG0 = vbslq_f32(isWhite0, w, vbslq_f32(isRed0, lo, hi));
        float32x4_t aG1 = vbslq_f32(isWhite1, w, vbslq_f32(isRed1, lo, hi));
        float32x4_t aB0 = vbslq_f32(isWhite0, w, lo);
        float32x4_t aB1 = vbslq_f32(isWhite1, w, lo);

        tput0.x = vbslq_f32(isDiff0, vmulq_f32(tput0.x, aR0), tput0.x);
        tput1.x = vbslq_f32(isDiff1, vmulq_f32(tput1.x, aR1), tput1.x);
        tput0.y = vbslq_f32(isDiff0, vmulq_f32(tput0.y, aG0), tput0.y);
        tput1.y = vbslq_f32(isDiff1, vmulq_f32(tput1.y, aG1), tput1.y);
        tput0.z = vbslq_f32(isDiff0, vmulq_f32(tput0.z, aB0), tput0.z);
        tput1.z = vbslq_f32(isDiff1, vmulq_f32(tput1.z, aB1), tput1.z);

        vec3x4 bounce0 = cosineSampleHemisphere4(n0, rng0);
        vec3x4 bounce1 = cosineSampleHemisphere4(n1, rng1);

        rd0.x = vbslq_f32(isDiff0, bounce0.x, rd0.x);
        rd1.x = vbslq_f32(isDiff1, bounce1.x, rd1.x);
        rd0.y = vbslq_f32(isDiff0, bounce0.y, rd0.y);
        rd1.y = vbslq_f32(isDiff1, bounce1.y, rd1.y);
        rd0.z = vbslq_f32(isDiff0, bounce0.z, rd0.z);
        rd1.z = vbslq_f32(isDiff1, bounce1.z, rd1.z);

        ro0.x = vmlaq_f32(pos0.x, n0.x, BIAS);
        ro1.x = vmlaq_f32(pos1.x, n1.x, BIAS);
        ro0.y = vmlaq_f32(pos0.y, n0.y, BIAS);
        ro1.y = vmlaq_f32(pos1.y, n1.y, BIAS);
        ro0.z = vmlaq_f32(pos0.z, n0.z, BIAS);
        ro1.z = vmlaq_f32(pos1.z, n1.z, BIAS);
    }

    float cr0[4], cg0[4], cb0[4];
    float cr1[4], cg1[4], cb1[4];
    vst1q_f32(cr0, color0.x);
    vst1q_f32(cg0, color0.y);
    vst1q_f32(cb0, color0.z);
    vst1q_f32(cr1, color1.x);
    vst1q_f32(cg1, color1.y);
    vst1q_f32(cb1, color1.z);

    for (int i = 0; i < 4; i++) {
        out0[i] = { cr0[i], cg0[i], cb0[i] };
        out1[i] = { cr1[i], cg1[i], cb1[i] };
    }
}

static vec3f camForward;
static vec3f camRight;
static vec3f camUp;
static vec3f camPos;

static void pinThread(int core) {
    Handle thread = CUR_THREAD_HANDLE;
    svcSetThreadCoreMask(thread, core, 1ULL << core);
}

static void inline Iowa(PixelData *buf, int start, int end) {
    const float32x4_t add = vdupq_n_f32(0.001f);

    for (int i = start; i < end; i += 16) {
        __builtin_prefetch(&buf[i + 256], 0, 3);

        float *ptr = (float *)&buf[i];

        float32x4x4_t v0 = vld4q_f32(ptr + 0);
        float32x4x4_t v1 = vld4q_f32(ptr + 16);
        float32x4x4_t v2 = vld4q_f32(ptr + 32);
        float32x4x4_t v3 = vld4q_f32(ptr + 48);

        v0.val[0] = vaddq_f32(v0.val[0], add);
        v1.val[1] = vaddq_f32(v1.val[1], add);
        v2.val[2] = vaddq_f32(v2.val[2], add);
        v3.val[0] = vaddq_f32(v3.val[0], add);

        vst4q_f32(ptr + 0, v0);
        vst4q_f32(ptr + 16, v1);
        vst4q_f32(ptr + 32, v2);
        vst4q_f32(ptr + 48, v3);
    }
}

static void Disasterpiece(int threadID) {
    int total = width * height;

    int chunk = total / THREAD_COUNT;
    int start = threadID * chunk;
    int end = (threadID == THREAD_COUNT - 1) ? total : start + chunk;

    for (int b = 0; b < FB_COUNT; b++) {
        Iowa(frameBuffers[2], start, end);
        Iowa(frameBuffers[3], start, end);
    }
}

static int frameIndex = frame;

static void renderTile(int startY, int endY, int frameIndex) {

    const float32x4_t invW = vdupq_n_f32(1.0f / RW);
    const float32x4_t invH = vdupq_n_f32(1.0f / RH);
    const float aspect = float(RW) / float(RH);

    const float32x4_t half = vdupq_n_f32(0.5f);
    const float32x4_t two = vdupq_n_f32(2.0f);
    const float32x4_t one = vdupq_n_f32(1.0f);

    const uint32x4_t vA = vdupq_n_u32(1664525);
    const uint32x4_t vC = vdupq_n_u32(1013904223);

    float32x4_t vScale = vdupq_n_f32(currentScale);
    float32x4_t vOne = vdupq_n_f32(1.0f);

    for (int y = startY; y < endY; y++) {

        float32x4_t py = vdupq_n_f32(float(y));
        py = vaddq_f32(py, half);
        float32x4_t uvy = vmulq_f32(py, invH);
        float32x4_t sy = vsubq_f32(vmulq_f32(uvy, two), one);

        for (int x = 0; x <= RW - 8; x += 8) {
            int base0 = y * RW + x;
            int base1 = base0 + 4;

            float32x4_t px0 = { float(x) + 0.5f, float(x + 1) + 0.5f, float(x + 2) + 0.5f, float(x + 3) + 0.5f };
            float32x4_t px1 = { float(x + 4) + 0.5f, float(x + 5) + 0.5f, float(x + 6) + 0.5f, float(x + 7) + 0.5f };

            float32x4_t uvx0 = vmulq_f32(px0, invW);
            float32x4_t uvx1 = vmulq_f32(px1, invW);

            float32x4_t sx0 = vmulq_n_f32(vsubq_f32(vmulq_f32(uvx0, two), one), aspect);
            float32x4_t sx1 = vmulq_n_f32(vsubq_f32(vmulq_f32(uvx1, two), one), aspect);

            vec3x4 ro0, rd0, ro1, rd1;
            ro0.x = ro1.x = vdupq_n_f32(camPos.x);
            ro0.y = ro1.y = vdupq_n_f32(camPos.y);
            ro0.z = ro1.z = vdupq_n_f32(camPos.z);

            rd0.x = vaddq_f32(vdupq_n_f32(camForward.x), vaddq_f32(vmulq_n_f32(sx0, camRight.x), vmulq_n_f32(sy, camUp.x)));
            rd0.y = vaddq_f32(vdupq_n_f32(camForward.y), vaddq_f32(vmulq_n_f32(sx0, camRight.y), vmulq_n_f32(sy, camUp.y)));
            rd0.z = vaddq_f32(vdupq_n_f32(camForward.z), vaddq_f32(vmulq_n_f32(sx0, camRight.z), vmulq_n_f32(sy, camUp.z)));

            rd1.x = vaddq_f32(vdupq_n_f32(camForward.x), vaddq_f32(vmulq_n_f32(sx1, camRight.x), vmulq_n_f32(sy, camUp.x)));
            rd1.y = vaddq_f32(vdupq_n_f32(camForward.y), vaddq_f32(vmulq_n_f32(sx1, camRight.y), vmulq_n_f32(sy, camUp.y)));
            rd1.z = vaddq_f32(vdupq_n_f32(camForward.z), vaddq_f32(vmulq_n_f32(sx1, camRight.z), vmulq_n_f32(sy, camUp.z)));

            rd0 = normalizeFast(rd0);
            rd1 = normalizeFast(rd1);

            uint32_t baseIdx = (y * 9277 + currentFrame * 26699) | 1;
            uint32x4_t vBase = vdupq_n_u32(baseIdx);

            const uint32_t xOff0[4] = { (uint32_t)x * 1973, (uint32_t)(x + 1) * 1973, (uint32_t)(x + 2) * 1973, (uint32_t)(x + 3) * 1973 };
            const uint32_t xOff1[4] = { (uint32_t)(x + 4) * 1973, (uint32_t)(x + 5) * 1973, (uint32_t)(x + 6) * 1973, (uint32_t)(x + 7) * 1973 };

            uint32x4_t rng0 = vaddq_u32(vBase, vld1q_u32(xOff0));
            uint32x4_t rng1 = vaddq_u32(vBase, vld1q_u32(xOff1));

            rng0 = vmlaq_u32(vC, rng0, vA);
            rng1 = vmlaq_u32(vC, rng1, vA);

            vec3f col0[4], col1[4];
            trace8(ro0, rd0, ro1, rd1, col0, col1, rng0, rng1);

            float *fbPtr0 = (float *)&frameBuffers[0][base0];
            float *fbPtr1 = (float *)&frameBuffers[0][base1];

            float32x4x4_t vFB0 = vld4q_f32(fbPtr0);
            float32x4x4_t vFB1 = vld4q_f32(fbPtr1);

            float32x4_t vNewR0 = { col0[0].x, col0[1].x, col0[2].x, col0[3].x };
            float32x4_t vNewG0 = { col0[0].y, col0[1].y, col0[2].y, col0[3].y };
            float32x4_t vNewB0 = { col0[0].z, col0[1].z, col0[2].z, col0[3].z };
            float32x4_t vNewR1 = { col1[0].x, col1[1].x, col1[2].x, col1[3].x };
            float32x4_t vNewG1 = { col1[0].y, col1[1].y, col1[2].y, col1[3].y };
            float32x4_t vNewB1 = { col1[0].z, col1[1].z, col1[2].z, col1[3].z };

            vFB0.val[0] = vfmaq_f32(vFB0.val[0], vsubq_f32(vNewR0, vFB0.val[0]), vScale);
            vFB0.val[1] = vfmaq_f32(vFB0.val[1], vsubq_f32(vNewG0, vFB0.val[1]), vScale);
            vFB0.val[2] = vfmaq_f32(vFB0.val[2], vsubq_f32(vNewB0, vFB0.val[2]), vScale);
            vFB0.val[3] = vOne;

            vFB1.val[0] = vfmaq_f32(vFB1.val[0], vsubq_f32(vNewR1, vFB1.val[0]), vScale);
            vFB1.val[1] = vfmaq_f32(vFB1.val[1], vsubq_f32(vNewG1, vFB1.val[1]), vScale);
            vFB1.val[2] = vfmaq_f32(vFB1.val[2], vsubq_f32(vNewB1, vFB1.val[2]), vScale);
            vFB1.val[3] = vOne;

            vst4q_f32(fbPtr0, vFB0);
            vst4q_f32(fbPtr1, vFB1);
        }
    }
}

const int TILE_H = 12;

static void workerThread(int id) {

    int core = id;
    pinThread(core);

    constexpr int tnum = 14;

    while (running) {
        std::unique_lock<std::mutex> lock(workMutex);
        workCV.wait(lock, [] { return workReady || !running; });
        lock.unlock();

        if (!running)
            return;

        while (true) {
            int tileBase = nextTile.fetch_add(tnum, std::memory_order_relaxed);

            for (int t = 0; t < tnum; t++) {
                int tile = tileBase + t;
                int startY = tile * TILE_H;

                if (startY >= RH)
                    goto worker_done;

                int endY = std::min(startY + TILE_H, RH);
                endY = std::min(endY, RH);

                renderTile(startY, endY, currentFrame);
                Disasterpiece(id);
            }
        }
    worker_done:
        tilesDone.fetch_add(1, std::memory_order_release);
    }
}

void CPURBSceneinit() {
    pinThread(2);
    vec2f u_resolution(width, height);
    GLint vsh = createAndCompileShader(GL_VERTEX_SHADER, rt_vs);
    GLint fsh = createAndCompileShader(GL_FRAGMENT_SHADER, rt_fs);
    if (!vsh || !fsh) {
        TRACE("Shader compile failed — aborting");
        return;
    }

    s_program = glCreateProgram();
    glViewport(0, 0, 1280, 720);
    glAttachShader(s_program, vsh);
    glAttachShader(s_program, fsh);
    glBindFragDataLocation(s_program, 0, "fragColor");
    glLinkProgram(s_program);

    resolutionLoc = glGetUniformLocation(s_program, "u_resolution");
    loc_time = glGetUniformLocation(s_program, "u_time");

    GLint success;
    glGetProgramiv(s_program, GL_LINK_STATUS, &success);
    if (!success) {
        char buf[512];
        glGetProgramInfoLog(s_program, sizeof(buf), nullptr, buf);
        TRACE("Link error: %s", buf);
    }
    glDeleteShader(vsh);
    glDeleteShader(fsh);

    loc_mdlvMtx = glGetUniformLocation(s_program, "mdlvMtx");
    loc_projMtx = glGetUniformLocation(s_program, "projMtx");
    loc_time = glGetUniformLocation(s_program, "u_time");

    static float vertices[] = {
        -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
    };

    glGenTextures(2, tex);
    glGenFramebuffers(2, fbo);

    for (int i = 0; i < 2; i++) {

        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, RW, RH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);

    glBindVertexArray(s_vao);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glEnable(GL_FRAMEBUFFER_SRGB);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    glUseProgram(s_program);
    auto projMtx = glm::perspective(glm::radians(40.0f), 1280.0f / 720.0f, 0.01f, 500.0f);
    glUniformMatrix4fv(loc_projMtx, 1, GL_FALSE, glm::value_ptr(projMtx));

    s_startTicks = armGetSystemTick();

    camPos = vec3f(0, 2, -6);
    vec3f target = vec3f(0, 2, 0);

    camForward = normalize(target - camPos);
    camRight = normalize(cross(camForward, vec3f(0, 1, 0)));
    camUp = cross(camRight, camForward);

    s_lastFrameTime = s_startTicks;
    s_fpsUpdateTime = s_startTicks;
    s_frameCount = 0;

    initTextRenderer();

    nextTile = 0;
    running = true;
    cpuRenderRunning = false;
    tilesDone = 0;
    workReady = false;

    for (int i = 0; i < THREAD_COUNT; i++) {
        workers[i] = std::thread(workerThread, i);
    }

    int total = width * height;

    for (int i = 0; i < FB_COUNT; i++) {
        frameBuffers[i] = (PixelData *)aligned_alloc(64, total * sizeof(PixelData));
        memset(frameBuffers[i], 0, total * sizeof(PixelData));
    }

    glUniform1i(glGetUniformLocation(s_program, "screenTex"), 0);

    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, RW, RH, 0, GL_RGBA, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

// static void startCPURender() {
//     currentRenderBuf = 0;
//     currentScale = 1.0f / float(frame + 1);
//     tilesDone = 0;
//     currentFrame = frame;
//     nextTile = 0;
//     {
//         std::lock_guard<std::mutex> lock(workMutex);
//         workReady = true;
//     }
//     workCV.notify_all();
// }

// static void waitForRender() {
//     while (tilesDone.load(std::memory_order_acquire) < THREAD_COUNT)
//         __asm__("yield");
//     {
//         std::lock_guard<std::mutex> lock(workMutex);
//         workReady = false;
//     }
//     workCV.notify_all();
// }

float getTime4() {
    u64 elapsed = armGetSystemTick() - s_startTicks;
    return (elapsed * 625 / 12) / 2000000000.0;
}

void CPURBRender() {

    u64 currentTime = armGetSystemTick();
    s_frameCount++;
    u64 timeSinceUpdate = currentTime - s_fpsUpdateTime;
    float secondsSinceUpdate = (timeSinceUpdate * 625.0f / 12.0f) / 1000000000.0f;

    if (secondsSinceUpdate >= 0.01f) {
        s_fps = s_frameCount / secondsSinceUpdate;
        s_frameCount = 0;
        s_fpsUpdateTime = currentTime;
    }

    memcpy(frameBuffers[1], frameBuffers[0], RW * RH * sizeof(PixelData));

    currentRenderBuf = 0;
    currentScale = 1.0f / float(frame + 1);
    tilesDone = 0;
    currentFrame = frame;
    nextTile = 0;
    {
        std::lock_guard<std::mutex> lock(workMutex);
        workReady = true;
    }
    workCV.notify_all();

    while (true) {
        int tnum = 6;
        int tileBase = nextTile.fetch_add(tnum, std::memory_order_relaxed);

        for (int t = 0; t < tnum; t++) {
            int startY = (tileBase + t) * TILE_H;
            if (startY >= RH)
                goto main_done;
            renderTile(startY, std::min(startY + TILE_H, RH), currentFrame);
            Disasterpiece(0);
        }
    }
main_done:

    while (tilesDone.load(std::memory_order_acquire) < THREAD_COUNT)
        __asm__("yield");
    {
        std::lock_guard<std::mutex> lock(workMutex);
        workReady = false;
    }
    workCV.notify_all();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(s_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, RW, RH, GL_RGBA, GL_FLOAT, frameBuffers[1]);
    glBindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glUniform1f(loc_time, getTime4());
    glUniform2f(resolutionLoc, width, height);

    glBindVertexArray(0);
    char fpsText[32];
    snprintf(fpsText, sizeof(fpsText), "%.3f", s_fps);
    drawText(fpsText, -0.95f, 0.90f, 0.02f, 1.0f, 0.0f, 0.0f);
    char sampleText[64];
    snprintf(sampleText, sizeof(sampleText), "Samples: %d", frame);
    drawText(sampleText, -0.95f, 0.85f, 0.02f, 1.0f, 0.0f, 0.0f);

    eglSwapBuffers(s_display, s_surface);
    frame++;
}

void CPURBExit() {
    cpuRenderRunning = false;
    running = false;
    nextTile = 0;
    workCV.notify_all();

    for (int i = 0; i < THREAD_COUNT; i++) {
        if (workers[i].joinable())
            workers[i].join();
    }
    cleanupTextRenderer();
    glDeleteBuffers(1, &s_vbo);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteProgram(s_program);

    free(frameBuffers[0]);
    free(frameBuffers[1]);
    frameBuffers[0] = frameBuffers[1] = nullptr;

    free(frameBuffers[2]);
    free(frameBuffers[3]);
    frameBuffers[2] = frameBuffers[3] = nullptr;

    frame = 0;
}

int CPURBMain(int argc, char *argv[]) {

    setMesaConfig();

    if (!initEgl(nwindowGetDefault()))
        return EXIT_FAILURE;

    gladLoadGLLoader((GLADloadproc)eglGetProcAddress);

    CPURBSceneinit();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    while (appletMainLoop()) {

        padUpdate(&pad);
        u32 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_B) {
            CPURBExit();
            deinitEgl();
            state = STATE_MENU;
            return 0;
        }

        CPURBRender();
        eglSwapBuffers(s_display, s_surface);
    }

    CPURBExit();

    deinitEgl();
    return EXIT_SUCCESS;
}
