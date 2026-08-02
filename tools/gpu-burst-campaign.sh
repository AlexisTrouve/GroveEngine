#!/usr/bin/env bash
# Campagne longue sur les salves 0xC0000374 (known-annoyances §3ter).
#
# QUOI : boucle le jeu de tests GPU pendant DURATION_MIN, en horodatant chaque passe, en notant
#   QUELLES cibles echouent, et en echantillonnant la telemetrie GPU AVANT et APRES chaque passe.
#
# POURQUOI cette forme : le phenomene vient par SALVES d'une constante de temps de l'ordre de la
#   dizaine de minutes. Toute mesure courte conclut ce que sa fenetre contient -- c'est l'erreur
#   commise le 02/08 ("5 echecs sur 5 donc deterministe", faux 30 min plus tard). Il faut donc une
#   serie longue et regulierement espacee, pas des rafales.
#
# CE QU'ON CHERCHE : la salve corrile-t-elle avec la temperature, l'horloge SM (throttling), ou un
#   simple compteur cumule de creations/destructions de device ? La colonne `pass` sert a ce dernier.
set -u

BUILD="C:/Users/alexi/Documents/projects/groveengine/build"
LOG="${1:-/tmp/gpu_burst.log}"
DURATION_MIN="${2:-60}"

END=$(( $(date +%s) + DURATION_MIN * 60 ))
pass=0

telemetry() {   # temp,util,clock -> une seule ligne, champs separes par des virgules
    nvidia-smi --query-gpu=temperature.gpu,utilization.gpu,clocks.sm,pstate,power.draw --format=csv,noheader 2>/dev/null \
        | head -1 | tr -d ' ' | tr '\n' ' '
}

{
    echo "# campagne demarree $(date '+%Y-%m-%d %H:%M:%S') -- duree ${DURATION_MIN} min"
    echo "# colonnes: heure | passe | echoues | temp,util,clk,pstate,W AVANT | idem APRES | noms"
} >> "$LOG"

while [ "$(date +%s)" -lt "$END" ]; do
    pass=$(( pass + 1 ))
    ts=$(date '+%H:%M:%S')
    before=$(telemetry)

    out=$(cd "$BUILD" && ctest -R "gpu|Gpu" 2>&1)

    after=$(telemetry)
    # Nombre d'echecs + noms des cibles fautives (vide si tout passe).
    nfail=$(printf '%s' "$out" | grep -oE '[0-9]+ tests failed' | head -1 | grep -oE '^[0-9]+')
    [ -z "$nfail" ] && nfail=0
    names=$(printf '%s' "$out" | sed -n 's/^[[:space:]]*[0-9]\+ - \([A-Za-z0-9_]*\).*/\1/p' | tr '\n' ',' )

    printf '%s | %3d | %d | %s| %s| %s\n' "$ts" "$pass" "$nfail" "$before" "$after" "$names" >> "$LOG"
done

echo "# campagne terminee $(date '+%H:%M:%S') -- $pass passes" >> "$LOG"
