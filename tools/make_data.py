#!/usr/bin/env python3
"""Gera data/ (LittleFS): config e modelo ativos (A) + payloads de teste A e B.
Uso: python3 tools/make_data.py configA.json modeloA.tflite configB.json modeloB.tflite
"""
import base64, json, pathlib, shutil, sys, zlib


def load(cfg_path, model_path):
    cfg = json.load(open(cfg_path, encoding="utf-8"))
    model = open(model_path, "rb").read()
    cfg.setdefault("model", {})
    cfg["model"]["size"] = len(model)
    cfg["model"]["crc32"] = f"0x{zlib.crc32(model) & 0xFFFFFFFF:08x}"
    return cfg, model


def payload(cfg, model, tag):
    return {"config": cfg, "model_b64": base64.b64encode(model).decode("ascii"), "tag": tag}


cfg_a, model_a = load(sys.argv[1], sys.argv[2])
cfg_b, model_b = load(sys.argv[3], sys.argv[4])

data = pathlib.Path("data")
data.mkdir(exist_ok=True)
json.dump(cfg_a, open(data / "config.json", "w", encoding="utf-8"), ensure_ascii=False)
shutil.copyfile(sys.argv[2], data / "model.tflite")
json.dump(payload(cfg_a, model_a, "A"), open(data / "test_a.json", "w", encoding="utf-8"), ensure_ascii=False)
json.dump(payload(cfg_b, model_b, "B"), open(data / "test_b.json", "w", encoding="utf-8"), ensure_ascii=False)
for f in sorted(data.iterdir()):
    print(f"{f.name:16s} {f.stat().st_size:7d} B")
