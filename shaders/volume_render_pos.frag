#version 460 core

layout(location = 0) in vec3 iFragPos;

layout(push_constant) uniform PushConsts {
    layout(offset = 128) vec4 viewPos;
} fPushConsts;

void main() {

}
