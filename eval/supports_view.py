# Draws the support toolpaths of two G-code files side by side, so a change
# to the organic supports can be checked by eye against native Orca, not
# only by grams. Front (X-Z), side (Y-Z) and top (X-Y) views; support red,
# the part grey. Each file is centred on its part, so where Orca arranged the
# model on its bed doesn't matter. Also prints support length per file and
# the layer where the two differ most.
# usage: supports_view.py <a.gcode> <label a> <b.gcode> <label b> <out.png>
import re, sys
from PIL import Image, ImageDraw

def read(path):
    segs = []                                     # (x0, y0, x1, y1, z, is_support)
    x = y = z = e = 0.0; rel = False; feat = ''
    for line in open(path, errors='replace'):
        if line.startswith(';'):
            m = re.match(r';\s*(?:FEATURE:|TYPE:)\s*(.*)', line)
            if m: feat = m.group(1).strip().lower()
            continue
        code = line.split(';', 1)[0].split()
        if not code: continue
        c = code[0]
        if c == 'M83': rel = True
        elif c == 'M82': rel = False
        elif c == 'G92':
            for w in code[1:]:
                if w[0] == 'E': e = float(w[1:])
        elif c in ('G0', 'G1', 'G2', 'G3'):
            nx, ny, nz, ne = x, y, z, None
            for w in code[1:]:
                k, v = w[0], w[1:]
                if k == 'X': nx = float(v)
                elif k == 'Y': ny = float(v)
                elif k == 'Z': nz = float(v)
                elif k == 'E': ne = float(v)
            de = 0.0 if ne is None else (ne if rel else ne - e)
            if ne is not None and not rel: e = ne
            if de > 0 and (nx != x or ny != y):
                segs.append((x, y, nx, ny, nz, feat.startswith('support')))
            x, y, z = nx, ny, nz
    return segs

def centre(segs):
    part = [s for s in segs if not s[5]] or segs
    xs = [v for s in part for v in (s[0], s[2])]; ys = [v for s in part for v in (s[1], s[3])]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    return [(s[0] - cx, s[1] - cy, s[2] - cx, s[3] - cy, s[4], s[5]) for s in segs]

def stats(segs):
    per = {}
    for s in segs:
        if s[5]: per[round(s[4], 2)] = per.get(round(s[4], 2), 0) + ((s[2] - s[0]) ** 2 + (s[3] - s[1]) ** 2) ** .5
    return per

a, la, b, lb, out = sys.argv[1:]
A, B = centre(read(a)), centre(read(b))
allx = [v for S in (A, B) for s in S for v in (s[0], s[2])]
ally = [v for S in (A, B) for s in S for v in (s[1], s[3])]
allz = [s[4] for S in (A, B) for s in S]
span = max(max(allx) - min(allx), max(ally) - min(ally), max(allz)) * 1.05 or 1
P = 420; sc = P / span
img = Image.new('RGB', (3 * P + 40, 2 * P + 70), (12, 12, 12)); d = ImageDraw.Draw(img)
views = [('front X-Z', lambda s: (s[0], s[4], s[2], s[4])), ('side Y-Z', lambda s: (s[1], s[4], s[3], s[4])), ('top X-Y', lambda s: (s[0], -s[1], s[2], -s[3]))]
for row, (S, lab) in enumerate(((A, la), (B, lb))):
    oy = 30 + row * (P + 20)
    d.text((10, oy - 18), lab, fill=(230, 230, 230))
    for col, (name, f) in enumerate(views):
        ox = 10 + col * (P + 10)
        d.rectangle([ox, oy, ox + P, oy + P], outline=(60, 60, 60))
        if row == 0: d.text((ox + 4, 8), name, fill=(150, 150, 150))
        for sup in (False, True):
            for s in S:
                if s[5] != sup: continue
                u0, v0, u1, v1 = f(s)
                if col < 2:   # vertical axis is z, from the bottom
                    p = (ox + P / 2 + u0 * sc, oy + P - v0 * sc, ox + P / 2 + u1 * sc, oy + P - v1 * sc)
                else:
                    p = (ox + P / 2 + u0 * sc, oy + P / 2 + v0 * sc, ox + P / 2 + u1 * sc, oy + P / 2 + v1 * sc)
                d.line(p, fill=(225, 37, 27) if sup else (90, 90, 90), width=1)
img.save(out)
sa, sb = stats(A), stats(B)
ta, tb = sum(sa.values()), sum(sb.values())
worst = max(set(sa) | set(sb), key=lambda z: abs(sa.get(z, 0) - sb.get(z, 0)), default=None)
print(f'support length: {la} {ta:.1f} mm, {lb} {tb:.1f} mm ({(tb - ta) / ta * 100 if ta else 0:+.2f}%), '
      f'support layers {len(sa)} vs {len(sb)}' + (f', most different layer z={worst}: {sa.get(worst, 0):.1f} vs {sb.get(worst, 0):.1f} mm' if worst is not None else ''))
