#!/usr/bin/env bash
# ============================================================================
# remote-build.sh — ferme de build déportée, multi-projets, avec file d'attente
# ----------------------------------------------------------------------------
# QUOI     : synchronise un dépôt (et ses dépendances voisines) vers le serveur de
#            build, y lance cmake + ninja (+ ctest), et rapatrie les artefacts.
#            Sait produire du natif Linux OU du Windows par cross-compilation.
#
# POURQUOI : la compilation locale sature thermiquement le poste. Mesuré, le
#            serveur n'est PAS un compromis : 167 s contre 200 s en local sur la
#            même config, alors qu'il a 4 cœurs contre 16 et tourne en nice 19.
#            Windows/MinGW compile bien moins vite que Linux/g++.
#            Chiffres et méthode : docs/design/build-speed.md.
#
#            ⚠️ DÉPORT, pas compilation DISTRIBUÉE. distcc enverrait ~5 Go de TU
#            préprocessées par build (8,2 Mo mesurés pour UNE TU) ; déporter envoie
#            l'arbre versionné — 4,5 Mo. Un aller-retour au lieu de milliers.
#            Ne pas « améliorer » ce script en distribuant.
#
# COMMENT  : 1. `git ls-files` → tar → ssh. Seuls les fichiers VERSIONNÉS partent,
#               d'où la charge utile minuscule et l'exclusion automatique de build/.
#               ⚠️ COROLLAIRE : un fichier NEUF non `git add`é n'est PAS envoyé.
#               C'est la première erreur qu'on fait avec cet outil.
#            2. côté serveur, extraction dans un dossier d'étape puis
#               `rsync -a --delete`. Le rsync propage les SUPPRESSIONS — une simple
#               extraction tar laisserait vivre les fichiers effacés, donc des
#               cibles fantômes.
#            3. tous les dépôts atterrissent côte à côte dans ~/grovefarm/<nom>,
#               ce qui fait résoudre les `add_subdirectory(../groveengine)` des
#               jeux (drifterra, DAOS, fractax) sans toucher à leurs dépôts.
#            4. un `flock` sérialise les builds : le serveur n'a que 8 threads et
#               héberge la PROD (ai.etheryale.com). Deux builds concurrents se
#               voleraient le CPU et gêneraient le service — d'où la file.
#            5. les dossiers de build distants sont CONSERVÉS entre deux appels :
#               les suivants sont incrémentaux, et ccache couvre le reste.
#
# USAGE    : tools/remote-build.sh [options] [-- <args cmake supplémentaires>]
#   --repo PATH     dépôt à construire (défaut : le dépôt courant)
#   --with PATH     dépôt voisin à synchroniser aussi (répétable) — nécessaire
#                   pour les jeux, qui font add_subdirectory(../groveengine)
#   --mingw         cross-compiler pour WINDOWS (sinon : natif Linux)
#   --test          lancer ctest après le build
#   --fetch DIR     rapatrier les exécutables produits + le runtime dans DIR
#   --clean         repartir d'un dossier de build vide
#   -j N            parallélisme distant (défaut 6 sur 8 threads : on laisse
#                   respirer la prod)
#
# EXEMPLES :
#   tools/remote-build.sh --mingw --test
#   tools/remote-build.sh --repo ../DAOS --with . --mingw --fetch /tmp/daos-win
# ============================================================================

set -euo pipefail

HOST="${GROVE_REMOTE_HOST:-debian@142.44.139.223}"
FARM="${GROVE_REMOTE_FARM:-grovefarm}"     # relatif au $HOME distant
JOBS="${GROVE_REMOTE_JOBS:-6}"
NICE="${GROVE_REMOTE_NICE:-19}"
LOCK_WAIT="${GROVE_REMOTE_LOCK_WAIT:-3600}"

# Capacités de la machine de build, exprimées comme les labels ctest qu'elle NE sait
# PAS faire tourner. C'est un réglage, pas une vérité gravée : le jour où la ferme a
# un GPU et un écran, on retire `gpu` d'ici (ou on passe --gpu) et rien d'autre ne
# bouge — ni le workflow CI, ni la doc, ni les appelants.
# Voir tests/CMakeLists.txt § « Labels de CAPACITÉ » pour la signification de chacun.
EXCLUDE_LABELS="${GROVE_REMOTE_EXCLUDE_LABELS:-gpu|platform-windows|timing-sensitive|known-fail-linux}"

REPO="."
WITH=()
MINGW=0
RUN_TESTS=0
CLEAN=0
FETCH=""
CMAKE_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo)  REPO="$2"; shift 2 ;;
        --with)  WITH+=("$2"); shift 2 ;;
        --mingw) MINGW=1; shift ;;
        # La ferme sait faire du GPU : on cesse d'écarter les tests qui en exigent.
        --gpu)   EXCLUDE_LABELS="$(echo "$EXCLUDE_LABELS" | sed -E 's/(^gpu\||\|gpu$|^gpu$)//')"; shift ;;
        --test)  RUN_TESTS=1; shift ;;
        --fetch) FETCH="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -j)      JOBS="$2"; shift 2 ;;
        --)      shift; CMAKE_ARGS=("$@"); break ;;
        *) echo "option inconnue : $1" >&2; exit 2 ;;
    esac
done

# --- Résolution des dépôts -------------------------------------------------
# Chaque chemin donné est ramené à la racine de SON dépôt git : c'est cette racine
# qui donne à la fois le nom du dossier distant et le périmètre de `git ls-files`.
repo_root() { git -C "$1" rev-parse --show-toplevel; }

# ⚠️ Le NOM distant vient du dépôt PRINCIPAL, pas du répertoire courant. Dans un git
# worktree, --show-toplevel renvoie le chemin du worktree : on obtiendrait
# « ui-repeater-p1 » au lieu de « groveengine », la ferme créerait un dossier par
# branche, et surtout les add_subdirectory(../groveengine) des jeux ne résoudraient
# plus. --git-common-dir pointe toujours le .git du dépôt principal, dont le parent
# porte le nom canonique. Le CONTENU envoyé reste bien celui du worktree courant.
repo_name() {
    local common
    common="$(git -C "$1" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)" || common=""
    if [[ -n "$common" ]]; then
        basename "$(dirname "$common")"
    else
        basename "$(repo_root "$1")"          # git ancien : on retombe sur l'ancien comportement
    fi
}

MAIN_ROOT="$(repo_root "$REPO")"
MAIN_NAME="$(repo_name "$REPO")"

ALL_ROOTS=("$MAIN_ROOT")
ALL_NAMES=("$MAIN_NAME")
for w in "${WITH[@]:-}"; do
    [[ -z "$w" ]] && continue
    ALL_ROOTS+=("$(repo_root "$w")")
    ALL_NAMES+=("$(repo_name "$w")")
done

echo "==> ferme : $HOST:~/$FARM   (-j$JOBS, nice $NICE)"
echo "==> cible : $MAIN_NAME$([[ $MINGW -eq 1 ]] && echo '  [cross mingw → Windows]' || echo '  [natif Linux]')"

# --- 1. Synchronisation ----------------------------------------------------
# ⚠️ Piège Windows → Linux : la CASSE. Le poste a un système de fichiers insensible
# à la casse, le serveur non. Un fichier commité sous un nom qui ne correspond pas à
# celui du disque (constaté sur DAOS : suivi `cmakelists.txt`, présent
# `CMakeLists.txt`) marche localement depuis toujours et casse net une fois déporté,
# avec un message qui n'évoque pas la casse ("does not appear to contain
# CMakeLists.txt"). On le signale AVANT d'envoyer, sinon le diagnostic coûte cher.
check_case() {
    local root="$1" name="$2" bad=0
    while IFS= read -r tracked; do
        [[ -e "$root/$tracked" ]] || continue
        # Nom réel sur le disque, comparé au nom suivi par git.
        local dir base real
        dir="$(dirname "$root/$tracked")"; base="$(basename "$tracked")"
        real="$(ls -1 "$dir" 2>/dev/null | grep -ixF "$base" | head -1)" || true
        if [[ -n "$real" && "$real" != "$base" ]]; then
            echo "    ⚠️  casse divergente : git suit '$tracked' mais le disque a '$real'" >&2
            bad=1
        fi
    done < <(git -C "$root" ls-files -- '*.txt' '*.cmake' 2>/dev/null | head -200)
    [[ $bad -eq 1 ]] && echo "    → à corriger dans $name (git mv), sinon le build distant échouera" >&2
    return 0
}

for i in "${!ALL_ROOTS[@]}"; do
    root="${ALL_ROOTS[$i]}"
    name="${ALL_NAMES[$i]}"
    echo "==> sync $name"
    check_case "$root" "$name"
    # -c (suivis) ET -o --exclude-standard (neufs, non ignorés).
    #
    # ⚠️ Le -o n'est PAS un confort, c'est une question de correction. Avec les seuls
    # fichiers suivis, un test rouge fraîchement écrit et pas encore `git add`é ne
    # partait pas : la ferme compilait sans lui et répondait « vert ». Un faux vert
    # sur un cycle TDD est pire que pas de ferme du tout.
    # --exclude-standard applique .gitignore, donc build/, _deps/ et deps/ restent
    # exclus — la charge utile ne gonfle pas.
    # --ignore-failed-read : un fichier suivi mais supprimé localement est listé par
    # git et absent du disque ; sans ça tar avorterait. Sa suppression est de toute
    # façon propagée par le rsync --delete ci-dessous.
    ( cd "$root" && git ls-files -z -c -o --exclude-standard \
        | tar --null -czf - --ignore-failed-read -T - 2>/dev/null ) | ssh "$HOST" "
        set -e
        rm -rf ~/$FARM/.stage && mkdir -p ~/$FARM/.stage ~/$FARM/$name
        tar xzf - -C ~/$FARM/.stage
        # --exclude 'build*' protège les dossiers de build distants, qui n'ont pas
        # d'équivalent côté source et seraient sinon effacés à chaque appel.
        rsync -a --delete --exclude 'build*' ~/$FARM/.stage/ ~/$FARM/$name/
        rm -rf ~/$FARM/.stage
    "
done

# --- 2. Build (sous verrou) ------------------------------------------------
if [[ $MINGW -eq 1 ]]; then
    BUILD_DIR="build-mingw"
    # Le fichier de toolchain vit dans groveengine ; les jeux le référencent donc
    # via leur voisin, exactement comme leur add_subdirectory.
    if [[ "$MAIN_NAME" == "groveengine" ]]; then
        TC="cmake/toolchains/mingw-w64-linux.cmake"
    else
        TC="../groveengine/cmake/toolchains/mingw-w64-linux.cmake"
    fi
    TC_ARG="-DCMAKE_TOOLCHAIN_FILE=$TC"
else
    BUILD_DIR="build"
    TC_ARG=""
fi

CLEAN_CMD=""
[[ $CLEAN -eq 1 ]] && CLEAN_CMD="rm -rf ~/$FARM/$MAIN_NAME/$BUILD_DIR;"

TEST_CMD=""
if [[ $RUN_TESTS -eq 1 ]]; then
    if [[ $MINGW -eq 1 ]]; then
        # Les binaires sont des PE Windows : ils ne s'exécutent pas sur le serveur.
        # On ne prétend donc PAS les tester — utiliser --fetch et lancer localement.
        TEST_CMD="echo '==> ctest ignoré : les binaires mingw ne tournent pas sur le serveur (voir --fetch)'"
    else
        # ⚠️ ctest en SÉRIE : plusieurs tests portent leur propre chien de garde et
        # débordent sous charge parallèle (MemoryLeakHunter passe en 161 s contre un
        # plafond de 180). En parallèle on récolterait de faux rouges.
        TEST_CMD="
            echo '==> ctest (série)'
            cd ~/$FARM/$MAIN_NAME/$BUILD_DIR
            T0=\$(date +%s)
            # -LE : on écarte par CAPACITÉ (label), pas par nom. Le label dit POURQUOI
            # le test est écarté, et le jour où la ferme a un GPU/écran il suffit de
            # retirer 'gpu' de EXCLUDE_LABELS (ou de passer --gpu).
            nice -n $NICE ctest --output-on-failure -LE '$EXCLUDE_LABELS' > /tmp/grove-ctest-$MAIN_NAME.log 2>&1 || true
            echo \"    exclus par capacité : $EXCLUDE_LABELS\"
            echo \"    \$((\$(date +%s) - T0)) s\"
            grep -E 'tests passed|tests failed out of' /tmp/grove-ctest-$MAIN_NAME.log || true
            grep -E '^\s+[0-9]+ - ' /tmp/grove-ctest-$MAIN_NAME.log | grep -iE 'Failed|Timeout' || true
        "
    fi
fi

# flock -w : on ATTEND son tour plutôt que d'échouer. Le message n'apparaît que si
# le verrou n'est pas libre immédiatement, pour ne pas bruiter le cas courant.
ssh "$HOST" "
    set -e
    mkdir -p ~/$FARM
    exec 9>~/$FARM/.build.lock
    if ! flock -n 9; then
        echo '==> un autre build occupe la ferme, mise en file…'
        flock -w $LOCK_WAIT 9 || { echo 'abandon : file trop longue'; exit 1; }
    fi

    $CLEAN_CMD
    cd ~/$FARM/$MAIN_NAME

    echo '==> configure'
    nice -n $NICE cmake -B $BUILD_DIR -G Ninja $TC_ARG -DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS[*]:-} \
        > /tmp/grove-cfg-$MAIN_NAME.log 2>&1 || {
        echo 'CONFIGURE ÉCHOUÉ :'; tail -25 /tmp/grove-cfg-$MAIN_NAME.log; exit 1; }

    echo '==> build'
    T0=\$(date +%s)
    if nice -n $NICE ninja -C $BUILD_DIR -j$JOBS > /tmp/grove-bld-$MAIN_NAME.log 2>&1; then
        echo \"    \$((\$(date +%s) - T0)) s — OK\"
    else
        echo \"    \$((\$(date +%s) - T0)) s — ÉCHEC\"
        grep -B6 -iE 'error:' /tmp/grove-bld-$MAIN_NAME.log | head -40
        exit 1
    fi
    $TEST_CMD
"

# --- 3. Rapatriement -------------------------------------------------------
# Les DLL de runtime accompagnent OBLIGATOIREMENT les exécutables : le serveur croise
# en GCC 14 alors que le poste a MinGW GCC 15, donc les DLL locales ne conviennent
# pas. On prend la variante **posix** — la win32 n'a ni std::thread ni std::mutex.
if [[ -n "$FETCH" ]]; then
    echo "==> rapatriement vers $FETCH"
    mkdir -p "$FETCH"
    ssh "$HOST" "
        cd ~/$FARM/$MAIN_NAME/$BUILD_DIR
        tar czf - \$(find . -name '*.exe' -o -name '*.dll' | head -400) 2>/dev/null
    " | tar xzf - -C "$FETCH" 2>/dev/null || true

    if [[ $MINGW -eq 1 ]]; then
        for d in libstdc++-6.dll libgcc_s_seh-1.dll; do
            ssh "$HOST" "cat /usr/lib/gcc/x86_64-w64-mingw32/14-posix/$d" > "$FETCH/$d"
        done
        ssh "$HOST" "cat /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll" > "$FETCH/libwinpthread-1.dll"
    fi
    echo "    $(find "$FETCH" -name '*.exe' | wc -l) exécutables, $(find "$FETCH" -name '*.dll' | wc -l) DLL"
fi
