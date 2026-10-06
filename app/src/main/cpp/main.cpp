// Minha Engine - Etapa 1: janela, loop, camera orbital e cubo iluminado
#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <cmath>
#include <vector>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Engine", __VA_ARGS__)

// ---------- Matematica ----------
struct Mat4 { float m[16]; };

static Mat4 identity() { Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }

static Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

static Mat4 perspective(float fov, float aspect, float n, float f) {
    Mat4 r{};
    float t = 1.0f / tanf(fov * 0.5f);
    r.m[0] = t / aspect; r.m[5] = t;
    r.m[10] = (f + n) / (n - f); r.m[11] = -1;
    r.m[14] = 2 * f * n / (n - f);
    return r;
}

static Mat4 translate(float x, float y, float z) {
    Mat4 r = identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r;
}

static Mat4 rotX(float a) {
    Mat4 r = identity(); float c = cosf(a), s = sinf(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r;
}

static Mat4 rotY(float a) {
    Mat4 r = identity(); float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r;
}

// ---------- Shaders ----------
static const char* VS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
void main() {
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* FS = R"(#version 300 es
precision mediump float;
in vec3 vNormal;
uniform vec3 uLightDir;
out vec4 outColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(-uLightDir)), 0.0);
    vec3 base = 0.5 + 0.5 * n;
    vec3 col = base * (0.25 + 0.85 * diff);
    outColor = vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
}
)";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf); LOG("Shader erro: %s", buf); }
    return s;
}

// ---------- Estado ----------
struct Engine {
    android_app* app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0, height = 0;
    bool active = false;

    GLuint prog = 0, vao = 0, vbo = 0, ibo = 0;
    GLint uMVP = -1, uModel = -1, uLight = -1;

    float yaw = 0.6f, pitch = 0.4f, dist = 3.5f;
    float lastX = 0, lastY = 0;
    bool dragging = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

static void buildCube(Engine& e) {
    const float F[6][9] = {
        { 1, 0, 0,  0, 1, 0,  0, 0, 1}, {-1, 0, 0,  0, 0, 1,  0, 1, 0},
        { 0, 1, 0,  0, 0, 1,  1, 0, 0}, { 0,-1, 0,  1, 0, 0,  0, 0, 1},
        { 0, 0, 1,  1, 0, 0,  0, 1, 0}, { 0, 0,-1,  0, 1, 0,  1, 0, 0},
    };
    const int S[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    std::vector<float> vtx; std::vector<unsigned short> idx;
    for (int f = 0; f < 6; f++) {
        const float* d = F[f];
        for (int c = 0; c < 4; c++) {
            for (int i = 0; i < 3; i++)
                vtx.push_back(0.5f * (d[i] + S[c][0] * d[3 + i] + S[c][1] * d[6 + i]));
            for (int i = 0; i < 3; i++) vtx.push_back(d[i]);
        }
        unsigned short b = f * 4;
        unsigned short q[6] = {b, (unsigned short)(b+1), (unsigned short)(b+2),
                               b, (unsigned short)(b+2), (unsigned short)(b+3)};
        idx.insert(idx.end(), q, q + 6);
    }
    glGenVertexArrays(1, &e.vao); glBindVertexArray(e.vao);
    glGenBuffers(1, &e.vbo); glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
    glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(float), vtx.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &e.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, e.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned short), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
}

static bool initGL(Engine& e) {
    e.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(e.display, nullptr, nullptr);
    const EGLint cfgAttr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
    EGLConfig cfg; EGLint n = 0;
    eglChooseConfig(e.display, cfgAttr, &cfg, 1, &n);
    if (n < 1) { LOG("Sem config EGL"); return false; }
    e.surface = eglCreateWindowSurface(e.display, cfg, e.app->window, nullptr);
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    e.context = eglCreateContext(e.display, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (!eglMakeCurrent(e.display, e.surface, e.surface, e.context)) return false;
    eglQuerySurface(e.display, e.surface, EGL_WIDTH, &e.width);
    eglQuerySurface(e.display, e.surface, EGL_HEIGHT, &e.height);

    e.prog = glCreateProgram();
    glAttachShader(e.prog, compile(GL_VERTEX_SHADER, VS));
    glAttachShader(e.prog, compile(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(e.prog);
    e.uMVP = glGetUniformLocation(e.prog, "uMVP");
    e.uModel = glGetUniformLocation(e.prog, "uModel");
    e.uLight = glGetUniformLocation(e.prog, "uLightDir");
    buildCube(e);
    glEnable(GL_DEPTH_TEST);
    LOG("GL pronto: %dx%d | %s", e.width, e.height, glGetString(GL_RENDERER));
    return true;
}

static void termGL(Engine& e) {
    if (e.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (e.context != EGL_NO_CONTEXT) eglDestroyContext(e.display, e.context);
        if (e.surface != EGL_NO_SURFACE) eglDestroySurface(e.display, e.surface);
        eglTerminate(e.display);
    }
    e.display = EGL_NO_DISPLAY; e.context = EGL_NO_CONTEXT; e.surface = EGL_NO_SURFACE;
    e.active = false;
}

static void draw(Engine& e) {
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - e.start).count();
    glViewport(0, 0, e.width, e.height);
    glClearColor(0.05f, 0.07f, 0.10f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Mat4 model = mul(rotY(t * 0.5f), rotX(t * 0.3f));
    Mat4 view = mul(translate(0, 0, -e.dist), mul(rotX(e.pitch), rotY(e.yaw)));
    Mat4 proj = perspective(1.05f, (float)e.width / e.height, 0.1f, 100.0f);
    Mat4 mvp = mul(proj, mul(view, model));

    glUseProgram(e.prog);
    glUniformMatrix4fv(e.uMVP, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(e.uModel, 1, GL_FALSE, model.m);
    glUniform3f(e.uLight, -0.4f, -0.8f, -0.5f);
    glBindVertexArray(e.vao);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr);
    eglSwapBuffers(e.display, e.surface);
}

// ---------- Entrada e ciclo de vida ----------
static int onInput(android_app* app, AInputEvent* ev) {
    Engine* e = (Engine*)app->userData;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    float x = AMotionEvent_getX(ev, 0), y = AMotionEvent_getY(ev, 0);
    switch (AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK) {
        case AMOTION_EVENT_ACTION_DOWN: e->dragging = true; break;
        case AMOTION_EVENT_ACTION_UP: e->dragging = false; break;
        case AMOTION_EVENT_ACTION_MOVE:
            if (e->dragging) {
                e->yaw += (x - e->lastX) * 0.01f;
                e->pitch += (y - e->lastY) * 0.01f;
                if (e->pitch > 1.5f) e->pitch = 1.5f;
                if (e->pitch < -1.5f) e->pitch = -1.5f;
            }
            break;
    }
    e->lastX = x; e->lastY = y;
    return 1;
}

static void onCmd(android_app* app, int32_t cmd) {
    Engine* e = (Engine*)app->userData;
    if (cmd == APP_CMD_INIT_WINDOW && app->window) e->active = initGL(*e);
    else if (cmd == APP_CMD_TERM_WINDOW) termGL(*e);
}

void android_main(android_app* app) {
    Engine engine;
    engine.app = app;
    app->userData = &engine;
    app->onAppCmd = onCmd;
    app->onInputEvent = onInput;

    while (true) {
        int events; android_poll_source* src;
        while (ALooper_pollOnce(engine.active ? 0 : -1, nullptr, &events, (void**)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) { termGL(engine); return; }
        }
        if (engine.active) draw(engine);
    }
}
