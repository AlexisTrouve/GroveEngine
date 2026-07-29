$input a_position, a_color0
$output v_texcoord0, v_color0

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

	// Screen UVs of THIS fragment and of the light centre, packed into the (otherwise unused)
	// colour varying. The fragment stage marches between the two through the occlusion map, so it
	// needs both in the SAME space -- and screen space is the one the occlusion map lives in, which
	// spares an inverse transform per pixel.
	//
	// The light centre is constant across the quad, so interpolating it is exact, not an
	// approximation: a constant interpolates to itself.
	vec4 clipLight = mul(u_modelViewProj, vec4(u_light.xy, 0.0, 1.0));
	vec2 uvFrag  = gl_Position.xy / gl_Position.w * 0.5 + 0.5;
	vec2 uvLight = clipLight.xy / clipLight.w * 0.5 + 0.5;

	// Same render-target origin split as the composite: GL samples from the bottom-left, D3D and
	// Metal from the top-left. Without this the march would walk the map upside down on one family
	// of backends -- and only on that family.
#if !BGFX_SHADER_LANGUAGE_GLSL
	uvFrag.y  = 1.0 - uvFrag.y;
	uvLight.y = 1.0 - uvLight.y;
#endif

	v_color0 = vec4(uvFrag, uvLight);

	vec4 unused = a_color0;
}
