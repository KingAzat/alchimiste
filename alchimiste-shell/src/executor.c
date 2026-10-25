/*
 * executor — exécution de l'arbre de commandes.
 *
 * Phase 5 :
 *   - chaque pipeline devient un groupe de processus (setpgid) ;
 *   - un pipeline de premier plan reçoit le terminal (tcsetpgrp) :
 *     Ctrl+C / Ctrl+Z atteignent les commandes, jamais le shell ;
 *   - arrière-plan (« & ») : tâche enregistrée, PID affiché, aucun
 *     processus zombie (récolte SIGCHLD) ;
 *   - tubes, redirections, internes, affectations et alias des phases
 *     précédentes.
 */
#include "executor.h"
#include "shell.h"
#include "builtins.h"
#include "variables.h"
#include "redirection.h"
#include "jobs.h"
#include "process.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ------------------------------------------------------------------ */
/* Commande après expansion                                             */
/* ------------------------------------------------------------------ */

typedef struct XCmd {
    char **argv;    /* arguments expansés, terminé par NULL */
    int    argc;
    char **anames;  /* affectations de tête : noms */
    char **avalues; /* ... et valeurs déjà expansées */
    int    nasgn;
    Redir *redirs;  /* copie des redirections, fichiers expansés */
} XCmd;

static void redirs_free(Redir *r)
{
    while (r != NULL) {
        Redir *next = r->next;

        free(r->filename);
        free(r);
        r = next;
    }
}

static void xcmd_free(XCmd *x)
{
    int i;

    for (i = 0; i < x->argc; i++)
        free(x->argv[i]);
    free(x->argv);
    for (i = 0; i < x->nasgn; i++) {
        free(x->anames[i]);
        free(x->avalues[i]);
    }
    free(x->anames);
    free(x->avalues);
    redirs_free(x->redirs);
    memset(x, 0, sizeof *x);
}

/*
 * Copie les redirections en expansant les noms de fichiers (sans jokers :
 * une redirection contenant des métacaractères non protégés est ambiguë).
 */
static Redir *redirs_copy_expand(const Cmd *c)
{
    Redir *head = NULL;
    Redir **tail = &head;
    const Redir *r;

    for (r = c->redirs; r != NULL; r = r->next) {
        Redir *nr = xmalloc(sizeof *nr);

        *nr = *r; /* le champ filename d'origine reste la propriété du parser */
        nr->next = NULL;

        if (r->filename != NULL) {
            int meta = 0;
            char *name = expand_string(r->filename, &meta);

            if (name == NULL) { /* erreur de substitution */
                redirs_free(head);
                free(nr);
                return NULL;
            }
            if (meta || name[0] == '\0') {
                fprintf(stderr, "alchimiste-shell: redirection ambiguë : "
                                "« %s »\n", r->filename);
                free(name);
                redirs_free(head);
                free(nr);
                return NULL;
            }
            nr->filename = name;
        }
        *tail = nr;
        tail = &nr->next;
    }
    return head;
}

/*
 * Expande une commande : sépare les affectations de tête, expande chaque
 * mot restant (0 à n champs par mot à cause des jokers) et copie les
 * redirections avec fichiers expansés. Retourne 0, ou -1 si erreur.
 */
static int xcmd_build(const Cmd *c, XCmd *x)
{
    int i, k;
    int assign_zone = 1;

    memset(x, 0, sizeof *x);
    x->argv = xmalloc(sizeof(char *) * (MAX_ARGS + 1));
    x->anames = xmalloc(sizeof(char *));
    x->avalues = xmalloc(sizeof(char *));

    for (i = 0; i < c->argc; i++) {
        const char *raw = c->argv[i];

        if (assign_zone && expand_is_assignment(raw)) {
            const char *eq = strchr(raw, '=');
            char *name = xstrndup(raw, (size_t)(eq - raw));
            char *value = expand_string(eq + 1, NULL);

            if (value == NULL) {
                free(name);
                xcmd_free(x);
                return -1;
            }
            x->anames = xrealloc(x->anames,
                                 sizeof(char *) * (size_t)(x->nasgn + 1));
            x->avalues = xrealloc(x->avalues,
                                  sizeof(char *) * (size_t)(x->nasgn + 1));
            x->anames[x->nasgn] = name;
            x->avalues[x->nasgn] = value;
            x->nasgn++;
            continue;
        }
        assign_zone = 0;

        {
            int nf = 0;
            char **f = expand_word(raw, 0, &nf, NULL);

            if (f == NULL) { /* erreur d'expansion (substitution) */
                xcmd_free(x);
                return -1;
            }
            for (k = 0; k < nf; k++) {
                if (x->argc >= MAX_ARGS) {
                    fprintf(stderr, "alchimiste-shell: trop d'arguments "
                                    "(maximum %d)\n", MAX_ARGS);
                    for (; k < nf; k++)
                        free(f[k]);
                    free(f);
                    xcmd_free(x);
                    return -1;
                }
                x->argv[x->argc++] = f[k]; /* transfert de propriété */
            }
            free(f);
        }
    }
    x->argv[x->argc] = NULL;

    if (c->redirs != NULL) {
        x->redirs = redirs_copy_expand(c);
        if (x->redirs == NULL) {
            xcmd_free(x);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Expansion des alias                                                  */
/* ------------------------------------------------------------------ */

/*
 * Si le premier mot d'une commande unique est un alias, on reconstruit la
 * ligne « valeur_de_l_alias autres_arguments » et on ré-exécute l'ensemble
 * (lexage + parse + exécution). La profondeur est bornée pour détecter
 * les alias récursifs.
 */
static int try_alias_expansion(const Pipeline *pl)
{
    const Cmd *c;
    const char *first;
    const char *value;
    size_t len;
    int i;
    char *joined;

    if (pl->n != 1 || pl->cmds[0]->argc == 0)
        return 0; /* rien à faire */

    c = pl->cmds[0];
    first = c->argv[0];

    /* un mot protégé (« "ls" ») ne déclenche pas l'alias */
    if (first[0] == '\'' || first[0] == '"' || first[0] == '\\')
        return 0;

    value = alias_get(first);
    if (value == NULL)
        return 0;

    if (g_shell.alias_depth >= ALIAS_MAX_DEPTH) {
        fprintf(stderr, "alchimiste-shell: alias « %s » récursif\n", first);
        return -1;
    }

    len = strlen(value);
    for (i = 1; i < c->argc; i++)
        len += 1 + strlen(c->argv[i]);
    joined = xmalloc(len + 1);
    strcpy(joined, value);
    for (i = 1; i < c->argc; i++) {
        strcat(joined, " ");
        strcat(joined, c->argv[i]);
    }

    g_shell.alias_depth++;
    shell_eval_no_history(joined); /* ré-analyse et exécution complètes */
    g_shell.alias_depth--;

    free(joined);
    return 1; /* la commande a été traitée */
}

/* ------------------------------------------------------------------ */
/* Représentation d'un pipeline pour la table des tâches                */
/* ------------------------------------------------------------------ */

static char *pipeline_repr(const Pipeline *pl)
{
    size_t len = 32;
    char *s;
    int i, j;
    const Redir *r;

    for (i = 0; i < pl->n; i++) {
        for (j = 0; j < pl->cmds[i]->argc; j++)
            len += strlen(pl->cmds[i]->argv[j]) + 1;
        for (r = pl->cmds[i]->redirs; r != NULL; r = r->next)
            len += 32 + (r->filename != NULL ? strlen(r->filename) : 0);
        len += 4;
    }
    if (pl->background)
        len += 4;

    s = xmalloc(len);
    s[0] = '\0';
    for (i = 0; i < pl->n; i++) {
        if (i > 0)
            strcat(s, " | ");
        for (j = 0; j < pl->cmds[i]->argc; j++) {
            if (j > 0)
                strcat(s, " ");
            strcat(s, pl->cmds[i]->argv[j]);
        }
        for (r = pl->cmds[i]->redirs; r != NULL; r = r->next) {
            char buf[32];

            switch (r->kind) {
            case RDIR_IN:
                snprintf(buf, sizeof buf, " %d<", r->fd);
                break;
            case RDIR_OUT:
                snprintf(buf, sizeof buf, " %d>", r->fd);
                break;
            case RDIR_APPEND:
                snprintf(buf, sizeof buf, " %d>>", r->fd);
                break;
            case RDIR_DUP:
                snprintf(buf, sizeof buf, " %d>&%d", r->fd, r->fd_to);
                break;
            default: /* RDIR_ALL */
                if (r->append)
                    strcat(s, " &>>");
                else
                    strcat(s, " &>");
                buf[0] = '\0';
                break;
            }
            strcat(s, buf);
            if (r->filename != NULL)
                strcat(s, r->filename);
        }
    }
    if (pl->background)
        strcat(s, " &");
    return s;
}

/* ------------------------------------------------------------------ */
/* Pipeline : fork, tubes, redirections, groupe, terminal, tâche        */
/* ------------------------------------------------------------------ */

/*
 * Crée n-1 tubes et n fils. Le fils i lit sur le tube i-1 et écrit dans
 * le tube i ; les descripteurs inutilisés sont fermés partout.
 *
 * Contrôle des tâches (mode interactif) :
 *   - le premier fils crée le groupe de processus du pipeline ;
 *   - les autres le rejoignent (setpgid côté père ET côté fils, pour
 *     éviter toute course) ;
 *   - le terminal est donné au groupe dès le premier fork : Ctrl+C et
 *     Ctrl+Z atteignent la commande, pas le shell.
 *
 * Une commande interne placée dans un pipeline s'exécute dans son fils
 * (sémantique de Bash). Le statut rendu est celui de la DERNIÈRE
 * commande du pipeline.
 */
static int spawn_pipeline(Pipeline *pl, XCmd *xs)
{
    int n = pl->n;
    int interactive = g_shell.interactive;
    /* Le terminal n'est donné qu'à un pipeline de PREMIER plan : une
     * tâche d'arrière-plan ne doit jamais posséder le terminal, sinon
     * le shell deviendrait lui-même un processus d'arrière-plan et ne
     * pourrait plus lire l'entrée (EIO). */
    int want_terminal = interactive && !pl->background;
    int (*pfd)[2] = NULL;
    pid_t pids[MAX_PIPE_CMDS];
    pid_t pgid = 0;
    pid_t pid;
    int i, k, a;
    int status = 0;
    int last = 0;
    char *repr;
    Job *job;

    if (n > 1) {
        pfd = xmalloc(sizeof(int[2]) * (size_t)(n - 1));
        for (i = 0; i < n - 1; i++) {
            if (pipe(pfd[i]) < 0) {
                fprintf(stderr, "alchimiste-shell: pipe : %s\n",
                        strerror(errno));
                for (k = 0; k < i; k++) {
                    close(pfd[k][0]);
                    close(pfd[k][1]);
                }
                free(pfd);
                return 1;
            }
        }
    }

    for (i = 0; i < n; i++) {
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "alchimiste-shell: fork : %s\n", strerror(errno));
            /* Nettoyage : tue et attend les fils déjà créés. */
            if (interactive && pgid > 0) {
                kill(-pgid, SIGKILL);
                while (waitpid(-pgid, &status, 0) > 0)
                    ;
            } else {
                for (k = 0; k < i; k++)
                    kill(pids[k], SIGKILL);
                for (k = 0; k < i; k++)
                    waitpid(pids[k], &status, 0);
            }
            for (k = 0; k < n - 1; k++) {
                close(pfd[k][0]);
                close(pfd[k][1]);
            }
            free(pfd);
            return 1;
        }

        if (pid == 0) {
            /* ---------------- processus fils ---------------- */
            if (interactive) {
                if (i == 0)
                    setpgid(0, 0);   /* crée le groupe du pipeline  */
                else
                    setpgid(0, pgid); /* rejoint le groupe          */
            }
            process_reset_child_signals(); /* avant execvp ! */

            if (i > 0)
                dup2(pfd[i - 1][0], STDIN_FILENO); /* entrée = tube amont */
            if (i < n - 1)
                dup2(pfd[i][1], STDOUT_FILENO);    /* sortie = tube aval  */
            for (k = 0; k < n - 1; k++) {          /* fermer l'inutile    */
                close(pfd[k][0]);
                close(pfd[k][1]);
            }

            for (a = 0; a < xs[i].nasgn; a++)
                setenv(xs[i].anames[a], xs[i].avalues[a], 1);

            if (redirection_apply(xs[i].redirs) < 0) {
                fflush(stdout);
                fflush(stderr);
                _exit(1);
            }

            if (xs[i].argc > 0) {
                int bi = builtin_index(xs[i].argv[0]);

                if (bi >= 0) { /* interne dans un pipeline : dans le fils */
                    int st =
                        builtin_dispatch_idx(bi, xs[i].argc, xs[i].argv);

                    fflush(stdout);
                    fflush(stderr);
                    _exit(st);
                }
                execvp(xs[i].argv[0], xs[i].argv);
                if (errno == ENOENT || errno == ENOTDIR) {
                    fprintf(stderr, "alchimiste-shell: commande introuvable "
                                    ": %s\n", xs[i].argv[0]);
                    _exit(127);
                }
                fprintf(stderr, "alchimiste-shell: %s : %s\n", xs[i].argv[0],
                        strerror(errno));
                _exit(126);
            }
            _exit(0); /* redirections/affectations seules */
        }

        /* ---------------- processus père ---------------- */
        pids[i] = pid;
        if (interactive) {
            if (i == 0) {
                pgid = pid;
                setpgid(pid, pid); /* confirme côté père (anti-course) */
                if (want_terminal)
                    process_give_terminal(pgid); /* premier plan seulement */
            } else {
                setpgid(pid, pgid);
            }
        } else if (i == 0) {
            pgid = pid; /* suivi sans contrôle de terminal (mode script) */
        }
    }

    /* Le père ferme TOUS les descripteurs de tubes, sinon les lecteurs
     * ne verraient jamais la fin de fichier. */
    for (k = 0; k < n - 1; k++) {
        close(pfd[k][0]);
        close(pfd[k][1]);
    }

    /* Enregistrement de la tâche (numéro, ligne, processus). */
    repr = pipeline_repr(pl);
    job = job_add(pgid, pids, n, repr);
    free(repr);

    if (pl->background) {
        /* Arrière-plan : pas d'attente, la tâche est suivie par la table. */
        if (interactive)
            fprintf(stderr, "[%d] %d\n", job_id(job), (int)pids[n - 1]);
        last = 0;
    } else {
        /* Premier plan : le shell attend en gardant la main sur les
         * signaux (jobs_wait gère le terminal et les arrêts Ctrl+Z). */
        last = jobs_wait(job);
        if (job_all_done(job))
            job_remove(job); /* terminée : retirée de la table */
        /* si suspendue : conservée, visible par « jobs », « fg », « bg » */
    }

    free(pfd);
    return last;
}

/* ------------------------------------------------------------------ */
/* Commande unique sans tube : interne dans le shell, sinon fils        */
/* ------------------------------------------------------------------ */

static int execute_expanded(Pipeline *pl, XCmd *x)
{
    int i, bi, st;
    VarSnapshot *snap;
    int fds[3];

    /* Affectations seules (« X=1 ») : persistantes dans le shell. */
    if (x->argc == 0) {
        for (i = 0; i < x->nasgn; i++)
            var_set(x->anames[i], x->avalues[i]);
        if (x->nasgn > 0)
            return 0;
        /* Redirections seules (« > f ») : crée simplement le fichier. */
        if (redirection_save(fds) != 0)
            return 1;
        fflush(stdout); /* données périmées vers l'ancienne cible */
        fflush(stderr);
        st = (redirection_apply(x->redirs) < 0) ? 1 : 0;
        redirection_restore(fds);
        return st;
    }

    /* Commande interne : exécutée dans le processus du shell. */
    bi = builtin_index(x->argv[0]);
    if (bi >= 0) {
        if (redirection_save(fds) != 0)
            return 1;
        st = 1;
        if (redirection_apply(x->redirs) == 0) {
            /*
             * Piège classique des tampons stdio : printf() écrit dans un
             * tampon mémoire, alors que la redirection ne modifie que le
             * descripteur. Il faut donc :
             *   1. vider le tampon AVANT, pour que les données déjà
             *      produites partent vers l'ancienne cible ;
             *   2. vider le tampon APRÈS l'interne, pour que SA sortie
             *      aille bien dans le fichier de redirection.
             */
            fflush(stdout);
            fflush(stderr);
            snap = (x->nasgn > 0)
                       ? vars_temp_assign(x->nasgn, x->anames, x->avalues)
                       : NULL;
            st = builtin_dispatch_idx(bi, x->argc, x->argv);
            fflush(stdout);
            fflush(stderr);
            vars_temp_restore(snap, x->nasgn);
        }
        redirection_restore(fds);
        return st;
    }

    /* Commande externe : passe par le mécanisme complet (groupe,
     * terminal, tâche) afin que Ctrl+C fonctionne aussi ici. */
    {
        XCmd xs[1];

        xs[0] = *x; /* le pipeline n'a qu'une commande */
        return spawn_pipeline(pl, xs);
    }
}

/* ------------------------------------------------------------------ */
/* Exécution d'un pipeline                                              */
/* ------------------------------------------------------------------ */

static int execute_pipeline(Pipeline *pl)
{
    XCmd *xs;
    int i, st;
    int aliased;

    aliased = try_alias_expansion(pl);
    if (aliased < 0)
        return 1;
    if (aliased == 1)
        return g_shell.last_status;

    /* Expansion de toutes les commandes du pipeline. */
    xs = xcalloc((size_t)pl->n, sizeof *xs);
    for (i = 0; i < pl->n; i++) {
        if (xcmd_build(pl->cmds[i], &xs[i]) < 0) {
            int j;

            for (j = 0; j < i; j++)
                xcmd_free(&xs[j]);
            free(xs);
            return 2; /* erreur d'expansion ou de redirection ambiguë */
        }
    }

    if (pl->n == 1 && (xs[0].argc == 0 ||
                       builtin_index(xs[0].argv[0]) >= 0)) {
        st = execute_expanded(pl, &xs[0]);
    } else {
        st = spawn_pipeline(pl, xs);
    }

    for (i = 0; i < pl->n; i++)
        xcmd_free(&xs[i]);
    free(xs);
    return st;
}

/* ------------------------------------------------------------------ */
/* Parcours de l'arbre                                                  */
/* ------------------------------------------------------------------ */

int executor_run(CommandLine *cl)
{
    Node *n;
    int skip = 0;
    int status = g_shell.last_status;

    for (n = cl->head; n != NULL; n = n->next) {
        if (!skip)
            status = execute_pipeline(n->pl);

        /*
         * Mise à jour immédiate : Bash met « $? » à jour après CHAQUE
         * commande, pas après chaque ligne. Sans cela, « false; echo $? »
         * expanse « $? » avec la valeur de la ligne précédente (0).
         */
        g_shell.last_status = status;

        /* Le séparateur décide si le noeud suivant sera exécuté. */
        switch (n->sep) {
        case SEP_AND:
            skip = (status != 0);
            break;
        case SEP_OR:
            skip = (status == 0);
            break;
        default: /* SEP_SEQ */
            skip = 0;
            break;
        }
    }
    return status;
}
