#ifndef REDIRECTION_H
#define REDIRECTION_H

#include "parser.h"

/*
 * redirection — application des redirections sur les descripteurs.
 *
 * Utilise open() + dup2() + close(). Les redirections sont appliquées
 * DANS L'ORDRE de déclaration, ce qui reproduit la sémantique de Bash :
 *   « > f 2>&1 » envoie tout vers f, « 2>&1 > f » garde l'ancienne sortie.
 *
 * Deux contextes d'utilisation :
 *   - dans un fils (avant execvp) : aucun retour en arrière nécessaire ;
 *   - pour une commande interne dans le shell : les descripteurs
 *     0/1/2 sont sauvegardés (redirection_save) puis restaurés
 *     (redirection_restore) après l'exécution.
 */

/* Applique la liste de redirections. 0 = succès, -1 = erreur (affichée). */
int redirection_apply(const Redir *list);

/* Sauvegarde stdin/stdout/stderr (dup). -1 = échec. */
int redirection_save(int saved[3]);

/* Restaure stdin/stdout/stderr à partir d'une sauvegarde. */
void redirection_restore(const int saved[3]);

#endif /* REDIRECTION_H */
