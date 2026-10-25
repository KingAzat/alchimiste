/*
 * main — point d'entrée d'alchimiste-shell.
 *
 *   alchimiste-shell              mode interactif (ou lecture de stdin)
 *   alchimiste-shell script.sh    mode script
 */
#include "shell.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    int status;

    shell_init(argc, argv);

    if (argc > 2) {
        fprintf(stderr, "usage : alchimiste-shell [script]\n");
        status = 2;
    } else if (argc == 2) {
        status = shell_run_script(argv[1]);
    } else {
        status = shell_run_interactive();
    }

    shell_cleanup();
    return status;
}
