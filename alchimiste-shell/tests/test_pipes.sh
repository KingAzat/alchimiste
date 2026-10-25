#!/usr/bin/env bash
# test_pipes.sh — tubes (pipes) simples et chaînés.
set -u

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

run() { printf '%s\n' "$1" | "$BIN" 2>/dev/null; }

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

echo "--- Tubes simples ---"
check_out "P01 tube simple"        "printf 'b\na\nc\n' | sort"      $'a\nb\nc'
check_out "P02 guillemets + tube"  'echo "a b" | wc -w'              '2'
check_out "P03 interne dans tube"  'echo coucou | cat'                'coucou'
check_out "P04 echo -n dans tube"  'echo -n abc | wc -c'              '3'
check_out "P05 echo dans tube"     'echo abc | wc -c'                 '4'

echo "--- Chaînes de tubes ---"
check_out "P06 trois commandes"    "printf 'x\ny\nx\n' | sort | uniq -c"  $'      2 x\n      1 y'
check_out "P07 quatre commandes"   "printf '3\n1\n2\n1\n' | sort | uniq | wc -l" '3'
check_out "P08 grep + wc"          'touch x.c y.c
ls | grep .c | wc -l'              '2'

echo "--- Statut d'un pipeline (dernière commande) ---"
check_status "P09 true | false"    'true | false'                     1
check_status "P10 false | true"    'false | true'                     0
check_status "P11 inconnu | cat (statut = dernière commande)" 'commande_xyz_inconnue | cat' 0

echo "--- Tube + redirection combinés ---"
check_out "P12 tube vers fichier"  "printf 'b\na\n' | sort > tri.txt
cat tri.txt"                       $'a\nb'
check_out "P13 fichier vers tube"  'printf "z\ny\n" > src.txt
cat < src.txt | sort'               $'y\nz'

echo
echo "Résultat : $PASS réussis, $FAIL échoués"
if [[ $FAIL -gt 0 ]]; then
    printf 'Échecs : %s\n' "${FAILED[*]}"
    exit 1
fi
exit 0
