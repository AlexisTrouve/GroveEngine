$input v_texcoord0

#include <bgfx_shader.sh>

// La frame composée en HDR (ce que le composite a écrit) et la lueur floutée, réduite.
SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);

// u_present = (bloomIntensity, exposure, tonemapMode, 0)
//   tonemapMode : 0 = aucun, 1 = reinhard, 2 = aces — même énumération que grove::light::TonemapMode.
uniform vec4 u_present;

// u_grade0 = (tintR, tintG, tintB, contrast)
// u_grade1 = (saturation, 0, 0, 0)
uniform vec4 u_grade0;
uniform vec4 u_grade1;

// Présentation (plans B, T puis G) — étage fragment. LA QUEUE du post-traitement, au complet.
//
// QUOI  : `backbuffer = grade( tonemap( (composée + lueur × intensité) × exposition ) )`.
//         C'est la dernière passe du MONDE ; le HUD est soumis après elle, donc il ne brille pas, ne
//         subit pas la courbe et n'est pas étalonné. Le FONDU, lui, passe encore après et recouvre
//         tout — les deux comportements sont opposés et chacun est juste pour son effet.
//
// ⚠️ L'ORDRE EST LOAD-BEARING de bout en bout :
//    - la lueur AVANT la courbe : sinon elle ressortirait au-dessus de 1 et réécrêterait, donnant un
//      aplat blanc collé sur l'image au lieu d'une source lumineuse ;
//    - l'étalonnage APRÈS la courbe : il opère dans un espace d'affichage borné à [0,1], ce qui est la
//      raison pour laquelle son pivot de contraste est 0,5 et non 0,18 ;
//    - et dans l'étalonnage : teinte → contraste → saturation, l'ordre d'un étalonnage réel. Teinter
//      après avoir désaturé donnerait un virage sépia au lieu d'une image équilibrée puis désaturée.
//
// Les courbes miment grove::light::tonemapReinhard/tonemapACES et grove::light::gradeColor, dont les
// oracles CPU sont la source de vérité (TonemapMathUnit, GradeMathUnit).
// Plans : lighting-bloom.md, lighting-tonemap.md, lighting-grade.md.
void main()
{
	vec3 scene = texture2D(s_scene, v_texcoord0).rgb;
	vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;

	vec3 c = scene + bloom * u_present.x;

	float mode = u_present.z;

	// Mode 0 = IDENTITÉ EXACTE, exposition comprise. Le contournement à coût nul jusque dans le shader.
	if (mode > 0.5)
	{
		c = max(c * u_present.y, vec3_splat(0.0));

		if (mode > 1.5)
		{
			// ACES, approximation de Narkowicz. Constantes reproduites depuis l'oracle : un ajustement
			// empirique, pas une dérivation.
			vec3 num = c * (2.51 * c + 0.03);
			vec3 den = c * (2.43 * c + 0.59) + 0.14;
			c = clamp(num / den, 0.0, 1.0);
		}
		else
		{
			// Reinhard : x / (1 + x). Tend vers 1 sans jamais l'atteindre, donc aucun réécrêtage.
			c = c / (1.0 + c);
		}
	}

	// ---- Colorimétrie (plan G) ---------------------------------------------------------------
	// 1. teinte, 2. contraste autour du gris moyen, 3. saturation vers la LUMINANCE (celle du bloom,
	// pas une moyenne plate : un rouge pur et un bleu pur doivent donner des gris DIFFÉRENTS).
	c *= u_grade0.rgb;
	c = (c - vec3_splat(0.5)) * u_grade0.w + vec3_splat(0.5);
	float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
	c = vec3_splat(luma) + (c - vec3_splat(luma)) * u_grade1.x;

	// Borne finale, UNE SEULE FOIS : le contraste peut sortir de la plage — (0 − 0,5)·2 + 0,5 = −0,5.
	// Écrêter entre chaque étape détruirait un intermédiaire hors plage que la suite ramène dedans.
	gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
