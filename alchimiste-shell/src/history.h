#ifndef HISTORY_H
#define HISTORY_H

/*
 * history — historique des commandes saisies.
 *
 * L'historique est conservé en mémoire pendant la session et sauvegardé,
 * en mode interactif uniquement, dans ~/.alchimiste_history (fichier
 * dédié : l'historique personnel de Bash n'est jamais touché).
 */
void history_init(void);           /* charge ~/.alchimiste_history       */
void history_add(const char *line); /* ignore les lignes vides/blanches  */
void history_save(void);           /* écrase le fichier (mode interactif) */
void history_clear(void);
int  history_count(void);
const char *history_get(int idx);  /* NULL si indice invalide */
void history_free(void);

#endif /* HISTORY_H */
