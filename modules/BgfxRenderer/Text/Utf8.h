#pragma once

#include <cstdint>

namespace grove {

// QUOI : décode UN codepoint UTF-8 à partir de *p et avance p des octets consommés.
// POURQUOI : le rendu texte itérait octet par octet, donc un caractère multi-octets
//   ("é" = 0xC3 0xA9, accents FR, i18n) était lu comme 2 glyphes faux/larges. Tout le
//   texte affiché (labels, HUD) passe par ici → un décodeur unique = source de vérité.
// COMMENT : 1 octet ASCII, sinon 2/3/4 octets selon les bits de tête ; une séquence
//   malformée dégrade vers l'octet de tête brut (repli Latin-1) — rien n'est perdu et p
//   avance toujours d'au moins 1 (pas de boucle infinie côté appelant).
// L'appelant garantit *p != '\0' avant d'appeler (boucle `while (*p)`).
inline uint32_t decodeUtf8(const char*& p) {
    const unsigned char b0 = static_cast<unsigned char>(*p);
    if (b0 < 0x80) { ++p; return b0; }                       // ASCII

    int extra;
    uint32_t cp;
    if      ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1Fu; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0Fu; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07u; }
    else { ++p; return b0; }                                  // octet de continuation isolé / invalide

    ++p;  // consommé l'octet de tête
    for (int i = 0; i < extra; ++i) {
        const unsigned char bc = static_cast<unsigned char>(*p);
        if ((bc & 0xC0) != 0x80) { return cp; }               // tronqué / invalide → ce qu'on a (p ne consomme pas bc)
        cp = (cp << 6) | (bc & 0x3Fu);
        ++p;
    }
    return cp;
}

} // namespace grove
