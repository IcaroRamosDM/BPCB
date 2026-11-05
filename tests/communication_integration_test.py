#!/usr/bin/env python3
"""Instala .bancada reais e exercita o executor UART em PTYs; nenhum hardware físico."""

import copy
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
import zipfile

cli, build, worker = map(pathlib.Path, sys.argv[1:])
checks = 0


def check(value):
    global checks
    checks += 1
    assert value


with tempfile.TemporaryDirectory(prefix="bpcb-comm-integracao-") as temp:
    root = pathlib.Path(temp)
    data = root / "dados"
    config = root / "config.json"

    def call(*args):
        return subprocess.run(
            [str(cli), *map(str, args)], capture_output=True, text=True, timeout=10
        )

    for bus in ("I2C", "SPI", "UART", "JTAG"):
        p = build / f"Comunicacao-{bus}.bancada"
        check(call("inspect", p).returncode == 0)
        check(call("install", data, p).returncode == 0)
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    selection = {
        "adapter": "serial",
        "bus": "uart",
        "device": port,
        "connections_checked": True,
        "timeout_ms": 500,
    }
    config.write_text(json.dumps(selection))

    def run(reply):
        failure = []

        def echo():
            try:
                received = b""
                deadline = time.monotonic() + 5
                while len(received) < 4:
                    ready, _, _ = select.select(
                        [master], [], [], max(0, deadline - time.monotonic())
                    )
                    if not ready:
                        raise AssertionError("Nenhum comando recebido pelo PTY")
                    received += os.read(master, 4 - len(received))
                check(received == bytes.fromhex("A55A00FF"))
                os.write(master, reply)
            except BaseException as exc:
                failure.append(exc)

        t = threading.Thread(target=echo)
        t.start()
        result = call("run", data, "communication-uart", "transferencia", config)
        t.join(6)
        check(not t.is_alive())
        check(not failure)
        return result, [json.loads(line) for line in result.stdout.splitlines()]

    r, events = run(bytes.fromhex("A55A00FF"))
    check(r.returncode == 0)
    end = events[-1]
    check(end["status"] == "ok")
    check(not (data / "results").exists())
    snapshot = next(e["data"] for e in events if e["type"] == "snapshot")
    check(
        snapshot["communication"]["device"] == port and snapshot["communication"]["bus"] == "uart"
    )
    check(snapshot["communication_definition"]["operations"][0]["expect"] == "A5 5A 00 FF")
    measure = next(e for e in events if e["type"] == "measurement")
    check(measure["verdict"] == "pass" and measure["received_bytes"] == "A5 5A 00 FF")
    check(end["record"]["snapshot"]["communication"]["device"] == port)
    check(end["record"]["snapshot"]["communication_actual_hz"] == 0)
    record = root / "resultado.json"
    record.write_text(json.dumps(end["record"]))
    pdf = root / "resultado.pdf"
    check(call("report-record", record, pdf).returncode == 0 and pdf.stat().st_size > 1000)
    import shutil

    if shutil.which("pdftotext"):
        text = subprocess.check_output(["pdftotext", str(pdf), "-"], text=True)
        check(
            "Comunicação utilizada" in text
            and "serial" in text
            and port in text
            and "A5 5A 00 FF" in text
            and "Clock efetivo" not in text
        )
    if os.environ.get("BPCB_TEST_EVIDENCE"):
        destination = pathlib.Path(os.environ["BPCB_TEST_EVIDENCE"])
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(record, destination / "resultado-uart-PTY.json")
        shutil.copy2(pdf, destination / "resultado-uart-PTY.pdf")
    r, events = run(bytes.fromhex("A55A00FE"))
    check(r.returncode != 0 and events[-1]["status"] == "error")
    check(next(e for e in events if e["type"] == "measurement")["verdict"] == "fail")
    check(not (data / "results").exists())
    config.write_text(json.dumps({**selection, "connections_checked": False}))
    r = call("run", data, "communication-uart", "transferencia", config)
    check(r.returncode != 0 and "conex" in r.stdout.lower())
    check(not select.select([master], [], [], 0.02)[0])
    config.write_text(json.dumps(selection))
    r = call("run", data, "communication-spi", "transferencia", config)
    check(r.returncode != 0 and "exige spi" in r.stdout.lower())
    check(not select.select([master], [], [], 0.02)[0])
    # O executor também rejeita chamadas diretas incompatíveis antes de abrir/transmitir.
    with zipfile.ZipFile(build / "Comunicacao-UART.bancada") as archive:
        manifest = json.loads(archive.read("manifest.json"))
    request = {
        "api": 2,
        "mode": "hardware",
        "communication": selection,
        "communication_definition": copy.deepcopy(manifest["communication"]),
    }
    request["communication_definition"]["max_transfer"] = 2
    r = subprocess.run(
        [str(worker)], input=json.dumps(request) + "\n", capture_output=True, text=True, timeout=5
    )
    check(r.returncode != 0 and "limite" in r.stdout.lower())
    check(not select.select([master], [], [], 0.02)[0])
    # Declaração exige versão compatível e rejeita erros de digitação na importação.
    fixture = root / "fixture"
    fixture.mkdir()
    bad = copy.deepcopy(manifest)
    bad["min_app"] = "0.11.0"
    (fixture / "manifest.json").write_text(json.dumps(bad))
    r = call("pack", fixture, root / "bad.bancada")
    check(r.returncode != 0)
    bad["min_app"] = "0.12.0"
    bad["communication"]["operation"] = []
    (fixture / "manifest.json").write_text(json.dumps(bad))
    check(call("pack", fixture, root / "bad2.bancada").returncode != 0)
    # Pacote CH347/JTAG não pode anunciar compatibilidade com o BPCB anterior.
    with zipfile.ZipFile(build / "Comunicacao-JTAG.bancada") as archive:
        contents = {name: archive.read(name) for name in archive.namelist()}
        m = json.loads(contents["manifest.json"])
        m["min_app"] = "0.12.0"
        contents["manifest.json"] = json.dumps(m).encode()
    incompatible = root / "jtag-incompativel.bancada"
    with zipfile.ZipFile(incompatible, "w") as archive:
        for name, content in contents.items():
            archive.writestr(name, content)
    r = call("inspect", incompatible)
    check(r.returncode != 0 and "0.13.0" in r.stderr + r.stdout)
    # O worker da versão anterior tem validação estrita e não pode receber
    # campos novos específicos do CH347 ao executar um adaptador já conhecido.
    legacy_dist = (
        pathlib.Path(__file__).resolve().parents[4] / "distribuicao/BPCB-0.12.0-linux-x86_64.tar.gz"
    )
    legacy_package = os.environ.get("BPCB_LEGACY_COMM_PACKAGE")
    if legacy_package or legacy_dist.is_file():
        legacy = root / "uart-0.12.bancada"
        if legacy_package:
            legacy.write_bytes(pathlib.Path(legacy_package).read_bytes())
        else:
            import tarfile

            with tarfile.open(legacy_dist) as archive:
                member = next(
                    m for m in archive.getmembers() if m.name.endswith("/Comunicacao-UART.bancada")
                )
                legacy.write_bytes(archive.extractfile(member).read())
        check(call("install", data, legacy).returncode == 0)
        config.write_text(json.dumps(selection))
        r, events = run(bytes.fromhex("A55A00FF"))
        check(r.returncode == 0 and events[-1]["status"] == "ok")
        check(next(e for e in events if e["type"] == "measurement")["verdict"] == "pass")
        check("ch347_mode" not in events[-1]["record"]["snapshot"]["communication"])
        check(not (data / "results").exists())
    os.close(master)
    os.close(slave)
print(
    (
        f"Integração: {checks} verificações; instalação dos quatro pacotes, UART PTY, "
        "seleção propagada, comparação pass/fail, limites e incompatibilidades antes de I/O"
        "; histórico continua opcional, incluindo worker legado 0.12 quando disponível."
    )
)
