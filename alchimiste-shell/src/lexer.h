#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

/*
 * lexer — analyse lexicale d'une ligne de commande.
 *
 * Produit une suite de tokens : mots (guillemets conservés dans le texte
 * brut, l'expansion est faite plus tard), opérateurs (; | & && || < > >>),
 * nombres de descripteurs (« 2 » dans « 2> err.txt ») et formes spéciales
 * (>&  &>  &>>).
 *
 * Règles gérées :
 *   - espaces et tabulations multiples ;
 *   - guillemets simples (aucune expansion à l'intérieur) ;
 *   - guillemets doubles (expansion des variables, échappements \" \\ \$) ;
 *   - caractères d'échappement « \ » ;
 *   - commentaires « # » en début de mot ;
 *   - détection des nombres de descripteurs (chiffres immédiatement
 *     suivis de « > ») ;
 *   - lignes vides ;
 *   - erreurs : guillemet non fermé, antislash en fin de ligne.
 */

typedef enum TokenType {
    TOK_WORD,         /* mot (texte brut, guillemets inclus)             */
    TOK_IO_NUMBER,    /* nombre devant une redirection (ex. « 2 » de 2>) */
    TOK_PIPE,         /* |   */
    TOK_SEMI,         /* ;   */
    TOK_AND,          /* &&  */
    TOK_OR,           /* ||  */
    TOK_AMP,          /* &   */
    TOK_LT,           /* <   */
    TOK_GT,           /* >   */
    TOK_APPEND,       /* >>  */
    TOK_GTAMP,        /* >&  (duplication de descripteur)                */
    TOK_RALL,         /* &>  (sortie + erreurs)                          */
    TOK_RALL_APPEND,  /* &>> */
    TOK_EOF           /* sentinelle de fin de tableau                    */
} TokenType;

typedef struct Token {
    TokenType type;
    char     *text; /* texte du mot (TOK_WORD / TOK_IO_NUMBER), sinon NULL */
} Token;

/*
 * Découpe « line » en tokens. Retourne un tableau alloué terminé par un
 * token TOK_EOF ; *count reçoit le nombre de tokens Y COMPRIS la sentinelle.
 * En cas d'erreur de syntaxe, affiche le message et retourne NULL.
 */
Token *lexer_tokenize(const char *line, size_t *count);
void   lexer_free_tokens(Token *toks, size_t count);

/* Nom lisible d'un type de token (messages d'erreur du parser). */
const char *token_type_name(TokenType t);

#endif /* LEXER_H */
