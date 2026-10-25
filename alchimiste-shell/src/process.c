/*
 * process — signaux du shell et récolte asynchrone des enfants.
 */
#include "process.h"
#include "shell.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

volatile sig_atomic_t g_sigint_received = 0;

/* ------------------------------------------------------------------ */
/* Anneau des enfants récoltés par le handler SIGCHLD                   */
/* ------------------------------------------------------------------ */

/*
 * Le handler ne peut utiliser que des fonctions asynchrones-sûres :
 * waitpid() oui, mais pas jobs_update_pid() (malloc, printf...). Les
 * couples (pid, statut) sont donc stockés dans un anneau de taille
 * fixe, que le code principal draine via process_drain_sigchld().
 */
#define REAP_RING 128

static volatile sig_atomic_t g_ring_tail = 0;
static volatile sig_atomic_t g_ring_head = 0;

static struct reap_entry {
    volatile sig_atomic_t pid;
    volatile sig_atomic_t status;
} g_ring[REAP_RING];

/* ------------------------------------------------------------------ */
/* Gestionnaires                                                        */
/* ------------------------------------------------------------------ */

static void sigint_handler(int sig)
{
    (void)sig;
    g_sigint_received = 1;
    /* Retour à la ligne pour retrouver une invite propre.
     * write() est asynchrone-sûr, contrairement à printf(). */
    (void)write(STDERR_FILENO, "\n", 1);
}

static void sigchld_handler(int sig)
{
    int saved_errno = errno;
    int st;
    pid_t p;

    (void)sig;
    while ((p = waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        sig_atomic_t tail = g_ring_tail;

        if (tail - g_ring_head < REAP_RING) {
            g_ring[tail % REAP_RING].pid = (sig_atomic_t)p;
            g_ring[tail % REAP_RING].status = (sig_atomic_t)st;
            g_ring_tail = tail + 1;
        }
        /* Anneau plein : la mise à jour est perdue (cas extrême documenté),
         * mais l'enfant est tout de même récolté : pas de zombie. */
    }
    errno = saved_errno;
}

/* ------------------------------------------------------------------ */
/* Installation des signaux                                             */
/* ------------------------------------------------------------------ */

void process_install_shell_signals(int interactive)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* PAS de SA_RESTART : read()/waitpid() doivent
                        pouvoir être interrompus (réactivité Ctrl+C). */

    if (interactive) {
        sa.sa_handler = sigint_handler;
        sigaction(SIGINT, &sa, NULL);

        sa.sa_handler = SIG_IGN;
        sigaction(SIGQUIT, &sa, NULL); /* le shell survit à Ctrl+\ */
        sigaction(SIGTSTP, &sa, NULL); /* ... et à Ctrl+Z          */
        sigaction(SIGTTIN, &sa, NULL); /* lecture en arrière-plan  */
        sigaction(SIGTTOU, &sa, NULL); /* écriture en arrière-plan */
    } else {
        /* Mode script : Ctrl+C termine le shell et ses commandes,
         * exactement comme un script Bash lancé au premier plan. */
        sa.sa_handler = SIG_DFL;
        sigaction(SIGINT, &sa, NULL);

        sa.sa_handler = SIG_IGN;
        sigaction(SIGTSTP, &sa, NULL);
        sigaction(SIGTTIN, &sa, NULL);
        sigaction(SIGTTOU, &sa, NULL);
    }

    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);
}

void process_reset_child_signals(void)
{
    /* Les signaux ignorés traversent exec() : sans cette remise à
     * zéro, un fils hériterait de SIGINT ignoré et devrait
     * invulnérable à Ctrl+C. */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

/* ------------------------------------------------------------------ */
/* Contrôle du terminal                                                 */
/* ------------------------------------------------------------------ */

int process_give_terminal(pid_t pgid)
{
    int rc = tcsetpgrp(STDIN_FILENO, pgid);

    if (rc != 0) {
        /* Diagnostic une seule fois : sans terminal de contrôle (par
         * exemple si le shell est lancé sans session adaptée), Ctrl+C
         * et Ctrl+Z ne peuvent pas atteindre les commandes. */
        static int warned = 0;

        if (!warned) {
            warned = 1;
            fprintf(stderr, "alchimiste-shell: avertissement : terminal de "
                            "contrôle indisponible, Ctrl+C/Ctrl+Z "
                            "n'atteindront pas les commandes\n");
        }
    }
    return rc;
}

int process_take_terminal_back(void)
{
    return tcsetpgrp(STDIN_FILENO, g_shell.shell_pgid);
}

/* ------------------------------------------------------------------ */
/* Drainage de l'anneau                                                 */
/* ------------------------------------------------------------------ */

void process_drain_sigchld(reaped_fn fn, void *ctx)
{
    /* Lecture unique de la position d'écriture : cohérence si le
     * handler s'exécute pendant le parcours. */
    sig_atomic_t tail = g_ring_tail;

    while (g_ring_head < tail) {
        size_t idx = (size_t)(g_ring_head % REAP_RING);

        fn((pid_t)g_ring[idx].pid, (int)g_ring[idx].status, ctx);
        g_ring_head++;
    }
}

void process_reap_nowait(reaped_fn fn, void *ctx)
{
    int st;
    pid_t p;

    process_drain_sigchld(fn, ctx);
    while ((p = waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
        fn(p, st, ctx);
}
