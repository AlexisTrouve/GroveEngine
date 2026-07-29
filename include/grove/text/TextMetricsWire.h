#pragma once

// ============================================================================
// Transport d'une table de métriques de police sur IIO — encodage DENSE, pur, header-only.
//
// QUOI : sérialise/désérialise un grove::text::Metrics en une seule chaîne compacte, pour que le
//        renderer (qui POSSÈDE la police) pousse ses avances de glyphes vers les consommateurs qui
//        doivent mesurer du texte sans dépendre de lui (l'UIModule, un jeu).
//
// POURQUOI un encodage à la main plutôt qu'un objet JSON imbriqué : IIO ne transporte QUE le JSON
//        propre du nœud (`IntraIO::publish` copie `getJsonData()`), pas les enfants assemblés par
//        `setChild()` — piège documenté et déjà payé au prix d'un rouge sur IT_033. Une chaîne
//        scalaire est immunisée contre ce piège : elle voyage toujours, quel que soit le chemin.
//
// COMMENT : la table est DENSE sur une plage contiguë de codepoints (une police cuit ASCII +
//        Latin-1, donc une plage suffit) — on transmet le premier codepoint puis les avances dans
//        l'ordre, séparées par des espaces. Pas de clés répétées, pas d'échappement, un seul
//        `strtof` par entrée au décodage.
//
//        L'encodage est symétrique et testé en aller-retour : c'est ce qui garantit que ce que
//        l'UIModule mesure est bien ce que le renderer dessine — le contraire ferait diverger le
//        curseur du texte, exactement le défaut qu'on répare.
// ============================================================================

#include <grove/text/TextMetrics.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace grove {
namespace text {

// Charge utile prête à publier. Les scalaires partent en champs JSON normaux, la table en UNE chaîne.
struct MetricsWire {
    float baseSize = 8.0f;
    float lineHeight = 8.0f;
    uint32_t firstCodepoint = 32;
    std::string advances;  // avances séparées par des espaces, à partir de firstCodepoint
};

// ----------------------------------------------------------------------------
// Encodage : plage dense [firstCodepoint, firstCodepoint + advances.size()).
// Un codepoint sans glyphe dans la police y figure avec l'avance de repli — la plage reste dense,
// ce qui est tout l'intérêt (indexation directe, pas de clés).
// ----------------------------------------------------------------------------
inline MetricsWire encodeDense(const Metrics& m, uint32_t firstCodepoint, uint32_t lastCodepoint) {
    MetricsWire w;
    w.baseSize = m.baseSize;
    w.lineHeight = m.lineHeight;
    w.firstCodepoint = firstCodepoint;

    w.advances.reserve(static_cast<size_t>(lastCodepoint - firstCodepoint + 1) * 6);
    for (uint32_t cp = firstCodepoint; cp <= lastCodepoint; ++cp) {
        if (cp != firstCodepoint) w.advances += ' ';
        // advanceOf(cp, baseSize) rend l'avance À LA TAILLE DE BASE, donc l'échelle est neutre :
        // on transmet exactement ce que la table contient.
        w.advances += std::to_string(m.advanceOf(cp, m.baseSize));
    }
    return w;
}

// ----------------------------------------------------------------------------
// Décodage. Une charge utile vide/illisible rend une table VIDE — donc le consommateur retombe sur
// son repli monospace au lieu de mesurer avec des valeurs fantaisistes. Échec franc, pas silencieux.
// ----------------------------------------------------------------------------
inline Metrics decodeDense(const MetricsWire& w) {
    Metrics m;
    if (w.baseSize > 0.0f) m.baseSize = w.baseSize;
    if (w.lineHeight > 0.0f) m.lineHeight = w.lineHeight;

    const char* p = w.advances.c_str();
    uint32_t cp = w.firstCodepoint;
    while (*p != '\0') {
        char* end = nullptr;
        const float adv = std::strtof(p, &end);
        if (end == p) break;  // rien de lisible : on s'arrête sur ce qu'on a
        m.advances[cp++] = adv;
        p = end;
        while (*p == ' ') ++p;
    }

    // L'avance de repli suit la police plutôt que de rester au 8 monospace : un codepoint hors plage
    // (CJK, emoji) obtient ainsi une largeur plausible pour CETTE police. On prend celle de l'espace
    // si elle existe, sinon on garde le défaut.
    const auto space = m.advances.find(static_cast<uint32_t>(' '));
    if (space != m.advances.end() && space->second > 0.0f) m.fallbackAdvance = space->second;

    return m;
}

}  // namespace text
}  // namespace grove
