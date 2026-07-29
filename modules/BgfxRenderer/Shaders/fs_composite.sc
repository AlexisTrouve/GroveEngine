$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_light, 1);

uniform vec4 u_ambient;

// Full-screen composite — fragment stage.
//
//   final.rgb = scene.rgb * (ambient.rgb + light.rgb)
//
// The light buffer is ADDITIVE (each lamp adds its contribution) and RGBA16F, so its values may go
// PAST 1.0 where lamps overlap. That overbright is deliberately NOT clamped here: it is what the
// bloom pass will extract later. Clamping at this point would throw the information away exactly
// where it is still cheap to keep.
//
// Alpha is forced to 1.0: this pass produces the frame's final colour, it does not blend into
// anything, so the scene's alpha has nothing left to mean.
// The light buffer is RESOLVED with a small cross tap, and that is what antialiases shadow edges.
//
// POURQUOI ici et pas dans la passe de lampe : an opaque occluder writes 0, so the march's answer is
// BINARY — one sample inside a wall annihilates the product. No amount of finer stepping softens a
// binary answer, and filtering the occlusion map does not survive the `pow(prod, stepLen)` that turns
// per-unit transmittance into a distance (a half-covered texel is COVERAGE, but the model reads it as
// DENSITY and crushes it). The lamp pass dithers the boundary across neighbouring pixels instead;
// this tap is what turns that dither back into a gradient.
//
// Five taps, one pixel apart, weighted to keep the centre dominant so lamp gradients and the
// overbright peak survive: a box blur would visibly soften every highlight to fix an edge.
#define LIGHT_RESOLVE_W 0.5

void main()
{
	vec4 scene = texture2D(s_scene, v_texcoord0);

	vec2 tx = u_viewTexel.xy;
	vec3 light = texture2D(s_light, v_texcoord0).rgb * (1.0 - LIGHT_RESOLVE_W);
	light += ( texture2D(s_light, v_texcoord0 + vec2( tx.x, 0.0)).rgb
	         + texture2D(s_light, v_texcoord0 + vec2(-tx.x, 0.0)).rgb
	         + texture2D(s_light, v_texcoord0 + vec2(0.0,  tx.y)).rgb
	         + texture2D(s_light, v_texcoord0 + vec2(0.0, -tx.y)).rgb ) * (LIGHT_RESOLVE_W * 0.25);

	vec3 lit = scene.rgb * (u_ambient.rgb + light);

	gl_FragColor = vec4(lit, 1.0);
}
