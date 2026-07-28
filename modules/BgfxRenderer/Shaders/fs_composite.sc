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
void main()
{
	vec4 scene = texture2D(s_scene, v_texcoord0);
	vec4 light = texture2D(s_light, v_texcoord0);

	vec3 lit = scene.rgb * (u_ambient.rgb + light.rgb);

	gl_FragColor = vec4(lit, 1.0);
}
