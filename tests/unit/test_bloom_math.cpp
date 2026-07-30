/**
 * Unit: the bloom math (plan B, tranche B0) — grove::light bloom oracle.
 *
 * QUOI  : the bright-pass curve (what fraction of a pixel blooms) and the blur kernel weights.
 *
 * POURQUOI ce fichier existe alors que le bloom est un effet purement visuel — trois raisons, et la
 *         troisième est la vraie :
 *
 *         1. C'est la convention de tout l'éclairage ici : la courbe de retombée d'une lampe est en
 *            C++ dans grove::light et le shader la mime (LightMathUnit). Un shader ne s'assertionne
 *            pas ; son oracle, oui.
 *         2. Le genou du seuil se juge sur sa DÉRIVÉE, pas sur ses valeurs — et une dérivée ne se
 *            lit pas sur une capture. Sans genou, la pente saute de 0 à 1/seuil au franchissement :
 *            la lueur s'amorce par un ourlet net à l'endroit exact où la scène atteint le seuil.
 *            (Ce point a d'abord été écrit faux dans ce fichier ; voir le test concerné.)
 *         3. LES POIDS DU NOYAU SONT TÉLÉVERSÉS depuis ce fichier, pas écrits en dur dans le .sc.
 *            Un noyau dont les poids ne somment pas exactement à 1 change la luminosité globale de
 *            la lueur — un bug qui ressemble à un mauvais réglage d'intensity, qu'on « corrige »
 *            alors en tournant le bouton, et qui reste. Ici l'oracle est la source unique et ce test
 *            prouve ce que le GPU utilise.
 *
 * Plan : docs/design/lighting-bloom.md
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/light/Bloom.h>

#include <cmath>

using namespace grove;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("bloom: luminance is a normalised perceptual weighting", "[bloom]") {
    // Sums to 1 by construction, so white is exactly 1 and black exactly 0. A kernel that did not
    // normalise would make a white pixel bloom at some other threshold than the documented one.
    CHECK_THAT(light::luminance(1.0f, 1.0f, 1.0f), WithinAbs(1.0f, 1e-6f));
    CHECK_THAT(light::luminance(0.0f, 0.0f, 0.0f), WithinAbs(0.0f, 1e-6f));

    // Perceptual, not an average: green carries most of the luminance, blue least. A plain (r+g+b)/3
    // would make these three equal, which is the mistake this asserts against.
    const float r = light::luminance(1.0f, 0.0f, 0.0f);
    const float g = light::luminance(0.0f, 1.0f, 0.0f);
    const float b = light::luminance(0.0f, 0.0f, 1.0f);
    CHECK(g > r);
    CHECK(r > b);

    // Overbright is NOT clamped — the whole point of the RGBA16F targets is that values past 1 exist,
    // and a luminance that saturated at 1 would make every overbright pixel bloom identically.
    CHECK(light::luminance(3.0f, 3.0f, 3.0f) > 2.9f);
}

TEST_CASE("bloom: the bright pass is a fraction, and it is zero well below the knee", "[bloom]") {
    const float threshold = 1.0f;

    // Below the knee's start (threshold - knee = threshold/2) NOTHING blooms. Not "almost nothing":
    // exactly zero, or an unlit scene would acquire a uniform veil.
    CHECK_THAT(light::brightPassFraction(0.0f,  threshold), WithinAbs(0.0f, 1e-7f));
    CHECK_THAT(light::brightPassFraction(0.25f, threshold), WithinAbs(0.0f, 1e-7f));
    CHECK_THAT(light::brightPassFraction(0.5f,  threshold), WithinAbs(0.0f, 1e-7f));

    // Well above the threshold the curve is the linear one: (luma - threshold) / luma.
    CHECK_THAT(light::brightPassFraction(4.0f, threshold), WithinRel((4.0f - 1.0f) / 4.0f, 1e-4f));

    // A fraction, so it never exceeds 1 however overbright the pixel — otherwise the pass would
    // AMPLIFY rather than extract, and a bright pixel would bloom brighter than itself.
    for (float luma = 0.0f; luma < 50.0f; luma += 0.37f) {
        const float f = light::brightPassFraction(luma, threshold);
        CHECK(f >= 0.0f);
        CHECK(f <= 1.0f);
    }
}

TEST_CASE("bloom: the knee softens the SLOPE, and that is what it is for", "[bloom]") {
    // ⚠️ Ce test a d'abord été écrit sur une justification FAUSSE, et l'erreur méritait d'être
    // corrigée ici plutôt qu'effacée. J'avais écrit que le seuil net produisait une DISCONTINUITÉ de
    // valeur qui « scintille ». C'est faux : la version nette, max(0, luma - t) / luma, vaut
    // exactement 0 en t et croît depuis 0 — elle est continue en valeur. Un test sur les sauts de
    // valeur aurait donc passé au vert avec un seuil net, sans rien discriminer : le piège même que
    // cette session a documenté deux fois.
    //
    // Ce que le genou change vraiment, c'est la DÉRIVÉE. Sans lui, la pente saute de 0 à 1/t en
    // franchissant le seuil : la lueur commence par un pli visible, un ourlet net à l'endroit exact
    // où la scène atteint le seuil. Avec lui, elle démarre plus bas et monte progressivement.
    //
    // LE DISCRIMINANT, et il est simple : sous le seuil mais dans le genou, un seuil net rend
    // EXACTEMENT zéro, et le genou rend quelque chose.
    const float threshold = 1.0f;

    CHECK(light::brightPassFraction(0.70f, threshold) > 0.0f);   // dans le genou (t/2 .. t)
    CHECK(light::brightPassFraction(0.95f, threshold) > 0.0f);

    // ...et ce quelque chose reste modeste : le genou adoucit l'amorce, il ne fait pas briller une
    // scène normalement exposée. La borne est CALCULÉE, pas devinée — j'avais d'abord écrit 0.10 au
    // feeling et la vraie valeur est 0.1066, ce qui a fait échouer le test pour une raison qui
    // n'apprenait rien. À 95 % du seuil : genou = 0.5, doux = (0.45²)/(4·0.5) = 0.10125,
    // fraction = 0.10125/0.95 = 0.1066.
    CHECK_THAT(light::brightPassFraction(0.95f, threshold), WithinRel(0.1066f, 1e-3f));
    CHECK(light::brightPassFraction(0.95f, threshold) < 0.15f);

    // La progression est monotone et sans à-coup sur tout le domaine, genou compris.
    float prev = light::brightPassFraction(0.0f, threshold);
    float worstJump = 0.0f;
    const float dl = 0.002f;
    for (float luma = dl; luma <= 3.0f; luma += dl) {
        const float f = light::brightPassFraction(luma, threshold);
        worstJump = std::fmax(worstJump, std::fabs(f - prev));
        CHECK(f >= prev - 1e-6f);            // monotone : plus lumineux ne peut pas briller MOINS
        prev = f;
    }
    INFO("worst adjacent jump = " << worstJump);
    CHECK(worstJump < 0.01f);

    // La pente de part et d'autre du seuil : le genou la rend continue, à un facteur près. Sans
    // genou, la pente à gauche est 0 et celle à droite 1/t — un rapport infini.
    const float eps = 0.01f;
    const float slopeLeft  = (light::brightPassFraction(threshold, threshold)
                            - light::brightPassFraction(threshold - eps, threshold)) / eps;
    const float slopeRight = (light::brightPassFraction(threshold + eps, threshold)
                            - light::brightPassFraction(threshold, threshold)) / eps;
    INFO("slope left=" << slopeLeft << " right=" << slopeRight);
    CHECK(slopeLeft > 0.0f);                        // 0 exactement avec un seuil net
    CHECK(slopeLeft < slopeRight * 3.0f);           // du même ordre, pas un saut
}

TEST_CASE("bloom: a zero threshold passes everything through untouched", "[bloom]") {
    // The degenerate setting must be a TRUE identity, not "almost": it is the documented way to bloom
    // a whole scene (a dreamy veil), and it is also where a division by the knee would blow up.
    //
    // La boucle part au-dessus de zéro : à luma == 0 la garde « rien à extraire » passe AVANT celle du
    // seuil et la fraction vaut 0. C'est volontaire et sans conséquence — cette fraction multiplie une
    // couleur noire — mais ma première version bouclait depuis 0 et échouait sur ce cas, en
    // prétendant tester le seuil nul alors qu'elle testait l'ordre de deux gardes.
    for (float luma = 0.05f; luma < 5.0f; luma += 0.31f) {
        CHECK_THAT(light::brightPassFraction(luma, 0.0f), WithinAbs(1.0f, 1e-5f));
    }
    CHECK_THAT(light::brightPassFraction(0.0f, 0.0f), WithinAbs(0.0f, 1e-7f));   // le cas noir, explicite
}

TEST_CASE("bloom: the bright pass preserves HUE", "[bloom]") {
    // The fraction multiplies the whole colour, so the result stays on the same ray from black.
    // Thresholding each channel SEPARATELY would shift the hue of any pixel whose channels straddle
    // the threshold — a red-orange spark would bloom pure red.
    float out[3] = {0, 0, 0};
    light::brightPass(2.0f, 1.0f, 0.5f, 1.0f, out);

    const float f = light::brightPassFraction(light::luminance(2.0f, 1.0f, 0.5f), 1.0f);
    CHECK(f > 0.0f);                                   // this sample must actually bloom
    CHECK_THAT(out[0], WithinRel(2.0f * f, 1e-5f));
    CHECK_THAT(out[1], WithinRel(1.0f * f, 1e-5f));
    CHECK_THAT(out[2], WithinRel(0.5f * f, 1e-5f));

    // ...and the ratios survive, which is the hue statement itself.
    CHECK_THAT(out[0] / out[1], WithinRel(2.0f, 1e-4f));
    CHECK_THAT(out[1] / out[2], WithinRel(2.0f, 1e-4f));
}

TEST_CASE("bloom: the blur kernel is normalised over its REFLECTED taps", "[bloom]") {
    // 9 taps, 5 unique weights (symmetric): the total the shader accumulates is w0 + 2*(w1+..+w4).
    // Normalising the five as if they were five taps is the easy mistake — it would DARKEN the blur
    // by ~2x, which reads as "intensity is too low" and gets compensated at the wrong knob.
    float w[5] = {0, 0, 0, 0, 0};
    light::bloomHalfKernel(2.0f, w);

    const float total = w[0] + 2.0f * (w[1] + w[2] + w[3] + w[4]);
    INFO("reflected total = " << total);
    CHECK_THAT(total, WithinAbs(1.0f, 1e-5f));

    // Gaussian shape: positive and strictly decreasing away from the centre. A kernel with a
    // non-monotone tail is a box blur with extra steps and shows ringing.
    for (int i = 0; i < 5; ++i) CHECK(w[i] > 0.0f);
    for (int i = 1; i < 5; ++i) CHECK(w[i] < w[i - 1]);

    // Tighter sigma concentrates the centre; wider spreads it. Both must stay normalised, since the
    // renderer picks sigma from a config knob.
    float tight[5] = {0, 0, 0, 0, 0};
    light::bloomHalfKernel(0.7f, tight);
    CHECK(tight[0] > w[0]);
    CHECK_THAT(tight[0] + 2.0f * (tight[1] + tight[2] + tight[3] + tight[4]), WithinAbs(1.0f, 1e-5f));

    float wide[5] = {0, 0, 0, 0, 0};
    light::bloomHalfKernel(6.0f, wide);
    CHECK(wide[0] < w[0]);
    CHECK_THAT(wide[0] + 2.0f * (wide[1] + wide[2] + wide[3] + wide[4]), WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("bloom: a degenerate sigma degrades to a passthrough, not to a NaN", "[bloom]") {
    // sigma 0 would divide by zero in the Gaussian. The renderer derives sigma from a published
    // radius, so a game CAN reach this — and a NaN weight would propagate through the blur and paint
    // the whole screen black. Fail-soft: the centre tap keeps everything.
    float w[5] = {0, 0, 0, 0, 0};
    light::bloomHalfKernel(0.0f, w);
    CHECK_THAT(w[0], WithinAbs(1.0f, 1e-5f));
    for (int i = 1; i < 5; ++i) CHECK_THAT(w[i], WithinAbs(0.0f, 1e-6f));

    float neg[5] = {0, 0, 0, 0, 0};
    light::bloomHalfKernel(-3.0f, neg);
    CHECK_THAT(neg[0], WithinAbs(1.0f, 1e-5f));
}
