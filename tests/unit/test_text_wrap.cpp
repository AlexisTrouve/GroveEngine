/**
 * Unit Tests: grove::text::wrapText — retour à la ligne automatique, PUR (sans police, sans GPU).
 *
 * QUOI     : le découpage d'un texte en LIGNES VISUELLES (ce qu'on voit sur une rangée), par
 *            opposition aux lignes LOGIQUES (ce que séparent les '\n').
 *
 * POURQUOI : dès qu'un texte se replie, chaque traduction entre (ligne, colonne) et pixels doit
 *            passer par les lignes visuelles — clic, curseur, flèches Haut/Bas, surlignage,
 *            défilement. Se tromper de notion à un seul de ces endroits produit un décalage que rien
 *            ne rattrape. Le découpage étant ISOLÉ ici, il se teste exhaustivement sans monter de
 *            widget ni de fenêtre.
 *
 * COMMENT  : métrique factice à 10px par glyphe ASCII, donc toute largeur attendue se compte à la
 *            main : "abc" = 30, une boîte de 100 tient 10 caractères. Les cas UTF-8 utilisent des
 *            octets explicites.
 */

#include <catch2/catch_test_macros.hpp>

#include <grove/text/TextWrap.h>

#include <string>
#include <vector>

using namespace grove::text;

namespace {

// Chaque glyphe fait 10px, quel qu'il soit.
auto tenPx = [](uint32_t) { return 10.0f; };

// Rend les lignes visuelles sous forme de chaînes, pour que les attentes se lisent d'un coup d'œil.
std::vector<std::string> drawn(const std::string& text, float maxWidth) {
    std::vector<std::string> out;
    for (const VisualLine& v : wrapText(text, maxWidth, tenPx)) {
        out.push_back(text.substr(v.begin, v.length()));
    }
    return out;
}

const std::string kE = "\xC3\xA9";  // "é"

}  // namespace

// ============================================================================
// Sans repli : le découpage logique, à l'identique
// ============================================================================

TEST_CASE("Wrap: maxWidth <= 0 signifie AUCUN repli", "[text][unit][wrap]") {
    // Le patron « défaut à coût zéro » : un appelant qui ne veut pas de wrap obtient exactement le
    // découpage logique, sans chemin de code particulier.
    REQUIRE(drawn("une ligne tres tres longue qui deborderait", 0.0f)
            == std::vector<std::string>{"une ligne tres tres longue qui deborderait"});
}

TEST_CASE("Wrap: les coupures DURES sont toujours respectees", "[text][unit][wrap]") {
    REQUIRE(drawn("ab\ncd", 0.0f) == std::vector<std::string>{"ab", "cd"});
    // Meme avec une largeur genereuse, un '\n' coupe.
    REQUIRE(drawn("ab\ncd", 1000.0f) == std::vector<std::string>{"ab", "cd"});
}

TEST_CASE("Wrap: un texte vide produit UNE ligne vide", "[text][unit][wrap]") {
    // Invariant sur lequel la vue s'appuie : « la ligne 0 existe », donc aucun cas particulier.
    const auto lines = wrapText(std::string_view(""), 100.0f, tenPx);
    REQUIRE(lines.size() == 1u);
    REQUIRE(lines[0].length() == 0u);
}

TEST_CASE("Wrap: un texte finissant par un saut de ligne a une derniere ligne VIDE",
          "[text][unit][wrap]") {
    // Cohérent avec le comptage de lignes du modèle : "a\n" fait deux lignes, la seconde vide.
    REQUIRE(drawn("a\n", 100.0f) == std::vector<std::string>{"a", ""});
}

// ============================================================================
// Repli sur les espaces
// ============================================================================

TEST_CASE("Wrap: on coupe a la derniere espace qui tienne", "[text][unit][wrap]") {
    // "aaa bbb ccc" : chaque mot fait 30, chaque espace 10. Une boite de 80 tient "aaa bbb" (70)
    // mais pas "aaa bbb ccc".
    REQUIRE(drawn("aaa bbb ccc", 80.0f) == std::vector<std::string>{"aaa bbb", "ccc"});
}

TEST_CASE("Wrap: l'espace de coupe ne reapparait PAS en tete de la ligne suivante",
          "[text][unit][wrap]") {
    // L'alinea fantome classique : si l'espace repartait avec la ligne suivante, tout le texte
    // semblerait indente d'un cran une ligne sur deux.
    const auto lines = drawn("aa bb", 30.0f);
    REQUIRE(lines == std::vector<std::string>{"aa", "bb"});
    for (const std::string& l : lines) {
        REQUIRE_FALSE(l.empty());
        REQUIRE(l.front() != ' ');
    }
}

TEST_CASE("Wrap: plusieurs espaces consecutives sont sautees d'un bloc", "[text][unit][wrap]") {
    REQUIRE(drawn("aa    bb", 30.0f) == std::vector<std::string>{"aa", "bb"});
}

// ============================================================================
// Le cas dur : un mot plus large que la boîte
// ============================================================================

TEST_CASE("Wrap: un mot plus large que la boite est coupe au CARACTERE", "[text][unit][wrap]") {
    // Sans cette coupe forcee, le mot deborderait indefiniment et la mise en page serait fausse.
    REQUIRE(drawn("abcdefgh", 30.0f) == std::vector<std::string>{"abc", "def", "gh"});
}

TEST_CASE("Wrap: une largeur ABSURDE produit toujours au moins un caractere par ligne",
          "[text][unit][wrap]") {
    // La garantie de TOTALITE de l'algorithme : une boite de 2px (plus etroite qu'un glyphe) ne doit
    // pas boucler indefiniment ni rendre des lignes vides.
    const auto lines = drawn("abcd", 2.0f);
    REQUIRE(lines == std::vector<std::string>{"a", "b", "c", "d"});
}

TEST_CASE("Wrap: un mot long apres un mot court coupe au bon endroit", "[text][unit][wrap]") {
    // "ab " tient (30), puis "cdefgh" ne tient pas et n'a aucune opportunite -> coupe au caractere.
    REQUIRE(drawn("ab cdefgh", 30.0f) == std::vector<std::string>{"ab", "cde", "fgh"});
}

// ============================================================================
// UTF-8 : jamais de coupe au milieu d'un codepoint
// ============================================================================

TEST_CASE("Wrap: une coupe forcee tombe sur une frontiere de codepoint",
          "[text][unit][wrap][utf8]") {
    // "ééé" = 6 octets, 3 caracteres de 10px. Une boite de 20 tient 2 caracteres.
    const std::string text = kE + kE + kE;
    const auto lines = wrapText(std::string_view(text), 20.0f, tenPx);

    REQUIRE(lines.size() == 2u);
    for (const VisualLine& v : lines) {
        // Une frontiere qui tomberait entre 0xC3 et 0xA9 produirait un octet de continuation isole.
        if (v.begin < text.size()) REQUIRE_FALSE(grove::isUtf8Continuation(text[v.begin]));
        if (v.end < text.size())   REQUIRE_FALSE(grove::isUtf8Continuation(text[v.end]));
    }
    REQUIRE(text.substr(lines[0].begin, lines[0].length()) == kE + kE);
    REQUIRE(text.substr(lines[1].begin, lines[1].length()) == kE);
}

TEST_CASE("Wrap: un accent compte pour UN glyphe, pas pour deux octets",
          "[text][unit][wrap][utf8]") {
    // Compte en octets, "café" ferait 50 et ne tiendrait pas dans 40.
    const std::string text = "caf" + kE;
    REQUIRE(wrapText(std::string_view(text), 40.0f, tenPx).size() == 1u);
}

// ============================================================================
// Repérage : quelle ligne visuelle contient cet index ?
// ============================================================================

TEST_CASE("Wrap: visualLineAt situe un index dans les lignes repliees", "[text][unit][wrap]") {
    const std::string text = "aaa bbb ccc";
    const auto lines = wrapText(std::string_view(text), 80.0f, tenPx);  // {"aaa bbb", "ccc"}
    REQUIRE(lines.size() == 2u);

    REQUIRE(visualLineAt(lines, 0) == 0u);
    REQUIRE(visualLineAt(lines, 6) == 0u);
    REQUIRE(visualLineAt(lines, 8) == 1u);    // debut de "ccc"
    REQUIRE(visualLineAt(lines, 11) == 1u);   // fin du texte
    REQUIRE(visualLineAt(lines, 999) == 1u);  // hors bornes : sature
}

TEST_CASE("Wrap: un index pose SUR la coupe appartient a la ligne SUIVANTE", "[text][unit][wrap]") {
    // C'est ce qui place le curseur au debut de la ligne d'arrivee apres un repli, plutot qu'en bout
    // de la precedente -- la difference se voit immediatement a l'usage.
    const std::string text = "aaa bbb";
    const auto lines = wrapText(std::string_view(text), 30.0f, tenPx);  // {"aaa", "bbb"}
    REQUIRE(lines.size() == 2u);
    REQUIRE(lines[1].begin == 4u);
    REQUIRE(visualLineAt(lines, 4) == 1u);
}

// ============================================================================
// Combinaison : coupures dures ET repli
// ============================================================================

TEST_CASE("Wrap: coupures dures et repli se combinent", "[text][unit][wrap]") {
    REQUIRE(drawn("aaa bbb\nccc ddd", 30.0f)
            == std::vector<std::string>{"aaa", "bbb", "ccc", "ddd"});
}

TEST_CASE("Wrap: `next` permet de reconstituer le texte sans perte", "[text][unit][wrap]") {
    // Invariant structurel : en enchainant les `next`, on parcourt tout le texte une seule fois.
    // S'il etait faux, du texte disparaitrait a l'affichage ou serait dessine deux fois.
    const std::string text = "aaa bbb\nccc";
    const auto lines = wrapText(std::string_view(text), 30.0f, tenPx);
    size_t pos = 0;
    for (const VisualLine& v : lines) {
        REQUIRE(v.begin == pos);
        REQUIRE(v.end >= v.begin);
        REQUIRE(v.next >= v.end);
        pos = v.next;
    }
    REQUIRE(pos == text.size());
}
