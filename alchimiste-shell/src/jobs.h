#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

/*
 * jobs — contrôle des tâches.
 *
 * Chaque pipeline lancé devient une « tâche » (job) : un groupe de
 * processus (pgid) contenant un ou plusieurs fils. La table conserve
 * l'état de chaque processus (en cours / terminé) et l'état global de
 * la tâche (En cours / Arrêtée / Terminée).
 *
 * En mode interactif :
 *   - un pipeline de premier plan reçoit le terminal (tcsetpgrp) ;
 *   - Ctrl+C et Ctrl+Z atteignent donc les commandes, pas le shell ;
 *   - fg ramène une tâche au premier plan, bg la relance en arrière-plan.
 */

typedef enum JobState {
    JOB_RUNNING, /* en cours (premier plan ou arrière-plan) */
    JOB_STOPPED, /* suspendue par SIGTSTP (Ctrl+Z)           */
    JOB_DONE     /* terminée, en attente de notification      */
} JobState;

typedef struct Job Job;

/* Enregistre une nouvelle tâche (pids = les n processus du pipeline). */
Job *job_add(pid_t pgid, const pid_t *pids, int npids, const char *cmdline);

void job_remove(Job *j);
int  job_id(const Job *j);
int  job_all_done(const Job *j);
int  job_status(const Job *j); /* statut de la dernière commande du pipeline */

/* Met à jour la table après un waitpid() réussi. */
void jobs_update_pid(pid_t pid, int status);

/* Récupère tous les enfants morts/suspendus disponibles (anti-zombie). */
void jobs_reap(void);

/* Affiche (et retire) les tâches en arrière-plan terminées, avant
 * l'invite de commande. */
void jobs_notify_done(void);

/* Attend la fin (ou la suspension) d'une tâche de premier plan. */
int jobs_wait(Job *j);

void jobs_free_all(void);

/* Commandes internes (appelées depuis builtins.c). */
int jobs_bi_main(int argc, char **argv);
int fg_bi_main(int argc, char **argv);
int bg_bi_main(int argc, char **argv);

#endif /* JOBS_H */
