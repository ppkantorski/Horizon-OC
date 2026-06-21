#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

#define GLM_FORCE_PURE
#include "fur_png.h"
#include "noise_png.h"
#include "sates.h"
#include "stb_image.h"
#include "wall_png.h"
#include "wunk_png.h"
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
}

static const char *const vertexShaderSource = R"text(
    #version 330 core

    out vec2 v_uv;

    void main() {
        vec2 pos = vec2(
            (gl_VertexID == 1) ? 3.0 : -1.0,
            (gl_VertexID == 2) ? 3.0 : -1.0
        );
    v_uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
    }
)text";

static const char *const fragmentShaderSource = R"text(
    #version 330 core

    out vec4 fragColor;

    uniform vec2 u_resolution;
    uniform float u_time;
    uniform sampler2D u_texture1;
    uniform sampler2D u_texture2;
    uniform sampler2D u_texture3;
    uniform sampler2D u_texture4;

    const float PI = 3.1416;
    const float TAU = 2 * PI;

    float displace(vec3 p, sampler2D tex) {
        float s = 4.5;
        float u = s / TAU * atan(p.y / p.x);
        float v = sign(p.z) / TAU *
            acos((p.z * p.z * sqrt(s * s + 1) + sqrt(1 - p.z * p.z * s * s)) / (p.z * p.z + 1));
        vec2 uv = 2.0 * vec2(u, v);
        float disp = texture(tex, uv).r;
        return disp * 0.06;
    }

    mat2 rot2D(float a) {
        float sa = sin(a);
        float ca = cos(a);
        return mat2(ca, sa, -sa, ca);
    }

    void rotate(inout vec3 p) {
        p.xy *= rot2D(sin(u_time * 0.8) * 0.25);
        p.yz *= rot2D(sin(u_time * 0.7) * 0.2);
    }

    float map(vec3 p) {
        float dist = length(vec2(length(p.xy) - 0.6, p.z)) - 0.22;
        return dist * 0.7;
    }

    vec3 getNormal(vec3 p) {
        vec2 e = vec2(0.01, 0.0);
        vec3 n = vec3(map(p)) - vec3(map(p - e.xyy), map(p - e.yxy), map(p - e.yyx));
        return normalize(n);
    }

    float rayMarch(vec3 ro, vec3 rd) {
        float dist = 0.0;
        for (int i = 0; i < 32; i++) {
            vec3 p = ro + dist * rd;

            rotate(p);
            float hit = map(p);
            dist += hit;

            // displace
            dist -= displace(0.5 * p, u_texture2);

            vec2 uv = fract(p.xy * 0.317);
            float t39 = texture(u_texture1, uv.yx).r;
            float texNoise = texture(u_texture2, uv).r;
            float t3 = texture(u_texture3, uv * 0.73).r;
            float car = texture(u_texture4, uv * 0.37).r;

            dist += (texNoise + t39 + t3 + car) * 1e-4;
            dist += (texNoise + t39 + t3 + car) * 1e-5;
            dist += (texNoise + t39 + t3 + car) * 1e-6;

            if (dist > 100.0 || abs(hit) < 0.0001) break;
        }
        return dist;
    }

    vec3 triPlanar(sampler2D tex, vec3 p, vec3 normal) {
        normal = abs(normal);
        normal = pow(normal, vec3(15));
        normal /= normal.x + normal.y + normal.z;
        p = p * 0.5 + 0.5;
        return (texture(tex, p.xy) * normal.z +
                texture(tex, p.xz) * normal.y +
                texture(tex, p.yz) * normal.x).rgb;
}

    vec3 render(vec2 offset) {
        vec2 uv = (4.0 * (gl_FragCoord.xy + offset) - u_resolution.xy) / u_resolution.y;
        vec3 col = vec3(0);

        vec3 ro = vec3(0, 0, -1.0);
        vec3 rd = normalize(vec3(uv, 1.0));

        // return to normal rendering path
        float dist = rayMarch(ro, rd);

        if (dist < 100.0) {
            vec3 p = ro + dist * rd;
            rotate(p);
            col += triPlanar(u_texture1, p * 1.0, getNormal(p));
        } else {
            float phi = atan(uv.y, uv.x);
            float rho = length(uv) + 0.2;

            phi += sin(0.3 * rho - 0.5 * u_time);

            float h = sin(8.0 * phi) * 0.5 + 0.5;

            vec2 st;
            st.x = 3.0 * phi / PI;
            st.y = u_time * 0.5 + PI / (rho + 0.1 * smoothstep(0.45, 0.5, h));

            col += texture(u_texture3, st).rgb;

            float occ = smoothstep(0.0, 0.45, h) - smoothstep(0.5, 1.0, h);
            col *= 1.0 - 0.45 * occ * rho;
            col *= rho;
        }
        return col;
    }

    vec3 renderAAx4() {
        vec4 e = vec4(0.125, -0.125, 0.375, -0.375);
        vec3 colAA = render(e.xz) + render(e.yw) + render(e.wx) + render(e.zy);
        return colAA /= 4.0;
    }

    void main() {
        vec3 color = renderAAx4();

        fragColor = vec4(color, 1.0);
    }
)text";

static GLuint s_program;
static GLuint s_vao, s_vbo;

static GLint resolutionLoc;
static GLuint tex1;
static GLuint tex2;
static GLuint tex3;
static GLuint tex4;

static GLint loc_mdlvMtx, loc_projMtx;
static GLint loc_lightPos, loc_ambient, loc_diffuse, loc_specular, loc_tex_diffuse;
static GLint loc_time;

static u64 s_startTicks;

static u64 s_lastFrameTime = 0;
static float s_fps = 0.0f;
static int s_frameCount = 0;
static u64 s_fpsUpdateTime = 0;

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

void frRamSceneInit() {
    GLint vsh = createAndCompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLint fsh = createAndCompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    s_program = glCreateProgram();
    glAttachShader(s_program, vsh);
    glAttachShader(s_program, fsh);
    glLinkProgram(s_program);
    resolutionLoc = glGetUniformLocation(s_program, "u_resolution");
    GLuint tex1Loc = glGetUniformLocation(s_program, "u_texture1");
    GLuint tex2Loc = glGetUniformLocation(s_program, "u_texture2");
    GLuint tex3Loc = glGetUniformLocation(s_program, "u_texture3");
    GLuint tex4Loc = glGetUniformLocation(s_program, "u_texture4");
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
    loc_lightPos = glGetUniformLocation(s_program, "lightPos");
    loc_ambient = glGetUniformLocation(s_program, "ambient");
    loc_diffuse = glGetUniformLocation(s_program, "diffuse");
    loc_specular = glGetUniformLocation(s_program, "specular");
    loc_tex_diffuse = glGetUniformLocation(s_program, "tex_diffuse");
    loc_time = glGetUniformLocation(s_program, "u_time");

    struct Vertex {
        float position[3];
        float color[3];
        glm::vec2 texcoord;
        glm::vec3 normal;
    };

    static const Vertex vertices[] = {
        { { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);

    glBindVertexArray(s_vao);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, color));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texcoord));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    glGenerateMipmap(GL_TEXTURE_2D);

    int width, height, nchan;
    stbi_set_flip_vertically_on_load(true);

    glGenTextures(1, &tex1);
    glBindTexture(GL_TEXTURE_2D, tex1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    stbi_uc *img = stbi_load_from_memory((const stbi_uc *)fur_png, fur_png_size, &width, &height, &nchan, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);

    glGenTextures(1, &tex2);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    img = stbi_load_from_memory((const stbi_uc *)noise_png, noise_png_size, &width, &height, &nchan, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);

    glGenTextures(1, &tex3);
    glBindTexture(GL_TEXTURE_2D, tex3);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    img = stbi_load_from_memory((const stbi_uc *)wall_png, wall_png_size, &width, &height, &nchan, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);

    glGenTextures(1, &tex4);
    glBindTexture(GL_TEXTURE_2D, tex4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    img = stbi_load_from_memory((const stbi_uc *)wunk_png, wunk_png_size, &width, &height, &nchan, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);

    glUseProgram(s_program);
    auto projMtx = glm::perspective(glm::radians(40.0f), 1280.0f / 720.0f, 0.01f, 500.0f);
    glUniformMatrix4fv(loc_projMtx, 1, GL_FALSE, glm::value_ptr(projMtx));
    glUniform4f(loc_lightPos, 0.0f, 0.0f, 0.5f, 1.0f);
    glUniform3f(loc_ambient, 0.1f, 0.1f, 0.1f);
    glUniform3f(loc_diffuse, 0.4f, 0.4f, 0.4f);
    glUniform4f(loc_specular, 0.5f, 0.5f, 0.5f, 20.0f);

    s_startTicks = armGetSystemTick();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex1);
    glUniform1i(tex1Loc, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glUniform1i(tex2Loc, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex3);
    glUniform1i(tex3Loc, 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, tex4);
    glUniform1i(tex4Loc, 3);

    s_lastFrameTime = s_startTicks;
    s_fpsUpdateTime = s_startTicks;
    s_frameCount = 0;

    initTextRenderer();
}
float getTime1() {
    u64 elapsed = armGetSystemTick() - s_startTicks;
    return (elapsed * 625 / 12) / 2000000000.0;
}

void frRamRender() {

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

    glViewport(0, 0, 1280, 720);
    glUniform1f(loc_time, getTime1());
    glUniform2f(resolutionLoc, 640.0f, 360.0f);
    glBindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    char fpsText[32];
    snprintf(fpsText, sizeof(fpsText), "%.3f", s_fps);
    drawText(fpsText, -0.95f, 0.90f, 0.02f, 1.0f, 0.0f, 0.0f);
}

void frRamExit() {
    cleanupTextRenderer();
    glDeleteBuffers(1, &s_vbo);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteProgram(s_program);
}

int frRamMain(int argc, char *argv[]) {

    setMesaConfig();

    if (!initEgl(nwindowGetDefault()))
        return EXIT_FAILURE;

    gladLoadGLLoader((GLADloadproc)eglGetProcAddress);

    frRamSceneInit();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    while (appletMainLoop()) {

        padUpdate(&pad);
        u32 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_B) {
            frRamExit();
            deinitEgl();
            state = STATE_MENU;
            return 0;
        }

        frRamRender();
        eglSwapBuffers(s_display, s_surface);
    }

    frRamExit();

    deinitEgl();
    return EXIT_SUCCESS;
}
