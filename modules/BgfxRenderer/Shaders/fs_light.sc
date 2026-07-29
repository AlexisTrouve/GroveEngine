$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// How far apart the occlusion samples sit, IN SCREEN PIXELS, and the ceiling on how many are taken.
//
// ⚠️ This used to be a fixed COUNT (16), which made the step length `distance / 16` — proportional to
// how far the fragment sat from the lamp. Two defects followed, and both were visible: matter thinner
// than one step was stepped over entirely, and the shadow edge came out as a STAIRCASE whose tread
// was the step length (measured ~19 px under a 340-unit lamp). Worse, the defect scaled WITH the
// lamp: doubling a light's radius doubled the ugliness, so no fixed count could ever be right.
//
// A step is now a constant number of pixels, so edge quality no longer depends on the lamp's size —
// a small lamp simply takes fewer samples. The cap bounds the cost of a very large one.
#define OCCLUSION_STEP_PX 3.0
#define MAX_OCCLUSION_STEPS 64

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

	// How many samples THIS fragment needs: the ray's length on screen, divided by the step. Near the
	// lamp that is a handful; at the rim of a wide lamp it saturates at the cap. u_viewRect.zw is the
	// light view's pixel size, which is what turns a UV distance into a pixel distance.
	float pxDist = length((uvFrag - uvLight) * u_viewRect.zw);
	float steps  = clamp(ceil(pxDist / OCCLUSION_STEP_PX), 1.0, float(MAX_OCCLUSION_STEPS));
	vec2  stepUV = (uvFrag - uvLight) / steps;

	// ORDERED DITHER on where the march STARTS (core C2b) — this is what antialiases shadow edges.
	//
	// An occluder writes 0, so the march's verdict is BINARY: one sample inside a wall annihilates the
	// product however many others miss it. Every pixel within a step of the boundary therefore returns
	// the SAME answer and the edge comes out as a staircase (measured tread: 3.9 px). Shrinking the
	// step only shrinks the tread and pays linearly; filtering the occlusion map does not survive the
	// pow() below, which reads a half-covered texel as DENSITY rather than as COVERAGE and crushes it.
	//
	// Offsetting each pixel's samples by a different fraction of a step spreads that one answer across
	// the pixels of a tread; the composite then resolves them into a ramp (fs_composite.sc). Cost: one
	// modulo. An ORDERED pattern rather than a hash on purpose — noise on a still image reads as grain,
	// a fine regular pattern reads as a soft edge.
	//
	// ⚠️ The multipliers are the whole point: NEIGHBOURING pixels must get very DIFFERENT offsets. A
	//    first attempt used mod(x + 3y, 8), which steps by 1/8 per pixel — a gradient, not a dither.
	//    Adjacent pixels then marched almost identically, agreed, and left the resolve nothing to
	//    average. Stepping by 5/8 cycles 0, 5, 2, 7, 4, 1, 6, 3: consecutive pixels land at opposite
	//    ends of the step. Measured: 3.5 px off the fit line before, 1.6 px after.
	//
	// ⚠️ Strictly neutral where there is no matter: in vacuum every sample returns 1 whatever the
	//    offset, so a scene with no occluder is unchanged to the byte.
	vec2 fc = floor(gl_FragCoord.xy);
	float dither = mod(fc.x * 5.0 + fc.y * 3.0, 8.0) * 0.125;

	// ⚠️ [loop] is not decoration — WITHOUT it the adaptive step count is a lie.
	//
	// fxc reads the clamp above, deduces the bound can never exceed 64, and UNROLLS: the D3D11
	// bytecode went from 1.9 KB to 23 KB, and an unrolled loop is predicated, so every lit fragment
	// would pay all 64 samples whatever `steps` said. Forcing a real loop puts it back to 2.2 KB and
	// makes a fragment that needs 8 samples take 8. Same picture (verified pixel-identical), a
	// fraction of the work. Guarded because [loop] is HLSL-only; GLSL and SPIR-V keep the dynamic
	// bound and do not unroll.
	int n = int(steps);
	vec3 prod = vec3_splat(1.0);
#if BGFX_SHADER_LANGUAGE_HLSL
	[loop]
#endif
	for (int i = 1; i <= n; ++i) {
		prod *= texture2D(s_occlusion, uvLight + stepUV * (float(i) - dither)).rgb;
	}

	// World length of one step: the fragment sits at `d` in the light's unit disc, so the distance
	// travelled is d * radius. Dividing by the ACTUAL step count is what keeps t^distance exact —
	// using the cap here instead would make every capped fragment under-absorb.
	float stepLen = (d * u_light.z) / steps;
	vec3 survives = pow(prod, vec3_splat(stepLen));

	gl_FragColor = vec4(u_lightColor.rgb * a * survives, 1.0);
}
