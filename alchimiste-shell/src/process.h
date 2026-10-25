#ifndef PROCESS_H
#define PROCESS_H

#include <signal.h>
#include <sys/types.h>

/*
 * process — signaux du shell et récolte asynchrone des enfants.
 *
 * En mode interactif, le shell installe :
 *   - un gestionnaire SIGINT (Ctrl+C) : marque le signal et affiche un
 *     saut de ligne ; le shell ne meurt jamais d'un Ctrl+C ;
 *   - SIGQUIT (Ctrl+\), SIGTSTP (Ctrl+Z), SIGTTIN, SIGTTOU : ignorés,
 *     comme le fait Bash (sinon le shell s'arrêterait au lieu des
 *     commandes de premier plan) ;
 *   - un gestionnaire SIGCHLD : waitpid(WNOHANG|WUNTRACED|WCONTINUED)
 *     en boucle, les résultats sont stockés dans un anneau consultable
 *     par le code principal -> aucun zombie, même en arrière-plan.
 *
 * Les signaux sont installés SANS SA_RESTART afin que read() (invite)
 * et waitpid() (attente de premier plan) soient interrompus et puissent
 * réagir immédiatement.
 */

/* Positionné à 1 par le gestionnaire SIGINT ; le code principal le lit
 * et le remet à zéro. */
extern volatile sig_atomic_t g_sigint_received;

/* Fonction appelée pour chaque enfant récolté (pid + statut waitpid). */
typedef void (*reaped_fn)(pid_t pid, int status, void *ctx);

/* Installe les signaux du shell (mode interactif ou script). */
void process_install_shell_signals(int interactive);

/*
 * Rétablit les dispositions par défaut dans un fils juste avant execvp :
 * les signaux IGNORÉS survivent à exec(), il faut donc les réactiver
 * explicitement, sinon « sleep 10 » ignorerait Ctrl+C.
 */
void process_reset_child_signals(void);

/* Donne/rend le terminal au groupe de processus indiqué (contrôle des
 * tâches). Retourne 0 ou -1 (errno). */
int process_give_terminal(pid_t pgid);
int process_take_terminal_back(void);

/* Transmet les enfants récoltés par le handler SIGCHLD à fn. */
void process_drain_sigchld(reaped_fn fn, void *ctx);

/* Drainage + waitpid(WNOHANG) : récupère tout ce qui est déjà mort. */
void process_reap_nowait(reaped_fn fn, void *ctx);

#endif /* PROCESS_H */
