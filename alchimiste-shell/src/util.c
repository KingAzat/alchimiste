#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void oom(void)
{
    fprintf(stderr, "alchimiste-shell: mémoire insuffisante\n");
    exit(1);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p == NULL)
        oom();
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n, size);
    if (p == NULL)
        oom();
    return p;
}

void *xrealloc(void *ptr, size_t n)
{
    void *p = realloc(ptr, n);
    if (p == NULL)
        oom();
    return p;
}

char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (p == NULL)
        oom();
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *path_search(const char *name, const char *path_list, int mode)
{
    char full[4096];
    const char *p;

    if (name == NULL || name[0] == '\0')
        return NULL;

    /* Un chemin contenant '/' est testé tel quel. */
    if (strchr(name, '/') != NULL) {
        if (access(name, mode) == 0)
            return xstrdup(name);
        return NULL;
    }

    if (path_list == NULL || *path_list == '\0')
        path_list = "/bin:/usr/bin"; /* repli, comme execvp */

    p = path_list;
    for (;;) {
        const char *colon = strchr(p, ':');
        size_t dlen = (colon != NULL) ? (size_t)(colon - p) : strlen(p);
        int nwritten;

        if (dlen == 0)
            nwritten = snprintf(full, sizeof full, "./%s", name);
        else
            nwritten = snprintf(full, sizeof full, "%.*s/%s", (int)dlen, p,
                                name);
        if (nwritten > 0 && (size_t)nwritten < sizeof full &&
            access(full, mode) == 0)
            return xstrdup(full);

        if (colon == NULL)
            break;
        p = colon + 1;
    }
    return NULL;
}
