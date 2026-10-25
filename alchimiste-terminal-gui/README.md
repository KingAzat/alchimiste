# alchimiste-terminal-gui — émulateur de terminal graphique

Fenêtre X11 qui lance un shell (defaut : `/bin/sh`) dans un pseudo-terminal
(pty) et affiche sa sortie. C'est la version graphique de
`alchimiste-terminal` (le relais texte) : une vraie fenêtre indépendante,
comme GNOME Terminal — mais minimaliste.

## Compilation

```bash
make
```

## Utilisation

```bash
./alchimiste-terminal-gui              # sh par defaut
./alchimiste-terminal-gui /bin/bash    # bash
./alchimiste-terminal-gui ../alchimiste-shell/alchimiste-shell  # votre shell
```

Fermez la fenêtre avec `exit` dans le shell, ou le bouton X de la fenêtre.

## Ce qui fonctionne

- fenetre X11 propre (fond noir, texte blanc, curseur inverse)
- toucher : touches, backspace (efface), tab, Entree, Ctrl+C, Ctrl+D, Ctrl+Z
  (geres par le shell enfant comme dans un vrai terminal)
- `TERM=xterm` fourni au shell enfant
- redimensionnement de la fenetre → contourne la taille au shell
- barre de titre : `alchimiste-terminal — <commande lancée>`
- caractere special recreated : conversions UTF-8 → Latin-1 (accents manuels
  quelques-uns sont affichés, characters non represente → `?`)

## Limites actuelles (volontaires — « version ecole »)

- pas de COULEURS : les sequences ANSI de couleur sontIGNOREES (texte blanc)
- pas de commande de defilement (scrollback) visible au-delà de la haute
- pas de copier-coller de sélection a la sourie
- redimensionner la fenetre EFFACE l'ecran (la grille est recree)
- taille limitee : sequences ANSI les plus courantes ignorees, ne gerant pas
  position precise du curseur
- police : variable a compilation (9x15 par défaut, repli `fixed` / `10x20`)

C'est une demo pedagogique X11. Un vrai terminal genre GNOME Terminal utilise
la biblitheque VTE : un cathode ray tube complet, des milliers de lignes.
