#version 460 core
const vec2 VertexPos[6] =vec2[](
vec2(-1,-1), vec2(1,-1), vec2(-1,1),
vec2(1,1), vec2(-1,1), vec2(1,-1)
);
out gl_PerVertex {
    vec4 gl_Position;
};


void main() {
    gl_Position = vec4(VertexPos[gl_VertexIndex],0.0, 1.0);
}
