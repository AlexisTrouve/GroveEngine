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

// ============================================================================
// Tranche A2 — fondu croisé.
//
// Le mélange se fait sur une pose FIGÉE à l'instant de la transition (snapshot blending) : le
// sortant ne s'anime plus pendant le fondu. C'est le compromis assumé, et il achète l'exactitude
// du cas ré-entrant (un play() pendant un fondu, la situation NORMALE d'un jeu de plateforme).
// Justification complète dans docs/design/anim-state-machine.md.
// ============================================================================

namespace {
// Un clip qui tient une valeur CONSTANTE — sert de cible de fondu dont la pose ne dépend pas du
// temps, ce qui rend l'oracle du mélange exact et indépendant de l'avancement de l'entrant.
Clip makeConstant(Property prop, float value, float duration = 1.0f) {
    Clip c;
    c.duration = duration;
    Track t;
    t.nodeId = 0;
    t.property = prop;
    t.keys = { Keyframe{0.0f, value, Easing::Linear}, Keyframe{duration, value, Easing::Linear} };
    c.tracks.push_back(t);
    return c;
}
} // namespace

TEST_CASE("Animator: à mi-fondu, la pose est à mi-chemin", "[anim][animator][fade]") {
    Clip walk = makeRamp(0.0f, 100.0f, 1.0f);
    Clip idle = makeConstant(Property::TranslationX, 200.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &walk);
    anim.addState("idle", &idle);

    anim.play("walk");
    anim.update(0.50f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.01f));   // la pose de départ du fondu

    anim.play("idle", 0.40f);      // fondu de 0.4 s
    anim.update(0.20f, h);         // à la moitié -> poids 0.5 (linéaire)

    // 50 (pose figée du sortant) -> 200 (entrant constant), à 50 % = 125.
    REQUIRE_THAT(h.local(0).x, WithinAbs(125.0f, 0.01f));
    REQUIRE(anim.isFading());
}

// ⚠️ LE cas qui justifie la tranche. Une rotation est un flottant brut : l'interpoler linéairement
// de 3.0 vers -3.0 traverse ZÉRO, et le membre fait presque un tour complet dans le mauvais sens
// pendant toute la durée du fondu. Il faut l'ARC LE PLUS COURT (ici : passer par ±π, 0.28 rad de
// chemin au lieu de 6.0).
//
// ⚠️ Le réglage est choisi POUR discriminer : avec des angles « propres » (0 -> π/2) les deux
// implémentations donnent le MÊME résultat et le test ne prouverait rien. C'est exactement le
// piège documenté dans known-annoyances -- ici on straddle volontairement le passage à ±π.
TEST_CASE("Animator: le fondu d'une rotation prend l'arc le plus court", "[anim][animator][fade]") {
    Clip left  = makeConstant(Property::Rotation,  3.0f);
    Clip right = makeConstant(Property::Rotation, -3.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("left", &left);
    anim.addState("right", &right);

    anim.play("left");
    anim.update(0.10f, h);
    REQUIRE_THAT(h.local(0).rotation, WithinAbs(3.0f, 0.01f));

    anim.play("right", 1.0f);
    anim.update(0.50f, h);         // mi-fondu

    // Arc court : delta = -6.0 rad ramené dans [-π, π) = +0.2832 ; à 50 % -> 3.0 + 0.1416 ≈ π.
    // Un lerp naïf donnerait 0.0 -- soit le membre à l'horizontale au lieu de pointer vers l'arrière.
    REQUIRE_THAT(h.local(0).rotation, WithinAbs(3.1416f, 0.01f));
}

TEST_CASE("Animator: un fondu terminé laisse l'entrant PUR", "[anim][animator][fade]") {
    Clip walk = makeRamp(0.0f, 100.0f, 1.0f);
    Clip idle = makeConstant(Property::TranslationX, 200.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &walk);
    anim.addState("idle", &idle);

    anim.play("walk");
    anim.update(0.50f, h);
    anim.play("idle", 0.20f);
    anim.update(0.50f, h);         // bien au-delà de la durée du fondu

    REQUIRE_FALSE(anim.isFading());
    REQUIRE_THAT(h.local(0).x, WithinAbs(200.0f, 0.01f));   // plus aucune trace du sortant
}

TEST_CASE("Animator: un fondu de durée 0 est une coupe franche", "[anim][animator][fade]") {
    Clip walk = makeRamp(0.0f, 100.0f, 1.0f);
    Clip idle = makeConstant(Property::TranslationX, 200.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &walk);
    anim.addState("idle", &idle);

    anim.play("walk");
    anim.update(0.50f, h);
    anim.play("idle", 0.0f);       // chemin explicite, PAS une division par zéro
    anim.update(0.01f, h);

    REQUIRE_FALSE(anim.isFading());
    REQUIRE_THAT(h.local(0).x, WithinAbs(200.0f, 0.01f));
}

// NON-RÉGRESSION A1 : sans durée précisée et sans défaut posé, on garde la coupe franche.
// Tout le code écrit contre A1 doit continuer à se comporter à l'identique.
TEST_CASE("Animator: sans fondu demandé, le comportement A1 est inchangé", "[anim][animator][fade]") {
    Clip walk = makeRamp(0.0f, 100.0f, 1.0f);
    Clip idle = makeConstant(Property::TranslationX, 200.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &walk);
    anim.addState("idle", &idle);

    anim.play("walk");
    anim.update(0.50f, h);
    anim.play("idle");             // aucune durée -> coupe franche, comme en A1
    anim.update(0.01f, h);

    REQUIRE_FALSE(anim.isFading());
    REQUIRE_THAT(h.local(0).x, WithinAbs(200.0f, 0.01f));
}

// ⚠️ LE cas ré-entrant, et la raison du snapshot blending. Dans un jeu de plateforme, changer
// d'état PENDANT un fondu est la situation normale (marche -> saut -> chute en trois frames).
// Si le nouveau fondu repartait du clip entrant plutôt que de la pose réellement affichée, le
// personnage SAUTERAIT à l'instant du second play -- un à-coup d'autant plus gros que le premier
// fondu était peu avancé.
TEST_CASE("Animator: changer d'état PENDANT un fondu ne provoque aucun à-coup", "[anim][animator][fade]") {
    Clip walk = makeRamp(0.0f, 100.0f, 1.0f);
    Clip idle = makeConstant(Property::TranslationX, 200.0f);
    Clip fall = makeConstant(Property::TranslationX, -50.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &walk);
    anim.addState("idle", &idle);
    anim.addState("fall", &fall);

    anim.play("walk");
    anim.update(0.50f, h);
    anim.play("idle", 0.40f);
    anim.update(0.10f, h);                       // fondu au quart -> 50 + (200-50)*0.25 = 87.5
    const float displayed = h.local(0).x;
    REQUIRE_THAT(displayed, WithinAbs(87.5f, 0.01f));

    // Second basculement en plein fondu. Au tout début du nouveau fondu (poids 0), la pose doit
    // valoir EXACTEMENT ce qui était affiché -- c'est la définition de « pas d'à-coup ».
    anim.play("fall", 0.40f);
    anim.update(0.0f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(displayed, 0.01f));
}

// ============================================================================
// Tranche A3 — `Once{"cible"}` : un état joué UNE fois qui bascule tout seul.
//
// L'archétype universel : attaque -> idle, atterrissage -> idle, porte qui s'ouvre -> ouverte.
// C'est une propriété de l'ANIMATION (« ce clip ne boucle pas et il mène là »), pas une décision
// de gameplay — d'où sa place ici et pas chez le jeu. La frontière du §1 du plan tient : le jeu
// dit toujours QUAND attaquer, le moteur ne fait qu'enchaîner ce qui suit la dernière image.
// ============================================================================

TEST_CASE("Animator: un état Once bascule sur sa cible quand le clip finit", "[anim][animator][once]") {
    Clip attack = makeConstant(Property::TranslationX, 300.0f, 0.5f);   // 0.5 s
    Clip idle   = makeConstant(Property::TranslationX, 10.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("attack", &attack, Once{"idle"});
    anim.addState("idle", &idle);

    anim.play("attack");
    anim.update(0.30f, h);
    REQUIRE(anim.current() == "attack");        // pas encore fini : on ne bascule PAS

    anim.update(0.30f, h);                      // 0.6 s > 0.5 s : le clip est allé au bout
    REQUIRE(anim.current() == "idle");
}

TEST_CASE("Animator: un état Once ne boucle pas", "[anim][animator][once]") {
    Clip ramp = makeRamp(0.0f, 100.0f, 1.0f);
    Clip idle = makeConstant(Property::TranslationX, 10.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("land", &ramp, Once{"idle"});
    anim.addState("idle", &idle);

    anim.play("land");
    anim.update(0.90f, h);
    // Un état bouclé serait revenu vers 0 après 1 s ; celui-ci doit finir sa rampe, pas la reprendre.
    REQUIRE_THAT(h.local(0).x, WithinAbs(90.0f, 0.01f));
    REQUIRE(anim.current() == "land");
}

// La bascule automatique doit emprunter le MÊME fondu que les autres : sans ça, l'enchaînement
// attaque -> idle serait la seule transition du système à produire une coupe franche, et on aurait
// résolu la couture partout sauf là où elle se voit le plus.
TEST_CASE("Animator: la bascule d'un Once passe par le fondu par défaut", "[anim][animator][once]") {
    Clip attack = makeConstant(Property::TranslationX, 300.0f, 0.5f);
    Clip idle   = makeConstant(Property::TranslationX, 10.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.setDefaultFade(0.20f);
    anim.addState("attack", &attack, Once{"idle"});
    anim.addState("idle", &idle);

    anim.play("attack", 0.0f);          // entrée franche pour isoler la SORTIE
    anim.update(0.60f, h);              // le clip finit -> bascule vers idle
    REQUIRE(anim.current() == "idle");
    REQUIRE(anim.isFading());

    // À mi-fondu : 300 (dernière image de l'attaque) -> 10 (idle), soit 155.
    anim.update(0.10f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(155.0f, 0.01f));
}

TEST_CASE("Animator: setDefaultFade s'applique à un play() sans durée", "[anim][animator][once]") {
    Clip walk = makeConstant(Property::TranslationX, 100.0f);
    Clip idle = makeConstant(Property::TranslationX, 0.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.setDefaultFade(0.20f);
    anim.addState("walk", &walk);
    anim.addState("idle", &idle);

    anim.play("walk");
    anim.update(0.10f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(100.0f, 0.01f));

    anim.play("idle");                  // aucune durée -> le défaut posé, pas 0
    anim.update(0.10f, h);
    REQUIRE(anim.isFading());
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.01f));   // mi-chemin 100 -> 0
}

// NON-RÉGRESSION : sans setDefaultFade, un play() sans durée reste une coupe franche. C'est ce qui
// garantit que tout le code écrit contre A1/A2 ne change pas de comportement en silence.
TEST_CASE("Animator: sans defaultFade posé, play() sans durée reste franc", "[anim][animator][once]") {
    Clip walk = makeConstant(Property::TranslationX, 100.0f);
    Clip idle = makeConstant(Property::TranslationX, 0.0f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("walk", &walk);
    anim.addState("idle", &idle);

    anim.play("walk");
    anim.update(0.10f, h);
    anim.play("idle");
    anim.update(0.01f, h);

    REQUIRE_FALSE(anim.isFading());
    REQUIRE_THAT(h.local(0).x, WithinAbs(0.0f, 0.01f));
}

// Fail-soft, comme play() sur un nom inconnu : une cible qui n'existe pas fige l'état sur sa
// dernière image plutôt que de vider l'animateur. Un perso figé se voit et se corrige ; un
// animateur vidé ou un crash sur une faute de frappe, non.
TEST_CASE("Animator: un Once vers une cible inconnue fige sans casser", "[anim][animator][once]") {
    Clip attack = makeConstant(Property::TranslationX, 300.0f, 0.5f);
    Hierarchy h = makeOneNode();

    Animator anim;
    anim.addState("attack", &attack, Once{"idel"});     // faute de frappe volontaire

    anim.play("attack");
    anim.update(0.60f, h);
    REQUIRE(anim.current() == "attack");
    REQUIRE(anim.finished());

    anim.update(0.60f, h);                              // et ça ne s'emballe pas au tour suivant
    REQUIRE(anim.current() == "attack");
    REQUIRE_THAT(h.local(0).x, WithinAbs(300.0f, 0.01f));
}
