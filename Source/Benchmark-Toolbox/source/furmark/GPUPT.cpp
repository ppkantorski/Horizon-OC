#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

#define GLM_FORCE_PURE
#include "sates.h"
#include "stb_image.h"
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
    float *vertexData = (float *)malloc(100 * 64 * 6 * 5 * sizeof(float));
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

    free(vertexData);
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
}

static void setMesaConfig() {

    setenv("EGL_LOG_LEVEL", "debug", 1);
    setenv("MESA_VERBOSE", "all", 1);
    setenv("NOUVEAU_MESA_DEBUG", "1", 1);

    setenv("NV50_PROG_OPTIMIZE", "0", 1);
    setenv("NV50_PROG_DEBUG", "1", 1);
    setenv("NV50_PROG_CHIPSET", "0x120", 1);
}

static const char *const vertexShaderSource = R"text(
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

static const char *const fragmentShaderSource = R"text(
    #version 330 core
    // Alright so originally this was supposed to be a way more complex shader
    // It was supposed to be a proceduraly generated path tracing scene
    // Unfortunately, my stupidity knows no bounds, and well over 5000 lines in I decided that I was better off waterboarding myself
    // To anyone who even wants to ask, no you don't want to see the original, it was a fucking war crime
    // Ask Adam why he thought it was a good idea to copy entire libraries into this shit, and then acted surprised when it didn't work
    // so, have a cornel box instead, because I cannot be fucked to make a proper shader file loader, despite needing one soon anyway.

    in vec2 uv;
    out vec4 fragColor;

    uniform vec2 u_resolution;
    uniform float u_time;

    // Sampler variables
    uniform int u_frame;
    uniform sampler2D u_prevFrame;

    #define WHITE 0
    #define RED 1
    #define GREEN 2
    #define LIGHT 3
    #define MIRROR 4

    struct Hit {
        float dist;
        int material;
        vec3 normal;
    };

    float hash(vec2 p){
        return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);
    }

    vec3 rand3(vec3 p){
        float f = float(u_frame);

        return vec3(
            hash(p.xy + f),
            hash(p.yz + f*1.37),
            hash(p.zx + f*2.17)
        ) * 2.0 - 1.0;
    }

    float sdSphere(vec3 p, float r){
        return length(p) - r;
    }

    Hit map(vec3 p)
    {
        Hit h;
        h.dist = 1e9;
        h.material = WHITE;

        // ---------- mirror sphere ----------
        float s = sdSphere(p - vec3(0,1.0,-0.5), 1.0);
        if(s < h.dist){
            h.dist = s;
            h.material = MIRROR;
            h.normal = normalize(p - vec3(0,1.0,-0.5));
        }

        // ---------- room walls (inward planes) ----------

        // left (red)
        float left = p.x + 2.0;
        if(left < h.dist){
            h.dist = left;
            h.material = RED;
            h.normal = vec3(1, 0, 0);
        }

        // right (green)
        float right = 2.0 - p.x;
        if(right < h.dist){
            h.dist = right;
            h.material = GREEN;
            h.normal = vec3(-1, 0, 0);
        }

        // floor
        float floor = p.y;
        if(floor < h.dist){
            h.dist = floor;
            h.material = WHITE;
            h.normal = vec3(0, 1, 0);
        }

        // ceiling
        float ceil = 4.0 - p.y;
        if(ceil < h.dist){
            h.dist = ceil;
            h.material = WHITE;
            h.normal = vec3(0, -1, 0);
        }

        // back wall
        float back = 2.0 - p.z;
        if(back < h.dist){
            h.dist = back;
            h.material = WHITE;
            h.normal = vec3(0, 0, -1);
        }

        vec3 lCenter = vec3(0.0, 3.98, -1.2);
        vec3 lSize = vec3(0.9, 0.01, 0.9);

        vec3 d = abs(p - lCenter) - lSize;
        float light = length(max(d,0.0)) + min(max(d.x,max(d.y,d.z)),0.0);

        if(light < h.dist){
            h.dist = light;
            h.material = LIGHT;
        }

        return h;
    }

    Hit raymarch(vec3 ro, vec3 rd)
    {
        float t = 0.0;

        // 80 steps, lower values give better performance, while higher values increase load
        // increasing load beyond this only decreases power draw, while lowering results in graphical glitches
        for(int i=0;i<80;i++){
            vec3 p = ro + rd*t;
            Hit h = map(p);

            if(h.dist < 0.001){
                h.dist = t;
                return h;
            }

            t += h.dist;
            if(t > 50.0) break;
        }

        Hit miss;
        miss.dist = -1.0;
        return miss;
    }

    vec3 getColor(int m){
        if(m==RED) return vec3(1,0.2,0.2);
        if(m==GREEN) return vec3(0.2,1,0.2);
        return vec3(0.9);
    }

    bool isLight(int m){ return m==LIGHT; }
    bool isMirror(int m){ return m==MIRROR; }

    vec3 trace(vec3 ro, vec3 rd)
    {
        vec3 color = vec3(0);
        vec3 throughput = vec3(1);

        // Controls the amount of bounces made by rays in the scene
        // reduced to 3 for parody with CPU mode
        for(int bounce=0; bounce<3; bounce++)
        {
            Hit h = raymarch(ro, rd);

            // sky fallback
            if(h.dist < 0.0){
                color += throughput * vec3(0.7,0.8,1.0);
                break;
            }

            vec3 pos = ro + rd*h.dist;
            vec3 n = h.normal;

            // hit light
            if(isLight(h.material)){
                color += throughput * vec3(12.0);
                break;
            }

            // mirror bounce
            if(isMirror(h.material)){
                rd = reflect(rd, n);
            }
            else{
                // diffuse bounce (hemisphere)
                vec3 r = rand3(pos + float(bounce) + float(u_frame));
                rd = normalize(n + r);
                // cosine weighting (energy can only be transfered)
                // I mean if you want to change this sure, but you'll get flashbanged.

                float cosTheta = max(dot(rd, n), 0.0);

                throughput *= getColor(h.material) * cosTheta;
            }

            ro = pos + n*0.001;
        }

        // Clamp color values so we don't get a concussion simulator
        return color;
    }

    vec3 pathTrace(vec2 uv)
    {
        // convert uv to screen space values
        vec2 p = uv * 2.0 - 1.0;
        p.x *= u_resolution.x / u_resolution.y;

        // Camera setup
        vec3 ro = vec3(0,2,-6);
        vec3 target = vec3(0,2,0);

        vec3 forward = normalize(target - ro);
        vec3 right = normalize(cross(forward, vec3(0,1,0)));
        vec3 up = cross(right, forward);

        vec3 rd = normalize(forward + p.x*right + p.y*up);

        // Add pixel jittering to reduce noise
        vec2 jitter = vec2(
            hash(gl_FragCoord.xy + float(u_frame)),
            hash(gl_FragCoord.yx + float(u_frame))
        ) / u_resolution;

        return trace(ro, rd + vec3(jitter, 00));
    }

    void main()
    {
        //setup output
        vec2 uv = gl_FragCoord.xy / u_resolution;
        vec3 newSample = pathTrace(uv);

        // Accumulation
        vec3 prev = texture(u_prevFrame, gl_FragCoord.xy / u_resolution).rgb;
        vec3 accumulated;

        if(u_frame == 0)
            accumulated = newSample;
        else
            accumulated = (prev *float(u_frame) + newSample) / float(u_frame + 1);

        // Output final pixels, send to vertex
        fragColor = vec4(accumulated, 1.0);
    }
)text";

static GLuint s_program;
static GLuint s_vao, s_vbo;

static GLint resolutionLoc;

static GLint loc_mdlvMtx, loc_projMtx;
static GLint loc_time;

static u64 s_startTicks;

static u64 s_lastFrameTime = 0;
static float s_fps = 0.0f;
static int s_frameCount = 0;
static u64 s_fpsUpdateTime = 0;

static GLuint tex[2], fbo[2];
static GLuint frameLoc, prevframeLoc;
static int frame = 0;

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

void GPUPTSceneInit() {
    GLint vsh = createAndCompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLint fsh = createAndCompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
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

    frameLoc = glGetUniformLocation(s_program, "u_frame");
    prevframeLoc = glGetUniformLocation(s_program, "u_prevFrame");
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1280, 720, 0, GL_RGBA, GL_FLOAT, nullptr);
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
    auto projMtx = glm::perspective(glm::radians(40.0f), 1280.0f / 720.0f, 0.01f, 1000.0f);
    glUniformMatrix4fv(loc_projMtx, 1, GL_FALSE, glm::value_ptr(projMtx));

    s_startTicks = armGetSystemTick();

    s_lastFrameTime = s_startTicks;
    s_fpsUpdateTime = s_startTicks;
    s_frameCount = 0;

    initTextRenderer();
}
float getTime2() {
    u64 elapsed = armGetSystemTick() - s_startTicks;
    return (elapsed * 625 / 12) / 2000000000.0;
}

void GPUPTRender() {
    static int X = 0;
    static int Y = 1;

    u64 currentTime = armGetSystemTick();
    s_frameCount++;
    u64 timeSinceUpdate = currentTime - s_fpsUpdateTime;
    float secondsSinceUpdate = (timeSinceUpdate * 625.0f / 12.0f) / 1000000000.0f;

    if (secondsSinceUpdate >= 0.01f) {
        s_fps = s_frameCount / secondsSinceUpdate;
        s_frameCount = 0;
        s_fpsUpdateTime = currentTime;
    }

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(s_program);
    glUniform1f(loc_time, getTime2());
    glUniform2f(resolutionLoc, 1280.0f, 720.0f);
    glBindVertexArray(s_vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[X]);
    glUniform1i(prevframeLoc, 0);
    glUniform1i(frameLoc, frame);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo[Y]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[Y]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, 1280, 720, 0, 0, 1280, 720, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    if (frame == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[X]);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    std::swap(X, Y);
    frame++;

    glBindVertexArray(0);
    char fpsText[32];
    snprintf(fpsText, sizeof(fpsText), "%.3f", s_fps);
    drawText(fpsText, -0.95f, 0.90f, 0.02f, 1.0f, 0.0f, 0.0f);

    char sampleText[64];
    snprintf(sampleText, sizeof(sampleText), "Samples: %d", frame);
    drawText(sampleText, -0.95f, 0.90f, 0.02f, 1.0f, 0.0f, 0.0f);
}

void GPUPTExit() {
    cleanupTextRenderer();
    glDeleteBuffers(1, &s_vbo);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteProgram(s_program);
    frame = 0;
}

int GPUPTMain(int argc, char *argv[]) {

    setMesaConfig();

    if (!initEgl(nwindowGetDefault()))
        return EXIT_FAILURE;

    gladLoadGLLoader((GLADloadproc)eglGetProcAddress);

    GPUPTSceneInit();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    while (appletMainLoop()) {

        padUpdate(&pad);
        u32 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_B) {
            GPUPTExit();
            deinitEgl();
            state = STATE_MENU;
            return 0;
        }

        GPUPTRender();
        eglSwapBuffers(s_display, s_surface);
    }

    GPUPTExit();

    deinitEgl();
    return EXIT_SUCCESS;
}
