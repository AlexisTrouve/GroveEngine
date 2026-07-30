$input v_texcoord0

#include <bgfx_shader.sh>

// La frame composée en HDR (ce que le composite a écrit) et la lueur floutée au quart de résolution.
SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);

// u_present = (bloomIntensity, 0, 0, 0)
uniform vec4 u_present;

// Présentation (plan B, tranche B2) — étage fragment.
//
// QUOI  : `backbuffer = frame_composée + lueur × intensité`. C'est la dernière passe du monde ; le HUD
//         est soumis APRÈS elle et ne brille donc pas.
//
// POURQUOI une passe à part alors qu'elle ne fait qu'une addition : parce que le bloom a besoin de
//         lire la frame composée, et qu'un backbuffer ne s'échantillonne pas. Le composite doit donc
//         écrire dans une cible, et il faut ensuite quelqu'un pour amener cette cible à l'écran.
//         C'est aussi, et ce n'est pas un hasard, l'endroit exact où atterriront le tonemapping, les
//         fondus et la colorimétrie annoncés avec le bloom — la passe existe pour la queue de
//         post-traitement, pas seulement pour cette addition.
//
// COMMENT: la lueur est ADDITIVE, comme la lumière elle-même. Un mélange (mix) l'imposerait au prix
//         de la scène : une lueur blanche DÉLAVERAIT ce qu'elle entoure au lieu de l'auréoler, et un
//         halo dans le vide — où la scène est noire — n'existerait pas du tout. C'est le même
//         raisonnement que le terme de diffusion du composite (plan A2), pour la même raison.
//
// L'échantillonnage de `s_bloom` est BILINÉAIRE et depuis le quart de résolution : c'est la remontée
// en échelle, et elle est gratuite (le filtrage matériel fait le travail). Une lueur est basse
// fréquence par nature ; la résoudre au pixel serait payer pour une information qu'on vient d'étaler.
//
// ⚠️ Pas d'écrêtage ici non plus. La sortie va au backbuffer, qui écrête lui-même en 8 bits ; borner
//    ici en plus ne changerait rien à l'image et ferait perdre le sur-brillant à un futur tonemap qui
//    voudra le lire.
void main()
{
	vec3 scene = texture2D(s_scene, v_texcoord0).rgb;
	vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;

	gl_FragColor = vec4(scene + bloom * u_present.x, 1.0);
}
