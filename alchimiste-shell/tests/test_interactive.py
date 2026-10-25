#!/usr/bin/env python3
"""
test_interactive.py — tests interactifs d'alchimiste-shell.

Ces tests exigent un vrai terminal : ils lancent le shell dans un
pseudo-terminal (pty) en tant que chef de session AVEC terminal de
contrôle (setsid + TIOCSCTTY), comme le ferait un véritable émulateur
de terminal. Sans cela, tcsetpgrp() échouerait et Ctrl+C / Ctrl+Z
n'atteindraient jamais les commandes.

Vérifications :
  - bannière et invite ;
  - Ctrl+C interrompt la commande de premier plan mais pas le shell ;
  - $? vaut 130 après une commande tuée par SIGINT ;
  - Ctrl+Z suspend la commande -> visible par « jobs » ;
  - « bg » relance la tâche suspendue en arrière-plan ;
  - « fg » la ramène au premier plan et attend sa fin ;
  - l'opérateur « & » affiche [N] PID ;
  - une tâche en arrière-plan terminée est notifiée avant l'invite.

Utilisation :  python3 tests/test_interactive.py
"""

import fcntl
import os
import pty
import select
import subprocess
import sys
import termios
import time

BIN = os.environ.get("SHELL_BIN", "./alchimiste-shell")


def _make_session_leader_with_tty():
    """Exécuté dans le fils avant exec : nouvelle session + pty comme
    terminal de contrôle (reproduit un vrai environnement interactif)."""
    os.setsid()
    fcntl.ioctl(0, termios.TIOCSCTTY, 0)


class Session:
    def __init__(self):
        self.master, slave = pty.openpty()
        self.p = subprocess.Popen(
            [BIN],
            stdin=slave,
            stdout=slave,
            stderr=slave,
            close_fds=True,
            preexec_fn=_make_session_leader_with_tty,
        )
        os.close(slave)
        self.buf = b""

    def send(self, data, delay=0.15):
        time.sleep(delay)
        if isinstance(data, str):
            data = data.encode()
        os.write(self.master, data)

    def clear(self):
        """Vide le tampon : le prochain expect ne verra que la sortie
        produite après cet instant (évite les faux positifs)."""
        self.read_available(0.1)
        self.buf = b""

    def read_available(self, duration=0.2):
        end = time.time() + duration
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self.master, 4096)
                except OSError:
                    break
                self.buf += chunk

    def expect(self, name, pattern, timeout=8.0):
        """Attend que pattern apparaisse dans la sortie du pty."""
        deadline = time.time() + timeout
        pat = pattern.encode()
        while time.time() < deadline:
            if pat in self.buf:
                print(f"PASS : {name}")
                return True
            r, _, _ = select.select([self.master], [], [], 0.1)
            if r:
                try:
                    chunk = os.read(self.master, 4096)
                except OSError:
                    break
                self.buf += chunk
        print(f"FAIL : {name}")
        print("----- extrait de la sortie (600 derniers octets) -----")
        print(self.buf.decode(errors="replace")[-600:])
        return False

    def close(self):
        try:
            self.p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait()
        try:
            os.close(self.master)
        except OSError:
            pass


def main():
    results = []
    s = Session()

    results.append(s.expect("bannière affichée", "ALCHIMISTE SHELL"))
    results.append(s.expect("invite utilisateur@hôte", "$ "))

    s.clear()
    s.send("echo vivant\n")
    results.append(s.expect("commande simple", "vivant"))

    # --- Ctrl+C sur une commande de premier plan -------------------
    s.clear()
    s.send("sleep 30\n", delay=0.3)
    time.sleep(0.8)
    s.read_available()
    s.send(b"\x03")  # Ctrl+C -> SIGINT au groupe de premier plan
    results.append(
        s.expect("Ctrl+C interrompt sleep (le shell survit)", "$ ", timeout=5)
    )
    # $? doit valoir 130 (128 + SIGINT) DANS LA COMMANDE SUIVANTE
    s.clear()
    s.send("echo code=$?\n")
    results.append(s.expect("code de retour 130 après SIGINT", "code=130"))

    s.clear()
    s.send("echo apres-clic\n")
    results.append(s.expect("shell fonctionnel après Ctrl+C", "apres-clic"))

    # --- Ctrl+Z : suspension --------------------------------------
    s.clear()
    s.send("sleep 5\n", delay=0.3)
    time.sleep(0.8)
    s.read_available()
    s.send(b"\x1a")  # Ctrl+Z -> SIGTSTP au groupe de premier plan
    results.append(
        s.expect("Ctrl+Z suspend la commande (retour à l'invite)", "$ ",
                 timeout=5)
    )
    s.clear()
    s.send("jobs\n")
    results.append(s.expect("jobs affiche la tâche arrêtée", "Arrêtée"))

    # --- bg : reprise en arrière-plan ------------------------------
    s.clear()
    s.send("bg\n")
    results.append(s.expect("bg relance en arrière-plan", "sleep 5 &"))
    s.clear()
    s.send("jobs\n")
    results.append(s.expect("jobs affiche la tâche en cours", "En cours"))

    # --- fg : retour au premier plan -------------------------------
    s.clear()
    s.send("fg\n")
    results.append(s.expect("fg réaffiche la commande", "sleep 5"))
    results.append(
        s.expect("fg attend la fin de la tâche", "$ ", timeout=10)
    )

    # --- arrière-plan explicite avec & ------------------------------
    s.clear()
    s.send("sleep 2 &\n")
    results.append(s.expect("notification [N] PID", "["))
    s.clear()
    s.send("jobs\n")
    results.append(s.expect("jobs suit l'arrière-plan", "En cours"))

    # --- notification de fin avant l'invite -------------------------
    results.append(
        s.expect("notification « Terminée » avant l'invite", "Terminée",
                 timeout=8)
    )

    # --- sortie ------------------------------------------------------
    s.clear()
    s.send("exit\n")
    results.append(s.expect("message d'au revoir", "Au revoir"))
    s.close()
    results.append(s.p.returncode == 0)  # sortie propre

    passed = sum(1 for r in results if r)
    print(f"\n{passed}/{len(results)} tests interactifs réussis")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
