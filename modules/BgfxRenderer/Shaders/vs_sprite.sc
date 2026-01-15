$input a_position, a_color0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

// Instanced sprite shader with texture support
// Instance data layout (5 x vec4 = 80 bytes):
// i_data0: x, y, scaleX, scaleY
// i_data1: rotation, u0, v0, u1
// i_data2: v1, textureId, layer, padding
// i_data3: reserved
// i_data4: r, g, b, a (color)

void main()
{
    // Extract transform from instance data
    vec2 pos = i_data0.xy;
    vec2 scale = i_data0.zw;
    float rot = i_data1.x;

    // Extract UVs from instance data
    float u0 = i_data1.y;
    float v0 = i_data1.z;
    float u1 = i_data1.w;
    float v1 = i_data2.x;

    float c = cos(rot);
    float s = sin(rot);

    // Transform vertex (quad is 0-1, center at 0.5)
    vec2 localPos = a_position.xy - vec2(0.5, 0.5);
    vec2 rotatedPos;
    rotatedPos.x = localPos.x * c - localPos.y * s;
    rotatedPos.y = localPos.x * s + localPos.y * c;
    vec2 worldPos = rotatedPos * scale + pos;

    gl_Position = mul(u_viewProj, vec4(worldPos, 0.0, 1.0));

    // Interpolate UVs based on vertex position in quad (0-1)
    // a_position.xy is the vertex position (0,0), (1,0), (1,1), (0,1)
    v_texcoord0 = mix(vec2(u0, v0), vec2(u1, v1), a_position.xy);

    // Color = vertex color * instance color
    v_color0 = a_color0 * i_data4;
}
