#version 150 core

in vec2 v_texcoord;
in vec4 v_color;

out vec4 frag_color;

uniform sampler2D u_texture;

void main() {
    // Sample GL_RED texture with swizzle mask (R,R,R,1)
    // The .r channel contains the glyph intensity
    float alpha = texture(u_texture, v_texcoord).r;
    frag_color = vec4(v_color.rgb, v_color.a * alpha);
}
