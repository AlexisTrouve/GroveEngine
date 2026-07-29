$input a_position, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// u_nebula = (centre.x, centre.y, radius, unused)
uniform vec4 u_nebula;

// Soft radial medium — vertex stage (lighting A4).
//
// ⚠️ POURQUOI CE FICHIER EXISTE — et surtout, pourquoi la raison qui figurait ici était FAUSSE.
//
// Il a d'abord réutilisé `vs_light` tel quel : le problème géométrique est identique (mettre un quad
// unitaire à l'échelle d'un rayon autour d'un centre monde), et dupliquer un shader pour renommer un
// uniform semblait le mauvais échange. Puis un test d'asset sans rapport (`AssetSpriteGpu`) s'est mis
// à mourir en corruption de tas au teardown, et une série de coupes a « désigné » le partage de
// bytecode : bgfx dédoublonne les shaders par hachage, deux programmes bâtis sur le même vertex
// déséquilibreraient son comptage de références. Ce fichier a été créé sur ce diagnostic.
//
// **Le diagnostic était faux.** Chacune de ces coupes RECONSTRUISAIT la cible, et c'est la
// reconstruction qui faisait passer le test — pas la coupe. Vérifié après coup : la configuration
// d'origine (programme nébuleuse partageant `vs_light`), avec un build propre, passe **5 fois sur 5**.
// La vraie cause était un **artefact de build périmé**, laissé par une commande qui compilait pendant
// que la suite tournait — la nuisance n°3 du registre, dans laquelle je suis retombé.
//
// Ce fichier est donc CONSERVÉ pour une raison honnête et beaucoup plus modeste : `u_nebula` dit ce
// qu'il place, là où `u_light` obligeait à un commentaire d'excuse. C'est de la lisibilité, pas une
// correction. Réutiliser `vs_light` marcherait tout aussi bien.
//
// **La leçon**, elle, vaut au-delà d'ici : une coupe différentielle qui reconstruit à chaque étape ne
// discrimine rien si la reconstruction est elle-même le remède. Il faut vérifier que la variante
// « saine » échoue encore APRÈS reconstruction, avant d'attribuer quoi que ce soit.
//
// Le -1..1 local passe en varying : dans cet espace le bord du volume est exactement à length == 1,
// quels que soient le rayon et le zoom — c'est ce qui laisse l'étage fragment calculer sa densité
// sans jamais connaître une coordonnée monde.
void main()
{
	vec2 world = u_nebula.xy + a_position.xy * u_nebula.z;
	gl_Position = mul(u_modelViewProj, vec4(world, 0.0, 1.0));

	v_texcoord0 = a_position.xy;
	v_color0 = a_color0;
}
