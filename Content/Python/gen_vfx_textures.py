"""
Génère les masques peints de M_VFX_Erode (Art Bible §7.4, Curffe Plan-Visuals Task E3) en PNG.

Utilisation (Python avec Pillow, hors éditeur) :
    python Content/Python/gen_vfx_textures.py [dossier_de_sortie]

Sortie par défaut : <projet>/Saved/VFXTextures/
  - T_VFX_Teardrop.png  (128 x 256) : R = flamme en goutte, pointe en haut
  - T_VFX_SoftRing.png  (256 x 256) : R = anneau
  - T_VFX_Burst.png     (256 x 256) : R = étoile à 6 branches
  Pour les trois : R = forme en 3 aplats (cœur 1.0, corps 0.66, bord 0.33, ~2 px d'anticrénelage),
  G = bruit de valeur raccordable de 64 px (déformation des UV), B = 0.

Import dans l'éditeur : compression TC_Masks, sRGB désactivé, mips actifs (le script d'import est
dans Plan-Visuals E3 ; ce fichier ne dépend pas d'Unreal).
"""

import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

SS = 4  # suréchantillonnage pour l'anticrénelage
STEPS = (0.33, 0.66, 1.0)  # bord, corps, cœur (Art Bible §7.4 : 2-3 aplats)
NOISE_TILE = 64
SEED = 1702


def project_dir():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def value_noise_tile(size=NOISE_TILE, seed=SEED):
    """Bruit de valeur raccordable (2 octaves, 4 et 8 cellules sur la tuile), valeurs 0-255."""
    rng = random.Random(seed)
    octaves = []
    for cells in (4, 8):
        octaves.append((cells, [[rng.random() for _ in range(cells)] for _ in range(cells)]))

    def smooth(t):
        return t * t * (3.0 - 2.0 * t)

    img = Image.new("L", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            total, weight = 0.0, 0.0
            for amp_index, (cells, grid) in enumerate(octaves):
                amp = 0.5 ** amp_index
                fx, fy = x * cells / size, y * cells / size
                x0, y0 = int(fx) % cells, int(fy) % cells
                x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
                tx, ty = smooth(fx - int(fx)), smooth(fy - int(fy))
                top = grid[y0][x0] * (1 - tx) + grid[y0][x1] * tx
                bottom = grid[y1][x0] * (1 - tx) + grid[y1][x1] * tx
                total += amp * (top * (1 - ty) + bottom * ty)
                weight += amp
            px[x, y] = int(round(255 * total / weight))
    return img


def tiled_noise(width, height):
    tile = value_noise_tile()
    out = Image.new("L", (width, height))
    for ty in range(0, height, NOISE_TILE):
        for tx in range(0, width, NOISE_TILE):
            out.paste(tile, (tx, ty))
    return out


def stepped_mask(width, height, draw_level):
    """Dessine les 3 aplats du plus grand (bord) au plus petit (cœur), suréchantillonné puis réduit."""
    big = Image.new("L", (width * SS, height * SS), 0)
    draw = ImageDraw.Draw(big)
    for level, value in enumerate(STEPS):
        draw_level(draw, level, int(round(255 * value)), width * SS, height * SS)
    small = big.resize((width, height), Image.LANCZOS)
    # ~2 px de bord doux au total (réduction + léger flou), Plan-Visuals E3
    return small.filter(ImageFilter.GaussianBlur(0.6))


def teardrop_points(cx, base_y, radius, height, n=96):
    """Goutte : cercle en bas (rayon radius, posé sur base_y), deux tangentes qui se rejoignent en pointe en haut.
    Coordonnées image (y vers le bas)."""
    cy = base_y - radius
    tip_y = base_y - height
    dist = max(cy - tip_y, radius + 1e-3)
    alpha = math.acos(radius / dist)  # angle entre centre->pointe et centre->point de tangence
    a0 = -math.pi / 2 + alpha  # tangence à droite
    sweep = 2 * math.pi - 2 * alpha  # par le bas jusqu'à la tangence à gauche
    pts = [(cx, tip_y)]
    for i in range(n + 1):
        a = a0 + sweep * i / n
        pts.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
    return pts


def draw_teardrop(draw, level, value, w, h):
    # Les flammes s'ancrent en bas : chaque aplat partage la même base, plus petit vers le cœur
    scale = (1.0, 0.74, 0.48)[level]
    radius = 0.40 * w * scale
    height = 0.92 * h * scale
    base_y = 0.97 * h
    draw.polygon(teardrop_points(w / 2, base_y, radius, height), fill=value)


def draw_ring(draw, level, value, w, h):
    # Anneau : bande 0.70-0.95 du rayon, cœur au milieu de la bande ; le trou reste à 0
    inner, outer = ((0.70, 0.95), (0.75, 0.91), (0.80, 0.86))[level]
    cx, cy, r = w / 2, h / 2, w / 2
    draw.ellipse((cx - outer * r, cy - outer * r, cx + outer * r, cy + outer * r), outline=value,
                 width=max(1, int(round((outer - inner) * r))))


def draw_burst(draw, level, value, w, h, points=6):
    scale = (1.0, 0.66, 0.36)[level]
    cx, cy, r = w / 2, h / 2, 0.95 * w / 2 * scale
    inner = 0.42 * r
    pts = []
    for i in range(points * 2):
        a = -math.pi / 2 + i * math.pi / points
        rad = r if i % 2 == 0 else inner
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    draw.polygon(pts, fill=value)


def make(name, width, height, drawer, out_dir):
    r = stepped_mask(width, height, drawer)
    g = tiled_noise(width, height)
    b = Image.new("L", (width, height), 0)
    img = Image.merge("RGB", (r, g, b))
    path = os.path.join(out_dir, name + ".png")
    img.save(path)
    hist = sorted(set(r.getdata()))
    print("%s %dx%d -> %s (R : %d valeurs, max %d)" % (name, width, height, path, len(hist), max(hist)))
    return path


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(project_dir(), "Saved", "VFXTextures")
    os.makedirs(out_dir, exist_ok=True)
    make("T_VFX_Teardrop", 128, 256, draw_teardrop, out_dir)
    make("T_VFX_SoftRing", 256, 256, draw_ring, out_dir)
    make("T_VFX_Burst", 256, 256, draw_burst, out_dir)


if __name__ == "__main__":
    main()
