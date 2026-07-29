#pragma once

// QUOI : en-tête de REDIRECTION — le décodeur UTF-8 vit désormais dans include/grove/text/Utf8.h.
// POURQUOI : la mesure de texte de l'UI (grove::text::Metrics) en a besoin, et l'UIModule ne dépend
//   pas du renderer. Plutôt que d'en dupliquer une seconde copie — deux décodeurs UTF-8 finissent
//   toujours par diverger — le fichier a été remonté dans include/, partagé par tout le moteur.
// COMMENT : ce chemin reste valide pour les appelants existants (TextPass, BitmapFont, TextFit) ;
//   le symbole ne bouge pas (grove::decodeUtf8), donc rien à changer chez eux.

#include <grove/text/Utf8.h>
