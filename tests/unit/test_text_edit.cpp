/**
 * Unit Tests: grove::text::EditModel — le modèle d'édition partagé, PUR (ni police, ni widget, ni GPU).
 *
 * QUOI     : tampon, curseur, sélection, insertion/suppression, et navigation par LIGNES.
 *
 * POURQUOI : `UITextInput` et `UITextArea` doivent partager exactement cette logique. Deux
 *            implémentations divergeraient — et c'est précisément ce qui a produit les défauts déjà
 *            rencontrés dans ce chantier (la suppression recassait l'UTF-8 que l'insertion gérait ;
 *            deleteCharBefore laissait l'ancre derrière et fabriquait une sélection fantôme). Testé
 *            ici une fois, les deux vues en héritent.
 *
 * COMMENT  : aucune dépendance — on manipule le modèle directement et on assène le texte résultant.
 *            Les cas UTF-8 utilisent des octets explicites ("\xC3\xA9") : ils portent sur des octets,
 *            ils ne doivent dépendre ni de l'encodage du fichier ni du compilateur.
 */

#include <catch2/catch_test_macros.hpp>

#include <grove/text/TextEdit.h>

#include <string>

using namespace grove::text;

namespace {
const std::string kE = "\xC3\xA9";  // "é" : 2 octets, 1 caractère
}

// ============================================================================
// Insertion / suppression
// ============================================================================

TEST_CASE("EditModel: insertion au curseur", "[text][unit][edit]") {
    EditModel m;
    REQUIRE(m.insert("abc"));
    REQUIRE(m.text() == "abc");
    REQUIRE(m.cursor() == 3);

    m.setCursor(1);
    REQUIRE(m.insert("XY"));
    REQUIRE(m.text() == "aXYbc");
    REQUIRE(m.cursor() == 3);
}

TEST_CASE("EditModel: maxLength refuse le debordement", "[text][unit][edit]") {
    EditModel m;
    m.maxLength = 4;
    REQUIRE(m.insert("abcd"));
    REQUIRE_FALSE(m.insert("e"));
    REQUIRE(m.text() == "abcd");
}

TEST_CASE("EditModel: remplacer une selection dans un champ PLEIN reste possible",
          "[text][unit][edit]") {
    // L'operation ne rallonge pas le texte : la refuser serait absurde. C'est pour cela que la
    // suppression de la selection precede la verification de maxLength.
    EditModel m;
    m.maxLength = 4;
    m.insert("abcd");
    m.setCursor(0);
    m.setCursor(2, /*extend=*/true);   // "ab" selectionne
    REQUIRE(m.insert("XY"));
    REQUIRE(m.text() == "XYcd");
}

TEST_CASE("EditModel: insert() signale le changement par le TEXTE, pas par la longueur",
          "[text][unit][edit]") {
    // Remplacer "abc" par "ZZZ" ne change pas la longueur. Un appelant qui se fierait a la longueur
    // n'emettrait jamais son evenement de changement -- le champ serait correct et le jeu ne le
    // saurait pas. C'est un vrai defaut rencontre sur le chemin du collage.
    EditModel m;
    m.insert("abcdef");
    m.setCursor(0);
    m.setCursor(3, /*extend=*/true);
    REQUIRE(m.insert("ZZZ"));          // <- doit etre TRUE malgre la longueur inchangee
    REQUIRE(m.text() == "ZZZdef");
}

TEST_CASE("EditModel: Backspace et Suppr enjambent un caractere ENTIER",
          "[text][unit][edit][utf8]") {
    EditModel m;
    m.insert("a" + kE + "b");   // "aéb"
    REQUIRE(m.text().size() == 4u);

    m.setCursor(3);             // apres 'é'
    REQUIRE(m.deleteBefore());
    REQUIRE(m.text() == "ab");  // les DEUX octets partent ensemble

    m.setCursor(0);
    m.insert(kE);               // "éab"
    m.setCursor(0);
    REQUIRE(m.deleteAfter());
    REQUIRE(m.text() == "ab");
}

TEST_CASE("EditModel: une suppression ne laisse pas d'ancre orpheline", "[text][unit][edit]") {
    // Le bug reel attrape par le garde-fou ASCII : deplacer le curseur en laissant l'ancre derriere
    // fabrique une selection fantome, et la fleche suivante se contente de la replier au lieu de
    // bouger. Toute mutation doit retablir l'invariant.
    EditModel m;
    m.insert("AB");
    REQUIRE(m.deleteBefore());
    REQUIRE(m.text() == "A");
    REQUIRE_FALSE(m.hasSelection());   // <- l'ancre a suivi
}

// ============================================================================
// Sélection
// ============================================================================

TEST_CASE("EditModel: l'ancre est le bord FIXE, le curseur le bord mobile", "[text][unit][edit]") {
    EditModel m;
    m.insert("abcdef");
    m.setCursor(2);
    m.setCursor(4, /*extend=*/true);
    REQUIRE(m.hasSelection());
    REQUIRE(m.selectedText() == "cd");

    // Etendre vers l'ARRIERE au-dela de l'ancre : les bornes se reordonnent toutes seules.
    m.setCursor(0, /*extend=*/true);
    REQUIRE(m.selectedText() == "ab");
}

TEST_CASE("EditModel: une fleche NUE replie la selection sur le bord vise", "[text][unit][edit]") {
    EditModel m;
    m.insert("abcdef");
    m.setCursor(1);
    m.setCursor(4, /*extend=*/true);   // "bcd" selectionne, curseur en 4

    m.moveCursor(-1);                   // gauche NUE -> repli sur le bord GAUCHE (1), pas 3
    REQUIRE_FALSE(m.hasSelection());
    REQUIRE(m.cursor() == 1);

    m.setCursor(1);
    m.setCursor(4, /*extend=*/true);
    m.moveCursor(1);                    // droite NUE -> repli sur le bord DROIT (4)
    REQUIRE(m.cursor() == 4);
}

TEST_CASE("EditModel: taper REMPLACE la selection", "[text][unit][edit]") {
    EditModel m;
    m.insert("abcdef");
    m.setCursor(0);
    m.setCursor(3, /*extend=*/true);
    m.insert("X");
    REQUIRE(m.text() == "Xdef");
    REQUIRE_FALSE(m.hasSelection());
}

TEST_CASE("EditModel: selectWordAt prend le mot entier, accents compris",
          "[text][unit][edit][utf8]") {
    EditModel m;
    m.setText("le caf" + kE + " chaud");
    m.selectWordAt(4);
    REQUIRE(m.selectedText() == "caf" + kE);
}

// ============================================================================
// L'invariant central : jamais d'index au milieu d'un codepoint
// ============================================================================

TEST_CASE("EditModel: setCursor recolle TOUTE position sur une frontiere de codepoint",
          "[text][unit][edit][utf8]") {
    EditModel m;
    m.setText("a" + kE + "b");   // octets : a(0) é(1,2) b(3)
    for (int i = -5; i <= 10; ++i) {
        m.setCursor(i);
        INFO("setCursor(" << i << ") -> " << m.cursor());
        REQUIRE(m.cursor() != 2);                            // jamais entre les deux octets de 'é'
        REQUIRE(m.cursor() >= 0);
        REQUIRE(m.cursor() <= static_cast<int>(m.text().size()));
    }
}

// ============================================================================
// Lignes (le socle du multiligne)
// ============================================================================

TEST_CASE("EditModel: comptage et reperage des lignes", "[text][unit][edit][lines]") {
    EditModel m;
    m.setText("un\ndeux\ntrois");

    REQUIRE(m.lineCount() == 3);
    REQUIRE(m.lineNumberAt(0) == 0);
    REQUIRE(m.lineNumberAt(3) == 1);    // premier octet de "deux"
    REQUIRE(m.lineNumberAt(8) == 2);

    REQUIRE(m.lineStart(5) == 3u);      // debut de "deux"
    REQUIRE(m.lineEnd(5) == 7u);        // le '\n' qui la termine
    REQUIRE(m.startOfLine(2) == 8u);    // debut de "trois"
}

TEST_CASE("EditModel: un texte vide compte UNE ligne", "[text][unit][edit][lines]") {
    EditModel m;
    REQUIRE(m.lineCount() == 1);
    REQUIRE(m.lineStart(0) == 0u);
    REQUIRE(m.lineEnd(0) == 0u);
}

TEST_CASE("EditModel: monter et descendre conservent la COLONNE", "[text][unit][edit][lines]") {
    EditModel m;
    m.setText("abcdef\nghijkl");
    m.setCursor(3);                 // colonne 3 de la ligne 0

    m.moveCursorByLine(1);
    REQUIRE(m.cursor() == 10);      // 7 (debut ligne 1) + 3
    REQUIRE(m.lineNumberAt(m.cursor()) == 1);

    m.moveCursorByLine(-1);
    REQUIRE(m.cursor() == 3);       // retour a la colonne 3 de la ligne 0
}

TEST_CASE("EditModel: descendre vers une ligne PLUS COURTE sature en bout de ligne",
          "[text][unit][edit][lines]") {
    EditModel m;
    m.setText("abcdefgh\nxy");
    m.setCursor(7);                 // colonne 7
    m.moveCursorByLine(1);
    REQUIRE(m.cursor() == 11);      // fin de "xy" (9 + 2), pas au-dela
    REQUIRE(m.lineNumberAt(m.cursor()) == 1);
}

TEST_CASE("EditModel: la colonne se compte en CARACTERES, pas en octets",
          "[text][unit][edit][lines][utf8]") {
    // Ligne 0 accentuee : comptee en octets, la colonne deriverait d'une ligne a l'autre et pourrait
    // viser le milieu d'un codepoint.
    EditModel m;
    m.setText(kE + kE + std::string("X\nabcd"));  // ligne 0 = "ééX" (5 octets, 3 caracteres)
    m.setCursor(4);                                // apres le 2e 'é' -> colonne 2
    m.moveCursorByLine(1);
    // Ligne 1 commence a l'octet 6 ; colonne 2 -> octet 8.
    REQUIRE(m.cursor() == 8);
    REQUIRE(m.lineNumberAt(m.cursor()) == 1);
}

TEST_CASE("EditModel: monter depuis la premiere ligne va au DEBUT du texte",
          "[text][unit][edit][lines]") {
    // Le geste attendu quand il n'y a plus de ligne au-dessus.
    EditModel m;
    m.setText("abcdef\nghi");
    m.setCursor(3);
    m.moveCursorByLine(-1);
    REQUIRE(m.cursor() == 0);
}

TEST_CASE("EditModel: descendre depuis la derniere ligne va a la FIN du texte",
          "[text][unit][edit][lines]") {
    EditModel m;
    m.setText("abc\ndef");
    m.setCursor(5);
    m.moveCursorByLine(1);
    REQUIRE(m.cursor() == 7);
}

TEST_CASE("EditModel: Debut/Fin agissent sur la LIGNE, pas sur le texte",
          "[text][unit][edit][lines]") {
    EditModel m;
    m.setText("abc\ndefgh");
    m.setCursor(6);          // dans "defgh"

    m.moveToLineStart();
    REQUIRE(m.cursor() == 4);
    m.moveToLineEnd();
    REQUIRE(m.cursor() == 9);

    // Les variantes « tout le texte » restent disponibles pour la vue monoligne.
    m.moveToTextStart();
    REQUIRE(m.cursor() == 0);
    m.moveToTextEnd();
    REQUIRE(m.cursor() == 9);
}

TEST_CASE("EditModel: selectionner en montant/descendant etend depuis l'ancre",
          "[text][unit][edit][lines]") {
    EditModel m;
    m.setText("abcd\nefgh");
    m.setCursor(2);
    m.moveCursorByLine(1, /*extend=*/true);
    REQUIRE(m.hasSelection());
    REQUIRE(m.selectedText() == "cd\nef");
}

TEST_CASE("EditModel: un saut de ligne s'insere comme n'importe quel texte",
          "[text][unit][edit][lines]") {
    EditModel m;
    m.insert("abcd");
    m.setCursor(2);
    REQUIRE(m.insert("\n"));
    REQUIRE(m.text() == "ab\ncd");
    REQUIRE(m.lineCount() == 2);
    REQUIRE(m.cursor() == 3);
}
