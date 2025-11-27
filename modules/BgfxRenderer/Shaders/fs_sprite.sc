$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // Sample texture and multiply by vertex/instance color
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    gl_FragColor = texColor * v_color0;
}
