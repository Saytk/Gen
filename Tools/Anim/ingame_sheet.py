"""Planche et GIF d'un sort capturé EN JEU (Content/Python/gen_anim_pie.py cast_capture) : rendu réel, caméra de jeu.

Usage : python Tools/Anim/ingame_sheet.py <nom> [<nom>...] [--crop 0.45] [--every 3]
    .superpowers/sdd/Anim/InGame/<nom>/ -> <nom>.gif (temps réel), <nom>_slow.gif (x0,25), <nom>_sheet.png
Recadrage carré autour du personnage (part de la hauteur de l'image : --crop), qui suit son déplacement.
Chaque image est annotée : temps de jeu, montage actif, lacet de l'acteur (pour voir s'il se tourne vers la visée).
"""
import argparse
import json
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), ".superpowers", "sdd", "Anim", "InGame")


def _font(size):
    for f in ("consola.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def frames(name, crop=0.45, cell=420):
    folder = os.path.join(ROOT, name)
    meta = json.load(open(os.path.join(folder, "meta.json")))
    font = _font(15)
    out = []
    for i, fr in enumerate(meta["frames"]):
        path = os.path.join(folder, "f%03d.png" % i)
        if not os.path.exists(path):
            continue
        im = Image.open(path).convert("RGB")
        if "sx" not in fr:  # caméra d'animation (gen_anim_pie.rig_capture) : déjà cadrée
            c = im.resize((cell, cell), Image.LANCZOS)
            d = ImageDraw.Draw(c)
            d.rectangle((0, 0, cell, 22), fill=(0, 0, 0))
            d.text((4, 3), f"{fr['t']:.2f}s {fr['montage'][3:] if fr['montage'] != '-' else '-'} yaw {fr['yaw']:.0f} v {fr['speed']}",
                   fill=(255, 220, 160), font=font)
            out.append(c)
            continue
        side = int(im.height * crop)
        cx, cy = fr["sx"], fr["sy"] - side * 0.1  # un peu au-dessus du centre de la capsule : la tête et les bras
        box = (int(cx - side / 2), int(cy - side / 2), int(cx + side / 2), int(cy + side / 2))
        c = im.crop(box).resize((cell, cell), Image.LANCZOS)
        d = ImageDraw.Draw(c)
        d.rectangle((0, 0, cell, 22), fill=(0, 0, 0))
        d.text((4, 3), f"{fr['t']:.2f}s  {fr['montage']}  yaw {fr['yaw']:.0f}", fill=(255, 220, 160), font=font)
        out.append(c)
    return meta, out


def build(name, crop, every):
    meta, ims = frames(name, crop)
    folder = os.path.join(ROOT, name)
    ms = int(meta["interval"] * 1000)
    q = [im.quantize(colors=200) for im in ims]
    q[0].save(os.path.join(folder, name + ".gif"), save_all=True, append_images=q[1:], duration=ms, loop=0)
    q[0].save(os.path.join(folder, name + "_slow.gif"), save_all=True, append_images=q[1:], duration=ms * 4, loop=0)
    pick = ims[::every]
    cols = 8
    rows = (len(pick) + cols - 1) // cols
    w = 240
    sheet = Image.new("RGB", (cols * w, rows * w), (20, 20, 20))
    for n, im in enumerate(pick):
        sheet.paste(im.resize((w, w)), ((n % cols) * w, (n // cols) * w))
    sheet.save(os.path.join(folder, name + "_sheet.png"))
    print(os.path.join(folder, name + "_sheet.png"), f"{len(ims)} frames")


def compare(names, out_name, cell=420):
    """Plusieurs captures côte à côte, synchronisées image par image (même scénario), en GIF temps réel et lent."""
    seqs = [frames(n, cell=cell)[1] for n in names]
    meta = frames(names[0], cell=cell)[0]
    n = max(len(s) for s in seqs)
    font = _font(18)
    out = []
    for i in range(n):
        c = Image.new("RGB", (cell * len(seqs), cell + 26), (12, 12, 12))
        d = ImageDraw.Draw(c)
        for k, (name, s) in enumerate(zip(names, seqs)):
            c.paste(s[min(i, len(s) - 1)], (k * cell, 26))
            d.text((k * cell + 6, 3), name, fill=(255, 255, 255), font=font)
        out.append(c.quantize(colors=200))
    os.makedirs(os.path.join(ROOT, out_name), exist_ok=True)
    ms = int(meta["interval"] * 1000)
    for suffix, k in (("", 1), ("_slow", 4)):
        path = os.path.join(ROOT, out_name, out_name + suffix + ".gif")
        out[0].save(path, save_all=True, append_images=out[1:], duration=ms * k, loop=0)
        print(path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="+")
    ap.add_argument("--compare", default="", help="nom de sortie : met les captures côte à côte")
    ap.add_argument("--crop", type=float, default=0.45)
    ap.add_argument("--every", type=int, default=3)
    a = ap.parse_args()
    if a.compare:
        compare(a.names, a.compare)
        raise SystemExit
    for n in a.names:
        build(n, a.crop, a.every)
