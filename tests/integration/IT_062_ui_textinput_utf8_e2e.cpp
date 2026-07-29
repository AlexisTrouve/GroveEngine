/**
 * Integration Test IT_062: éditer du texte ACCENTUÉ dans un UITextInput (E2E, vrai module).
 *
 * QUOI     : taper, effacer et déplacer le curseur sur des caractères UTF-8 multi-octets.
 *
 * POURQUOI : UITextInput compte son curseur en OCTETS —
 *              text.erase(cursorPosition - 1, 1)   (UITextInput.cpp:269)
 *            donc un Backspace sur "é" (0xC3 0xA9, DEUX octets) n'en retire qu'un et laisse une
 *            demi-séquence UTF-8 dans le champ. En français c'est un bug de tous les jours : tout
 *            "é", "à", "ç", "—" est cassé. Ironie : insertFilteredText() avait été écrit exprès pour
 *            l'UTF-8 multi-octets EN ENTRÉE (fix #5/C2) — la suppression le recasse derrière.
 *            Même famille de défaut que les flèches gauche/droite, qui sautent d'un octet.
 *
 * COMMENT  : E2E par le vrai module chargé dynamiquement, piloté par les MÊMES topics que publie le
 *            véritable InputModule (input:keyboard:text pour les caractères, input:keyboard:key
 *            {scancode} pour les touches d'édition) — pas d'appel direct au widget. On observe
 *            ui:text_changed, la seule sortie publique : le curseur n'est pas publié, donc son
 *            exactitude est prouvée INDIRECTEMENT (insérer après un déplacement révèle où il était).
 *
 *            Les littéraux sont écrits en octets explicites ("\xC3\xA9") plutôt qu'en "é" : le test
 *            porte précisément sur des octets, il ne doit pas dépendre de l'encodage du fichier
 *            source ni des options du compilateur.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <memory>
#include <string>

using namespace grove;

namespace {

// "é" = U+00E9 = 0xC3 0xA9 en UTF-8. Deux octets, UN caractère.
const std::string kEAigu = "\xC3\xA9";

// Scancodes SDL bruts, tels que publiés par InputModule (cf. sdlScancodeToEditKey dans UIModule.cpp).
constexpr int kScanBackspace = 42;
constexpr int kScanDelete    = 76;
constexpr int kScanLeft      = 80;
constexpr int kScanRight     = 79;
constexpr int kScanHome      = 74;

// Harnais : charge le vrai UIModule sur la fixture textinput et expose de quoi le piloter.
struct TextInputHarness {
    std::shared_ptr<IntraIO> inputPub;
    std::shared_ptr<IntraIO> uiIO;
    std::shared_ptr<IntraIO> observer;
    ModuleLoader loader;
    std::unique_ptr<IModule> module;

    std::string lastText;
    int textChanges = 0;

    explicit TextInputHarness(const std::string& suffix) {
        auto& mgr = IntraIOManager::getInstance();
        inputPub = mgr.createInstance("input_publisher_" + suffix);
        uiIO     = mgr.createInstance("ui_module_" + suffix);
        observer = mgr.createInstance("test_observer_" + suffix);

        std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
        uiPath = "../modules/libUIModule.dll";
#endif
        module = loader.load(uiPath, "ui_module_" + suffix);
        REQUIRE(module != nullptr);

        JsonDataNode cfg("config");
        cfg.setInt("windowWidth", 800);
        cfg.setInt("windowHeight", 600);
        cfg.setString("layoutFile", "../../assets/ui/test_e2e_textinput.json");
        cfg.setInt("baseLayer", 1000);
        REQUIRE_NOTHROW(module->setConfiguration(cfg, uiIO.get(), nullptr));

        observer->subscribe("ui:text_changed", [this](const Message& m) {
            textChanges++;
            lastText = m.data->getString("text", "");
        });
    }

    ~TextInputHarness() { if (module) module->shutdown(); }

    void pump() {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        module->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    }

    // Le champ est en (100,100) taille 300x40 -> centre (250,120).
    void focusField() {
        auto move = std::make_unique<JsonDataNode>("d");
        move->setDouble("x", 250.0); move->setDouble("y", 120.0);
        inputPub->publish("input:mouse:move", std::move(move));
        pump();
        for (bool pressed : {true, false}) {
            auto d = std::make_unique<JsonDataNode>("d");
            d->setInt("button", 0); d->setBool("pressed", pressed);
            d->setDouble("x", 250.0); d->setDouble("y", 120.0);
            inputPub->publish("input:mouse:button", std::move(d));
            pump();
        }
    }

    void type(const std::string& s) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("text", s);
        inputPub->publish("input:keyboard:text", std::move(d));
        pump();
    }

    void pressKey(int scancode) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("scancode", scancode);
        d->setBool("pressed", true);
        inputPub->publish("input:keyboard:key", std::move(d));
        pump();
    }
};

// Rend une chaîne lisible dans un message d'échec : "aé" -> "a<C3><A9>".
std::string hexdump(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (c >= 32 && c < 127) { out += static_cast<char>(c); }
        else { out += '<'; out += kHex[c >> 4]; out += kHex[c & 0xF]; out += '>'; }
    }
    return out;
}

}  // namespace

TEST_CASE("IT_062: Backspace efface un caractere accentue ENTIER", "[integration][ui][e2e][utf8]") {
    // Le cas nominal du bug. Taper "é" puis Backspace doit vider le champ. Avec un curseur compté en
    // octets, il reste l'octet de tête 0xC3 — une séquence UTF-8 invalide que le rendu affichera en
    // caractère de remplacement, et que le jeu recevra telle quelle dans ui:text_changed.
    TextInputHarness h("utf8_bs");
    h.focusField();

    h.type(kEAigu);
    INFO("apres saisie : " << hexdump(h.lastText));
    REQUIRE(h.lastText == kEAigu);
    REQUIRE(h.lastText.size() == 2u);  // bien DEUX octets pour UN caractere

    h.pressKey(kScanBackspace);
    INFO("apres backspace : " << hexdump(h.lastText));
    REQUIRE(h.lastText.empty());  // <-- ROUGE aujourd'hui : il reste "\xC3"
}

TEST_CASE("IT_062: Backspace au milieu d'un mot accentue ne casse pas les voisins",
          "[integration][ui][e2e][utf8]") {
    // Plus proche d'un vrai usage : effacer la fin d'un mot accentue doit laisser un prefixe VALIDE.
    TextInputHarness h("utf8_word");
    h.focusField();

    h.type("caf" + kEAigu);  // "café"
    REQUIRE(h.lastText == "caf" + kEAigu);

    h.pressKey(kScanBackspace);
    INFO("apres backspace : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "caf");  // <-- ROUGE aujourd'hui : "caf\xC3"
}

TEST_CASE("IT_062: Suppr efface un caractere accentue ENTIER", "[integration][ui][e2e][utf8]") {
    // Le pendant avant-curseur : deleteCharAfter() a exactement le meme defaut d'un octet.
    TextInputHarness h("utf8_del");
    h.focusField();

    h.type(kEAigu + std::string("a"));  // "éa", curseur en fin
    REQUIRE(h.lastText == kEAigu + std::string("a"));

    h.pressKey(kScanLeft);   // avant 'a'
    h.pressKey(kScanLeft);   // avant 'é'  (doit enjamber DEUX octets)
    h.pressKey(kScanDelete); // supprime 'é' en entier

    INFO("apres suppr : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "a");
}

TEST_CASE("IT_062: la fleche gauche enjambe un caractere accentue ENTIER",
          "[integration][ui][e2e][utf8]") {
    // Le curseur n'est pas publie : on prouve sa position en INSERANT apres l'avoir deplace.
    // Un pas d'un octet placerait le 'X' ENTRE les deux octets de 'é' et produirait une chaine
    // corrompue que ce REQUIRE attrape.
    TextInputHarness h("utf8_left");
    h.focusField();

    h.type("a" + kEAigu);   // "aé", curseur en fin
    h.pressKey(kScanLeft);  // doit se poser AVANT 'é', pas entre ses deux octets
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "aX" + kEAigu);
}

TEST_CASE("IT_062: la fleche droite enjambe un caractere accentue ENTIER",
          "[integration][ui][e2e][utf8]") {
    // NB : on part de Home et NON de deux Left. Avec "Left, Left, Right" les erreurs d'un octet se
    // COMPENSENT (on retombe sur la bonne position par accident) et le test ne morde pas. Depuis une
    // origine franche, un seul Right doit enjamber les DEUX octets du 'é' d'un coup.
    TextInputHarness h("utf8_right");
    h.focusField();

    h.type(kEAigu + std::string("b"));  // "éb", curseur en fin
    h.pressKey(kScanHome);              // debut de chaine, position 0
    h.pressKey(kScanRight);             // doit se poser APRES 'é' (octet 2), pas entre ses octets
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == kEAigu + std::string("Xb"));
}

TEST_CASE("IT_062: l'ASCII pur reste inchange (non-regression)", "[integration][ui][e2e][utf8]") {
    // Garde-fou : le correctif UTF-8 ne doit RIEN changer au comportement mono-octet existant,
    // deja verrouille par IT_016. Si ce cas casse, le correctif est allé trop loin.
    TextInputHarness h("utf8_ascii");
    h.focusField();

    h.type("AB");
    REQUIRE(h.lastText == "AB");
    h.pressKey(kScanBackspace);
    REQUIRE(h.lastText == "A");
    h.pressKey(kScanLeft);
    h.type("Z");
    REQUIRE(h.lastText == "ZA");
}
