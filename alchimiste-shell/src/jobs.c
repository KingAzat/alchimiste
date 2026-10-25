/*
 * jobs — contrôle des tâches.
 */
#include "jobs.h"
#include "shell.h"
#include "process.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

/* ------------------------------------------------------------------ */
/* Structures                                                           */
/* ------------------------------------------------------------------ */

typedef struct Proc {
    pid_t pid;
    int   status; /* dernier statut waitpid de ce processus */
    int   done;   /* 1 : le processus s'est terminé          */
} Proc;

struct Job {
    int      id;                       /* numéro affiché [N]        */
    pid_t    pgid;                     /* groupe de processus       */
    char    *cmdline;                  /* ligne de commande d'origine */
    JobState state;
    int      nprocs;
    Proc     procs[MAX_PIPE_CMDS];
    struct Job *next;
};

/*
 * Événements arrivés entre fork() et l'enregistrement de la tâche :
 * le handler SIGCHLD peut récolter un fils avant que job_add() ne
 * connaisse son pid. Ces événements sont mis de côté puis rejoués.
 */
typedef struct Pending {
    pid_t pid;
    int   status;
    struct Pending *next;
} Pending;

static Job     *g_jobs;
static Pending *g_pending;
static int      g_next_id = 1;

/* ------------------------------------------------------------------ */
/* Mise à jour depuis waitpid                                           */
/* ------------------------------------------------------------------ */

static void on_reaped(pid_t pid, int status, void *ctx)
{
    (void)ctx;
    jobs_update_pid(pid, status);
}

void jobs_reap(void)
{
    process_reap_nowait(on_reaped, NULL);
}

static Job *job_find_pid(pid_t pid)
{
    Job *j;
    int i;

    for (j = g_jobs; j != NULL; j = j->next)
        for (i = 0; i < j->nprocs; i++)
            if (j->procs[i].pid == pid)
                return j;
    return NULL;
}

void jobs_update_pid(pid_t pid, int status)
{
    Job *j = job_find_pid(pid);
    int i;

    if (j == NULL) {
        /* Tâche pas encore enregistrée (course avec SIGCHLD) :
         * on met l'événement de côté, job_add() le rejouera. */
        Pending *p = xmalloc(sizeof *p);

        p->pid = pid;
        p->status = status;
        p->next = g_pending;
        g_pending = p;
        return;
    }

    for (i = 0; i < j->nprocs; i++)
        if (j->procs[i].pid == pid)
            break;

    j->procs[i].status = status;
    if (WIFSTOPPED(status)) {
        j->state = JOB_STOPPED;
        return;
    }
    if (WIFCONTINUED(status)) {
        if (j->state == JOB_STOPPED)
            j->state = JOB_RUNNING;
        return;
    }
    j->procs[i].done = 1;
    if (job_all_done(j))
        j->state = JOB_DONE;
}

/* ------------------------------------------------------------------ */
/* Table des tâches                                                     */
/* ------------------------------------------------------------------ */

Job *job_add(pid_t pgid, const pid_t *pids, int npids, const char *cmdline)
{
    Job *j = xcalloc(1, sizeof *j);
    int i;

    j->id = g_next_id++;
    j->pgid = pgid;
    j->cmdline = xstrdup(cmdline);
    j->state = JOB_RUNNING;
    j->nprocs = npids;
    for (i = 0; i < npids; i++) {
        j->procs[i].pid = pids[i];
        j->procs[i].status = 0;
        j->procs[i].done = 0;
    }

    /* Ajout en fin de liste : affichage dans l'ordre de création. */
    if (g_jobs == NULL) {
        g_jobs = j;
    } else {
        Job *t = g_jobs;

        while (t->next != NULL)
            t = t->next;
        t->next = j;
    }

    /* Rejoue les événements arrivés avant l'enregistrement. */
    {
        Pending *p = g_pending;
        Pending *next;

        g_pending = NULL;
        for (; p != NULL; p = next) {
            next = p->next;
            jobs_update_pid(p->pid, p->status);
            free(p);
        }
    }
    return j;
}

void job_remove(Job *j)
{
    Job **pp;

    for (pp = &g_jobs; *pp != NULL; pp = &(*pp)->next) {
        if (*pp == j) {
            *pp = j->next;
            free(j->cmdline);
            free(j);
            return;
        }
    }
}

int job_id(const Job *j)
{
    return j->id;
}

int job_all_done(const Job *j)
{
    int i;

    for (i = 0; i < j->nprocs; i++)
        if (!j->procs[i].done)
            return 0;
    return 1;
}

int job_status(const Job *j)
{
    const Proc *p = &j->procs[j->nprocs - 1];

    if (WIFSTOPPED(p->status))
        return 128 + WSTOPSIG(p->status);
    if (WIFEXITED(p->status))
        return WEXITSTATUS(p->status);
    if (WIFSIGNALED(p->status))
        return 128 + WTERMSIG(p->status);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Attente d'une tâche de premier plan                                  */
/* ------------------------------------------------------------------ */

int jobs_wait(Job *j)
{
    int st;
    pid_t p;

    /* Le terminal est donné au groupe de la tâche : Ctrl+C / Ctrl+Z
     * atteignent la commande, pas le shell. */
    if (g_shell.interactive)
        process_give_terminal(j->pgid);

    for (;;) {
        jobs_reap();
        if (job_all_done(j) || j->state == JOB_STOPPED)
            break;

        p = waitpid(-1, &st, WUNTRACED | WCONTINUED);
        if (p > 0) {
            jobs_update_pid(p, st);
            continue;
        }
        if (p < 0 && errno == EINTR)
            continue; /* interrompu par un signal : on reboucle */
        if (p < 0 && errno == ECHILD) {
            /* Plus aucun fils : tout doit déjà être dans l'anneau. */
            jobs_reap();
            if (!job_all_done(j)) {
                int i;

                for (i = 0; i < j->nprocs; i++)
                    j->procs[i].done = 1;
                j->state = JOB_DONE;
            }
            break;
        }
        if (p < 0) {
            fprintf(stderr, "alchimiste-shell: waitpid : %s\n",
                    strerror(errno));
            break;
        }
    }

    if (g_shell.interactive)
        process_take_terminal_back();
    return job_status(j);
}

/* ------------------------------------------------------------------ */
/* Notifications et affichage                                           */
/* ------------------------------------------------------------------ */

static char job_marker(const Job *j)
{
    Job *last = NULL, *prev = NULL, *it;

    for (it = g_jobs; it != NULL; it = it->next) {
        prev = last;
        last = it;
    }
    if (j == last)
        return '+';
    if (j == prev)
        return '-';
    return ' ';
}

static const char *job_state_str(const Job *j)
{
    switch (j->state) {
    case JOB_RUNNING:
        return "En cours";
    case JOB_STOPPED:
        return "Arrêtée";
    default:
        return "Terminée";
    }
}

void jobs_notify_done(void)
{
    Job *j, *next;

    for (j = g_jobs; j != NULL; j = next) {
        next = j->next;
        if (j->state == JOB_DONE) {
            fprintf(stderr, "[%d]%c  %-10s  %s\n", j->id, job_marker(j),
                    job_state_str(j), j->cmdline);
            job_remove(j);
        }
    }
}

void jobs_free_all(void)
{
    Job *j, *next;

    for (j = g_jobs; j != NULL; j = next) {
        next = j->next;
        free(j->cmdline);
        free(j);
    }
    g_jobs = NULL;
}

/* ------------------------------------------------------------------ */
/* Commandes internes : jobs, fg, bg                                    */
/* ------------------------------------------------------------------ */

static Job *job_current(void)
{
    Job *j, *found = NULL;

    for (j = g_jobs; j != NULL; j = j->next)
        if (j->state != JOB_DONE)
            found = j; /* la plus récente non terminée */
    return found;
}

static Job *job_from_spec(const char *spec)
{
    const char *s = (spec[0] == '%') ? spec + 1 : spec;
    Job *j;
    int id;

    for (const char *p = s; *p != '\0'; p++)
        if (!isdigit((unsigned char)*p)) {
            fprintf(stderr, "jobs : spécification de tâche invalide : %s\n",
                    spec);
            return NULL;
        }
    if (*s == '\0') {
        fprintf(stderr, "jobs : spécification de tâche invalide : %s\n", spec);
        return NULL;
    }

    id = atoi(s);
    for (j = g_jobs; j != NULL; j = j->next)
        if (j->id == id)
            return j;

    fprintf(stderr, "jobs : %%%d : aucune tâche de ce numéro\n", id);
    return NULL;
}

int jobs_bi_main(int argc, char **argv)
{
    Job *j, *next;

    (void)argv; /* aucun argument n'est pris en charge */
    if (argc != 1) {
        fprintf(stderr, "jobs : argument non pris en charge\n");
        return 2;
    }

    jobs_reap();
    for (j = g_jobs; j != NULL; j = next) {
        next = j->next;
        printf("[%d]%c  %-10s  %s\n", j->id, job_marker(j),
               job_state_str(j), j->cmdline);
        if (j->state == JOB_DONE)
            job_remove(j);
    }
    return 0;
}

static Job *lookup_job_arg(int argc, char **argv, const char *cmdname)
{
    Job *j;

    if (argc > 2) {
        fprintf(stderr, "%s : trop d'arguments\n", cmdname);
        return NULL;
    }
    if (argc == 2) {
        j = job_from_spec(argv[1]);
        if (j == NULL)
            return NULL;
    } else {
        j = job_current();
        if (j == NULL) {
            fprintf(stderr, "%s : aucune tâche courante\n", cmdname);
            return NULL;
        }
    }
    if (j->state == JOB_DONE) {
        fprintf(stderr, "%s : la tâche %d s'est terminée\n", cmdname, j->id);
        return NULL;
    }
    return j;
}

int fg_bi_main(int argc, char **argv)
{
    Job *j = lookup_job_arg(argc, argv, "fg");
    int st;

    if (j == NULL)
        return 1;

    printf("%s\n", j->cmdline);
    fflush(stdout);

    if (kill(-j->pgid, SIGCONT) < 0 && errno != ESRCH) {
        fprintf(stderr, "fg : kill : %s\n", strerror(errno));
        return 1;
    }
    j->state = JOB_RUNNING;

    st = jobs_wait(j);
    if (job_all_done(j))
        job_remove(j);
    return st;
}

int bg_bi_main(int argc, char **argv)
{
    Job *j = lookup_job_arg(argc, argv, "bg");

    if (j == NULL)
        return 1;

    if (kill(-j->pgid, SIGCONT) < 0 && errno != ESRCH) {
        fprintf(stderr, "bg : kill : %s\n", strerror(errno));
        return 1;
    }
    j->state = JOB_RUNNING;

    printf("[%d] %s &\n", j->id, j->cmdline);
    return 0;
}
