$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// u_fog = (density at the core, scatter at the core, unused, unused)
uniform vec4 u_fog;
// u_fogColor.rgb = the medium's colour, 0..1 per channel. White is neutral; a darker channel
// extinguishes faster (sunsets, tinted nebulae). Alpha unused.
uniform vec4 u_fogColor;

// Soft radial MEDIUM — fragment stage (lighting A4).
//
// POURQUOI ce shader existe : `render:fog` is a RECTANGLE of uniform density, which is the right
// primitive for a fog bank or a room full of smoke but cannot be a nebula — its edges are hard and
// its interior is flat. Stacking rects to fake a gradient was measured and produces a visible
// ziggurat: 14 concentric rectangle outlines, not a cloud (blog/91 probe, discarded).
//
// So the density here VARIES across the quad, and the falloff is the same squared curve a lamp uses
// (grove::light::attenuation): it reaches EXACTLY zero at the rim, which is what makes the bounding
// quad sufficient — the medium writes pure vacuum at its own edge, so no square is ever visible.
//
// COMMENT: it reuses vs_light, which already scales a unit quad by a radius and hands the fragment
// stage its position in the light's own -1..1 disc. `u_light` therefore carries (cx, cy, radius) for
// a nebula too — an odd name here, kept rather than duplicating a vertex shader that would differ
// by nothing that matters.
void main()
{
	float d = length(v_texcoord0);
	float t = max(0.0, 1.0 - d);
	float f = t * t;                     // 1 at the core, exactly 0 at the rim

	// Beer-Lambert, per channel, scaled by the local density. This is fogPerUnit(density * f, colour)
	// written out: alpha_c = density / colour_c, and the falloff multiplies the exponent — so the
	// core absorbs at the stated density and the edge absorbs nothing at all.
	vec3 alpha = vec3_splat(u_fog.x) / max(u_fogColor.rgb, vec3_splat(0.004));
	vec3 perUnit = exp(-alpha * f);

	// Alpha carries 1 - scatter, which the multiplicative blend accumulates into PROD(1 - scatter_i)
	// across overlapping media — the composite recovers the total as 1 - alpha. At the rim f is 0, so
	// this is exactly 1: no scattering, no seam.
	gl_FragColor = vec4(perUnit, 1.0 - u_fog.y * f);

	vec4 unused = v_color0;   // vs_light packs march UVs here; a medium has no ray to march
}
