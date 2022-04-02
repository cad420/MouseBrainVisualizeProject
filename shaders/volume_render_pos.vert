#version 460 core

layout(location = 0) in vec3 iVertexPos;

layout(location = 0) out vec3 oVertexPos;


out gl_PerVertex {
    vec4 gl_Position;
};

layout(binding = 0) uniform MVPMatrix {
    mat4 mvp;
    mat4 model;
} mvpMatrix;

void main() {
    oVertexPos = vec3(mvpMatrix.model * vec4(iVertexPos,1.f));
    gl_Position = mvpMatrix.mvp * vec4(iVertexPos,1.f);
}
