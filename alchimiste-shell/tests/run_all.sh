#!/usr/bin/env bash
# run_all.sh — orchestrateur de la suite de tests d'alchimiste-shell.
#
# Exécute :
#   1. la compilation (make) ;
#   2. les tests unitaires (lexer / parser / expansion) ;
#   3. les tests fonctionnels (commandes, tubes, redirections) ;
#   4. les tests interactifs sous pseudo-terminal (Ctrl+C, Ctrl+Z, jobs).
#
# Code de sortie : 0 si tout réussit, 1 sinon.
set -u

cd "$(dirname "$0")/.." || exit 1

RC=0

echo "=== Compilation (make) ==="
if ! make >/dev/null; then
    echo "ÉCHEC : la compilation a échoué"
    exit 1
fi
echo "OK"

echo
echo "=== Tests unitaires : lexer / parser / expansion ==="
gcc -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -Isrc \
    -o tests/test_parser tests/test_parser.c build/lexer.o build/parser.o \
    build/variables.o build/util.o || exit 1
./tests/test_parser || RC=1

echo
echo "=== Tests fonctionnels : commandes ==="
bash tests/test_commands.sh || RC=1

echo
echo "=== Tests fonctionnels : tubes ==="
bash tests/test_pipes.sh || RC=1

echo
echo "=== Tests fonctionnels : redirections ==="
bash tests/test_redirections.sh || RC=1

if command -v python3 >/dev/null 2>&1; then
    echo
    echo "=== Tests interactifs (pseudo-terminal) ==="
    python3 tests/test_interactive.py || RC=1
else
    echo
    echo "=== Tests interactifs ignorés (python3 absent) ==="
fi

echo
if [[ $RC -eq 0 ]]; then
    echo "======================================"
    echo "  TOUS LES TESTS ONT RÉUSSI"
    echo "======================================"
else
    echo "======================================"
    echo "  DES TESTS ONT ÉCHOUÉ"
    echo "======================================"
fi
exit $RC
