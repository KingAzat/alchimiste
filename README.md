# Alchimiste — mini-shell Linux + émulateur de terminal (en C)

Projet de Sciences des Systèmes d'Exploitation (SCE) : un **interpréteur de
commandes** fonctionnel écrit en C11/POSIX, accompagné de deux **émulateurs
de terminal** — un en mode relais texte et un en vraie fenêtre graphique X11.

Les trois programmes sont **indépendants** : chacun se compile et s'exécute
seul, sans dépendre des autres. L'émulateur lance le shell qu'on lui donne en
argument (par défaut `/bin/sh`), exactement comme GNOME Terminal lance Bash.

## Les trois projets

| Dossier | Rôle | Type |
|---------|------|------|
| `alchimiste-shell/` | Le **shell** : lit et interprète les commandes (lexer, parser, tubes, redirections, jobs, signaux) | Programme autonome |
| `alchimiste-terminal-gui/` | **Émulateur de terminal graphique** : vraie fenêtre X11, lance un shell dans un pseudo-terminal (pty) | Programme autonome |
| `alchimiste-terminal/` | **Émulateur de terminal texte** : relais clavier/pty dans le terminal courant (à la tmux) | Programme autonome |

## Compilation

Chaque projet a son propre `Makefile` (GCC, C11, `-Wall -Wextra -Wpedantic`,
sans avertissement) :

```bash
cd alchimiste-shell         && make        # produit ./alchimiste-shell
cd alchimiste-terminal-gui  && make        # produit ./alchimiste-terminal-gui
cd alchimiste-terminal      && make        # produit ./alchimiste-terminal
```

Le terminal graphique nécessite X11 (`pkg-config x11`, présent par défaut sous
la plupart des Linux de bureau).

## Utilisation

```bash
# Le shell, en interactif
./alchimiste-shell/alchimiste-shell

# Le shell, en mode script
./alchimiste-shell/alchimiste-shell alchimiste-shell/examples/exemple.sh

# La fenêtre graphique, avec le shell du projet dedans
./alchimiste-terminal-gui/alchimiste-terminal-gui \
    ./alchimiste-shell/alchimiste-shell

# La fenêtre graphique, avec bash
./alchimiste-terminal-gui/alchimiste-terminal-gui /bin/bash

# Le relais texte (dans le terminal courant)
./alchimiste-terminal/alchimiste-terminal /bin/sh
```

## Tests

```bash
cd alchimiste-shell && make test    # 219 tests (unitaires + fonctionnels + pty)
```

Toutes les suites passent sous AddressSanitizer / UndefinedBehaviorSanitizer /
LeakSanitizer sans erreur ni fuite. Détails dans `alchimiste-shell/README.md`
et `alchimiste-shell/docs/`.

## Documentation

- `alchimiste-shell/README.md` — présentation complète du shell ;
- `alchimiste-shell/docs/rapport.md` — rapport universitaire ;
- `alchimiste-shell/docs/architecture.md` — architecture des modules ;
- `alchimiste-shell/docs/soutenance.md` — préparation de soutenance ;
- `alchimiste-terminal-gui/README.md` — fonctionnalités et limites du GUI ;
- `alchimiste-terminal/README.md` — fonctionnement du relais texte.

## Auteur

Azaria Tiendrebeogo (KingAzat).
