#include "relay.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include "terminal.h"

#define BUF_SIZE 4096

static volatile sig_atomic_t g_child_exited = 0;
static int g_resize_pipe[2] = {-1, -1};

static void sigchld_handler(int sig)
{
    (void)sig;
    g_child_exited = 1;
}

/* SIGWINCH : écrit un octet dans le self-pipe pour réveiller le select()
 * en sécurité (un pipe est "async-signal-safe" ; ioctl ne l'est pas). */
static void sigwinch_handler(int sig)
{
    (void)sig;
    char c = 'R';
    ssize_t r = write(g_resize_pipe[1], &c, 1);
    (void)r;
}

/* Prépare le self-pipe et installe le gestionnaire SIGWINCH. */
static int relay_setup_resize(void)
{
    if (pipe(g_resize_pipe) < 0) {
        perror("pipe");
        return -1;
    }

    /* Non bloquant : si l'octet d'un signal précédent n'a pas été
     * consommé, le write du handler échoue avec EAGAIN, ce n'est pas grave. */
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(g_resize_pipe[i], F_GETFL);
        if (flags >= 0)
            fcntl(g_resize_pipe[i], F_SETFL, flags | O_NONBLOCK);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; /* le pipe réveille select() de lui-même */
    sigaction(SIGWINCH, &sa, NULL);
    return 0;
}

/* Vide le self-pipe et transmet la nouvelle taille au shell enfant. */
static void relay_handle_resize(int pty_master)
{
    char buf[64];
    ssize_t n;
    do {
        n = read(g_resize_pipe[0], buf, sizeof(buf));
    } while (n > 0);

    int r, c;
    if (tty_get_winsize(&r, &c) == 0)
        tty_set_winsize(pty_master, r, c);
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t r = write(fd, buf + written, len - written);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        written += (size_t)r;
    }
    return 0;
}

/* Lecture sur pty_master vers stdout. Retourne 0 si EOF ou erreur fatale,
 * 1 si des données ont été lues et transmises. */
static int relay_pty_to_stdout(int pty_master)
{
    char buf[BUF_SIZE];
    ssize_t n = read(pty_master, buf, BUF_SIZE);
    if (n > 0) {
        if (write_all(STDOUT_FILENO, buf, (size_t)n) < 0) {
            return 0;
        }
        return 1;
    }
    if (errno == EINTR || errno == EAGAIN)
        return 1;
    return 0; /* le shell enfant a fermé le pty, ou EOF */
}

/* Lecture sur stdin vers pty_master. Retourne 0 si EOF (Ctrl+D),
 * 1 si des données ont été transmises. */
static int relay_stdin_to_pty(int pty_master)
{
    char buf[BUF_SIZE];
    ssize_t n = read(STDIN_FILENO, buf, BUF_SIZE);
    if (n > 0) {
        if (write_all(pty_master, buf, (size_t)n) < 0)
            return 0;
        return 1;
    }
    if (errno == EINTR || errno == EAGAIN)
        return 1;
    return 0; /* EOF : plus rien à transmettre */
}

int relay_run(int pty_master, int *child_status)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* sans SA_RESTART : select est coupé par SIGCHLD */
    sigaction(SIGCHLD, &sa, NULL);

    int resize_fd = -1;
    if (relay_setup_resize() == 0) {
        resize_fd = g_resize_pipe[0];
    }

    int running = 1;
    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(pty_master, &readfds);
        int maxfd = (STDIN_FILENO > pty_master ? STDIN_FILENO : pty_master) + 1;
        if (resize_fd >= 0) {
            FD_SET(resize_fd, &readfds);
            if (resize_fd > maxfd - 1)
                maxfd = resize_fd + 1;
        }

        int ready = select(maxfd, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                if (g_child_exited)
                    break; /* le shell enfant s'est terminé */
                continue;
            }
            perror("select");
            return -1;
        }

        if (resize_fd >= 0 && FD_ISSET(resize_fd, &readfds)) {
            relay_handle_resize(pty_master);
        }

        if (FD_ISSET(pty_master, &readfds)) {
            if (!relay_pty_to_stdout(pty_master)) {
                running = 0;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (!relay_stdin_to_pty(pty_master)) {
                running = 0;
            }
        }
    }

    /* Attend l'enfant et récupère son statut */
    if (resize_fd >= 0) {
        /* Le terminal va être restauré : on arrête de suivre SIGWINCH */
        signal(SIGWINCH, SIG_DFL);
        close(g_resize_pipe[0]);
        close(g_resize_pipe[1]);
        g_resize_pipe[0] = g_resize_pipe[1] = -1;
    }
    pid_t waited = waitpid(-1, child_status, 0);
    if (waited < 0 && errno != ECHILD) {
        perror("waitpid");
        return -1;
    }

    return 0;
}
