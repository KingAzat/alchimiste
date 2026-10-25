/* main — point d'entrée de l'émulateur de terminal GRAPHIQUE.
 *
 *   ./alchimiste-terminal-gui              lance /bin/sh
 *   ./alchimiste-terminal-gui /bin/bash     lance un autre shell
 *   ./alchimiste-terminal-gui ../alchimiste-shell/alchimiste-shell
 *
 * Fenêtre X11, avec :
 *   - rendu UNICODE (police ISO10646) : bannières UTF-8 (╔ ═ ╗ ║ ╚ ╝)
 *     affichées correctement, accents inclus ;
 *   - double tampon (pixmap hors écran) : zéro scintillement ;
 *   - XFlush après chaque rendu : frappe affichée immédiatement ;
 *   - scrollback de 2000 lignes, molette de la souris pour remonter ;
 *   - curseur dessiné en bordure de boîte ;
 *   - ?, message de fin puis fermeture sur frappe.
 *
 * Limites volontaires (voir README) : pas de couleurs, pas de
 * copier-coller. */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

/* ----------------------------- constantes ------------------------------- */

#define RING_CAP  2000  /* lignes d'historique (scrollback)             */
#define COL_CAP    320  /* largeur max d'une ligne en mémoire           */
#define WIN_W_DEFAULT 800
#define WIN_H_DEFAULT 500

/* ------------------------- variables X11 (avant usage) ------------------- */

static Display *g_disp;
static Window   g_win;
static Pixmap   g_pix;            /* double tampon hors écran */
static GC       gc_text, gc_bg, gc_cur;
static XFontStruct *g_font;
static int      g_unicode_font;   /* 1 = ISO10646 (2 octets), 0 = Latin-1  */
static int      g_char_w, g_line_h, font_ascent;
static int      g_rows, g_cols;   /* taille visible */
static int      g_win_w, g_win_h; /* taille réelle de la fenêtre */

/* Handler d'erreur X : journalise sur stderr sans tuer le programme
 * (un BadMatch ne doit pas faire disparaître l'émulateur). */
static int x_error_trap(Display *d, XErrorEvent *ev)
{
    char msg[256];
    XGetErrorText(d, ev->error_code, msg, sizeof msg);
    fprintf(stderr, "avertissement X11 : %s\n", msg);
    return 0;
}

/* -------------------------- anneau de lignes ---------------------------- */

typedef struct {
    unsigned short cp[COL_CAP];  /* codepoints Unicode BMP (>Latin1 avec ISO) */
    int len;                      /* longueur utile                            */
} Row;

static Row ring[RING_CAP];
static int ring_cur;       /* index de la ligne en cours d'écriture           */
static int ring_count;     /* lignes valides (sature à RING_CAP)              */
static int cur_col;        /* colonne du curseur dans la ligne courante       */
static int scroll_off;     /* lignes remontées (0 = vue en bas)               */

/* Met une ligne à zéro. */
static void ring_zero_row(int slot)
{
    memset(ring[slot].cp, 0, sizeof ring[slot].cp);
    ring[slot].len = 0;
}

/* Index dans ring[] de la ligne d'ancienneté `age` (0 = ligne courante). */
static int ring_slot_for_age(int age)
{
    return (ring_cur - age + RING_CAP) % RING_CAP;
}

/* Nombre maximum de lignes remontables par la molette. */
static int max_scroll(void)
{
    int ms = ring_count - g_rows;
    return ms > 0 ? ms : 0;
}

/* Convertit un codepoint Unicode en celle affichable dans notre police. */
static unsigned short rune_to_display(unsigned int cp)
{
    if (cp < 0x20 || cp == 0x7F)          /* non imprimable ASCII  */
        return ' ';
    if (cp >= 0x80 && cp <= 0x9F)          /* C1 : pas de glyphe    */
        return '?';
    if (!g_unicode_font && cp > 0xFF)      /* police Latin-1 seule  */
        return '?';
    return (unsigned short)cp;
}

/* Écrit un codepoint dans la ligne courante. */
static void ring_putc(unsigned int cp)
{
    Row *r = &ring[ring_cur];
    if (cur_col >= COL_CAP)
        return;                            /* ligne trop longue : ignorée */
    r->cp[cur_col] = rune_to_display(cp);
    cur_col++;
    if (cur_col > r->len)
        r->len = cur_col;
}

/* ------------------------ mini-décodeur UTF-8 --------------------------- */

/* Pousse un octet dans le décodeur (les blocs UTF-8 peuvent être coupés
 * entre deux read(), l'état est conservé). Renvoie 1 lorsqu'un caractère
 * complet est produit dans *out. */
static int utf8_feed(unsigned char b, unsigned short *out)
{
    static unsigned char buf[4];
    static int need = 0, total = 0;

    if (need == 0) {
        if (b < 0x80) {                    /* ASCII direct */
            *out = b;
            return 1;
        }
        if ((b & 0xE0) == 0xC0)      { buf[0] = b; total = need = 2; }
        else if ((b & 0xF0) == 0xE0) { buf[0] = b; total = need = 3; }
        else if ((b & 0xF8) == 0xF0) { buf[0] = b; total = need = 4; }
        else { *out = '?'; return 1; }
        return 0;
    }
    buf[total - need] = b;
    need--;
    if (need == 0) {
        unsigned int cp = 0;
        switch (total) {
        case 2:
            cp = ((buf[0] & 0x1F) << 6) | (buf[1] & 0x3F);
            break;
        case 3:
            cp = ((buf[0] & 0x0F) << 12) | ((buf[1] & 0x3F) << 6) | (buf[2] & 0x3F);
            break;
        case 4:
            cp = ((buf[0] & 0x07) << 18) | ((buf[1] & 0x3F) << 12)
               | ((buf[2] & 0x3F) << 6) | (buf[3] & 0x3F);
            break;
        }
        if (cp > 0xFFFE)                   /* hors BMP / non défini */
            cp = '?';
        *out = (unsigned short)cp;
        return 1;
    }
    return 0;
}

/* ------------------------ codes de contrôle ANSI ------------------------- */

typedef enum {
    ST_NORMAL,  /* texte normal                                     */
    ST_ESC,     /* ESC reçu, on attend l'octet suivant              */
    ST_CSI,     /* séquence ESC[ ... final ignorée                  */
    ST_OSC      /* séquence ESC] ... BEL ignorée (titre, couleurs)   */
} ParseState;

static ParseState g_state = ST_NORMAL;

/* Traite un octet de SORTIE du shell. */
static void feed_byte(unsigned char b)
{
    switch (g_state) {
    case ST_NORMAL:
        if (b == '\n') {           /* retour à la ligne : nouvelle ligne */
            ring_cur = (ring_cur + 1) % RING_CAP;
            if (ring_count < RING_CAP)
                ring_count++;
            ring_zero_row(ring_cur);
            cur_col = 0;
            return;
        }
        if (b == '\r') { cur_col = 0;   return; }
        if (b == '\b') { if (cur_col > 0) cur_col--; return; }
        if (b == '\t') {
            cur_col = ((cur_col / 8) + 1) * 8;
            if (cur_col > COL_CAP)
                cur_col = COL_CAP;
            return;
        }
        if (b == 0x1B) { g_state = ST_ESC;   return; }
        if (b == 0x0C || b == 0x0B)   /* form feed / vertical tab : saute */
            return;
        if (b < 0x20 || b == 0x7F)    /* autres octets de contrôle : rien */
            return;
        {
            unsigned short cp;
            if (utf8_feed(b, &cp))
                ring_putc(cp);
        }
        return;

    case ST_ESC:
        if (b == '[')
            g_state = ST_CSI;
        else if (b == ']')
            g_state = ST_OSC;
        else if (b == '(' || b == ')')
            /* désignation de jeu : le prochain octet finalise via ST_CSI */
            g_state = ST_CSI;
        else
            g_state = ST_NORMAL;   /* séquence 1 octet : consommée */
        return;

    case ST_CSI:
        if (b >= 0x40 && b <= 0x7E)
            g_state = ST_NORMAL;
        return;

    case ST_OSC:
        if (b == 0x07)
            g_state = ST_NORMAL;
        return;
    }
}

/* ----------------------------- fenêtre ----------------------------------- */

/* Crée (ou recrée) le pixmap du double tampon. */
static void pix_alloc(void)
{
    static int old_rows = -1, old_cols = -1;
    if (g_pix) {
        if (old_rows == g_rows && old_cols == g_cols)
            return;
        XFreePixmap(g_disp, g_pix);
    }
    old_rows = g_rows;
    old_cols = g_cols;
    g_pix = XCreatePixmap(g_disp, g_win,
                          (unsigned int)g_cols * g_char_w,
                          (unsigned int)g_rows * g_line_h,
                          (unsigned int)DefaultDepth(g_disp,
                                                     DefaultScreen(g_disp)));
}

static void terminal_redraw(void)
{
    XSetForeground(g_disp, gc_bg, BlackPixel(g_disp, DefaultScreen(g_disp)));
    /* Peint d'abord TOUTE la fenêtre en noir : les zones non couvertes
     * par le pixmap (fenêtre agrandie) ne gardent aucun reste. */
    XFillRectangle(g_disp, g_win, gc_bg, 0, 0,
                   (unsigned int)g_win_w, (unsigned int)g_win_h);
    XFillRectangle(g_disp, g_pix, gc_bg, 0, 0,
                   (unsigned int)g_cols * g_char_w,
                   (unsigned int)g_rows * g_line_h);

    XSetForeground(g_disp, gc_text, WhitePixel(g_disp, DefaultScreen(g_disp)));
    XSetFont(g_disp, gc_text, g_font->fid);

    /* Ancrage du contenu :
     *  - historique PETIT → lignes en HAUT (démarrage d'un terminal) ;
     *  - historique PLEIN → collé en bas, plus ancien en haut. */
    for (int r = 0; r < g_rows; r++) {
        int age;
        if (ring_count > g_rows)
            age = (g_rows - 1 - r) + scroll_off;
        else
            age = ring_count - 1 - r;
        if (age < 0 || age >= ring_count)
            continue;

        int slot = ring_slot_for_age(age);
        int len = ring[slot].len;
        if (len > g_cols)
            len = g_cols;
        if (len == 0)
            continue;

        /* conversion codepoints → XChar2b (2 octets) pour XDrawString16.
         * Une cellule à 0 = case jamais écrite (ex. sautée par un tab) :
         * on la rend comme un espace, sinon la police Unicode dessine
         * son glyphe « indéfini » (aspect de "J"). */
        XChar2b rowbuf[COL_CAP];
        for (int i = 0; i < len; i++) {
            unsigned short cp = ring[slot].cp[i];
            if (cp == 0)
                cp = ' ';
            rowbuf[i].byte1 = (unsigned char)(cp >> 8);
            rowbuf[i].byte2 = (unsigned char)(cp & 0xFF);
        }
        XDrawString16(g_disp, g_pix, gc_text,
                      0, r * g_line_h + font_ascent, rowbuf, len);
    }

    /* curseur (bordure blanche) : à la ligne courante, vue en bas */
    if (scroll_off == 0) {
        int cy = (ring_count > g_rows) ? g_rows - 1 : ring_count - 1;
        int x = (cur_col < g_cols ? cur_col : g_cols - 1) * g_char_w;
        int y = cy * g_line_h;
        XSetForeground(g_disp, gc_cur,
                       WhitePixel(g_disp, DefaultScreen(g_disp)));
        XDrawRectangle(g_disp, g_pix, gc_cur, x, y,
                       (unsigned int)g_char_w - 1,
                       (unsigned int)g_line_h - 1);
    }

    /* copie du tampon, bornée à la taille réelle de la fenêtre (sinon
     * BadMatch après un rétrécissement). */
    int cw = g_cols * g_char_w;
    int ch = g_rows * g_line_h;
    if (cw > g_win_w) cw = g_win_w;
    if (ch > g_win_h) ch = g_win_h;
    XCopyArea(g_disp, g_pix, g_win, gc_bg, 0, 0,
              (unsigned int)cw, (unsigned int)ch, 0, 0);
    XFlush(g_disp);
}

/* --------------------------- pseudo-terminal ----------------------------- */

static volatile sig_atomic_t g_child_exited = 0;

static void sigchld_handler(int sig)
{
    (void)sig;
    g_child_exited = 1;
}

static int pty_open(char *slave_name, size_t size)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        perror("posix_openpt");
        return -1;
    }
    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        perror("grantpt/unlockpt");
        close(master);
        return -1;
    }
    const char *name = ptsname(master);
    if (name == NULL) {
        perror("ptsname");
        close(master);
        return -1;
    }
    snprintf(slave_name, size, "%s", name);
    return master;
}

static pid_t child_spawn(int master, const char *shell,
                         int rows, int cols, const char *slave_name)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        close(master);
        if (setsid() < 0)
            _exit(1);
        int slave = open(slave_name, O_RDWR);
        if (slave < 0)
            _exit(1);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO)
            close(slave);

        ioctl(STDIN_FILENO, TIOCSCTTY, 1);

        struct winsize ws;
        ws.ws_row = (unsigned short)rows;
        ws.ws_col = (unsigned short)cols;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl(STDIN_FILENO, TIOCSWINSZ, &ws);

        setenv("TERM", "xterm", 1);
        /* L'émulateur désactive la bannière du sous-shell alchimiste
         * (lancement minimal : uniquement l'invite). Ne touche pas
         * bash ni sh, qui ignorent cette variable. */
        setenv("ALCH_BANNIERE", "0", 1);
        execlp(shell, shell, (char *)NULL);
        perror("exec");
        _exit(127);
    }
    return pid;
}

/* Encode un codepoint Latin-1 (keysym < 0x100) en UTF-8. */
static int utf8_encode(unsigned int cp, unsigned char *out)
{
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    out[0] = (unsigned char)(0xC0 | (cp >> 6));
    out[1] = (unsigned char)(0x80 | (cp & 0x3F));
    return 2;
}

/* Convertit une frappe clavier X11 en octets pour le shell.
 * Stratégie : partir du keysym (lettres, chiffres, accents —
 * XK_eacute = 0xE9 etc.) et encoder en UTF-8 de façon déterministe ;
 * XLookupString ne sert qu'aux touches spéciales (flèches, F1, pavé
 * numérique) qui ne font pas partie des plages simples. */
static int key_to_bytes(XKeyEvent *ev, unsigned char *out)
{
    KeySym ksym;
    char buf[8];
    int n;

    if (ev->state & ControlMask) {
        n = XLookupString(ev, buf, sizeof buf, &ksym, NULL);
        if (n > 0) {
            memcpy(out, buf, (size_t)n);
            return n;
        }
        /* Ctrl+A → 0x01, Ctrl+C → 0x03, ... (lettres uniquement). */
        if ((ksym >= XK_a && ksym <= XK_z) || (ksym >= XK_A && ksym <= XK_Z))
            return (int)(ksym & 0x1F);
        return 0;
    }

    /* Keysym selon Maj : index 0 (non Maj), 1 (Maj) dans la keymap. */
    int idx = (ev->state & ShiftMask) ? 1 : 0;
    ksym = XLookupKeysym(ev, idx);
    if (ksym == NoSymbol && idx == 1)
        ksym = XLookupKeysym(ev, 0);       /* touche à une seule valeur */

    /* Verr. Maj (Caps Lock) : inverse la casse des lettres. */
    if (ev->state & LockMask) {
        if (ksym >= XK_a && ksym <= XK_z)
            ksym = ksym - XK_a + XK_A;
        else if (ksym >= XK_A && ksym <= XK_Z)
            ksym = ksym - XK_A + XK_a;
    }

    switch (ksym) {
    case XK_Return:
    case XK_KP_Enter:
        out[0] = '\n';
        return 1;
    case XK_BackSpace:
        out[0] = 0x7F;
        return 1;
    case XK_Tab:
        out[0] = '\t';
        return 1;
    case XK_KP_Space:
        out[0] = ' ';
        return 1;
    default:
        break;
    }

    /* Printable keysym : 0x20..0xFF correspond à l'ISO Latin-1 des
     * toutes les lettres, chiffres et accents du clavier fr. */
    if (ksym >= 0x20 && ksym <= 0xFF)
        return utf8_encode((unsigned int)ksym, out);

    /* Prix de secours pour les touches qui ne tombent pas dans les
     * plages simples (flèches, F#, pavé numérique...) : XLookupString. */
    n = XLookupString(ev, buf, sizeof buf, &ksym, NULL);
    if (n > 0) {
        memcpy(out, buf, (size_t)n);
        return n;
    }
    return 0;
}

/* Message de fin, écrit dans l'anneau comme du texte. */
static void show_exit_message(const char *shell_name)
{
    char msg[COL_CAP];
    snprintf(msg, sizeof msg,
             "— processus terminé (%s) — une touche pour fermer la fenêtre —",
             shell_name);
    feed_byte('\n');
    for (unsigned char *p = (unsigned char *)msg; *p; p++)
        feed_byte(*p);
}

/* Vide ce qui reste dans le pty, montre le message final et passe en
 * phase « une touche pour fermer ». */
static void do_child_end(int master, const char *shell_name, int *waiting_exit)
{
    unsigned char tail[4096];
    for (;;) {
        ssize_t n = read(master, tail, sizeof tail);
        if (n <= 0)
            break;
        for (ssize_t k = 0; k < n; k++)
            feed_byte(tail[k]);
    }
    show_exit_message(shell_name);
    scroll_off = 0;
    terminal_redraw();
    *waiting_exit = 1;
}

/* --------------------------------- main ---------------------------------- */

int main(int argc, char *argv[])
{
    const char *shell = (argc > 1) ? argv[1] : "/bin/sh";
    const char *shell_name = strrchr(shell, '/');
    shell_name = shell_name ? shell_name + 1 : shell;

    /* nécessaire pour que XLookupString rende les accents en UTF-8 */
    setlocale(LC_ALL, "");
    XSetLocaleModifiers("");

    g_disp = XOpenDisplay(NULL);
    if (g_disp == NULL) {
        fprintf(stderr,
                "Erreur : X11 indisponible (session graphique ?).\n");
        return 1;
    }
    XSetErrorHandler(x_error_trap);
    int scr = DefaultScreen(g_disp);

    /* police Unicode en priorité : la bannitaire du shell (╔ ═ ╗ ║ ╚ ╝)
     * s'affiche alors correctement. */
    const char *fonts[] = {
        "-misc-fixed-medium-r-normal--20-200-75-75-c-100-iso10646-1",
        "-misc-fixed-medium-r-normal--15-140-75-75-c-90-iso10646-1",
        "9x15",
        "fixed",
    };
    g_unicode_font = -1;
    for (unsigned i = 0; i < sizeof fonts / sizeof fonts[0]; i++) {
        g_font = XLoadQueryFont(g_disp, fonts[i]);
        if (g_font) {
            /* iso10646 → Unicode total ; sinon Latin-1 uniquement */
            if (strstr(fonts[i], "iso10646") != NULL)
                g_unicode_font = 1;
            else if (g_unicode_font < 0)
                g_unicode_font = 0;
            break;
        }
    }
    if (g_font == NULL || g_unicode_font < 0) {
        fprintf(stderr, "Erreur : aucune police fixed.\n"
                        "Essayez : sudo apt install xfonts-base\n");
        XCloseDisplay(g_disp);
        return 1;
    }
    g_char_w = g_font->max_bounds.width;
    font_ascent = g_font->ascent;
    g_line_h = g_font->ascent + g_font->descent;

    g_rows = WIN_H_DEFAULT / g_line_h;
    g_cols = WIN_W_DEFAULT / g_char_w;

    g_win = XCreateSimpleWindow(g_disp, DefaultRootWindow(g_disp), 20, 20,
                                (unsigned int)(g_cols * g_char_w),
                                (unsigned int)(g_rows * g_line_h), 1,
                                WhitePixel(g_disp, scr), BlackPixel(g_disp, scr));
    g_win_w = g_cols * g_char_w;
    g_win_h = g_rows * g_line_h;

    XSizeHints hints;
    memset(&hints, 0, sizeof hints);
    hints.flags = PSize;
    XSetWMNormalHints(g_disp, g_win, &hints);

    char title[] = "Émulateur de terminal";

    /* WM_NAME est interprété en Latin-1 par certains gestionnaires
     * (le É UTF-8 devient illisible). La bonne propriété est
     * _NET_WM_NAME (type UTF8_STRING), écrite ici manuellement. */
    {
        Atom net_name = XInternAtom(g_disp, "_NET_WM_NAME", False);
        Atom utf8_str = XInternAtom(g_disp, "UTF8_STRING", False);
        XChangeProperty(g_disp, g_win, net_name, utf8_str, 8,
                        PropModeReplace,
                        (const unsigned char *)title,
                        (int)strlen(title));
        /* repli : WM_NAME en ASCII pur, interprétable partout */
        XStoreName(g_disp, g_win, "Emulateur de terminal");
    }

    Atom wm_protocols = XInternAtom(g_disp, "WM_PROTOCOLS", False);
    Atom wm_delete    = XInternAtom(g_disp, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_disp, g_win, &wm_delete, 1);

    /* ButtonPressMask : molette pour le scrollback */
    XSelectInput(g_disp, g_win,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask);

    gc_text = XCreateGC(g_disp, g_win, 0, NULL);
    gc_bg   = XCreateGC(g_disp, g_win, 0, NULL);
    gc_cur  = XCreateGC(g_disp, g_win, 0, NULL);

    /* anneau : une ligne vue par défaut (l'invite du sous-shell) */
    ring_zero_row(ring_cur);
    ring_count = 1;

    /* Aucune bannière de l'émulateur : au lancement, la seule ligne
     * affichée est l'invite du sous-shell. */

    pix_alloc();
    XMapWindow(g_disp, g_win);
    XFlush(g_disp);

    char slave_name[256];
    int master = pty_open(slave_name, sizeof slave_name);
    if (master < 0) {
        XCloseDisplay(g_disp);
        return 1;
    }
    /* non bloquant : après la mort de l'enfant, read() renvoie EAGAIN
     * au lieu de bloquer la boucle. */
    fcntl(master, F_SETFL, O_NONBLOCK);

    pid_t child = child_spawn(master, shell, g_rows, g_cols, slave_name);
    if (child < 0) {
        close(master);
        XCloseDisplay(g_disp);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;          /* select est coupé par SIGCHLD */
    sigaction(SIGCHLD, &sa, NULL);

    int waiting_exit = 0;     /* phase « une touche pour fermer » */
    int running = 1;
    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int xfd = XConnectionNumber(g_disp);
        FD_SET(xfd, &readfds);
        int maxfd = xfd + 1;
        if (!waiting_exit) {
            FD_SET(master, &readfds);
            if (master > xfd)
                maxfd = master + 1;
        }

        int ready = select(maxfd, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                if (g_child_exited && !waiting_exit)
                    do_child_end(master, shell_name, &waiting_exit);
                continue;
            }
            perror("select");
            break;
        }

        if (!waiting_exit && FD_ISSET(master, &readfds)) {
            unsigned char buf[4096];
            ssize_t n = read(master, buf, sizeof buf);
            if (n > 0) {
                for (ssize_t k = 0; k < n; k++)
                    feed_byte(buf[k]);
                scroll_off = 0;    /* suit la sortie en direct */
                terminal_redraw();
            } else if (n == 0) {   /* pty fermé : fin de session */
                g_child_exited = 1;
                do_child_end(master, shell_name, &waiting_exit);
            }
            /* EAGAIN : rien de neuf, on continue. */
        }

        if (FD_ISSET(xfd, &readfds)) {
            while (XPending(g_disp) > 0) {
                XEvent ev;
                XNextEvent(g_disp, &ev);

                switch (ev.type) {
                case Expose:
                    if (ev.xexpose.count == 0)
                        terminal_redraw();
                    break;

                case ConfigureNotify:
                    g_win_w = ev.xconfigure.width;
                    g_win_h = ev.xconfigure.height;
                    {
                        int nrows = g_win_h / g_line_h;
                        int ncols = g_win_w / g_char_w;
                        if (nrows < 1 || ncols < 1)
                            continue;
                        g_rows = nrows;
                        g_cols = ncols < COL_CAP ? ncols : COL_CAP;
                        pix_alloc();
                        scroll_off = 0;
                        terminal_redraw();
                        struct winsize ws;
                        ws.ws_row = (unsigned short)g_rows;
                        ws.ws_col = (unsigned short)g_cols;
                        ws.ws_xpixel = 0;
                        ws.ws_ypixel = 0;
                        ioctl(master, TIOCSWINSZ, &ws);
                    }
                    break;

                case ButtonPress:
                    if (ev.xbutton.button == 4) {  /* molette haut */
                        int ms = max_scroll();
                        if (scroll_off < ms) {
                            scroll_off += 3;
                            if (scroll_off > ms)
                                scroll_off = ms;
                            terminal_redraw();
                        }
                    } else if (ev.xbutton.button == 5) { /* molette bas */
                        if (scroll_off > 0) {
                            scroll_off -= 3;
                            if (scroll_off < 0)
                                scroll_off = 0;
                            terminal_redraw();
                        }
                    }
                    break;

                case KeyPress:
                    if (waiting_exit) {
                        running = 0;   /* toute frappe ferme la fenêtre */
                        break;
                    }
                    {
                        unsigned char bytes[8];
                        int nb = key_to_bytes(&ev.xkey, bytes);
                        if (nb > 0) {
                            if (scroll_off != 0) {
                                scroll_off = 0;   /* frappe → retour bas */
                                terminal_redraw();
                            }
                            if (write(master, bytes, (size_t)nb) < 0)
                                running = 0;
                        }
                    }
                    break;

                case ClientMessage:
                    if (ev.xclient.message_type == wm_protocols &&
                        (Atom)ev.xclient.data.l[0] == wm_delete)
                        running = 0;
                    break;
                }
            }
        }
    }

    /* fin : s'assurer que l'enfant est parti puis nettoyer. */
    if (!g_child_exited) {
        kill(child, SIGHUP);
    }
    {
        int status;
        waitpid(child, &status, 0);
    }

    close(master);
    XFreePixmap(g_disp, g_pix);
    XDestroyWindow(g_disp, g_win);
    XFreeGC(g_disp, gc_text);
    XFreeGC(g_disp, gc_bg);
    XFreeGC(g_disp, gc_cur);
    XFreeFont(g_disp, g_font);
    XCloseDisplay(g_disp);
    return 0;
}
