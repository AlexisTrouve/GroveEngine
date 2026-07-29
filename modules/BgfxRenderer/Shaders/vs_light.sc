$input a_position, a_color0
$output v_texcoord0

#include <bgfx_shader.sh>

// u_light = (centre.x, centre.y, radius, intensity)
uniform vec4 u_light;

// Radial light — vertex stage.
//
// The mesh is a UNIT quad spanning -1..1. Here it is scaled by the light's radius and moved to its
// centre, both in WORLD space, then transformed by the light view's camera — which the module binds
// to the same matrices as the world view, so a lamp lands exactly where a sprite at the same
// coordinates lands.
//
// The local -1..1 position rides through as the varying: in that space the rim of the light is
// exactly length == 1, whatever the radius or the zoom. That is what lets the fragment stage compute
// the falloff without ever knowing a world coordinate — no inverse transform, no per-pixel matrix.
void main()
{
	vec2 world = u_light.xy + a_position.xy * u_light.z;
	gl_Position = mul(u_modelViewProj, vec4(world, 0.0, 1.0));

	v_texcoord0 = a_position.xy;

	vec4 unused = a_color0;
}
