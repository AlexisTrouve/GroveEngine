#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>

namespace grove::assets {

/**
 * @brief One rectangle to pack into an atlas. w,h are inputs; x,y are filled in by shelfPack().
 */
struct PackRect { int w = 0, h = 0, x = 0, y = 0; };

struct PackResult { int width = 0, height = 0; bool ok = false; };

/**
 * @brief Shelf bin-packing — place rects into an atlas of fixed max width; height grows in horizontal rows.
 *
 * QUOI : range les rects en "étagères" horizontales (largeur max fixe, hauteur qui croît), du plus HAUT au
 *   plus bas (heuristique shelf classique) ; remplit x/y de chaque rect en place et renvoie la taille atlas
 *   utilisée. Un `gutter` de padding transparent entoure chaque rect (évite le bleeding sous filtrage).
 * POURQUOI : c'est le cœur du packing runtime (phase 2b) — assembler N petites images en une sheet. Logique
 *   PURE (std seul, pas d'image/GPU) -> testable headless ; le decode + l'upload sont au-dessus (AtlasPacker).
 * COMMENT : on trie des indices par hauteur décroissante ; curseur (x,y) qui avance de w+gutter ; quand la
 *   ligne déborde maxWidth on descend d'une étagère (shelfH+gutter). ok=false si un rect seul est trop large.
 */
inline PackResult shelfPack(std::vector<PackRect>& rects, int maxWidth, int gutter = 1) {
    if (maxWidth <= 0) return { 0, 0, false };

    std::vector<int> order(rects.size());
    for (int i = 0; i < static_cast<int>(rects.size()); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b){ return rects[a].h > rects[b].h; });

    int cursorX = gutter, cursorY = gutter, shelfH = 0, usedW = 0;
    for (int idx : order) {
        PackRect& r = rects[idx];
        if (r.w + 2 * gutter > maxWidth) return { 0, 0, false };   // can't fit even on its own shelf
        if (cursorX + r.w + gutter > maxWidth) {                   // start a new shelf
            cursorY += shelfH + gutter;
            cursorX = gutter;
            shelfH = 0;
        }
        r.x = cursorX;
        r.y = cursorY;
        cursorX += r.w + gutter;
        shelfH = std::max(shelfH, r.h);
        usedW = std::max(usedW, cursorX);
    }
    return { usedW, cursorY + shelfH + gutter, true };
}

} // namespace grove::assets
