#version 330 core

in vec2 v_texcoord;
in vec4 v_color;

out vec4 frag_color;

uniform sampler2D u_texture;

void main() {
    float alpha = texture(u_texture, v_texcoord).r;
    frag_color = vec4(v_color.rgb, v_color.a * alpha);
}
