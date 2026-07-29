$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Steps taken through the occlusion map per fragment. A tuning knob, not dogma: too few and a thin
// wall is stepped over, too many and the cost is paid on every lit pixel.
#define OCCLUSION_STEPS 16

// u_light = (centre.x, centre.y, radius, intensity)
uniform vec4 u_light;
// u_lightColor.rgb = colour 0..1 (alpha unused: a light ADDS, it does not blend)
uniform vec4 u_lightColor;
// u_lightCone = (axis.x, axis.y, cosOuter, cosInner) -- precomputed on the CPU.
// Omni is NOT a branch: it ships cosOuter = cosInner = -1, which passes every direction.
uniform vec4 u_lightCone;

// Per-pixel transmittance of the matter light travels through (lighting core C2). Each texel holds
// the transmittance PER UNIT of length; an all-white map is vacuum and changes nothing.
SAMPLER2D(s_occlusion, 0);

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

	// OCCLUSION (core C2). March the occlusion map from the lamp to this fragment and accumulate
	// what survives. A wall writes 0 and kills the ray; a filter writes a colour and tints it; fog
	// writes exp(-alpha). One mechanism, three plans -- see lighting-transmittance-core.md.
	//
	// Each texel is transmittance PER UNIT, so a step of length s contributes t^s. Since every step
	// here has the SAME length, (t0*t1*...)^s equals the product of each t^s -- so the product is
	// accumulated first and raised ONCE, instead of a pow per step. Exact, not an approximation.
	vec2 uvFrag  = v_color0.xy;
	vec2 uvLight = v_color0.zw;
	vec2 stepUV  = (uvFrag - uvLight) / float(OCCLUSION_STEPS);

	vec3 prod = vec3_splat(1.0);
	for (int i = 1; i <= OCCLUSION_STEPS; ++i) {
		prod *= texture2D(s_occlusion, uvLight + stepUV * float(i)).rgb;
	}

	// World length of one step: the fragment sits at `d` in the light's unit disc, so the distance
	// travelled is d * radius.
	float stepLen = (d * u_light.z) / float(OCCLUSION_STEPS);
	vec3 survives = pow(prod, vec3_splat(stepLen));

	gl_FragColor = vec4(u_lightColor.rgb * a * survives, 1.0);
}
