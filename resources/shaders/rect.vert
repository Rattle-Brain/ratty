#version 150 core

in vec2 a_position;
in vec4 a_color;

out vec4 v_color;

uniform mat4 u_projection;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_color = a_color;
}
