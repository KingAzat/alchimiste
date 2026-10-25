#ifndef ALCHIMISTE_RELAY_H
#define ALCHIMISTE_RELAY_H

/* Boucle principale : relit stdin (clavier) vers pty_master, et pty_master
 * vers stdout (écran), jusqu'à fermeture d'un côté ou réception de EOF.
 * child_status reçoit le statut de sortie du processus enfant (waitpid).
 * Retourne 0 en cas de succès, -1 en cas d'erreur. */
int relay_run(int pty_master, int *child_status);

#endif /* ALCHIMISTE_RELAY_H */
