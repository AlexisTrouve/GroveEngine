/**
 * Unit Tests — le CONTRAT de routage d'ENTRÉE de UIWidget (souris, molette, focus, saisie).
 *
 * QUOI     : les prédicats que le routeur interroge à la place d'énumérer les types concrets —
 *            `absorbsPoint` / `surfacesClick` (souris, S1a), `handleMouseWheel` / `acceptsFocus`
 *            (molette et focus, S1b), `submitsOn` / `swallowsSubmitKey` (contrat de saisie, S1b).
 *
 * POURQUOI : ce test existe à cause d'un défaut RÉEL, introduit puis rattrapé pendant S1a.
 *            L'ancienne fonction finissait par `return handled ? target : nullptr;`, ce qui rendait
 *            INATTEIGNABLES les six `return target` anticipés placés au-dessus. Les conditions qui
 *            les gardaient — `handled && !pressed && …` pour le bouton, `handled && !pressed` pour la
 *            roue — ne changeaient donc jamais le résultat. Les transcrire fidèlement dans
 *            `surfacesClick`, ce qui semblait la chose prudente à faire, RÉDUISAIT le comportement :
 *            bouton et roue cessaient d'être remontés au press.
 *
 *            Et la suite complète est passée 204/204 AVEC ce défaut. Rien ne l'observait : le module
 *            ne réagit à ces deux widgets qu'au relâchement, donc l'écart n'avait aucun effet visible
 *            par l'IIO — jusqu'au jour où quelqu'un s'appuierait sur le press. C'est exactement le
 *            genre de trou qu'un test de bout en bout ne peut pas boucher : il faut interroger le
 *            contrat lui-même.
 *
 * COMMENT  : on n'a besoin ni de fenêtre, ni d'IIO, ni de renderer — les deux prédicats sont purs.
 *            On instancie les widgets, on pose une géométrie connue, on lit la matrice.
 */

#include <catch2/catch_test_macros.hpp>

#include "Widgets/UIButton.h"
#include "Widgets/UICheckbox.h"
#include "Widgets/UILabel.h"
#include "Widgets/UIList.h"
#include "Widgets/UIModal.h"
#include "Widgets/UIPanel.h"
#include "Widgets/UIRadial.h"
#include "Widgets/UISlider.h"
#include "Widgets/UITabs.h"
#include "Widgets/UIScrollPanel.h"
#include "Widgets/UITextArea.h"
#include "Widgets/UITextInput.h"

using namespace grove;

namespace {

// Pose une boîte connue en coordonnées ABSOLUES (le hit-test travaille en absolu).
void placeAt(UIWidget& w, float x, float y, float width, float height) {
    w.x = x; w.y = y; w.width = width; w.height = height;
    w.absX = x; w.absY = y;
}

}  // namespace

// ============================================================================
// surfacesClick — la matrice « le module doit-il en être saisi ? »
// ============================================================================

TEST_CASE("Contrat: par defaut, un widget n'est remonte que s'il a CONSOMME", "[ui][unit][contract]") {
    // Le défaut couvre slider, checkbox, saisies… et tout widget décoratif.
    UIPanel panel;
    CHECK_FALSE(panel.surfacesClick(true,  false));
    CHECK_FALSE(panel.surfacesClick(false, false));
    CHECK(panel.surfacesClick(true,  true));
    CHECK(panel.surfacesClick(false, true));
}

TEST_CASE("Contrat: le BOUTON est remonte au PRESS comme au RELACHEMENT", "[ui][unit][contract]") {
    // ⚠️ LE GARDE-FOU DE LA RÉGRESSION. La condition d'origine (`handled && !pressed && a de quoi
    // émettre`) était du code MORT : la sortie de secours renvoyait déjà la cible dès que `handled`.
    // Un bouton consommant un press DOIT donc rester remonté.
    UIButton btn;
    CHECK(btn.surfacesClick(/*pressed*/ true,  /*handled*/ true));
    CHECK(btn.surfacesClick(/*pressed*/ false, /*handled*/ true));
    CHECK_FALSE(btn.surfacesClick(true,  false));

    // ...et cela ne doit dépendre NI d'un onClick, NI d'un événement déclaratif : c'est précisément
    // ce filtre-là qui n'a jamais eu d'effet.
    UIButton bare;                       // aucun onClick, aucun eventBindings
    CHECK(bare.surfacesClick(true, true));
    UIButton withAction;
    withAction.onClick = "game:start";
    CHECK(withAction.surfacesClick(true, true));
}

TEST_CASE("Contrat: la ROUE est remontee au PRESS comme au RELACHEMENT", "[ui][unit][contract]") {
    // Jumelle du cas bouton : `handled && !pressed` etait mort de la meme facon.
    UIRadial wheel;
    CHECK(wheel.surfacesClick(true,  true));
    CHECK(wheel.surfacesClick(false, true));
    CHECK_FALSE(wheel.surfacesClick(true, false));
}

TEST_CASE("Contrat: onglets et modale sont remontes au PRESS SANS avoir consomme",
          "[ui][unit][contract]") {
    // Ces deux-là ne consomment rien : c'est le module qui résout le changement d'onglet et la
    // fermeture au clic extérieur (lui seul peut publier). Sans ce cas particulier, un clic hors
    // dialogue ne fermerait plus une modale.
    UITabs tabs;
    CHECK(tabs.surfacesClick(/*pressed*/ true,  /*handled*/ false));
    CHECK_FALSE(tabs.surfacesClick(/*pressed*/ false, /*handled*/ false));

    UIModal modal;
    CHECK(modal.surfacesClick(true,  false));
    CHECK_FALSE(modal.surfacesClick(false, false));
}

TEST_CASE("Contrat: la LISTE est remontee sur les deux fronts, consommes ou non",
          "[ui][unit][contract]") {
    // Le press peut amorcer un défilement par glisser, le relâchement peut sélectionner une ligne.
    UIList list;
    CHECK(list.surfacesClick(true,  false));
    CHECK(list.surfacesClick(false, false));
    CHECK(list.surfacesClick(true,  true));
    CHECK(list.surfacesClick(false, true));
}

// ============================================================================
// absorbsPoint — l'opacité au pointeur
// ============================================================================

TEST_CASE("Contrat: un widget DECORATIF est transparent au clic", "[ui][unit][contract]") {
    // C'est le défaut, et il porte : sans lui, un panneau de fond intercepterait les clics destinés
    // à ce qu'il contient. (Le hit-test descend d'abord dans les enfants, puis demande au parent.)
    UIPanel panel;   placeAt(panel, 0, 0, 100, 100);
    UILabel label;   placeAt(label, 0, 0, 100, 100);
    CHECK_FALSE(panel.absorbsPoint(50, 50));
    CHECK_FALSE(label.absorbsPoint(50, 50));
}

TEST_CASE("Contrat: les widgets interactifs absorbent DANS leur boite, pas dehors",
          "[ui][unit][contract]") {
    // Vérifie que chaque `absorbsPoint` délègue bien au prédicat du widget — une délégation vers le
    // mauvais prédicat rendrait le widget cliquable au mauvais endroit, ce qu'aucun test de bout en
    // bout ne verrait tant qu'on clique au centre.
    UIButton btn;       placeAt(btn, 10, 10, 80, 40);
    UICheckbox check;   placeAt(check, 200, 10, 120, 24);
    UISlider slider;    placeAt(slider, 10, 100, 200, 20);
    UITextInput input;  placeAt(input, 10, 150, 200, 30);

    CHECK(btn.absorbsPoint(50, 30));        CHECK_FALSE(btn.absorbsPoint(5, 30));
    CHECK(check.absorbsPoint(210, 20));     CHECK_FALSE(check.absorbsPoint(190, 20));
    CHECK(slider.absorbsPoint(100, 110));   CHECK_FALSE(slider.absorbsPoint(100, 90));
    CHECK(input.absorbsPoint(100, 160));    CHECK_FALSE(input.absorbsPoint(100, 200));
}

TEST_CASE("Contrat: la ROUE absorbe sur un DISQUE, pas sur un rectangle", "[ui][unit][contract]") {
    // Discrimination qui vaut le détour : la roue est centrée sur (x,y) et son prédicat est radial.
    // Un coin de la boîte englobante est HORS du disque — une délégation vers un test rectangulaire
    // passerait tous les autres cas et échouerait seulement ici.
    UIRadial wheel;
    wheel.x = 400; wheel.y = 300; wheel.absX = 400; wheel.absY = 300;
    wheel.innerRadius = 40; wheel.outerRadius = 160;

    CHECK(wheel.absorbsPoint(400 + 100, 300));          // dans la couronne
    CHECK_FALSE(wheel.absorbsPoint(400 + 200, 300));    // au-dela du rayon exterieur
    CHECK_FALSE(wheel.absorbsPoint(400 + 150, 300 + 150));  // coin de la boite englobante
}

// ============================================================================
// S1b — molette, focus, et le contrat de SOUMISSION des widgets de saisie
// ============================================================================

TEST_CASE("Contrat: seuls les hotes de defilement retiennent la molette", "[ui][unit][contract]") {
    // `true` = "je suis l'hote, arrete de remonter" -- PAS "j'ai defile". Une liste en butee retient
    // quand meme la molette : c'est le comportement d'origine (la remontee s'arretait au premier
    // hote trouve sans regarder s'il avait bouge), et le rendre conditionnel serait un AUTRE
    // comportement, pas une correction.
    UIList list;
    UIScrollPanel scroll;
    UIPanel panel;
    UIButton btn;

    CHECK(list.handleMouseWheel(1.0f));
    CHECK(scroll.handleMouseWheel(1.0f));
    CHECK_FALSE(panel.handleMouseWheel(1.0f));
    CHECK_FALSE(btn.handleMouseWheel(1.0f));

    // Une liste vide n'a rien a faire defiler -- elle retient la molette malgre tout.
    UIList empty;
    CHECK(empty.handleMouseWheel(-1.0f));
}

TEST_CASE("Contrat: seuls les widgets de saisie prennent le focus clavier", "[ui][unit][contract]") {
    UITextInput input;
    UITextArea area;
    UIButton btn;
    UIPanel panel;

    CHECK(input.acceptsFocus());
    CHECK(area.acceptsFocus());
    CHECK_FALSE(btn.acceptsFocus());
    CHECK_FALSE(panel.acceptsFocus());
}

TEST_CASE("Contrat: le champ soumet sur ENTREE, la zone sur CTRL+ENTREE",
          "[ui][unit][contract]") {
    UITextInput input;
    UITextArea area;

    constexpr int kEnter = 13, kReturn = 10, kLetterA = 65;

    // Champ monoligne : Entree soumet, avec ou sans Ctrl (le comportement d'origine ne regardait
    // pas le modificateur).
    CHECK(input.submitsOn(kEnter,  false));
    CHECK(input.submitsOn(kReturn, false));
    CHECK(input.submitsOn(kEnter,  true));
    CHECK_FALSE(input.submitsOn(kLetterA, false));

    // Zone multiligne : Entree SEULE insere un saut de ligne, donc elle ne soumet pas.
    CHECK_FALSE(area.submitsOn(kEnter, false));
    CHECK(area.submitsOn(kEnter,  true));
    CHECK(area.submitsOn(kReturn, true));
    CHECK_FALSE(area.submitsOn(kLetterA, true));
}

TEST_CASE("Contrat: seule la ZONE avale sa touche de soumission", "[ui][unit][contract]") {
    // LE garde-fou du second piege de S1b. Les deux widgets ne soumettent pas au meme MOMENT :
    //   - la zone avale Ctrl+Entree AVANT que la touche atteigne le widget (sinon elle inserait un
    //     saut de ligne EN PLUS de soumettre) ;
    //   - le champ laisse Entree traverser, et soumet APRES la frappe -- il publie donc
    //     ui:text_changed PUIS ui:text_submit.
    // Aligner les deux "pour simplifier" casse silencieusement l'un des deux.
    UITextInput input;
    UITextArea area;

    CHECK_FALSE(input.swallowsSubmitKey());
    CHECK(area.swallowsSubmitKey());
}
