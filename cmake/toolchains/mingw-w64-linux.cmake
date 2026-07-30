# ============================================================================
# mingw-w64-linux.cmake — cross-compilation Linux → Windows x86_64
# ----------------------------------------------------------------------------
# QUOI     : fichier de toolchain CMake pour produire des binaires WINDOWS depuis
#            une machine Linux, avec mingw-w64.
#
# POURQUOI : le serveur de build est sous Linux, mais les binaires doivent tourner
#            sur le poste Windows. Compiler NATIVEMENT sous Linux donnerait des ELF
#            inexécutables sur Windows — et exigerait en plus que tout le parc
#            compile sous Linux, ce que la dette « port Linux » (parkée) empêche.
#            Cross-compiler contourne les deux : c'est la MÊME famille de toolchain
#            que le MinGW du poste, donc le même code, la même cible.
#
# COMMENT  : cmake -B build --toolchain cmake/toolchains/mingw-w64-linux.cmake
#            Prérequis serveur : apt install g++-mingw-w64-x86-64
#
# ⚠️ PIÈGE — LA VARIANTE DE THREADING. Debian installe TROIS binaires :
#            -g++, -g++-win32 et -g++-posix, et le défaut (-g++) pointe sur
#            **win32**, dont la libstdc++ n'implémente NI std::thread NI
#            std::mutex : le moteur, qui en vit, ne compile tout simplement pas.
#            On force donc explicitement les variantes `-posix`, qui correspondent
#            aussi au MinGW du poste (x86_64-**posix**-seh-rev0). Ne pas
#            "simplifier" en retirant les suffixes.
#
# ⚠️ VERSIONS. Le cross de Debian est en GCC 14, le MinGW du poste en GCC 15. Sans
#            conséquence sur la correction — un déport compile TOUS les objets avec
#            le même compilateur, contrairement à une compilation distribuée qui
#            les mélangerait — mais les binaires produits ici dépendent des DLL de
#            runtime de GCC 14. Il faut donc rapatrier celles du serveur avec les
#            exécutables, pas réutiliser celles du poste.
# ============================================================================

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(GROVE_MINGW_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${GROVE_MINGW_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${GROVE_MINGW_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER  ${GROVE_MINGW_PREFIX}-windres)

# Racine de recherche : on ne veut trouver QUE les bibliothèques et en-têtes de la
# cible Windows. Sans ça, CMake attraperait joyeusement les .so et les en-têtes
# Linux de l'hôte et produirait des erreurs de link incompréhensibles.
set(CMAKE_FIND_ROOT_PATH /usr/${GROVE_MINGW_PREFIX})

# PROGRAM = NEVER : les OUTILS (cmake, ninja, git…) doivent venir de l'hôte Linux.
# Les trois autres = ONLY : bibliothèques, en-têtes et paquets viennent de la cible.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
