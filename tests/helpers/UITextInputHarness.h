#pragma once

// ============================================================================
// Harnais E2E pour UITextInput — charge le VRAI module et le pilote par les VRAIS topics.
//
// QUOI : monte un UIModule sur la fixture `assets/ui/test_e2e_textinput.json` et expose de quoi
//        cliquer, taper et appuyer sur des touches comme le ferait un utilisateur.
//
// POURQUOI header partagé : deux suites l'utilisent (IT_062 UTF-8, IT_063 sélection) et d'autres
//        suivront (presse-papiers, multiligne). Dupliquer le harnais ferait diverger la façon dont
//        chaque test simule l'entrée — et le jour où le pipeline d'entrée change, on corrigerait
//        les tests un par un au lieu d'un seul endroit.
//
// COMMENT : on publie exactement ce que publie le véritable InputModule — `input:mouse:move`,
//        `input:mouse:button`, `input:keyboard:text` pour les caractères, `input:keyboard:key`
//        {scancode, shift, ctrl, alt} pour les touches d'édition. Aucun appel direct au widget :
//        c'est ce qui fait de ces tests une preuve de bout en bout et non une inspection de code.
//        La sortie observée est `ui:text_changed`, la seule publique — la position du curseur et la
//        sélection ne sont pas publiées, donc on les prouve INDIRECTEMENT (taper après un
//        déplacement révèle où était le curseur ; taper sur une sélection révèle son étendue).
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <memory>
#include <string>

namespace grove {
namespace uitest {

// Scancodes SDL bruts, tels que publiés par InputModule (cf. sdlScancodeToEditKey dans UIModule.cpp).
constexpr int kScanBackspace = 42;
constexpr int kScanDelete    = 76;
constexpr int kScanReturn    = 40;
constexpr int kScanLeft      = 80;
constexpr int kScanRight     = 79;
constexpr int kScanHome      = 74;
constexpr int kScanEnd       = 77;
constexpr int kScanA         = 4;   // SDL_SCANCODE_A
constexpr int kScanC         = 6;   // SDL_SCANCODE_C
constexpr int kScanV         = 25;  // SDL_SCANCODE_V
constexpr int kScanX         = 27;  // SDL_SCANCODE_X
constexpr int kScanUp        = 82;  // SDL_SCANCODE_UP
constexpr int kScanDown      = 81;  // SDL_SCANCODE_DOWN

// "é" = U+00E9 = 0xC3 0xA9 en UTF-8. Deux octets, UN caractère. Écrit en octets explicites pour ne
// dépendre ni de l'encodage du fichier source ni des options du compilateur.
inline const std::string kEAigu = "\xC3\xA9";

// La fixture place le champ en (100,100) taille 300x40, fontSize 18.
constexpr double kFieldX = 100.0, kFieldY = 100.0;
constexpr double kFieldCenterX = 250.0, kFieldCenterY = 120.0;
constexpr double kTextOriginX = 108.0;  // kFieldX + PADDING(8)

// Variante multiligne : meme harnais, autre fixture. La zone de texte est en (100,100), 300x120,
// fontSize 16 / lineHeight 20 -> le texte commence a (108, 108) et chaque ligne fait 20px.
constexpr double kAreaX = 100.0, kAreaY = 100.0;
constexpr double kAreaTextOriginX = 108.0, kAreaTextOriginY = 108.0;
constexpr double kAreaLineHeight = 20.0;

struct TextInputHarness {
    std::shared_ptr<IntraIO> inputPub;
    std::shared_ptr<IntraIO> uiIO;
    std::shared_ptr<IntraIO> observer;
    ModuleLoader loader;
    std::unique_ptr<IModule> module;

    std::string lastText;
    int textChanges = 0;

    // `layout` permet de monter une autre fixture (la zone de texte multiligne) avec le meme harnais.
    explicit TextInputHarness(const std::string& suffix,
                              const std::string& layout = "../../assets/ui/test_e2e_textinput.json") {
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
        cfg.setString("layoutFile", layout);
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

    void moveMouse(double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
        pump();
    }

    void mouseButton(bool pressed, double x, double y, int button = 0) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", button); d->setBool("pressed", pressed);
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:button", std::move(d));
        pump();
    }

    // Clic complet (press + release) à une position donnée.
    void clickAt(double x, double y) {
        moveMouse(x, y);
        mouseButton(true, x, y);
        mouseButton(false, x, y);
    }

    void focusField() { clickAt(kFieldCenterX, kFieldCenterY); }

    void type(const std::string& s) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("text", s);
        inputPub->publish("input:keyboard:text", std::move(d));
        pump();
    }

    // Touche d'édition, avec ses modificateurs — InputModule les publie DÉJÀ (InputConverter.cpp),
    // c'est l'UIModule qui les laissait tomber.
    void pressKey(int scancode, bool shift = false, bool ctrl = false) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("scancode", scancode);
        d->setBool("pressed", true);
        d->setBool("shift", shift);
        d->setBool("ctrl", ctrl);
        d->setBool("alt", false);
        inputPub->publish("input:keyboard:key", std::move(d));
        pump();
    }
};

// Rend une chaîne lisible dans un message d'échec : "aé" -> "a<C3><A9>".
inline std::string hexdump(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (c >= 32 && c < 127) { out += static_cast<char>(c); }
        else { out += '<'; out += kHex[c >> 4]; out += kHex[c & 0xF]; out += '>'; }
    }
    return out;
}

}  // namespace uitest
}  // namespace grove
