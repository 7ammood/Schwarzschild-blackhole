#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

using namespace glm;
using namespace std;

int g_width = 800;
int g_height = 600;
float g_aspect = static_cast<float>(g_width) / g_height;
constexpr float FOV = 0.6f;
constexpr int MAX_OBJECTS = 16;

GLuint g_outputTex = 0; 

struct Camera {
    float azimuth = 0.0f;
    float elevation = 1.57f;
    float radius = 8e10f;
    double lastX = 0.0;
    double lastY = 0.0;
    bool firstMouse = true;

    static constexpr float MIN_RADIUS = 1.5e10f;
    static constexpr float SCROLL_SPEED = 3e9f;
    static constexpr float ROTATE_SPEED = 0.005f;
    static constexpr float MIN_ELEVATION = 0.01f;
    static constexpr float MAX_ELEVATION = 3.14f;
};

struct alignas(16) ObjectData {
    vec4 posRadius;
    vec4 color;
    vec4 massPad;
};

Camera cam;


string loadTextFile(const string& path) {
    ifstream file(path);
    if (!file.is_open()) {
        cerr << "FAILED to open shader file: " << path << endl;
        cerr << "  Resolved against working directory: "
            << filesystem::current_path() << endl;
        cerr << "  If that directory doesn't contain a 'shaders/' folder, "
            << "your run/working directory is wrong, not the file itself." << endl;
        return "";
    }
    stringstream ss;
    ss << file.rdbuf();
    string content = ss.str();
    if (content.empty()) {
        cerr << "WARNING: shader file is empty: " << path << endl;
    }
    return content;
}

GLuint compileShader(GLenum type, const string& src) {
    const char* csrc = src.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &csrc, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        cerr << "Shader compilation failed:\n" << log << endl;
    }
    return shader;
}

GLuint compileShaderFromFile(GLenum type, const string& path) {
    string src = loadTextFile(path);
    return compileShader(type, src);
}

GLuint linkProgram(GLuint* shaders, int count) {
    GLuint prog = glCreateProgram();
    for (int i = 0; i < count; i++)
        glAttachShader(prog, shaders[i]);
    glLinkProgram(prog);

    int success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        cerr << "Program linking failed:\n" << log << endl;
        return 0; 
    }
    return prog;
}

void scrollCallback(GLFWwindow*, double, double yoffset) {
    cam.radius -= static_cast<float>(yoffset) * Camera::SCROLL_SPEED;
    cam.radius = std::max(cam.radius, Camera::MIN_RADIUS);
}

void handleMouseInput(GLFWwindow* window) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (cam.firstMouse) {
        cam.lastX = xpos;
        cam.lastY = ypos;
        cam.firstMouse = false;
    }

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        cam.azimuth += static_cast<float>(xpos - cam.lastX) * Camera::ROTATE_SPEED;
        cam.elevation += static_cast<float>(cam.lastY - ypos) * Camera::ROTATE_SPEED;
        cam.elevation = glm::clamp(cam.elevation, Camera::MIN_ELEVATION, Camera::MAX_ELEVATION);
    }

    cam.lastX = xpos;
    cam.lastY = ypos;
}

struct CameraUBOData {
    vec4 pos;
    vec4 right;
    vec4 up;
    vec4 forward;
    float tanHalfFov;
    float aspect;
    float _pad[2];
};

void uploadCameraUBO(GLuint ubo) {
    vec3 p(
        cam.radius * sin(cam.elevation) * cos(cam.azimuth),
        cam.radius * cos(cam.elevation),
        cam.radius * sin(cam.elevation) * sin(cam.azimuth)
    );
    vec3 f = normalize(-p);
    vec3 r = normalize(cross(f, vec3(0.0f, 1.0f, 0.0f)));
    vec3 u = cross(r, f);

    CameraUBOData data = {
        vec4(p, 0.0f),
        vec4(r, 0.0f),
        vec4(u, 0.0f),
        vec4(f, 0.0f),
        tan(FOV),
        g_aspect,
        { 0.0f, 0.0f }
    };

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
}

void uploadObjectsUBO(GLuint ubo, const vector<ObjectData>& objects) {
    int count = static_cast<int>(objects.size());

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(int), &count);
    glBufferSubData(GL_UNIFORM_BUFFER, 16, count * sizeof(ObjectData), objects.data());
}

GLuint createOutputTexture() {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_width, g_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    return tex;
}


void resizeOutputTexture(int width, int height) {
    glBindTexture(GL_TEXTURE_2D, g_outputTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    if (width <= 0 || height <= 0) return;
    g_width = width;
    g_height = height;
    g_aspect = static_cast<float>(width) / height;
    glViewport(0, 0, width, height);
    resizeOutputTexture(width, height);
}

GLuint createUBO(GLuint binding, GLsizeiptr size) {
    GLuint ubo;
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo);
    return ubo;
}

GLuint createFullscreenQuad() {
    float verts[] = { -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1 };

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    return vao;
}

int main() {
    if (!glfwInit()) {
        cerr << "Failed to initialize GLFW" << endl;
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(g_width, g_height, "Black Hole Raytracer", nullptr, nullptr);
    if (!window) {
        cerr << "Failed to create window" << endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    if (glewInit() != GLEW_OK) {
        cerr << "Failed to initialize GLEW" << endl;
        glfwTerminate();
        return -1;
    }

    glfwSetScrollCallback(window, scrollCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    vector<ObjectData> objects = {
        { vec4(0, 0, 0, 1.2e10f),              vec4(0, 0, 0, 1),                  vec4(8.54e36f, 0, 0, 0) },
        { vec4(4e10f, 0, 0, 4e9f),              vec4(1.0f, 0.4f, 0.1f, 1.0f),     vec4(1.9e30f,  0, 0, 0) },
        { vec4(-3e10f, 1e10f, -2e10f, 8e9f),    vec4(0.2f, 0.6f, 1.0f, 1.0f),     vec4(1.9e30f,  0, 0, 0) },
        { vec4(0, -1.5e10f, 4e10f, 10e9f),      vec4(0.2f, 0.9f, 0.3f, 1.0f),     vec4(1.0e28f,  0, 0, 0) },
        { vec4(2e10f, -2e10f, -3e10f, 5e9f),    vec4(0.6f, 0.2f, 0.9f, 1.0f),     vec4(1.0e28f,  0, 0, 0) },
    };

    glViewport(0, 0, g_width, g_height);
    g_outputTex = createOutputTexture();
    GLuint camUBO = createUBO(1, sizeof(CameraUBOData));
    GLuint objUBO = createUBO(3, 16 + MAX_OBJECTS * sizeof(ObjectData));
    GLuint quadVAO = createFullscreenQuad();


    GLuint cs = compileShaderFromFile(GL_COMPUTE_SHADER, "shaders/raytrace.comp");
    GLuint computeProg = linkProgram(&cs, 1);

    GLuint vs = compileShaderFromFile(GL_VERTEX_SHADER, "shaders/quad.vert");
    GLuint fs = compileShaderFromFile(GL_FRAGMENT_SHADER, "shaders/quad.frag");
    GLuint renderShaders[] = { vs, fs };
    GLuint renderProg = linkProgram(renderShaders, 2);

    if (computeProg == 0 || renderProg == 0) {
        cerr << "\nOne or more shader programs failed to build — see errors above.\n"
            << "This is the #1 cause of a blank/white output: the shader source\n"
            << "was empty (file not found) or failed to compile. Fix that before\n"
            << "anything else; the render loop will not produce useful output\n"
            << "with a broken program bound.\n" << endl;
        glfwTerminate();
        return -1;
    }

    GLint uResolutionLoc = glGetUniformLocation(computeProg, "uResolution");

    while (!glfwWindowShouldClose(window)) {
        handleMouseInput(window);
        uploadCameraUBO(camUBO);
        uploadObjectsUBO(objUBO, objects);

        glUseProgram(computeProg);
        glUniform2i(uResolutionLoc, g_width, g_height);
        glBindImageTexture(0, g_outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);


        GLuint groupsX = (g_width + 15) / 16;
        GLuint groupsY = (g_height + 15) / 16;
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        glUseProgram(renderProg);
        glBindTexture(GL_TEXTURE_2D, g_outputTex);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}