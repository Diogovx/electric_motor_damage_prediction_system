"""Compara o modelo embutido (EQ ref) com o ModelManager (probs) e confere o DSP (DSPCHK)."""
import re
import sys

ref_re = re.compile(r"EQ ref: ([\d.]+) ([\d.]+) ([\d.]+)")
dyn_re = re.compile(r"MODEL_MGR: probs: ([\d.]+) ([\d.]+) ([\d.]+)")
dsp_re = re.compile(r"DSPCHK max_diff=([\d.eE+-]+)")

pairs, dsp = [], []
ref = None
for line in open(sys.argv[1], errors="replace"):
    m = dsp_re.search(line)
    if m:
        dsp.append(float(m.group(1)))
        continue
    m = ref_re.search(line)
    if m:
        ref = [float(x) for x in m.groups()]
        continue
    m = dyn_re.search(line)
    if m and ref is not None:
        pairs.append((ref, [float(x) for x in m.groups()]))
        ref = None

if not pairs:
    sys.exit("Nenhum par 'EQ ref' / 'probs' encontrado no log.")

diffs = [max(abs(a - b) for a, b in zip(r, d)) for r, d in pairs]
same = sum(1 for r, d in pairs if r.index(max(r)) == d.index(max(d)))

print(f"pares comparados : {len(pairs)}")
print(f"mesma classe     : {same} ({100.0 * same / len(pairs):.1f}%)")
print(f"diferenca maxima : {max(diffs):.4f}  (ate ~0.0005 e so arredondamento do log)")
print(f"diferenca media  : {sum(diffs) / len(diffs):.4f}")
if dsp:
    print(f"features do DSP  : diferenca maxima {max(dsp):.6g} em {len(dsp)} janelas")
else:
    print("features do DSP  : nenhuma linha DSPCHK no log")