# exemple.sh — script de démonstration pour alchimiste-shell.
# Exécution :   ./alchimiste-shell examples/exemple.sh
#
# Sous-ensemble supporté : commandes simples, variables, séquences ;,
# opérateurs && et ||, tubes |, redirections, arrière-plan &,
# commentaires #. Ce script N'EST PAS un script Bash complet.

echo "=== Démonstration d'alchimiste-shell ==="
date

# Variables et environnement
NOM=Alchimiste
echo "Bonjour $NOM !"
export DEMO_VAR=42
echo "DEMO_VAR=$DEMO_VAR"

# Séquences et opérateurs conditionnels
mkdir -p /tmp/alchimiste-demo && cd /tmp/alchimiste-demo || echo "échec"

# Fichiers et redirections
echo "première ligne" > notes.txt
echo "deuxième ligne" >> notes.txt
echo "--- contenu de notes.txt ---"
cat < notes.txt

# Tubes : trois commandes reliées
printf 'cerise\npomme\nabricot\ncerise\n' | sort | uniq -c

# Jokers
touch a.c b.c
echo "fichiers .c : " *.c

# Arrière-plan et tâches
sleep 1 &
jobs

# Code de retour et condition
grep première notes.txt > /dev/null && echo "« première » trouvée dans notes.txt" || echo "« première » absente de notes.txt"

# Commande inconnue : erreur propre, code 127
# commande_qui_nexiste_pas

cd -
echo "Fin de la démonstration."
