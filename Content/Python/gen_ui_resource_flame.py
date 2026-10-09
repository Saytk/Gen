"""
Logo de la ressource « flammes » (T_UI_Resource_Flame, 256 px RGBA), à droite de la barre de vie au-dessus des
personnages. Redessiné en procédural à 1024 px d'après le concept Docs/Design/example_hpbar_concept_v0.jpg : bras de
flamme en spirale effilés (jaune au centre du trait, orange puis rouge vers les bouts), centre sombre, contour sombre épais.
Utilisation : python Content/Python/gen_ui_resource_flame.py  (Pillow + numpy) -> Saved/UIIcons/T_UI_Resource_Flame.png
"""
import numpy as np
from PIL import Image, ImageFilter
N = 1024
y, x = np.mgrid[0:N, 0:N].astype(np.float32)
cx, cy = N * 0.5, N * 0.57
X = (x - cx) / (N * 0.5); Y = (y - cy) / (N * 0.5)
up = np.clip(-Y, 0, None)
Ys = np.where(Y < 0, Y / (1 + 0.4 * up), Y)                 # le haut s'étire en pointe de flamme
Xs = X * (1 + 0.15 * up)
r = np.sqrt(Xs ** 2 + Ys ** 2) + 1e-6
th = np.arctan2(-Ys, Xs)                                    # angle trigonométrique (Y vers le haut)
fill = np.zeros_like(r); core_t = np.zeros_like(r); along = np.zeros_like(r)
def stroke(start, r0, r1, span, w0):
    global fill, core_t, along
    b = np.log(r1 / r0) / span
    for k in (0, 1):
        phi = np.mod(th - start, 2 * np.pi) + 2 * np.pi * k     # angle parcouru le long du bras
        ok = phi <= span
        ra = r0 * np.exp(b * phi)
        u = np.clip(phi / span, 0, 1)
        hw = w0 * np.sin(np.pi * np.clip(u * 0.92 + 0.04, 0, 1)) ** 0.6 * (1 - 0.55 * u)
        d = np.abs(r - ra) / np.maximum(hw, 1e-4)
        m = ok & (d < 1)
        a = np.clip((1 - d) / 0.08, 0, 1) * ok
        better = a > fill
        fill = np.where(better, a, fill)
        core_t = np.where(better, 1 - d, core_t)
        along = np.where(better, u, along)
# bras qui s'enroulent vers l'intérieur (sens horaire en montant), le plus long fait la pointe en haut
for i, (st, r1, sp, w) in enumerate([(-0.2, 0.92, 1.15 * np.pi, 0.21), (1.05, 0.74, 1.0 * np.pi, 0.18), (2.3, 0.70, 0.95 * np.pi, 0.17), (3.55, 0.68, 0.95 * np.pi, 0.16), (4.8, 0.66, 0.9 * np.pi, 0.15)]):
    stroke(st, 0.2, r1, sp, w)
hole = np.clip((r - 0.15) / 0.03, 0, 1)
fill *= hole
mask = Image.fromarray((fill * 255).astype(np.uint8))
outl = np.asarray(mask.filter(ImageFilter.MaxFilter(91)).filter(ImageFilter.MinFilter(61)).filter(ImageFilter.MaxFilter(41)).filter(ImageFilter.GaussianBlur(6))) / 255.0
outl = np.clip((outl - 0.45) / 0.1 + 0.5, 0, 1)
# centre : disque sombre plein (le fond du tourbillon)
centre = np.clip((0.22 - r) / 0.01, 0, 1)
outl = np.maximum(outl, centre)
yellow = np.array([255, 222, 110.]); orange = np.array([246, 138, 32.]); red = np.array([214, 62, 20.])
c = np.clip(core_t, 0, 1)[..., None]; u = along[..., None]
col = orange + (yellow - orange) * np.clip(c * 1.6 - 0.2, 0, 1) * (1 - 0.6 * u)
col = col + (red - col) * np.clip(u * 1.3 - 0.45, 0, 1) * (1 - c)
light = np.clip(1.0 + 0.12 * (-X - Y), 0.85, 1.12)[..., None]
col = np.clip(col * light, 0, 255)
dark = np.array([34, 13, 5.])
rgb = dark * (1 - fill[..., None]) + col * fill[..., None]
alpha = np.maximum(outl, fill)
img = Image.fromarray(np.dstack([rgb, alpha * 255]).astype(np.uint8))
bb = img.split()[3].point(lambda v: 255 if v > 8 else 0).getbbox()
img = img.crop(bb); side = max(img.size) + 16
sq = Image.new("RGBA", (side, side), (0, 0, 0, 0)); sq.paste(img, ((side - img.size[0]) // 2, (side - img.size[1]) // 2))
out = sq.resize((256, 256), Image.LANCZOS).transpose(Image.FLIP_LEFT_RIGHT); import os
out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Saved", "UIIcons")
os.makedirs(out_dir, exist_ok=True)
out.save(os.path.join(out_dir, "T_UI_Resource_Flame.png"))
print(os.path.abspath(os.path.join(out_dir, "T_UI_Resource_Flame.png")))
