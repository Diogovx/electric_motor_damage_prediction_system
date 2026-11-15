#!/usr/bin/env python3
"""Resume os ensaios de reconfiguracao (CSV gravado pelo Node-RED)."""
import csv
import statistics
import sys

CAMPOS = ["transfer_ms", "persist_ms", "switch_ms", "refill_ms", "reconf_ms", "gap_ms", "heap"]


def resumo(nome, linhas):
    if not linhas:
        return
    print(f"\n{nome} (n={len(linhas)})")
    print(f"{'campo':<13}{'media':>10}{'dp':>9}{'min':>10}{'max':>10}")
    for c in CAMPOS:
        v = [float(l[c]) for l in linhas if l.get(c) not in (None, "")]
        if not v:
            continue
        dp = statistics.stdev(v) if len(v) > 1 else 0.0
        print(f"{c:<13}{statistics.mean(v):>10.1f}{dp:>9.1f}{min(v):>10.1f}{max(v):>10.1f}")


def main():
    caminho = sys.argv[1] if len(sys.argv) > 1 else "logs/reconfig_trials.csv"
    with open(caminho, newline="") as f:
        linhas = list(csv.DictReader(f))
    resumo("todos", linhas)
    for tag in sorted({l["tag"] for l in linhas}):
        resumo(f"destino {tag}", [l for l in linhas if l["tag"] == tag])
    heap = [float(l["heap"]) for l in linhas if l.get("heap")]
    if len(heap) > 1:
        print(f"\nheap livre: primeiro={heap[0]:.0f}  ultimo={heap[-1]:.0f}  variacao={heap[-1] - heap[0]:+.0f} bytes")


main()
