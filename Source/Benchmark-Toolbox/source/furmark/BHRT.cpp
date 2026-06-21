#include <assert.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <thread>
#include <vector>

#include <arm_neon.h>
#include <condition_variable>

#define GLM_FORCE_PURE
#include "colormap_png.h"
#include "sates.h"
#include "sk_png.h"
#include "stb_image.h"
#include "vec23.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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

    setenv("EGL_LOG_LEVEL", "debug", 1);
    setenv("MESA_VERBOSE", "all", 1);
}

static u64 s_startTicks = 0;
static u64 s_lastFrameTime = 0;
static float s_fps = 0.0f;
static int s_frameCount = 0;
static u64 s_fpsUpdateTime = 0;

// static int frame = 0;

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

// static GLint resolutionLoc;

// static GLint loc_mdlvMtx, loc_projMtx;
static GLint loc_time;

static GLuint createAndCompileShader(GLenum type, const char *source) {
    GLint success;
    GLchar msg[4096];

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

static int RenderX = 1280;
static int RenderY = 720;

// static int OutputX = 1280;
// static int OutputY = 720;

static const int THREAD_COUNT = 2;
static std::thread workers[THREAD_COUNT];
alignas(64) static std::atomic<bool> running(true);

static std::mutex workMutex;
static std::condition_variable workCV;

// alignas(64) static bool workReady = false;
// alignas(64) static int currentFrame = 0;

static void pinThread(int core) {
    Handle thread = CUR_THREAD_HANDLE;
    svcSetThreadCoreMask(thread, core, 1ULL << core);
}

static const char *const vertexShaderSource = R"text(
    #version 330 core

    layout(location = 0) in vec3 position;

    out vec2 uv;

    void main() {
    uv = (position.xy + 1.0) * 0.5;
    gl_Position = vec4(position, 1.0);
    }
)text";

static const char *const fragmentShaderSource = R"text(
    #version 330 core

    // Constants
    const float PI = 3.14159265359;
    const float EPSILON = 0.0001;
    const float INFINITY = 1000000.0;

    out vec4 fragColor;

    uniform vec2 res;
    uniform float time;
    // textures
    uniform samplerCube galaxy;
    uniform sampler2D colorMap;

    // Rendering params
    uniform float frontView = 0.0;
    uniform float topView = 0.0;
    uniform float cameraRoll = 0.0;

    // checkerboard
    uniform int frameIndex;
    uniform sampler3D noiseTex;

    // Precomp radial bands
    uniform sampler2D orbitalLUT;

    // h2 Interval
    // uniform int h2Interval;

    // Phys params
    uniform float fovScale = 1.0;

    // Accretion disk
    uniform float adiskHeight = 0.1;
    uniform float adiskLit = 15;
    uniform float adiskDensityV = 2.0;
    uniform float adiskDensityH = 1.5;
    uniform float adiskNoiseScale = 0.4;
    uniform float adiskNoiseLOD = 5.0;
    uniform float adiskSpeed = 0.12;
    uniform float orbitalScale;

    uniform sampler2D deflectionMap;
    uniform float photonScreenRadius;
    uniform sampler2D diskColorMap;

    struct Ring {
        vec3 center;
        vec3 normal;
        float innerRadius;
        float outerRadius;
        float rotateSpeed;
    };

    // Simplex 3D Noise
    // by Ian McEwan, Ashima Arts

    vec4 permute(vec4 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }
    vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

    float snoise(vec3 v) {
        const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
        const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

        // 1st corner
        vec3 i = floor(v + dot(v, C.yyy));
        vec3 x0 = v - i + dot(i, C.xxx);

        // Other corners
        vec3 g = step(x0.yzx, x0.xyz);
        vec3 l = 1.0 - g;
        vec3 i1 = min(g.xyz, l.zxy);
        vec3 i2 = max(g.xyz, l.zxy);

        // x0 = x0 - 0. + 0.0 * C
        vec3 x1 = x0 - i1 + 1.0 * C.xxx;
        vec3 x2 = x0 - i2 + 2.0 * C.xxx;
        vec3 x3 = x0 - 1. + 3.0 * C.xxx;

        // Permutations
        i = mod(i, 289.0);
        vec4 p = permute(permute(permute(i.z + vec4(0.0, i1.z, i2.z, 1.0)) + i.y + vec4(0.0, i1.y, i2.y, 1.0)) + i.x + vec4(0.0, i1.x, i2.x, 1.0));

        // Gradients
        // N * N points uniformly over a square, mapped to an octahedron
        float n_ = 1.0 / 7.0; // N=7
        vec3 ns = n_ * D.wyz - D.xzx;

        vec4 j = p - 49.0 * floor(p * ns.z * ns.z); //  mod(p,N*N)

        vec4 x_ = floor(j * ns.z);
        vec4 y_ = floor(j - 7.0 * x_); // mod(j,N)

        vec4 x = x_ * ns.x + ns.yyyy;
        vec4 y = y_ * ns.x + ns.yyyy;
        vec4 h = 1.0 - abs(x) - abs(y);

        vec4 b0 = vec4(x.xy, y.xy);
        vec4 b1 = vec4(x.zw, y.zw);

        vec4 s0 = floor(b0) * 2.0 + 1.0;
        vec4 s1 = floor(b1) * 2.0 + 1.0;
        vec4 sh = -step(h, vec4(0.0));

        vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
        vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

        vec3 p0 = vec3(a0.xy, h.x);
        vec3 p1 = vec3(a0.zw, h.y);
        vec3 p2 = vec3(a1.xy, h.z);
        vec3 p3 = vec3(a1.zw, h.w);

        // Normalize Gradients
        vec4 norm =
            taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
        p0 *= norm.x;
        p1 *= norm.y;
        p2 *= norm.z;
        p3 *= norm.w;

        // Mix final noise val
        vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
        m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
    }

    // Ring computes

    float ringDistance(vec3 rayOrigin, vec3 rayDir, Ring ring){
        float denominator = dot(rayDir, ring.normal);
        float constant = -dot(ring.center, ring.normal);
        if (abs(denominator) < EPSILON) {
            return -1.0;
        } else {
            float t = -(dot(rayOrigin, ring.normal) + constant) / denominator;
            if (t < 0.0) {
                return -1.0;
            }

            vec3 intersection = rayOrigin + t * rayDir;

            // Comp distance to ring center
            float d = length(intersection - ring.center);
            if (d >= ring.innerRadius && d <= ring.outerRadius) {
                return t;
            }
            return -1.0;
        }
    }

    vec3 panoramaColor (sampler2D tex, vec3 dir) {
        vec2 uv = vec2(0.5 - atan(dir.z, dir.x) / PI * 0.5, 0.5 - asin(dir.y) / PI);
        return texture2D(tex, uv).rgb;
    }

    vec3 accel(float h2, vec3 pos) {
        float r2 = dot(pos, pos);
        float r4 = r2 * r2;
        float r2_5 = r4 * sqrt(r2);
        vec3 acc = -1.5 * h2 * pos / r2_5;
        return acc;
    }

    vec4 quadFromAxisAngle(vec3 axis, float angle) {
        vec4 qr;
        float half_angle = (angle * 0.5) * 3.14159 / 180.0;
        qr.x = axis.x * sin(half_angle);
        qr.y = axis.y * sin(half_angle);
        qr.z = axis.z * sin(half_angle);
        qr.w = cos(half_angle);
        return qr;
    }

    vec4 quadConj(vec4 q) { return vec4(-q.x, -q.y, -q.z, q.w); }

    vec4 quat_mult(vec4 q1, vec4 q2) {
        vec4 qr;
        qr.x = (q1.w * q2.x) + (q1.x * q2.w) + (q1.y * q2.z) - (q1.z * q2.y);
        qr.y = (q1.w * q2.y) - (q1.x * q2.z) + (q1.y * q2.w) + (q1.z * q2.x);
        qr.z = (q1.w * q2.z) + (q1.x * q2.y) - (q1.y * q2.x) + (q1.z * q2.w);
        qr.w = (q1.w * q2.w) - (q1.x * q2.x) - (q1.y * q2.y) - (q1.z * q2.z);
        return qr;
    }

    vec3 rotateVector(vec3 position, vec3 axis, float angle) {
        vec4 qr = quadFromAxisAngle(axis, angle);
        vec4 qr_conj = quadConj(qr);
        vec4 q_pos = vec4(position.x, position.y, position.z, 0);

        vec4 q_tmp = quat_mult(qr, q_pos);
        qr = quat_mult(q_tmp, qr_conj);

        return vec3(qr.x, qr.y, qr.z);
    }

    #define IN_RANGE(x, a, b) (((x) > (a)) && ((x) < (b)))

    void cartesianToSpherical(in vec3 xyz, out float rho, out float phi, out float theta) {
        rho = sqrt((xyz.x * xyz.x) + (xyz.y * xyz.y) + (xyz.z * xyz.z));
        phi = asin(xyz.y / rho);
        theta = atan(xyz.z, xyz.x);
    }

    // Cartesian -> Spherical coord
    vec3 toSpherical(vec3 p) {
        float rho = sqrt((p.x * p.x) + (p.y * p.y) + (p.z * p.z));
        float theta = atan(p.z, p.x);
        float phi = asin(p.y / rho);
        return vec3(rho, theta, phi);
    }

    vec3 toSpherical2(vec3 pos) {
        vec3 radialCoords;
        radialCoords.x = length(pos) * 1.5 + 0.55;
        radialCoords.y = atan(-pos.x, -pos.z) * 1.5;
        radialCoords.z = abs(pos.y);
        return radialCoords;
    }

    //  ring color
    void ringColor(vec3 rayOrigin, vec3 rayDir, Ring ring, inout float minDistance, inout vec3 color) {
        float distance = ringDistance(rayOrigin, normalize(rayDir), ring);
        if (distance >= EPSILON && distance < minDistance && distance <= length(rayDir) + EPSILON) {
                minDistance = distance;

                vec3 intersection = rayOrigin + normalize(rayDir) * minDistance;
                vec3 ringColor;

            {
                float dist = length(intersection);

                float v = clamp((dist - ring.innerRadius) / (ring.outerRadius - ring.innerRadius), 0.0, 1.0);

                vec3 base = cross(ring.normal, vec3(0.0, 0.0, 1.0));
                float angle = acos(dot(normalize(base), normalize(intersection)));

                if (dot(cross(base, intersection), ring.normal) < 0.0)
                    angle = -angle;

                float u = 0.5 - 0.5 * angle / PI;
                //.Hack
                u += time * ring.rotateSpeed;

                vec3 color = vec3(0.0, 0.5, 0.0);
                //.Hack GU
                float alpha = 0.5;
                ringColor = vec3(color);
            }
            color += ringColor;
        }
    }

    mat3 lookAt(vec3 origin, vec3 target, float roll){
        vec3 rr = vec3(sin(roll), cos(roll), 0.0);
        vec3 ww = normalize(target - origin);
        vec3 uu = normalize(cross(ww, rr));
        vec3 vv = normalize(cross(uu, ww));

        return mat3(uu, vv, ww);
    }

    float sqrLength(vec3 a) { return dot(a, a); }

    // Accretion Disc
    // for laughs, I tried running this bullshit on my PC with a 5060, at 1080p this thing was dying
    // I am terrified at how its going to run on NX
    void adiskColor(vec3 pos, inout vec3 color, inout float alpha, float stepSize, vec3 dir) {
        float innerRadius = 2.6;
        float outerRadius = 12.0;

        //Accertion disks increase in density the closer we get to the event horizon
        float thinDiskHeight = 0.18;
        float density = max(0.0, 1.0 - length(pos.xyz / vec3(outerRadius, thinDiskHeight, outerRadius)));
        if (density < 0.001) {
            return;
        }

        // Set particles to 0 once we go past the innermost circular orbit
        density *= pow(1.0 - abs(pos.y) / thinDiskHeight, 6.0);
        density *= smoothstep(innerRadius, innerRadius + 0.4, length(pos.xz));

        // Dont compute if the density is tiny
        if (density < 0.003) {
            return;
        }

        float r = length(pos.xz);

        // hell
        float theta = atan(pos.z, pos.x);
        float radialU = clamp((r - innerRadius) / (outerRadius - innerRadius), 0.0, 1.0);
        vec2  cs = textureLod(orbitalLUT, vec2(radialU, 0.5), 0.0).rg;

        float cosT = cos(theta);
        float sinT = sin(theta);
        float cosA = cosT * cs.x - sinT * cs.y;
        float sinA = sinT * cs.x + cosT * cs.y;

        // Rotate the actual local XZ space smoothly over time
        vec3 rotatedPos = pos;
        rotatedPos.x = pos.x * cosA - pos.z * sinA;
        rotatedPos.z = pos.x * sinA + pos.z * cosA;

        // Noise texture parameters
        // noise size, higher = finer grain
        float noise = 0.0;
        float amp = 1.0;
        float scale = 0.35;
        float totalAmp = 0.0;

        // Stretch factors across respective axis
        float stretchX = 1.0;
        float stretchY = 2.0;
        float stretchZ = 0.2;

        // Frequency mul per octave, higher = finer detail
        float lacunarity = 1.8;
        // Brightness drop per octave, higher = sharper micro grain
        float gain = 0.45;

        #define NOISE_LOD 4
        for (int i = 0; i < NOISE_LOD; i++) {
            // Use positions along flow direction to map correctly
            vec3 sampleCoords = vec3(rotatedPos.x * stretchX, rotatedPos.y * stretchY, rotatedPos.z * stretchZ);

            float n = textureLod(noiseTex, sampleCoords, 0.0).r * 2.0 - 1.0;
            noise += n * amp;
            totalAmp += amp;
            scale *= lacunarity;
            amp *= gain;
        }
        #undef NOISE_LOD
        // Normalize to [0,1]
        noise = clamp(noise / totalAmp * 0.5 + 0.5, 0.0, 1.0);

        // Add dopler shifiting
        // Basically, in a black hole the accretion disk is spinning so rapidly that it significantly changes light
        // The side approaching the obvserver will be blueshifted, while the opposing side will be redshifted

        // Disc rotation
        vec3 orbitalVelDir = normalize(vec3(-pos.z, 0.0, pos.x));

        // Vel approximation
        float speed = clamp(0.65 / sqrt(max(r, 2.0)), 0.0, 0.6);
        vec3 velocityVector = orbitalVelDir * speed;

        // Dot product between orbital velocity and C towards camera
        float cosTheta = dot(velocityVector, -normalize(dir));

        // Lorentz factor (time dialation)
        // Stay with me, as we cross the empty skies
        float gamma = 1.0 / sqrt(1.0 - speed * speed);

        // Come sail with me
        float doppler = 1.0 / (gamma * (1.0 - cosTheta * speed));

        // Time, Shift
        // We discover the entry
        // To other planes
        // accretion disk colors, based on their distance
        // Apply whitening with blue shift
        vec3 innerColor = vec3(1.3, 1.1, 0.9) * pow(doppler, 1.5);
        vec3 midColor = vec3(1.0, 0.4, 0.05);
        vec3 outerColor = vec3(0.4, 0.02, 0.0);

        // Stay with me
        // Come sail with me
        float radialT = clamp((r - innerRadius) / (outerRadius - innerRadius), 0.0, 1.0);
        vec3 dustColor = (radialT < 0.2) ? mix(innerColor, midColor, radialT / 0.2)
        : mix(midColor, outerColor, (radialT - 0.2) / 0.8);

        // We fly in dreams
        // As we cross the space and time
        // Just stay with me
        // Scaling factor for relativistic beaming (scales @3.5/4^)
        float beaming = pow(doppler, 4.0);

        // Scale colors and attenuate alpha for volumetric depth
        // Controls disk opacity
        float absorption = 1.2;
        float sampleAlpha = clamp(density * noise * stepSize * absorption * beaming, 0.0, 1.0);

        color += dustColor * adiskLit * sampleAlpha * alpha * beaming;

        // Attenuate alpha for realistic occlusion
        alpha *= (1.0 - sampleAlpha);
    }

    float hash2d(vec2 co) {
        return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
    }

    // Nebula/Skybox gen
    // Reads from the actual galaxy texture first, then creates stars with varying intensities
    float _starHash(vec2 p){
        return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
    }

    vec3 sampleSky(vec3 dir){
        vec3 sky = textureLod(galaxy, dir, 0.0).rgb;

        float phi = atan(dir.z, dir.x) * (1.0 / (2.0 * PI)) + 0.5;
        float theta = asin(clamp(dir.y, -1.0, 1.0)) * (1.0 / PI) + 0.5;
        vec2 uv = vec2(phi, theta);

        // Star field
        vec2 grid  = uv * vec2(600.0, 300.0);
        vec2 cell  = floor(grid);
        vec2 local = fract(grid);

        float brightness = 0.0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                vec2 nc = cell + vec2(float(dx), float(dy));
                // Random sub-cell position and magnitude
                float jx  = _starHash(nc);
                float jy  = _starHash(nc + vec2(1.3, 7.4));
                float mag = _starHash(nc + vec2(3.7, 2.1));
                // Power law for formula, mostly dim stars with rare bight points
                mag = pow(mag, 5.0);
                float d = length(local - vec2(float(dx), float(dy)) - vec2(jx, jy));

                brightness += smoothstep(0.045, 0.0, d) * mag;
            }
        }
        // Add star color, some closer to A/O types while others are K/G types
        float warmth = _starHash(cell + vec2(9.1, 4.6));
        vec3 starCol = mix(vec3(0.82, 0.91, 1.0), vec3(1.0,  0.82, 0.55), step(0.85, warmth));

        // XMB ass wave
        float band = exp(-abs(dir.y) * 5.5) * 0.022;
        vec3  nebula = vec3(0.12, 0.18, 0.38) * band;

        return sky + starCol * brightness + nebula;
    }

    vec3 traceColor(vec3 pos, vec3 dir) {
        vec3 color = vec3(0.0);
        float alpha = 1.0;

        dir = normalize(dir);

        //Initial val
        vec3 h = cross(pos, dir);
        float h2 = dot(h, h);

        // Dithering
        float dither = hash2d(vec2(gl_FragCoord.xy));
        float initialStep = clamp(length(pos) * 0.04, 0.02, 0.3);
        pos += dir * (initialStep * dither);

        // Ray iterations count
        for (int i = 0; i < 320; i++) {
            // Dynamically change steps taken based on distance from the black hole
            float dist = length(pos);
            float stepSize = clamp(dist * 0.04, 0.02, 0.3);

            // scale lensing by step size
            // Original didn't, and while it worked, dynamic changes break it
            vec3 acc = accel(h2, pos) * stepSize;
            dir += acc;
            h = cross(pos, dir);
            h2 = dot(h, h);

            if (dot(pos, pos) < 1.0) return color;

            // pass dynamic step sizes
            adiskColor(pos, color, alpha, stepSize, dir);

            pos += dir * stepSize;
        }

        dir = rotateVector(dir, vec3(0.0, 1.0, 0.0), time);
        color += sampleSky(dir) * alpha;
        return color;
    }

    // COUGH COUGH WHAT THE FUCK AHH
    uniform vec3 camPos;
    uniform mat3 view;

    void main() {
        ivec2 coord = ivec2(gl_FragCoord.xy);
        if ((coord.x + coord.y + frameIndex) % 2 != 0){
            discard;
        }
        vec2 uv = gl_FragCoord.xy / res - vec2(0.5);
        uv.x   *= res.x / res.y;

        // Distance from black hole, good enough for LOD
        float screenDist = length(uv);

        vec3 dir = view * normalize(vec3(-uv.x * fovScale, uv.y * fovScale, 1.0));

        vec3 color;

        if (screenDist > photonScreenRadius) {
            // If we can, use the fast path
            // Sample the CPU-computed exit direction
            vec2 deflectUV  = gl_FragCoord.xy / res; // [0,1]
            vec4 deflection = textureLod(deflectionMap, deflectUV, 0.0);

            if (deflection.w < 0.5) {
                // if hit BH, black
                color = vec3(0.0);
            } else {
                // Exit direction from CPU
                vec3 exitDir = normalize(deflection.xyz);
                vec4 cpuDisk = textureLod(diskColorMap, deflectUV, 0.0);
                color = cpuDisk.rgb;
                float alpha = cpuDisk.a;
                // Apply time to match GPU
                exitDir = rotateVector(exitDir, vec3(0.0, 1.0, 0.0), time);
                color += sampleSky(exitDir) * alpha;
            }
        } else {
            // If we are close enough, use the full tracing
            color = traceColor(camPos, dir);
        }

        fragColor = vec4(color, 1.0);

    }
)text";

static const char *const bloom_extract_fs = R"text(
    #version 330 core
    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D sceneTex;
    uniform float threshold;

    void main() {
        vec3 color = texture(sceneTex, uv).rgb;
        // Luminance val
        float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
        // Softer fall off instead of hard edges
        float contrib = smoothstep(threshold, threshold + 0.3, brightness);
        fragColor = vec4(color * contrib, 1.0);
    }
)text";

static const char *const bloom_blur_fs = R"text(
    #version 330 core
    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D blurTex;
    uniform vec2 direction;   // (1,0) or (0,1)
    uniform vec2 texelSize;   // 1.0 / vec2(BLOOM_W, BLOOM_H)

    // 9-tap Gaussian weights
    const float weight[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);

    void main() {
        vec3 result = texture(blurTex, uv).rgb * weight[0];
        vec2 step = direction * texelSize;
        for (int i = 1; i < 5; i++) {
            result += texture(blurTex, uv + step * float(i)).rgb * weight[i];
            result += texture(blurTex, uv - step * float(i)).rgb * weight[i];
        }
        fragColor = vec4(result, 1.0);
    }
)text";

static const char *const bloom_composite_fs = R"text(
    #version 330 core
    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D sceneTex;
    uniform sampler2D bloomTex;
    uniform float bloomStrength;

    void main() {
        vec3 scene = texture(sceneTex, uv).rgb;
        vec3 bloom  = texture(bloomTex, uv).rgb;

        //Make sure that bloom never darkens
        vec3 result = scene + bloom * bloomStrength;

        // Make sure that the bloom doesn't snap to white
        result = vec3(1.0) - exp(-result * 1.2);

        fragColor = vec4(result, 1.0);
    }
)text";

static const char *const checkerboard_resolve_fs = R"text(
    #version 330 core
    in vec2 uv;
    out vec4 fragColor;

    uniform sampler2D currentTex;
    uniform sampler2D previousTex;
    uniform int frameIndex;
    uniform vec2 texelSize;

    void main() {
        ivec2 coord = ivec2(gl_FragCoord.xy);
        bool thisFramePixel = ((coord.x + coord.y + frameIndex) % 2 == 0);

        if (thisFramePixel) {
            // ie, if pixel X is written, use it directly
            fragColor = texture(currentTex, uv);
        } else {
        // Reconstruct from the previous frame with a 4 average for smoothing
        // Ie, if this pixel was skipped use data from 4 average
        vec3 c = texture(previousTex, uv).rgb;
        c += texture(previousTex, uv + vec2( texelSize.x, 0.0)).rgb;
        c += texture(previousTex, uv + vec2(-texelSize.x, 0.0)).rgb;
        c += texture(previousTex, uv + vec2(0.0,  texelSize.y)).rgb;
        fragColor = vec4(c * 0.25, 1.0);
        }
    }
)text";

// static GLint res;
static GLuint tex1;
static GLuint tex2;
static GLint resloc;

static GLint loc_camPos;
static GLint loc_view;

static GLuint s_bloomExtractProg;
static GLuint s_bloomBlurProg;
static GLuint s_bloomCompositeProg;

static GLuint s_sceneFbo, s_sceneTex;
static GLuint s_bloomFboA, s_bloomTexA;
static GLuint s_bloomFboB, s_bloomTexB;

static GLint s_blur_dirLoc;
static GLint s_blur_texelLoc;
static GLint s_composite_bloomStrengthLoc;

static const int BLOOM_W = 320;
static const int BLOOM_H = 180;

static GLuint s_prevSceneFbo;
static GLuint s_prevSceneTex;
static GLuint s_resolveProg;

static GLint s_resolve_frameIndexLoc;
// static GLint s_resolve_texelSizeLoc;
static GLint s_rt_frameIndexLoc;
static int s_frameIndex = 0;

static GLuint s_noiseTex3D;
static GLint s_noiseTexLoc;

static const int DEFLECT_W = 480;
static const int DEFLECT_H = 270;
static GLuint s_deflectionTex;
static GLint s_deflectionTexLoc;

static GLuint s_diskColorTex;
static GLint s_diskColorTexLoc;
alignas(64) static float s_diskColorBuf[2][DEFLECT_W * DEFLECT_H * 4];

static std::thread s_deflectThreads[2];
static std::atomic<uint32_t> s_targetFrame{ 0 };
static std::atomic<int> s_threadsFinished{ 0 };

static std::chrono::time_point<std::chrono::high_resolution_clock> s_frameStartTime;
static std::atomic<float> s_currentCpuFps{ 0.0f };
static std::atomic<float> s_lastCpuLatencyMs{ 0.0f };

extern "C" float bhrt_cpu_fps(void) {
    return s_currentCpuFps.load(std::memory_order_acquire);
}

alignas(64) static float s_deflectBuf[2][DEFLECT_W * DEFLECT_H * 4];
static std::atomic<int> s_deflectReadBuf{ 0 };
static std::atomic<bool> s_deflectReady{ true };

struct alignas(64) FrameUniforms {
    float camPos[3];
    float view[9];
    float orbitalLUT[256 * 4];
};

static FrameUniforms g_uniforms[2];
static std::atomic<int> g_uniformWriteIdx{ 0 };
static std::atomic<bool> g_uniformReady{ false };
static std::mutex g_uniformMutex;

static std::thread s_deflectThread;

float snoise_cpu(float v_x, float v_y, float v_z) {

    const float C_x = 1.0f / 6.0f;
    const float C_y = 1.0f / 3.0f;

    float dot_v_Cyyy = (v_x + v_y + v_z) * C_y;
    float i_x = std::floor(v_x + dot_v_Cyyy);
    float i_y = std::floor(v_y + dot_v_Cyyy);
    float i_z = std::floor(v_z + dot_v_Cyyy);

    float dot_i_Cxxx = (i_x + i_y + i_z) * C_x;
    float x0_x = v_x - i_x + dot_i_Cxxx;
    float x0_y = v_y - i_y + dot_i_Cxxx;
    float x0_z = v_z - i_z + dot_i_Cxxx;

    float g_x = (x0_y >= x0_x) ? 1.0f : 0.0f;
    float g_y = (x0_z >= x0_y) ? 1.0f : 0.0f;
    float g_z = (x0_x >= x0_z) ? 1.0f : 0.0f;

    float l_x = 1.0f - g_x;
    float l_y = 1.0f - g_y;
    float l_z = 1.0f - g_z;

    float i1_x = std::min(g_x, l_z);
    float i1_y = std::min(g_y, l_x);
    float i1_z = std::min(g_z, l_y);

    float i2_x = std::max(g_x, l_z);
    float i2_y = std::max(g_y, l_x);
    float i2_z = std::max(g_z, l_y);

    float32x4_t i1 = { 0.0f, i1_x, i2_x, 1.0f };
    float32x4_t i2 = { 0.0f, i1_y, i2_y, 1.0f };
    float32x4_t i3 = { 0.0f, i1_z, i2_z, 1.0f };

    float32x4_t vC_xxx = vdupq_n_f32(C_x);
    float32x4_t v_zero = vdupq_n_f32(0.0f);
    float32x4_t v_one = vdupq_n_f32(1.0f);

    float32x4_t c_mult = { 0.0f, 1.0f, 2.0f, 3.0f };
    float32x4_t c_offset = vmulq_f32(c_mult, vC_xxx);

    float32x4_t dx = vsubq_f32(vdupq_n_f32(x0_x), i1);
    float32x4_t dy = vsubq_f32(vdupq_n_f32(x0_y), i2);
    float32x4_t dz = vsubq_f32(vdupq_n_f32(x0_z), i3);

    dx = vaddq_f32(dx, c_offset);
    dy = vaddq_f32(dy, c_offset);
    dz = vaddq_f32(dz, c_offset);

    float i_mod_x = i_x - std::floor(i_x * (1.0f / 289.0f)) * 289.0f;
    float i_mod_y = i_y - std::floor(i_y * (1.0f / 289.0f)) * 289.0f;
    float i_mod_z = i_z - std::floor(i_z * (1.0f / 289.0f)) * 289.0f;

    float32x4_t p_z = vaddq_f32(vdupq_n_f32(i_mod_z), i3);
    float32x4_t p_y = vaddq_f32(vdupq_n_f32(i_mod_y), i2);
    float32x4_t p_x = vaddq_f32(vdupq_n_f32(i_mod_x), i1);

    float32x4_t p = vpermute(vaddq_f32(vpermute(vaddq_f32(vpermute(p_z), p_y)), p_x));

    float ns_z = 1.0f / 7.0f;
    float ns_x = ns_z * 2.0f;
    float32x4_t vns_z = vdupq_n_f32(ns_z);
    float32x4_t vns_x = vdupq_n_f32(ns_x);

    float32x4_t j_floor = vrndmq_f32(vmulq_f32(p, vdupq_n_f32(ns_z * ns_z)));
    float32x4_t j = vmlsq_f32(p, j_floor, vdupq_n_f32(49.0f));

    float32x4_t x_ = vrndmq_f32(vmulq_f32(j, vns_z));
    float32x4_t y_ = vrndmq_f32(vmlsq_f32(j, x_, vdupq_n_f32(7.0f)));

    float32x4_t gx = vmlaq_f32(vdupq_n_f32(-1.0f), x_, vns_x);
    float32x4_t gy = vmlaq_f32(vdupq_n_f32(-1.0f), y_, vns_x);

    float32x4_t h = vsubq_f32(vsubq_f32(v_one, vabsq_f32(gx)), vabsq_f32(gy));

    float32x4_t b0_floor = vrndmq_f32(gx);
    float32x4_t b1_floor = vrndmq_f32(gy);
    float32x4_t s0 = vmlaq_f32(v_one, b0_floor, vdupq_n_f32(2.0f));
    float32x4_t s1 = vmlaq_f32(v_one, b1_floor, vdupq_n_f32(2.0f));

    uint32x4_t h_lt_zero = vcltq_f32(h, v_zero);
    float32x4_t sh = vbslq_f32(h_lt_zero, vdupq_n_f32(-1.0f), v_zero);

    float32x4_t ax = vmlaq_f32(gx, s0, sh);
    float32x4_t ay = vmlaq_f32(gy, s1, sh);

    float32x4_t dot_p = vmulq_f32(ax, ax);
    dot_p = vmlaq_f32(dot_p, ay, ay);
    dot_p = vmlaq_f32(dot_p, h, h);

    float32x4_t norm = vtaylorInvSqrt(dot_p);
    ax = vmulq_f32(ax, norm);
    ay = vmulq_f32(ay, norm);
    h = vmulq_f32(h, norm);

    float32x4_t dot_dx = vmulq_f32(dx, dx);
    dot_dx = vmlaq_f32(dot_dx, dy, dy);
    dot_dx = vmlaq_f32(dot_dx, dz, dz);

    float32x4_t m = vmaxq_f32(vsubq_f32(vdupq_n_f32(0.6f), dot_dx), v_zero);
    m = vmulq_f32(m, m);
    float32x4_t m4 = vmulq_f32(m, m);

    float32x4_t dot_p_dx = vmulq_f32(ax, dx);
    dot_p_dx = vmlaq_f32(dot_p_dx, ay, dy);
    dot_p_dx = vmlaq_f32(dot_p_dx, h, dz);

    float32x4_t final_vec = vmulq_f32(m4, dot_p_dx);

    float final_sum = vaddvq_f32(final_vec);

    return 42.0f * final_sum;
}

static void buildNoise3() {
    const int N = 64;
    std::vector<uint8_t> data(N * N * N);
    for (int z = 0; z < N; z++)
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float fx = float(x) / float(N);
                float fy = float(y) / float(N);
                float fz = float(z) / float(N);

                float v = snoise_cpu(fx * 16.0f, fy * 16.0f, fz * 16.0f);
                data[z * N * N + y * N + x] = (uint8_t)((v * 0.5f + 0.5f) * 255.0f);
            }

    glGenTextures(1, &s_noiseTex3D);
    glBindTexture(GL_TEXTURE_3D, s_noiseTex3D);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, N, N, N, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
}

static float s_orbitalLUT[256 * 2];
static GLuint s_orbitalLUTTex;
static GLint loc_orbitalLUT;

void updateOrbitalLUT(float t) {

    const float innerRadius = 2.6f;
    const float outerRadius = 12.0f;
    const float orbitalScale = 2.5f;

    int i = 0;
    for (; i <= 252; i += 4) {

        float32x4_t idx = { (float)i, (float)(i + 1), (float)(i + 2), (float)(i + 3) };
        float32x4_t u = vmulq_n_f32(idx, 1.0f / 255.0f);
        float32x4_t r = vmlaq_n_f32(vdupq_n_f32(innerRadius), u, outerRadius - innerRadius);

        float32x4_t rsqrt_r = vrsqrteq_f32(r);
        rsqrt_r = vmulq_f32(rsqrt_r, vrsqrtsq_f32(vmulq_f32(r, rsqrt_r), rsqrt_r));
        float32x4_t sqrt_r = vmulq_f32(r, rsqrt_r);
        float32x4_t r1_5 = vmulq_f32(r, sqrt_r);

        float32x4_t rcp = vrecpeq_f32(r1_5);
        rcp = vmulq_f32(rcp, vrecpsq_f32(r1_5, rcp));
        float32x4_t speed = vmulq_n_f32(rcp, orbitalScale);

        float32x4_t angle = vmulq_n_f32(speed, t);

        float angles[4];
        vst1q_f32(angles, angle);

        float cs[8];
        fastSinCos(angles[0], &cs[1], &cs[0]);
        fastSinCos(angles[1], &cs[3], &cs[2]);
        fastSinCos(angles[2], &cs[5], &cs[4]);
        fastSinCos(angles[3], &cs[7], &cs[6]);

        s_orbitalLUT[(i + 0) * 2 + 0] = cs[0];
        s_orbitalLUT[(i + 0) * 2 + 1] = cs[1];
        s_orbitalLUT[(i + 1) * 2 + 0] = cs[2];
        s_orbitalLUT[(i + 1) * 2 + 1] = cs[3];
        s_orbitalLUT[(i + 2) * 2 + 0] = cs[4];
        s_orbitalLUT[(i + 2) * 2 + 1] = cs[5];
        s_orbitalLUT[(i + 3) * 2 + 0] = cs[6];
        s_orbitalLUT[(i + 3) * 2 + 1] = cs[7];
    }

    for (; i < 256; i++) {
        float u = i / 255.0f;
        float r = innerRadius + u * (outerRadius - innerRadius);
        float speed = orbitalScale / powf(fmaxf(r, 0.2f), 1.5f);
        float angle = t * speed;
        fastSinCos(angle, &s_orbitalLUT[i * 2 + 1], &s_orbitalLUT[i * 2 + 0]);
    }

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s_orbitalLUTTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RG, GL_FLOAT, s_orbitalLUT);
}

static inline float32x4x3_t neon_accel(float32x4_t h2, float32x4_t px, float32x4_t py, float32x4_t pz) {
    float32x4_t r2 = vmlaq_f32(vmlaq_f32(vmulq_f32(px, px), py, py), pz, pz);
    float32x4_t r4 = vmulq_f32(r2, r2);
    float32x4_t rsqrt_r = vrsqrteq_f32(r2);
    rsqrt_r = vmulq_f32(rsqrt_r, vrsqrtsq_f32(vmulq_f32(r2, rsqrt_r), rsqrt_r));
    float32x4_t r2_5 = vmulq_f32(r4, vmulq_f32(r2, rsqrt_r));

    float32x4_t rcp = vrecpeq_f32(r2_5);
    rcp = vmulq_f32(rcp, vrecpsq_f32(r2_5, rcp));

    float32x4_t scale = vmulq_n_f32(vmulq_f32(h2, rcp), -1.5f);

    float32x4x3_t result;
    result.val[0] = vmulq_f32(scale, px);
    result.val[1] = vmulq_f32(scale, py);
    result.val[2] = vmulq_f32(scale, pz);
    return result;
}

static void traceDeflect4(float cpx, float cpy, float cpz, float32x4_t dx, float32x4_t dy, float32x4_t dz, float *outX, float *outY, float *outZ,
                          float *outW, float *outDR, float *outDG, float *outDB, float *outDA, float dither) {
    float32x4_t px = vdupq_n_f32(cpx);
    float32x4_t py = vdupq_n_f32(cpy);
    float32x4_t pz = vdupq_n_f32(cpz);

    float32x4_t hx = vsubq_f32(vmulq_f32(py, dz), vmulq_f32(pz, dy));
    float32x4_t hy = vsubq_f32(vmulq_f32(pz, dx), vmulq_f32(px, dz));
    float32x4_t hz = vsubq_f32(vmulq_f32(px, dy), vmulq_f32(py, dx));
    float32x4_t h2 = vmlaq_f32(vmlaq_f32(vmulq_f32(hx, hx), hy, hy), hz, hz);

    float diskR[4] = {}, diskG[4] = {}, diskB[4] = {};
    float diskAlpha[4] = { 1.f, 1.f, 1.f, 1.f };
    const float innerR = 2.6f, outerR = 12.0f, thinH = 0.18f, absorption = 1.2f;

    float32x4_t alive = vdupq_n_f32(1.0f);
    float32x4_t hitBH = vdupq_n_f32(0.0f);

    float32x4_t r2_init = vmlaq_f32(vmlaq_f32(vmulq_f32(px, px), py, py), pz, pz);
    float32x4_t rsqrt_r_init = vrsqrteq_f32(r2_init);
    rsqrt_r_init = vmulq_f32(rsqrt_r_init, vrsqrtsq_f32(vmulq_f32(r2_init, rsqrt_r_init), rsqrt_r_init));
    float32x4_t dist_init = vmulq_f32(r2_init, rsqrt_r_init);

    float32x4_t initialStep = vminq_f32(vmaxq_f32(vmulq_n_f32(dist_init, 0.04f), vdupq_n_f32(0.02f)), vdupq_n_f32(0.3f));
    initialStep = vmulq_n_f32(initialStep, dither);

    px = vaddq_f32(px, vmulq_f32(dx, initialStep));
    py = vaddq_f32(py, vmulq_f32(dy, initialStep));
    pz = vaddq_f32(pz, vmulq_f32(dz, initialStep));

    for (int i = 0; i < 80; i++) {
        float32x4_t r2 = vmlaq_f32(vmlaq_f32(vmulq_f32(px, px), py, py), pz, pz);

        float32x4_t rsqrt_r = vrsqrteq_f32(r2);
        rsqrt_r = vmulq_f32(rsqrt_r, vrsqrtsq_f32(vmulq_f32(r2, rsqrt_r), rsqrt_r));
        float32x4_t dist = vmulq_f32(r2, rsqrt_r);
        float32x4_t stepSize = vminq_f32(vmaxq_f32(vmulq_n_f32(dist, 0.04f), vdupq_n_f32(0.02f)), vdupq_n_f32(0.3f));

        float32x4x3_t acc = neon_accel(h2, px, py, pz);
        dx = vaddq_f32(dx, vmulq_f32(acc.val[0], stepSize));
        dy = vaddq_f32(dy, vmulq_f32(acc.val[1], stepSize));
        dz = vaddq_f32(dz, vmulq_f32(acc.val[2], stepSize));

        hx = vsubq_f32(vmulq_f32(py, dz), vmulq_f32(pz, dy));
        hy = vsubq_f32(vmulq_f32(pz, dx), vmulq_f32(px, dz));
        hz = vsubq_f32(vmulq_f32(px, dy), vmulq_f32(py, dx));
        h2 = vmlaq_f32(vmlaq_f32(vmulq_f32(hx, hx), hy, hy), hz, hz);

        uint32x4_t insideEH = vcltq_f32(r2, vdupq_n_f32(1.0f));

        uint32x4_t justDied = vandq_u32(insideEH, vreinterpretq_u32_f32(alive));
        hitBH = vaddq_f32(hitBH, vreinterpretq_f32_u32(justDied));

        alive = vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(alive), insideEH));

        px = vaddq_f32(px, vmulq_f32(vmulq_f32(dx, stepSize), alive));
        py = vaddq_f32(py, vmulq_f32(vmulq_f32(dy, stepSize), alive));
        pz = vaddq_f32(pz, vmulq_f32(vmulq_f32(dz, stepSize), alive));

        {
            float pxs[4], pys[4], pzs[4], dxs[4], dys[4], dzs[4], ss[4], als[4];
            vst1q_f32(pxs, px);
            vst1q_f32(pys, py);
            vst1q_f32(pzs, pz);
            vst1q_f32(dxs, dx);
            vst1q_f32(dys, dy);
            vst1q_f32(dzs, dz);
            vst1q_f32(ss, stepSize);
            vst1q_f32(als, alive);

            for (int lane = 0; lane < 4; lane++) {
                if (als[lane] < 0.5f || diskAlpha[lane] < 0.01f)
                    continue;
                float px_ = pxs[lane], py_ = pys[lane], pz_ = pzs[lane];
                float r_xz = sqrtf(px_ * px_ + pz_ * pz_);

                float density = 1.0f - sqrtf((px_ / outerR) * (px_ / outerR) + (py_ / thinH) * (py_ / thinH) + (pz_ / outerR) * (pz_ / outerR));
                if (density < 0.001f || r_xz < innerR || r_xz > outerR)
                    continue;

                float radT = (r_xz - innerR) / (outerR - innerR);
                float vertFade = 1.0f - fabsf(py_) / thinH;
                vertFade = vertFade * vertFade * vertFade * vertFade * vertFade * vertFade;
                density *= vertFade;
                if (density < 0.003f)
                    continue;

                float speed = fminf(0.65f / sqrtf(fmaxf(r_xz, 2.f)), 0.6f);

                float vx = -pz_ / r_xz, vz = px_ / r_xz;
                float len_d = sqrtf(dxs[lane] * dxs[lane] + dys[lane] * dys[lane] + dzs[lane] * dzs[lane]);
                float cosT = -(dxs[lane] * vx + dzs[lane] * vz) / (len_d + 1e-8f);
                float gamma_ = 1.0f / sqrtf(1.0f - speed * speed);
                float doppler = 1.0f / (gamma_ * (1.0f - cosT * speed));
                doppler = fmaxf(doppler, 0.001f);
                float beaming = doppler * doppler * doppler * doppler;
                float cr = (radT < 0.2f) ? (1.3f * (1.f - radT / 0.2f) + 1.0f * (radT / 0.2f))
                                         : (1.0f * (1.f - (radT - 0.2f) / 0.8f) + 0.4f * (radT - 0.2f) / 0.8f);
                float cg = (radT < 0.2f) ? (1.1f * (1.f - radT / 0.2f) + 0.4f * (radT / 0.2f))
                                         : (0.4f * (1.f - (radT - 0.2f) / 0.8f) + 0.02f * (radT - 0.2f) / 0.8f);
                float cb = (radT < 0.2f) ? (0.9f * (1.f - radT / 0.2f) + 0.05f * (radT / 0.2f))
                                         : (0.05f * (1.f - (radT - 0.2f) / 0.8f) + 0.0f * (radT - 0.2f) / 0.8f);
                cr *= powf(doppler, 1.5f);
                cg *= powf(doppler, 1.5f);
                cb *= powf(doppler, 1.5f);

                float sampleAlpha = fminf(density * ss[lane] * absorption * beaming * 0.45f, 1.0f);
                float lit = 15.0f;

                diskR[lane] += cr * lit * sampleAlpha * diskAlpha[lane] * beaming;
                diskG[lane] += cg * lit * sampleAlpha * diskAlpha[lane] * beaming;
                diskB[lane] += cb * lit * sampleAlpha * diskAlpha[lane] * beaming;
                diskAlpha[lane] *= (1.0f - sampleAlpha);
            }
        }

        if (vmaxvq_u32(vreinterpretq_u32_f32(alive)) == 0)
            break;
    }

    float32x4_t len2 = vmlaq_f32(vmlaq_f32(vmulq_f32(dx, dx), dy, dy), dz, dz);
    float32x4_t rlen = vrsqrteq_f32(len2);
    rlen = vmulq_f32(rlen, vrsqrtsq_f32(vmulq_f32(len2, rlen), rlen));

    vst1q_f32(outX, vmulq_f32(dx, rlen));
    vst1q_f32(outY, vmulq_f32(dy, rlen));
    vst1q_f32(outZ, vmulq_f32(dz, rlen));

    uint32x4_t wasBH = vcgtq_f32(hitBH, vdupq_n_f32(0.0f));
    float32x4_t exitW = vreinterpretq_f32_u32(vbicq_u32(vdupq_n_u32(0x3F800000u), wasBH));
    vst1q_f32(outW, exitW);

    outDR[0] = diskR[0];
    outDR[1] = diskR[1];
    outDR[2] = diskR[2];
    outDR[3] = diskR[3];
    outDG[0] = diskG[0];
    outDG[1] = diskG[1];
    outDG[2] = diskG[2];
    outDG[3] = diskG[3];
    outDB[0] = diskB[0];
    outDB[1] = diskB[1];
    outDB[2] = diskB[2];
    outDB[3] = diskB[3];
    outDA[0] = diskAlpha[0];
    outDA[1] = diskAlpha[1];
    outDA[2] = diskAlpha[2];
    outDA[3] = diskAlpha[3];
}

static void deflectionWorkerFunc(const float camPos[3], const float view[9], float *deflectBuf, float *diskBuf, int startY, int endY) {

    float camDistW = sqrtf(camPos[0] * camPos[0] + camPos[1] * camPos[1] + camPos[2] * camPos[2]);
    float photonRadiusW = 12.0f / camDistW;

    float overlapRadius = photonRadiusW * 0.85f;
    float photonRadiusSqW = photonRadiusW * overlapRadius;
    const float aspectW = float(DEFLECT_W) / float(DEFLECT_H);

    for (int py_idx = 0; py_idx < DEFLECT_H; py_idx++)
        for (int px_idx = 0; px_idx < DEFLECT_W; px_idx += 4) {

            float cv = (py_idx + 0.5f) / DEFLECT_H - 0.5f;
            float cu = ((px_idx + 2.0f) / DEFLECT_W - 0.5f) * aspectW;

            if (cu * cu + cv * cv <= photonRadiusSqW) {
                int base = (py_idx * DEFLECT_W + px_idx) * 4;

                for (int k = 0; k < 4; k++) {
                    deflectBuf[base + k * 4 + 0] = 0.0f;
                    deflectBuf[base + k * 4 + 1] = 0.0f;
                    deflectBuf[base + k * 4 + 2] = 0.0f;
                    deflectBuf[base + k * 4 + 3] = 0.0f;

                    diskBuf[base + k * 4 + 0] = 0.0f;
                    diskBuf[base + k * 4 + 1] = 0.0f;
                    diskBuf[base + k * 4 + 2] = 0.0f;
                    diskBuf[base + k * 4 + 3] = 0.0f;
                }

                continue;
            }

            float uv[4][2];

            for (int k = 0; k < 4; k++) {
                float u = ((px_idx + k + 0.5f) / DEFLECT_W) - 0.5f;
                float v = (py_idx + 0.5f) / DEFLECT_H - 0.5f;

                u *= aspectW;

                uv[k][0] = -u;
                uv[k][1] = v;
            }

            const float fov = 1.0f;

            float ldx[4], ldy[4], ldz[4];

            for (int k = 0; k < 4; k++) {

                float lx = uv[k][0] * fov;
                float ly = uv[k][1] * fov;
                float lz = 1.0f;

                float len = sqrtf(lx * lx + ly * ly + lz * lz);

                lx /= len;
                ly /= len;
                lz /= len;

                ldx[k] = view[0] * lx + view[3] * ly + view[6] * lz;
                ldy[k] = view[1] * lx + view[4] * ly + view[7] * lz;
                ldz[k] = view[2] * lx + view[5] * ly + view[8] * lz;
            }

            float32x4_t dx4 = vld1q_f32(ldx);
            float32x4_t dy4 = vld1q_f32(ldy);
            float32x4_t dz4 = vld1q_f32(ldz);

            float dither = float(((px_idx * 73) + (py_idx * 101)) % 256) / 255.0f;

            float outX[4], outY[4], outZ[4], outW[4];
            float outDR[4], outDG[4], outDB[4], outDA[4];

            traceDeflect4(camPos[0], camPos[1], camPos[2], dx4, dy4, dz4, outX, outY, outZ, outW, outDR, outDG, outDB, outDA, dither);

            int base = (py_idx * DEFLECT_W + px_idx) * 4;

            for (int k = 0; k < 4; k++) {

                deflectBuf[base + k * 4 + 0] = outX[k];
                deflectBuf[base + k * 4 + 1] = outY[k];
                deflectBuf[base + k * 4 + 2] = outZ[k];
                deflectBuf[base + k * 4 + 3] = outW[k];

                diskBuf[base + k * 4 + 0] = outDR[k];
                diskBuf[base + k * 4 + 1] = outDG[k];
                diskBuf[base + k * 4 + 2] = outDB[k];
                diskBuf[base + k * 4 + 3] = outDA[k];
            }
        }
}

static void CPU_FPS_Start() {

    if (!running.load(std::memory_order_acquire))
        return;

    s_frameStartTime = std::chrono::high_resolution_clock::now();
}

static void CPU_FPS_End() {
    static auto lastFpsTime = std::chrono::high_resolution_clock::now();
    static int frameCount = 0;
    static float timeAccumulator = 0.0f;

    auto now = std::chrono::high_resolution_clock::now();

    float latency = std::chrono::duration<float>(now - s_frameStartTime).count();
    s_lastCpuLatencyMs.store(latency * 1000.0f, std::memory_order_release);

    float deltaFps = std::chrono::duration<float>(now - lastFpsTime).count();
    lastFpsTime = now;

    frameCount++;
    timeAccumulator += deltaFps;

    if (timeAccumulator >= 0.01f) {
        float fps = static_cast<float>(frameCount) / timeAccumulator;
        s_currentCpuFps.store(fps, std::memory_order_release);

        frameCount = 0;
        timeAccumulator = 0.0f;
    }
}

static void makeFbo(GLuint &fbo, GLuint &tex, int w, int h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void computeCamera_NEON(float t, float *outCamPos, float *outView) {

    float s = sinf(t * 0.1f);
    float c = cosf(t * 0.1f);

    float32x4_t eye = { -c * 15.f, s * 15.f, s * 15.f, 0.f };
    float32x4_t target = vdupq_n_f32(0.f);
    float32x4_t worldUp = { 0.f, 1.f, 0.f, 0.f };

    float32x4_t fwd = neon_normalize3(vsubq_f32(target, eye));
    float32x4_t right = neon_normalize3(neon_cross3(fwd, worldUp));
    float32x4_t newUp = neon_cross3(right, fwd);

    neon_store3(outCamPos, eye);

    // float32x4_t neg_fwd = vnegq_f32(fwd);
    neon_store3(outView + 0, right);
    neon_store3(outView + 3, newUp);
    neon_store3(outView + 6, fwd);
}

static void initTrace() {

    float initCamPos[3], initView[9];
    computeCamera_NEON(0.0f, initCamPos, initView);
    const float aspect = float(DEFLECT_W) / float(DEFLECT_H);

    for (int row = 0; row < DEFLECT_H; row++) {
        for (int col = 0; col < DEFLECT_W; col += 4) {
            float ldx[4], ldy[4], ldz[4];
            for (int k = 0; k < 4; k++) {
                float u = ((col + k + 0.5f) / DEFLECT_W - 0.5f) * aspect;
                float v = (row + 0.5f) / DEFLECT_H - 0.5f;
                float lx = -u, ly = v, lz = 1.0f;
                float len = sqrtf(lx * lx + ly * ly + lz * lz);
                lx /= len;
                ly /= len;
                lz /= len;
                ldx[k] = initView[0] * lx + initView[3] * ly + initView[6] * lz;
                ldy[k] = initView[1] * lx + initView[4] * ly + initView[7] * lz;
                ldz[k] = initView[2] * lx + initView[5] * ly + initView[8] * lz;
            }
            int base = (row * DEFLECT_W + col) * 4;
            for (int k = 0; k < 4; k++) {
                s_deflectBuf[0][base + k * 4 + 0] = ldx[k];
                s_deflectBuf[0][base + k * 4 + 1] = ldy[k];
                s_deflectBuf[0][base + k * 4 + 2] = ldz[k];

                s_deflectBuf[0][base + k * 4 + 3] = 1.0f;
            }
        }
    }

    memcpy(s_deflectBuf[1], s_deflectBuf[0], sizeof(s_deflectBuf[0]));

    for (int i = 0; i < DEFLECT_W * DEFLECT_H; i++) {
        s_diskColorBuf[0][i * 4 + 0] = 0.0f;
        s_diskColorBuf[0][i * 4 + 1] = 0.0f;
        s_diskColorBuf[0][i * 4 + 2] = 0.0f;
        s_diskColorBuf[0][i * 4 + 3] = 1.0f;
    }

    memcpy(s_diskColorBuf[1], s_diskColorBuf[0], sizeof(s_diskColorBuf[0]));
}

static void deflectThreadFunc(int threadIdx, int coreID) {

    pinThread(coreID);

    uint32_t localFrame = 0;

    while (running.load(std::memory_order_acquire)) {

        {
            std::unique_lock<std::mutex> lock(workMutex);
            workCV.wait(lock, [&] { return s_targetFrame.load(std::memory_order_acquire) > localFrame || !running.load(std::memory_order_acquire); });
        }

        if (!running.load(std::memory_order_acquire))
            break;

        localFrame = s_targetFrame.load(std::memory_order_acquire);

        int readIdx = g_uniformWriteIdx.load(std::memory_order_acquire);
        int writeIdx = s_deflectReadBuf.load(std::memory_order_acquire) ^ 1;

        int halfHeight = DEFLECT_H / 2;
        int startY = halfHeight * threadIdx;
        int endY = startY + halfHeight;

        deflectionWorkerFunc(g_uniforms[readIdx].camPos, g_uniforms[readIdx].view, s_deflectBuf[writeIdx], s_diskColorBuf[writeIdx], startY, endY);

        if (s_threadsFinished.fetch_add(1, std::memory_order_acq_rel) == 1) {

            CPU_FPS_End();

            s_deflectReadBuf.store(writeIdx, std::memory_order_release);
            s_deflectReady.store(true, std::memory_order_release);

            s_threadsFinished.store(0, std::memory_order_release);
        }
    }
}

void BHRTSceneInit() {
    pinThread(0);
    GLint vsh = createAndCompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLint fsh = createAndCompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    running = true;

    s_program = glCreateProgram();
    glAttachShader(s_program, vsh);
    glAttachShader(s_program, fsh);
    glLinkProgram(s_program);
    GLuint tex1loc = glGetUniformLocation(s_program, "galaxy");
    GLuint tex2loc = glGetUniformLocation(s_program, "colorMap");
    loc_time = glGetUniformLocation(s_program, "time");
    resloc = glGetUniformLocation(s_program, "res");
    s_noiseTexLoc = glGetUniformLocation(s_program, "noiseTex");
    buildNoise3();

    GLint success;
    glGetProgramiv(s_program, GL_LINK_STATUS, &success);
    if (!success) {
        char buf[512];
        glGetProgramInfoLog(s_program, sizeof(buf), nullptr, buf);
        TRACE("Link error: %s", buf);
    }
    glDeleteShader(vsh);
    glDeleteShader(fsh);

    float verts[] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    int width, height, nchan;
    stbi_set_flip_vertically_on_load(true);

    glGenTextures(1, &tex1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex1);
    stbi_uc *img = stbi_load_from_memory((const stbi_uc *)sk_png, sk_png_size, &width, &height, &nchan, 4);
    for (int f = 0; f < 6; f++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    stbi_image_free(img);

    glGenTextures(1, &tex2);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    img = stbi_load_from_memory((const stbi_uc *)colormap_png, colormap_png_size, &width, &height, &nchan, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);

    glGenTextures(1, &s_orbitalLUTTex);
    glBindTexture(GL_TEXTURE_2D, s_orbitalLUTTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, 256, 1, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    initTrace();

    glGenTextures(1, &s_deflectionTex);
    glBindTexture(GL_TEXTURE_2D, s_deflectionTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, DEFLECT_W, DEFLECT_H, 0, GL_RGBA, GL_FLOAT, s_deflectBuf[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glUseProgram(s_program);

    s_deflectThreads[0] = std::thread(deflectThreadFunc, 0, 1);

    s_deflectThreads[1] = std::thread(deflectThreadFunc, 1, 2);

    loc_camPos = glGetUniformLocation(s_program, "camPos");
    loc_view = glGetUniformLocation(s_program, "view");
    s_deflectionTexLoc = glGetUniformLocation(s_program, "deflectionMap");
    s_diskColorTexLoc = glGetUniformLocation(s_program, "diskColorMap");

    glActiveTexture(GL_TEXTURE4);

    glBindTexture(GL_TEXTURE_2D, s_deflectionTex);
    glUniform1i(s_deflectionTexLoc, 4);

    glUniform1i(tex2loc, 1);

    glGenTextures(1, &s_diskColorTex);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, s_diskColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, DEFLECT_W, DEFLECT_H, 0, GL_RGBA, GL_FLOAT, s_diskColorBuf[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glUniform1i(s_diskColorTexLoc, 5);

    // auto projMtx = glm::perspective(glm::radians(40.0f), 1280.0f / 720.0f, 0.01f, 1000.0f);

    s_startTicks = armGetSystemTick();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex1);
    glUniform1i(tex1loc, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glUniform1i(tex2loc, 1);

    loc_orbitalLUT = glGetUniformLocation(s_program, "orbitalLUT");
    glUniform1i(loc_orbitalLUT, 3);

    s_lastFrameTime = s_startTicks;
    s_fpsUpdateTime = s_startTicks;
    s_frameCount = 0;

    initTextRenderer();

    makeFbo(s_sceneFbo, s_sceneTex, 1280, 720);
    makeFbo(s_bloomFboA, s_bloomTexA, BLOOM_W, BLOOM_H);
    makeFbo(s_bloomFboB, s_bloomTexB, BLOOM_W, BLOOM_H);

    GLuint bvsh = createAndCompileShader(GL_VERTEX_SHADER, rt_vs);

    auto linkBloom = [&](const char *fs) -> GLuint {
        GLuint fsh = createAndCompileShader(GL_FRAGMENT_SHADER, fs);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, bvsh);
        glAttachShader(prog, fsh);
        glLinkProgram(prog);
        glDeleteShader(fsh);

        GLint ok;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetProgramInfoLog(prog, sizeof buf, nullptr, buf);
            FILE *f = fopen("/switch/bhrt_err.txt", "a");
            if (f) {
                fprintf(f, "Bloom Link:\n%s\n", buf);
                fclose(f);
            }
        }
        return prog;
    };

    s_bloomExtractProg = linkBloom(bloom_extract_fs);
    s_bloomBlurProg = linkBloom(bloom_blur_fs);
    s_bloomCompositeProg = linkBloom(bloom_composite_fs);
    glDeleteShader(bvsh);

    glUseProgram(s_bloomExtractProg);
    glUniform1i(glGetUniformLocation(s_bloomExtractProg, "sceneTex"), 0);

    glUniform1f(glGetUniformLocation(s_bloomExtractProg, "threshold"), 0.7f);

    glUseProgram(s_bloomBlurProg);
    glUniform1i(glGetUniformLocation(s_bloomBlurProg, "blurTex"), 0);
    s_blur_dirLoc = glGetUniformLocation(s_bloomBlurProg, "direction");
    s_blur_texelLoc = glGetUniformLocation(s_bloomBlurProg, "texelSize");
    glUniform2f(s_blur_texelLoc, 1.0f / BLOOM_W, 1.0f / BLOOM_H);

    glUseProgram(s_bloomCompositeProg);
    glUniform1i(glGetUniformLocation(s_bloomCompositeProg, "sceneTex"), 0);
    glUniform1i(glGetUniformLocation(s_bloomCompositeProg, "bloomTex"), 1);
    s_composite_bloomStrengthLoc = glGetUniformLocation(s_bloomCompositeProg, "bloomStrength");

    glUniform1f(s_composite_bloomStrengthLoc, 2.4f);

    makeFbo(s_prevSceneFbo, s_prevSceneTex, 1280, 720);

    GLuint resolve_vsh = createAndCompileShader(GL_VERTEX_SHADER, rt_vs);
    GLuint resolve_fsh = createAndCompileShader(GL_FRAGMENT_SHADER, checkerboard_resolve_fs);
    s_resolveProg = glCreateProgram();
    glAttachShader(s_resolveProg, resolve_vsh);
    glAttachShader(s_resolveProg, resolve_fsh);
    glLinkProgram(s_resolveProg);
    glDeleteShader(resolve_vsh);
    glDeleteShader(resolve_fsh);

    {
        GLint ok;
        glGetProgramiv(s_resolveProg, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetProgramInfoLog(s_resolveProg, sizeof(buf), nullptr, buf);
            FILE *f = fopen("/switch/bhrt_err.txt", "a");
            if (f) {
                fprintf(f, "RESOLVE LINK:\n%s\n", buf);
                fclose(f);
            }
        }
    }

    glUseProgram(s_resolveProg);
    glUniform1i(glGetUniformLocation(s_resolveProg, "currentTex"), 0);
    glUniform1i(glGetUniformLocation(s_resolveProg, "previousTex"), 1);
    glUniform2f(glGetUniformLocation(s_resolveProg, "texelSize"), 1.0f / 1280.0f, 1.0f / 720.0f);
    s_resolve_frameIndexLoc = glGetUniformLocation(s_resolveProg, "frameIndex");

    glUseProgram(s_program);
    s_rt_frameIndexLoc = glGetUniformLocation(s_program, "frameIndex");

    glBindFramebuffer(GL_FRAMEBUFFER, s_prevSceneFbo);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(s_program);
}

float getTime5() {
    u64 elapsed = armGetSystemTick() - s_startTicks;
    return (elapsed * 625 / 12) / 2000000000.0;
}

void BHRTRender() {

    u64 currentTime = armGetSystemTick();
    s_frameCount++;

    u64 timeSinceUpdate = currentTime - s_fpsUpdateTime;
    float secondsSinceUpdate = (timeSinceUpdate * 625.0f / 12.0f) / 1000000000.0f;

    if (secondsSinceUpdate >= 0.01f) {
        s_fps = s_frameCount / secondsSinceUpdate;
        s_frameCount = 0;
        s_fpsUpdateTime = currentTime;
    }

    glBindVertexArray(s_vao);
    float t = getTime5();

    float camPos[3];
    float view[9];
    computeCamera_NEON(t, camPos, view);

    float camDist = sqrtf(camPos[0] * camPos[0] + camPos[1] * camPos[1] + camPos[2] * camPos[2]);
    float photonRadius = 12.0f / camDist;

    {
        std::lock_guard<std::mutex> lk(workMutex);
        int wi = g_uniformWriteIdx.load(std::memory_order_acquire) ^ 1;
        memcpy(g_uniforms[wi].camPos, camPos, sizeof(camPos));
        memcpy(g_uniforms[wi].view, view, sizeof(view));

        g_uniformWriteIdx.store(wi, std::memory_order_release);
        s_targetFrame.fetch_add(1, std::memory_order_release);
    }

    CPU_FPS_Start();

    workCV.notify_all();

    updateOrbitalLUT(t);

    glBindFramebuffer(GL_FRAMEBUFFER, s_sceneFbo);
    glViewport(0, 0, 1280, 720);
    glUseProgram(s_program);
    glUniform1f(glGetUniformLocation(s_program, "photonScreenRadius"), photonRadius);
    glUniform3fv(loc_camPos, 1, camPos);
    glUniformMatrix3fv(loc_view, 1, GL_FALSE, view);
    glUniform1i(s_rt_frameIndexLoc, s_frameIndex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, s_noiseTex3D);
    glUniform1i(s_noiseTexLoc, 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s_orbitalLUTTex);
    glUniform1f(loc_time, t);
    glUniform2f(resloc, RenderX, RenderY);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, s_deflectionTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DEFLECT_W, DEFLECT_H, GL_RGBA, GL_FLOAT, s_deflectBuf[0]);
    glActiveTexture(GL_TEXTURE5);

    glBindTexture(GL_TEXTURE_2D, s_diskColorTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DEFLECT_W, DEFLECT_H, GL_RGBA, GL_FLOAT, s_diskColorBuf[0]);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, s_prevSceneFbo);
    glViewport(0, 0, 1280, 720);
    glUseProgram(s_resolveProg);
    glUniform1i(s_resolve_frameIndexLoc, s_frameIndex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_sceneTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_prevSceneTex);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFboA);
    glViewport(0, 0, BLOOM_W, BLOOM_H);
    glUseProgram(s_bloomExtractProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_prevSceneTex);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUseProgram(s_bloomBlurProg);
    const int BLUR_ITERATIONS = 8;

    for (int i = 0; i < BLUR_ITERATIONS; i++) {

        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFboB);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_bloomTexA);
        glUniform2f(s_blur_dirLoc, 1.0f, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFboA);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_bloomTexB);
        glUniform2f(s_blur_dirLoc, 0.0f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 1280, 720);
    glUseProgram(s_bloomCompositeProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_prevSceneTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_bloomTexA);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    char fpsText[32];
    snprintf(fpsText, sizeof(fpsText), "%.3f", s_fps);
    drawText(fpsText, -0.95f, 0.90f, 0.02f, 1.0f, 0.0f, 0.0f);

    char CPUFPS[64];
    snprintf(CPUFPS, sizeof(CPUFPS), "%.3f", s_currentCpuFps.load(std::memory_order_acquire));
    drawText(CPUFPS, 0.00f, 0.90f, 0.02f, 1.0f, 0.0f, 0.0f);

    s_frameIndex ^= 1;
}

void BHRTExit() {
    cleanupTextRenderer();

    glDeleteFramebuffers(1, &s_prevSceneFbo);
    glDeleteTextures(1, &s_prevSceneTex);
    glDeleteProgram(s_resolveProg);
    glDeleteBuffers(1, &s_vbo);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteProgram(s_program);
    glDeleteTextures(1, &s_orbitalLUTTex);

    running.store(false, std::memory_order_release);
    workCV.notify_all();

    if (s_deflectThreads[0].joinable())
        s_deflectThreads[0].join();
    if (s_deflectThreads[1].joinable())
        s_deflectThreads[1].join();
}

int BHRTMain(int arcg, char *argv[]) {

    setMesaConfig();

    if (!initEgl(nwindowGetDefault()))
        return EXIT_FAILURE;

    gladLoadGLLoader((GLADloadproc)eglGetProcAddress);

    BHRTSceneInit();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    while (appletMainLoop()) {

        padUpdate(&pad);
        u32 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_B) {
            state = STATE_MENU;
            break;
        }

        BHRTRender();
        eglSwapBuffers(s_display, s_surface);
    }

    BHRTExit();

    deinitEgl();
    return EXIT_SUCCESS;
}
