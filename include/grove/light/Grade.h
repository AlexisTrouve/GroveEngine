#pragma once

// ============================================================================
// Grade.h — la colorimétrie (grove::light), header-only.
//
// QUOI  : teinte → contraste → saturation, appliqués à une couleur DÉJÀ tonemappée, donc dans un espace
//         d'affichage borné à [0,1]. Ni renderer, ni IIO, ni GPU — même contrat que Light.h/Bloom.h.
//
// POURQUOI un oracle ici alors que le FONDU n'en a pas eu (lighting-fade.md §4) : parce qu'il y a de
//         quoi se tromper. Un fondu est un `lerp`, il n'a aucune décision. Ici il y en a quatre — la
//         luminance à réutiliser, le pivot du contraste, l'ordre non commutatif, et la propriété
//         « saturation 0 rend un gris de la BONNE luminance ». Le motif s'applique quand il y a une
//         décision à verrouiller, pas par habitude.
//
// Plan : docs/design/lighting-grade.md
// ============================================================================

#include "Bloom.h"   // grove::light::luminance — UNE seule définition de la luminance dans ce moteur

namespace grove {
namespace light {

// Les trois réglages. Les valeurs par défaut sont les NEUTRES, et leur combinaison est une identité
// exacte — c'est le contournement à coût nul jusque dans l'oracle.
struct GradeParams {
    float saturation = 1.0f;                    // 0 = noir et blanc, 1 = neutre, >1 = criard
    float contrast   = 1.0f;                    // <1 rapproche du gris moyen, >1 en écarte
    float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;   // multiplication par canal ; blanc = neutre
};

// Le gris moyen autour duquel le contraste pivote.
//
// ⚠️ 0,5 et PAS 0,18. On opère APRÈS le tonemapping, donc dans un espace d'affichage borné à [0,1] où
//    le gris moyen est à 0,5 — pas dans le linéaire de scène, où la référence serait 18 %. Un pivot à
//    0,18 assombrirait toute image dont on relève le contraste, ce qui se lirait comme « le contraste
//    assombrit » : un défaut incompréhensible sans cette ligne.
inline float gradeContrastPivot() { return 0.5f; }

// Les réglages sont-ils tous neutres ? C'est cette réponse qui décide si la passe de présentation doit
// exister.
//
// ⚠️ Comparaison EXACTE, sans epsilon : les valeurs viennent de défauts JSON, donc elles valent
//    exactement 1.0f quand elles sont absentes, et un jeu qui publie délibérément un neutre demande
//    « pas d'étalonnage » — ce qui doit rester gratuit. Un faux positif rendrait la colorimétrie inerte
//    (le chaînon jamais câblé) ; un faux négatif ferait payer une passe plein écran pour rien.
inline bool gradeIsNeutral(const GradeParams& p) {
    return p.saturation == 1.0f && p.contrast == 1.0f
        && p.tintR == 1.0f && p.tintG == 1.0f && p.tintB == 1.0f;
}

// Applique l'étalonnage. `out` reçoit 3 flottants dans [0,1].
//
// COMMENT — l'ordre est celui d'un étalonnage réel, et il n'est PAS commutatif :
//   1. **teinte** (balance des blancs) — multiplication par canal ;
//   2. **contraste** — écarte du gris moyen ;
//   3. **saturation** — interpole vers la luminance.
//
// Teinter APRÈS avoir désaturé donnerait un virage monochrome (un sépia) au lieu d'une image équilibrée
// puis désaturée. Les deux sont des effets légitimes, mais un seul est ce qu'on attend de trois boutons
// nommés ainsi.
//
// ⚠️ La borne finale est appliquée UNE SEULE FOIS, à la fin. Écrêter entre chaque étape détruirait un
//    intermédiaire hors plage que la suite ramène dedans (une teinte forte suivie d'un contraste faible),
//    et le résultat sortirait plus sombre que demandé.
inline void gradeColor(float r, float g, float b, const GradeParams& p, float* out) {
    // 1. Teinte.
    float cr = r * p.tintR;
    float cg = g * p.tintG;
    float cb = b * p.tintB;

    // 2. Contraste, autour du gris moyen.
    const float pivot = gradeContrastPivot();
    cr = (cr - pivot) * p.contrast + pivot;
    cg = (cg - pivot) * p.contrast + pivot;
    cb = (cb - pivot) * p.contrast + pivot;

    // 3. Saturation : interpolation vers la LUMINANCE, celle du bloom et pas une nouvelle.
    //    Une moyenne (r+g+b)/3 rendrait un rouge pur et un bleu pur au même gris, alors que l'œil les
    //    voit à des clartés très différentes (0,2126 contre 0,0722).
    const float luma = luminance(cr, cg, cb);
    cr = luma + (cr - luma) * p.saturation;
    cg = luma + (cg - luma) * p.saturation;
    cb = luma + (cb - luma) * p.saturation;

    // Borne finale, une seule fois : le contraste peut sortir de la plage — (0 − 0,5)·2 + 0,5 = −0,5 —
    // et un canal négatif donnerait un comportement dépendant du backend.
    out[0] = (cr < 0.0f) ? 0.0f : ((cr > 1.0f) ? 1.0f : cr);
    out[1] = (cg < 0.0f) ? 0.0f : ((cg > 1.0f) ? 1.0f : cg);
    out[2] = (cb < 0.0f) ? 0.0f : ((cb > 1.0f) ? 1.0f : cb);
}

} // namespace light
} // namespace grove
