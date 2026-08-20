#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_color;
// 0 for a coverage mask to be tinted with a_color, 1 for a colour glyph
// (emoji) whose texels are already the final colour.
layout(location = 3) in float a_is_color;

out vec2 v_texcoord;
out vec4 v_color;
out float v_is_color;

uniform mat4 u_projection;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_is_color = a_is_color;
}
