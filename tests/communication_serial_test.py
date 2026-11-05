#!/usr/bin/env python3
"""Real termios I/O against a pseudo-terminal; never opens physical hardware."""

import json
import os
import pathlib
import pty
import select
import subprocess
import sys
import tempfile
import threading
import time

cli = sys.argv[1]
checks = 0


def check(value):
    global checks
    checks += 1
    assert value


with tempfile.TemporaryDirectory(prefix="bpcb-serial-") as temp:
    root = pathlib.Path(temp)
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    config = {
        "adapter": "serial",
        "bus": "uart",
        "device": port,
        "timeout_ms": 500,
        "baud": 115200,
        "connections_checked": True,
    }
    c = root / "config.json"
    o = root / "operation.json"

    def call(operation, selection=None):
        c.write_text(json.dumps(config if selection is None else selection))
        o.write_text(json.dumps(operation))
        return subprocess.run(
            [cli, "comm-exchange", str(c), str(o)], capture_output=True, text=True, timeout=5
        )

    def device():
        received = b""
        while len(received) < 4:
            received += os.read(master, 4 - len(received))
        check(received == bytes.fromhex("01234567"))
        os.write(master, bytes.fromhex("89AB"))
        time.sleep(0.02)
        os.write(master, bytes.fromhex("CDEF"))

    worker = threading.Thread(target=device)
    worker.start()
    r = call({"write": "01 23 45 67", "read": 4, "expect": "89 AB CD EF"})
    worker.join(2)
    check(not worker.is_alive())
    check(r.returncode == 0)
    data = json.loads(r.stdout)
    check(data["received"] == "89 AB CD EF" and data["matched"])
    check(data["configuration"]["device"] == port)
    begin = time.monotonic()
    r = call({"read": 2})
    check(r.returncode != 0)
    check(time.monotonic() - begin < 2)
    check("limite" in r.stderr.lower() or "incompleta" in r.stderr.lower())
    r = call({"read": 1}, {**config, "connections_checked": False})
    check(r.returncode != 0 and "Confirme" in r.stderr)
    c.write_text(json.dumps(config))
    r = subprocess.run([cli, "comm-probe", str(c)], capture_output=True, text=True, timeout=5)
    check(r.returncode == 0 and "sem abrir" in json.loads(r.stdout)["scope"])
    # Permission/type validation does not allow a normal file to be used as a serial port.
    ordinary = root / "regular"
    ordinary.write_bytes(b"UNCHANGED")
    r = call({"write": "00", "read": 0}, {**config, "device": str(ordinary)})
    check(r.returncode != 0 and ordinary.read_bytes() == b"UNCHANGED")
    # Escrita sem leitura também conserva baud/handle até esvaziar a fila.
    r = call({"write": "DE AD BE EF", "read": 0})
    check(r.returncode == 0)
    check(os.read(master, 4) == bytes.fromhex("DEADBEEF"))
    r = call({"write": "00", "read": 0, "expect": ""})
    check(r.returncode != 0)
    # Cancellation while blocked on an incomplete reply.
    c.write_text(json.dumps({**config, "timeout_ms": 5000}))
    o.write_text(json.dumps({"read": 1}))
    process = subprocess.Popen(
        [cli, "comm-exchange", str(c), str(o)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    time.sleep(0.15)
    process.terminate()
    begin = time.monotonic()
    process.communicate(timeout=2)
    check(time.monotonic() - begin < 1)
    # The public exchange API routes protocol blocks through the laboratory executor.
    for reply, expected in [(b"AT\r\nWIFI CONNECTED\r\nOK\r\n", True), (b"ERROR\r\nOK\r\n", False)]:
        errors = []

        def at_device():
            try:
                command = b""
                deadline = time.monotonic() + 3
                while b"\r\n" not in command:
                    if time.monotonic() > deadline:
                        raise TimeoutError(command)
                    if select.select([master], [], [], 0.05)[0]:
                        command += os.read(master, 128)
                check(command == b"AT\r\n")
                os.write(master, reply[:2])
                time.sleep(0.005)
                os.write(master, reply[2:])
            except BaseException as error:
                errors.append(repr(error))

        thread = threading.Thread(target=at_device)
        thread.start()
        r = call(
            dict(
                kind="uart_protocol",
                protocol="at",
                write="41 54",
                delimiter="crlf",
                timeout_ms=400,
                max_bytes=128,
            )
        )
        thread.join(4)

        if errors or r.returncode != (0 if expected else 1):
            print(r.returncode, r.stdout, r.stderr, errors, flush=True)
        check(not thread.is_alive() and not errors)
        check((r.returncode == 0) == expected)
        data = json.loads(r.stdout)
        check(data["matched"] == expected and data["transport"]["configuration"]["device"] == port)
    os.close(master)
    os.close(slave)
print(
    (
        f"UART: {checks} verificações de termios real com PTY, fragmentação, timeout, "
        "cancelamento, consentimento e preservação de arquivo."
    )
)
