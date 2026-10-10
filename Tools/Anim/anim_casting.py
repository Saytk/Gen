"""Casting des animations d'un sort : candidats côte à côte, puis montages refaits et avant/après.

Usage (depuis la racine du projet, éditeur ouvert sur le port 8010, PYTHONIOENCODING=utf-8) :
    python Tools/Anim/anim_casting.py <Set>                    capture des candidats + planches
    python Tools/Anim/anim_casting.py <Set> --no-capture       planches seulement
    python Tools/Anim/anim_casting.py <Set> --only a,b         recapture une partie des candidats
    python Tools/Anim/anim_casting.py <Set> --montages before  capture des séquences de montages actuelles
    python Tools/Anim/anim_casting.py <Set> --apply            git lfs lock + retarget + montages refaits (sauvés)
    python Tools/Anim/anim_casting.py <Set> --montages after   capture après, puis planche et GIF avant/après
    python Tools/Anim/anim_casting.py <Set> --analyse Gideon:RMB_Cast   mains par image (trouver le lâcher)
    python Tools/Anim/anim_casting.py <Set> --check            avant commit : clips utilisés, fuites vers Paragon / _Local

<Set> = Tools/Anim/sets/<Set>.json :
    "spell", "brief", "window" (s), "fps", "size", "max_len", "angles", "clips" {label: chemin source},
    "montages" {AM_nom: [segment...]} (voir Content/Python/gen_anim_montage.py),
    "sequences" {nom: [AM_nom, ...]} : enchaînements tels que joués en jeu, pour l'avant/après.

Sorties dans .superpowers/sdd/Anim/<Set>/ :
    candidates_<angle>.png / .gif / _half.gif     candidats (planche : une ligne par clip, une image tous les 0,1 s)
    montage_<angle>.png / .gif / _half.gif        avant/après des montages
"""
import argparse
import json
import os
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
OUT_ROOT = os.path.join(PROJECT, ".superpowers", "sdd", "Anim")
ANIMS = "/Game/Gen/Champions/Curffe/Animations"
sys.path.insert(0, os.path.join(PROJECT, "Tools", "Mcp"))


def load_set(name):
    with open(os.path.join(HERE, "sets", name + ".json"), encoding="utf-8") as f:
        cfg = json.load(f)
    cfg.setdefault("fps", 30)
    cfg.setdefault("size", 320)
    cfg.setdefault("max_len", 2.0)
    cfg.setdefault("angles", ["three_quarter"])
    return cfg


def editor(code, port="8010"):
    """Exécute du Python dans l'éditeur et renvoie sa sortie texte."""
    import mcp_call
    txt = mcp_call.call("execute_python_code", {"code": "import unreal, json, importlib\n" + code, "auto_save": False}, port)
    try:
        res = json.loads(txt)
    except Exception:
        return txt
    if not res.get("success", True) or res.get("error"):
        raise RuntimeError(txt[:3000])
    return res.get("output", txt)


# ---------- capture ----------

def capture_candidates(name, cfg, only, port):
    clips = {k: v for k, v in cfg["clips"].items() if not only or k in only}
    for angle in cfg["angles"]:
        out = editor("import gen_anim_capture as gac\nimportlib.reload(gac)\n"
                     f"print(json.dumps(gac.capture_set({name!r}, {clips!r}, fps={cfg['fps']}, angle={angle!r}, "
                     f"size={cfg['size']}, max_len={cfg['max_len']})))", port)
        print(angle, out[:600])


def capture_montages(name, cfg, when, port):
    for angle in cfg["angles"]:
        for seq, ams in cfg["sequences"].items():
            folder = os.path.join(OUT_ROOT, name, "montage", angle, f"{when}_{seq}").replace("\\", "/")
            paths = [f"{ANIMS}/{a}" for a in ams]
            out = editor("import gen_anim_montage as gam\nimportlib.reload(gam)\n"
                         f"print(gam.capture_sequence({folder!r}, {paths!r}, fps={cfg['fps']}, angle={angle!r}, size={cfg['size']}))", port)
            print(angle, f"{when}_{seq}", out.strip()[:200])


def apply(name, cfg, port):
    files = [f"Content/Gen/Champions/Curffe/Animations/{am}.uasset" for am in cfg["montages"]]
    locks = subprocess.run(["git", "lfs", "locks", "--json"], cwd=PROJECT, capture_output=True, text=True).stdout
    mine = subprocess.run(["git", "config", "user.name"], cwd=PROJECT, capture_output=True, text=True).stdout.strip()
    for lk in json.loads(locks or "[]"):
        if lk["path"] in files and lk.get("owner", {}).get("name") not in (mine, None):
            raise SystemExit(f"Locked by {lk['owner']['name']}: {lk['path']} — stop and tell the user.")
    held = {lk["path"] for lk in json.loads(locks or "[]")}
    todo = [f for f in files if f not in held]
    if todo:
        subprocess.run(["git", "lfs", "lock", *todo], cwd=PROJECT, check=True)
    segs_of = lambda m: m["segments"] if isinstance(m, dict) else m
    srcs = sorted({s["src"] for m in cfg["montages"].values() for s in segs_of(m)})
    print(editor("import gen_anim_montage as gam\nimportlib.reload(gam)\n"
                 f"print(json.dumps(gam.retarget({srcs!r})))", port))
    for am, segs in cfg["montages"].items():
        print(am, editor("import gen_anim_montage as gam\n"
                         f"print(json.dumps(gam.apply_montage({ANIMS + '/' + am!r}, {segs!r})))", port).strip())


# ---------- planches ----------

def _font(size):
    for f in ("consola.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def _frames(folder):
    meta = json.load(open(os.path.join(folder, "meta.json")))
    files = sorted(p for p in os.listdir(folder) if p.startswith("f") and p.endswith(".png"))
    return meta, [os.path.join(folder, p) for p in files]


# Le service de capture rend le personnage presque noir sur un ciel bleu : on garde sa silhouette
# (ce que le joueur lit depuis la caméra de jeu), sombre sur fond clair, recadrée à la même échelle pour tous.
BG, FG = (226, 222, 214), (40, 30, 26)
_masks = {}


def _mask(path):
    if path not in _masks:
        r, _, b = Image.open(path).convert("RGB").split()
        # personnage : bleu ≈ rouge (gris foncé) ; fond : bleu nettement au-dessus du rouge
        _masks[path] = ImageChops.subtract(b, r).point(lambda v: 255 if v < 14 else 0)
    return _masks[path]


def _layout(labels, data, pad=0.08):
    """Centre par ligne (boîte englobante de toutes ses images), taille commune (la plus grande)."""
    boxes = {}
    for label, (_, files) in zip(labels, data):
        x0 = y0 = 10 ** 6
        x1 = y1 = 0
        for f in files:
            bb = _mask(f).getbbox()
            if bb:
                x0, y0, x1, y1 = min(x0, bb[0]), min(y0, bb[1]), max(x1, bb[2]), max(y1, bb[3])
        boxes[label] = (x0, y0, x1, y1)
    side = max(max(b[2] - b[0], b[3] - b[1]) for b in boxes.values()) * (1 + 2 * pad)
    return {k: ((b[0] + b[2]) / 2, (b[1] + b[3]) / 2, side) for k, b in boxes.items()}


def _cell(path, center, size):
    cx, cy, side = center
    box = tuple(int(v) for v in (cx - side / 2, cy - side / 2, cx + side / 2, cy + side / 2))
    m = _mask(path).crop(box)
    im = Image.new("RGB", m.size, BG)
    im.paste(Image.new("RGB", m.size, FG), (0, 0), m)
    return im.resize((size, size), Image.LANCZOS)


def build_sheet(base, labels, out, max_len, step=0.1, cell=150):
    data = [_frames(os.path.join(base, k)) for k in labels]
    layout = _layout(labels, data)
    ncols = int(min(max_len, max(m["length"] for m, _ in data)) / step) + 1
    lab_w = 200
    font, small = _font(14), _font(11)
    sheet = Image.new("RGB", (lab_w + ncols * cell, 20 + len(labels) * (cell + 2)), (18, 18, 22))
    d = ImageDraw.Draw(sheet)
    for c in range(ncols):
        d.text((lab_w + c * cell + 4, 3), f"{c * step:.1f}s", fill=(200, 200, 200), font=small)
    for r, (label, (meta, files)) in enumerate(zip(labels, data)):
        y = 20 + r * (cell + 2)
        d.text((6, y + 6), label, fill=(255, 220, 160), font=font)
        d.text((6, y + 26), f"{meta['length']:.2f}s", fill=(180, 180, 180), font=small)
        for c in range(ncols):
            t = c * step
            if t > meta["length"] + 1e-3:
                break
            i = min(int(round(t * meta["fps"])), len(files) - 1)
            sheet.paste(_cell(files[i], layout[label], cell), (lab_w + c * cell + 1, y))
    sheet.save(out)
    return out


def build_gif(base, labels, out, fps, window=None, speed=1.0, cell=240, cols=4, hold=0.4):
    """Toutes les lignes en temps réel, synchronisées ; chacune tient sa dernière pose jusqu'à la plus longue, puis boucle."""
    data = [_frames(os.path.join(base, k)) for k in labels]
    layout = _layout(labels, data)
    cols = min(cols, len(labels))
    total = max(len(f) for _, f in data) + int(hold * fps)
    rows = (len(labels) + cols - 1) // cols
    font = _font(15)
    cache, frames = {}, []
    for t in range(total):
        canvas = Image.new("RGB", (cols * cell, rows * (cell + 22)), (18, 18, 22))
        d = ImageDraw.Draw(canvas)
        for n, (label, (meta, files)) in enumerate(zip(labels, data)):
            key = files[min(t, len(files) - 1)]
            if key not in cache:
                cache[key] = _cell(key, layout[label], cell - 4)
            x, y = (n % cols) * cell, (n // cols) * (cell + 22)
            canvas.paste(cache[key], (x + 2, y + 22))
            sec = min(t, len(files) - 1) / fps
            col = (255, 120, 80) if window and sec <= window else (255, 220, 160)  # orange : fenêtre de jeu du sort
            d.text((x + 4, y + 3), f"{label}  {sec:.2f}/{meta['length']:.2f}s", fill=col, font=font)
        frames.append(canvas.quantize(colors=128))
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=int(1000 / fps / speed), loop=0, optimize=True)
    return out


def build_all(base, labels, prefix, cfg, window):
    outs = [build_sheet(base, labels, prefix + ".png", cfg["max_len"], step=cfg.get("step", 0.1)),
            build_gif(base, labels, prefix + ".gif", cfg["fps"], window),
            build_gif(base, labels, prefix + "_half.gif", cfg["fps"], window, speed=0.5)]
    for o in outs:
        print(o)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("set")
    ap.add_argument("--no-capture", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--check", action="store_true", help="clips Paragon utilisés + fuites vers les packs Paragon / _Local (avant commit)")
    ap.add_argument("--montages", choices=["before", "after", "sheets"])
    ap.add_argument("--analyse", default="", help="Gideon:Clip,Serath:Clip... : retarget si besoin, puis mains/torse par image")
    ap.add_argument("--bones", default="hand_r,hand_l:spine_05", help="os[,os...]:référence pour --analyse (ex. pelvis,foot_l:root)")
    ap.add_argument("--every", type=int, default=1)
    ap.add_argument("--port", default="8010")
    a = ap.parse_args()
    cfg = load_set(a.set)
    root = os.path.join(OUT_ROOT, a.set)
    if a.analyse:
        srcs = a.analyse.split(",")
        editor("import gen_anim_montage as gam\nimportlib.reload(gam)\n"
               f"print(json.dumps(gam.retarget({srcs!r})))", a.port)
        for src in srcs:
            bones, ref = a.bones.split(":")
            bones = tuple(bones.split(","))
            out = editor(f"import gen_anim_montage as gam\nprint(json.dumps(gam.analyse({src!r}, {bones!r}, {ref!r}, {a.every})))", a.port)
            print(f"== {src}  (frame, then per bone {bones} [side, fwd, up, m/s] relative to {ref})")
            for row in json.loads(out):
                print(" ", row)
        return
    if a.apply:
        apply(a.set, cfg, a.port)
        return
    if a.check:
        paths = [f"{ANIMS}/{am}" for am in cfg["montages"]]
        print(editor("import gen_anim_montage as gam\nimportlib.reload(gam)\n"
                     f"print(json.dumps(gam.check_leaks({paths!r}), indent=1))", a.port))
        return
    if a.montages:
        if a.montages != "sheets":
            capture_montages(a.set, cfg, a.montages, a.port)
        for angle in cfg["angles"]:
            base = os.path.join(root, "montage", angle)
            labels = [f"{w}_{s}" for s in cfg["sequences"] for w in ("before", "after") if os.path.isdir(os.path.join(base, f"{w}_{s}"))]
            build_all(base, labels, os.path.join(root, f"montage_{angle}"), cfg, None)
        return
    if not a.no_capture:
        capture_candidates(a.set, cfg, [s for s in a.only.split(",") if s], a.port)
    for angle in cfg["angles"]:
        base = os.path.join(root, angle)
        labels = [k for k in cfg["clips"] if os.path.isdir(os.path.join(base, k))]
        build_all(base, labels, os.path.join(root, f"candidates_{angle}"), cfg, cfg.get("window"))


if __name__ == "__main__":
    main()
