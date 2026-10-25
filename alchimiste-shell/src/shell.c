/*
 * shell — boucle principale (REPL), lecture des lignes et invite de commande.
 */
#include "shell.h"
#include "util.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "variables.h"
#include "history.h"
#include "jobs.h"
#include "process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <sys/utsname.h>

Shell g_shell;

static void load_rc_file(void); /* exécuté avant la boucle interactive */

/* ------------------------------------------------------------------ */
/* Lecture d'une ligne sur l'entrée standard                            */
/* ------------------------------------------------------------------ */

/*
 * Tampon d'entrée : read() peut renvoyer plusieurs lignes d'un coup
 * (tube, fichier) ; on mémorise donc ce qui a été lu et on extrait les
 * lignes une par une.
 */
static char   g_inbuf[MAX_LINE];
static size_t g_inlen = 0;
static size_t g_inpos = 0;

/*
 * Extrait la prochaine ligne d'entrée et la copie dans buf (taille size).
 * Retourne : 1 ligne lue, 0 fin de fichier, 2 ligne trop longue,
 * 3 interrompue par Ctrl+C, -1 erreur.
 * On utilise read() et pas fgets() : cela permet de détecter les lignes
 * dépassant la taille du tampon et de réagir proprement aux signaux.
 */
static int read_input_line(char *buf, size_t size)
{
    int discarding = 0;

    for (;;) {
        char *nl;

        /* 1. Une ligne complète est-elle déjà dans le tampon ? */
        if (g_inlen > g_inpos) {
            nl = memchr(g_inbuf + g_inpos, '\n', g_inlen - g_inpos);
            if (nl != NULL) {
                size_t len = (size_t)(nl - (g_inbuf + g_inpos));

                if (discarding) { /* fin d'une ligne trop longue : on la jette */
                    g_inpos += len + 1;
                    return 2;
                }
                if (len + 1 > size) {
                    g_inpos += len + 1;
                    return 2;
                }
                memcpy(buf, g_inbuf + g_inpos, len);
                g_inpos += len + 1;
                buf[len] = '\0';
                if (len > 0 && buf[len - 1] == '\r') /* tolère CRLF */
                    buf[len - 1] = '\0';
                return 1;
            }
        }

        /* 2. Compacte le tampon puis lit davantage. */
        if (g_inpos > 0) {
            memmove(g_inbuf, g_inbuf + g_inpos, g_inlen - g_inpos);
            g_inlen -= g_inpos;
            g_inpos = 0;
        }
        if (g_inlen == sizeof g_inbuf) {
            /* tampon plein sans '\n' : ligne trop longue, on jette */
            g_inlen = 0;
            discarding = 1;
            continue;
        }

        ssize_t n = read(STDIN_FILENO, g_inbuf + g_inlen,
                         sizeof g_inbuf - g_inlen);
        if (n > 0) {
            g_inlen += (size_t)n;
            continue;
        }
        if (n == 0) { /* fin de fichier */
            if (g_inlen > g_inpos && !discarding) {
                size_t len = g_inlen - g_inpos; /* dernière ligne sans '\n' */

                if (len + 1 > size) {
                    g_inpos = g_inlen;
                    return 2;
                }
                memcpy(buf, g_inbuf + g_inpos, len);
                g_inpos = g_inlen;
                buf[len] = '\0';
                if (len > 0 && buf[len - 1] == '\r')
                    buf[len - 1] = '\0';
                return 1;
            }
            return 0;
        }
        /* n < 0 */
        if (errno == EINTR) {
            /*
             * Lecture interrompue par un signal. On rend la main à la
             * boucle principale : elle récoltera les enfants morts
             * (SIGCHLD), affichera les notifications de tâches puis
             * réaffichera l'invite. C'est ce qui permet d'afficher
             * « [1]+ Terminée ... » pendant que le shell attend une
             * commande, exactement comme Bash. La ligne éventuellement
             * en cours de frappe reste dans le tampon du terminal.
             */
            if (g_sigint_received)
                g_sigint_received = 0; /* ligne abandonnée : Ctrl+C */
            return 3;
        }
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Invite de commande : utilisateur@hôte:chemin$                        */
/* ------------------------------------------------------------------ */

static void print_prompt(void)
{
    char cwd[PATH_MAX];
    char dispbuf[PATH_MAX + 2];
    char host[128];
    const char *user = var_get("USER");
    const char *home = var_get("HOME");
    const char *disp;
    struct utsname uts;

    if (user == NULL || *user == '\0') {
        struct passwd *pw = getpwuid(geteuid());
        user = (pw != NULL && pw->pw_name != NULL) ? pw->pw_name : "utilisateur";
    }

    if (uname(&uts) == 0 && uts.nodename[0] != '\0') {
        snprintf(host, sizeof host, "%s", uts.nodename);
        char *dot = strchr(host, '.');
        if (dot != NULL)
            *dot = '\0'; /* nom court, comme le fait Bash */
    } else {
        snprintf(host, sizeof host, "%s", "localhost");
    }

    if (getcwd(cwd, sizeof cwd) == NULL)
        snprintf(cwd, sizeof cwd, "%s", "?");

    disp = cwd;
    if (home != NULL && *home != '\0') {
        size_t hl = strlen(home);
        if (strcmp(cwd, home) == 0) {
            snprintf(dispbuf, sizeof dispbuf, "~");
            disp = dispbuf;
        } else if (strncmp(cwd, home, hl) == 0 && cwd[hl] == '/') {
            snprintf(dispbuf, sizeof dispbuf, "~%s", cwd + hl);
            disp = dispbuf;
        }
    }

    fprintf(stderr, "%s@%s:%s%c ", user, host, disp,
            geteuid() == 0 ? '#' : '$');
}

static void print_banner(void)
{
    fprintf(stderr,
        "╔══════════════════════════════════════╗\n"
        "║           ALCHIMISTE SHELL            ║\n"
        "║        Mini-shell Linux en C          ║\n"
        "╚══════════════════════════════════════╝\n");
}

/* ------------------------------------------------------------------ */
/* Évaluation d'une ligne                                               */
/* ------------------------------------------------------------------ */

void shell_eval_no_history(const char *line)
{
    size_t ntok = 0;
    Token *toks;
    CommandLine *cl;
    int err = 0;

    /* 1. analyse lexicale */
    toks = lexer_tokenize(line, &ntok);
    if (toks == NULL) { /* erreur de syntaxe du lexer */
        g_shell.last_status = 2;
        return;
    }

    /* 2. analyse syntaxique : construction de l'arbre */
    cl = parser_parse(toks, &err);
    lexer_free_tokens(toks, ntok);
    if (cl == NULL) {
        if (err) /* erreur de syntaxe du parser */
            g_shell.last_status = 2;
        return; /* ligne vide : rien à faire */
    }

    /* 3. exécution */
    g_shell.last_status = executor_run(cl);

    /* 4. libération de l'arbre */
    parser_free(cl);
}

void shell_eval_line(const char *line)
{
    /* Les lignes non vides alimentent l'historique de la session. */
    history_add(line);
    shell_eval_no_history(line);
}

/* ------------------------------------------------------------------ */
/* Boucle interactive                                                   */
/* ------------------------------------------------------------------ */

int shell_run_interactive(void)
{
    char line[MAX_LINE];

    if (g_shell.interactive) {
        /* Bannière désactivée si ALCH_BANNIERE=0 (utilisé par
         * alchimiste-terminal-gui, dont la fenêtre montre directement
         * l'invite du shell). Le lancement normal la garde. */
        const char *sans_banner = getenv("ALCH_BANNIERE");
        if (sans_banner == NULL || strcmp(sans_banner, "0") != 0)
            print_banner();
        load_rc_file();
    }

    while (g_shell.running) {
        int rc;

        /* Récolte des enfants morts (anti-zombie) à chaque tour, et
         * notification des tâches en arrière-plan terminées. */
        jobs_reap();
        if (g_shell.interactive) {
            jobs_notify_done();
            print_prompt();
        }

        /* Ctrl+C pendant le traitement d'une commande : ignoré ici,
         * le gestionnaire a déjà affiché un saut de ligne. */
        if (g_sigint_received)
            g_sigint_received = 0;

        rc = read_input_line(line, sizeof line);
        if (rc == 0) { /* fin de fichier : Ctrl+D */
            fprintf(stderr, "exit\n");
            g_shell.exit_code = g_shell.last_status;
            break;
        }
        if (rc == 3)
            continue; /* Ctrl+C à l'invite : ligne abandonnée */
        if (rc == 2) {
            fprintf(stderr,
                    "alchimiste-shell: ligne trop longue (maximum %d caractères)\n",
                    MAX_LINE - 1);
            continue;
        }
        if (rc < 0) {
            fprintf(stderr, "alchimiste-shell: erreur de lecture : %s\n",
                    strerror(errno));
            break;
        }

        shell_eval_line(line);
        fflush(stdout);
    }

    fprintf(stderr, "Au revoir !\n");
    history_save(); /* sauvegarde du fichier dédié (mode interactif) */
    return g_shell.exit_code;
}

/* ------------------------------------------------------------------ */
/* Initialisation / nettoyage                                           */
/* ------------------------------------------------------------------ */

void shell_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    memset(&g_shell, 0, sizeof g_shell);
    g_shell.prog_name = "alchimiste-shell";
    g_shell.running = 1;
    g_shell.interactive = isatty(STDIN_FILENO) ? 1 : 0;
    g_shell.shell_pgid = getpgrp();

    vars_init_from_environ(); /* l'environnement devient la table initiale */
    history_init();          /* charge ~/.alchimiste_history si interactif */

    /* Signaux : installés avant le moindre fork(). */
    process_install_shell_signals(g_shell.interactive);

    if (g_shell.interactive) {
        pid_t fg = tcgetpgrp(STDIN_FILENO);

        if (fg >= 0 && fg != g_shell.shell_pgid)
            fprintf(stderr, "alchimiste-shell: avertissement : le shell "
                            "n'est pas au premier plan\n");
    }
}

void shell_cleanup(void)
{
    jobs_free_all();
    history_free();
    alias_free_all();
    vars_free_all();
}

int shell_run_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[MAX_LINE];
    int lineno = 0;

    if (f == NULL) {
        fprintf(stderr, "alchimiste-shell: %s : %s\n", path, strerror(errno));
        return 127;
    }

    while (g_shell.running && fgets(line, sizeof line, f) != NULL) {
        char *nl;
        size_t len;

        lineno++;
        nl = strchr(line, '\n');
        if (nl == NULL && !feof(f)) {
            fprintf(stderr, "alchimiste-shell: %s:%d : ligne trop longue, "
                            "ignorée\n", path, lineno);
            {
                int ch;

                while ((ch = fgetc(f)) != '\n' && ch != EOF)
                    ;
            }
            continue;
        }
        if (nl != NULL)
            *nl = '\0';
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\r')
            line[len - 1] = '\0';

        shell_eval_no_history(line);
        fflush(stdout);
    }
    fclose(f);

    jobs_reap(); /* récolte finale, sans zombies oubliés */
    /* Code de sortie : celui de « exit » si utilisé, sinon celui de la
     * dernière commande (comportement de Bash). */
    return g_shell.running ? g_shell.last_status : g_shell.exit_code;
}

/* ------------------------------------------------------------------ */
/* source : exécution d'un fichier dans le processus courant            */
/* ------------------------------------------------------------------ */

int shell_source_file(const char *path)
{
    char *full;
    FILE *f;
    char line[MAX_LINE];
    int last = 0;

    if (g_shell.source_depth >= SOURCE_MAX_DEPTH) {
        fprintf(stderr, "source : trop de niveaux d'imbrication\n");
        return 1;
    }

    /* Comme Bash : chemin direct s'il contient un '/', sinon répertoire
     * courant d'abord, puis recherche dans PATH (lecture, pas exécution). */
    if (strchr(path, '/') != NULL) {
        full = xstrdup(path);
    } else if (access(path, R_OK) == 0) {
        full = xstrdup(path); /* trouvé dans le répertoire courant */
    } else {
        full = path_search(path, var_get("PATH"), R_OK);
    }
    if (full == NULL) {
        fprintf(stderr, "source : %s : fichier introuvable\n", path);
        return 1;
    }
    f = fopen(full, "r");
    if (f == NULL) {
        fprintf(stderr, "source : %s : %s\n", full, strerror(errno));
        free(full);
        return 1;
    }

    g_shell.source_depth++;
    while (g_shell.running && fgets(line, sizeof line, f) != NULL) {
        char *nl = strchr(line, '\n');
        size_t len;

        if (nl != NULL)
            *nl = '\0';
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\r')
            line[len - 1] = '\0';

        /* Exécuté dans le processus courant : cd, variables et alias
         * persistent après le source (c'est sa raison d'être). */
        shell_eval_no_history(line);
        last = g_shell.last_status;
        fflush(stdout);
    }
    fclose(f);
    g_shell.source_depth--;
    free(full);
    return last;
}

/* Fichier de configuration personnel, exécuté au démarrage interactif. */
static void load_rc_file(void)
{
    const char *home = var_get("HOME");
    char path[PATH_MAX];

    if (home == NULL || *home == '\0')
        return;
    if (snprintf(path, sizeof path, "%s/.alchimiste_rc", home) >=
        (int)sizeof path)
        return;
    if (access(path, R_OK) == 0)
        shell_source_file(path); /* erreurs affichées, non fatales */
}
