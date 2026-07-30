/**
 * Unit: les courbes de tonemapping (plan T, tranche T0) — oracle grove::light.
 *
 * QUOI  : les deux courbes de compression des hautes lumières et le réglage d'exposition.
 *
 * POURQUOI un oracle : même raison que pour le bloom et la retombée des lampes — la courbe est
 *         évaluée PAR PIXEL dans un shader, où rien ne s'assertionne. Ici l'enjeu est net : le
 *         tonemapping existe pour que **deux valeurs sur-brillantes différentes restent
 *         différentes**, et c'est une propriété qu'on peut vérifier exactement.
 *
 * ⚠️ CE FICHIER TESTE DES PROPRIÉTÉS, PAS DES CONSTANTES. Les coefficients d'ACES sont un ajustement
 *    empirique (Narkowicz), pas une dérivation : un test qui les recopierait ne prouverait que le
 *    copier-coller. Ce qui doit être vrai, en revanche, l'est indépendamment des chiffres — monotone,
 *    0 → 0, borné, et surtout INJECTIF au-dessus de 1, là où l'écrêtage 8 bits perd l'information.
 *
 * Plan : docs/design/lighting-tonemap.md
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/light/Tonemap.h>

#include <cmath>
#include <vector>

using namespace grove;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Les deux courbes partagent leur contrat, donc les propriétés se testent une fois pour les deux.
static std::vector<std::pair<const char*, float (*)(float)>> curves() {
    return {
        {"reinhard", &light::tonemapReinhard},
        {"aces",     &light::tonemapACES},
    };
}

TEST_CASE("tonemap: le noir reste noir", "[tonemap]") {
    // Une courbe qui décalerait le zéro remonterait le noir de toute la scène en un voile gris —
    // le défaut le plus visible possible sur un jeu spatial, et le plus facile à écrire par erreur.
    for (auto& [name, f] : curves()) {
        INFO("courbe " << name);
        CHECK_THAT(f(0.0f), WithinAbs(0.0f, 1e-6f));
    }
}

TEST_CASE("tonemap: monotone strict — l'ordre des luminosités est préservé", "[tonemap]") {
    // Sans ça, deux zones dont l'une est plus lumineuse que l'autre pourraient s'inverser à l'écran.
    for (auto& [name, f] : curves()) {
        INFO("courbe " << name);
        float prev = f(0.0f);
        for (float x = 0.01f; x <= 40.0f; x += 0.01f) {
            const float y = f(x);
            CHECK(y > prev - 1e-7f);
            prev = y;
        }
    }
}

TEST_CASE("tonemap: LE point du chantier — deux sur-brillances restent DISTINCTES", "[tonemap]") {
    // ⚠️ L'assertion qui justifie tout le plan T. Écrites dans un backbuffer 8 bits, les valeurs 2 et
    //    8 rendent EXACTEMENT le même blanc : l'information conservée depuis L1 par le choix RGBA16F
    //    est jetée à la dernière ligne. Une courbe de tonemapping doit les séparer.
    //
    //    La marge de 0,02 n'est pas cosmétique : elle représente ~5 niveaux sur 255, donc un écart
    //    qu'un œil voit et qu'un readback 8 bits peut mesurer. Une courbe qui les séparerait de 1e-4
    //    passerait un test « != » et ne changerait rien à l'image.
    for (auto& [name, f] : curves()) {
        INFO("courbe " << name);
        const float a = f(2.0f);
        const float b = f(8.0f);
        INFO("f(2)=" << a << " f(8)=" << b);
        CHECK(b > a + 0.02f);

        // ...et les deux restent SOUS 1, sinon elles se réécrêteraient au même blanc et on n'aurait
        // rien gagné. C'est la seconde moitié de la propriété, et elle est aussi importante.
        CHECK(a < 1.0f);
        CHECK(b <= 1.0f);
    }
}

TEST_CASE("tonemap: la compression ne franchit jamais 1, aussi haut qu'on monte", "[tonemap]") {
    // Un dépassement se réécrêterait, donc les très hautes valeurs redeviendraient indistinguables —
    // exactement le défaut qu'on corrige, repoussé plus loin au lieu d'être réglé.
    for (auto& [name, f] : curves()) {
        INFO("courbe " << name);
        for (float x : {1.0f, 5.0f, 20.0f, 100.0f, 1000.0f, 1e5f}) {
            INFO("x=" << x << " -> " << f(x));
            CHECK(f(x) <= 1.0f + 1e-5f);
        }
    }
}

TEST_CASE("tonemap: les tons moyens ne sont pas écrasés", "[tonemap]") {
    // Une courbe qui comprimerait dès le bas rendrait toute la scène plate et grise. La compression
    // doit se concentrer sur les hautes lumières : à mi-échelle, la sortie doit rester substantielle.
    for (auto& [name, f] : curves()) {
        INFO("courbe " << name);
        CHECK(f(0.5f) > 0.25f);
        // ...et la pente doit rester notable en bas : un doublement de l'entrée doit se voir.
        CHECK(f(0.4f) > f(0.2f) + 0.05f);
    }
}

TEST_CASE("tonemap: reinhard suit exactement x/(1+x)", "[tonemap]") {
    // La seule courbe dont la formule EST la spécification (aucune constante ajustée), donc la seule
    // qu'on peut assertionner au chiffre sans tester un copier-coller.
    for (float x : {0.0f, 0.25f, 1.0f, 3.0f, 7.0f, 15.0f}) {
        CHECK_THAT(light::tonemapReinhard(x), WithinRel(x / (1.0f + x), 1e-5f));
    }
    // Son point remarquable, celui qui explique la surprise annoncée au plan : 1 -> 0,5. Un jeu qui
    // active le tonemap sans toucher à l'exposition voit sa scène s'assombrir de moitié.
    CHECK_THAT(light::tonemapReinhard(1.0f), WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("tonemap: aces est plus CONTRASTÉ que reinhard, c'est sa raison d'être", "[tonemap]") {
    // Les deux modes doivent donner des looks DIFFÉRENTS, sinon en offrir deux est un mensonge. La
    // signature d'une courbe filmique est une épaule : elle tient les tons moyens plus haut que
    // reinhard tout en roulant vers le blanc.
    CHECK(light::tonemapACES(0.5f) > light::tonemapReinhard(0.5f));
    CHECK(light::tonemapACES(1.0f) > light::tonemapReinhard(1.0f));
}

TEST_CASE("tonemap: l'exposition place la scène SUR la courbe", "[tonemap]") {
    // `exposure` multiplie avant la courbe. Sans elle le tonemap est subi ; c'est le bouton qui décide
    // quelle partie de la plage dynamique atterrit dans les tons moyens.
    const float mid = light::tonemapReinhard(1.0f);
    CHECK(light::tonemapExposed(1.0f, 2.0f, light::TonemapMode::Reinhard) > mid);
    CHECK(light::tonemapExposed(1.0f, 0.5f, light::TonemapMode::Reinhard) < mid);

    // Exposition neutre = la courbe seule.
    CHECK_THAT(light::tonemapExposed(3.0f, 1.0f, light::TonemapMode::Reinhard),
               WithinRel(light::tonemapReinhard(3.0f), 1e-5f));
    CHECK_THAT(light::tonemapExposed(3.0f, 1.0f, light::TonemapMode::ACES),
               WithinRel(light::tonemapACES(3.0f), 1e-5f));

    // ⚠️ Mode None = IDENTITÉ EXACTE, y compris au-dessus de 1 et y compris avec une exposition.
    //    C'est le contournement à coût nul exprimé dans l'oracle : le défaut ne doit rien changer.
    for (float x : {0.0f, 0.5f, 1.0f, 4.0f, 50.0f}) {
        CHECK_THAT(light::tonemapExposed(x, 1.0f, light::TonemapMode::None), WithinAbs(x, 1e-6f));
    }

    // Entrées dégénérées : pas de NaN, pas de négatif qui remonterait.
    CHECK_THAT(light::tonemapExposed(1.0f, 0.0f, light::TonemapMode::Reinhard), WithinAbs(0.0f, 1e-6f));
    CHECK(light::tonemapExposed(1.0f, -3.0f, light::TonemapMode::Reinhard) >= 0.0f);
    CHECK(light::tonemapReinhard(-1.0f) >= 0.0f);
    CHECK(light::tonemapACES(-1.0f) >= 0.0f);
}

TEST_CASE("tonemap: reinhard separe A L'INFINI, aces sature vers 6 -- l'arbitrage entre les deux",
          "[tonemap]") {
    // ⚠️ Ce cas est ne d'une MESURE GPU et pas du plan : a intensite 8, ACES rendait 255 la ou reinhard
    //    rendait 226. L'epaule de l'ajustement de Narkowicz atteint le blanc vers x ~ 6, donc au-dela
    //    ACES re-ecrete -- exactement le defaut que le tonemapping corrige, mais repousse plus loin.
    //
    // Ce n'est pas un defaut a corriger, c'est ce que fait une courbe filmique : elle a un point blanc.
    // Mais c'est un ARBITRAGE que l'auteur doit connaitre, et c'est ce qui justifie d'offrir les deux
    // modes plutot qu'un seul :
    //   - plage dynamique extreme (une supernova a cote d'une bougie) -> reinhard, qui n'atteint
    //     jamais 1 et separe donc indefiniment ;
    //   - rendu filmique contraste avec un vrai blanc -> aces, en reglant `exposure` pour que la scene
    //     tienne sous son point blanc.
    //
    // On assertionne des FAITS sur les courbes, pas la limitation : si l'ajustement d'ACES etait un
    // jour remplace par un meilleur, ce test devrait etre relu, pas simplement rendu vert.

    // Reinhard reste injectif aussi haut qu'on monte, et c'est sa propriete distinctive.
    CHECK(light::tonemapReinhard(20.0f) > light::tonemapReinhard(8.0f) + 0.005f);
    CHECK(light::tonemapReinhard(200.0f) > light::tonemapReinhard(20.0f) + 0.001f);
    CHECK(light::tonemapReinhard(1e6f) < 1.0f);          // jamais atteint

    // ACES est essentiellement blanc des ~6 : son point blanc est la, et c'est mesurable.
    CHECK(light::tonemapACES(6.0f) > 0.98f);
    // Donc au-dela, deux valeurs tres differentes se confondent -- fait constate, a connaitre.
    CHECK(light::tonemapACES(20.0f) - light::tonemapACES(8.0f) < 0.01f);

    // La ou ACES gagne : sous son point blanc, il tient les tons moyens plus haut que reinhard.
    for (float x : {0.3f, 0.6f, 1.0f, 2.0f, 3.0f}) {
        INFO("x=" << x << " aces=" << light::tonemapACES(x) << " reinhard=" << light::tonemapReinhard(x));
        CHECK(light::tonemapACES(x) > light::tonemapReinhard(x));
    }
}
