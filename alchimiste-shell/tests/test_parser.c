/*
 * test_parser — tests unitaires du lexer et du parser d'alchimiste-shell.
 *
 * Compilation (faite par « make test ») :
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L \
 *       -o tests/test_parser tests/test_parser.c \
 *       build/lexer.o build/parser.o build/variables.o build/util.o
 *
 * Ce fichier fournit lui-même la définition de g_shell (shell.o n'est pas
 * lié : le test ne contient pas de boucle principale).
 */
#include "lexer.h"
#include "parser.h"
#include "variables.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

Shell g_shell; /* utilisé par l'expansion pour $? (tests phase 3) */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            g_pass++;                                                      \
            printf("PASS : %s\n", name);                                   \
        } else {                                                           \
            g_fail++;                                                      \
            printf("FAIL : %s  (%s:%d)\n", name, __FILE__, __LINE__);      \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------------ */
/* Aides                                                                */
/* ------------------------------------------------------------------ */

/* Lexer sans message d'erreur sur stderr pour les cas d'échec attendus. */
static Token *lex_quiet(const char *s, size_t *n)
{
    /* le lexer imprime ses erreurs : on les accepte dans la sortie test */
    return lexer_tokenize(s, n);
}

static CommandLine *parse_line(const char *s, int *err)
{
    size_t n = 0;
    Token *toks = lex_quiet(s, &n);
    CommandLine *cl;

    if (toks == NULL) {
        *err = 1;
        return NULL;
    }
    cl = parser_parse(toks, err);
    lexer_free_tokens(toks, n);
    return cl;
}

static void free_cl(CommandLine *cl)
{
    parser_free(cl);
}

/* ------------------------------------------------------------------ */
/* Tests du lexer                                                       */
/* ------------------------------------------------------------------ */

static void test_lexer_basics(void)
{
    size_t n = 0;
    Token *t;

    t = lex_quiet("", &n);
    CHECK(t != NULL && n == 1 && t[0].type == TOK_EOF, "lexer : ligne vide -> seul EOF");
    lexer_free_tokens(t, n);

    t = lex_quiet("   \t  ", &n);
    CHECK(t != NULL && n == 1 && t[0].type == TOK_EOF, "lexer : ligne blanche -> seul EOF");
    lexer_free_tokens(t, n);

    t = lex_quiet("ls", &n);
    CHECK(t != NULL && t[0].type == TOK_WORD && strcmp(t[0].text, "ls") == 0
              && t[1].type == TOK_EOF,
          "lexer : mot seul");
    lexer_free_tokens(t, n);

    t = lex_quiet("ls    -la     /tmp", &n);
    CHECK(t != NULL && n == 4 && t[0].type == TOK_WORD && t[1].type == TOK_WORD
              && t[2].type == TOK_WORD && t[3].type == TOK_EOF
              && strcmp(t[1].text, "-la") == 0,
          "lexer : espaces multiples");
    lexer_free_tokens(t, n);

    t = lex_quiet("echo \"a b\" c", &n);
    CHECK(t != NULL && strcmp(t[1].text, "\"a b\"") == 0
              && strcmp(t[2].text, "c") == 0,
          "lexer : guillemets doubles (mot unique, brut)");
    lexer_free_tokens(t, n);

    t = lex_quiet("echo 'x  y'", &n);
    CHECK(t != NULL && strcmp(t[1].text, "'x  y'") == 0,
          "lexer : guillemets simples");
    lexer_free_tokens(t, n);

    t = lex_quiet("echo a\\ b", &n);
    CHECK(t != NULL && strcmp(t[1].text, "a\\ b") == 0,
          "lexer : antislash protégeant une espace");
    lexer_free_tokens(t, n);

    t = lex_quiet("cat \"mon fichier.txt\"", &n);
    CHECK(t != NULL && strcmp(t[1].text, "\"mon fichier.txt\"") == 0,
          "lexer : argument contenant une espace");
    lexer_free_tokens(t, n);
}

static void test_lexer_operators(void)
{
    size_t n = 0;
    Token *t;

    t = lex_quiet("a;b|c&&d||e&f>g>>h<i", &n);
    CHECK(t != NULL
              && t[0].type == TOK_WORD && t[1].type == TOK_SEMI
              && t[2].type == TOK_WORD && t[3].type == TOK_PIPE
              && t[4].type == TOK_WORD && t[5].type == TOK_AND
              && t[6].type == TOK_WORD && t[7].type == TOK_OR
              && t[8].type == TOK_WORD && t[9].type == TOK_AMP
              && t[10].type == TOK_WORD && t[11].type == TOK_GT
              && t[12].type == TOK_WORD && t[13].type == TOK_APPEND
              && t[14].type == TOK_WORD && t[15].type == TOK_LT
              && t[16].type == TOK_WORD && t[17].type == TOK_EOF,
          "lexer : tous les opérateurs ; | && || & > >> <");
    lexer_free_tokens(t, n);

    t = lex_quiet("ls 2> err.txt", &n);
    CHECK(t != NULL && t[1].type == TOK_IO_NUMBER && strcmp(t[1].text, "2") == 0
              && t[2].type == TOK_GT && t[3].type == TOK_WORD,
          "lexer : nombre de descripteur 2>");
    lexer_free_tokens(t, n);

    t = lex_quiet("ls 2>&1", &n);
    CHECK(t != NULL && t[1].type == TOK_IO_NUMBER && t[2].type == TOK_GTAMP
              && strcmp(t[3].text, "1") == 0,
          "lexer : 2>&1");
    lexer_free_tokens(t, n);

    t = lex_quiet("cmd >&2", &n);
    CHECK(t != NULL && t[1].type == TOK_GTAMP && strcmp(t[2].text, "2") == 0,
          "lexer : >&2 (sortie vers erreurs)");
    lexer_free_tokens(t, n);

    t = lex_quiet("cmd &> f", &n);
    CHECK(t != NULL && t[1].type == TOK_RALL && t[2].type == TOK_WORD,
          "lexer : &> fichier");
    lexer_free_tokens(t, n);

    t = lex_quiet("cmd &>> f", &n);
    CHECK(t != NULL && t[1].type == TOK_RALL_APPEND && t[2].type == TOK_WORD,
          "lexer : &>> fichier");
    lexer_free_tokens(t, n);

    /* « 2 » suivi d'une espace puis > : mot ordinaire, pas un fd */
    t = lex_quiet("echo 2 > x", &n);
    CHECK(t != NULL && t[1].type == TOK_WORD && strcmp(t[1].text, "2") == 0
              && t[2].type == TOK_GT,
          "lexer : espace entre 2 et > -> mot ordinaire");
    lexer_free_tokens(t, n);

    /* « 2>x » collé : nombre de descripteur */
    t = lex_quiet("echo 2>x", &n);
    CHECK(t != NULL && t[1].type == TOK_IO_NUMBER && t[2].type == TOK_GT,
          "lexer : 2>x collé -> nombre de descripteur");
    lexer_free_tokens(t, n);

    /* mot contenant des chiffres puis > : pas un fd */
    t = lex_quiet("echo x2>y", &n);
    CHECK(t != NULL && t[1].type == TOK_WORD && strcmp(t[1].text, "x2") == 0
              && t[2].type == TOK_GT,
          "lexer : x2>y -> mot x2 puis redirection");
    lexer_free_tokens(t, n);
}

static void test_lexer_errors(void)
{
    size_t n = 0;
    Token *t;

    t = lex_quiet("echo \"abc", &n);
    CHECK(t == NULL, "lexer : guillemet double non fermé -> erreur");
    lexer_free_tokens(t, n);

    t = lex_quiet("echo 'abc", &n);
    CHECK(t == NULL, "lexer : guillemet simple non fermé -> erreur");
    lexer_free_tokens(t, n);

    t = lex_quiet("echo abc\\", &n);
    CHECK(t == NULL, "lexer : antislash en fin de ligne -> erreur");
    lexer_free_tokens(t, n);
}

static void test_lexer_comments(void)
{
    size_t n = 0;
    Token *t;

    t = lex_quiet("echo a # commentaire ignoré", &n);
    CHECK(t != NULL && n == 3 && t[1].type == TOK_WORD
              && strcmp(t[1].text, "a") == 0,
          "lexer : commentaire # en fin de ligne");
    lexer_free_tokens(t, n);

    t = lex_quiet("# commentaire seul", &n);
    CHECK(t != NULL && n == 1 && t[0].type == TOK_EOF,
          "lexer : ligne entièrement commentée");
    lexer_free_tokens(t, n);

    t = lex_quiet("echo a#b", &n);
    CHECK(t != NULL && n == 3 && strcmp(t[1].text, "a#b") == 0,
          "lexer : # à l'intérieur d'un mot");
    lexer_free_tokens(t, n);
}

/* ------------------------------------------------------------------ */
/* Tests du parser                                                      */
/* ------------------------------------------------------------------ */

static void test_parser_structure(void)
{
    int err = 0;
    CommandLine *cl;

    cl = parse_line("ls -la /tmp", &err);
    CHECK(cl != NULL && err == 0 && cl->head->pl->n == 1
              && cl->head->pl->cmds[0]->argc == 3,
          "parser : commande simple avec arguments");
    free_cl(cl);

    cl = parse_line("a | b | c", &err);
    CHECK(cl != NULL && cl->head->pl->n == 3, "parser : pipeline de 3 commandes");
    free_cl(cl);

    cl = parse_line("a ; b", &err);
    CHECK(cl != NULL && cl->head->sep == SEP_SEQ && cl->head->next != NULL,
          "parser : séquence ;");
    free_cl(cl);

    cl = parse_line("a && b || c", &err);
    CHECK(cl != NULL && cl->head->sep == SEP_AND
              && cl->head->next->sep == SEP_OR
              && cl->head->next->next != NULL,
          "parser : chaîne && puis ||");
    free_cl(cl);

    cl = parse_line("sleep 10 &", &err);
    CHECK(cl != NULL && cl->head->pl->background == 1,
          "parser : arrière-plan &");
    free_cl(cl);

    cl = parse_line("mkdir test ; cd test ; pwd", &err);
    CHECK(cl != NULL && cl->head->sep == SEP_SEQ
              && cl->head->next->sep == SEP_SEQ,
          "parser : trois commandes séparées par ;");
    free_cl(cl);
}

static void test_parser_redirections(void)
{
    int err = 0;
    CommandLine *cl;
    const Redir *r;

    cl = parse_line("echo hi > f.txt", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_OUT && r->fd == 1
              && strcmp(r->filename, "f.txt") == 0,
          "parser : redirection >");
    free_cl(cl);

    cl = parse_line("echo hi >> f.txt", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_APPEND,
          "parser : redirection >>");
    free_cl(cl);

    cl = parse_line("cat < f.txt", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_IN && r->fd == 0,
          "parser : redirection d'entrée <");
    free_cl(cl);

    cl = parse_line("ls err 2> e.txt 2>> e2.txt", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_OUT && r->fd == 2
              && r->next != NULL && r->next->kind == RDIR_APPEND
              && r->next->fd == 2,
          "parser : 2> et 2>>");
    free_cl(cl);

    cl = parse_line("ls 2>&1 > f", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_DUP && r->fd == 2
              && r->fd_to == 1 && r->next != NULL && r->next->kind == RDIR_OUT,
          "parser : 2>&1 puis > (ordre conservé)");
    free_cl(cl);

    cl = parse_line("cmd >&2", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_DUP && r->fd == 1
              && r->fd_to == 2,
          "parser : >&2");
    free_cl(cl);

    cl = parse_line("cmd &> f", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_ALL && r->append == 0,
          "parser : &>");
    free_cl(cl);

    cl = parse_line("cmd &>> f", &err);
    r = (cl != NULL) ? cl->head->pl->cmds[0]->redirs : NULL;
    CHECK(cl != NULL && r != NULL && r->kind == RDIR_ALL && r->append == 1,
          "parser : &>>");
    free_cl(cl);

    /* redirection AVANT la commande : autorisée comme dans Bash */
    cl = parse_line("> f.txt echo hi", &err);
    CHECK(cl != NULL && cl->head->pl->cmds[0]->argc == 2,
          "parser : redirection avant les mots");
    free_cl(cl);
}

static void test_parser_errors(void)
{
    int err = 0;
    CommandLine *cl;

    cl = parse_line("| ls", &err);
    CHECK(cl == NULL && err == 1, "parser : | en début de ligne -> erreur");
    free_cl(cl);

    cl = parse_line("ls |", &err);
    CHECK(cl == NULL && err == 1, "parser : | en fin de ligne -> erreur");
    free_cl(cl);

    cl = parse_line("echo >", &err);
    CHECK(cl == NULL && err == 1, "parser : > sans fichier -> erreur");
    free_cl(cl);

    cl = parse_line("a ; ; b", &err);
    CHECK(cl == NULL && err == 1, "parser : ; ; -> erreur");
    free_cl(cl);

    cl = parse_line("&& a", &err);
    CHECK(cl == NULL && err == 1, "parser : && en début de ligne -> erreur");
    free_cl(cl);

    cl = parse_line("&", &err);
    CHECK(cl == NULL && err == 1, "parser : & seul -> erreur");
    free_cl(cl);

    cl = parse_line("a && || b", &err);
    CHECK(cl == NULL && err == 1, "parser : && suivi de || -> erreur");
    free_cl(cl);

    cl = parse_line("echo a | ; cat", &err);
    CHECK(cl == NULL && err == 1, "parser : ; après un tube -> erreur");
    free_cl(cl);

    err = 0;
    cl = parse_line("", &err);
    CHECK(cl == NULL && err == 0, "parser : ligne vide, sans erreur");
    free_cl(cl);

    err = 0;
    cl = parse_line("   ", &err);
    CHECK(cl == NULL && err == 0, "parser : ligne blanche, sans erreur");
    free_cl(cl);

    err = 0;
    cl = parse_line("# commentaire", &err);
    CHECK(cl == NULL && err == 0, "parser : commentaire seul, sans erreur");
    free_cl(cl);
}

/* ------------------------------------------------------------------ */
/* Tests de l'expansion (phase 3)                                      */
/* ------------------------------------------------------------------ */

static void test_expansion_vars(void)
{
    int n = 0;
    char **f;

    var_set("FOO", "bar");

    f = expand_word("$FOO", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "bar") == 0,
          "expansion : $FOO");
    expand_free(f, n);

    f = expand_word("${FOO}x", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "barx") == 0,
          "expansion : ${FOO} accolades");
    expand_free(f, n);

    f = expand_word("'$FOO'", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "$FOO") == 0,
          "expansion : guillemets simples = littéral");
    expand_free(f, n);

    f = expand_word("\"$FOO\"", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "bar") == 0,
          "expansion : guillemets doubles = expansion");
    expand_free(f, n);

    f = expand_word("x$FOO$FOO", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "xbarbar") == 0,
          "expansion : variables consécutives");
    expand_free(f, n);

    f = expand_word("$INEXISTANTE", 0, &n, NULL);
    CHECK(f != NULL && n == 0, "expansion : variable inconnue -> 0 champ");
    expand_free(f, n);

    f = expand_word("\"$INEXISTANTE\"", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && f[0][0] == '\0',
          "expansion : variable inconnue quotée -> 1 champ vide");
    expand_free(f, n);

    f = expand_word("avant$INEXISTANTE", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "avant") == 0,
          "expansion : concaténation avec variable vide");
    expand_free(f, n);

    g_shell.last_status = 5;
    f = expand_word("$?", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "5") == 0,
          "expansion : $?");
    expand_free(f, n);

    f = expand_word("a\\ b", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "a b") == 0,
          "expansion : antislash espace");
    expand_free(f, n);

    f = expand_word("\"Il a dit \\\"salut\\\"\"", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "Il a dit \"salut\"") == 0,
          "expansion : guillemets internes échappés");
    expand_free(f, n);
}

static void test_expansion_tilde_glob(void)
{
    int n = 0, meta = 0;
    char **f;
    char cwd[4096];
    const char *home;

    home = var_get("HOME");

    f = expand_word("~", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && home != NULL && strcmp(f[0], home) == 0,
          "expansion : tilde seul");
    expand_free(f, n);

    f = expand_word("~/Documents", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strncmp(f[0], home, strlen(home)) == 0
              && strcmp(f[0] + strlen(home), "/Documents") == 0,
          "expansion : tilde avec chemin");
    expand_free(f, n);

    f = expand_word("\"~\"", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "~") == 0,
          "expansion : tilde protégé = littéral");
    expand_free(f, n);

    f = expand_word("a~b", 0, &n, NULL);
    CHECK(f != NULL && n == 1 && strcmp(f[0], "a~b") == 0,
          "expansion : tilde hors tête de mot = littéral");
    expand_free(f, n);

    /* jokers : on travaille dans un répertoire temporaire isolé */
    if (getcwd(cwd, sizeof cwd) != NULL
        && chdir("/tmp") == 0
        && (mkdir("alchitest_glob", 0777) == 0 || errno == EEXIST)
        && chdir("alchitest_glob") == 0) {
        FILE *fa = fopen("a.c", "w");
        FILE *fb = fopen("b.c", "w");
        FILE *fz = fopen("z.txt", "w");

        if (fa != NULL)
            fclose(fa);
        if (fb != NULL)
            fclose(fb);
        if (fz != NULL)
            fclose(fz);

        f = expand_word("*.c", 0, &n, NULL);
        CHECK(f != NULL && n == 2 && strcmp(f[0], "a.c") == 0
                  && strcmp(f[1], "b.c") == 0,
              "expansion : joker * trié");
        expand_free(f, n);

        f = expand_word("*.zzzinexistant", 0, &n, NULL);
        CHECK(f != NULL && n == 1 && strcmp(f[0], "*.zzzinexistant") == 0,
              "expansion : joker sans correspondance = littéral");
        expand_free(f, n);

        f = expand_word("\"*\"", 0, &n, &meta);
        CHECK(f != NULL && n == 1 && strcmp(f[0], "*") == 0 && meta == 0,
              "expansion : joker protégé = littéral");
        expand_free(f, n);

        f = expand_word("?.c", 0, &n, NULL);
        CHECK(f != NULL && n == 2, "expansion : joker ?");
        expand_free(f, n);

        var_set("GLOBVAR", "*.c");
        f = expand_word("$GLOBVAR", 0, &n, NULL);
        CHECK(f != NULL && n == 2,
              "expansion : jokers issus d'une variable");
        expand_free(f, n);
        var_unset("GLOBVAR");

        chdir("..");
        rmdir("alchitest_glob");
        chdir(cwd);
    } else {
        CHECK(0, "expansion : préparation du répertoire de test jokers");
    }
}

static void test_expansion_assignments(void)
{
    CHECK(expand_is_assignment("FOO=bar"), "affectation : FOO=bar");
    CHECK(expand_is_assignment("X="), "affectation : X= (valeur vide)");
    CHECK(expand_is_assignment("_X2=y"), "affectation : _X2=y");
    CHECK(!expand_is_assignment("\"FOO\"=x"), "affectation : quotée = non");
    CHECK(!expand_is_assignment("2X=y"), "affectation : chiffre en tête = non");
    CHECK(!expand_is_assignment("FOO"), "affectation : sans = non");
    CHECK(expand_is_assignment("a=b=c"), "affectation : a=b=c (valeur b=c)");
}

static void test_variables_table(void)
{
    const char *v;

    var_set("TESTVAR", "locale");
    v = var_get("TESTVAR");
    CHECK(v != NULL && strcmp(v, "locale") == 0, "variables : var_set/var_get");
    CHECK(getenv("TESTVAR") == NULL,
          "variables : locale non exportée invisible dans environ");

    var_export("TESTVAR");
    v = getenv("TESTVAR");
    CHECK(v != NULL && strcmp(v, "locale") == 0,
          "variables : export rend visible dans environ");

    var_set("TESTVAR", "nouvelle");
    v = getenv("TESTVAR");
    CHECK(v != NULL && strcmp(v, "nouvelle") == 0,
          "variables : l'export est conservé aux modifications");

    var_unset("TESTVAR");
    CHECK(var_get("TESTVAR") == NULL && getenv("TESTVAR") == NULL,
          "variables : unset supprime partout");

    CHECK(var_valid_name("OK_NAME1"), "variables : nom valide");
    CHECK(!var_valid_name("1BAD"), "variables : nom invalide");
    CHECK(!var_valid_name("A-B"), "variables : nom invalide (tiret)");

    alias_set("ll", "ls -la");
    v = alias_get("ll");
    CHECK(v != NULL && strcmp(v, "ls -la") == 0, "alias : définition/lecture");
    alias_unset("ll");
    CHECK(alias_get("ll") == NULL, "alias : suppression");
}

/* ------------------------------------------------------------------ */
/* Point d'entrée                                                       */
/* ------------------------------------------------------------------ */

int main(void)
{
    memset(&g_shell, 0, sizeof g_shell);
    g_shell.prog_name = "test_parser";
    g_shell.running = 1;
    vars_init_from_environ();

    test_lexer_basics();
    test_lexer_operators();
    test_lexer_errors();
    test_lexer_comments();
    test_parser_structure();
    test_parser_redirections();
    test_parser_errors();
    test_expansion_vars();
    test_expansion_tilde_glob();
    test_expansion_assignments();
    test_variables_table();

    printf("\n%d tests réussis, %d échoués\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
