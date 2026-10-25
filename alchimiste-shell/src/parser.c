/*
 * parser — analyse syntaxique par descente récursive.
 *
 * Le parser consomme le tableau de tokens produit par le lexer et construit
 * une CommandLine : une liste chaînée de pipelines reliés par des
 * séparateurs (;, &&, ||). Chaque pipeline contient une ou plusieurs
 * commandes simples (mots + redirections).
 */
#include "parser.h"
#include "shell.h" /* limites : MAX_PIPE_CMDS */
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Curseur sur le flux de tokens                                        */
/* ------------------------------------------------------------------ */

typedef struct P {
    const Token *t;
    size_t i;
} P;

static const Token *peek(P *p)
{
    return &p->t[p->i];
}

static const Token *advance(P *p)
{
    return &p->t[p->i++];
}

static int at(P *p, TokenType ty)
{
    return peek(p)->type == ty;
}

/* ------------------------------------------------------------------ */
/* Messages d'erreur                                                    */
/* ------------------------------------------------------------------ */

static void syntax_error_near(const Token *t)
{
    if (t->type == TOK_EOF) {
        fprintf(stderr, "alchimiste-shell: erreur de syntaxe : "
                        "fin de ligne inattendue\n");
    } else if (t->text != NULL) {
        fprintf(stderr, "alchimiste-shell: erreur de syntaxe près du "
                        "symbole inattendu « %s »\n", t->text);
    } else {
        fprintf(stderr, "alchimiste-shell: erreur de syntaxe près du "
                        "symbole inattendu « %s »\n", token_type_name(t->type));
    }
}

/* Un token peut-il commencer une commande ? */
static int can_start_command(TokenType t)
{
    switch (t) {
    case TOK_WORD:
    case TOK_IO_NUMBER:
    case TOK_LT:
    case TOK_GT:
    case TOK_APPEND:
    case TOK_GTAMP:
    case TOK_RALL:
    case TOK_RALL_APPEND:
        return 1;
    default:
        return 0;
    }
}

static int all_digits(const char *s)
{
    if (*s == '\0')
        return 0;
    for (; *s != '\0'; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Construction des structures                                          */
/* ------------------------------------------------------------------ */

static Cmd *cmd_new(void)
{
    Cmd *c = xcalloc(1, sizeof *c);

    c->argv = xmalloc(sizeof(char *) * 8);
    c->argv[0] = NULL;
    return c;
}

static void cmd_free(Cmd *c)
{
    int i;
    Redir *r, *next;

    if (c == NULL)
        return;
    for (i = 0; i < c->argc; i++)
        free(c->argv[i]);
    free(c->argv);
    for (r = c->redirs; r != NULL; r = next) {
        next = r->next;
        free(r->filename);
        free(r);
    }
    free(c);
}

static void cmd_add_arg(Cmd *c, const char *raw)
{
    c->argv = xrealloc(c->argv, sizeof(char *) * (size_t)(c->argc + 2));
    c->argv[c->argc++] = xstrdup(raw);
    c->argv[c->argc] = NULL;
}

static Redir *redir_new(RedirKind kind, int fd)
{
    Redir *r = xcalloc(1, sizeof *r);

    r->kind = kind;
    r->fd = fd;
    return r;
}

static void cmd_add_redir(Cmd *c, Redir *r)
{
    if (c->redirs_tail == NULL) {
        c->redirs = c->redirs_tail = r;
    } else {
        c->redirs_tail->next = r;
        c->redirs_tail = r;
    }
}

/* ------------------------------------------------------------------ */
/* Commande simple                                                      */
/* ------------------------------------------------------------------ */

/*
 * simple := (mot | redirection)+
 * Une commande peut n'être constituée que de redirections (« > f »),
 * auquel cas argv est vide et la commande ne fait que créer le fichier.
 */
static Cmd *parse_simple(P *p)
{
    Cmd *c = cmd_new();

    for (;;) {
        const Token *t = peek(p);

        switch (t->type) {
        case TOK_WORD:
            cmd_add_arg(c, t->text);
            advance(p);
            break;

        case TOK_IO_NUMBER: { /* « 2> err », « 2>> err », « 2<& », « 2>&1 » */
            int fd = atoi(t->text);
            const Token *op;

            advance(p);
            op = peek(p);
            if (op->type == TOK_GT || op->type == TOK_APPEND ||
                op->type == TOK_LT) {
                Redir *r;

                advance(p);
                if (!at(p, TOK_WORD)) {
                    syntax_error_near(peek(p));
                    cmd_free(c);
                    return NULL;
                }
                if (op->type == TOK_LT)
                    r = redir_new(RDIR_IN, fd);
                else if (op->type == TOK_APPEND)
                    r = redir_new(RDIR_APPEND, fd);
                else
                    r = redir_new(RDIR_OUT, fd);
                r->filename = xstrdup(peek(p)->text);
                advance(p);
                cmd_add_redir(c, r);
            } else if (op->type == TOK_GTAMP) { /* N>&M */
                Redir *r;

                advance(p);
                if (!at(p, TOK_WORD) || !all_digits(peek(p)->text)) {
                    syntax_error_near(peek(p));
                    cmd_free(c);
                    return NULL;
                }
                r = redir_new(RDIR_DUP, fd);
                r->fd_to = atoi(peek(p)->text);
                r->filename = xstrdup(peek(p)->text);
                advance(p);
                cmd_add_redir(c, r);
            } else {
                syntax_error_near(op);
                cmd_free(c);
                return NULL;
            }
            break;
        }

        case TOK_LT:
        case TOK_GT:
        case TOK_APPEND:
        case TOK_RALL:
        case TOK_RALL_APPEND: { /* redirection simple : > f, >> f, < f, &> f */
            TokenType ty = advance(p)->type;
            Redir *r;

            if (!at(p, TOK_WORD)) {
                syntax_error_near(peek(p));
                cmd_free(c);
                return NULL;
            }
            if (ty == TOK_LT)
                r = redir_new(RDIR_IN, 0);      /* entrée standard */
            else if (ty == TOK_GT)
                r = redir_new(RDIR_OUT, 1);     /* sortie standard */
            else if (ty == TOK_APPEND)
                r = redir_new(RDIR_APPEND, 1);
            else if (ty == TOK_RALL)
                r = redir_new(RDIR_ALL, 1);
            else
                r = redir_new(RDIR_ALL, 1);
            if (ty == TOK_RALL_APPEND)
                r->append = 1;
            r->filename = xstrdup(peek(p)->text);
            advance(p);
            cmd_add_redir(c, r);
            break;
        }

        case TOK_GTAMP: { /* >&M : duplique M sur la sortie standard */
            Redir *r;

            advance(p);
            if (!at(p, TOK_WORD) || !all_digits(peek(p)->text)) {
                syntax_error_near(peek(p));
                cmd_free(c);
                return NULL;
            }
            r = redir_new(RDIR_DUP, 1); /* sortie standard vers M */
            r->fd_to = atoi(peek(p)->text);
            r->filename = xstrdup(peek(p)->text);
            advance(p);
            cmd_add_redir(c, r);
            break;
        }

        default:
            goto done;
        }
    }

done:
    if (c->argc == 0 && c->redirs == NULL) {
        /* Aucun mot ni redirection : le token courant est inattendu. */
        syntax_error_near(peek(p));
        cmd_free(c);
        return NULL;
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* Pipeline                                                             */
/* ------------------------------------------------------------------ */

static void pipeline_free(Pipeline *pl)
{
    int i;

    if (pl == NULL)
        return;
    for (i = 0; i < pl->n; i++)
        cmd_free(pl->cmds[i]);
    free(pl->cmds);
    free(pl);
}

static Pipeline *parse_pipeline(P *p)
{
    Pipeline *pl = xcalloc(1, sizeof *pl);
    Cmd *c;

    pl->cmds = xmalloc(sizeof(Cmd *) * MAX_PIPE_CMDS);

    c = parse_simple(p);
    if (c == NULL) {
        pipeline_free(pl);
        return NULL;
    }
    pl->cmds[pl->n++] = c;

    while (at(p, TOK_PIPE)) {
        advance(p);
        if (!can_start_command(peek(p)->type)) {
            syntax_error_near(peek(p));
            pipeline_free(pl);
            return NULL;
        }
        if (pl->n >= MAX_PIPE_CMDS) {
            fprintf(stderr, "alchimiste-shell: trop de commandes dans le "
                            "pipeline (maximum %d)\n", MAX_PIPE_CMDS);
            pipeline_free(pl);
            return NULL;
        }
        c = parse_simple(p);
        if (c == NULL) {
            pipeline_free(pl);
            return NULL;
        }
        pl->cmds[pl->n++] = c;
    }
    return pl;
}

/* ------------------------------------------------------------------ */
/* Liste avec && et ||                                                  */
/* ------------------------------------------------------------------ */

static Node *node_new(Pipeline *pl)
{
    Node *n = xcalloc(1, sizeof *n);

    n->pl = pl;
    n->sep = SEP_SEQ;
    return n;
}

static void node_chain_free(Node *head)
{
    Node *n, *next;

    for (n = head; n != NULL; n = next) {
        next = n->next;
        pipeline_free(n->pl);
        free(n);
    }
}

static Node *parse_and_or(P *p)
{
    Pipeline *pl = parse_pipeline(p);
    Node *head, *tail;

    if (pl == NULL)
        return NULL;
    head = tail = node_new(pl);

    while (at(p, TOK_AND) || at(p, TOK_OR)) {
        Sep sep = (advance(p)->type == TOK_AND) ? SEP_AND : SEP_OR;

        pl = parse_pipeline(p);
        if (pl == NULL) {
            node_chain_free(head);
            return NULL;
        }
        tail->sep = sep;
        tail->next = node_new(pl);
        tail = tail->next;
    }
    return head;
}

/* ------------------------------------------------------------------ */
/* Ligne complète                                                       */
/* ------------------------------------------------------------------ */

void parser_free(CommandLine *cl)
{
    if (cl == NULL)
        return;
    node_chain_free(cl->head);
    free(cl);
}

CommandLine *parser_parse(const Token *toks, int *syntax_error)
{
    P p = { toks, 0 };
    CommandLine *cl;
    Node *tail = NULL;

    *syntax_error = 0;

    if (at(&p, TOK_EOF))
        return NULL; /* ligne vide : rien à faire, pas d'erreur */

    cl = xcalloc(1, sizeof *cl);

    for (;;) {
        Node *sub = parse_and_or(&p);
        Node *it;

        if (sub == NULL) {
            parser_free(cl);
            *syntax_error = 1;
            return NULL;
        }

        /* chaîne la liste de noeuds obtenue */
        if (cl->head == NULL) {
            cl->head = sub;
        } else {
            tail->next = sub;
        }
        for (it = sub; it->next != NULL; it = it->next)
            ;
        tail = it;

        if (at(&p, TOK_SEMI)) {
            advance(&p);
            if (at(&p, TOK_EOF))
                break; /* « ; » final autorisé */
            if (!can_start_command(peek(&p)->type)) {
                syntax_error_near(peek(&p));
                parser_free(cl);
                *syntax_error = 1;
                return NULL;
            }
            continue;
        }

        if (at(&p, TOK_AMP)) {
            advance(&p);
            tail->pl->background = 1; /* s'applique au dernier pipeline */
            if (at(&p, TOK_EOF))
                break; /* « & » final */
            if (!can_start_command(peek(&p)->type)) {
                syntax_error_near(peek(&p));
                parser_free(cl);
                *syntax_error = 1;
                return NULL;
            }
            continue;
        }

        break; /* normalement TOK_EOF */
    }

    if (!at(&p, TOK_EOF)) { /* token inattendu en fin de ligne */
        syntax_error_near(peek(&p));
        parser_free(cl);
        *syntax_error = 1;
        return NULL;
    }
    return cl;
}
