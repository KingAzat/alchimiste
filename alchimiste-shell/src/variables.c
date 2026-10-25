/*
 * variables — variables du shell, alias et expansion des mots.
 */
#include "variables.h"
#include "shell.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <glob.h>

/* ------------------------------------------------------------------ */
/* Table des variables                                                  */
/* ------------------------------------------------------------------ */

typedef struct Var {
    char *name;
    char *value;
    int   exported;
    struct Var *next;
} Var;

static Var *g_vars;

static Var *var_find(const char *name)
{
    Var *v;

    for (v = g_vars; v != NULL; v = v->next)
        if (strcmp(v->name, name) == 0)
            return v;
    return NULL;
}

int var_valid_name(const char *name)
{
    size_t i;

    if (!(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return 0;
    for (i = 1; name[i] != '\0'; i++)
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
            return 0;
    return 1;
}

int var_set(const char *name, const char *value)
{
    Var *v = var_find(name);

    if (v == NULL) {
        v = xcalloc(1, sizeof *v);
        v->name = xstrdup(name);
        v->next = g_vars;
        g_vars = v;
    }
    free(v->value);
    v->value = xstrdup(value);
    if (v->exported)
        setenv(name, value, 1); /* l'export est conservé : env synchronisée */
    return 0;
}

int var_set_exported(const char *name, const char *value)
{
    Var *v;

    var_set(name, value);
    v = var_find(name);
    if (v != NULL && !v->exported) {
        v->exported = 1;
        setenv(name, value, 1);
    }
    return 0;
}

int var_export(const char *name)
{
    Var *v = var_find(name);

    if (v == NULL) {
        /* export X sans valeur : crée X="" exportée, comme Bash */
        var_set_exported(name, "");
    } else if (!v->exported) {
        v->exported = 1;
        setenv(name, v->value, 1);
    }
    return 0;
}

int var_unset(const char *name)
{
    Var **pp;

    for (pp = &g_vars; *pp != NULL; pp = &(*pp)->next) {
        if (strcmp((*pp)->name, name) == 0) {
            Var *dead = *pp;

            *pp = dead->next;
            free(dead->name);
            free(dead->value);
            free(dead);
            unsetenv(name);
            return 0;
        }
    }
    return -1;
}

const char *var_get(const char *name)
{
    Var *v = var_find(name);

    return (v != NULL) ? v->value : NULL;
}

void vars_init_from_environ(void)
{
    extern char **environ;
    char **e;

    for (e = environ; *e != NULL; e++) {
        const char *eq = strchr(*e, '=');

        if (eq != NULL && eq != *e) {
            char *name = xstrndup(*e, (size_t)(eq - *e));

            if (var_valid_name(name))
                var_set_exported(name, eq + 1);
            free(name);
        }
    }
}

void vars_free_all(void)
{
    Var *v, *next;

    for (v = g_vars; v != NULL; v = next) {
        next = v->next;
        free(v->name);
        free(v->value);
        free(v);
    }
    g_vars = NULL;
}

/* Impression triée par nom (sortie stable, comparable entre exécutions). */
static int cmp_var_names(const void *a, const void *b)
{
    const Var *const *va = a;
    const Var *const *vb = b;

    return strcmp((*va)->name, (*vb)->name);
}

static void print_quoted_value(const char *v)
{
    putchar('"');
    for (; *v != '\0'; v++) {
        if (*v == '"' || *v == '\\')
            putchar('\\');
        putchar(*v);
    }
    putchar('"');
}

static void vars_print(int exported_only)
{
    size_t n = 0, i;
    Var **tab;
    Var *v;

    for (v = g_vars; v != NULL; v = v->next)
        n++;
    if (n == 0)
        return;

    tab = xmalloc(sizeof(Var *) * n);
    i = 0;
    for (v = g_vars; v != NULL; v = v->next)
        tab[i++] = v;
    qsort(tab, n, sizeof(Var *), cmp_var_names);

    for (i = 0; i < n; i++) {
        if (exported_only && !tab[i]->exported)
            continue;
        if (exported_only) {
            printf("export %s=", tab[i]->name);
            print_quoted_value(tab[i]->value);
            printf("\n");
        } else {
            printf("%s=%s\n", tab[i]->name, tab[i]->value);
        }
    }
    free(tab);
}

void vars_print_all(void)        { vars_print(0); }
void vars_print_exported(void)   { vars_print(1); }

/* ------------------------------------------------------------------ */
/* Affectations temporaires (« X=v cmd »)                               */
/* ------------------------------------------------------------------ */

struct VarSnapshot {
    char *name;
    char *old_value;
    int   existed;
};

VarSnapshot *vars_temp_assign(int n, char **names, char **values)
{
    VarSnapshot *sn;
    int i;

    if (n <= 0)
        return NULL;
    sn = xcalloc((size_t)n, sizeof *sn);
    for (i = 0; i < n; i++) {
        Var *v = var_find(names[i]);

        sn[i].name = xstrdup(names[i]);
        if (v != NULL) {
            sn[i].existed = 1;
            sn[i].old_value = xstrdup(v->value);
        }
        var_set(names[i], values[i]);
    }
    return sn;
}

void vars_temp_restore(VarSnapshot *sn, int n)
{
    int i;

    if (sn == NULL)
        return;
    for (i = 0; i < n; i++) {
        if (sn[i].existed)
            var_set(sn[i].name, sn[i].old_value);
        else
            var_unset(sn[i].name);
        free(sn[i].name);
        free(sn[i].old_value);
    }
    free(sn);
}

/* ------------------------------------------------------------------ */
/* Alias                                                                */
/* ------------------------------------------------------------------ */

typedef struct Alias {
    char *name;
    char *value;
    struct Alias *next;
} Alias;

static Alias *g_aliases;

int alias_set(const char *name, const char *value)
{
    Alias *a;

    for (a = g_aliases; a != NULL; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            free(a->value);
            a->value = xstrdup(value);
            return 0;
        }
    }
    a = xcalloc(1, sizeof *a);
    a->name = xstrdup(name);
    a->value = xstrdup(value);
    a->next = g_aliases;
    g_aliases = a;
    return 0;
}

int alias_unset(const char *name)
{
    Alias **pp;

    for (pp = &g_aliases; *pp != NULL; pp = &(*pp)->next) {
        if (strcmp((*pp)->name, name) == 0) {
            Alias *dead = *pp;

            *pp = dead->next;
            free(dead->name);
            free(dead->value);
            free(dead);
            return 0;
        }
    }
    return -1;
}

const char *alias_get(const char *name)
{
    Alias *a;

    for (a = g_aliases; a != NULL; a = a->next)
        if (strcmp(a->name, name) == 0)
            return a->value;
    return NULL;
}

void alias_print_all(void)
{
    Alias *a;

    for (a = g_aliases; a != NULL; a = a->next)
        printf("alias %s='%s'\n", a->name, a->value);
}

void alias_free_all(void)
{
    Alias *a, *next;

    for (a = g_aliases; a != NULL; a = next) {
        next = a->next;
        free(a->name);
        free(a->value);
        free(a);
    }
    g_aliases = NULL;
}

/* ------------------------------------------------------------------ */
/* Expansion                                                            */
/* ------------------------------------------------------------------ */

typedef struct Buf {
    char *s;
    size_t len, cap;
} Buf;

static void buf_reserve(Buf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        size_t want = (b->cap == 0) ? 64 : b->cap;

        while (want < b->len + extra + 1)
            want *= 2;
        b->cap = want;
        b->s = xrealloc(b->s, b->cap);
    }
}

static void buf_putc(Buf *b, char c)
{
    buf_reserve(b, 1);
    b->s[b->len++] = c;
    b->s[b->len] = '\0';
}

static void buf_puts(Buf *b, const char *s)
{
    size_t n = strlen(s);

    buf_reserve(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

static int name_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static int valid_name_n(const char *s, size_t n)
{
    size_t i;

    if (n == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_'))
        return 0;
    for (i = 1; i < n; i++)
        if (!name_char(s[i]))
            return 0;
    return 1;
}

/*
 * Expande la construction « $... » pointée par *pp.
 * in_dquote : à l'intérieur de guillemets doubles (pas de jokers).
 * Retourne -1 en cas d'erreur de syntaxe (${ sans }, nom invalide).
 */
static int expand_dollar(const char **pp, Buf *out, int in_dquote, int *meta)
{
    const char *p = *pp + 1; /* caractère suivant le '$' */

    if (*p == '?') { /* statut de la dernière commande */
        char tmp[16];

        snprintf(tmp, sizeof tmp, "%d", g_shell.last_status);
        buf_puts(out, tmp);
        *pp = p + 1;
        return 0;
    }

    if (*p == '$') { /* identifiant du shell */
        char tmp[16];

        snprintf(tmp, sizeof tmp, "%d", (int)getpid());
        buf_puts(out, tmp);
        *pp = p + 1;
        return 0;
    }

    if (*p == '{') { /* ${NOM} */
        const char *end = strchr(p + 1, '}');
        const char *v;
        size_t nlen;
        char name[256];

        if (end == NULL) {
            fprintf(stderr, "alchimiste-shell: ${ : substitution incorrecte\n");
            return -1;
        }
        nlen = (size_t)(end - (p + 1));
        if (nlen == 0 || nlen >= sizeof name || !valid_name_n(p + 1, nlen)) {
            fprintf(stderr, "alchimiste-shell: ${%.*s} : nom de variable "
                            "incorrect\n", (int)nlen, p + 1);
            return -1;
        }
        memcpy(name, p + 1, nlen);
        name[nlen] = '\0';
        v = var_get(name);
        if (v != NULL) {
            buf_puts(out, v);
            if (!in_dquote && strpbrk(v, "*?[") != NULL)
                *meta = 1; /* les jokers venant d'une variable comptent */
        }
        *pp = end + 1;
        return 0;
    }

    if (isalpha((unsigned char)*p) || *p == '_') { /* $NOM */
        const char *q = p;
        const char *v;
        size_t nlen;
        char name[256];

        while (name_char(*q))
            q++;
        nlen = (size_t)(q - p);
        if (nlen >= sizeof name)
            nlen = sizeof name - 1;
        memcpy(name, p, nlen);
        name[nlen] = '\0';
        v = var_get(name);
        if (v != NULL) {
            buf_puts(out, v);
            if (!in_dquote && strpbrk(v, "*?[") != NULL)
                *meta = 1;
        }
        *pp = q;
        return 0;
    }

    /* Paramètres positionnels non gérés ($1, $@, $*) : chaîne vide. */
    if (isdigit((unsigned char)*p) || *p == '@' || *p == '*') {
        *pp = p + 1;
        return 0;
    }

    /* '$' littéral : fin de mot ou caractère suivant non spécial. */
    buf_putc(out, '$');
    *pp = p;
    return 0;
}

void expand_free(char **fields, int count)
{
    int i;

    if (fields == NULL)
        return;
    for (i = 0; i < count; i++)
        free(fields[i]);
    free(fields);
}

char **expand_word(const char *raw, unsigned flags, int *out_count,
                   int *out_meta)
{
    Buf b = { NULL, 0, 0 };
    const char *p = raw;
    int meta = 0;
    int quoted = 0;
    char **arr;

    if (out_meta != NULL)
        *out_meta = 0;

    /* Expansion du tilde, uniquement en tête de mot non protégé. */
    if (p[0] == '~') {
        const char *rest = p + 1;
        const char *slash = rest;

        while (*slash != '\0' && *slash != '/')
            slash++;
        if (*rest == '\0' || *rest == '/') {
            const char *home = var_get("HOME");

            if (home != NULL && *home != '\0') {
                buf_puts(&b, home);
                quoted = 1; /* l'expansion produit toujours un champ */
            } else {
                buf_putc(&b, '~'); /* HOME absent : '~' littéral */
            }
            p = rest;
        } else {
            size_t ulen = (size_t)(slash - rest);

            if (ulen < 128) { /* « ~utilisateur » */
                char user[128];
                struct passwd *pw;

                memcpy(user, rest, ulen);
                user[ulen] = '\0';
                pw = getpwnam(user);
                if (pw != NULL && pw->pw_dir != NULL) {
                    buf_puts(&b, pw->pw_dir);
                    quoted = 1;
                    p = slash;
                }
                /* utilisateur inconnu : '~' reste littéral */
            }
        }
    }

    while (*p != '\0') {
        char c = *p;

        if (c == '\'') { /* '...' : strictement littéral */
            quoted = 1;
            p++;
            while (*p != '\0' && *p != '\'')
                buf_putc(&b, *p++);
            if (*p != '\0')
                p++;
            continue;
        }

        if (c == '"') { /* "..." : variables expansées, \" \\ \$ protégés */
            quoted = 1;
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0' &&
                    strchr("\"\\$", p[1]) != NULL) {
                    p++;
                    buf_putc(&b, *p++);
                    continue;
                }
                if (*p == '$') {
                    if (expand_dollar(&p, &b, 1, &meta) < 0) {
                        free(b.s);
                        return NULL;
                    }
                    continue;
                }
                buf_putc(&b, *p++);
            }
            if (*p != '\0')
                p++;
            continue;
        }

        if (c == '\\') { /* \x -> x */
            p++;
            if (*p != '\0')
                buf_putc(&b, *p++);
            continue;
        }

        if (c == '$') {
            if (expand_dollar(&p, &b, 0, &meta) < 0) {
                free(b.s);
                return NULL;
            }
            continue;
        }

        if (strchr("*?[", c) != NULL)
            meta = 1; /* métacaractère de joker non protégé */

        buf_putc(&b, *p++);
    }

    /*
     * Règle des champs : un mot devenu entièrement vide et ne comportant
     * aucune protection disparaît de la ligne de commandes
     * (ex. « echo $INEXISTANTE » -> 0 argument).
     */
    if (b.len == 0 && !quoted) {
        arr = xmalloc(sizeof(char *));
        arr[0] = NULL;
        *out_count = 0;
        return arr;
    }

    /* Jokers : remplacés par la liste triée des correspondances. */
    if (!(flags & EXP_NOGLOB) && meta) {
        glob_t g;

        memset(&g, 0, sizeof g);
        if (glob(b.s, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            size_t i;

            arr = xmalloc(sizeof(char *) * (g.gl_pathc + 1));
            for (i = 0; i < g.gl_pathc; i++)
                arr[i] = xstrdup(g.gl_pathv[i]);
            arr[g.gl_pathc] = NULL;
            *out_count = (int)g.gl_pathc;
            globfree(&g);
            free(b.s);
            if (out_meta != NULL)
                *out_meta = 1;
            return arr;
        }
        globfree(&g);
        /* aucune correspondance : le mot reste littéral (comportement Bash) */
    }

    arr = xmalloc(sizeof(char *) * 2);
    arr[0] = (b.s != NULL) ? b.s : xstrdup("");
    arr[1] = NULL;
    *out_count = 1;
    if (out_meta != NULL)
        *out_meta = meta;
    return arr;
}

char *expand_string(const char *raw, int *out_meta)
{
    int count = 0;
    int meta = 0;
    char **f = expand_word(raw, EXP_NOGLOB, &count, &meta);
    char *s;

    if (f == NULL)
        return NULL;
    s = (count > 0) ? f[0] : xstrdup("");
    if (count > 1) { /* ne devrait pas arriver avec EXP_NOGLOB */
        int i;

        for (i = 1; i < count; i++)
            free(f[i]);
    }
    free(f);
    if (out_meta != NULL)
        *out_meta = meta;
    return s;
}

int expand_is_assignment(const char *raw)
{
    size_t i = 0;

    if (!(isalpha((unsigned char)raw[0]) || raw[0] == '_'))
        return 0;
    while (name_char(raw[i]))
        i++;
    /* aucun guillemet possible avant '=' car seuls des caractères de nom
     * ont été consommés */
    return raw[i] == '=';
}
