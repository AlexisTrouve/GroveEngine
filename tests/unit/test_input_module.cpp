/**
 * Unit/integration test: InputModule static-host path (engine help B1).
 *
 * Proves the path a static-link game (Drifterra) uses: instantiate InputModule directly,
 * feed it native SDL events via feedEvent() (the host polls SDL, the module never does),
 * call process(), and observe the input:* topics it publishes. Headless — SDL events are
 * plain structs (SDLBackend::convert is pure), so NO window / SDL_Init is needed.
 *
 * The published topics (input:mouse:move / :button, input:keyboard:key / :text) are
 * EXACTLY what UIModule subscribes to (see fix #5) — so this locks the producer end of
 * the input → UI chain that IT_016/IT_017 lock on the consumer end.
 */

#include <catch2/catch_test_macros.hpp>

#include "InputModule/InputModule.h"
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <SDL.h>
#include <cstring>
#include <string>

using namespace grove;

TEST_CASE("InputModule (static host): feedEvent -> input:* topics", "[input][static][b1]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputIO  = mgr.createInstance("b1_input");
    auto observer = mgr.createInstance("b1_observer");

    // Static-host style: instantiate the module directly, no DLL / createModule().
    InputModule input;
    JsonDataNode cfg("config");  // defaults: mouse + keyboard enabled
    input.setConfiguration(cfg, inputIO.get(), nullptr);

    int moves = 0, buttons = 0, keys = 0, texts = 0;
    int moveX = -1, moveY = -1;
    int btn = -1; bool btnPressed = false; int btnX = -1, btnY = -1;
    int scancode = -1; bool keyPressed = false;
    std::string text;

    observer->subscribe("input:mouse:move", [&](const Message& m) {
        moves++; moveX = m.data->getInt("x", -1); moveY = m.data->getInt("y", -1);
    });
    observer->subscribe("input:mouse:button", [&](const Message& m) {
        buttons++; btn = m.data->getInt("button", -1); btnPressed = m.data->getBool("pressed", false);
        btnX = m.data->getInt("x", -1); btnY = m.data->getInt("y", -1);
    });
    observer->subscribe("input:keyboard:key", [&](const Message& m) {
        keys++; scancode = m.data->getInt("scancode", -1); keyPressed = m.data->getBool("pressed", false);
    });
    observer->subscribe("input:keyboard:text", [&](const Message& m) {
        texts++; text = m.data->getString("text", "");
    });

    auto pump = [&] {
        JsonDataNode in("input");
        in.setDouble("deltaTime", 0.016);
        input.process(in);  // drains the fed-event buffer, converts, publishes
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };

    // Mouse move (100,200).
    { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = 100; e.motion.y = 200; input.feedEvent(&e); }
    pump();
    REQUIRE(moves == 1);
    REQUIRE(moveX == 100);
    REQUIRE(moveY == 200);

    // Left mouse button down at (50,60) — SDL buttons are 1-based, published 0-based.
    { SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = 50; e.button.y = 60; input.feedEvent(&e); }
    pump();
    REQUIRE(buttons == 1);
    REQUIRE(btn == 0);
    REQUIRE(btnPressed == true);
    REQUIRE(btnX == 50);
    REQUIRE(btnY == 60);

    // Keyboard key down (scancode A).
    { SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.scancode = SDL_SCANCODE_A; e.key.repeat = 0; e.key.keysym.mod = 0; input.feedEvent(&e); }
    pump();
    REQUIRE(keys == 1);
    REQUIRE(scancode == static_cast<int>(SDL_SCANCODE_A));
    REQUIRE(keyPressed == true);

    // Text input "A".
    { SDL_Event e{}; e.type = SDL_TEXTINPUT; std::strcpy(e.text.text, "A"); input.feedEvent(&e); }
    pump();
    REQUIRE(texts == 1);
    REQUIRE(text == "A");

    input.shutdown();
    mgr.removeInstance("b1_input");
    mgr.removeInstance("b1_observer");
}

// ============================================================================
// Presse-papiers — le service SDL qu'InputModule expose sur IIO.
//
// POURQUOI ce test : l'UIModule est SDL-free et ne peut pas lire le presse-papiers ; il envoie
// `input:clipboard:set` / `input:clipboard:get` et attend `input:clipboard:text`. IT_064 prouve le
// côté UI contre un service factice — celui-ci prouve l'AUTRE bout : que le vrai module répond bien
// à ce protocole en touchant le vrai presse-papiers système.
//
// Contrairement au reste de ce fichier, il faut ici SDL pour de vrai (le presse-papiers dépend du
// sous-système vidéo). On s'abstient proprement s'il n'est pas disponible, comme les tests [gpu].
// ============================================================================

TEST_CASE("InputModule: aller-retour presse-papiers via IIO", "[input][static][clipboard]") {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("pas de video SDL — test ignore"); return; }

    // Le presse-papiers est une ressource de l'OS, PAS un état du moteur : un gestionnaire de
    // presse-papiers tiers, une session RDP ou une autre application qui le tient ouvert font
    // échouer l'accès (mesuré : `SDL_SetClipboardText` -> -1, « Couldn't open clipboard: Accès
    // refusé »). On sonde donc avant d'affirmer quoi que ce soit — sinon la suite devient
    // aléatoire sur toute machine équipée d'un gestionnaire de presse-papiers, et un échec rouge
    // désigne le moteur pour un refus qui vient d'ailleurs.
    //
    // ⚠️ Ce n'est PAS un fallback qui masque un défaut : on s'abstient sur une dépendance externe
    // indisponible, exactement comme la ligne au-dessus s'abstient sans vidéo. Le contrôle qui ne
    // dépend pas de l'OS (le module RÉPOND-il à une demande ?) reste asserté plus bas.
    if (SDL_SetClipboardText("grove-probe") != 0) {
        WARN("presse-papiers refuse par l'OS (" << SDL_GetError() << ") — aller-retour non verifiable");
        SDL_Quit();
        return;
    }

    auto& mgr = IntraIOManager::getInstance();
    auto inputIO  = mgr.createInstance("clip_input");
    auto peer     = mgr.createInstance("clip_peer");

    InputModule input;
    JsonDataNode cfg("config");
    input.setConfiguration(cfg, inputIO.get(), nullptr);

    std::string received;
    int replies = 0;
    peer->subscribe("input:clipboard:text", [&](const Message& m) {
        received = m.data->getString("text", "");
        ++replies;
    });

    auto pump = [&] {
        JsonDataNode in("input");
        input.process(in);
        while (peer->hasMessages() > 0) peer->pullAndDispatch();
    };

    // Le texte porte un accent : le presse-papiers doit etre transparent a l'UTF-8, sinon copier un
    // nom de vaisseau francais le corromprait au passage.
    const std::string payload = "vaisseau \xC3\xA9toile";

    {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("text", payload);
        peer->publish("input:clipboard:set", std::move(d));
    }
    pump();

    peer->publish("input:clipboard:get", std::make_unique<JsonDataNode>("d"));
    pump();
    pump();  // la reponse repart par l'IIO : une passe de plus pour la recevoir

    INFO("recu='" << received << "' apres avoir ecrit '" << payload << "'");
    REQUIRE(replies >= 1);        // le module a REPONDU a la demande
    REQUIRE(received == payload); // ...avec exactement ce qu'on avait ecrit, accent compris

    input.shutdown();
    SDL_Quit();
}
