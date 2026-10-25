#!/usr/bin/env bash
# test_commands.sh — commandes simples, internes, variables, opérateurs,
# arrière-plan, scripts, erreurs de syntaxe et d'exécution.
#
# Ces tests utilisent Bash comme harnais : ils pilotent alchimiste-shell
# par son entrée standard (mode non interactif : ni bannière ni invite)
# et comparent les sorties réelles aux sorties attendues.
set -u
# Tests indépendants de la langue du système : les programmes externes
# (cat, ls, grep...) doivent émettre des messages en anglais (ex. "No such file").
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

# Répertoire de travail isolé (les tests destructifs restent dedans).
TMP=$(mktemp -d)
export HOME="$TMP"   # isole .alchimiste_history et .alchimiste_rc
cd "$TMP" || exit 1
trap 'cd /; rm -rf "$TMP"' EXIT

run()      { printf '%s\n' "$1" | "$BIN" 2>/dev/null; }
run_err()  { printf '%s\n' "$1" | "$BIN" 2>&1 >/dev/null; }

pass() { PASS=$((PASS + 1)); echo "PASS : $1"; }
fail() {
    FAIL=$((FAIL + 1))
    FAILED+=("$1")
    echo "FAIL : $1"
    echo "  attendu : $(printf '%q' "$2")"
    echo "  obtenu  : $(printf '%q' "$3")"
}

check_out() { # nom, entrée, attendu (comparaison exacte de stdout)
    local name=$1 input=$2 expected=$3 actual
    actual=$(run "$input")
    if [[ "$actual" == "$expected" ]]; then
        pass "$name"
    else
        fail "$name" "$expected" "$actual"
    fi
}

check_contains() { # nom, entrée, motif (inclus dans stdout)
    local name=$1 input=$2 pat=$3 actual
    actual=$(run "$input")
    if [[ "$actual" == *"$pat"* ]]; then
        pass "$name"
    else
        fail "$name" "*$pat*" "$actual"
    fi
}

check_status() { # nom, entrée, code de sortie attendu
    local name=$1 input=$2 code=$3 actual
    printf '%s\n' "$input" | "$BIN" >/dev/null 2>&1
    actual=$?
    if [[ "$actual" == "$code" ]]; then
        pass "$name"
    else
        fail "$name" "$code" "$actual"
    fi
}

check_err_contains() { # nom, entrée, motif (inclus dans stderr)
    local name=$1 input=$2 pat=$3 actual
    actual=$(run_err "$input")
    if [[ "$actual" == *"$pat"* ]]; then
        pass "$name"
    else
        fail "$name" "*$pat*" "$actual"
    fi
}

echo "--- Commandes simples et arguments ---"
check_out "T01 commande simple"            'echo bonjour'                       'bonjour'
check_out "T02 commande avec arguments"    'echo a b c'                         'a b c'
check_out "T03 espaces multiples"          'echo    un     deux'                'un deux'
check_out "T04 guillemets doubles"         'echo "Bonjour tout le monde"'       'Bonjour tout le monde'
check_out "T05 guillemets simples"         "echo 'Texte entre guillemets'"      'Texte entre guillemets'
check_out "T06 échappement d'espace"       'echo un\ deux'                      'un deux'
check_out "T07 guillemets internes"        'echo "Il a dit \"salut\""'          'Il a dit "salut"'
check_out "T08 argument avec espace (fichier)" \
    'touch "mon fichier.txt"
ls'                                       'mon fichier.txt'
check_out "T09 champ vide non quoté disparaît" \
    'echo a $INEXISTANTE b'                  'a b'
check_out "T09b champ vide quoté conservé" \
    'echo a "$INEXISTANTE" b'                'a  b'

echo "--- Commande interne cd ---"
check_out "T10 cd + pwd"                   'cd /tmp
pwd'                                        '/tmp'
check_out "T11 cd sans argument (HOME)"    'cd
pwd'                                        "$HOME"
check_status "T12 cd inexistant"           'cd /repertoire_qui_nexiste_pas'     1
check_err_contains "T13 cd message d'erreur" 'cd /repertoire_qui_nexiste_pas'   'cd :'
check_out "T14 cd - retour"                'cd /tmp
cd -
pwd'                                        "$HOME
$HOME"

echo "--- Commande inconnue ---"
check_status "T15 commande inconnue : statut 127" 'commande_totalement_inconnue' 127
check_err_contains "T16 commande inconnue : message" 'commande_totalement_inconnue' 'commande introuvable'

echo "--- Variables d'environnement ---"
check_out "T17 \$HOME"                     'echo $HOME'                         "$HOME"
check_out "T18 \${HOME} accolades"         'echo ${HOME}'                       "$HOME"
check_out "T19 guillemets simples : littéral" "echo '\$HOME'"                    '$HOME'
check_out "T20 affectation et lecture"     'X=azerty
echo $X'                                    'azerty'
check_out "T21 unset"                      'X=1
unset X
echo [$X]'                                  '[]'
check_contains "T22 export + env"          'export SCE_TESTVar=ok
env | grep ^SCE_TESTVar='                    'SCE_TESTVar=ok'
check_out "T23 \$? après true"             'true
echo $?'                                    '0'
check_out "T24 \$? après false"            'false
echo $?'                                    '1'
check_out "T25 \$? après commande inconnue" 'commande_inconnue_xyz
echo $?'                                    '127'
actual=$(run 'echo $$')
if [[ "$actual" =~ ^[0-9]+$ ]]; then pass "T26 \$\$ numérique"; else fail "T26 \$\$ numérique" '<nombre>' "$actual"; fi

echo "--- \$? mis à jour APRÈS CHAQUE commande (régression) ---"
check_out "T26a \$? même ligne après false"    'false; echo $?'                     '1'
check_out "T26b \$? même ligne après true"     'true; echo $?'                      '0'
check_out "T26c \$? après exit N même ligne"  "sh -c 'exit 3'; echo \$?"           '3'
check_out "T26d \$? dans || (échec amont)"    'false || echo code=$?'              'code=1'
check_out "T26e \$? chaîne ; complète"        'false; true; echo $?'               '0'
check_out "T26f \$? après pipeline échoué"    'printf x | grep rien; echo $?'      '1'
check_out "T26g \$? dans && (succès amont)"   'true && echo code=$?'               'code=0'

echo "--- Séquences et opérateurs conditionnels ---"
check_out "T27 séquence ;"                 'echo a ; echo b'                    $'a\nb'
check_out "T28 && si succès"               'true && echo oui'                   'oui'
check_out "T29 && si échec"                'false && echo non'                  ''
check_out "T30 || si échec"                'false || echo secours'              'secours'
check_out "T31 || si succès"               'true || echo jamais'                ''
check_contains "T32 cd || message"         'cd /inexistant || echo Échec'       'Échec'
check_out "T33 chaîne mixte"               'false && echo a || echo b'          'b'
check_out "T34 mkdir ; cd ; pwd"           'mkdir projet
cd projet
pwd'                                         "$TMP/projet"

echo "--- Arrière-plan et jobs ---"
check_contains "T35 & puis jobs (En cours)" 'sleep 0.5 &
jobs'                                        'En cours'
check_contains "T36 tâche terminée (Terminée)" 'sleep 0.1 &
sleep 0.4
jobs'                                        'Terminée'

echo "--- Tilde et jokers ---"
check_out "T37 tilde"                      'echo ~'                             "$HOME"
check_out "T38 tilde + chemin"             'echo ~/Documents'                   "$HOME/Documents"
check_out "T39 tilde protégé"              'echo "~"'                           '~'
check_out "T40 joker *"                    'touch a.c b.c z.txt
echo *.c'                                    'a.c b.c'
check_out "T41 joker ?"                    'touch test
echo t?st'                                   'test'
check_out "T42 joker sans correspondance"  'echo *.aucune_chance'               '*.aucune_chance'
check_out "T43 joker protégé"              'echo "*"'                           '*'

echo "--- type / alias / umask / history ---"
check_out "T44 type interne"               'type cd'                            'cd est une commande interne du shell'
check_contains "T45 type externe"          'type ls'                            'ls est /'
check_status "T46 type inconnu"            'type commande_bizarre'              1
check_out "T47 alias"                      "alias ll='echo ALIAS-OK'
ll"                                          'ALIAS-OK'
check_out "T48 alias + arguments"          "alias s='echo start'
s x y"                                       'start x y'
check_status "T49 unalias puis usage"      'alias tt=echo
unalias tt
tt'                                          127
check_out "T50 umask"                      'umask 022
umask'                                       '0022'
check_contains "T51 history"               'echo un
echo deux
history 2'                                   'echo deux'
check_out "T52 history -c"                 'echo x
history -c
history'                                     $'x\n    1  history'

echo "--- Divers ---"
check_contains "T53 help"                  'help'                               'Commandes internes'
check_out "T54 commentaires"               '# un commentaire
echo après'                                 'après'
check_out "T55 ligne vide"                 '
echo deux'                                  'deux'
check_status "T56 exit 5"                  'exit 5'                             5
check_out "T57 exit sans argument"         'false
exit'                                       ''

echo "--- Scripts et source ---"
cat > sce_script.sh <<'EOF'
# script de test
echo début
X=valeur_interne
echo $X
cd /tmp
pwd
echo fin
EOF
actual=$("$BIN" sce_script.sh 2>/dev/null)
expected=$'début\nvaleur_interne\n/tmp\nfin'
if [[ "$actual" == "$expected" ]]; then pass "T58 script multi-commandes"; else fail "T58 script multi-commandes" "$expected" "$actual"; fi
check_status "T59 code de sortie du script" 'exit
true'                                        0

printf 'VARDEMO=cinq\ncd /usr\n' > sce_source.sh
check_out "T60 source persiste variables+répertoire" 'source sce_source.sh
echo $VARDEMO
pwd'                                         $'cinq\n/usr'
check_out "T61 point . équivalent"          '. sce_source.sh
echo $VARDEMO'                               'cinq'

echo "--- Erreurs de syntaxe ---"
check_status "T62 | en début de ligne"     '| ls'                               2
check_status "T63 guillemet non fermé"     'echo "abc'                         2
check_status "T64 ; ; "                    'a ; ; b'                            2
check_status "T65 > sans fichier"          'echo >'                             2
check_err_contains "T66 message d'erreur de syntaxe" '| ls'                  'erreur de syntaxe'

echo "--- Erreurs d'exécution ---"
check_status "T67 cat fichier inexistant"  'cat /tmp/fichier_inexistant_sce_42' 1
check_err_contains "T68 message du programme externe" 'cat /tmp/fichier_inexistant_sce_42' 'No such file'
check_status "T69 trop d'arguments"        "echo $(printf 'a%.0s ' $(seq 1 300))" 2
check_err_contains "T70 message trop d'arguments" "echo $(printf 'a%.0s ' $(seq 1 300))" 'trop d'

echo
echo "Résultat : $PASS réussis, $FAIL échoués"
if [[ $FAIL -gt 0 ]]; then
    printf 'Échecs : %s\n' "${FAILED[*]}"
    exit 1
fi
exit 0
