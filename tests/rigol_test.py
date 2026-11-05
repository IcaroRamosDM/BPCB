#!/usr/bin/env python3
"""Only loopback sockets: never discovers or connects to a real instrument."""

import json
import socket
import subprocess
import sys
import threading
import time

cli = sys.argv[1]
responses = {
    "*IDN?": "RIGOL TECHNOLOGIES,DHO802,TEST-SIMULATED,1.0",
    ":CHANnel1:UNITs?": "VOLT",
    ":TRIGger:STATus?": "STOP",
    ":CURSor:MODE?": "MAN",
    ":CURSor:MANual:TYPE?": "TIME",
    ":CURSor:MANual:SOURce?": "CHAN1",
    ":CURSor:MANual:CAX?": "-0.001",
    ":CURSor:MANual:CBX?": "0.002",
    ":MEASure:ITEM? VTOP,CHANnel1": "4.8",
    ":MEASure:ITEM? VAVG,CHANnel1": "0.01",
}
for channel in (1, 2):
    for command, value in {
        "DISPlay": "1",
        "UNITs": "VOLT",
        "COUPling": "DC",
        "PROBe": "10",
        "SCALe": "0.2",
        "OFFSet": "0",
    }.items():
        responses[f":CHANnel{channel}:{command}?"] = value
    for metric, value in {
        "VAVG": "0.110",
        "VPP": "0.008",
        "VMIN": "0.106",
        "VMAX": "0.114",
    }.items():
        responses[f":MEASure:ITEM? {metric},CHANnel{channel}"] = value
responses[":MEASure:ITEM? VAVG,CHANnel1"] = "0.01"


def scenario(field, changes=None, expected=None, fragment=False, silent=False):
    table = responses | dict(changes or {})
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen()
    port = server.getsockname()[1]
    seen = []
    errors = []

    def serve():
        try:
            with server:
                conn, _ = server.accept()
                with conn:
                    stream = conn.makefile("rb")
                    while line := stream.readline():
                        command = line.decode().strip()
                        seen.append(command)
                        assert "?" in command and command in table, command
                        if silent:
                            time.sleep(5.3)
                            break
                        answer = (table[command] + "\n").encode()
                        if fragment:
                            for byte in answer:
                                conn.sendall(bytes([byte]))
                        else:
                            conn.sendall(answer)
        except (BrokenPipeError, ConnectionResetError):
            pass
        except BaseException as ex:
            errors.append(ex)

    thread = threading.Thread(target=serve)
    thread.start()
    result = subprocess.run(
        [cli, "rigol", f"tcp://127.0.0.1:{port}", field], capture_output=True, text=True, timeout=8
    )
    thread.join(6)
    assert not thread.is_alive() and not errors, errors
    if expected is None:
        assert result.returncode != 0, result.stdout
    else:
        assert result.returncode == 0, result.stderr
        data = json.loads(result.stdout)
        if isinstance(expected, (int, float)):
            assert abs(data["value"] - expected) < 1e-9, data
        elif callable(expected):
            expected(data)
        else:
            assert expected in data["instrument"]
    return seen


def detailed(data):
    assert data["channels"][0]["VAVG"] == 0.110, data
    assert data["channels"][0]["VPP"] == 0.008 and data["channels"][0]["probe_ratio"] == 10
    assert data["channels"][1]["VAVG"] is None
    assert data["channels"][1]["coupling"] == "AC"
    assert "value" not in data and data["queried_at"]


scenario(
    "diagnostic",
    {
        ":MEASure:ITEM? VAVG,CHANnel1": "0.110",
        ":MEASure:ITEM? VAVG,CHANnel2": "9.9E37",
        ":CHANnel2:COUPling?": "AC",
    },
    expected=detailed,
    fragment=True,
)
scenario("identify", expected="DHO802", fragment=True)
scenario("cap_inicial", expected=0.01)
scenario("cap_inicial", {":MEASure:ITEM? VAVG,CHANnel1": "0.011"}, expected=0.011)
scenario("cap_inicial", {":MEASure:ITEM? VAVG,CHANnel1": "0.1"}, expected=0.1)
scenario("cap_inicial", {":MEASure:ITEM? VAVG,CHANnel1": "0.110"})
scenario("cap_final", expected=4.8)
scenario("cap_tempo", expected=3, fragment=True)
scenario("cap_final", {":TRIGger:STATus?": "RUN"})
scenario("cap_final", {":MEASure:ITEM? VTOP,CHANnel1": "9.9E37"})
scenario("cap_inicial", {":MEASure:ITEM? VAVG,CHANnel1": "NaN"})
scenario("cap_final", {":CHANnel1:UNITs?": "AMP"})
scenario("cap_tempo", {":CURSor:MANual:TYPE?": "AMPL"})
scenario("cap_tempo", {":CURSor:MANual:CBX?": "-0.002"})
scenario("identify", {"*IDN?": "OTHER,SCOPE,123,1"})
scenario("read:2:VAVG", expected=0.110)
scenario("read:1:VAVG", {":MEASure:ITEM? VAVG,CHANnel1": "0.110"}, expected=0.110)
for metric, value in [("VPP", 0.008), ("VMIN", 0.106), ("VMAX", 0.114)]:
    scenario("read:2:" + metric, expected=value)
scenario("read:2:VTOP", {":MEASure:ITEM? VTOP,CHANnel2": "4.6"}, expected=4.6)
scenario("read:2:TIME", {":CURSor:MANual:SOURce?": "CHAN2"}, expected=3)
scenario("read:2:TIME")
scenario("read:2:VAVG", {":CHANnel2:UNITs?": "AMP"})
scenario("read:2:VPP", {":TRIGger:STATus?": "RUN"})
scenario("read:2:VAVG", {":MEASure:ITEM? VAVG,CHANnel2": "NaN"})
for request in ["read:3:VAVG", "read:2:VAVG;RUN", "read:2:FREQ", "read:1:VAVG\nRUN"]:
    assert (
        subprocess.run(
            [cli, "rigol", "tcp://127.0.0.1:1", request], capture_output=True, timeout=8
        ).returncode
        != 0
    )
scenario("identify", silent=True)
for endpoint in ["tcp://hostname:5555", "tcp://127.0.0.1:0", "usbtmc:/etc/passwd"]:
    assert (
        subprocess.run(
            [cli, "rigol", endpoint, "identify"], capture_output=True, timeout=8
        ).returncode
        != 0
    )
print(
    "Rigol: leituras, fragmentação, unidades, captura parada, cursores, valores inválidos, timeout e endpoints OK."
)
