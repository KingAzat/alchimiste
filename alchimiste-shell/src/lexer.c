/*
 * lexer — analyse lexicale d'une ligne de commande.
 *
 * Le lexer ne fait AUCUNE expansion : il découpe la ligne en tokens en
 * respectant les guillemets et les antislashes. Les mots gardent leur
 * texte brut (guillemets inclus) ; les guillemets sont interprétés plus
 * tard, au moment de l'expansion (variables.c).
 */
#include "lexer.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_blank(char c)
{
    return c == ' ' || c == '\t';
}

static int is_operator_char(char c)
{
    return c != '\0' && strchr(";|&<>", c) != NULL;
}

const char *token_type_name(TokenType t)
{
    switch (t) {
    case TOK_WORD:        return "word";
    case TOK_IO_NUMBER:   return "nombre de descripteur";
    case TOK_PIPE:        return "|";
    case TOK_SEMI:        return ";";
    case TOK_AND:         return "&&";
    case TOK_OR:          return "||";
    case TOK_AMP:         return "&";
    case TOK_LT:          return "<";
    case TOK_GT:          return ">";
    case TOK_APPEND:      return ">>";
    case TOK_GTAMP:       return ">&";
    case TOK_RALL:        return "&>";
    case TOK_RALL_APPEND: return "&>>";
    case TOK_EOF:         return "fin de ligne";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Liste dynamique de tokens                                            */
/* ------------------------------------------------------------------ */

typedef struct TokList {
    Token *items;
    size_t count;
    size_t cap;
} TokList;

static void toklist_push(TokList *l, TokenType type, const char *text)
{
    if (l->count == l->cap) {
        l->cap = (l->cap == 0) ? 16 : l->cap * 2;
        l->items = xrealloc(l->items, l->cap * sizeof *l->items);
    }
    l->items[l->count].type = type;
    l->items[l->count].text = (text != NULL) ? xstrdup(text) : NULL;
    l->count++;
}

void lexer_free_tokens(Token *toks, size_t count)
{
    size_t i;

    if (toks == NULL)
        return;
    for (i = 0; i < count; i++)
        free(toks[i].text);
    free(toks);
}

/* ------------------------------------------------------------------ */
/* Lecture d'un mot                                                     */
/* ------------------------------------------------------------------ */

#define WR_ERR_OK        0
#define WR_ERR_BACKSLASH (-1) /* antislash en fin de ligne              */
#define WR_ERR_SQUOTE    (-2) /* guillemet simple non fermé             */
#define WR_ERR_DQUOTE    (-3) /* guillemet double non fermé             */

/*
 * Lit un mot à partir de *pos. Le texte brut est copié dans *out (guillemets
 * et antislashes conservés). Les opérateurs et les blancs non protégés
 * terminent le mot. Retourne un code WR_ERR_* en cas d'erreur.
 */
static int scan_word(const char *line, size_t *pos, char **out)
{
    size_t i = *pos;
    size_t len = 0;
    char *buf = xmalloc(strlen(line + i) + 1);

    while (line[i] != '\0') {
        char c = line[i];

        if (is_blank(c) || is_operator_char(c))
            break;

        if (c == '\\') {
            if (line[i + 1] == '\0') { /* « \ » en fin de ligne */
                free(buf);
                return WR_ERR_BACKSLASH;
            }
            buf[len++] = line[i];      /* l'antislash et le caractère   */
            buf[len++] = line[i + 1];  /* protégé sont conservés bruts  */
            i += 2;
            continue;
        }

        if (c == '\'') { /* guillemets simples : tout est littéral */
            buf[len++] = line[i++];
            while (line[i] != '\0' && line[i] != '\'')
                buf[len++] = line[i++];
            if (line[i] == '\0') {
                free(buf);
                return WR_ERR_SQUOTE;
            }
            buf[len++] = line[i++];
            continue;
        }

        if (c == '"') { /* guillemets doubles : \" \\ \$ protégés */
            buf[len++] = line[i++];
            while (line[i] != '\0' && line[i] != '"') {
                if (line[i] == '\\' && line[i + 1] != '\0' &&
                    strchr("\"\\$", line[i + 1]) != NULL) {
                    buf[len++] = line[i++];
                    buf[len++] = line[i++];
                    continue;
                }
                buf[len++] = line[i++];
            }
            if (line[i] == '\0') {
                free(buf);
                return WR_ERR_DQUOTE;
            }
            buf[len++] = line[i++];
            continue;
        }

        buf[len++] = line[i++];
    }

    buf[len] = '\0';
    *out = buf;
    *pos = i;
    return WR_ERR_OK;
}

/* ------------------------------------------------------------------ */
/* Point d'entrée                                                       */
/* ------------------------------------------------------------------ */

Token *lexer_tokenize(const char *line, size_t *count)
{
    TokList l = { NULL, 0, 0 };
    size_t i = 0;

    while (line[i] != '\0') {
        char c = line[i];

        if (is_blank(c)) { /* espaces multiples ignorés */
            i++;
            continue;
        }

        if (c == '#') /* commentaire : jusqu'à la fin de la ligne */
            break;

        /* Nombre de descripteur : chiffres immédiatement suivis de « > ». */
        if (isdigit((unsigned char)c)) {
            size_t j = i;

            while (isdigit((unsigned char)line[j]))
                j++;
            if (line[j] == '>') {
                char *num = xstrndup(line + i, j - i);

                toklist_push(&l, TOK_IO_NUMBER, num);
                free(num);
                i = j; /* le « > » sera lu comme opérateur au tour suivant */
                continue;
            }
            /* sinon : mot ordinaire commençant par un chiffre */
        }

        switch (c) {
        case '|':
            if (line[i + 1] == '|') {
                toklist_push(&l, TOK_OR, NULL);
                i += 2;
            } else {
                toklist_push(&l, TOK_PIPE, NULL);
                i++;
            }
            break;

        case '&':
            if (line[i + 1] == '&') {
                toklist_push(&l, TOK_AND, NULL);
                i += 2;
            } else if (line[i + 1] == '>') {
                if (line[i + 2] == '>') {
                    toklist_push(&l, TOK_RALL_APPEND, NULL);
                    i += 3;
                } else {
                    toklist_push(&l, TOK_RALL, NULL);
                    i += 2;
                }
            } else {
                toklist_push(&l, TOK_AMP, NULL);
                i++;
            }
            break;

        case ';':
            toklist_push(&l, TOK_SEMI, NULL);
            i++;
            break;

        case '<':
            toklist_push(&l, TOK_LT, NULL);
            i++;
            break;

        case '>':
            if (line[i + 1] == '>') {
                toklist_push(&l, TOK_APPEND, NULL);
                i += 2;
            } else if (line[i + 1] == '&') {
                toklist_push(&l, TOK_GTAMP, NULL);
                i += 2;
            } else {
                toklist_push(&l, TOK_GT, NULL);
                i++;
            }
            break;

        default: { /* un mot */
            char *w = NULL;
            int rc = scan_word(line, &i, &w);

            if (rc != WR_ERR_OK) {
                if (rc == WR_ERR_BACKSLASH)
                    fprintf(stderr, "alchimiste-shell: erreur de syntaxe : "
                                    "antislash en fin de ligne\n");
                else if (rc == WR_ERR_SQUOTE)
                    fprintf(stderr, "alchimiste-shell: erreur de syntaxe : "
                                    "guillemet simple non fermé\n");
                else
                    fprintf(stderr, "alchimiste-shell: erreur de syntaxe : "
                                    "guillemet double non fermé\n");
                lexer_free_tokens(l.items, l.count);
                return NULL;
            }
            toklist_push(&l, TOK_WORD, w);
            free(w);
            break;
        }
        }
    }

    toklist_push(&l, TOK_EOF, NULL);
    *count = l.count;
    return l.items;
}
