# Rapport de projet — ALCHIMISTE SHELL

**Mini-shell Linux en C — projet de Sciences des Systèmes d'Exploitation (SCE)**

---

| | |
|---|---|
| **Université** : `_________________________________` |
| **Filière / Module** : `SCE — Sciences des Systèmes d'Exploitation` |
| **Enseignant** : `_________________________________` |
| **Étudiants** : 1. `_____________________` 2. `_____________________` 3. `_____________________` |
| **Année universitaire** : 2026–2027 |
| **Langage** : C11 (POSIX.1-2008) · **Compilateur** : GCC · **Cible** : Ubuntu Linux |
| **Dépôt** : git (historique des 8 phases conservé, `git log`) |

---

## Table des matières

1. [Introduction](#1-introduction)
2. [Contexte et problématique](#2-contexte-et-problématique)
3. [Objectifs généraux et spécifiques](#3-objectifs)
4. [Cahier des charges](#4-cahier-des-charges)
5. [Architecture générale](#5-architecture-générale)
6. [Fonctionnement du lexer et du parser](#6-lexer-et-parser)
7. [Création et exécution des processus](#7-création-et-exécution-des-processus)
8. [Commandes internes et externes](#8-commandes-internes-et-externes)
9. [Pipes et redirections](#9-pipes-et-redirections)
10. [Gestion des variables et de l'environnement](#10-variables-et-environnement)
11. [Gestion des signaux et des tâches](#11-signaux-et-tâches)
12. [Tests et résultats](#12-tests-et-résultats)
13. [Difficultés techniques](#13-difficultés-techniques)
14. [Limites du projet](#14-limites-du-projet)
15. [Perspectives d'amélioration](#15-perspectives-damélioration)
16. [Conclusion](#16-conclusion)
17. [Répartition des tâches](#17-répartition-des-tâches)
18. [Références techniques](#18-références-techniques)

---

## 1. Introduction

Un shell n'est pas un simple lanceur de programmes : c'est le point de
rencontre entre l'utilisateur, le système de fichiers et les mécanismes
fondamentaux d'un système d'exploitation — processus, descripteurs,
signaux, terminal. Comprendre un shell, c'est comprendre comment le noyau
expose ses services aux programmes.

Ce projet, réalisé dans le cadre du module SCE, consiste à concevoir et
développer **alchimiste-shell**, un interpréteur de commandes Linux
écrit en C11/POSIX. L'objectif n'est pas de cloner Bash, mais de
construire un shell **réellement fonctionnel** : boucle interactive,
analyse lexicale et syntaxique complète, exécution de commandes externes,
tubes, redirections, variables, expansions, historique, arrière-plan et
contrôle des tâches avec groupes de processus et gestion du terminal.

Le projet a été mené en sept phases validées progressivement (compilation
et tests à chaque étape, historique git conservé), et se conclut par une
suite de **209 tests automatisés** — dont 17 sous pseudo-terminal pour les
comportements impossibles à tester par un simple tube (Ctrl+C, Ctrl+Z,
jobs/fg/bg). Le présent rapport reflète exactement l'état réel du
logiciel : ce qui fonctionne est démontré par des tests, ce qui manque
est explicitement listé dans les limites (section 14).

## 2. Contexte et problématique

Le shell est le programme le plus utilisé sur un serveur Linux : `bash`,
`dash`, `zsh` ou `fish` exécutent des millions de commandes chaque jour.
Derrière une apparente simplicité — lire une ligne, lancer un programme —
se cachent des mécanismes profondément liés au noyau :

- **Processus** : `fork()`, `exec()`, `waitpid()` — comment un programme
  en devient un autre sans jamais « appeler » le nouveau ;
- **Descripteurs** : tubes anonymes (`pipe`), duplication (`dup2`),
  redirection de l'entrée/sortie — la base de la composition de
  programmes Unix (« écrire des programmes qui font une chose et la font
  bien, connectés par des tubes ») ;
- **Signaux et terminal** : Ctrl+C ne « tue pas un programme », il
  déclenche SIGINT vers un **groupe de processus de premier plan** ;
  Ctrl+Z envoie SIGTSTP ; le terminal appartient à un groupe à la fois
  (`tcsetpgrp`) ;
- **Zombies** : tout processus fils terminé doit être récolté par son
  père, sous peine de saturer la table des processus.

La problématique du projet est donc double : (1) implémenter correctement
ces mécanismes, sans jamais déléguer l'interprétation à un shell existant
(l'usage de `system()` est proscrit) ; (2) structurer le code de façon
modulaire et pédagogique, chaque module pouvant être expliqué et défendu
en soutenance.

## 3. Objectifs

### 3.1 Objectif général

Produire un mini-shell Linux solide, extensible et pédagogiquement
défendable : `alchimiste-shell` doit compiler sans avertissement sous
GCC (`-Wall -Wextra -Wpedantic`), exécuter les programmes du système,
et gérer les mécanismes fondamentaux d'un shell.

### 3.2 Objectifs spécifiques

1. Boucle interactive avec invite `utilisateur@hôte:chemin$` et bannière ;
2. Analyseur lexical et syntaxique réel (guillemets, échappements,
   opérateurs, erreurs de syntaxe) — pas un simple `strtok()` ;
3. Exécution des commandes externes par `fork()`/`execvp()`/`waitpid()`,
   avec distinction 126/127 selon l'erreur ;
4. Opérateurs `;`, `&&`, `||`, `|`, `&` ;
5. Redirections `> >> < 2> 2>> 2>&1 >&N &> &>>` ;
6. Variables (locales/exportées), `$VAR ${} $? $$`, tilde, jokers `glob()` ;
7. Historique dédié (`~/.alchimiste_history`), alias ;
8. Contrôle des tâches complet : groupes de processus, terminal,
   `jobs`/`fg`/`bg`, notifications ;
9. Robustesse : pas de zombie, pas de fuite (sanitizers), limites de
   tailles vérifiées, messages d'erreur en français ;
10. Scripts : mode `alchimiste-shell script.sh` et `source` ;
11. Documentation complète et tests réels reproductibles.

## 4. Cahier des charges

Le cahier des charges imposait notamment : 18 commandes internes
(`cd, pwd, exit, help, export, unset, env, history, alias, unalias, jobs,
fg, bg, umask, type, set, source/.`, plus `echo`), la séparation
lexer/parser/executor/builtins/processus, un Makefile offrant `make`,
`make run`, `make test`, `make clean`, des tests couvrant 20 cas
obligatoires, et un développement **par phases** avec validation à chaque
étape.

Toutes ces exigences sont satisfaites ; les écarts éventuels avec Bash
sont documentés en section 14. Le projet ne prétend **pas** à la
compatibilité complète avec Bash : l'objectif est un sous-ensemble solide
et extensible.

## 5. Architecture générale

### 5.1 Modules (≈ 4 430 lignes de C, 12 fichiers .c + 11 .h)

| Module | Rôle | Lignes |
|---|---|---|
| `main.c` | point d'entrée, mode script/interactif | 28 |
| `shell.c` | REPL, lecture `read()`, invite, scripts, `source`, rc | 442 |
| `lexer.c` | analyse lexicale → tokens | 273 |
| `parser.c` | analyse syntaxique → arbre de commandes | 460 |
| `executor.c` | exécution, tubes, fork/exec/waitpid, alias | 620 |
| `variables.c` | variables, alias, expansion ($ ~ jokers) | 674 |
| `builtins.c` | registre et implémentation des internes | 517 |
| `redirection.c` | open/dup2/close des redirections | 100 |
| `process.c` | signaux, terminal, récolte SIGCHLD | 175 |
| `jobs.c` | table des tâches, jobs/fg/bg, notifications | 436 |
| `history.c` | historique en mémoire et fichier dédié | 133 |
| `util.c` | allocations vérifiées, recherche PATH | 92 |

### 5.2 Flux d'une commande

```
ligne → lexer (tokens) → parser (arbre) → expansion (variables.c)
      → executor (fork/execvp, tubes, redirections) → builtins ou externe
      → libération complète
```

La description détaillée (diagrammes tubes/signaux/terminal, structures
de données, propriété de la mémoire) figure dans
[`docs/architecture.md`](architecture.md).

### 5.3 Grammaire reconnue

```
ligne      := liste EOF
liste      := et_ou ((';' | '&') et_ou)*
et_ou      := pipeline (('&&' | '||') pipeline)*
pipeline   := simple ('|' simple)*
simple     := (mot | redirection)+
redirection:= [N] '>' | '>>' | '<' fichier   |   N '>&' M   |   '&' '>' fichier
```

## 6. Lexer et parser

### 6.1 Lexer

Le lexer (`src/lexer.c`) transforme une ligne en tableau de tokens
terminé par `TOK_EOF`. Il gère : espaces/tabulations multiples,
guillemets simples (contenu strictement littéral), guillemets doubles
(`\"`, `\\`, `\$` protégés), antislash d'échappement, commentaires `#`
en début de mot, opérateurs (`;` `|` `&` `&&` `||` `<` `>` `>>` `>&`
`&>` `&>>`) et **nombres de descripteurs** : dans `ls 2> err`, le `2`
collé au `>` devient un token `TOK_IO_NUMBER` (alors que `echo 2 > f`
traite `2` comme un simple argument — comportement Bash).

Point de conception important : les mots sont conservés **bruts**
(guillemets inclus). La suppression des guillemets et l'expansion sont
reportées à une phase distincte (section 10). Un mot `"a ; b"` forme
donc bien un seul token contenant le `;` protégé.

Erreurs détectées : guillemet simple/double non fermé, antislash en fin
de ligne — message `erreur de syntaxe`, statut 2.

### 6.2 Parser

Le parser (`src/parser.c`) est une **descente récursive** sans table.
Elle construit une `CommandLine` : liste chaînée de `Node` (séparateur
`SEP_SEQ`/`SEP_AND`/`SEP_OR` vers le suivant), chaque `Node` portant un
`Pipeline` (1 à 32 `Cmd`), chaque `Cmd` gardant ses mots bruts et ses
redirections **dans l'ordre de déclaration** — ce qui reproduit la
sémantique de `> f 2>&1` (tout vers `f`) contre `2>&1 > f` (erreurs vers
l'ancienne sortie).

Erreurs détectées : opérateur inattendu (`| ls`, `ls |`, `a ; ; b`,
`> ` sans fichier, `&& a`, `&` seul), avec le token fautif nommé dans le
message. 49 tests unitaires couvrent ce module (section 12).

## 7. Création et exécution des processus

### 7.1 Le trio fork / execvp / waitpid

```c
pid = fork();
if (pid == 0) {                       /* fils */
    process_reset_child_signals();    /* voir 11.2 */
    /* tubes, redirections, affectations */
    execvp(argv[0], argv);            /* remplace l'image du fils */
    _exit(errno == ENOENT ? 127 : 126);
}
waitpid(pid, &status, 0);             /* père : attend et récolte */
```

`fork()` duplique le shell ; le fils **remplace** son image par
`execvp()` (recherche dans `PATH` incluse). Le père attend avec
`waitpid()` et convertit le statut : `WEXITSTATUS` (sortie normale),
`128 + WTERMSIG` (signal — Ctrl+C donne ainsi `$? = 130`).

L'échec d'`execvp` est distingué : `127` si le programme est introuvable
(ENOENT/ENOTDIR), `126` sinon (permissions...). Aucune commande
inconnue n'est exécutée silencieusement.

### 7.2 Commandes internes

Les commandes internes s'exécutent **dans le processus du shell** quand
elles forment une commande seule — indispensable pour `cd`, `export`,
`alias`, `jobs`... Placées dans un pipeline (`history | grep cd`), elles
s'exécutent dans leur fils, comme dans Bash : elles ne peuvent alors pas
modifier l'état du shell.

### 7.3 Parcours de l'arbre et opérateurs conditionnels

L'exécuteur parcourt la liste de `Node` avec un drapeau `skip` :
`a && b` saute `b` si `$? != 0`, `a || b` saute `b` si `$? == 0`.
Une commande sautée ne modifie pas `$?` (`false && echo a || echo b`
affiche `b`), comme dans Bash.

## 8. Commandes internes et externes

### 8.1 Internes (implémentées en C — 19 entrées)

`cd` (HOME, `-` avec affichage, mise à jour PWD/OLDPWD), `pwd`,
`echo` (avec `-n`), `exit` (code optionnel, contrôle numérique),
`help`, `export` (avec/sans valeur, validation d'identifiants),
`unset`, `env` (affiche `environ`), `set` (toute la table, triée),
`history` (`-c`, `N`), `alias`/`unalias` (`-a`), `jobs`, `fg` (`%N`),
`bg`, `umask` (octal), `type` (alias / interne / chemin PATH),
`source` et `.`.

### 8.2 Externes (lancées, jamais réimplémentées)

Tout exécutable de `PATH` fonctionne : `ls, cat, cp, mv, rm, mkdir,
rmdir, touch, grep, find, sort, uniq, wc, head, tail, more, printf, date,
whoami, id, uname, hostname, ps, top, kill, sleep, chmod, chown, ln,
stat, file, df, du, free, tar, gzip, gunzip, zip, unzip, ping, curl,
wget, gcc, python3, git...` L'environnement de développement (Debian 13,
GCC 14.2) ne fournit ni `less` ni `cal` : les demander produit
`commande introuvable` et le statut 127 — comportement normal documenté.

`type` reflète cette distinction : `type cd` → *commande interne*,
`type ls` → */usr/bin/ls*, `type gcc` → */usr/bin/gcc*.

## 9. Pipes et redirections

### 9.1 Tubes

`spawn_pipeline()` (`src/executor.c`) crée n−1 tubes pour n commandes.
Le fils i relié par `dup2()` : sortie du tube i−1 sur stdin, entrée du
tube i sur stdout. **Tous** les descripteurs de tubes sont ensuite fermés
— dans chaque fils et dans le père — sinon les lecteurs ne verraient
jamais la fin de fichier. Le statut du pipeline est celui de sa dernière
commande (`true | false` → 1, `false | true` → 0, comme Bash).

### 9.2 Redirections

`redirection_apply()` (`src/redirection.c`) applique les redirections
**dans l'ordre** : `<` (O_RDONLY), `>` (O_TRUNC), `>>` (O_APPEND),
`2>`/`2>>` (fd 2), `N>&M` (`dup2(M, N)`), `&>`/`&>>` (fds 1 et 2).
Les fichiers sont créés en 0666, filtrés par l'umask (testé : `umask
077` puis `> f` produit `-rw-------`).

Pour une commande interne redirigée (`pwd > f`), les descripteurs
0/1/2 sont sauvegardés (`dup`), la redirection appliquée, l'interne
exécutée, puis tout restauré — avec le piège des tampons stdio résolu
(voir difficultés, 13.2).

## 10. Variables et environnement

### 10.1 Deux niveaux, comme Bash

- `X=valeur` crée une **variable locale** dans la table du shell
  (`variables.c`) ;
- `export X` la copie dans `environ` via `setenv()` : elle devient
  visible des commandes externes (héritée par `execvp`) ;
- `unset X` la retire des deux niveaux ; une modification conserve le
  statut exporté ;
- `env` affiche l'environnement exporté, `set` affiche toute la table
  (triée pour une sortie stable) ;
- `X=v commande` crée une affectation **temporaire** : snapshot puis
  restauration pour une interne, `setenv()` dans le fils pour une
  externe.

### 10.2 Expansion des mots

L'expansion (appelée par l'exécuteur, après le parse) interprète chaque
mot brut : guillemets simples (littéral), doubles (variables expansées,
échappements), antislash, `$NOM`, `${NOM}`, `$?`, `$$`, paramètres
positionnels non gérés (chaîne vide). Le **tilde** n'est expansé qu'en
tête de mot non protégé : `~`, `~/Documents`, `~utilisateur`
(`getpwnam()`).

Les **jokers** `* ? [...]` sont développés par `glob()` uniquement si
des métacaractères non protégés sont apparus au fil de l'expansion —
y compris issus d'une variable (`X='*.c'; echo $X` liste les fichiers,
`echo "$X"` reste littéral). Sans correspondance, le mot reste littéral
(comportement Bash par défaut). La **règle des champs** est respectée :
un mot devenu entièrement vide et non protégé disparaît
(`echo a $INEXISTANTE b` → `a b`), un champ quoté vide reste
(`echo a "$INEXISTANTE" b` → `a  b`).

Les redirections utilisent la même expansion **sans jokers** : un nom
contenant des métacaractères non protégés produit `redirection ambiguë`
(statut 2), comme Bash.

## 11. Signaux et tâches

### 11.1 Dispositions du shell

| Signal | Interactif | Script |
|---|---|---|
| SIGINT (Ctrl+C) | drapeau + `\n` : le shell ne meurt jamais | défaut (Ctrl+C tue le script, comme Bash) |
| SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU | ignorés | ignorés (sauf SIGINT) |
| SIGCHLD | handler de récolte (voir 11.3) | idem |

Les signaux sont installés **sans `SA_RESTART`** : `read()` (invite) et
`waitpid()` (attente de premier plan) doivent être interrompus pour que
le shell réagisse immédiatement (notification de fin de tâche,
Ctrl+C...).

### 11.2 Fils : remise à zéro avant execvp

Un signal **ignoré survit à `exec()`** : sans remise à `SIG_DFL` dans le
fils, `sleep 30` lancé depuis le shell ignorerait Ctrl+C.
`process_reset_child_signals()` rétablit donc toutes les dispositions
juste avant `execvp()`.

### 11.3 Anti-zombies : anneau asynchrone-sûr

Le handler SIGCHLD n'a le droit d'appeler que des fonctions
asynchrone-sûres. Il se limite donc à `waitpid(-1, WNOHANG|WUNTRACED|
WCONTINUED)` en boucle et stocke les couples (pid, statut) dans un
anneau de 128 entrées en `volatile sig_atomic_t`. Le code principal
draine cet anneau aux points sûrs (avant l'invite, dans `jobs`, pendant
l'attente de premier plan) et met alors à jour la table des tâches.
Les événements arrivés entre `fork()` et l'enregistrement de la tâche
sont mis de côté puis rejoués — la course SIGCHLD/fork est éliminée.

### 11.4 Groupes de processus et terminal

Chaque pipeline (premier plan **ou** arrière-plan) forme un groupe :
`setpgid()` est appelé à la fois dans le fils et dans le père
(anti-course). Le terminal est donné (`tcsetpgrp`) **uniquement aux
pipelines de premier plan**, dès le premier fork, puis rendu au shell
après l'attente. Ainsi Ctrl+C/Ctrl+Z atteignent la commande, jamais le
shell — prouvé par les tests pty (section 12.5) : après Ctrl+C sur
`sleep 30`, `$?` vaut 130 et le shell répond à la commande suivante.

Ctrl+Z (SIGTSTP) suspend la commande : `jobs_wait()` constate
`WIFSTOPPED`, reprend le terminal, conserve la tâche en état
« Arrêtée ». `bg` envoie SIGCONT, `fg` envoie SIGCONT + rend le
terminal puis attend. Les notifications `[1]+ Terminée ...` s'affichent
avant l'invite, même pendant que le shell attend une saisie — grâce à
l'interruption de `read()` par SIGCHLD (voir difficultés 13.3).

## 12. Tests et résultats

### 12.1 Méthode

`make test` exécute la suite complète ; la sortie intégrale de la
dernière exécution est conservée dans `tests/results/run_all.log`. Tous
les tests destructifs s'exécutent dans un répertoire `mktemp -d` isolé,
avec `HOME` redéfini (l'historique et le rc de l'utilisateur ne sont
jamais touchés). Aucun résultat n'est fabriqué : les extraits ci-dessous
sont des sorties réelles.

### 12.2 Correspondance avec les 20 tests obligatoires

| # | Cas exigé | Test | Résultat |
|---|---|---|---|
| 1 | commande simple | T01 `echo bonjour` | PASS |
| 2 | commande avec arguments | T02 | PASS |
| 3 | guillemets | T04, T05, T06, T07, T08 | PASS |
| 4 | commande interne cd | T10–T14 | PASS |
| 5 | commande inconnue | T15 (statut 127), T16 (message) | PASS |
| 6 | variable d'environnement | T17–T26 | PASS |
| 7 | pipe simple | P01–P05 | PASS |
| 8 | chaîne de pipes | P06–P08 (jusqu'à 4 commandes) | PASS |
| 9 | redirection `>` | R01–R05 | PASS |
| 10 | redirection `>>` | R03 | PASS |
| 11 | redirection `<` | R06–R08 | PASS |
| 12 | redirection `2>` | R09–R11 | PASS |
| 13 | séquence `;` | T27, T34 | PASS |
| 14 | `&&` et `\|\|` | T28–T33 | PASS |
| 15 | arrière-plan | T35 (`&` + jobs En cours) | PASS |
| 16 | jobs | T35, T36, I07–I09 | PASS |
| 17 | Ctrl+C | I03–I05 (pty, `$? = 130`) | PASS |
| 18 | script multi-commandes | T58, T59 | PASS |
| 19 | erreurs de syntaxe | T62–T66 | PASS |
| 20 | erreurs d'accès fichiers | T67, T68, R07, R08 | PASS |

### 12.3 Résultats bruts (sortie réelle de `make test`)

```
=== Tests unitaires : lexer / parser / expansion ===
86 tests réussis, 0 échoués

=== Tests fonctionnels : commandes ===
Résultat : 71 réussis, 0 échoués

=== Tests fonctionnels : tubes ===
Résultat : 13 réussis, 0 échoués

=== Tests fonctionnels : redirections ===
Résultat : 22 réussis, 0 échoués

=== Tests interactifs (pseudo-terminal) ===
17/17 tests interactifs réussis

======================================
  TOUS LES TESTS ONT RÉUSSI
======================================
```

**Total : 209 tests, 0 échec.**

### 12.4 Extraits représentatifs (tests réels)

Session non interactive (tube) — guillemets, variables, opérateurs :

```
$ printf 'echo "Bonjour tout le monde"\nX=azerty\necho $X\nfalse || echo secours\n' \
    | ./alchimiste-shell
Bonjour tout le monde
azerty
secours
```

Ctrl+C puis Ctrl+Z sous pseudo-terminal (tests/test_interactive.py) :

```
PASS : Ctrl+C interrompt sleep (le shell survit)
PASS : code de retour 130 après SIGINT
PASS : Ctrl+Z suspend la commande (retour à l'invite)
PASS : jobs affiche la tâche arrêtée          →  [1]+  Arrêtée   sleep 5
PASS : bg relance en arrière-plan             →  [1] sleep 5 &
PASS : fg réaffiche la commande / attend la fin
PASS : notification « Terminée » avant l'invite
```

### 12.5 Tests mémoire : AddressSanitizer + UBSan + LeakSanitizer

valgrind n'étant pas disponible dans l'environnement de développement,
la vérification mémoire a utilisé `make debug`
(`-fsanitize=address,undefined`) : l'intégralité de la suite (209
tests) a été rejouée **sans aucune erreur d'exécution, aucun
comportement indéfini, aucune fuite détectée**, y compris sur les
chemins d'erreur (alias récursif, `${` non fermée, redirection ambiguë,
fichiers absents, `exit abc`).

## 13. Difficultés techniques

### 13.1 Un `read()` n'est pas une ligne

Premier test réel de la phase 1 : sur un **tube**, `read()` renvoie tout
ce qui est disponible — plusieurs lignes d'un coup. La version naïve
perdait les lignes suivantes. Correction : tampon d'entrée statique et
extraction ligne par ligne (`shell.c`), avec détection des lignes trop
longues (rejet et purge jusqu'au `\n`).

### 13.2 Le piège des tampons stdio et des redirections (bug critique)

Symptôme : `echo "première ligne" > notes.txt` laissait le fichier
**vide** et affichait la ligne... après coup. Cause : `printf()` écrit
dans un tampon mémoire alors que la redirection ne modifie que le
**descripteur** ; au moment du vidage (automatique, ou avant un fork),
le descripteur était déjà restauré — la sortie partait vers le terminal.
Nos tests de la phase 4 avaient passé « par hasard », l'ordre des vidages
mimant la sortie attendue. Correction : `fflush(stdout/stderr)` **avant**
d'appliquer une redirection (les données périmées partent vers l'ancienne
cible) et **après** l'exécution de l'interne (sa sortie part vers le
fichier). Ce cas est désormais couvert par les tests R01–R05.

### 13.3 Signaux : trois défauts trouvés par les tests pty

1. Le premier harnais de test pty ne faisait pas du shell un chef de
   session avec terminal de contrôle : `tcsetpgrp` échouait et Ctrl+C
   n'atteignait pas `sleep` (le shell restait bloqué 30 s dans
   `waitpid`). Correction du **harnais** (`setsid` + `TIOCSCTTY`) et,
   côté shell, diagnostic explicite si `tcsetpgrp` échoue.
2. Le terminal était donné **aussi** aux pipelines d'arrière-plan : le
   shell devenait un processus d'arrière-plan, sa lecture suivante
   échouait en EIO et il quittait proprement... immédiatement.
   Correction : terminal réservé au premier plan.
3. Les notifications « Terminée » n'apparaissaient pas : `read()`
   interrompu par SIGCHLD rebouclait sans drainer l'anneau. Correction :
   toute interruption de lecture rend la main à la boucle principale,
   qui récolte, notifie et réaffiche l'invite.

### 13.4 Course SIGCHLD / enregistrement des tâches

Un fils très court peut mourir avant que le père n'ait enregistré son
pid dans la table : l'événement était perdu et la tâche restait
« En cours » à vie. Correction : les événements orphelins sont mis de
côté puis **rejoués** au moment de l'enregistrement (`jobs.c`).

### 13.5 Choix : expansion séparée du lexage

Garder les mots bruts (guillemets inclus) dans le lexer et reporter
toute l'interprétation à une phase d'expansion distincte a simplifié le
traitement de `"$X"` (littéral) contre `$X` (jokers actifs) et de la
règle des champs vides — au prix d'une fonction d'expansion plus longue,
mais testée unitairement (37 tests).

## 14. Limites du projet

Limites **assumées et documentées** (comparaison avec Bash limitée au
sous-ensemble revendiqué) :

1. Pas de substitution de commandes (`$(...)`, `` `...` ``) ni de
   here-documents (`<<`) ;
2. Pas de découpage de mots sur les variables : `X="a b" ; ls $X`
   passe un seul argument `a b` (Bash en passe deux) ;
3. Alias expansé uniquement en tête d'une **commande unique** — pas
   dans un pipeline (`ll | wc` ne déclenche pas l'alias `ll`) ;
4. `&` s'applique au dernier pipeline de la ligne : `a && b &`
   n'arrière-planise que `b` (Bash arrière-planise toute la liste) ;
5. Pas de paramètres positionnels (`$1`, `$@`, `$#`), pas de `wait` ;
6. Pas d'édition de ligne (flèches, historique à la saisie) : lecture
   `read()` ligne à ligne — readline aurait été une dépendance externe ;
7. `env` interne n'exécute pas de commande (utiliser `/usr/bin/env`) ;
8. `set` n'offre pas d'options (`set -e`...) ;
9. limites volontaires : 4095 caractères par ligne, 256 arguments,
   32 commandes par pipeline, 1000 entrées d'historique ;
10. anneau SIGCHLD de 128 entrées : au-delà (rafale extrême), une mise
    à jour d'état peut être perdue — jamais de zombie pour autant ;
11. `fg`/`bg` acceptent `%N` et `N`, pas les spécifications par nom
    (`%sleep`) ;
12. le shell n'envoie pas SIGHUP aux tâches à sa sortie (comportement
    de Bash non reproduit) ; les tâches d'arrière-plan deviennent
    orphelines et sont adoptées par init.

## 15. Perspectives d'amélioration

1. Substitution de commandes `$(...)` et here-documents ;
2. découpage de mots et `IFS` ;
3. `wait` / paramètres positionnels pour les scripts ;
4. édition de ligne et complétion (readline ou implémentation maison
   en mode brut) ;
5. alias dans les pipelines et à toute position de commande ;
6. `set -e`, gestion d'erreurs de script plus fine ;
7. table des tâches persistante entre sessions ;
8. fuzzing du lexer (génération aléatoire de lignes) : le lexer est déjà
   borné par la taille de ligne, base saine pour ce type de tests.

## 16. Conclusion

Le cahier des charges est rempli : alchimiste-shell compile sans
avertissement sous GCC (C11, `-Wall -Wextra -Wpedantic`), exécute les
programmes du système, fournit 19 commandes internes, les opérateurs
`; && || | &`, toutes les redirections demandées, les variables et
expansions, l'historique, les scripts et `source`, et un contrôle des
tâches complet — groupes de processus et terminal réellement
implémentés, pas simulés. La robustesse est prouvée par 209 tests
automatisés exécutés avec succès, y compris sous AddressSanitizer,
UndefinedBehaviorSanitizer et LeakSanitizer.

Au-delà du résultat, la valeur du projet réside dans la méthode :
phases validées une à une (historique git conservé), tests réels dès la
première phase — qui ont révélé trois bugs authentiques (tampons stdio,
terminal en arrière-plan, notifications bloquées) qu'une simple
démonstration manuelle aurait laissés passer — et une documentation qui
distingue clairement ce qui est fait de ce qui ne l'est pas.

## 17. Répartition des tâches

*(à compléter par le groupe — proposition de répartition équilibrée)*

| Membre | Modules | Phases |
|---|---|---|
| `Étudiant 1` | lexer, parser, tests unitaires | 2 |
| `Étudiant 2` | executor, redirection, tubes | 1, 4 |
| `Étudiant 3` | variables, expansion, builtins, historique | 3 |
| `Étudiant 1` | process, jobs, signaux, tests pty | 5 |
| `Étudiant 2` | scripts, source, rc, exemple | 6 |
| `Étudiant 3` | suite de tests, sanitizer, corrections | 7 |
| ensemble | rapport, architecture, soutenance | 7 |

Chaque membre doit être capable de présenter n'importe quel module
(exigence de soutenance) ; la répartition ci-dessus reflète la
responsabilité première de chacun.

## 18. Références techniques

- **POSIX.1-2008 / Single UNIX Specification** — `fork`, `execvp`,
  `waitpid`, `pipe`, `dup2`, `setpgid`, `tcsetpgrp`, `sigaction`,
  `glob`, pages de manuel Linux sections 2 et 3 ;
- **Kerrisk, M.**, *The Linux Programming Interface*, No Starch Press —
  chapitres Process Control, Signals, Job Control ;
- **Stevens, W. R. & Rago, S. A.**, *Advanced Programming in the UNIX
  Environment*, 3e éd., Addison-Wesley ;
- **Bash Reference Manual** (GNU) — sémantique de référence des
  opérateurs, expansions et codes de sortie (126/127/130...) ;
- Documentation GCC : options `-Wall -Wextra -Wpedantic`,
  sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer,
  LeakSanitizer) ;
- Pages de manuel : `pty(7)` et le module Python `pty` pour les tests
  interactifs.

