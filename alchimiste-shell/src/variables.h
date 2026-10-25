#ifndef VARIABLES_H
#define VARIABLES_H

/*
 * variables — variables du shell, alias et expansion des mots.
 *
 * Deux niveaux de variables, comme dans Bash :
 *   - la table interne (locale) : « X=valeur » crée une variable locale ;
 *   - l'environnement exporté : « export X » copie X dans environ, ce qui
 *     la rend visible des commandes externes (execvp hérite d'environ).
 *
 * L'expansion (expand_word) interprète un mot BRUT issu du parser :
 *   - guillemets simples : contenu strictement littéral ;
 *   - guillemets doubles : variables expansées, \" \\ \$ protégés ;
 *   - antislash hors guillemets : protège le caractère suivant ;
 *   - $VAR, ${VAR}, $?, $$ ;
 *   - ~ et ~/... et ~utilisateur (uniquement en tête de mot non protégé) ;
 *   - jokers * ? [ ] (uniquement hors guillemets) via glob().
 */

/* --- table des variables --- */

int var_valid_name(const char *name);

int var_set(const char *name, const char *value);       /* locale         */
int var_set_exported(const char *name, const char *value); /* locale+export */
int var_export(const char *name);                       /* export NAME    */
int var_unset(const char *name);
const char *var_get(const char *name);                  /* NULL si absente */

void vars_init_from_environ(void); /* importe l'environnement initial */
void vars_print_all(void);        /* « set »  : toutes les variables  */
void vars_print_exported(void);   /* « export » sans argument         */
void vars_free_all(void);

/* Affectations temporaires (préfixe « X=v cmd ») : sauvegarde/restore. */
typedef struct VarSnapshot VarSnapshot;
VarSnapshot *vars_temp_assign(int n, char **names, char **values);
void vars_temp_restore(VarSnapshot *sn, int n);

/* --- alias --- */

int alias_set(const char *name, const char *value);
int alias_unset(const char *name);
const char *alias_get(const char *name); /* NULL si absent */
void alias_print_all(void);
void alias_free_all(void);

/* --- expansion --- */

#define EXP_NOGLOB 1u /* pas d'expansion des jokers (redirections, valeurs) */

/*
 * Expande un mot brut. Retourne un tableau de champs alloués (0 à n champs,
 * propriété transférée à l'appelant) ou NULL en cas d'erreur (message déjà
 * affiché). out_meta (optionnel) reçoit 1 si le résultat contient des
 * métacaractères de joker non protégés.
 */
char **expand_word(const char *raw, unsigned flags, int *out_count,
                   int *out_meta);

/* Libère les champs ET le tableau. */
void expand_free(char **fields, int count);

/*
 * Expande un mot en une seule chaîne (valeur d'affectation, nom de fichier
 * de redirection) : jamais de découpage en jokers. NULL en cas d'erreur.
 */
char *expand_string(const char *raw, int *out_meta);

/* Le mot brut est-il de la forme NAME=valeur ? */
int expand_is_assignment(const char *raw);

#endif /* VARIABLES_H */
