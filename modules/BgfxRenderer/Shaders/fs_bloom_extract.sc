$input v_texcoord0

#include <bgfx_shader.sh>

// La frame COMPOSÉE, en HDR. Pas le buffer de lumière : voir POURQUOI ci-dessous.
SAMPLER2D(s_scene, 0);

// u_bloomExtract = (threshold, knee, texelX, texelY)
//   texelX/Y sont les texels de la cible SOURCE (pleine résolution), pas de la nôtre.
uniform vec4 u_bloomExtract;

// Extraction du sur-brillant (plan B, tranche B3) — étage fragment.
//
// QUOI  : lit la frame composée en pleine résolution, garde la FRACTION de chaque pixel qui dépasse
//         le seuil, et écrit le résultat dans une cible au QUART de la résolution.
//
// POURQUOI la frame composée et pas le buffer de lumière — c'est LE choix d'architecture du plan B :
//         le buffer de lumière laisserait sans lueur un sprite additif (`blend:"additive"`), qui
//         existe dans le moteur exactement pour les choses qui brillent (le panache de moteur). Et
//         recalculer ici la formule d'éclairage dupliquerait `scene × (ambiant + lumière) +
//         lumière × scatter` dans un second shader, où elle dériverait. Le composite est propriétaire
//         de cette formule ; cette passe en consomme le résultat.
//
// COMMENT: quatre taps, décalés d'un demi-texel source autour du centre. La réduction au quart est
//         DÉJÀ une partie du flou (elle moyenne le voisinage), et prendre un seul tap laisserait un
//         pixel sur seize décider — la lueur scintillerait sur un mouvement d'un pixel. Les quatre
//         taps sont pris AVANT le seuillage : seuiller chaque tap séparément puis moyenner ferait
//         qu'un pixel isolé très brillant compterait pour un quart au lieu de sa vraie contribution.
//
// La courbe est celle de grove::light::brightPassFraction, et c'est son oracle qui la verrouille
// (BloomMathUnit). Elle rend une FRACTION que l'on multiplie : la teinte est préservée, là où
// seuiller chaque canal décalerait la couleur d'une étincelle rouge-orangé vers le rouge pur.
void main()
{
	vec2 tx = u_bloomExtract.zw;

	vec3 c = texture2D(s_scene, v_texcoord0 + vec2( tx.x,  tx.y)).rgb;
	c += texture2D(s_scene, v_texcoord0 + vec2(-tx.x,  tx.y)).rgb;
	c += texture2D(s_scene, v_texcoord0 + vec2( tx.x, -tx.y)).rgb;
	c += texture2D(s_scene, v_texcoord0 + vec2(-tx.x, -tx.y)).rgb;
	c *= 0.25;

	// Luminance Rec. 709, NON écrêtée : les cibles sont en RGBA16F précisément pour que les valeurs
	// dépassent 1, et une luminance saturée ferait briller identiquement tout le sur-brillant.
	float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));

	float threshold = u_bloomExtract.x;
	float knee      = u_bloomExtract.y;

	// Genou doux (Karis). Sans lui la PENTE saute de 0 à 1/seuil au franchissement et la lueur
	// s'amorce par un ourlet net là où la scène atteint le seuil — la loi maison « saturer en
	// douceur, pas borner dur » appliquée à un seuil.
	float soft = clamp(luma - threshold + knee, 0.0, 2.0 * knee);
	soft = (soft * soft) / (4.0 * knee + 0.0001);

	float contribution = max(soft, luma - threshold);
	// max(luma, ε) et pas luma : un pixel noir donnerait 0/0.
	float fraction = clamp(contribution / max(luma, 0.0001), 0.0, 1.0);

	gl_FragColor = vec4(c * fraction, 1.0);
}
