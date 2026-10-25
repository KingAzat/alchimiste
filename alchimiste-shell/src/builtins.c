/*
 * builtins — commandes internes du shell.
 *
 * Phase 1 : cd, pwd, echo, exit, help.
 * Les phases suivantes ajoutent : export, unset, env, set, history,
 * alias, unalias, jobs, fg, bg, umask, type, source.
 *
 * Chaque fonction reçoit argc/argv comme un programme C classique
 * (argv[0] = nom de la commande interne).
 */
#include "builtins.h"
#include "shell.h"
#include "variables.h"
#include "history.h"
#include "jobs.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

typedef int (*builtin_fn)(int argc, char **argv);

typedef struct Builtin {
    const char *name;
    builtin_fn  fn;
    const char *help;
} Builtin;

static int bi_cd(int argc, char **argv);
static int bi_pwd(int argc, char **argv);
static int bi_echo(int argc, char **argv);
static int bi_exit(int argc, char **argv);
static int bi_help(int argc, char **argv);
static int bi_export(int argc, char **argv);
static int bi_unset(int argc, char **argv);
static int bi_env(int argc, char **argv);
static int bi_set(int argc, char **argv);
static int bi_history(int argc, char **argv);
static int bi_alias(int argc, char **argv);
static int bi_unalias(int argc, char **argv);
static int bi_umask(int argc, char **argv);
static int bi_type(int argc, char **argv);
static int bi_source(int argc, char **argv);

/* La table s'étoffe au fil des phases du projet (jobs/fg/bg : phase 5,
 * source : phase 6). */
static const Builtin g_builtins[] = {
    { "cd",      bi_cd,      "cd [REP]       change de répertoire (défaut : HOME ; « - » : précédent)" },
    { "pwd",     bi_pwd,     "pwd            affiche le répertoire courant" },
    { "echo",    bi_echo,    "echo [-n] M..  affiche ses arguments" },
    { "exit",    bi_exit,    "exit [CODE]    quitte le shell" },
    { "help",    bi_help,    "help [CMDE]    affiche cette aide" },
    { "export",  bi_export,  "export [N[=V]] exporte une variable dans l'environnement" },
    { "unset",   bi_unset,   "unset NOM..    supprime des variables" },
    { "env",     bi_env,     "env            affiche l'environnement exporté" },
    { "set",     bi_set,     "set            affiche toutes les variables (locales et exportées)" },
    { "history", bi_history, "history [-c|N] historique des commandes" },
    { "alias",   bi_alias,   "alias [N=V]    définit ou affiche des alias" },
    { "unalias", bi_unalias, "unalias [-a|N] supprime des alias" },
    { "umask",   bi_umask,   "umask [MODE]   affiche ou définit le masque de création (octal)" },
    { "type",    bi_type,    "type NOM..     indique comment NOM serait interprété" },
    { "jobs",    jobs_bi_main, "jobs           liste les tâches en cours" },
    { "fg",      fg_bi_main,   "fg [%N]        ramène une tâche au premier plan" },
    { "bg",      bg_bi_main,   "bg [%N]        relance une tâche en arrière-plan" },
    { "source",  bi_source,    "source FICHIER exécute FICHIER dans le shell courant" },
    { ".",       bi_source,    ". FICHIER      équivalent de source" },
};

#define BUILTIN_COUNT ((int)(sizeof g_builtins / sizeof g_builtins[0]))

int builtin_count(void)             { return BUILTIN_COUNT; }
const char *builtin_name(int idx)   { return g_builtins[idx].name; }
const char *builtin_help_text(int idx) { return g_builtins[idx].help; }

int builtin_index(const char *name)
{
    int i;

    for (i = 0; i < BUILTIN_COUNT; i++)
        if (strcmp(g_builtins[i].name, name) == 0)
            return i;
    return -1;
}

int builtin_dispatch_idx(int idx, int argc, char **argv)
{
    return g_builtins[idx].fn(argc, argv);
}

/* ------------------------------------------------------------------ */
/* cd                                                                   */
/* ------------------------------------------------------------------ */

static int bi_cd(int argc, char **argv)
{
    const char *dest = NULL;
    const char *oldpwd;
    char cwd[PATH_MAX];
    int was_dash = 0;

    if (argc > 2) {
        fprintf(stderr, "cd : trop d'arguments\n");
        return 1;
    }

    oldpwd = var_get("PWD"); /* conservé avant le changement */

    if (argc == 2) {
        if (strcmp(argv[1], "-") == 0) {
            dest = var_get("OLDPWD");
            was_dash = 1;
            if (dest == NULL || *dest == '\0') {
                fprintf(stderr, "cd : OLDPWD n'est pas défini\n");
                return 1;
            }
        } else {
            dest = argv[1];
        }
    } else {
        dest = var_get("HOME");
        if (dest == NULL || *dest == '\0') {
            fprintf(stderr, "cd : HOME n'est pas défini\n");
            return 1;
        }
    }

    if (chdir(dest) != 0) {
        fprintf(stderr, "cd : %s : %s\n", dest, strerror(errno));
        return 1;
    }

    if (getcwd(cwd, sizeof cwd) != NULL) {
        if (was_dash)
            printf("%s\n", cwd); /* « cd - » affiche le répertoire atteint */
        if (oldpwd != NULL)
            var_set("OLDPWD", oldpwd);
        var_set("PWD", cwd);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pwd                                                                  */
/* ------------------------------------------------------------------ */

static int bi_pwd(int argc, char **argv)
{
    char cwd[PATH_MAX];

    if (argc > 1 && strcmp(argv[1], "-L") != 0 && strcmp(argv[1], "-P") != 0) {
        fprintf(stderr, "pwd : option non reconnue : %s\n", argv[1]);
        return 2;
    }

    if (getcwd(cwd, sizeof cwd) == NULL) {
        fprintf(stderr, "pwd : répertoire courant inaccessible : %s\n",
                strerror(errno));
        return 1;
    }
    printf("%s\n", cwd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* echo                                                                 */
/* ------------------------------------------------------------------ */

static int bi_echo(int argc, char **argv)
{
    int newline = 1;
    int start = 1;
    int i;

    if (argc >= 2 && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        start = 2;
    }

    for (i = start; i < argc; i++)
        printf("%s%s", (i > start) ? " " : "", argv[i]);
    if (newline)
        printf("\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* exit                                                                 */
/* ------------------------------------------------------------------ */

static int bi_exit(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "exit : trop d'arguments\n");
        return 1;
    }

    if (argc == 2) {
        char *end = NULL;
        long v = strtol(argv[1], &end, 10);

        if (end == argv[1] || *end != '\0') {
            fprintf(stderr, "exit : %s : argument numérique requis\n", argv[1]);
            g_shell.exit_code = 2;
        } else {
            g_shell.exit_code = (int)(v & 0xFF);
        }
    } else {
        g_shell.exit_code = g_shell.last_status;
    }

    g_shell.running = 0;
    return g_shell.exit_code;
}

/* ------------------------------------------------------------------ */
/* help                                                                 */
/* ------------------------------------------------------------------ */

static int bi_help(int argc, char **argv)
{
    int i;

    if (argc == 2) {
        int idx = builtin_index(argv[1]);

        if (idx < 0) {
            fprintf(stderr, "help : aucune rubrique d'aide pour « %s »\n",
                    argv[1]);
            return 1;
        }
        printf("%s\n", g_builtins[idx].help);
        return 0;
    }

    printf("Commandes internes d'alchimiste-shell :\n");
    for (i = 0; i < BUILTIN_COUNT; i++)
        printf("  %s\n", g_builtins[i].help);
    printf("\nOpérateurs gérés : ; && || | & > >> < 2> 2>> 2>&1 >&N &> &>>\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* export                                                               */
/* ------------------------------------------------------------------ */

static int bi_export(int argc, char **argv)
{
    int i;
    int st = 0;

    if (argc == 1) {
        vars_print_exported();
        return 0;
    }

    for (i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');

        if (eq != NULL) {
            *eq = '\0';
            if (!var_valid_name(argv[i])) {
                fprintf(stderr, "export : « %s » : identifiant invalide\n",
                        argv[i]);
                st = 1;
            } else {
                var_set_exported(argv[i], eq + 1);
            }
        } else if (!var_valid_name(argv[i])) {
            fprintf(stderr, "export : « %s » : identifiant invalide\n",
                    argv[i]);
            st = 1;
        } else {
            var_export(argv[i]);
        }
    }
    return st;
}

/* ------------------------------------------------------------------ */
/* unset                                                                */
/* ------------------------------------------------------------------ */

static int bi_unset(int argc, char **argv)
{
    int i;
    int st = 0;

    for (i = 1; i < argc; i++) {
        if (!var_valid_name(argv[i])) {
            fprintf(stderr, "unset : « %s » : identifiant invalide\n",
                    argv[i]);
            st = 1;
        } else {
            var_unset(argv[i]);
        }
    }
    return st;
}

/* ------------------------------------------------------------------ */
/* env et set                                                           */
/* ------------------------------------------------------------------ */

static int bi_env(int argc, char **argv)
{
    extern char **environ;
    int i;

    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "env : arguments non pris en charge "
                        "(utilisez la commande externe /usr/bin/env)\n");
        return 2;
    }
    for (i = 0; environ[i] != NULL; i++)
        printf("%s\n", environ[i]);
    return 0;
}

static int bi_set(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "set : options non prises en charge\n");
        return 2;
    }
    vars_print_all();
    return 0;
}

/* ------------------------------------------------------------------ */
/* history                                                              */
/* ------------------------------------------------------------------ */

static int bi_history(int argc, char **argv)
{
    int i;
    int total = history_count();

    if (argc == 2 && strcmp(argv[1], "-c") == 0) {
        history_clear();
        return 0;
    }

    if (argc == 2) {
        int n;
        const char *s;

        for (s = argv[1]; *s != '\0'; s++)
            if (!isdigit((unsigned char)*s)) {
                fprintf(stderr, "history : argument invalide : %s\n", argv[1]);
                return 2;
            }
        n = atoi(argv[1]);
        for (i = (n >= total) ? 0 : total - n; i < total; i++)
            printf("%5d  %s\n", i + 1, history_get(i));
        return 0;
    }

    if (argc > 2) {
        fprintf(stderr, "history : trop d'arguments\n");
        return 2;
    }

    for (i = 0; i < total; i++)
        printf("%5d  %s\n", i + 1, history_get(i));
    return 0;
}

/* ------------------------------------------------------------------ */
/* alias / unalias                                                      */
/* ------------------------------------------------------------------ */

static int bi_alias(int argc, char **argv)
{
    int i;
    int st = 0;

    if (argc == 1) {
        alias_print_all();
        return 0;
    }

    for (i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');

        if (eq == NULL) {
            const char *v = alias_get(argv[i]);

            if (v != NULL) {
                printf("alias %s='%s'\n", argv[i], v);
            } else {
                fprintf(stderr, "alias : « %s » : introuvable\n", argv[i]);
                st = 1;
            }
        } else {
            *eq = '\0';
            if (!var_valid_name(argv[i])) {
                fprintf(stderr, "alias : « %s » : nom invalide\n", argv[i]);
                st = 1;
            } else {
                alias_set(argv[i], eq + 1);
            }
        }
    }
    return st;
}

static int bi_unalias(int argc, char **argv)
{
    int i;
    int st = 0;

    if (argc == 2 && strcmp(argv[1], "-a") == 0) {
        alias_free_all();
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "unalias : un nom est attendu (ou -a)\n");
        return 2;
    }
    for (i = 1; i < argc; i++) {
        if (alias_unset(argv[i]) != 0) {
            fprintf(stderr, "unalias : « %s » : introuvable\n", argv[i]);
            st = 1;
        }
    }
    return st;
}

/* ------------------------------------------------------------------ */
/* umask                                                                */
/* ------------------------------------------------------------------ */

static int bi_umask(int argc, char **argv)
{
    const char *s;

    if (argc == 1) {
        mode_t m = umask(0);

        umask(m);
        printf("%04o\n", (unsigned)m);
        return 0;
    }
    if (argc > 2) {
        fprintf(stderr, "umask : trop d'arguments\n");
        return 1;
    }
    for (s = argv[1]; *s != '\0'; s++)
        if (*s < '0' || *s > '7') {
            fprintf(stderr, "umask : mode octal invalide : %s\n", argv[1]);
            return 1;
        }
    umask((mode_t)strtol(argv[1], NULL, 8));
    return 0;
}

/* ------------------------------------------------------------------ */
/* type                                                                 */
/* ------------------------------------------------------------------ */

static int bi_type(int argc, char **argv)
{
    int i;
    int st = 0;

    if (argc < 2) {
        fprintf(stderr, "type : un nom de commande est attendu\n");
        return 2;
    }

    for (i = 1; i < argc; i++) {
        const char *name = argv[i];
        const char *al = alias_get(name);

        if (al != NULL) {
            printf("%s est un alias vers « %s »\n", name, al);
            continue;
        }
        if (builtin_index(name) >= 0) {
            printf("%s est une commande interne du shell\n", name);
            continue;
        }
        {
            char *full = path_search(name, var_get("PATH"), X_OK);

            if (full != NULL) {
                printf("%s est %s\n", name, full);
                free(full);
            } else {
                fprintf(stderr, "type : %s : introuvable\n", name);
                st = 1;
            }
        }
    }
    return st;
}

/* ------------------------------------------------------------------ */
/* source / .                                                           */
/* ------------------------------------------------------------------ */

static int bi_source(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "source : un nom de fichier est attendu\n");
        return 2;
    }
    return shell_source_file(argv[1]);
}
