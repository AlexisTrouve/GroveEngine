#pragma once

#include <SDL.h>

namespace grove {
namespace test {

/**
 * @brief Apply the default GroveEngine window icon (cross-platform).
 *
 * QUOI : charge assets/icon.bmp et le pose comme icône de la fenêtre SDL.
 *
 * POURQUOI : on veut l'icône GroveEngine par défaut sur les fenêtres, en FALLBACK —
 * si l'app n'a pas déjà posé sa propre icône, elle hérite de celle de l'engine. On
 * passe par SDL (SDL_LoadBMP + SDL_SetWindowIcon), qui est PORTABLE (Windows ET Linux,
 * X11/Wayland) — pas de chemin Win32-only à dupliquer pour Linux plus tard. BMP plutôt
 * que PNG pour rester sur du SDL core (pas de dépendance SDL_image/stb au runtime).
 *
 * COMMENT : on essaie quelques chemins candidats (le cwd des visual tests varie :
 * racine projet, build/, build/tests/). Échec gracieux : si aucun ne charge, on ne
 * touche rien et la fenêtre garde l'icône SDL par défaut (jamais de crash). Une app
 * qui VEUT sa propre icône appelle simplement SDL_SetWindowIcon elle-même au lieu de
 * (ou après) ceci.
 *
 * @return true si l'icône GroveEngine a été appliquée, false sinon (icône SDL gardée).
 */
inline bool setWindowIconGrove(SDL_Window* window) {
    if (!window) return false;

    static const char* kCandidates[] = {
        "assets/icon.bmp",        // run from project root (recommended for visual tests)
        "../assets/icon.bmp",     // run from build/
        "../../assets/icon.bmp",  // run from build/tests/
    };

    for (const char* path : kCandidates) {
        SDL_Surface* icon = SDL_LoadBMP(path);
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_FreeSurface(icon);
            return true;
        }
    }
    return false;  // no icon found -> keep SDL's default, no error
}

} // namespace test
} // namespace grove
