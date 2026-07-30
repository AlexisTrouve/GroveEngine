/**
 * Unit: la colorimetrie (plan G, tranche Ga) — oracle grove::light.
 *
 * QUOI  : teinte -> contraste -> saturation, dans cet ordre, sur une couleur deja tonemappee.
 *
 * POURQUOI un oracle ici alors que le FONDU n'en a pas eu : parce qu'il y a de quoi se tromper. Le
 *         fondu est un `lerp`, il n'a aucune decision. Ici il y en a quatre :
 *           - la luminance a REUTILISER (celle du bloom) plutot qu'a reinventer ;
 *           - le pivot du contraste, qui a une bonne et une mauvaise reponse ;
 *           - l'ordre des trois operations, non commutatif ;
 *           - et la propriete exacte : `saturation 0` doit rendre un gris dont la LUMINANCE est celle
 *             de l'original, pas n'importe quel gris.
 *
 *         Le motif « une piece pure par tranche » n'est donc ni suivi par habitude ni abandonne par
 *         flemme : il s'applique quand il y a une decision a verrouiller.
 *
 * Plan : docs/design/lighting-grade.md
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/light/Grade.h>

#include <cmath>

using namespace grove;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Les neutres, ecrits une fois : c'est l'identite attendue.
static light::GradeParams neutral() { return light::GradeParams{}; }

TEST_CASE("grade: les neutres sont une IDENTITE EXACTE", "[grade]") {
    // Le contournement a cout nul exprime dans l'oracle : un jeu qui publie `render:grade {}` ou
    // `{saturation: 1}` demande explicitement « pas d'etalonnage », et ca doit etre gratuit ET exact.
    const light::GradeParams p = neutral();
    for (float v = 0.0f; v <= 1.0f; v += 0.05f) {
        float out[3] = {0, 0, 0};
        light::gradeColor(v, v * 0.5f, 1.0f - v, p, out);
        INFO("v=" << v);
        CHECK_THAT(out[0], WithinAbs(v, 1e-6f));
        CHECK_THAT(out[1], WithinAbs(v * 0.5f, 1e-6f));
        CHECK_THAT(out[2], WithinAbs(1.0f - v, 1e-6f));
    }
    CHECK(light::gradeIsNeutral(p));
}

TEST_CASE("grade: saturation 0 rend un gris de la BONNE luminance", "[grade]") {
    // ⚠️ LE test de ce fichier, et le discriminant du plan. Une desaturation juste interpole vers la
    //    luminance Rec. 709 ; une moyenne (r+g+b)/3 rendrait un rouge pur et un bleu pur au MEME gris.
    //    Une assertion « c'est devenu gris » passerait avec la mauvaise formule.
    light::GradeParams p = neutral();
    p.saturation = 0.0f;

    float red[3] = {0, 0, 0};
    light::gradeColor(1.0f, 0.0f, 0.0f, p, red);
    float blue[3] = {0, 0, 0};
    light::gradeColor(0.0f, 0.0f, 1.0f, p, blue);
    float green[3] = {0, 0, 0};
    light::gradeColor(0.0f, 1.0f, 0.0f, p, green);

    // Gris = les trois canaux egaux.
    CHECK_THAT(red[0], WithinAbs(red[1], 1e-6f));
    CHECK_THAT(red[1], WithinAbs(red[2], 1e-6f));
    CHECK_THAT(blue[0], WithinAbs(blue[2], 1e-6f));

    // ...et le gris vaut la LUMINANCE de l'original. C'est la meme fonction que le seuil du bloom.
    CHECK_THAT(red[0],   WithinRel(light::luminance(1.0f, 0.0f, 0.0f), 1e-5f));
    CHECK_THAT(green[0], WithinRel(light::luminance(0.0f, 1.0f, 0.0f), 1e-5f));
    CHECK_THAT(blue[0],  WithinRel(light::luminance(0.0f, 0.0f, 1.0f), 1e-5f));

    // LA discrimination : le gris du rouge doit etre ~3x celui du bleu (0.2126 contre 0.0722), et le
    // vert doit dominer les deux. Une moyenne plate les rendrait tous les trois a 0.333.
    INFO("rouge=" << red[0] << " vert=" << green[0] << " bleu=" << blue[0]);
    CHECK(green[0] > red[0]);
    CHECK(red[0] > blue[0] * 2.5f);
    CHECK(red[0] < 0.30f);        // et surtout PAS 0.333 : ce serait la moyenne
}

TEST_CASE("grade: saturation > 1 ecarte de la luminance", "[grade]") {
    // Un effet legitime (couleurs criardes), donc pas borne en haut. La borne finale de sortie s'occupe
    // du depassement eventuel par canal.
    light::GradeParams p = neutral();
    p.saturation = 2.0f;

    float out[3] = {0, 0, 0};
    // Un rouge deja domine par le rouge doit le devenir davantage.
    light::gradeColor(0.6f, 0.4f, 0.4f, p, out);
    INFO("r=" << out[0] << " g=" << out[1] << " b=" << out[2]);
    CHECK(out[0] > 0.6f);
    CHECK(out[1] < 0.4f);
}

TEST_CASE("grade: le contraste ecarte du GRIS MOYEN, dans les deux sens", "[grade]") {
    // ⚠️ Les DEUX sens, et c'est le discriminant : un simple gain eclaircirait tout, donc une assertion
    //    sur le seul pixel clair passerait sans qu'il s'agisse d'un contraste.
    light::GradeParams p = neutral();
    p.contrast = 2.0f;

    float dark[3] = {0, 0, 0};
    light::gradeColor(0.3f, 0.3f, 0.3f, p, dark);
    float bright[3] = {0, 0, 0};
    light::gradeColor(0.7f, 0.7f, 0.7f, p, bright);

    INFO("0.3 -> " << dark[0] << " | 0.7 -> " << bright[0]);
    CHECK(dark[0] < 0.3f);        // le sombre s'assombrit
    CHECK(bright[0] > 0.7f);      // le clair s'eclaircit

    // Le PIVOT est 0,5 et pas 0,18 : on opere apres le tonemapping, dans un espace d'affichage borne
    // ou le gris moyen est a 0,5. Un pivot a 0,18 assombrirait toute image contrastee, ce qui se
    // lirait comme « le contraste assombrit » -- un defaut incomprehensible sans cette ligne.
    float mid[3] = {0, 0, 0};
    light::gradeColor(0.5f, 0.5f, 0.5f, p, mid);
    CHECK_THAT(mid[0], WithinAbs(0.5f, 1e-6f));    // le pivot est un point FIXE

    // Un contraste < 1 rapproche du gris : la brume laiteuse.
    light::GradeParams flat = neutral();
    flat.contrast = 0.25f;
    float flatDark[3] = {0, 0, 0};
    light::gradeColor(0.0f, 0.0f, 0.0f, flat, flatDark);
    CHECK(flatDark[0] > 0.3f);     // le noir remonte vers le gris
    CHECK(flatDark[0] < 0.5f);
}

TEST_CASE("grade: la teinte est une multiplication par canal", "[grade]") {
    light::GradeParams p = neutral();
    p.tintR = 0.5f; p.tintG = 1.0f; p.tintB = 1.5f;   // une nuit bleutee

    float out[3] = {0, 0, 0};
    light::gradeColor(0.6f, 0.6f, 0.6f, p, out);
    INFO("r=" << out[0] << " g=" << out[1] << " b=" << out[2]);
    CHECK_THAT(out[0], WithinRel(0.30f, 1e-4f));
    CHECK_THAT(out[1], WithinRel(0.60f, 1e-4f));
    CHECK_THAT(out[2], WithinRel(0.90f, 1e-4f));
}

TEST_CASE("grade: l'ORDRE des trois operations n'est pas commutatif", "[grade]") {
    // ⚠️ L'ordre retenu est teinte -> contraste -> saturation, celui d'un etalonnage reel. Ce test
    //    prouve qu'il IMPORTE : teinter apres avoir desature donnerait un virage monochrome (sepia) au
    //    lieu d'une image equilibree puis desaturee. Les deux sont des effets, un seul est ce qu'on
    //    attend de trois boutons nommes ainsi.
    //
    // La preuve : avec saturation 0 ET une teinte, le resultat doit rester GRIS -- puisque la teinte
    // s'applique AVANT, donc la desaturation qui suit ecrase toute couleur. Si l'ordre etait inverse,
    // la sortie serait teintee.
    light::GradeParams p = neutral();
    p.saturation = 0.0f;
    p.tintR = 1.5f; p.tintG = 1.0f; p.tintB = 0.5f;

    float out[3] = {0, 0, 0};
    light::gradeColor(0.5f, 0.5f, 0.5f, p, out);
    INFO("r=" << out[0] << " g=" << out[1] << " b=" << out[2]);
    CHECK_THAT(out[0], WithinAbs(out[1], 1e-6f));
    CHECK_THAT(out[1], WithinAbs(out[2], 1e-6f));
    // ...et ce gris tient compte de la teinte appliquee avant : la luminance de (0.75, 0.5, 0.25) et
    // non celle de (0.5, 0.5, 0.5). La teinte n'est donc pas simplement ignoree.
    CHECK_THAT(out[0], WithinRel(light::luminance(0.75f, 0.5f, 0.25f), 1e-4f));
}

TEST_CASE("grade: la sortie est bornee a [0,1], une seule fois, A LA FIN", "[grade]") {
    // Le contraste peut sortir de la plage : (0 - 0.5)*2 + 0.5 = -0.5. Un canal negatif donnerait un
    // comportement dependant du backend.
    light::GradeParams p = neutral();
    p.contrast = 3.0f;
    float out[3] = {0, 0, 0};
    light::gradeColor(0.0f, 0.0f, 1.0f, p, out);
    for (int i = 0; i < 3; ++i) {
        INFO("canal " << i << " = " << out[i]);
        CHECK(out[i] >= 0.0f);
        CHECK(out[i] <= 1.0f);
    }

    // ⚠️ Bornee A LA FIN et pas entre chaque etape : un intermediaire hors plage que la suite ramene
    //    dans la plage doit survivre. Ici la teinte pousse a 1,8 puis le contraste faible le ramene --
    //    un ecretage intermediaire aurait perdu l'information et rendu un resultat plus sombre.
    light::GradeParams q = neutral();
    q.tintR = 3.0f;
    q.contrast = 0.25f;
    float chained[3] = {0, 0, 0};
    light::gradeColor(0.6f, 0.6f, 0.6f, q, chained);
    // (0.6*3 = 1.8 - 0.5)*0.25 + 0.5 = 0.825 -- donc PAS le 0.5 qu'un ecretage a 1.0 aurait donne
    // ((1.0-0.5)*0.25+0.5 = 0.625). La difference est mesurable, c'est ce qui rend le test utile.
    INFO("chaine sans ecretage intermediaire : " << chained[0]);
    CHECK(chained[0] > 0.70f);
}

TEST_CASE("grade: gradeIsNeutral reconnait exactement les neutres", "[grade]") {
    // C'est cette fonction qui decide si la passe de presentation doit exister. Un faux positif rendrait
    // la colorimetrie inerte (le chainon jamais cable) ; un faux negatif ferait payer une passe plein
    // ecran a un jeu qui n'a rien demande.
    CHECK(light::gradeIsNeutral(neutral()));

    light::GradeParams p = neutral(); p.saturation = 0.999f;
    CHECK_FALSE(light::gradeIsNeutral(p));
    p = neutral(); p.contrast = 1.001f;
    CHECK_FALSE(light::gradeIsNeutral(p));
    p = neutral(); p.tintB = 0.99f;
    CHECK_FALSE(light::gradeIsNeutral(p));

    // La comparaison est EXACTE, sans epsilon : les valeurs viennent de defauts JSON, donc elles valent
    // exactement 1.0f quand elles sont absentes.
    p = neutral(); p.saturation = 1.0f; p.contrast = 1.0f;
    CHECK(light::gradeIsNeutral(p));
}
