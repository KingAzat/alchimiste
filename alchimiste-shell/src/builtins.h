#ifndef BUILTINS_H
#define BUILTINS_H

/*
 * builtins — registre des commandes internes.
 *
 * Une commande interne s'exécute dans le processus du shell car elle doit
 * modifier son état (répertoire courant, variables, tâches...) ou fait
 * partie des objectifs pédagogiques (cd, pwd, export, jobs, ...).
 */

/* Indice de la commande interne, -1 si le nom n'est pas interne. */
int builtin_index(const char *name);

/* Appelle la commande interne d'indice idx avec argc/argv déjà expansés. */
int builtin_dispatch_idx(int idx, int argc, char **argv);

const char *builtin_name(int idx);
const char *builtin_help_text(int idx);
int builtin_count(void);

#endif /* BUILTINS_H */
