#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

/*
 * parser — analyse syntaxique : construit un arbre de commandes.
 *
 * Grammaire (sous-ensemble du shell POSIX) :
 *
 *   ligne      := liste EOF
 *   liste      := et_ou ((';' | '&') et_ou)*
 *   et_ou      := pipeline (('&&' | '||') pipeline)*
 *   pipeline   := simple ('|' simple)*
 *   simple     := (mot | redirection)+
 *   redirection:= [N] '>'  fichier      [N] '>>' fichier
 *                 [N] '<'  fichier      N   '>&' M
 *                 '&' '>' fichier  et  '&' '>>' fichier
 *
 * Les mots sont stockés BRUTS (guillemets inclus) : l'expansion
 * (variables, tilde, jokers) est réalisée entre l'analyse et l'exécution.
 */

typedef enum RedirKind {
    RDIR_IN,     /* < fichier        (fd 0 par défaut)                    */
    RDIR_OUT,    /* > fichier        (fd 1 par défaut, ou N pour N>)      */
    RDIR_APPEND, /* >> fichier                                             */
    RDIR_DUP,    /* N>&M : duplique le descripteur M sur N                */
    RDIR_ALL     /* &> ou &>> : sortie et erreurs vers le même fichier    */
} RedirKind;

typedef struct Redir {
    RedirKind kind;
    int  fd;        /* descripteur concerné (source)                      */
    int  fd_to;     /* descripteur cible (uniquement pour RDIR_DUP)       */
    int  append;    /* ajout pour RDIR_ALL (&>>)                          */
    char *filename; /* texte brut du fichier (expansion différée)         */
    struct Redir *next;
} Redir;

/* Commande simple : mots bruts + redirections, dans l'ordre de saisie. */
typedef struct Cmd {
    char **argv;      /* terminé par NULL */
    int    argc;
    Redir *redirs;    /* liste chaînée, ordre de déclaration conservé */
    Redir *redirs_tail;
} Cmd;

/* Pipeline : suite de commandes reliées par « | ». */
typedef struct Pipeline {
    Cmd **cmds;
    int   n;
    int   background; /* 1 si l'opérateur « & » suit le pipeline */
} Pipeline;

/* Séparateur entre deux pipelines d'une même liste. */
typedef enum Sep {
    SEP_SEQ, /* ;  : toujours exécuter la suivante            */
    SEP_AND, /* && : exécuter la suivante si statut == 0       */
    SEP_OR   /* || : exécuter la suivante si statut != 0       */
} Sep;

typedef struct Node {
    Pipeline *pl;
    Sep sep;          /* séparateur vers le noeud suivant */
    struct Node *next;
} Node;

typedef struct CommandLine {
    Node *head;
} CommandLine;

/*
 * Construit l'arbre à partir des tokens. Retourne NULL si la ligne est
 * vide. En cas d'erreur de syntaxe, affiche le message, positionne
 * *syntax_error à 1 et retourne NULL.
 */
CommandLine *parser_parse(const Token *toks, int *syntax_error);

/* Libère tout l'arbre (mots, redirections, pipelines, noeuds). */
void parser_free(CommandLine *cl);

#endif /* PARSER_H */
