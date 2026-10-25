# Préparation de la soutenance — alchimiste-shell

## 1. Plan de présentation orale (7 minutes)

| Temps | Contenu | Points clés à dire |
|---|---|---|
| 0:00–0:30 | **Introduction** | « Un shell = le rendez-vous entre l'utilisateur et les mécanismes du noyau : processus, descripteurs, signaux, terminal. » |
| 0:30–2:00 | **Architecture** | Schéma ci-dessous ; séparation lexer / parser / expansion / executor / jobs ; mots conservés bruts, expansion séparée. |
| 2:00–3:00 | **Mécanismes clés** | fork/execvp/waitpid, pipe/dup2, setpgid + tcsetpgrp, anti-zombies (SIGCHLD). |
| 3:00–5:00 | **Démonstration** | Scénario ci-dessous (section 3). |
| 5:00–6:00 | **Tests et qualité** | 209 tests automatisés, 17 sous pseudo-terminal ; ASan/UBSan/LSan sans erreur ; développement en 7 phases avec git. |
| 6:00–7:00 | **Limites et conclusion** | Annoncer clairement ce qui n'est PAS fait (substitution, découpage de mots...) ; perspective d'extension. |

Règle d'or : **ne jamais prétendre** à une fonctionnalité non testée ;
renvoyer vers les limites documentées (rapport, section 14).

## 2. Schéma d'architecture (à reproduire au tableau)

```
        ligne saisie
             │
   ┌─────────▼─────────┐    ┌──────────────────────────────┐
   │ shell.c (REPL)    │    │ process.c                    │
   │ read() + invite   │◄───┤ signaux : SIGINT drapeau,    │
   └─────────┬─────────┘    │ SIGCHLD -> anneau (anti-     │
             │              │ zombies), tcsetpgrp          │
   ┌─────────▼─────────┐    └──────────────▲───────────────┘
   │ lexer.c           │                   │
   │ ligne -> tokens   │            ┌──────┴───────┐
   └─────────┬─────────┘            │ jobs.c       │
             │                      │ jobs/fg/bg   │
   ┌─────────▼─────────┐            │ notifications│
   │ parser.c          │            └──────▲───────┘
   │ tokens -> arbre   │                   │
   └─────────┬─────────┘    ┌──────────────┴───────────────┐
             │              │ executor.c                   │
   ┌─────────▼─────────┐    │  ; && ||    tubes   &        │
   │ variables.c       │───►│  fork + execvp + waitpid     │
   │ expansion $ ~ *   │    │  setpgid / terminal          │
   └───────────────────┘    │      │ redirection.c         │
   history.c  (fichier dédié)     │ builtins.c (19 internes)
                            └─────┴────────────────────────┘
```

## 3. Scénario de démonstration (à préparer dans /tmp/alchimiste-demo)

```bash
make && ./alchimiste-shell
```

1. **Invite et basiques** :
   `pwd` · `mkdir demo && cd demo` · `echo "Bonjour tout le monde"` ·
   `echo 'littéral $HOME'` contre `echo $HOME`
2. **Commandes externes et code de retour** :
   `ls -la` · `commande_inconnue` → message + `echo $?` → 127
3. **Tubes et redirections** :
   `printf 'b\na\nb\n' | sort | uniq -c` ·
   `echo un > notes.txt` · `echo deux >> notes.txt` · `cat < notes.txt` ·
   `ls /inexistant 2> err.txt` · `cat err.txt`
4. **Opérateurs conditionnels** :
   `grep un notes.txt > /dev/null && echo trouvé || echo absent`
5. **Arrière-plan et tâches** :
   `sleep 30 &` → `[1] PID` · `jobs` · **Ctrl+Z** sur `sleep 5` → `jobs`
   (Arrêtée) · `bg` · `fg`
6. **Ctrl+C** : `sleep 30` puis Ctrl+C → le shell survit, `echo $?` → 130
7. **Variables et alias** :
   `NOM=Alchimiste` · `export X=1` + `env | grep X=` ·
   `alias ll='ls -la'` · `ll` · `type ll` · `type cd`
8. **Historique** : `history`
9. **Script** (autre terminal) : `./alchimiste-shell examples/exemple.sh`
10. **Sortie** : `exit` → « Au revoir ! »

Prévoir un filet de sécurité : si la démo interactive échoue, montrer
`make test` (209 tests) et `tests/results/run_all.log`.

## 4. Explication des cinq fonctions POSIX (questions garanties)

### fork() — créer un processus
`fork()` duplique le processus appelant : même code, mêmes descripteurs,
même mémoire (copie à l'écriture). Il renvoie **0 dans le fils** et **le
pid du fils dans le père** — c'est cette valeur de retour qui permet
d'écrire deux comportements dans un seul programme. Dans notre shell,
chaque commande externe est un fils (executor.c, `spawn_pipeline`).

### execvp() — remplacer le programme
`execvp(nom, argv)` remplace l'image du processus courant par le
programme `nom` (recherché dans `PATH` — le « p » de execvp), en
conservant pid, descripteurs ouverts et environnement. **Il ne revient
jamais** en cas de succès : c'est pourquoi on l'appelle toujours dans un
fils. En cas d'échec il revient avec -1/errno : notre shell distingue
ENOENT (127, « commande introuvable ») des autres erreurs (126).

### waitpid() — attendre et récolter
`waitpid(pid, &status, 0)` suspend le père jusqu'à la fin du fils et
récupère le statut : `WIFEXITED`/`WEXITSTATUS` (sortie normale),
`WIFSIGNALED`/`WTERMSIG` (tué par signal → `$? = 128 + n°signal`),
`WIFSTOPPED` (suspendu — utilisé pour Ctrl+Z, avec l'option WUNTRACED).
Sans wait, un fils terminé resterait **zombie** ; notre shell récolte
aussi en WNOHANG via SIGCHLD pour l'arrière-plan.

### pipe() — relier deux processus
`pipe(fd[2])` crée un tube anonyme : ce qui est écrit dans `fd[1]` sort
par `fd[0]` (FIFO, capacité limitée). Un pipeline `a | b` = un tube par
« | » ; le fils a duplique `fd[0]... ` non : **a** écrit dans `fd[1]`
(sortie), **b** lit `fd[0]` (entrée). Point critique évoqué en soutenance :
il faut **fermer tous les descripteurs inutilisés** (père et fils),
sinon le lecteur ne voit jamais la fin de fichier et la chaîne se
bloque.

### dup2() — dupliquer un descripteur
`dup2(ancien, nouveau)` fait pointer le numéro `nouveau` vers la même
entrée que `ancien` (en fermant l'ancien contenu de `nouveau`). C'est LA
fonction des redirections : `> f` = `open(f)` puis `dup2(fd, 1)` ; les
tubes l'utilisent pour brancher stdin/stdout (`dup2(p[0], 0)`). Ordre
important : dans `> f 2>&1`, le `2>&1` (`dup2(1, 2)`) copie le fd 1
**tel qu'il est à ce moment** — d'où la différence avec `2>&1 > f`.

## 5. Questions probables du professeur et réponses

**Q1. Pourquoi cd est-il interne et pas un programme ?**
Chaque commande externe est un fils : un `cd` externe changerait le
répertoire du fils puis mourrait, sans jamais affecter le shell. Seul un
`chdir()` dans le processus du shell fonctionne — c'est le critère qui
décide de l'internalisation (idem export, alias, jobs...).

**Q2. Comment le shell survit-il à Ctrl+C alors que sleep meurt ?**
Ctrl+C n'« appuie » pas sur le shell : le terminal délivre SIGINT au
**groupe de processus de premier plan**. Chaque pipeline forme son
propre groupe (`setpgid`) et reçoit le terminal (`tcsetpgrp`) — le
signal atteint donc la commande, pas le shell. Le shell, de son côté,
intercepte SIGINT (drapeau + saut de ligne). Après la mort du fils,
`$?` vaut 128+2 = 130.

**Q3. Pourquoi des zombies et comment les évitez-vous ?**
Un fils terminé non attendu reste dans la table des processus (zombie).
Trois mécanismes : (1) attente explicite pour le premier plan
(`jobs_wait`) ; (2) handler SIGCHLD qui fait `waitpid(WNOHANG)` en
boucle et stocke les résultats dans un anneau asynchrone-sûr —
`waitpid` et les écritures `volatile sig_atomic_t` sont les seules
opérations autorisées dans un handler ; (3) drain de l'anneau avant
chaque invite, dans `jobs`, et pendant l'attente.

**Q4. Que fait `2>&1` et pourquoi l'ordre compte-t-il ?**
`dup2(1, 2)` : le descripteur 2 pointe vers la cible **actuelle** de 1.
`> f 2>&1` : tout vers f. `2>&1 > f` : stderr vers l'ancienne sortie,
stdout vers f. Nos redirections sont appliquées dans l'ordre de
déclaration (test R12/R13).

**Q5. Différence entre guillemets simples et doubles ?**
Simples : contenu strictement littéral (`'$HOME'` reste `$HOME`).
Doubles : variables expansées, `\"` `\\` `\$` protégés. Testé par les
tests unitaires et T19.

**Q6. Pourquoi 127 et 126 ?**
Convention shell : 127 = commande introuvable (ENOENT après recherche
PATH), 126 = trouvée mais non exécutable (permissions). 128+n = tuée
par le signal n.

**Q7. Comment testez-vous Ctrl+C sans clavier ?**
Par un pseudo-terminal (module Python `pty`) : le shell y est lancé
comme chef de session avec terminal de contrôle (`setsid` +
`TIOCSCTTY`), puis le test écrit le caractère 0x03 dans le pty — le
noyau transforme cela en SIGINT exactement comme une vraie touche.
17 tests couvrent Ctrl+C, Ctrl+Z, jobs/fg/bg (tests/test_interactive.py).

**Q8. Où vérifiez-vous les erreurs d'appels système ?**
Systématiquement : fork, pipe, open, dup2, waitpid, setpgid, tcsetpgrp,
malloc (via xmalloc qui termine proprement avec message), execvp (126/
127), fichiers de scripts (127 + message), lignes trop longues,
dépassements d'arguments. Chaque message est testé.

**Q9. Pourquoi pas strtok() sur les espaces ?**
C'est l'énoncé même du projet : strtok casserait `echo "a ; b"`,
`cat "mon fichier.txt"`, `\;`... Notre lexer est une vraie analyse
lexicale avec états (guillemets, échappements), testée par 86 tests
unitaires.

**Q10. Que se passe-t-il pour `a && b &` ?**
Limite documentée : `&` s'applique au dernier pipeline — `a` s'exécute
au premier plan puis `b` en arrière-plan, là où Bash arrière-planise
toute la liste. (Rapport, limite n°4.)

**Q11. Comment fonctionne l'historique sans toucher à Bash ?**
Fichier dédié `~/.alchimiste_history`, chargé au démarrage interactif,
sauvegardé à la sortie, capacité 1000 ; `history`, `history -c`,
`history N`. `.bash_history` n'est jamais lu ni écrit.

**Q12. Un exemple de bug trouvé par les tests ?**
Le plus intéressant : les tampons stdio. `echo x > f` laissait f vide
parce que printf écrit en mémoire alors que la redirection change le
descripteur ; le vidage avait lieu après restauration. Corrigé par un
double fflush (avant/après), détection possible seulement parce que le
script d'exemple vérifiait le **contenu du fichier**.

**Q13. Que faites-vous si fork échoue en plein pipeline ?**
Les fils déjà créés sont tués (SIGKILL, via leur groupe) puis attendus,
les tubes fermés, message d'erreur, statut 1 (executor.c).

**Q14. Pourquoi read() et pas fgets() ?**
Trois raisons : détecter proprement les lignes trop longues (purge
jusqu'au \n), ne pas supposer « un read = une ligne » (faux sur les
tubes — bug réel de la phase 1), et être interrompu par les signaux
(notifications de tâches pendant l'attente de saisie).

**Q15. Et l'expansion des jokers ?**
`glob()` (POSIX) sur les mots contenant des métacaractères non protégés
— y compris issus de variables non quotées ; résultat trié ; sans
correspondance le mot reste littéral (comme Bash par défaut). Les
redirections refusent les jokers (« redirection ambiguë »).

## 6. Comparaison alchimiste-shell / Bash

| Aspect | Bash | alchimiste-shell |
|---|---|---|
| Analyse | lexer + parser complets | lexer + parser (descente récursive), sous-ensemble |
| Opérateurs | ; && \|\| \| & et bien plus (( ), [[ ]], <(...)...) | ; && \|\| \| & |
| Redirections > >> < 2> 2>> 2>&1 &> | oui | oui (testés) |
| Here-doc, substitution $(), \$(()) | oui | **non** (documenté) |
| Variables | locales/exportées, tableaux, $((...)) | locales/exportées, $ ${} $? $$ |
| Découpage de mots / IFS | oui | **non** (documenté) |
| Jokers | * ? [ ] + extglob | * ? [ ] via glob() |
| Historique | .bash_history, !!, Ctrl+R | ~/.alchimiste_history dédié, history/-c/N |
| Édition de ligne | readline | **non** (read() ligne à ligne) |
| Contrôle des tâches | complet (jobs/fg/bg, % spécifications riches) | complet sur le noyau (groupes + terminal), %N/N seulement |
| Anti-zombies | récolte SIGCHLD | idem (anneau async-signal-safe) |
| Commandes internes | ~60 | 19 (les demandées + echo) |
| Scripts | complet (fonctions, set -e...) | sous-ensemble : commandes, commentaires, variables |
| Taille | ~150 000 lignes | ≈ 4 400 lignes |

Message clé : « Mêmes mécanismes fondamentaux, périmètre volontairement
restreint, tout ce qui est revendiqué est testé. »

## 7. Choix techniques à justifier

1. **Mots bruts + expansion séparée** — reproduit l'architecture de
   Bash ; simplifie `"$X"` vs `$X` et la règle des champs.
2. **Anneau SIGCHLD** — le respect des fonctions asynchrone-sûres est
   un point de cours démontré en pratique.
3. **setpgid père ET fils** — élimination de la course fork/exec.
4. **Terminal donné dès le premier fork du premier plan** — réduit la
   fenêtre où une commande pourrait lire depuis un mauvais groupe.
5. **_exit() dans les fils** — ne pas rejouer les atexit/vidages du
   père ; fflush explicites avant.
6. **Limites chiffrées (4095/256/32/1000)** — robustesse exigée par le
   cahier des charges ; messages explicites au lieu de débordements.
7. **Fichier d'historique dédié** — ne jamais corrompre
   .bash_history de l'utilisateur.
8. **Messages en français** — projet francophone, ergonomie de
   soutenance (choix assumé, les codes de retour restent ceux de Bash).

## 8. Anticipez les demandes « montrez-moi »

- `git log --oneline` : les 7 phases + correctifs ;
- `make` : compilation sans avertissement ;
- `make test` : 209 tests en direct ;
- `tests/results/run_all.log` : sortie archivée ;
- `make debug` + relance d'une suite : sanitizers propres ;
- un `grep -n "system(" src/*.c` vide : aucun recours à system().
