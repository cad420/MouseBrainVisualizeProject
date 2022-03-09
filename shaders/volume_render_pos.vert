#version 460 core

layout(location = 0) in vec3 iVertexPos;

layout(location = 0) out vec3 oVertexPos;


out gl_PerVertex {
    vec4 gl_Position;
};

layout(push_constant) uniform PushConsts {
    mat4 mvp;
    mat4 model;
} vPushConsts;

void main() {
    oVertexPos = vec3(vPushConsts.model * vec4(iVertexPos,1.f));
    gl_Position = vPushConsts.mvp * vec4(iVertexPos,1.f);
}
