/**
 * Integration Test IT_063: SÉLECTION de texte dans un UITextInput (E2E, vrai module).
 *
 * QUOI     : sélectionner au clavier (Maj+flèches, Maj+Début/Fin, Ctrl+A) et à la souris (glisser,
 *            double-clic), puis prouver que taper ou effacer REMPLACE la sélection.
 *
 * POURQUOI : l'en-tête du widget annonce « Text selection (future) » depuis toujours
 *            (UITextInput.h:49) — rien n'existe. Et les modificateurs n'arrivaient même pas :
 *            UIModule.cpp portait `bool ctrl = false; // TODO: Add ctrl modifier to UIContext`,
 *            ce qui rendait INDÉTECTABLES Ctrl+A/C/V — les branches correspondantes de onKeyInput
 *            étaient donc du code mort. InputModule publie pourtant shift/ctrl/alt depuis toujours
 *            (InputConverter.cpp:38-41) : le câble existait, il n'était pas branché.
 *
 * COMMENT  : ni le curseur ni la sélection ne sont publiés. On les prouve donc INDIRECTEMENT, par
 *            leur effet observable sur ui:text_changed : taper sur une sélection la remplace, donc
 *            le texte résultant révèle exactement ce qui était sélectionné. C'est une preuve plus
 *            forte qu'un accesseur — elle teste le comportement que l'utilisateur subit.
 */

#include <catch2/catch_test_macros.hpp>

#include "helpers/UITextInputHarness.h"

#include <map>

using namespace grove;
using namespace grove::uitest;

// ============================================================================
// Sélection au clavier
// ============================================================================

TEST_CASE("IT_063: Maj+Droite selectionne, et la frappe REMPLACE", "[integration][ui][e2e][selection]") {
    TextInputHarness h("sel_shift_right");
    h.focusField();

    h.type("abcd");
    h.pressKey(kScanHome);                    // curseur au debut
    h.pressKey(kScanRight, /*shift=*/true);   // selectionne "a"
    h.pressKey(kScanRight, /*shift=*/true);   // selectionne "ab"
    h.type("X");                              // doit REMPLACER "ab"

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "Xcd");
}

TEST_CASE("IT_063: Maj+Gauche selectionne vers l'arriere", "[integration][ui][e2e][selection]") {
    TextInputHarness h("sel_shift_left");
    h.focusField();

    h.type("abcd");                          // curseur en fin
    h.pressKey(kScanLeft, /*shift=*/true);   // selectionne "d"
    h.pressKey(kScanLeft, /*shift=*/true);   // selectionne "cd"
    h.type("Z");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abZ");
}

TEST_CASE("IT_063: une fleche SANS Maj replie la selection sans rien effacer",
          "[integration][ui][e2e][selection]") {
    // Convention universelle : une fleche nue annule la selection et pose le curseur sur le BORD
    // correspondant. Un widget qui effacerait la selection, ou qui la garderait, surprendrait.
    TextInputHarness h("sel_collapse");
    h.focusField();

    h.type("abcd");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);  // "a" selectionne
    h.pressKey(kScanRight, /*shift=*/true);  // "ab" selectionne
    h.pressKey(kScanRight);                  // repli SANS Maj -> curseur au bord droit (index 2)
    h.type("X");                             // insere, ne remplace pas

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abXcd");
}

TEST_CASE("IT_063: Maj+Fin puis Maj+Debut selectionnent jusqu'aux extremites",
          "[integration][ui][e2e][selection]") {
    TextInputHarness h("sel_homeend");
    h.focusField();

    h.type("abcd");
    h.pressKey(kScanHome);
    h.pressKey(kScanEnd, /*shift=*/true);  // selectionne tout depuis le debut
    h.type("Q");
    REQUIRE(h.lastText == "Q");

    h.type("wxyz");                          // "Qwxyz"
    h.pressKey(kScanHome, /*shift=*/true);   // selectionne tout depuis la fin
    h.type("R");
    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "R");
}

TEST_CASE("IT_063: Ctrl+A selectionne tout", "[integration][ui][e2e][selection]") {
    // Verrouille aussi la levee du `bool ctrl = false; // TODO` : sans le modificateur propage,
    // Ctrl+A est indetectable et cette assertion ne peut pas passer.
    TextInputHarness h("sel_ctrl_a");
    h.focusField();

    h.type("bonjour");
    h.pressKey(kScanA, /*shift=*/false, /*ctrl=*/true);
    h.type("!");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "!");
}

// ============================================================================
// Suppression sur une sélection
// ============================================================================

TEST_CASE("IT_063: Backspace sur une selection efface la SELECTION, pas un caractere",
          "[integration][ui][e2e][selection]") {
    TextInputHarness h("sel_backspace");
    h.focusField();

    h.type("abcd");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);  // "ab" selectionne
    h.pressKey(kScanBackspace);

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "cd");  // et NON "acd" (un seul caractere efface)
}

TEST_CASE("IT_063: Suppr sur une selection efface la SELECTION", "[integration][ui][e2e][selection]") {
    TextInputHarness h("sel_delete");
    h.focusField();

    h.type("abcd");
    h.pressKey(kScanEnd);
    h.pressKey(kScanLeft, /*shift=*/true);
    h.pressKey(kScanLeft, /*shift=*/true);  // "cd" selectionne
    h.pressKey(kScanDelete);

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "ab");
}

// ============================================================================
// UTF-8 : une sélection ne doit jamais couper un codepoint
// ============================================================================

TEST_CASE("IT_063: une selection sur du texte accentue reste sur des frontieres de caracteres",
          "[integration][ui][e2e][selection][utf8]") {
    // La jonction des deux chantiers : la selection se deplace en CARACTERES, donc remplacer une
    // selection d'accents ne peut pas laisser de demi-sequence UTF-8.
    TextInputHarness h("sel_utf8");
    h.focusField();

    const std::string base = kEAigu + kEAigu + std::string("z");  // "ééz"
    h.type(base);
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);  // selectionne le 1er 'é' ENTIER (2 octets)
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "X" + kEAigu + std::string("z"));
}

// ============================================================================
// Sélection à la souris
// ============================================================================

TEST_CASE("IT_063: glisser la souris selectionne l'intervalle balaye",
          "[integration][ui][e2e][selection][mouse]") {
    // La police par defaut du harnais est la 8x8 monospace (pas de renderer, donc pas de table
    // d'avances) : chaque caractere fait 8px. "abcdefgh" occupe donc 108..172.
    // On presse a 108 (avant 'a') et on relache a 132 (apres 'c') -> "abc" selectionne.
    TextInputHarness h("sel_drag");
    h.focusField();

    h.type("abcdefgh");

    h.moveMouse(kTextOriginX, kFieldCenterY);
    h.mouseButton(true, kTextOriginX, kFieldCenterY);        // presse avant 'a'
    h.moveMouse(kTextOriginX + 24.0, kFieldCenterY);         // glisse jusqu'apres 'c'
    h.mouseButton(false, kTextOriginX + 24.0, kFieldCenterY);
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "Xdefgh");
}

TEST_CASE("IT_063: un simple clic ne selectionne RIEN (non-regression)",
          "[integration][ui][e2e][selection][mouse]") {
    // Garde-fou : le glisser ne doit pas transformer chaque clic en selection. Un clic pose le
    // curseur et la frappe INSERE.
    TextInputHarness h("sel_click_only");
    h.focusField();

    h.type("abcd");
    h.clickAt(kTextOriginX + 16.0, kFieldCenterY);  // clic net entre 'b' et 'c'
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abXcd");
}

// ============================================================================
// Ce que le RENDERER recevra réellement.
//
// POURQUOI ce bloc : les tests ci-dessus prouvent le MODÈLE de sélection (quel intervalle est
// sélectionné), pas ce qui sera DESSINÉ. Or les deux vraies façons de rater un surlignage sont
// géométriques — un rectangle décalé du texte qu'il surligne — et d'ORDRE : peint PAR-DESSUS le
// texte, il le rend illisible. Aucune des deux ne se voit dans le modèle.
//
// L'UIModule publiant ses primitives sur IIO, ces deux propriétés sont directement observables :
// on écoute `render:sprite:*` (updateRect passe par là) et `render:text:*`, et on assène la
// géométrie exacte + la relation de couches. C'est la leçon du chantier 9-slice appliquée au
// niveau où elle est vérifiable sans GPU.
// ============================================================================

namespace {

struct DrawnRect { double cx = 0, cy = 0, w = 0, h = 0; int color = 0; int layer = -1; };
struct DrawnText { double x = 0; std::string text; int layer = -1; };

// La couleur de surlignage par défaut du widget (TextInputStyle::selectionColor).
constexpr int kSelectionColor = static_cast<int>(0x4444AAAA);

}  // namespace

TEST_CASE("IT_063: le surlignage publie est aligne sur la selection, et PASSE SOUS le texte",
          "[integration][ui][e2e][selection][render]") {
    TextInputHarness h("sel_render");

    // Sans renderer, la police reste la 8x8 monospace : chaque caractere fait exactement 8px, donc
    // toutes les valeurs attendues se comptent a la main. Le texte commence a kTextOriginX = 108.
    std::map<int, DrawnRect> rects;
    std::map<int, DrawnText> texts;
    auto onRect = [&](const Message& m) {
        DrawnRect r;
        r.cx = m.data->getDouble("cx", 0.0);
        r.cy = m.data->getDouble("cy", 0.0);
        r.w  = m.data->getDouble("scaleX", 0.0);
        r.h  = m.data->getDouble("scaleY", 0.0);
        r.color = m.data->getInt("color", 0);
        r.layer = m.data->getInt("layer", -1);
        rects[m.data->getInt("renderId", -1)] = r;
    };
    auto onText = [&](const Message& m) {
        DrawnText t;
        t.x = m.data->getDouble("x", 0.0);
        t.text = m.data->getString("text", "");
        t.layer = m.data->getInt("layer", -1);
        texts[m.data->getInt("renderId", -1)] = t;
    };
    h.observer->subscribe("render:sprite:add", onRect);
    h.observer->subscribe("render:sprite:update", onRect);
    h.observer->subscribe("render:text:add", onText);
    h.observer->subscribe("render:text:update", onText);

    h.focusField();
    h.type("abcdefgh");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);  // "abc" selectionne
    h.pump();                                 // une frame de rendu de plus, pour publier l'etat courant

    // --- Le rectangle de surlignage existe et couvre EXACTEMENT "abc" ---
    const DrawnRect* highlight = nullptr;
    for (const auto& [id, r] : rects) {
        if (r.color == kSelectionColor && r.w > 0.0) { highlight = &r; break; }
    }
    REQUIRE(highlight != nullptr);           // sans surlignage publie, la selection est invisible

    // "abc" = 3 x 8 = 24px de large, a partir de 108 -> centre a 120 (cx = CENTRE, cf. convention
    // d'ancrage du moteur : x,y = coin / cx,cy = centre).
    REQUIRE(highlight->w == 24.0);
    REQUIRE(highlight->cx == 120.0);

    // --- Il passe AU-DESSUS du fond du champ ---
    // C'est LA relation qui doit tenir par les couches : surlignage et fond sont tous deux des
    // sprites, donc triés entre eux par couche au sein d'une meme passe. (Le texte, lui, part dans
    // TextPass, qui s'execute apres SpritePass quelles que soient les couches -- le surlignage est
    // donc sous le texte par construction du graphe de rendu, pas par comparaison de couches.)
    //
    // Ce cas est ne d'un VRAI bug : le renderer retained FIGE la couche d'une entree a sa premiere
    // publication, et le widget publiait ses entrees cachees avec un 0 litteral. La toute premiere
    // frame passant par la branche placeholder, le surlignage -- ET LE CURSEUR -- se retrouvaient
    // definitivement a la couche 0, donc DERRIERE le fond du champ. Le curseur de saisie n'etait en
    // fait jamais visible.
    const DrawnRect* fieldBg = nullptr;
    for (const auto& [id, r] : rects) {
        if (r.color == static_cast<int>(0x222222FF) && r.w == 300.0) { fieldBg = &r; break; }
    }
    REQUIRE(fieldBg != nullptr);
    INFO("surlignage layer=" << highlight->layer << " fond layer=" << fieldBg->layer);
    REQUIRE(highlight->layer > fieldBg->layer);

    // Et le CURSEUR, meme cause meme correctif : lui aussi doit passer au-dessus du fond.
    const DrawnRect* caret = nullptr;
    for (const auto& [id, r] : rects) {
        if (r.color == static_cast<int>(0xFFFFFFFF) && r.w == 2.0) { caret = &r; break; }
    }
    REQUIRE(caret != nullptr);
    REQUIRE(caret->layer > fieldBg->layer);

    // Le texte est bien la, a l'origine attendue.
    const DrawnText* body = nullptr;
    for (const auto& [id, t] : texts) {
        if (t.text == "abcdefgh") { body = &t; break; }
    }
    REQUIRE(body != nullptr);
    REQUIRE(body->x == 108.0);
}

TEST_CASE("IT_063: sans selection, aucun surlignage n'est peint",
          "[integration][ui][e2e][selection][render]") {
    // Le pendant discriminant : si le rectangle etait publie en permanence (ne serait-ce qu'avec une
    // largeur nulle mais une couleur pleine), le test precedent passerait pour de mauvaises raisons.
    TextInputHarness h("sel_render_none");

    std::map<int, DrawnRect> rects;
    auto onRect = [&](const Message& m) {
        DrawnRect r;
        r.w = m.data->getDouble("scaleX", 0.0);
        r.color = m.data->getInt("color", 0);
        rects[m.data->getInt("renderId", -1)] = r;
    };
    h.observer->subscribe("render:sprite:add", onRect);
    h.observer->subscribe("render:sprite:update", onRect);

    h.focusField();
    h.type("abcdefgh");   // curseur en fin, AUCUNE selection
    h.pump();

    for (const auto& [id, r] : rects) {
        if (r.color == kSelectionColor) {
            REQUIRE(r.w == 0.0);  // presente mais repliee a zero, ou absente : jamais visible
        }
    }
}

// ============================================================================
// Double-clic — sélection du mot.
// ============================================================================

TEST_CASE("IT_063: un double-clic selectionne le MOT sous le curseur",
          "[integration][ui][e2e][selection][mouse]") {
    // Police 8x8 monospace (pas de renderer) : "bonjour le monde", le mot "le" occupe les index
    // 8..10, soit les pixels 108+64=172 a 108+80=188. On double-clique au milieu, a 178.
    TextInputHarness h("sel_dblclick");
    h.focusField();

    h.type("bonjour le monde");
    h.clickAt(178.0, kFieldCenterY);
    h.clickAt(178.0, kFieldCenterY);  // second clic rapide au meme endroit
    h.type("X");                       // doit REMPLACER "le"

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "bonjour X monde");
}

TEST_CASE("IT_063: un double-clic sur un mot ACCENTUE le prend en entier",
          "[integration][ui][e2e][selection][mouse][utf8]") {
    // Le cas francais. "caf" + e-aigu = 5 octets mais 4 caracteres affiches ; en monospace 8px le mot
    // occupe donc 108..140. Double-clic a 120, dans le mot.
    // Une segmentation ASCII-only s'arreterait avant l'accent et laisserait l'accent orphelin.
    TextInputHarness h("sel_dblclick_utf8");
    h.focusField();

    h.type("le caf" + kEAigu + std::string(" chaud"));
    h.clickAt(kTextOriginX + 36.0, kFieldCenterY);  // dans "café"
    h.clickAt(kTextOriginX + 36.0, kFieldCenterY);
    h.type("THE");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "le THE chaud");
}

TEST_CASE("IT_063: deux clics ELOIGNES ne forment pas un double-clic",
          "[integration][ui][e2e][selection][mouse]") {
    // Discrimination : sans le test de proximite, deux clics rapides n'importe ou selectionneraient
    // un mot au hasard. Ici le second clic doit simplement REPOSER le curseur.
    TextInputHarness h("sel_dblclick_far");
    h.focusField();

    h.type("bonjour le monde");
    h.clickAt(kTextOriginX + 8.0, kFieldCenterY);    // dans "bonjour"
    h.clickAt(kTextOriginX + 72.0, kFieldCenterY);   // loin : dans "le"
    h.type("X");                                      // insere, ne remplace pas

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "bonjour lXe monde");
}
