// Unique source de la cible fournisseur du PCH partagé.
// CMake exige qu'une bibliothèque ait au moins une source ; c'est la compilation de
// CETTE TU qui produit le .gch que les ~166 cibles de test réutilisent (REUSE_FROM).
// Volontairement vide de logique : rien à exécuter, seulement à précompiler.
#include "grove_test_pch.h"
