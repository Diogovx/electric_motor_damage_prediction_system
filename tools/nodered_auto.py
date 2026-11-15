#!/usr/bin/env python3
"""Liga/desliga o ensaio automatico do Node-RED editando ~/.node-red/flows.json.
Pare o Node-RED antes de rodar."""
import json
import pathlib
import sys

modo = sys.argv[1] if len(sys.argv) > 1 else ""
if modo not in ("on", "off"):
    sys.exit("uso: nodered_auto.py on|off")

p = pathlib.Path.home() / ".node-red" / "flows.json"
flows = json.loads(p.read_text())
if not isinstance(flows, list):
    sys.exit("flows.json em formato inesperado")

achou = False
for n in flows:
    if n.get("id") == "inj_auto":
        if modo == "on":
            n.pop("d", None)
        else:
            n["d"] = True
        achou = True
    if n.get("id") == "fn_csv" and "p.heap" not in n.get("func", ""):
        n["func"] = n["func"].replace("p.gap_ms]", "p.gap_ms, p.heap]")

if not achou:
    sys.exit("no inj_auto nao encontrado")
p.write_text(json.dumps(flows, indent=4))
print("ensaio automatico:", modo)
