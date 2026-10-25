# alchimiste-shell — mini-shell Linux en C

Projet universitaire de Sciences des Systèmes d'Exploitation (SCE) :
un interpréteur de commandes fonctionnel écrit en C11/POSIX, compilé avec
GCC sans avertissement (`-Wall -Wextra -Wpedantic`), sans aucune
bibliothèque externe.

```
╔══════════════════════════════════════╗
║           ALCHIMISTE SHELL            ║
║        Mini-shell Linux en C          ║
╚══════════════════════════════════════╝
azaria@ubuntu:~$ mkdir test
azaria@ubuntu:~$ cd test
azaria@ubuntu:~/test$ echo "Bonjour Linux"
Bonjour Linux
azaria@ubuntu:~/test$ printf 'b\na\n' | sort | uniq -c
      1 a
      1 b
azaria@ubuntu:~/test$ sleep 30 &
[1] 12345
azaria@ubuntu:~/test$ jobs
[1]+  En cours    sleep 30 &
azaria@ubuntu:~/test$ exit
Au revoir !
```

## Compilation et exécution

```bash
make            # compile (GCC, C11, -Wall -Wextra -Wpedantic)
make run        # lance le shell en interactif
make test       # suite complète : 209 tests (unitaires + fonctionnels + pty)
make debug      # recompile avec AddressSanitizer + UndefinedBehaviorSanitizer
make clean      # supprime les objets et l'exécutable
```

L'exécutable produit est `alchimiste-shell` :

```bash
./alchimiste-shell                    # mode interactif
./alchimiste-shell examples/exemple.sh  # mode script
```

## Utilisation

- **Mode interactif** : invite `utilisateur@hôte:chemin$` (le répertoire
  personnel est abrégé en `~`). `exit` ou Ctrl+D quitte.
- **Mode script** : `./alchimiste-shell script.sh` exécute les lignes du
  fichier ; commentaires `#`, lignes vides, variables, opérateurs.
- **source** : `source fichier` (ou `. fichier`) exécute le fichier dans
  le processus courant — `cd`, variables et alias persistent.
- **~/.alchimiste_rc** : s'il existe, il est exécuté au démarrage
  interactif (alias personnels, etc.).
- **Historique** : sauvegardé dans `~/.alchimiste_history` (fichier
  dédié — l'historique personnel de Bash n'est **jamais** touché).

## Opérateurs pris en charge

| Opérateur | Signification |
|-----------|---------------|
| `;`       | exécution séquentielle |
| `&&`      | la suivante ne s'exécute que si la précédente réussit |
| `\|\|`    | la suivante ne s'exécute que si la précédente échoue |
| `\|`      | tube entre plusieurs commandes |
| `&`       | exécution en arrière-plan |
| `>` `>>`  | sortie standard (écraser / ajouter) |
| `<`       | entrée standard |
| `2>` `2>>`| sortie d'erreurs (écraser / ajouter) |
| `2>&1` `N>&M` | duplication de descripteurs |
| `&>` `&>>`| sortie ET erreurs vers un fichier |

## Commandes internes (implémentées en C)

`cd` · `pwd` · `echo` · `exit` · `help` · `export` · `unset` · `env` ·
`set` · `history` · `alias` · `unalias` · `jobs` · `fg` · `bg` · `umask` ·
`type` · `source` (et `.`)

`help` les décrit toutes. `type NOM` indique comment NOM serait interprété.

## Expansions

- Variables : `$NOM`, `${NOM}`, `$?` (statut précédent), `$$` (PID du shell) ;
- guillemets simples (tout littéral) et doubles (variables expansées,
  `\"` `\\` `\$` protégés), antislash d'échappement ;
- tilde : `~`, `~/chemin`, `~utilisateur` ;
- jokers : `*`, `?`, `[...]` via `glob()`, triés ; sans correspondance le
  mot reste littéral (comportement Bash par défaut).

## Commandes externes

Tout exécutable trouvé dans `PATH` est lancé par `fork()` + `execvp()` :
`ls, cat, cp, mv, rm, mkdir, grep, find, sort, uniq, wc, head, tail, date,
ps, kill, sleep, chmod, gcc, python3, git...` — le shell n'implémente
**aucun** de ces programmes, il les exécute. Si une commande n'est pas
installée (ex. `less`, `cal` absents de l'environnement de développement),
le shell le signale : `commande introuvable`, statut 127.

## Contrôle des tâches

- Ctrl+C interrompt la commande de premier plan, **jamais le shell**
  (`$?` vaut ensuite 130) ;
- Ctrl+Z suspend la commande (état « Arrêtée ») ;
- `jobs` liste les tâches, `fg %N` les ramène au premier plan, `bg %N` les
  relance en arrière-plan ;
- les notifications `[1]+ Terminée ...` s'affichent avant l'invite ;
- aucun zombie : les enfants sont récoltés par un gestionnaire SIGCHLD.

## Limites connues (documentées honnêtement)

- pas de substitution de commandes `` `...` `` ou `$(...)`, ni de here-doc ;
- pas de découpage de mots sur les variables (`$X` contenant des espaces
  reste un seul argument) ;
- alias expansés uniquement en tête d'une commande **unique** (pas dans un
  pipeline) ;
- `&` s'applique au dernier pipeline d'une ligne (`a && b &` ≠ Bash) ;
- pas de paramètres positionnels (`$1`, `$@`, `$#`) ni de `wait` ;
- pas d'édition de ligne (flèches) : lecture ligne par ligne via `read()` ;
- `env` interne n'accepte pas d'argument (utiliser `/usr/bin/env`) ;
- une ligne est limitée à 4095 caractères, 256 arguments, 32 commandes par
  pipeline (limites volontaires, messages d'erreur explicites).

## Sécurité

- le shell ne doit **pas** être exécuté en administrateur ;
- `rm` et les commandes destructrices conservent leur comportement normal —
  tester dans un répertoire temporaire (les tests le font : `mktemp -d`) ;
- aucune commande inconnue n'est exécutée silencieusement (statut 127) ;
- `system()` n'est jamais utilisé : l'interprétation est intégralement
  écrite en C.

## Structure du projet

```
alchimiste-shell/
├── src/
│   ├── main.c          point d'entrée
│   ├── shell.c/.h      boucle REPL, lecture, invite, scripts, source
│   ├── lexer.c/.h      analyse lexicale (tokens)
│   ├── parser.c/.h     analyse syntaxique (arbre de commandes)
│   ├── executor.c/.h   exécution, tubes, fork/execvp/waitpid
│   ├── builtins.c/.h   commandes internes
│   ├── redirection.c/.h open/dup2/close
│   ├── process.c/.h    signaux, terminal, récolte des enfants
│   ├── jobs.c/.h       contrôle des tâches (jobs/fg/bg)
│   ├── variables.c/.h  variables, alias, expansion des mots
│   ├── history.c/.h    historique
│   └── util.c/.h       allocations vérifiées, recherche PATH
├── tests/              209 tests (voir ci-dessous)
├── docs/               rapport, architecture, soutenance
├── examples/exemple.sh démonstration
├── Makefile
└── README.md
```

## Tests

`make test` exécute :

| suite | contenu | résultat |
|-------|---------|----------|
| tests/test_parser.c | 86 tests unitaires : lexer, parser, expansion | 86/86 |
| tests/test_commands.sh | 71 tests : commandes, internes, variables, opérateurs, arrière-plan, scripts, erreurs | 71/71 |
| tests/test_pipes.sh | 13 tests : tubes simples et chaînés | 13/13 |
| tests/test_redirections.sh | 22 tests : toutes les redirections | 22/22 |
| tests/test_interactive.py | 17 tests sous pseudo-terminal : Ctrl+C, Ctrl+Z, jobs/fg/bg | 17/17 |

La sortie complète de la dernière exécution est conservée dans
`tests/results/run_all.log`. Les mêmes suites passent sous
AddressSanitizer / UndefinedBehaviorSanitizer / LeakSanitizer sans aucune
erreur ni fuite.

## Documentation

- `docs/rapport.md` — rapport universitaire complet (architecture, tests
  réels, difficultés, limites, perspectives) ;
- `docs/architecture.md` — description détaillée des modules et du flux ;
- `docs/soutenance.md` — préparation de la soutenance (plan, démo,
  questions probables, comparaison avec Bash).
