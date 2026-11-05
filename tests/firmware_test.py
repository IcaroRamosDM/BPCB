#!/usr/bin/env python3
"""Ferramentas falsas + PTY; nenhuma chamada de flash/USB real."""

from pathlib import Path
import copy
import hashlib
import json
import os
import pty
import signal
import subprocess
import sys
import tempfile
import time

cli = Path(sys.argv[1])
runner = Path(sys.argv[2])
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    assert value, detail


with tempfile.TemporaryDirectory(prefix="bpcb-firmware-qa-") as temporary:
    root = Path(temporary) / "Pastas com espaço e ação"
    root.mkdir()
    scripts = root / "scripts"
    (scripts / "target").mkdir(parents=True)
    (scripts / "target/stm32f1x.cfg").write_text("# fixture de dados, não executada\n")
    calls = root / "calls.jsonl"
    modefile = root / "mode"
    modefile.write_text("ok")
    pidfile = root / "child.pid"
    tool = root / "esptool ; $(exec nope)"
    tool.write_text("""#!/usr/bin/python3
import hashlib,json,os,pathlib,subprocess,sys,time
root=pathlib.Path(__file__).parent
mode=(root/'mode').read_text()
if sys.argv[1:]==['version']:
 print('esptool v4.9' if mode=='old' else 'esptool v5.3.1')
 sys.exit(0)
records=[]
for arg in sys.argv[1:]:
 path=pathlib.Path(arg)
 if path.is_file():
  assert 'bpcb-firmware-' in str(path) and path.name.startswith('image-')
  records.append(dict(name=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
with (root/'calls.jsonl').open('a') as out:out.write(json.dumps(dict(argv=sys.argv[1:],images=records))+'\\n')
if mode=='slow':time.sleep(10)
if mode=='fork':
 child=subprocess.Popen(['/usr/bin/python3','-c','import time; time.sleep(30)'])
 (root/'child.pid').write_text(str(child.pid))
if mode=='overflow':print('x'*70000)
if mode=='fail':print('falha simulada de verificação');sys.exit(9)
print('ferramenta simulada; imagens conferidas no snapshot')
""")
    tool.chmod(0o700)
    images = []
    for role, address, data in [
        ("bootloader", 0x1000, b"bootloader fixture"),
        ("partitions", 0x8000, b"partition fixture"),
        ("application", 0x10000, bytes(range(128))),
    ]:
        path = root / (role + " ; [exec false] $.bin")
        path.write_bytes(data)
        images.append(dict(role=role, file=str(path), format="bin", address=address))
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    manifest = dict(
        schema=1,
        name="Fixture de firmware",
        version="1.0",
        family="esp32",
        target="esp32",
        backend="esptool",
        images=images,
    )
    request = dict(
        manifest=manifest,
        executable=str(tool),
        action="program",
        timeout_seconds=2,
        trusted_tools=True,
        program_confirmed=False,
        communication=dict(
            adapter="serial", bus="uart", device=port, baud=115200, connections_checked=True
        ),
    )
    path = root / "request.json"

    def invoke(command, q, ok=True, executable=cli):
        path.write_text(json.dumps(q))
        r = subprocess.run(
            [str(executable), *([command] if executable == cli else []), str(path)],
            capture_output=True,
            text=True,
            timeout=8,
        )
        check((r.returncode == 0) == ok, (command, r.stdout[-500:], r.stderr[-1000:]))
        return json.loads(r.stdout) if r.stdout else None

    plan = invoke("firmware-plan", request)
    check(len(plan["sha256"]) == 64)
    check(len(plan["manifest"]["images"]) == 3)
    invoke("firmware-run", request, False)
    check(not calls.exists())
    ready = copy.deepcopy(request)
    ready.update(expected_sha256=plan["sha256"], program_confirmed=True)
    result = invoke("firmware-run", ready)
    check(result["ok"] and result["images"] == 3)
    actual = json.loads(calls.read_text().splitlines()[-1])
    check("write-flash" in actual["argv"])
    check("--force" not in actual["argv"] and "--erase-all" not in actual["argv"])
    check(actual["argv"][actual["argv"].index("--port") + 1] == port)
    check(
        [row["sha256"] for row in actual["images"]]
        == [hashlib.sha256(Path(row["file"]).read_bytes()).hexdigest() for row in images]
    )
    check(all("exec false" not in arg for arg in actual["argv"]))
    verify = copy.deepcopy(request)
    verify["action"] = "verify"
    plan = invoke("firmware-plan", verify)
    verify.update(expected_sha256=plan["sha256"])
    result = invoke("firmware-run", verify)
    check(result["ok"])
    check("verify-flash" in json.loads(calls.read_text().splitlines()[-1])["argv"])
    mutations = []
    for update in [
        dict(address=0x8000),
        dict(address=-1),
        dict(address=0x1000000),
        dict(address=0x10004),
        dict(format="elf"),
    ]:
        q = copy.deepcopy(request)
        q["manifest"]["images"][2].update(update)
        mutations.append(q)
    q = copy.deepcopy(request)
    del q["manifest"]["images"][2]["address"]
    mutations.append(q)
    q = copy.deepcopy(request)
    q["manifest"]["images"][2]["sha256"] = "0" * 64
    mutations.append(q)
    q = copy.deepcopy(request)
    q["extra"] = "--force"
    mutations.append(q)
    q = copy.deepcopy(request)
    q["manifest"]["images"][0]["file"] = str(root / "missing.bin")
    mutations.append(q)
    link = root / "link.bin"
    link.symlink_to(images[0]["file"])
    q = copy.deepcopy(request)
    q["manifest"]["images"][0]["file"] = str(link)
    mutations.append(q)
    for q in mutations:
        invoke("firmware-plan", q, False)
    # A stale aggregate review must fail before opening/running the fake flash tool.
    plan = invoke("firmware-plan", request)
    q = copy.deepcopy(ready)
    q["expected_sha256"] = plan["sha256"]
    count = len(calls.read_text().splitlines())
    Path(images[2]["file"]).write_bytes(b"changed")
    invoke("firmware-run", q, False)
    check(len(calls.read_text().splitlines()) == count)
    Path(images[2]["file"]).write_bytes(bytes(range(128)))
    for mode, expected in [("old", False), ("fail", False), ("overflow", False), ("slow", False)]:
        modefile.write_text(mode)
        plan = invoke("firmware-plan", request)
        q = copy.deepcopy(request)
        q.update(
            expected_sha256=plan["sha256"],
            program_confirmed=True,
            timeout_seconds=1 if mode == "slow" else 2,
        )
        if mode == "slow":
            plan = invoke("firmware-plan", q)
            q["expected_sha256"] = plan["sha256"]
        result = invoke("firmware-run", q, expected)
        if mode == "fail":
            check(result["exit_code"] == 9 and not result["ok"])
    modefile.write_text("slow")
    plan = invoke("firmware-plan", request)
    q = copy.deepcopy(request)
    q.update(expected_sha256=plan["sha256"], program_confirmed=True)
    path.write_text(json.dumps(q))
    proc = subprocess.Popen(
        [cli, "firmware-run", path], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    time.sleep(0.2)
    proc.send_signal(signal.SIGTERM)
    _, error = proc.communicate(timeout=4)
    check(proc.returncode != 0 and "cancel" in error.decode().lower())
    # A tool that exits must not leave a background child holding the port/pipe.
    modefile.write_text("fork")
    plan = invoke("firmware-plan", request)
    q = copy.deepcopy(request)
    q.update(expected_sha256=plan["sha256"], program_confirmed=True)
    result = invoke("firmware-run", q)
    child = int(pidfile.read_text())
    status = Path(f"/proc/{child}/stat")
    alive = status.exists() and status.read_text().split()[2] not in ["Z", "X"]
    if alive:
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
    check(not alive, "processo descendente ficou ativo após a ferramenta terminar")
    # Structural ELF/HEX checks use generated, independent fixtures; plan only.
    import struct

    def stm_request(file, format):
        q = copy.deepcopy(request)
        q.update(scripts=str(scripts), serial="FIXTURE")
        q["manifest"].update(
            family="stm32",
            target="stm32f1x",
            backend="openocd",
            images=[dict(role="application", file=str(file), format=format)],
        )
        return q

    def elf_fixture(segments=1):
        header = bytearray(52)
        header[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into(
            "<HHIIIIIHHHHHH", header, 16, 2, 40, 1, 0x08000001, 52, 0, 0, 52, 32, segments, 0, 0, 0
        )
        data = header + bytearray(32 * segments) + bytearray(b"CODE" * 4)
        for i in range(segments):
            struct.pack_into(
                "<IIIIIIII",
                data,
                52 + 32 * i,
                1,
                52 + 32 * segments,
                0x08000000,
                0x08000000,
                16,
                16,
                5,
                4,
            )
        return data

    elf = root / "application.elf"
    valid = elf_fixture()
    elf.write_bytes(valid)
    invoke("firmware-plan", stm_request(elf, "elf"))
    invalid = []
    for offset, fmt, value in [
        (28, "I", 0xFFFFFFF0),
        (44, "H", 129),
        (68, "I", 0xFFFFFFF0),
        (72, "I", 1),
        (64, "I", 0x20000000),
        (18, "H", 3),
        (24, "I", 0x09000000),
    ]:
        data = bytearray(valid)
        struct.pack_into("<" + fmt, data, offset, value)
        invalid.append(data)
    invalid.extend([elf_fixture(2), valid[:-1], b"\x7fELF" + bytes(48)])
    for data in invalid:
        elf.write_bytes(data)
        invoke("firmware-plan", stm_request(elf, "elf"), False)
    elf.write_bytes(valid)

    def hex_record(address, type, data):
        row = bytes([len(data), address >> 8, address & 255, type]) + data
        return ":" + (row + bytes([-sum(row) & 255])).hex().upper() + "\n"

    upper = hex_record(0, 4, bytes.fromhex("0800"))
    end = hex_record(0, 1, b"")
    body = hex_record(0, 0, b"ABCD")
    ihex = root / "application.hex"
    ihex.write_text(upper + body + end)
    invoke("firmware-plan", stm_request(ihex, "hex"))
    for content in [
        upper + body + body + end,
        upper + hex_record(0xFFFF, 0, b"AB") + end,
        upper + body,
        upper + body + end + body,
        upper + body[:-4] + "00\n" + end,
        upper + body.replace("\n", "\rGARBAGE\n") + end,
    ]:
        ihex.write_text(content)
        invoke("firmware-plan", stm_request(ihex, "hex"), False)
    ihex.write_text(upper + body + end)
    # Multi-image STM32 is merged once, without padding gaps or allowing shell/Tcl names.
    modefile.write_text("ok")
    stm = copy.deepcopy(request)
    stm.update(
        action="program",
        scripts=str(scripts),
        serial="FIXTURE",
        communication=request["communication"],
    )
    stm["manifest"].update(family="stm32", target="stm32f1x", backend="openocd")
    stm["manifest"]["images"] = copy.deepcopy(images[:2])
    stm["manifest"]["images"][0]["address"] = 0x0800FFF0
    stm["manifest"]["images"][1]["address"] = 0x08020000
    plan = invoke("firmware-plan", stm)
    stm.update(expected_sha256=plan["sha256"], program_confirmed=True)
    result = invoke("", stm, True, runner)
    check(result["ok"] and result["images"] == 2)
    contents = {}
    upper = 0
    end = False
    for line in result["hex_fixture"].splitlines():
        data = bytes.fromhex(line[1:])
        check(line[0] == ":" and sum(data) % 256 == 0 and len(data) == data[0] + 5)
        if data[3] == 4:
            upper = int.from_bytes(data[4:6], "big") << 16
        elif data[3] == 0:
            offset = int.from_bytes(data[1:3], "big") + upper
            for index, value in enumerate(data[4:-1]):
                check(offset + index not in contents)
                contents[offset + index] = value
        elif data[3] == 1:
            end = True
    check(end)
    for image in stm["manifest"]["images"]:
        expected = Path(image["file"]).read_bytes()
        check(bytes(contents[image["address"] + i] for i in range(len(expected))) == expected)
    check(
        len(contents)
        == sum(Path(image["file"]).stat().st_size for image in stm["manifest"]["images"])
    )
    os.close(master)
    os.close(slave)
    print(
        (
            f"{checks} verificações de firmware; processos falsos, snapshots, CRC/SHA, "
            "múltiplas imagens e PTY; sem gravação física."
        )
    )
