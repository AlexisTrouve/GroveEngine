#pragma once

// ============================================================================
// grove::ui::NineSlice — pure 9-slice (nine-patch) geometry.
//
// QUOI  : à partir d'un rectangle CIBLE (dx,dy,dw,dh en px écran) + la description d'une texture de bord
//         (dims natives srcW×srcH + épaisseurs des marges left/right/top/bottom en px source), calcule les
//         jusqu'à 9 quads (4 coins figés, 4 arêtes étirées sur un axe, 1 centre étiré sur deux) qui habillent
//         la cible avec un BORD CONTINU quelle que soit sa taille. Chaque quad porte son rect de destination
//         ET son sous-rectangle UV dans [0,1] de la texture source.
//
// POURQUOI : c'est la brique réutilisable qui rend "un asset de cadre -> un bouton/fenêtre de taille
//         arbitraire, coins nets, bords non déformés". Elle est PURE (aucun couplage renderer/IIO/GPU),
//         header-only, std-only — même contrat que grove::anim / grove::camera : testable headless et
//         utilisable par TOUT host (l'engine renderer l'utilise pour expandre render:nineslice, mais un jeu
//         peut aussi l'appeler directement). Séparer les maths du dessin = le prime "Modularité".
//
// COMMENT : les coins gardent leur taille source native (bord net, non pixel-stretché) ; les arêtes/centre
//         s'étirent parce que leur rect de destination diffère de leur région source alors que l'UV couvre
//         cette région (échantillonnage linéaire -> étirement propre). Si la cible est plus petite que
//         left+right (resp. top+bottom), les colonnes (resp. lignes) de bord sont RÉTRÉCIES proportionnellement
//         pour que les coins ne se chevauchent jamais, et la bande centrale disparaît (largeur 0 -> quad omis).
// ============================================================================

namespace grove {
namespace ui {

// Un quad résolu : destination (coin haut-gauche + taille, px écran) + UV source dans [0,1].
struct NinePatchQuad {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   // destination (top-left corner + size)
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;  // source UV
};

// Description de la texture de bord : ses dimensions natives + l'épaisseur des 4 marges (en px source).
// Les marges sont exprimées en pixels de la SOURCE (pas de la cible) — c'est ce qui fige la taille des coins.
struct NinePatch {
    float srcW = 0.0f, srcH = 0.0f;                          // dims natives de la texture source (px)
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;  // marges (px source)
};

// Calcule les quads du nine-patch pour la cible (dx,dy,dw,dh). Écrit jusqu'à 9 quads dans out[9] en
// ordre row-major (TL,TC,TR, ML,MC,MR, BL,BC,BR) et retourne le NOMBRE de quads NON dégénérés écrits
// (un quad de largeur ou hauteur <= 0 est omis, jamais écrit). Retourne 0 si la source ou la cible est
// dégénérée (srcW/srcH <= 0 -> UV indéfinis ; dw/dh <= 0 -> rien à couvrir).
inline int computeNineSlice(const NinePatch& np, float dx, float dy, float dw, float dh,
                            NinePatchQuad out[9]) {
    // Gardes de domaine : sans dims source on ne peut pas mapper d'UV ; sans surface cible rien à dessiner.
    if (np.srcW <= 0.0f || np.srcH <= 0.0f || dw <= 0.0f || dh <= 0.0f) return 0;

    // 1. Marges assainies : jamais négatives ; jamais > la dimension source (sinon les bandes UV se
    //    croiseraient). Si left+right dépasse srcW on les rabat proportionnellement pour qu'elles tiennent.
    float l = np.left  > 0.0f ? np.left  : 0.0f;
    float r = np.right > 0.0f ? np.right : 0.0f;
    float t = np.top   > 0.0f ? np.top   : 0.0f;
    float b = np.bottom> 0.0f ? np.bottom: 0.0f;
    if (l + r > np.srcW && (l + r) > 0.0f) { const float s = np.srcW / (l + r); l *= s; r *= s; }
    if (t + b > np.srcH && (t + b) > 0.0f) { const float s = np.srcH / (t + b); t *= s; b *= s; }

    // 2. Largeurs de colonnes DESTINATION : coins à taille source native tant qu'ils tiennent ; sinon
    //    (cible plus étroite que les deux coins) on rétrécit les coins pour qu'ils remplissent la cible
    //    sans se chevaucher, et la colonne centrale disparaît. Idem pour les lignes.
    float colL, colR, colC;
    if (l + r <= dw) { colL = l; colR = r; colC = dw - l - r; }
    else if (l + r > 0.0f) { const float s = dw / (l + r); colL = l * s; colR = r * s; colC = 0.0f; }
    else { colL = 0.0f; colR = 0.0f; colC = dw; }   // pas de marges horizontales -> tout est centre

    float rowT, rowB, rowC;
    if (t + b <= dh) { rowT = t; rowB = b; rowC = dh - t - b; }
    else if (t + b > 0.0f) { const float s = dh / (t + b); rowT = t * s; rowB = b * s; rowC = 0.0f; }
    else { rowT = 0.0f; rowB = 0.0f; rowC = dh; }

    // 3. Frontières UV source (fractions [0,1]) : 0 | left | srcW-right | 1 horizontalement, idem vertical.
    const float ux0 = 0.0f, ux1 = l / np.srcW, ux2 = (np.srcW - r) / np.srcW, ux3 = 1.0f;
    const float uy0 = 0.0f, uy1 = t / np.srcH, uy2 = (np.srcH - b) / np.srcH, uy3 = 1.0f;

    // 4. Positions DESTINATION des 3 colonnes / 3 lignes. La colonne droite est ancrée sur le bord droit
    //    de la cible (dx+dw-colR) pour rester exacte même après un rétrécissement des coins.
    const float x0 = dx, x1 = dx + colL, x2 = dx + dw - colR;
    const float y0 = dy, y1 = dy + rowT, y2 = dy + dh - rowB;

    // Table des 3 colonnes (x, largeur, u0, u1) et 3 lignes (y, hauteur, v0, v1).
    struct Span { float pos, size, s0, s1; };
    const Span cols[3] = { {x0, colL, ux0, ux1}, {x1, colC, ux1, ux2}, {x2, colR, ux2, ux3} };
    const Span rows[3] = { {y0, rowT, uy0, uy1}, {y1, rowC, uy1, uy2}, {y2, rowB, uy2, uy3} };

    // 5. Émission row-major : un quad par (ligne × colonne) dont les deux tailles sont > 0. Les quads
    //    dégénérés (bande de bord d'épaisseur 0, ou centre écrasé) sont simplement omis.
    int n = 0;
    for (int ri = 0; ri < 3; ++ri) {
        if (rows[ri].size <= 0.0f) continue;
        for (int ci = 0; ci < 3; ++ci) {
            if (cols[ci].size <= 0.0f) continue;
            NinePatchQuad& q = out[n++];
            q.x = cols[ci].pos; q.y = rows[ri].pos; q.w = cols[ci].size; q.h = rows[ri].size;
            q.u0 = cols[ci].s0; q.u1 = cols[ci].s1; q.v0 = rows[ri].s0; q.v1 = rows[ri].s1;
        }
    }
    return n;
}

} // namespace ui
} // namespace grove
