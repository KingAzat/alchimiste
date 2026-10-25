#include "terminal.h"

#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static struct termios g_orig_termios;
static int g_raw_saved = 0;

int tty_open_pty(char *slave_name, size_t slave_size)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        perror("grantpt/unlockpt");
        close(master);
        return -1;
    }

    const char *name = ptsname(master);
    if (name == NULL) {
        perror("ptsname");
        close(master);
        return -1;
    }

    snprintf(slave_name, slave_size, "%s", name);
    return master;
}

int tty_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) {
        perror("tcgetattr");
        return -1;
    }

    struct termios raw = g_orig_termios;

    /* Désactive : écho, mise en tampon ligne, signaux de fin de ligne,
     * interception de Ctrl-C / Ctrl-Z / Ctrl-\ (gérés par le shell enfant) */
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(ICRNL | INLCR | IGNCR | IXON | IXOFF);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cc[VMIN] = 1;   /* lire un caractère à la fois */
    raw.c_cc[VTIME] = 0;  /* pas d'attente temporisée */

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        perror("tcsetattr");
        return -1;
    }

    g_raw_saved = 1;
    return 0;
}

void tty_restore_mode(void)
{
    if (g_raw_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_raw_saved = 0;
    }
}

int tty_set_winsize(int fd, int rows, int cols)
{
    struct winsize ws;
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    return ioctl(fd, TIOCSWINSZ, &ws);
}

int tty_get_winsize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        return -1;
    }
    if (rows) *rows = (int)ws.ws_row;
    if (cols) *cols = (int)ws.ws_col;
    return 0;
}
