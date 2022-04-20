#version 460 core

layout(location = 0) in vec3 iFragPos;

layout(location = 0) out vec4 oEntryPos;

layout(location = 1) out vec4 oExitPos;

layout(binding = 1) uniform ViewPos {
    vec4 view_pos;
} viewPos;

void main() {
    bool inner = viewPos.view_pos.w == 1.f;
    if(gl_FrontFacing){
        if(inner){
            oEntryPos =  vec4(0.f);//inner must has back face fragement
        }
        else{
            oEntryPos = vec4(iFragPos,1.f);
        }
        oExitPos = vec4(0.f);
    }
    else{
        if(inner){
            oEntryPos = viewPos.view_pos;
        }
        else {
            oEntryPos = vec4(0.f);
        }
        oExitPos = vec4(iFragPos,1.f);
    }
}
