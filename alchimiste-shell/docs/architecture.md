# Architecture d'alchimiste-shell

## Vue d'ensemble

Le shell est organisé en modules strictement séparés, chacun responsable
d'une étape du traitement d'une ligne de commande :

```
              ligne saisie
                  │
 ┌────────────────▼────────────────┐
 │ shell.c      boucle REPL        │  lecture (read), invite, historique
 └────────────────┬────────────────┘
                  │
 ┌────────────────▼────────────────┐
 │ lexer.c      analyse lexicale   │  ligne -> suite de tokens
 └────────────────┬────────────────┘
                  │
 ┌────────────────▼────────────────┐
 │ parser.c     analyse syntaxique │  tokens -> arbre de commandes
 └────────────────┬────────────────┘
                  │
 ┌────────────────▼────────────────┐
 │ executor.c   exécution          │  parcours ; && || ; ; fork/exec
 │   ├── variables.c  expansion    │  $ ~ jokers -> arguments finaux
 │   ├── redirection.c             │  open/dup2/close
 │   ├── process.c    signaux      │  SIGINT/SIGCHLD, terminal
 │   └── jobs.c       tâches       │  table, jobs/fg/bg
 └────────────────┬────────────────┘
                  │
 ┌────────────────▼────────────────┐
 │ builtins.c   commandes internes │  cd, pwd, export, jobs, ...
 └─────────────────────────────────┘
```

`util.c` fournit les allocations vérifiées et la recherche dans PATH.

## Flux de traitement d'une ligne

1. **Lecture** (`shell.c`) : `read()` brut sur stdin avec un tampon de
   lignes (un `read()` sur un tube peut renvoyer plusieurs lignes).
   Interrompu par tout signal → retour à la boucle principale, qui
   récolte les enfants et réaffiche l'invite.
2. **Lexage** (`lexer.c`) : découpe en tokens en respectant les
   guillemets et antislashes. Les mots sont conservés **bruts**
   (guillemets inclus) ; les opérateurs (`; | & && || < > >> >& &>`)
   et les nombres de descripteurs (`2` de `2>`) deviennent des tokens
   dédiés.
3. **Analyse** (`parser.c`) : descente récursive selon la grammaire
   `ligne := liste EOF`, `liste := et_ou ((';'|'&') et_ou)*`,
   `et_ou := pipeline (('&&'|'||') pipeline)*`,
   `pipeline := simple ('|' simple)*`, `simple := (mot|redirection)+`.
   Résultat : une liste chaînée de `Node` contenant des `Pipeline`,
   chacun contenant des `Cmd` (mots bruts + `Redir`).
4. **Expansion** (`variables.c`, appelée par `executor.c`) : par mot,
   interprétation des guillemets, `$VAR`/`${}`/`$?`/`$$`, tilde, puis
   jokers (`glob()`) — uniquement si des métacaractères non protégés
   sont apparus. Un mot devenu vide et non protégé disparaît.
5. **Exécution** (`executor.c`) :
   - affectations seules → persistantes dans le shell ;
   - commande interne seule → dans le processus du shell (avec
     sauvegarde/restauration des descripteurs si redirections) ;
   - sinon → `spawn_pipeline` : tubes, forks, groupe de processus,
     terminal, enregistrement dans la table des tâches, attente.
6. **Libération** : l'arbre, les formes expansées et la ligne sont
   libérés après chaque commande.

## Exécution d'un pipeline (détail)

```
        ls -l  |  grep .c  |  wc -l
        ─────────────────────────────
 père   pipe(p0) pipe(p1)
        fork ──► fils 0 : setpgid(0,0)
                     dup2(p0[1], 1)
                     ferme tous les tubes
                     redirections éventuelles
                     execvp("ls")
        fork ──► fils 1 : setpgid(0, pgid0)
                     dup2(p0[0], 0) ; dup2(p1[1], 1)
                     execvp("grep")
        fork ──► fils 2 : setpgid(0, pgid0)
                     dup2(p1[0], 0)
                     execvp("wc")
        ferme TOUS les descripteurs de tubes   ← sinon EOF impossible
        job_add(pgid0, [pids], cmdline)
        premier plan : tcsetpgrp(pgid0) ; jobs_wait()
        arrière-plan : affiche [N] PID, rend la main
```

Points clés :
- le `setpgid` est fait **à la fois** dans le fils et dans le père, pour
  éliminer la course entre `fork()` et `execvp()` ;
- le terminal (`tcsetpgrp`) n'est donné **qu'aux pipelines de premier
  plan** ; le shell le reprend après l'attente ;
- le statut du pipeline est celui de sa **dernière** commande (comme Bash) ;
- les commandes internes placées dans un pipeline s'exécutent dans leur
  fils (sémantique Bash : `history | grep cd` ne modifie pas le shell).

## Signaux et récolte des enfants

```
 ┌──────────────────── processus shell (interactif) ────────────────────┐
 │ SIGINT   → drapeau + write("\n") : le shell ne meurt jamais          │
 │ SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU → ignorés (comme Bash)            │
 │ SIGCHLD  → waitpid(-1, WNOHANG|WUNTRACED|WCONTINUED) en boucle,      │
 │            résultats stockés dans un anneau (async-signal-safe),     │
 │            drainés par le code principal                             │
 └───────────────────────────────────────────────────────────────────────┘
 ┌──────────────────── fils avant execvp ───────────────────────────────┐
 │ reset des dispositions : un signal IGNORÉ survit à exec() !          │
 │ (sinon « sleep 30 » ignorerait Ctrl+C)                               │
 └───────────────────────────────────────────────────────────────────────┘
```

Le handler SIGCHLD n'appelle que `waitpid` et des écritures `volatile` :
aucune fonction non asynchrone-sûre. La mise à jour réelle de la table
des tâches (`printf`, `malloc`) se fait dans le code principal, aux
points sûrs : avant l'invite, dans `jobs`, pendant l'attente de premier
plan. Les événements arrivés entre `fork()` et l'enregistrement de la
tâche sont mis de côté puis rejoués.

## Terminal et groupes de processus

- chaque pipeline de premier plan forme un groupe (`setpgid`) qui reçoit
  le terminal (`tcsetpgrp`) : Ctrl+C / Ctrl+Z atteignent la commande,
  pas le shell ;
- après suspension (Ctrl+Z → SIGTSTP), `jobs_wait` constate
  `WIFSTOPPED`, reprend le terminal et conserve la tâche (état
  « Arrêtée ») ;
- `fg` renvoie SIGCONT + le terminal puis attend ; `bg` envoie SIGCONT ;
- en mode script (stdin ≠ terminal), pas de `setpgid` ni de `tcsetpgrp` :
  Ctrl+C tue le shell et ses commandes, exactement comme un script Bash.

## Variables : deux niveaux

- table interne (`variables.c`) : `X=v` crée une variable locale ;
- `export X` la copie dans `environ` (`setenv`) → visible des enfants ;
- `unset` la retire des deux ; `env` affiche `environ`, `set` affiche
  toute la table ;
- `X=v commande` : affectation temporaire (snapshot/restauration pour
  une interne, `setenv` dans le fils pour une externe).

## Historique

En mémoire pendant la session (capacité 1000), sauvegardé en mode
interactif dans `~/.alchimiste_history` — un fichier **dédié** :
`.bash_history` n'est jamais lu ni écrit. `history`, `history -c`,
`history N`.

## Propriété de la mémoire

Chaque producteur est responsable de sa libération : tokens (lexer),
arbre (parser), formes expansées (executor), tâches, variables, alias et
historique (shell_cleanup). Les suites de tests passent sous
LeakSanitizer sans aucune fuite détectée.
