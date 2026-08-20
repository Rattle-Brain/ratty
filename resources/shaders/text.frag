#version 330 core

in vec2 v_texcoord;
in vec4 v_color;
in float v_is_color;

out vec4 frag_color;

uniform sampler2D u_texture;

void main() {
    // The atlas is RGBA. A coverage mask was widened to (1, 1, 1, coverage) on
    // upload, so tinting is a matter of taking the RGB from the vertex colour;
    // a colour glyph keeps its own RGB. Alpha comes from the texture either way,
    // scaled by the requested opacity.
    vec4 texel = texture(u_texture, v_texcoord);

    vec3 rgb = mix(v_color.rgb, texel.rgb, v_is_color);
    frag_color = vec4(rgb, texel.a * v_color.a);
}
