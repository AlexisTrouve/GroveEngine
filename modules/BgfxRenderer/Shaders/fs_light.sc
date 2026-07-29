$input v_texcoord0

#include <bgfx_shader.sh>

// u_light = (centre.x, centre.y, radius, intensity)
uniform vec4 u_light;
// u_lightColor.rgb = colour 0..1 (alpha unused: a light ADDS, it does not blend)
uniform vec4 u_lightColor;
// u_lightCone = (axis.x, axis.y, cosOuter, cosInner) -- precomputed on the CPU.
// Omni is NOT a branch: it ships cosOuter = cosInner = -1, which passes every direction.
uniform vec4 u_lightCone;

// Radial light — fragment stage.
//
// v_texcoord0 is the position inside the light's own unit disc, so the distance to the centre is
// simply its length and the rim sits at exactly 1. The curve mirrors grove::light::attenuation
// (include/grove/light/Light.h), which is oracle-tested headlessly — this is the copy that runs on
// the GPU, and LightMathUnit is what keeps the two honest.
//
//   attenuation = (1 - d)^2   for d < 1, else 0
//
// Squared, not linear: a linear falloff reads as a hard-edged disc because the eye catches the
// derivative break at the rim. And it must reach EXACTLY zero at d == 1, otherwise light would leak
// past the quad and every lamp would show a square seam.
//
// The result is NOT clamped. Overlapping lamps are supposed to push the additive RGBA16F target past
// 1.0 — that overbright is what the bloom pass will extract.
void main()
{
	float d = length(v_texcoord0);
	float t = max(0.0, 1.0 - d);
	float a = t * t * u_light.w;          // squared falloff, scaled by intensity

	// CONE mask (L3). Works on the COSINE of the angle to the axis -- a dot product, never atan2.
	// cosine DECREASES as the angle grows, so the smoothstep runs from cosOuter up to cosInner;
	// writing it the other way round would invert the rim and light everything EXCEPT the cone.
	// Mirrors grove::light::coneFactor, which LightMathUnit pins headlessly.
	vec2 dir = (d > 0.0) ? (v_texcoord0 / d) : u_lightCone.xy;
	float cosA = dot(dir, u_lightCone.xy);
	a *= smoothstep(u_lightCone.z, u_lightCone.w, cosA);

	gl_FragColor = vec4(u_lightColor.rgb * a, 1.0);
}
