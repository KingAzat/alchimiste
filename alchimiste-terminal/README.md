# alchimiste-terminal — émulateur de terminal en C

Petit émulateur de terminal : il lance un shell (par défaut `/bin/sh` ou
`alchimiste-shell` s'il est à côté) dans un **pseudo-terminal (pty)** et fait
le relais entre ton clavier et ce shell, et entre le shell et ton écran.

Ce n'est **pas** un shell : il n'interprète aucune commande. Il se contente
d'afficher ce que le shell renvoie et d'envoyer ce que tu tapes.

## Compilation

```bash
make
```

## Utilisation

```bash
# Lance le shell par défaut du système
./alchimiste-terminal

# Lance ton alchimiste-shell à l'intérieur
./alchimiste-terminal ../alchimiste-shell/alchimiste-shell

# Lance bash
./alchimiste-terminal /bin/bash
```

À l'intérieur, tu tapes normalement. `exit` ou `Ctrl+D` quitte le shell
interne, et l'émulateur se ferme.

## Ce qu'il gère

- Entrée en mode "raw" (caractère par caractère, pas besoin d'appuyer sur Entrée pour envoyer au shell)
- Redimensionnement de la fenêtre (`SIGWINCH`)
- `Ctrl+C`, `Ctrl+Z`, `Ctrl+D` transmis au shell
- Codes couleurs ANSI de base (le shell les émet, l'émulateur les affiche tels quels)

## Fichiers

| Fichier | Rôle |
|---------|------|
| `main.c` | Boucle `select()` : clavier ↔ pty |
| `terminal.c/.h` | Ouverture du pty, mode raw du terminal, taille fenêtre |
| `relay.c/.h` | Lecture/écriture non bloquante sur les descripteurs |
| `Makefile` | Compilation avec `-Wall -Wextra -Wpedantic` |
