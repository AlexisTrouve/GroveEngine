/**
 * Unit tests: grove::anim::Animator — tranche A1 (états nommés, coupe franche, PAS encore de fondu).
 *
 * QUOI  : un Animator porte des états nommés (nom -> Clip*), `play(nom)` choisit l'état courant,
 *         `update(dt, hierarchy)` avance le clip courant et écrit les locaux.
 * POURQUOI : `grove::anim` avait Clip + AnimationPlayer + easing, et AUCUN moyen d'enchaîner deux
 *         clips — DAOS coud marche/chute/grimpe/rattrapage à la main. Plan :
 *         docs/design/anim-state-machine.md.
 * COMMENT : oracles NUMÉRIQUES (une pose est une valeur exacte, pas un jugement visuel), donc
 *         entièrement headless comme le reste de grove::anim.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/anim/Animator.h>

using namespace grove::anim;
using Catch::Matchers::WithinAbs;

namespace {
// Un clip d'une seconde qui pousse la translation X du nœud 0 de 0 -> 100, linéairement.
// Une VALEUR ÉCHANTILLONNÉE dit donc directement OÙ on en est dans le clip : x == 30 <=> t == 0.3 s.
// C'est ce qui rend l'idempotence de play() observable sans introspecter l'Animator.
Clip makeRamp(float from = 0.0f, float to = 100.0f, float duration = 1.0f) {
    Clip c;
    c.duration = duration;
    Track t;
    t.nodeId = 0;
    t.property = Property::TranslationX;
    t.keys = { Keyframe{0.0f, from, Easing::Linear}, Keyframe{duration, to, Easing::Linear} };
    c.tracks.push_back(t);
    return c;
}

// Une hiérarchie à un seul nœud — suffisant : A1 ne teste pas la composition, qui a ses tests.
Hierarchy makeOneNode() {
    Hierarchy h;
    h.addNode(-1);
    return h;
}
} // namespace

TEST_CASE("Animator: un état joué avance et écrit le local", "[anim][animator]") {
    Clip ramp = makeRamp();
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &ramp);
    anim.play("walk");

    REQUIRE(anim.current() == "walk");

    anim.update(0.25f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(25.0f, 0.01f));   // 0.25 s sur une rampe 0->100 en 1 s
}

// ⚠️ LA propriété du lot. Un jeu appelle play("walk") à CHAQUE frame tant que le perso marche —
// c'est la façon naturelle d'écrire l'appelant. Une implémentation qui relance le clip à chaque
// appel fige le personnage sur sa première image, pour toujours. On publie un ÉTAT, pas une
// TRANSITION (même classe que le flip de render:sprite:update, corrigé le 2026-07-31).
TEST_CASE("Animator: play() sur l'état DÉJÀ courant ne relance pas le clip", "[anim][animator]") {
    Clip ramp = makeRamp();
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &ramp);

    // Dix frames de 0.05 s, en re-demandant "walk" à chaque tour comme le ferait un vrai jeu.
    for (int frame = 0; frame < 10; ++frame) {
        anim.play("walk");
        anim.update(0.05f, h);
    }

    // 10 x 0.05 = 0.5 s => x == 50. Une implémentation qui remet l'horloge à 0 à chaque play()
    // donnerait 5 (une seule frame d'avance), ce qui est EXACTEMENT le bug qu'on verrouille.
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.01f));
}

TEST_CASE("Animator: play() d'un AUTRE état repart bien du début", "[anim][animator]") {
    Clip walk = makeRamp(0.0f, 100.0f);
    Clip jump = makeRamp(0.0f, 100.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &walk);
    anim.addState("jump", &jump);

    anim.play("walk");
    anim.update(0.60f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(60.0f, 0.01f));

    // Le contrepoids de l'idempotence : changer d'état DOIT remettre l'horloge à zéro, sinon un
    // saut hériterait de l'avancement de la marche. Sans ce cas, "play ne fait jamais rien" passerait.
    anim.play("jump");
    anim.update(0.10f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(10.0f, 0.01f));
    REQUIRE(anim.current() == "jump");
}

TEST_CASE("Animator: un nom inconnu est ignoré, l'état courant survit", "[anim][animator]") {
    Clip ramp = makeRamp();
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &ramp);
    anim.play("walk");
    anim.update(0.30f, h);

    // Fail-soft : une faute de frappe dans un nom d'anim ne doit pas jeter ni vider l'état. Un perso
    // qui garde son animation précédente est infiniment moins grave qu'un crash sur une chaîne.
    anim.play("wlak");
    REQUIRE(anim.current() == "walk");

    anim.update(0.20f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.01f));   // l'horloge a continué, sans coupure
}

TEST_CASE("Animator: sans état joué, update() ne touche à rien", "[anim][animator]") {
    Hierarchy h = makeOneNode();
    h.local(0).x = 7.0f;                    // valeur posée par le jeu

    Animator anim;
    anim.update(0.5f, h);                   // aucun addState, aucun play

    REQUIRE(anim.current().empty());
    REQUIRE_THAT(h.local(0).x, WithinAbs(7.0f, 0.01f));   // un Animator au repos n'écrase rien
}

TEST_CASE("Animator: un état en boucle repasse par le début", "[anim][animator]") {
    Clip ramp = makeRamp(0.0f, 100.0f, 1.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &ramp);           // loop = défaut
    anim.play("walk");

    anim.update(1.25f, h);                  // 1.25 s sur un clip d'1 s -> t = 0.25
    REQUIRE_THAT(h.local(0).x, WithinAbs(25.0f, 0.01f));
}

TEST_CASE("Animator: un état non bouclé se fige sur sa dernière image", "[anim][animator]") {
    Clip ramp = makeRamp(0.0f, 100.0f, 1.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("land", &ramp, /*loop=*/false);
    anim.play("land");

    anim.update(2.0f, h);                   // bien au-delà de la durée
    REQUIRE_THAT(h.local(0).x, WithinAbs(100.0f, 0.01f));
    REQUIRE(anim.finished());               // A3 s'en servira pour enchaîner (Once{"cible"})
}

// ⚠️ Le contrepoids de l'idempotence, et le trou que la première correction laissait ouvert.
// L'invariant voulu n'est PAS « ne jamais rembobiner l'état courant » mais « ne jamais rembobiner
// une anim EN COURS ». Sans la nuance, un coup d'épée qui s'est terminé ne peut plus jamais être
// rejoué : le joueur re-frappe, play("attack") court-circuite, rien ne bouge. Sur un état BOUCLÉ
// la nuance ne change rien (il joue toujours), donc seul ce cas-ci la met en évidence.
TEST_CASE("Animator: un état non bouclé TERMINÉ se rejoue quand on le redemande", "[anim][animator]") {
    Clip ramp = makeRamp(0.0f, 100.0f, 1.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("attack", &ramp, /*loop=*/false);

    anim.play("attack");
    anim.update(2.0f, h);                   // le coup est allé jusqu'au bout
    REQUIRE(anim.finished());
    REQUIRE_THAT(h.local(0).x, WithinAbs(100.0f, 0.01f));

    anim.play("attack");                    // le joueur re-frappe
    REQUIRE_FALSE(anim.finished());         // l'horloge est repartie...
    anim.update(0.20f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(20.0f, 0.01f));   // ...et le coup rejoue depuis le début
}
