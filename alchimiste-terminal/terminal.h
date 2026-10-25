#ifndef ALCHIMISTE_TERMINAL_H
#define ALCHIMISTE_TERMINAL_H

#include <stddef.h>
#include <termios.h>

/* Ouvre un pseudo-terminal. Retourne le descripteur du côté maître,
 * et écrit le nom du côté esclave dans slave_name (taille slave_size).
 * Retourne -1 en cas d'erreur. */
int tty_open_pty(char *slave_name, size_t slave_size);

/* Passe le terminal d'entrée (stdin) en mode "raw" : pas d'écho,
 * pas de mise en tampon ligne, transmission caractère par caractère.
 * Sauvegarde l'ancien état pour restauration ultérieure.
 * Retourne 0 en cas de succès, -1 sinon. */
int tty_raw_mode(void);

/* Restaure le terminal d'entrée à l'état sauvegardé. */
void tty_restore_mode(void);

/* Définit la taille du terminal associé au descripteur fd. */
int tty_set_winsize(int fd, int rows, int cols);

/* Récupère la taille actuelle du terminal stdout. */
int tty_get_winsize(int *rows, int *cols);

#endif /* ALCHIMISTE_TERMINAL_H */
