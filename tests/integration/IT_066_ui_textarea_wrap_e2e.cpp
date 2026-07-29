/**
 * Integration Test IT_066: RETOUR À LA LIGNE AUTOMATIQUE dans UITextArea (E2E, vrai module).
 *
 * QUOI     : une ligne trop large se replie sur plusieurs rangées, et TOUT ce qui traduit entre
 *            (rangée, colonne) et pixels suit ce repli — le texte dessiné, le clic, les flèches
 *            Haut/Bas, Début/Fin.
 *
 * POURQUOI ce test regarde CE QUE LE RENDERER REÇOIT : le repli est une affaire de mise en page, et
 *            une mise en page fausse ne se voit pas dans le modèle — le texte y est intact, c'est son
 *            découpage à l'écran qui ment. L'UIModule publiant ses primitives sur IIO, les rangées
 *            réellement dessinées sont directement observables (`render:text:*`), ce qui rend
 *            l'assertion exacte sans GPU.
 *
 * LE PIÈGE QUE CE TEST VERROUILLE : lignes LOGIQUES vs lignes VISUELLES. Avec le repli, une flèche
 *            Bas doit descendre d'UNE RANGÉE, pas sauter la ligne logique entière (qui peut en
 *            occuper cinq). Un clic doit viser la rangée sous le curseur, pas la n-ième ligne
 *            logique. Se tromper de notion à un seul de ces endroits produit un décalage que rien ne
 *            rattrape — et aucun test du modèle ne l'attraperait.
 *
 * COMMENT  : la zone fait 300px de large, PADDING 8 → 284px utilisables ; sans renderer la police
 *            reste la 8x8 monospace, donc 35 caractères par rangée et toutes les attentes se
 *            comptent à la main.
 */

#include <catch2/catch_test_macros.hpp>

#include "helpers/UITextInputHarness.h"

#include <map>
#include <string>
#include <vector>

using namespace grove;
using namespace grove::uitest;

namespace {

constexpr double kAreaCenterX = 250.0;
constexpr double kAreaFirstLineY = 118.0;

// Texte de 39 caracteres : 34 tiennent sur une rangee de 284px (34 x 8 = 272), le mot suivant non.
const std::string kLongLine = "aaaa bbbb cccc dddd eeee ffff gggg hhhh";

// Collecte les rangees de texte REELLEMENT publiees au renderer, dans l'ordre vertical.
struct DrawnRows {
    std::map<int, std::pair<double, std::string>> byId;   // renderId -> (y, texte)

    void attach(TextInputHarness& h) {
        auto onText = [this](const Message& m) {
            byId[m.data->getInt("renderId", -1)] =
                { m.data->getDouble("y", 0.0), m.data->getString("text", "") };
        };
        h.observer->subscribe("render:text:add", onText);
        h.observer->subscribe("render:text:update", onText);
    }

    // Rangees non vides, triees par ordonnee — c'est ce que l'utilisateur voit, de haut en bas.
    std::vector<std::string> rows() const {
        std::vector<std::pair<double, std::string>> v;
        for (const auto& [id, e] : byId) {
            if (!e.second.empty()) v.push_back(e);
        }
        std::sort(v.begin(), v.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<std::string> out;
        for (const auto& e : v) out.push_back(e.second);
        return out;
    }
};

}  // namespace

// ============================================================================
// Le repli lui-même
// ============================================================================

TEST_CASE("IT_066: une ligne trop large est repliee sur plusieurs rangees",
          "[integration][ui][e2e][textarea][wrap]") {
    TextInputHarness h("wrap_basic", "../../assets/ui/test_e2e_textarea.json");
    DrawnRows drawn;
    drawn.attach(h);

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type(kLongLine);
    h.pump();

    const auto rows = drawn.rows();
    INFO("rangees dessinees : " << rows.size());
    REQUIRE(rows.size() == 2u);
    REQUIRE(rows[0] == "aaaa bbbb cccc dddd eeee ffff gggg");   // 34 car. = 272px <= 284
    REQUIRE(rows[1] == "hhhh");                                  // le mot suivant descend en entier
}

TEST_CASE("IT_066: l'espace de coupe ne reapparait pas en tete de rangee",
          "[integration][ui][e2e][textarea][wrap]") {
    // L'alinea fantome : si l'espace repartait avec la rangee suivante, tout le texte semblerait
    // indente d'un cran une rangee sur deux.
    TextInputHarness h("wrap_space", "../../assets/ui/test_e2e_textarea.json");
    DrawnRows drawn;
    drawn.attach(h);

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type(kLongLine);
    h.pump();

    for (const std::string& r : drawn.rows()) {
        REQUIRE_FALSE(r.empty());
        REQUIRE(r.front() != ' ');
    }
}

TEST_CASE("IT_066: un mot plus large que la boite est coupe au caractere",
          "[integration][ui][e2e][textarea][wrap]") {
    // Sans coupe forcee, le mot deborderait sous le bord droit et disparaitrait sans indice.
    TextInputHarness h("wrap_longword", "../../assets/ui/test_e2e_textarea.json");
    DrawnRows drawn;
    drawn.attach(h);

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type(std::string(50, 'x'));   // 50 x 8 = 400px > 284
    h.pump();

    const auto rows = drawn.rows();
    REQUIRE(rows.size() == 2u);
    REQUIRE(rows[0].size() == 35u);   // 35 x 8 = 280 <= 284, le 36e deborderait
    REQUIRE(rows[1].size() == 15u);
}

TEST_CASE("IT_066: `wrap: false` retablit le decoupage strictement logique",
          "[integration][ui][e2e][textarea][wrap]") {
    // Le patron « defaut a cout zero » : desactiver le repli doit rendre EXACTEMENT une rangee par
    // ligne logique, sans chemin de code particulier.
    TextInputHarness h("wrap_off", "../../assets/ui/test_e2e_textarea_nowrap.json");
    DrawnRows drawn;
    drawn.attach(h);

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type(kLongLine);
    h.pump();

    const auto rows = drawn.rows();
    REQUIRE(rows.size() == 1u);
    REQUIRE(rows[0] == kLongLine);
}

// ============================================================================
// LIGNES VISUELLES vs LOGIQUES — là où une confusion se paierait
// ============================================================================

TEST_CASE("IT_066: la fleche Haut remonte d'UNE RANGEE, pas d'une ligne logique",
          "[integration][ui][e2e][textarea][wrap]") {
    // Le curseur est en fin ("hhhh", rangee 1, colonne 4 -> x = 32). Une fleche Haut doit le poser
    // sur la rangee 0 a la MEME abscisse, donc a l'index 4. Si la navigation suivait les lignes
    // LOGIQUES, il n'y aurait qu'une seule ligne et la fleche Haut irait au debut du texte (index 0).
    TextInputHarness h("wrap_up", "../../assets/ui/test_e2e_textarea.json");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type(kLongLine);
    h.pressKey(kScanUp);
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "aaaaX bbbb cccc dddd eeee ffff gggg hhhh");
}

TEST_CASE("IT_066: Debut et Fin agissent sur la RANGEE, pas sur la ligne logique",
          "[integration][ui][e2e][textarea][wrap]") {
    // Curseur en fin (rangee 1). Debut doit le poser au debut de "hhhh" (index 35), pas au debut du
    // texte. C'est le comportement d'un editeur : Debut ramene au bord GAUCHE DE CE QU'ON VOIT.
    TextInputHarness h("wrap_home", "../../assets/ui/test_e2e_textarea.json");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type(kLongLine);
    h.pressKey(kScanHome);
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "aaaa bbbb cccc dddd eeee ffff gggg Xhhhh");
}

TEST_CASE("IT_066: un clic vise la RANGEE sous le pointeur",
          "[integration][ui][e2e][textarea][wrap]") {
    // Rangee 0 : y 108..128 ; rangee 1 : 128..148. On clique a y=138, colonne 2 (x = 108 + 16).
    // La rangee 1 commence a l'index 35, donc le curseur doit se poser a 37 -> "hh|hh".
    TextInputHarness h("wrap_click", "../../assets/ui/test_e2e_textarea.json");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type(kLongLine);
    h.clickAt(kAreaTextOriginX + 16.0, 138.0);
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "aaaa bbbb cccc dddd eeee ffff gggg hhXhh");
}

// ============================================================================
// Le repli se refait quand ce dont il dépend change
// ============================================================================

TEST_CASE("IT_066: effacer du texte defait le repli", "[integration][ui][e2e][textarea][wrap]") {
    // Verrouille l'invalidation du cache de mise en page : si elle ratait une suppression, la zone
    // continuerait d'afficher deux rangees pour un texte qui tient desormais sur une seule.
    TextInputHarness h("wrap_shrink", "../../assets/ui/test_e2e_textarea.json");
    DrawnRows drawn;
    drawn.attach(h);

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type(kLongLine);
    h.pump();
    REQUIRE(drawn.rows().size() == 2u);

    for (int i = 0; i < 10; ++i) h.pressKey(kScanBackspace);   // "hhhh" + l'espace + du "gggg"
    h.pump();

    INFO("texte restant : " << h.lastText);
    REQUIRE(drawn.rows().size() == 1u);
}

TEST_CASE("IT_066: une selection qui traverse un repli surligne CHAQUE rangee",
          "[integration][ui][e2e][textarea][wrap]") {
    // Une sélection multi-rangees doit produire un bandeau par rangee. Un seul rectangle couvrant
    // tout serait faux des que la selection ne commence pas en colonne 0.
    TextInputHarness h("wrap_sel", "../../assets/ui/test_e2e_textarea.json");

    std::map<int, double> selWidths;
    auto onRect = [&](const Message& m) {
        if (m.data->getInt("color", 0) == static_cast<int>(0x4444AAAA)) {
            selWidths[m.data->getInt("renderId", -1)] = m.data->getDouble("scaleX", 0.0);
        }
    };
    h.observer->subscribe("render:sprite:add", onRect);
    h.observer->subscribe("render:sprite:update", onRect);

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type(kLongLine);
    h.pressKey(kScanA, /*shift=*/false, /*ctrl=*/true);   // tout selectionner
    h.pump();

    int painted = 0;
    for (const auto& [id, w] : selWidths) {
        if (w > 0.0) ++painted;
    }
    INFO("bandeaux de selection peints : " << painted);
    REQUIRE(painted == 2);   // une par rangee
}
