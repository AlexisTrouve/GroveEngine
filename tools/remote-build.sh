#!/usr/bin/env bash
# ============================================================================
# remote-build.sh — déporte le build (et les tests) sur une machine distante
# ----------------------------------------------------------------------------
# QUOI     : synchronise les sources versionnées vers un serveur, y lance
#            cmake + ninja (+ ctest si demandé), et rapatrie le verdict.
#
# POURQUOI : la compilation locale sature thermiquement le poste de travail. Ce
#            script sort 100 % de cette charge de la machine — et, mesuré, ne la
#            paie PAS en lenteur : VPS142 (4c/8t, nice -n 19, -j6) fait le build
#            complet depuis zéro en 167 s contre 200 s en local sur 16 cœurs.
#            Windows/MinGW compile nettement moins vite que Linux/g++ à cœurs
#            comparables. Chiffres et méthode : docs/design/build-speed.md.
#
#            ⚠️ C'est un DÉPORT, pas une compilation DISTRIBUÉE. La différence
#            n'est pas cosmétique : distcc enverrait ~5 Go de TU préprocessées
#            par build (8,2 Mo mesurés pour une seule TU de test), là où déporter
#            envoie l'arbre de sources — 4,5 Mo, 5 secondes. Un aller-retour au
#            lieu de milliers. Ne pas « améliorer » ce script en distribuant.
#
# COMMENT  : 1. `git ls-files` → tar → ssh : seuls les fichiers VERSIONNÉS
#               partent (pas de build/, pas de _deps/), d'où la charge utile
#               minuscule.
#            2. côté serveur, extraction dans un dossier d'étape puis
#               `rsync -a --delete` vers l'arbre de travail. Le rsync est ce qui
#               propage les SUPPRESSIONS : une simple extraction tar laisserait
#               vivre les fichiers effacés localement, et donc des cibles
#               fantômes. rsync n'existe que côté serveur, d'où ce détour.
#            3. le dossier build/ distant est CONSERVÉ entre deux appels — c'est
#               tout l'intérêt : les appels suivants sont incrémentaux.
#            4. `nice -n 19` et -j6 sur 8 threads : la cible est un serveur de
#               PRODUCTION (ai.etheryale.com). Le build doit céder le pas.
#
# USAGE    : tools/remote-build.sh [--test] [--clean] [-j N] [-- <args cmake>]
#            GROVE_REMOTE_HOST=debian@1.2.3.4 tools/remote-build.sh --test
# ============================================================================

set -euo pipefail

HOST="${GROVE_REMOTE_HOST:-debian@142.44.139.223}"
WORK="${GROVE_REMOTE_DIR:-grovebuild}"      # relatif au $HOME distant
JOBS="${GROVE_REMOTE_JOBS:-6}"
NICE="${GROVE_REMOTE_NICE:-19}"

RUN_TESTS=0
CLEAN=0
CMAKE_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)  RUN_TESTS=1; shift ;;
        --clean) CLEAN=1; shift ;;
        -j)      JOBS="$2"; shift 2 ;;
        --)      shift; CMAKE_ARGS=("$@"); break ;;
        *)       echo "option inconnue : $1" >&2; exit 2 ;;
    esac
done

# Se placer à la racine du dépôt : git ls-files et les chemins relatifs en dépendent.
cd "$(git rev-parse --show-toplevel)"

echo "==> cible : $HOST:~/$WORK   (-j$JOBS, nice $NICE)"

# --- 1. Synchronisation ----------------------------------------------------
# On envoie la LISTE des fichiers versionnés, pas le dossier : c'est ce qui
# exclut mécaniquement build/, _deps/ et tout artefact local, sans maintenir une
# liste d'exclusions qui dériverait du .gitignore.
echo "==> synchronisation des sources"
SYNC_START=$(date +%s)
git ls-files -z | tar --null -czf - -T - | ssh "$HOST" "
    set -e
    rm -rf ~/$WORK.stage && mkdir -p ~/$WORK.stage ~/$WORK
    tar xzf - -C ~/$WORK.stage
    # --delete propage les suppressions ; --exclude protège le dossier de build
    # distant, qui n'existe pas côté source et serait sinon effacé à chaque appel.
    rsync -a --delete --exclude 'build/' ~/$WORK.stage/ ~/$WORK/
    rm -rf ~/$WORK.stage
"
echo "    $(($(date +%s) - SYNC_START)) s"

# --- 2. Build (+ tests) ----------------------------------------------------
# Tout est exécuté en UNE session ssh : ouvrir une connexion par étape coûterait
# plus cher que les étapes elles-mêmes sur un build incrémental court.
CLEAN_CMD=""
[[ $CLEAN -eq 1 ]] && CLEAN_CMD="rm -rf ~/$WORK/build;"

TEST_CMD=""
if [[ $RUN_TESTS -eq 1 ]]; then
    # ⚠️ ctest en SÉRIE (pas de -j) : plusieurs tests du dépôt portent leur propre
    # chien de garde et débordent sous charge parallèle (MemoryLeakHunter passe en
    # 161 s contre un plafond de 180). En parallèle on récolterait de faux rouges.
    TEST_CMD="
        echo '==> ctest (série)'
        cd ~/$WORK/build
        T0=\$(date +%s)
        nice -n $NICE ctest --output-on-failure > /tmp/grove-ctest.log 2>&1 || true
        echo \"    \$((\$(date +%s) - T0)) s\"
        grep -E 'tests passed|tests failed out of' /tmp/grove-ctest.log || true
        grep -E '^\s+[0-9]+ - ' /tmp/grove-ctest.log | grep -iE 'Failed|Timeout' || true
    "
fi

ssh "$HOST" "
    set -e
    $CLEAN_CMD
    cd ~/$WORK
    echo '==> configure'
    nice -n $NICE cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS[*]:-} > /tmp/grove-cfg.log 2>&1 || {
        echo 'CONFIGURE ÉCHOUÉ :'; tail -25 /tmp/grove-cfg.log; exit 1; }

    echo '==> build'
    T0=\$(date +%s)
    if nice -n $NICE ninja -C build -j$JOBS > /tmp/grove-bld.log 2>&1; then
        echo \"    \$((\$(date +%s) - T0)) s — OK\"
    else
        echo \"    \$((\$(date +%s) - T0)) s — ÉCHEC\"
        grep -B6 -iE 'error:' /tmp/grove-bld.log | head -40
        exit 1
    fi
    $TEST_CMD
"
