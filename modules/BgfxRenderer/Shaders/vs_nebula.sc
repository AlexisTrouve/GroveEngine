$input a_position, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// u_nebula = (centre.x, centre.y, radius, unused)
uniform vec4 u_nebula;

// Soft radial medium — vertex stage (lighting A4).
//
// ⚠️ POURQUOI CE FICHIER EXISTE, alors qu'il est presque vs_light.
//
// Il a d'abord réutilisé `vs_light` tel quel : le problème géométrique est identique (mettre un quad
// unitaire à l'échelle d'un rayon autour d'un centre monde), et dupliquer un shader pour renommer un
// uniform semblait le mauvais échange. **C'était un bug**, pas un compromis.
//
// bgfx DÉDOUBLONNE les shaders par hachage de leur bytecode et les compte par référence. Deux
// programmes bâtis sur le MÊME bytecode de vertex, chacun créé avec `_destroyShaders = true`,
// déséquilibrent ce comptage : à la destruction, le tas est corrompu. Symptôme observé — un test
// d'asset sans aucun rapport (`AssetSpriteGpu`) mourait avec 0xC0000374 APRÈS son dernier assert,
// pendant le teardown, 6 fois sur 6. Localisé par coupes : passe débranchée → vert ; passe branchée
// mais programme non créé → vert. C'était donc le PROGRAMME, pas la passe.
//
// Un bytecode distinct suffit à l'éviter. Le renommage de l'uniform (`u_light` → `u_nebula`) tombe
// en prime : un milieu n'est pas une lampe, et le nom le dit maintenant.
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
