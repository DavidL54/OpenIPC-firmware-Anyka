#!/usr/bin/env python3

import argparse
import fcntl
import os
import select
import sys
import termios
import time
import tty


BOOT_START = b"autoboot:  1"
PASSWORD_GATE = b"wait password timeout"
PASSWORD_ACCEPTED = b"Password is correct"
UBOOT_PROMPT = b"=> "
SHELL_PROMPTS = (b"/ # ", b"~ # ", b"]# ")
EXIT_KEY = b"\x1d"  # Ctrl-]
INTERRUPT_KEY = b"\x03"  # Ctrl-C
INTERRUPT_DELAY = 0.95


def configure_serial(fd: int) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)


def write_all(fd: int, data: bytes) -> None:
    view = memoryview(data)
    while view:
        try:
            written = os.write(fd, view)
            view = view[written:]
        except BlockingIOError:
            select.select([], [fd], [], 0.1)


def send_password(fd: int, password: str, char_delay: float, enter_delay: float) -> None:
    encoded_password = password.encode("ascii")
    write_all(fd, encoded_password[:1])
    time.sleep(enter_delay)
    for byte in encoded_password:
        write_all(fd, bytes((byte,)))
        time.sleep(char_delay)
    time.sleep(enter_delay)
    write_all(fd, b"\r")


def append_received(received: bytearray, chunk: bytes) -> None:
    received.extend(chunk)
    if len(received) > 1024:
        del received[:-1024]


def wait_for_shell(fd: int, timeout: float, root_password: str) -> bool:
    print("Recherche du shell root Anyka...", file=sys.stderr)
    deadline = time.monotonic() + timeout
    next_probe = 0.0
    received = bytearray()

    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_probe:
            write_all(fd, b"\r")
            next_probe = now + 1.0

        readable, _, _ = select.select([fd], [], [], 0.1)
        if not readable:
            continue

        chunk = os.read(fd, 4096)
        if not chunk:
            continue

        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        append_received(received, chunk)
        if any(received.endswith(prompt) for prompt in SHELL_PROMPTS):
            return True
        if b"login:" in received:
            write_all(fd, b"root\r")
            received.clear()
        elif b"Password:" in received:
            write_all(fd, root_password.encode("ascii") + b"\r")
            received.clear()

    return False


def run_shell_command(fd: int, command: str, timeout: float) -> bool:
    write_all(fd, command.encode("ascii") + b"\r")
    deadline = time.monotonic() + timeout
    received = bytearray()

    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], 0.1)
        if not readable:
            continue

        chunk = os.read(fd, 4096)
        if not chunk:
            continue

        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        append_received(received, chunk)
        if any(received.endswith(prompt) for prompt in SHELL_PROMPTS):
            return True

    return False


def enter_uboot(
    fd: int,
    password: str,
    timeout: float,
    password_delay: float,
    char_delay: float,
    enter_delay: float,
) -> bool:
    print("Attente du premier instant de lecture Ctrl-C U-Boot...", file=sys.stderr)
    deadline = time.monotonic() + timeout
    received = bytearray()
    password_sent = False
    password_confirmed = False
    interrupt_sent = False
    password_send_at = 0.0

    while time.monotonic() < deadline:
        if interrupt_sent and not password_sent and time.monotonic() >= password_send_at:
            print("\nSaisie du mot de passe U-Boot, puis Entree...", file=sys.stderr)
            send_password(fd, password, char_delay, enter_delay)
            password_sent = True

        readable, _, _ = select.select([fd], [], [], 0.001)
        if not readable:
            continue

        chunk = os.read(fd, 1 if not interrupt_sent else 4096)
        if not chunk:
            continue

        append_received(received, chunk)

        if BOOT_START in received and not interrupt_sent:
            print("\nLecture U-Boot active, envoi de la touche d'arret...", file=sys.stderr)
            time.sleep(INTERRUPT_DELAY)
            write_all(fd, INTERRUPT_KEY)
            interrupt_sent = True
            password_send_at = time.monotonic() + password_delay

        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()

        if PASSWORD_GATE in received and not password_sent:
            print("\nPorte U-Boot explicite detectee, envoi du mot de passe...", file=sys.stderr)
            send_password(fd, password, char_delay, enter_delay)
            password_sent = True

        if PASSWORD_ACCEPTED in received:
            password_confirmed = True

        if password_confirmed and UBOOT_PROMPT in received:
            print("\nMot de passe valide et prompt U-Boot obtenu.", file=sys.stderr)
            return True

    if password_sent and not password_confirmed:
        print("\nLe mot de passe n'a pas ete accepte.", file=sys.stderr)
    else:
        print("\nPorte ou prompt U-Boot non detecte avant expiration.", file=sys.stderr)
    return False


def interactive_console(fd: int) -> None:
    if not sys.stdin.isatty():
        return

    print("Console U-Boot active. Ctrl-] pour quitter.", file=sys.stderr)
    stdin_fd = sys.stdin.fileno()
    old_stdin = termios.tcgetattr(stdin_fd)
    try:
        tty.setraw(stdin_fd)
        while True:
            readable, _, _ = select.select([fd, stdin_fd], [], [])
            if fd in readable:
                data = os.read(fd, 4096)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
            if stdin_fd in readable:
                data = os.read(stdin_fd, 1024)
                if not data or EXIT_KEY in data:
                    return
                write_all(fd, data)
    finally:
        termios.tcsetattr(stdin_fd, termios.TCSADRAIN, old_stdin)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Valide le mot de passe du U-Boot protege d'une GNCC GP5 AK3918AV130."
    )
    parser.add_argument("port", nargs="?", default="/dev/ttyUSB0")
    parser.add_argument(
        "--password",
        default=os.environ.get("AK3918_UBOOT_PASSWORD", "gvqrs*&"),
        help="mot de passe U-Boot (AK3918_UBOOT_PASSWORD ou valeur extraite par defaut)",
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument(
        "--reboot",
        action="store_true",
        help="envoie reboot depuis un shell root deja ouvert",
    )
    action.add_argument(
        "--shell-command",
        help="execute une commande dans le shell root puis quitte",
    )
    parser.add_argument(
        "--root-password",
        default=os.environ.get("AK3918_ROOT_PASSWORD", "openipc"),
        help="mot de passe Linux utilise pour acceder au shell (openipc par defaut)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=45.0,
        help="attente maximale du U-Boot en secondes (45 par defaut)",
    )
    parser.add_argument(
        "--shell-timeout",
        type=float,
        default=20.0,
        help="attente maximale du shell avec --reboot (20 par defaut)",
    )
    parser.add_argument(
        "--password-delay",
        type=float,
        default=2.0,
        help="delai entre Ctrl-C et le mot de passe en secondes (2 par defaut)",
    )
    parser.add_argument(
        "--char-delay",
        type=float,
        default=0.05,
        help="delai entre les caracteres du mot de passe (0.05 par defaut)",
    )
    parser.add_argument(
        "--enter-delay",
        type=float,
        default=0.2,
        help="pause avant l'envoi separe de la touche Entree (0.2 par defaut)",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="quitte des que le prompt U-Boot est valide",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 0.05 <= args.password_delay <= 5.0:
        print("--password-delay doit etre compris entre 0.05 et 5.0", file=sys.stderr)
        return 2
    if not 0.0 <= args.char_delay <= 0.5:
        print("--char-delay doit etre compris entre 0 et 0.5", file=sys.stderr)
        return 2
    if not 0.05 <= args.enter_delay <= 2.0:
        print("--enter-delay doit etre compris entre 0.05 et 2.0", file=sys.stderr)
        return 2

    try:
        password = args.password.encode("ascii")
    except UnicodeEncodeError:
        print("Le mot de passe doit etre ASCII.", file=sys.stderr)
        return 2
    if not password or b"\r" in password or b"\n" in password:
        print("Mot de passe vide ou invalide.", file=sys.stderr)
        return 2

    try:
        fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as error:
        print(f"Impossible d'ouvrir {args.port}: {error}", file=sys.stderr)
        return 1

    try:
        fcntl.ioctl(fd, termios.TIOCEXCL)
        configure_serial(fd)
        if args.shell_command is not None:
            try:
                args.shell_command.encode("ascii")
            except UnicodeEncodeError:
                print("--shell-command doit etre ASCII.", file=sys.stderr)
                return 2
            if "\r" in args.shell_command or "\n" in args.shell_command:
                print("--shell-command ne doit contenir qu'une ligne.", file=sys.stderr)
                return 2
            if not wait_for_shell(fd, args.shell_timeout, args.root_password):
                print("\nShell root non detecte.", file=sys.stderr)
                return 3
            if not run_shell_command(fd, args.shell_command, args.shell_timeout):
                print("\nCommande terminee sans retour au prompt.", file=sys.stderr)
                return 5
            return 0
        if args.reboot:
            if not wait_for_shell(fd, args.shell_timeout, args.root_password):
                print("\nShell root non detecte.", file=sys.stderr)
                return 3
            print("Envoi de reboot -f...", file=sys.stderr)
            write_all(fd, b"reboot -f\r")
        else:
            print("Redemarrez ou rallumez maintenant la camera.", file=sys.stderr)

        if not enter_uboot(
            fd,
            args.password,
            args.timeout,
            args.password_delay,
            args.char_delay,
            args.enter_delay,
        ):
            return 4
        if not args.validate_only:
            interactive_console(fd)
        return 0
    except OSError as error:
        print(f"Erreur serie: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nInterrompu.", file=sys.stderr)
        return 130
    finally:
        os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())