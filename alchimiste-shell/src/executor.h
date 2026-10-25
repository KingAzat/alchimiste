#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

/*
 * executor — exécution d'un arbre de commandes (CommandLine).
 * Retourne le code de sortie de la dernière commande exécutée.
 */
int executor_run(CommandLine *cl);

#endif /* EXECUTOR_H */
