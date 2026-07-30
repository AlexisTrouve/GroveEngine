// ============================================================================
// grove_test_pch.h — en-tête précompilé partagé par la suite de tests
// ----------------------------------------------------------------------------
// QUOI     : regroupe les en-têtes lourds inclus par la quasi-totalité des tests,
//            pour qu'ils soient parsés UNE fois au lieu d'une fois par TU.
//
// POURQUOI : mesuré sur ce dépôt — les tests pèsent 5 212 s CPU, soit 81 % du coût
//            de compilation du code projet, et ~62 % du coût d'une TU de test est
//            le simple parsing de ses en-têtes (Catch2 + nlohmann + spdlog sont
//            énormes et tirés partout). Un PCH partagé récupère ~1/3 de ce coût.
//            C'est le pendant de ccache : ccache supprime le coût des recompilations
//            IDENTIQUES, le PCH réduit celui des compilations NEUVES (un miss).
//
// COMMENT  : chaque inclusion est gardée par __has_include. C'est délibéré et non
//            décoratif : le PCH est partagé (REUSE_FROM) par ~166 cibles de test aux
//            jeux de dépendances différents, et une cible qui ne linke pas spdlog
//            n'a pas ses répertoires d'include. Sans la garde, une seule cible mal
//            appariée casserait le build entier ; avec, elle compile simplement sans
//            cet en-tête. La garde ne peut PAS produire un PCH incohérent : ce
//            fichier est compilé une seule fois, par la cible fournisseur, avec les
//            includes de celle-ci — les gardes y sont donc toutes résolues de la
//            même façon pour tout le monde.
//
// RÈGLE    : n'ajouter ici QUE des en-têtes stables et largement partagés. Un en-tête
//            du moteur en cours d'évolution y invaliderait le PCH — donc toute la
//            suite — à chaque édition. Rien de spécifique à un module (SDL, bgfx,
//            RHI/*) : ces cibles ont d'autres défines et GCC rejetterait le .gch.
//
// ⚠️ PIÈGE  : un PCH rejeté n'est PAS un simple manque à gagner. CMake force son
//            inclusion en ligne de commande ; quand GCC juge le .gch inutilisable, il
//            retombe sur le TEXTE de cet en-tête, qui doit alors compiler dans
//            l'environnement de la cible. Les gardes __has_include ci-dessous ne
//            protègent que les inclusions DIRECTES : grove/JsonDataNode.h tire
//            nlohmann/json.hpp transitivement, et une cible sans ce répertoire
//            d'include échoue en dur. C'est pourquoi tests/CMakeLists.txt n'attache ce
//            PCH qu'aux cibles liant Catch2 ET le moteur — ne pas relâcher ce filtre.
// ============================================================================

#ifndef GROVE_TEST_PCH_H
#define GROVE_TEST_PCH_H

// --- Bibliothèque standard : bon marché à l'unité, mais tirée par ~toutes les TU ---
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// --- Catch2 : présent dans 171 TU de test, et de loin le plus lourd du lot ---
#if defined(__has_include)
#  if __has_include(<catch2/catch_test_macros.hpp>)
#    include <catch2/catch_test_macros.hpp>
#  endif
#endif

// --- nlohmann/json : tiré transitivement par presque tout le moteur via IDataNode ---
#if defined(__has_include)
#  if __has_include(<nlohmann/json.hpp>)
#    include <nlohmann/json.hpp>
#  endif
#endif

// --- spdlog : lourd (embarque fmt), inclus directement par 44 TU ---
#if defined(__has_include)
#  if __has_include(<spdlog/spdlog.h>)
#    include <spdlog/spdlog.h>
#  endif
#endif

// --- Coeur GroveEngine : le trio inclus par la majorité des tests d'intégration ---
#if defined(__has_include)
#  if __has_include(<grove/JsonDataNode.h>)
#    include <grove/JsonDataNode.h>
#  endif
#  if __has_include(<grove/IntraIO.h>)
#    include <grove/IntraIO.h>
#  endif
#  if __has_include(<grove/IntraIOManager.h>)
#    include <grove/IntraIOManager.h>
#  endif
#endif

#endif  // GROVE_TEST_PCH_H
