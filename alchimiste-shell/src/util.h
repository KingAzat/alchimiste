#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/*
 * util — petites fonctions d'allocation vérifiées, partagées par tous les
 * modules. En cas d'échec d'allocation, le programme affiche un message et
 * se termine : un shell ne peut pas continuer de façon fiable sans mémoire.
 */

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *ptr, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/*
 * Recherche « name » dans la liste de répertoires « path_list » (format
 * PATH, composante vide = répertoire courant) avec le mode access()
 * indiqué (R_OK, X_OK...). Si name contient un '/', il est testé tel
 * quel. Retourne un chemin alloué (à libérer) ou NULL.
 */
char *path_search(const char *name, const char *path_list, int mode);

#endif /* UTIL_H */
