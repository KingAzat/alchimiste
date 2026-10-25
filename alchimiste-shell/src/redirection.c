/*
 * redirection — application des redirections sur les descripteurs.
 */
#include "redirection.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int redirection_save(int saved[3])
{
    int i;

    for (i = 0; i < 3; i++) {
        saved[i] = dup(i);
        if (saved[i] < 0) {
            int k;

            for (k = 0; k < i; k++)
                close(saved[k]);
            return -1;
        }
    }
    return 0;
}

void redirection_restore(const int saved[3])
{
    int i;

    for (i = 0; i < 3; i++) {
        if (saved[i] >= 0) {
            dup2(saved[i], i);
            close(saved[i]);
        }
    }
}

int redirection_apply(const Redir *r)
{
    for (; r != NULL; r = r->next) {
        int fd = -1;
        int ok = 1;

        switch (r->kind) {
        case RDIR_IN: /* < fichier */
            fd = open(r->filename, O_RDONLY);
            break;

        case RDIR_OUT: /* > fichier (écrasement) */
            fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            break;

        case RDIR_APPEND: /* >> fichier (ajout) */
            fd = open(r->filename, O_WRONLY | O_CREAT | O_APPEND, 0666);
            break;

        case RDIR_ALL: /* &> et &>> : sortie + erreurs */
            fd = open(r->filename,
                      O_WRONLY | O_CREAT | (r->append ? O_APPEND : O_TRUNC),
                      0666);
            break;

        case RDIR_DUP: /* N>&M : dup2(M, N) — ex. 2>&1 */
            if (dup2(r->fd_to, r->fd) < 0) {
                fprintf(stderr, "alchimiste-shell: >&%d : %s\n", r->fd_to,
                        strerror(errno));
                return -1;
            }
            break;
        }

        if (r->kind == RDIR_DUP)
            continue;

        if (fd < 0) {
            fprintf(stderr, "alchimiste-shell: %s : %s\n", r->filename,
                    strerror(errno));
            return -1;
        }

        /* Les droits 0666 sont filtrés par l'umask du processus. */
        if (r->kind == RDIR_ALL) {
            ok = (dup2(fd, STDOUT_FILENO) >= 0 &&
                  dup2(fd, STDERR_FILENO) >= 0);
        } else {
            ok = (dup2(fd, r->fd) >= 0);
        }
        close(fd);

        if (!ok) {
            fprintf(stderr, "alchimiste-shell: %s : %s\n", r->filename,
                    strerror(errno));
            return -1;
        }
    }
    return 0;
}
