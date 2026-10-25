/*
 * history — historique des commandes saisies.
 */
#include "history.h"
#include "shell.h"
#include "variables.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY_FILENAME "/.alchimiste_history"

static char **g_entries;
static size_t g_count;
static size_t g_cap;

/* Chemin du fichier d'historique (NULL si HOME est inconnu). */
static char *history_file_path(void)
{
    const char *home = var_get("HOME");
    size_t need;
    char *path;

    if (home == NULL || *home == '\0')
        return NULL;
    need = strlen(home) + strlen(HISTORY_FILENAME) + 1;
    path = xmalloc(need);
    snprintf(path, need, "%s%s", home, HISTORY_FILENAME);
    return path;
}

void history_init(void)
{
    char *path;
    FILE *f;
    char line[MAX_LINE];

    if (!g_shell.interactive)
        return; /* pas de fichier en mode script / tube */

    path = history_file_path();
    if (path == NULL)
        return;

    f = fopen(path, "r");
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            size_t len = strcspn(line, "\n");

            line[len] = '\0';
            if (len > 0 && line[len - 1] == '\r')
                line[len - 1] = '\0';
            if (line[0] != '\0')
                history_add(line);
        }
        fclose(f);
    }
    free(path);
}

void history_add(const char *line)
{
    /* Les lignes vides ou blanches n'entrent pas dans l'historique. */
    if (line[strspn(line, " \t")] == '\0')
        return;

    /* Capacité maximale atteinte : l'entrée la plus ancienne est perdue. */
    if (g_count == HISTORY_CAP) {
        free(g_entries[0]);
        memmove(g_entries, g_entries + 1,
                sizeof(char *) * (HISTORY_CAP - 1));
        g_count--;
    }

    if (g_count == g_cap) {
        g_cap = (g_cap == 0) ? 64 : g_cap * 2;
        g_entries = xrealloc(g_entries, sizeof(char *) * g_cap);
    }
    g_entries[g_count++] = xstrdup(line);
}

void history_save(void)
{
    char *path;
    FILE *f;
    size_t i;

    if (!g_shell.interactive)
        return;

    path = history_file_path();
    if (path == NULL)
        return;

    f = fopen(path, "w");
    if (f != NULL) {
        for (i = 0; i < g_count; i++)
            fprintf(f, "%s\n", g_entries[i]);
        fclose(f);
    }
    free(path);
}

void history_clear(void)
{
    size_t i;

    for (i = 0; i < g_count; i++)
        free(g_entries[i]);
    g_count = 0;
}

int history_count(void)
{
    return (int)g_count;
}

const char *history_get(int idx)
{
    if (idx < 0 || (size_t)idx >= g_count)
        return NULL;
    return g_entries[idx];
}

void history_free(void)
{
    history_clear();
    free(g_entries);
    g_entries = NULL;
    g_cap = 0;
}
