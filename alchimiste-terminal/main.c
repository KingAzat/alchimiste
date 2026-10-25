#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "relay.h"
#include "terminal.h"

int main(int argc, char *argv[])
{
    /* Shell à lancer : argument ou /bin/sh par défaut */
    const char *shell = (argc > 1) ? argv[1] : "/bin/sh";
    char *shell_args[] = {(char *)shell, NULL};

    char slave_name[256];
    int master = tty_open_pty(slave_name, sizeof(slave_name));
    if (master < 0) {
        fprintf(stderr, "Impossible d'ouvrir le pseudo-terminal.\n");
        return 1;
    }

    if (tty_raw_mode() < 0) {
        close(master);
        return 1;
    }

    /* Récupère la taille actuelle de notre terminal pour l'enfant */
    int rows = 24, cols = 80;
    tty_get_winsize(&rows, &cols);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        tty_restore_mode();
        close(master);
        return 1;
    }

    if (pid == 0) {
        /* ---------- ENFANT : le shell ---------- */
        close(master);

        /* Crée une nouvelle session et prend le pty comme terminal contrôlant */
        if (setsid() < 0) {
            _exit(1);
        }

        int slave = open(slave_name, O_RDWR);
        if (slave < 0) {
            _exit(1);
        }

        /* Duplique le pty vers stdin/stdout/stderr */
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO)
            close(slave);

        /* Fait de ce pty le terminal contrôlant du processus */
        ioctl(STDIN_FILENO, TIOCSCTTY, 1);

        /* Applique la taille de fenêtre */
        struct winsize ws;
        ws.ws_row = (unsigned short)rows;
        ws.ws_col = (unsigned short)cols;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl(STDIN_FILENO, TIOCSWINSZ, &ws);

        /* Lance le shell demandé */
        execvp(shell, shell_args);

        /* Si on arrive ici, execvp a échoué */
        perror("execvp");
        _exit(127);
    }

    /* ---------- PÈRE : l'émulateur ---------- */

    int child_status = 0;
    int rc = relay_run(master, &child_status);

    tty_restore_mode();
    close(master);

    if (rc < 0)
        return 1;
    if (WIFEXITED(child_status))
        return WEXITSTATUS(child_status);
    if (WIFSIGNALED(child_status))
        return 128 + WTERMSIG(child_status);
    return 0;
}
