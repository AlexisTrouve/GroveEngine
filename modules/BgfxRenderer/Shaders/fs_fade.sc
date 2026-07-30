$input v_texcoord0

#include <bgfx_shader.sh>

// u_fade = (r, g, b, amount)
uniform vec4 u_fade;

// Fondu plein écran (plan F2) — étage fragment. Le plus court shader du moteur, et c'est normal : tout
// le travail est fait par l'ÉTAT de mélange.
//
// QUOI  : émet la couleur du fondu avec `amount` en alpha. Le mélange alpha du RHI
//         (`dst = a·src + (1−a)·dst`) est EXACTEMENT le `mix` voulu, donc il n'y a rien à calculer ici.
//
// ⚠️ POURQUOI PAS un mélange ADDITIF — c'est le mode d'échec silencieux de cette tranche : en additif,
//    un fondu au NOIR ne ferait rien du tout (ajouter zéro). Et comme le fondu au noir est le cas de
//    loin le plus courant, la faute se présenterait comme « le fondu ne marche pas », sans indice.
//
// POURQUOI un shader dédié plutôt que le programme `color` existant : celui-ci transforme par
//         `u_modelViewProj`, donc il dépendrait de la transformation posée sur la vue du fondu. Une vue
//         neuve vaut l'identité, mais compter là-dessus rendrait le fondu sensible à un état qui ne le
//         concerne pas. Ici le quad arrive déjà en espace de clip (vs_composite) et rien ne peut le
//         déplacer.
//
// v_texcoord0 est reçu mais inutilisé : il vient de vs_composite, partagé avec les autres passes de
// post-traitement. Un fondu est uniforme, il n'a aucune coordonnée à lire.
void main()
{
	vec2 unused = v_texcoord0;
	gl_FragColor = u_fade;
}
