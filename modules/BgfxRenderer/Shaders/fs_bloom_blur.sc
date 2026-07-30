$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// u_bloomBlur = (stepX, stepY, 0, 0) — le pas d'UN tap, en UV.
//   Passe horizontale : (spacing·texelX, 0). Passe verticale : (0, spacing·texelY).
//   C'est ce qui fait qu'UN shader sert aux deux passes du flou séparable.
uniform vec4 u_bloomBlur;

// u_bloomWeights[0] = (w0, w1, w2, w3), u_bloomWeights[1] = (w4, 0, 0, 0)
//
// ⚠️ CES POIDS SONT TÉLÉVERSÉS, jamais écrits en dur ici. Ils viennent de
//    grove::light::bloomHalfKernel, et son test unitaire prouve que la somme RÉFLÉCHIE
//    (w0 + 2·(w1+w2+w3+w4)) vaut exactement 1. Un noyau non normalisé changerait la luminosité
//    globale de la lueur — un défaut qui ressemble à un mauvais réglage d'`intensity` et qu'on
//    compense alors au mauvais bouton. Les écrire ici en ferait une copie qui dérive.
uniform vec4 u_bloomWeights[2];

// Flou gaussien séparable, 9 taps (plan B, tranche B3) — étage fragment.
//
// POURQUOI séparable : un noyau 9×9 en une passe coûte 81 taps ; deux passes de 9 en coûtent 18 pour
// le même résultat, parce qu'une gaussienne 2D est le produit de deux gaussiennes 1D. C'est la seule
// raison du ping-pong entre deux cibles.
//
// COMMENT: le noyau est symétrique, donc 5 poids suffisent et le shader réfléchit les quatre
//         derniers. Déroulé explicitement plutôt qu'en boucle : indexer un tableau d'uniforms dans
//         une boucle se compile mal en HLSL, et à 9 taps le déroulage est plus lisible que le pli.
void main()
{
	vec2 d = u_bloomBlur.xy;

	vec3 sum  = texture2D(s_scene, v_texcoord0).rgb * u_bloomWeights[0].x;

	sum += (texture2D(s_scene, v_texcoord0 + d)
	      + texture2D(s_scene, v_texcoord0 - d)).rgb * u_bloomWeights[0].y;

	sum += (texture2D(s_scene, v_texcoord0 + d * 2.0)
	      + texture2D(s_scene, v_texcoord0 - d * 2.0)).rgb * u_bloomWeights[0].z;

	sum += (texture2D(s_scene, v_texcoord0 + d * 3.0)
	      + texture2D(s_scene, v_texcoord0 - d * 3.0)).rgb * u_bloomWeights[0].w;

	sum += (texture2D(s_scene, v_texcoord0 + d * 4.0)
	      + texture2D(s_scene, v_texcoord0 - d * 4.0)).rgb * u_bloomWeights[1].x;

	gl_FragColor = vec4(sum, 1.0);
}
