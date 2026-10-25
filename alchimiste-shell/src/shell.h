#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

/*
 * shell — contexte global du shell, initialisé dans main() et partagé par
 * tous les modules. Un seul objet global, volontairement simple à expliquer
 * lors de la soutenance.
 */

/* Limites volontaires du shell (robustesse, anti-débordement de tampon). */
#define MAX_LINE        4096  /* longueur maximale d'une ligne de commande    */
#define MAX_ARGS        256   /* nombre maximal d'arguments par commande      */
#define MAX_PIPE_CMDS   32    /* nombre maximal de commandes par pipeline     */
#define HISTORY_CAP     1000  /* nombre maximal d'entrées d'historique        */
#define ALIAS_MAX_DEPTH 10    /* garde-fou contre les alias récursifs         */
#define SOURCE_MAX_DEPTH 8    /* garde-fou contre les « source » imbriqués    */

typedef struct Shell {
    const char *prog_name; /* nom du programme utilisé dans les messages     */
    int   last_status;     /* code de retour de la dernière commande ($?)    */
    int   interactive;     /* 1 si l'entrée standard est un terminal         */
    int   running;         /* 0 : la boucle principale doit s'arrêter        */
    int   exit_code;       /* code de sortie demandé par « exit »            */
    pid_t shell_pgid;      /* groupe de processus du shell (contrôle tâches) */
    int   source_depth;    /* profondeur d'imbrication de « source »         */
    int   alias_depth;     /* profondeur d'expansion des alias               */
} Shell;

extern Shell g_shell;

void shell_init(int argc, char **argv);
void shell_cleanup(void);

/* Boucle interactive (stdin = terminal ou tube ; pas de fichier script). */
int  shell_run_interactive(void);

/* Mode script : exécute les lignes d'un fichier puis rend le dernier statut. */
int  shell_run_script(const char *path);

/* Évalue une ligne saisie (ajoutée à l'historique si non vide). */
void shell_eval_line(const char *line);

/* Évalue une ligne sans l'ajouter à l'historique (source, alias, scripts). */
void shell_eval_no_history(const char *line);

/* Exécute un fichier dans le processus courant (commande interne source). */
int  shell_source_file(const char *path);

#endif /* SHELL_H */
