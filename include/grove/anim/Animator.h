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
#include "Easing.h"
#include "Transform2D.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace grove {
namespace anim {

namespace detail {

// Mélange deux angles par l'ARC LE PLUS COURT.
//
// POURQUOI : un Track porte des flottants bruts. Interpoler linéairement une rotation de 3.0 vers
// -3.0 traverse ZÉRO — le membre fait presque un tour complet dans le mauvais sens, pendant toute
// la durée du fondu, puis se remet d'aplomb. C'est le bug classique du fondu croisé, et il est
// INVISIBLE tant que les deux angles ne straddlent pas le passage à ±π (avec 0 -> π/2, le lerp
// naïf donne exactement le même résultat).
//
// COMMENT : ramener l'écart dans [-π, π) en le décalant de π, en prenant le modulo 2π, en
// corrigeant le signe (fmod garde celui du dividende en C++), puis en retirant le π ajouté.
inline float lerpAngleShortest(float a, float b, float t) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    float delta = std::fmod(b - a + kPi, kTwoPi);
    if (delta < 0.0f) delta += kTwoPi;      // fmod peut rendre un négatif : on rebascule
    delta -= kPi;
    return a + delta * t;
}

// Mélange deux transforms LOCAUX. La rotation prend l'arc court, tout le reste un lerp droit.
inline Transform2D blendLocal(const Transform2D& from, const Transform2D& to, float t) {
    Transform2D out;
    out.x        = from.x      + (to.x      - from.x)      * t;
    out.y        = from.y      + (to.y      - from.y)      * t;
    out.scaleX   = from.scaleX + (to.scaleX - from.scaleX) * t;
    out.scaleY   = from.scaleY + (to.scaleY - from.scaleY) * t;
    out.rotation = lerpAngleShortest(from.rotation, to.rotation, t);
    return out;
}

} // namespace detail

// Un état joué UNE fois, qui enchaîne tout seul sur `next` à sa dernière image.
//
// POURQUOI un type dédié plutôt qu'un booléen de plus sur addState : `Once{"idle"}` se lit au site
// d'appel, là où `addState("attack", &clip, false, "idle")` oblige à aller chercher ce que `false`
// veut dire. C'est une propriété de l'ANIMATION (« ce clip ne boucle pas et il mène là »), pas une
// décision de gameplay : le jeu dit toujours QUAND attaquer, le moteur ne fait qu'enchaîner ce qui
// suit la dernière image. La frontière tenue par cette classe n'en est pas entamée.
struct Once {
    std::string next;
};

class Animator {
public:
    // Déclare un état. Le clip n'est PAS possédé — il doit survivre à l'Animator (même contrat
    // que AnimationPlayer : une donnée immuable partagée par des milliers d'instances animées).
    // Re-déclarer un nom écrase sa définition, ce qui rend une table reconstructible à chaud.
    void addState(const std::string& name, const Clip* clip, bool loop = true) {
        m_states[name] = State{clip, loop, std::string{}};
    }

    // Surcharge `Once` : joue une fois puis enchaîne sur la cible, avec le fondu par défaut.
    void addState(const std::string& name, const Clip* clip, Once once) {
        m_states[name] = State{clip, false, once.next};
    }

    // Fondu utilisé par un play() qui n'en précise pas, et par les enchaînements `Once`.
    // Défaut 0 = coupe franche : tout code écrit avant cette tranche est inchangé.
    void setDefaultFade(float seconds) { m_defaultFade = seconds; }

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
    //
    // `fade` (secondes) : 0 = coupe franche (défaut, comportement A1 bit pour bit). Le fondu part
    // de la pose RÉELLEMENT AFFICHÉE à la dernière frame — pas du clip sortant. Voir §m_fromPose.
    // `fade < 0` (le défaut) = « prends le fondu par défaut » ; une valeur explicite l'emporte,
    // 0 compris — un jeu peut donc forcer une coupe franche malgré un défaut posé.
    void play(const std::string& name, float fade = -1.0f) {
        if (name == m_current && m_player.isPlaying()) return;   // en cours : ne PAS rembobiner

        auto it = m_states.find(name);
        if (it == m_states.end()) return;

        if (fade < 0.0f) fade = m_defaultFade;

        // Le fondu part de ce qui est à l'écran. Sans pose affichée (aucun update() encore), il
        // n'y a rien d'où partir : coupe franche. Chemin explicite, jamais une division par zéro.
        if (fade > 0.0f && !m_lastPose.empty()) {
            m_fromPose = m_lastPose;
            m_fadeElapsed = 0.0f;
            m_fadeDuration = fade;
            m_fading = true;
        } else {
            m_fading = false;
        }

        m_current = name;
        m_player.play(it->second.clip, it->second.loop);
    }

    // Courbe du fondu. Linéaire par défaut — une courbe est un choix d'auteur, pas un défaut caché.
    void setFadeEasing(Easing curve) { m_fadeEasing = curve; }

    bool isFading() const { return m_fading; }

    const std::string& current() const { return m_current; }

    // Vrai quand un état NON bouclé a atteint sa dernière image. A3 s'en servira pour enchaîner
    // (Once{"cible"}). Faux tant qu'aucun état n'a été joué.
    bool finished() const { return !m_current.empty() && !m_player.isPlaying(); }

    // Avance l'état courant et écrit les locaux. Sans état joué : NO-OP strict — un Animator au
    // repos ne doit rien écraser de ce que le jeu a posé dans la hiérarchie.
    //
    // COMMENT (fondu) : 1. l'entrant écrit sa pose dans la hiérarchie (le chemin normal, inchangé) ;
    //   2. si un fondu court, on ré-écrit chaque local en mélangeant la pose figée du sortant vers
    //   celle qu'on vient d'écrire ; 3. on mémorise le résultat AFFICHÉ, qui servira de point de
    //   départ au prochain fondu. C'est cette mémorisation qui rend le cas ré-entrant exact.
    void update(float dt, Hierarchy& hierarchy) {
        if (m_current.empty()) return;

        m_player.update(dt, hierarchy);

        if (m_fading) {
            m_fadeElapsed += dt;
            const float u = (m_fadeDuration > 0.0f) ? (m_fadeElapsed / m_fadeDuration) : 1.0f;

            if (u >= 1.0f) {
                m_fading = false;               // terminé : la hiérarchie porte déjà l'entrant PUR
            } else {
                const float w = ease(m_fadeEasing, 0.0f, 1.0f, u);
                const size_t n = hierarchy.size();
                const size_t m = m_fromPose.size();
                for (size_t i = 0; i < n && i < m; ++i) {
                    const int id = static_cast<int>(i);
                    hierarchy.local(id) = detail::blendLocal(m_fromPose[i], hierarchy.local(id), w);
                }
            }
        }

        capturePose(hierarchy);

        // Enchaînement `Once` — APRÈS capturePose(), et l'ordre est le fond du sujet : le fondu
        // vers la cible doit partir de la DERNIÈRE IMAGE de l'état qui vient de finir, laquelle
        // n'est connue qu'une fois la pose écrite et mémorisée. Enchaîner plus haut ferait démarrer
        // le fondu depuis la pose de la frame PRÉCÉDENTE — un décalage d'une frame, invisible en
        // test unitaire et sensible à l'œil sur un enchaînement rapide.
        //
        // Cible inconnue -> play() ne fait rien : l'état reste figé sur sa dernière image (fail-soft
        // comme ailleurs). On retente au tour suivant, ce qui coûte une recherche de table sur un
        // état déjà terminé — pas une boucle, pas une allocation.
        if (!m_current.empty() && !m_player.isPlaying()) {
            auto it = m_states.find(m_current);
            if (it != m_states.end() && !it->second.next.empty()) {
                play(it->second.next);      // fondu par défaut
            }
        }
    }

private:
    struct State {
        const Clip* clip = nullptr;
        bool loop = true;
        std::string next;     // non vide = état Once : où aller à la dernière image
    };

    // Recopie la pose écrite cette frame. POURQUOI cette copie plutôt qu'un second AnimationPlayer
    // qui continuerait d'animer le sortant : c'est ce qui rend le cas RÉ-ENTRANT exact. Dans un jeu
    // de plateforme, changer d'état pendant un fondu est la situation NORMALE (marche -> saut ->
    // chute en trois frames) ; en repartant de la pose réellement affichée, le second basculement
    // ne peut pas provoquer d'à-coup, quelle que soit l'avancée du premier fondu.
    //
    // ⚠️ COMPROMIS ASSUMÉ : le sortant ne s'anime plus pendant le fondu (snapshot blending). Sur
    // les durées usuelles (0.1-0.2 s) c'est imperceptible — un cycle de marche bouge très peu en
    // 150 ms — et ça s'échange contre l'exactitude du cas ci-dessus, qui, lui, se voit.
    void capturePose(const Hierarchy& hierarchy) {
        const size_t n = hierarchy.size();
        m_lastPose.resize(n);
        for (size_t i = 0; i < n; ++i) m_lastPose[i] = hierarchy.local(static_cast<int>(i));
    }

    std::unordered_map<std::string, State> m_states;
    std::string m_current;       // vide = au repos
    AnimationPlayer m_player;    // horloge + bouclage, non réécrits ici

    std::vector<Transform2D> m_lastPose;   // ce qui a été AFFICHÉ à la dernière frame
    std::vector<Transform2D> m_fromPose;   // pose de départ du fondu courant (figée)
    bool  m_fading = false;
    float m_fadeElapsed = 0.0f;
    float m_fadeDuration = 0.0f;
    Easing m_fadeEasing = Easing::Linear;
    float m_defaultFade = 0.0f;   // 0 = coupe franche (comportement historique)
};

} // namespace anim
} // namespace grove
