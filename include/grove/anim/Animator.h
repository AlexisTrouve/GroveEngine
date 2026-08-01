#pragma once

/**
 * grove::anim::Animator — états d'animation nommés (système d'animation, tranche A1).
 *
 * QUOI  : une table `nom -> clip` et un état courant. `play(nom)` choisit l'état,
 *         `update(dt, hierarchy)` avance le clip courant et écrit les transforms LOCAUX.
 *         A1 fait une COUPE FRANCHE au changement d'état ; le fondu croisé arrive en A2.
 *
 * POURQUOI : `Clip` porte la donnée, `AnimationPlayer` porte l'horloge — et il manquait la couche
 *         qui dit « je suis en train de marcher », que chaque jeu recousait à la main (DAOS :
 *         marche / chute / grimpe / rattrapage). Ce qui est ici, c'est la COUTURE des poses ;
 *         la DÉCISION (quand passer de walk à fall) reste au jeu, délibérément — pas de
 *         condition, pas de prédicat, pas de langage d'expression. Même ligne que
 *         `scene:goto {node}` chez DialogueModule et que la bibliothèque fixe de FxModule.
 *
 * COMMENT : l'horloge et les règles de bouclage sont DÉLÉGUÉES à AnimationPlayer plutôt que
 *         réécrites (fmod en avant comme en arrière, clamp de fin, une seule source de vérité).
 *         Pur, headless, std seul — comme tout `grove::anim`, aucun couplage renderer/IIO/SDL.
 *
 * Plan et arbitrages : docs/design/anim-state-machine.md
 */

#include "AnimationPlayer.h"
#include "Clip.h"
#include "Transform2D.h"

#include <string>
#include <unordered_map>

namespace grove {
namespace anim {

class Animator {
public:
    // Déclare un état. Le clip n'est PAS possédé — il doit survivre à l'Animator (même contrat
    // que AnimationPlayer : une donnée immuable partagée par des milliers d'instances animées).
    // Re-déclarer un nom écrase sa définition, ce qui rend une table reconstructible à chaud.
    void addState(const std::string& name, const Clip* clip, bool loop = true) {
        m_states[name] = State{clip, loop};
    }

    // Bascule sur un état. Nom inconnu -> ignoré, l'état courant survit (fail-soft : une faute de
    // frappe dans un nom d'anim ne doit ni jeter ni vider l'état — un perso qui garde son
    // animation précédente est infiniment moins grave qu'un crash sur une chaîne).
    //
    // ⚠️ IDEMPOTENT — c'est la propriété la plus importante de cette classe, et elle ne se voit
    // pas. Un jeu appelle play("walk") à CHAQUE frame tant que le perso marche : c'est la façon
    // naturelle d'écrire l'appelant, et c'est la bonne. Sans ce court-circuit, chaque appel remet
    // l'horloge à 0 et le personnage reste figé sur sa première image — mesuré avant correction :
    // 5 au lieu de 50 sur dix frames. On publie un ÉTAT, pas une TRANSITION.
    //
    // (Exactement la classe de défaut corrigée le 2026-07-31 sur render:sprite:update, où un flip
    // « conservé si omis » aurait basculé d'une frame à l'autre. Deux couches, un même piège.)
    // ⚠️ La condition porte sur « EN COURS », pas sur « c'est le même nom ». Sur un état bouclé les
    // deux formulations coïncident (il joue toujours), et c'est ce qui rend la nuance invisible :
    // seul un état NON bouclé les sépare. Court-circuiter sur le seul nom rendrait un coup d'épée
    // terminé **impossible à rejouer** — le joueur re-frappe, rien ne bouge.
    void play(const std::string& name) {
        if (name == m_current && m_player.isPlaying()) return;   // en cours : ne PAS rembobiner

        auto it = m_states.find(name);
        if (it == m_states.end()) return;

        m_current = name;
        m_player.play(it->second.clip, it->second.loop);
    }

    const std::string& current() const { return m_current; }

    // Vrai quand un état NON bouclé a atteint sa dernière image. A3 s'en servira pour enchaîner
    // (Once{"cible"}). Faux tant qu'aucun état n'a été joué.
    bool finished() const { return !m_current.empty() && !m_player.isPlaying(); }

    // Avance l'état courant et écrit les locaux. Sans état joué : NO-OP strict — un Animator au
    // repos ne doit rien écraser de ce que le jeu a posé dans la hiérarchie.
    void update(float dt, Hierarchy& hierarchy) {
        if (m_current.empty()) return;
        m_player.update(dt, hierarchy);
    }

private:
    struct State {
        const Clip* clip = nullptr;
        bool loop = true;
    };

    std::unordered_map<std::string, State> m_states;
    std::string m_current;       // vide = au repos
    AnimationPlayer m_player;    // horloge + bouclage, non réécrits ici
};

} // namespace anim
} // namespace grove
