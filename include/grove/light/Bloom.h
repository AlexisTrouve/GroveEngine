#pragma once

// ============================================================================
// Bloom.h — the bright-pass curve and the blur kernel (grove::light), header-only.
//
// QUOI  : how much of a pixel blooms (a FRACTION of its colour), and the weights of the separable
//         Gaussian that spreads it. No renderer, no IIO, no GPU — same contract as Light.h.
//
// POURQUOI ici et pas en dur dans le shader :
//         - la courbe de seuil est la même famille que la retombée d'une lampe : évaluée par pixel,
//           donc inassertable en l'état, donc elle a besoin d'un oracle (cf. Light.h) ;
//         - et surtout **les poids du noyau sont TÉLÉVERSÉS depuis ce fichier**. Un noyau dont les
//           poids ne somment pas exactement à 1 change la luminosité globale de la lueur — un défaut
//           qui ressemble à un mauvais réglage d'`intensity` et qu'on compense alors au mauvais
//           bouton. En les téléversant, il n'existe qu'UNE source de vérité, et le test unitaire
//           prouve ce que le GPU utilise. Les écrire en dur dans le .sc en ferait une copie qui dérive.
//
// COMMENT: `brightPassFraction` renvoie un SCALAIRE que l'appelant multiplie par la couleur. C'est ce
//         qui préserve la teinte : seuiller chaque canal séparément décalerait la couleur d'un pixel
//         dont un seul canal dépasse (une étincelle rouge-orangé brillerait rouge pur).
//
// Plan : docs/design/lighting-bloom.md
// ============================================================================

#include <cmath>

namespace grove {
namespace light {

// Perceptual luminance, Rec. 709. Les poids somment à 1, donc le blanc vaut exactement 1 et le seuil
// documenté (1.0 = « seulement le sur-brillant ») veut dire ce qu'il dit.
//
// ⚠️ PAS d'écrêtage. Les cibles sont en RGBA16F précisément pour que les valeurs dépassent 1, et une
// luminance saturée à 1 ferait briller identiquement tout ce qui dépasse — ce qui reviendrait à jeter
// l'information pour laquelle le format a été choisi.
inline float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Largeur du genou : la moitié du seuil. C'est un choix, pas un bouton — un auteur ne saurait pas
// régler une largeur de genou, et la garder proportionnelle au seuil fait que la douceur suit
// l'échelle de ce qu'on seuille.
inline float bloomKnee(float threshold) {
    return threshold * 0.5f;
}

// Fraction de la couleur qui brille, dans [0,1].
//
// COMMENT (courbe de Karis) — trois régimes :
//   1. luma <= seuil - genou  ........ exactement 0. Pas « presque » : une scène non éclairée ne doit
//                                      pas acquérir un voile uniforme.
//   2. dans le genou .................  quadratique, donc la lueur s'amorce progressivement.
//   3. au-delà du seuil .............. (luma - seuil) / luma, la part linéaire classique.
//
// POURQUOI le genou plutôt qu'un seuil net : la version nette est continue en VALEUR mais sa PENTE
// saute de 0 à 1/seuil au franchissement, et la lueur s'amorce alors par un ourlet net à l'endroit
// exact où la scène atteint le seuil. C'est la loi maison « saturer en douceur, pas borner dur »
// appliquée à un seuil au lieu d'une borne.
//
// Le résultat est une FRACTION et jamais plus de 1 : la passe extrait, elle n'amplifie pas.
inline float brightPassFraction(float luma, float threshold) {
    if (luma <= 0.0f) return 0.0f;
    // Seuil nul = tout passe, à l'identique. C'est la façon documentée de baigner une scène entière
    // dans un voile, et c'est aussi le cas où la division par le genou exploserait.
    if (threshold <= 0.0f) return 1.0f;

    const float knee = bloomKnee(threshold);

    // Partie douce : une parabole qui vaut 0 en (seuil - genou) et rejoint la droite en (seuil + genou).
    float soft = luma - threshold + knee;
    if (soft < 0.0f)         soft = 0.0f;
    if (soft > 2.0f * knee)  soft = 2.0f * knee;
    soft = (soft * soft) / (4.0f * knee);

    // Le max des deux régimes : la parabole gouverne autour du seuil, la droite au-delà.
    float contribution = (soft > (luma - threshold)) ? soft : (luma - threshold);
    if (contribution <= 0.0f) return 0.0f;

    const float f = contribution / luma;
    return (f > 1.0f) ? 1.0f : f;
}

// Applique la fraction à une couleur. `out` reçoit 3 flottants.
//
// La teinte est préservée par construction : un seul scalaire multiplie les trois canaux, donc le
// résultat reste sur la même demi-droite depuis le noir.
inline void brightPass(float r, float g, float b, float threshold, float* out) {
    const float f = brightPassFraction(luminance(r, g, b), threshold);
    out[0] = r * f;
    out[1] = g * f;
    out[2] = b * f;
}

// Demi-noyau gaussien d'un flou séparable à **9 taps** : `out` reçoit 5 poids (centre + 4), le shader
// réfléchissant les quatre derniers.
//
// ⚠️ La normalisation porte sur les taps RÉFLÉCHIS : w0 + 2·(w1+w2+w3+w4) == 1. Normaliser les cinq
// comme s'ils étaient cinq taps assombrirait le flou d'un facteur ~2 — ce qui se lit comme
// « l'intensité est trop basse » et se compense au mauvais endroit.
//
// `sigma` est en **texels de la cible de flou** (donc en pixels du quart de résolution). Un sigma nul
// ou négatif dégrade en passe-plat (centre = 1) au lieu de produire un NaN : le renderer dérive sigma
// d'un rayon publié par le jeu, donc cette entrée est atteignable, et un poids NaN se propagerait
// dans tout le flou pour peindre l'écran en noir.
inline void bloomHalfKernel(float sigma, float* out) {
    if (!(sigma > 0.0f)) {                 // couvre 0, les négatifs et un NaN entrant
        out[0] = 1.0f;
        out[1] = out[2] = out[3] = out[4] = 0.0f;
        return;
    }

    const float inv2s2 = 1.0f / (2.0f * sigma * sigma);
    float w[5];
    for (int i = 0; i < 5; ++i) {
        const float d = static_cast<float>(i);
        w[i] = std::exp(-(d * d) * inv2s2);
    }

    const float total = w[0] + 2.0f * (w[1] + w[2] + w[3] + w[4]);
    for (int i = 0; i < 5; ++i) out[i] = w[i] / total;
}

// De combien réduire la cible de flou, pour un rayon de lueur donné en PIXELS ÉCRAN.
//
// QUOI  : le facteur de sous-échantillonnage (4, 8 ou 16). 4 = quart de résolution.
//
// POURQUOI cette fonction existe — c'est la correction d'un défaut VU sur une capture, pas déduit.
//         Le noyau a 9 taps ; leur écartement doit couvrir `radius`, donc il vaut `radius / (4·D)`
//         texels quand un texel fait `D` pixels. À D fixé, un rayon qui grandit écarte les taps, et
//         **passé environ 1,5 texel d'écartement la gaussienne est sous-échantillonnée** : chaque tap
//         imprime sa propre copie de la forme lumineuse au lieu de la lisser, ce qui donne un FESTON.
//         Mesuré : à D=4 le feston est franc dès 40 px, alors que la doc annonçait « ~60 px » par
//         raisonnement.
//
//         La réponse n'est donc pas un noyau plus large mais un TEXEL plus gros — et le mécanisme
//         exact vaut d'être dit, parce qu'il n'est pas celui qu'on suppose : **les taps tombent aux
//         MÊMES positions écran** dans tous les cas (le plus externe doit valoir `radius`, c'est
//         imposé). Ce qui change est l'EMPREINTE de chaque tap, soit un texel. À D=4 un tap couvre
//         4 px et laisse 12 px de trou jusqu'à son voisin — ces trous SONT le feston. À D=16 il couvre
//         16 px et rejoint son voisin. « Écartement ≤ 1,5 texel » dit exactement ça : l'empreinte
//         couvre le trou. C'est ce que fait une chaîne de mips en choisissant son niveau ; ici on
//         choisit le niveau directement, sans chaîne.
//
// COMMENT: bornes = 24 px à D=4, 48 px à D=8, 96 px à D=16, chacune étant le rayon où l'écartement
//         atteint 1,5 texel. Les facteurs sont des PUISSANCES DE DEUX et il n'y en a que trois : un
//         changement de facteur oblige à rebâtir les cibles, donc un jeu qui rampe son rayon (pour un
//         fondu) ne doit pas provoquer une reconstruction par frame. Trois paliers = au plus deux
//         reconstructions sur toute la course du bouton.
//
// ⚠️ Au-delà de 96 px on reste à 16 et la qualité redégrade — passer à 32 rendrait la cible si petite
//    (40 px de large en 1280) que la lueur montrerait des BLOCS au lieu d'un feston. On échange un
//    artefact contre un autre, donc on s'arrête. Limite documentée, pas cachée.
inline int bloomDownsample(float radiusPx) {
    if (radiusPx <= 24.0f) return 4;
    if (radiusPx <= 48.0f) return 8;
    return 16;
}

// Écartement d'UN tap, en texels de la cible de flou, pour un rayon écran et un facteur de réduction.
//
// Le tap le plus externe est le 4e, donc il tombe à `4 · spacing` texels du centre, soit
// `4 · spacing · D` pixels écran — et c'est ce qu'on veut égal à `radius`. D'où la division par 4·D.
// Garder cette formule ici plutôt que dans la passe est ce qui rend la borne ci-dessus vérifiable :
// les deux se lisent ensemble.
inline float bloomTapSpacing(float radiusPx, int downsample) {
    if (downsample <= 0) return 0.0f;
    return radiusPx / (4.0f * static_cast<float>(downsample));
}

} // namespace light
} // namespace grove
