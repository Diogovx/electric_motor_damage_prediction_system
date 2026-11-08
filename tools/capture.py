#!/usr/bin/env python3
"""Captura o log serial por N segundos e resume as classificacoes por classe."""
import collections
import pathlib
import re
import sys
import time

import serial

label = sys.argv[1]                                   # ex.: saudavel
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
port = sys.argv[3] if len(sys.argv) > 3 else "/dev/ttyACM0"
SKIP_S = 5.0                                          # descarta o inicio (motor acelerando, janela enchendo)

pathlib.Path("logs").mkdir(exist_ok=True)
out = open(f"logs/{label}.log", "w", encoding="utf-8")

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.5
ser.dtr = False      # evita resetar a placa ao abrir a porta
ser.rts = False
ser.open()

pat = re.compile(r"INFER: (Saudavel|Desbalanceamento|Falha de Engrenagem) \((\d+\.\d)%\)")
classes = ["Saudavel", "Desbalanceamento", "Falha de Engrenagem"]
counts = collections.Counter()

t0 = time.time()
buf = b""
while time.time() - t0 < duration:
    buf += ser.read(ser.in_waiting or 1)
    *lines, buf = buf.split(b"\n")
    for raw in lines:
        line = raw.decode("utf-8", errors="replace").rstrip("\r")
        out.write(line + "\n")
        m = pat.search(line)
        if m and time.time() - t0 >= SKIP_S:
            counts[m.group(1)] += 1

ser.close()
out.close()

total = sum(counts.values())
print(f"\n== {label}: {total} janelas (apos descartar {SKIP_S:.0f} s)")
for c in classes:
    pct = 100.0 * counts[c] / total if total else 0.0
    print(f"  {c:20s} {counts[c]:5d}  {pct:5.1f}%")