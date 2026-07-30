$input v_texcoord0

#include <bgfx_shader.sh>

// La frame composée en HDR (ce que le composite a écrit) et la lueur floutée, réduite.
SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);

// u_present = (bloomIntensity, exposure, tonemapMode, 0)
//   tonemapMode : 0 = aucun, 1 = reinhard, 2 = aces — même énumération que grove::light::TonemapMode.
uniform vec4 u_present;

// Présentation (plans B puis T) — étage fragment. La QUEUE du post-traitement.
//
// QUOI  : `backbuffer = tonemap( (frame_composée + lueur × intensité) × exposition )`.
//         C'est la dernière passe du monde ; le HUD est soumis APRÈS elle et ne brille donc pas, et ne
//         subit pas non plus la courbe — une interface doit rester lisible quelle que soit l'exposition
//         de la scène.
//
// POURQUOI une passe à part : le bloom a besoin de lire la frame composée, et un backbuffer ne
//         s'échantillonne pas. Le composite doit donc écrire dans une cible, et il faut ensuite
//         quelqu'un pour l'amener à l'écran. Cette passe a été écrite en annonçant qu'elle
//         accueillerait le tonemapping, les fondus et la colorimétrie ; le tonemapping y est.
//
// ⚠️ L'ORDRE EST LOAD-BEARING : la lueur est ajoutée AVANT la courbe, jamais après. La lueur est faite
//    du sur-brillant ; l'ajouter à une image déjà comprimée dans [0,1] la ferait ressortir au-dessus
//    de 1, donc réécrêter — et le halo serait un aplat blanc collé sur l'image au lieu de participer à
//    l'exposition. C'est la différence entre « une tache » et « une source lumineuse ».
//
// COMMENT la lueur : ADDITIVE, comme la lumière elle-même. Un mélange l'imposerait au prix de la
//         scène — une lueur blanche DÉLAVERAIT ce qu'elle entoure, et un halo dans le vide (où la
//         scène est noire) n'existerait pas du tout. Même raisonnement que le terme de diffusion du
//         composite (plan A2).
//
// COMMENT le tonemap : PAR CANAL, et pas sur la luminance. Comprimer la luminance puis rééchelonner le
//         RGB préserverait la saturation des hautes lumières et donnerait des halos fluo. Par canal,
//         une couleur saturée roule vers le blanc en saturant — ce que fait un film, et ce qu'on veut :
//         un cœur de lampe DOIT blanchir en son centre tout en gardant sa teinte sur les bords.
//
// Les courbes miment grove::light::tonemapReinhard / tonemapACES, dont l'oracle CPU est la source de
// vérité (TonemapMathUnit). Plans : lighting-bloom.md, lighting-tonemap.md.
void main()
{
	vec3 scene = texture2D(s_scene, v_texcoord0).rgb;
	vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;

	vec3 c = scene + bloom * u_present.x;

	float mode = u_present.z;

	// Mode 0 = IDENTITÉ EXACTE, exposition comprise. C'est le contournement à coût nul jusque dans le
	// shader : un jeu qui n'a pas demandé de courbe ne doit pas voir son image bouger d'un LSB, et
	// l'exposition elle-même n'a aucun sens sans courbe pour la recevoir (elle ne ferait que saturer
	// plus tôt).
	if (mode > 0.5)
	{
		c = max(c * u_present.y, vec3_splat(0.0));

		if (mode > 1.5)
		{
			// ACES, approximation de Narkowicz. Constantes reproduites depuis l'oracle : ce sont un
			// ajustement empirique, pas une dérivation.
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

	gl_FragColor = vec4(c, 1.0);
}
