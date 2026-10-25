#!/usr/bin/env bash
# test_redirections.sh — redirections > >> < 2> 2>> 2>&1 >&N &> &>>.
set -u
# Tests indépendants de la langue du système : les programmes externes
# (ls, grep...) doivent émettre des messages en anglais (ex. "No such file").
export LC_ALL=C

BIN=${SHELL_BIN:-./alchimiste-shell}
# Rend le chemin absolu AVANT tout cd : les tests travaillent dans un
# répertoire temporaire différent du répertoire du projet.
case "$BIN" in
    /*) ;;
    *) BIN="$PWD/${BIN#./}" ;;
esac
PASS=0
FAIL=0
FAILED=()

TMP=$(mktemp -d)
export HOME="$TMP"
cd "$TMP" || exit 1
trap 'cd /; rm -rf "$TMP"' EXIT

run()     { printf '%s\n' "$1" | "$BIN" 2>/dev/null; }
run_err() { printf '%s\n' "$1" | "$BIN" 2>&1 >/dev/null; }

pass() { PASS=$((PASS + 1)); echo "PASS : $1"; }
fail() {
    FAIL=$((FAIL + 1))
    FAILED+=("$1")
    echo "FAIL : $1"
    echo "  attendu : $(printf '%q' "$2")"
    echo "  obtenu  : $(printf '%q' "$3")"
}

check_out() {
    local name=$1 input=$2 expected=$3 actual
    actual=$(run "$input")
    if [[ "$actual" == "$expected" ]]; then
        pass "$name"
    else
        fail "$name" "$expected" "$actual"
    fi
}

check_contains() {
    local name=$1 input=$2 pat=$3 actual
    actual=$(run "$input")
    if [[ "$actual" == *"$pat"* ]]; then
        pass "$name"
    else
        fail "$name" "*$pat*" "$actual"
    fi
}

check_status() {
    local name=$1 input=$2 code=$3 actual
    printf '%s\n' "$input" | "$BIN" >/dev/null 2>&1
    actual=$?
    if [[ "$actual" == "$code" ]]; then
        pass "$name"
    else
        fail "$name" "$code" "$actual"
    fi
}

check_err_contains() {
    local name=$1 input=$2 pat=$3 actual
    actual=$(run_err "$input")
    if [[ "$actual" == *"$pat"* ]]; then
        pass "$name"
    else
        fail "$name" "*$pat*" "$actual"
    fi
}

echo "--- Sortie standard ---"
check_out "R01 > écrasement"       'echo Bonjour > f.txt
cat f.txt'                          'Bonjour'
check_out "R02 > écrase l'existant" 'echo longue_ligne > f.txt
echo court > f.txt
cat f.txt'                          'court'
check_out "R03 >> ajout"           'echo un > f.txt
echo deux >> f.txt
cat f.txt'                          $'un\ndeux'
check_out "R04 interne vers fichier" 'pwd > p.txt
cat p.txt'                          "$TMP"
check_out "R05 > fichier avec espaces" 'echo donnée > "mon fichier.txt"
cat "mon fichier.txt"'              'donnée'

echo "--- Entrée standard ---"
check_out "R06 < lecture"          'printf "l1\nl2\n" > in.txt
cat < in.txt'                       $'l1\nl2'
check_status "R07 < fichier inexistant" 'cat < /tmp/inexistant_sce.txt' 1
check_err_contains "R08 message d'erreur du shell" 'cat < /tmp/inexistant_sce.txt' 'alchimiste-shell'

echo "--- Erreurs (stderr) ---"
check_status "R09 2> capture les erreurs" 'ls /inexistant_sce 2> err.txt' 2
check_contains "R10 contenu du fichier d'erreurs" 'ls /inexistant_sce 2> err.txt
cat err.txt'                        'No such file'
check_contains "R11 2>> ajout des erreurs" 'ls /inexistant_sce 2> err.txt
ls /inexistant_sce2 2>> err.txt
grep -c "No such" err.txt'          '2'
check_contains "R12 2>&1 vers le fichier" 'ls /inexistant_sce > out.txt 2>&1
cat out.txt'                        'No such file'
check_contains "R13 2>&1 avant > : erreur sur stdout, fichier vide" 'ls /inexistant_sce 2>&1 > r13.txt
wc -c < r13.txt'                   $'No such file or directory\n0'
check_contains "R14 &> sortie + erreurs" 'ls /inexistant_sce &> tout.txt
cat tout.txt'                       'No such file'
check_out "R15 &> sortie normale"   'echo ok &> tout2.txt
cat tout2.txt'                      'ok'
check_contains "R16 &>> ajout"      'echo un &>> journal.txt
echo deux &>> journal.txt
cat journal.txt'                    $'un\ndeux'

echo "--- Cas particuliers ---"
check_out "R17 redirection seule (crée le fichier)" '> seul.txt
wc -c < seul.txt'                   '0'
check_out "R18 redirection avant la commande"       '> avant.txt echo bonjour
cat avant.txt'                      'bonjour'
check_status "R19 redirection ambiguë (joker)"      'echo x > *' 2
check_err_contains "R20 message redirection ambiguë" 'echo x > *' 'redirection ambiguë'
check_contains "R21 umask appliqué aux fichiers créés" 'umask 077
echo x > u.txt
ls -l u.txt'                        '-rw-------'
check_contains "R22 interne avec redirection" 'cd /tmp > /dev/null
pwd'                                '/tmp'

echo
echo "Résultat : $PASS réussis, $FAIL échoués"
if [[ $FAIL -gt 0 ]]; then
    printf 'Échecs : %s\n' "${FAILED[*]}"
    exit 1
fi
exit 0
