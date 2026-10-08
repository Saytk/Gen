"""
Génère les icônes provisoires de Curffe et les glyphes d'interface (UI_Guidelines §2.11) en PNG.

Utilisation (Python avec Pillow, hors éditeur ou dans l'éditeur si Pillow y est installé) :
    python Content/Python/gen_ui_icons.py [dossier_de_sortie]

Sortie par défaut : <projet>/Saved/UIIcons/
  - T_UI_Ability_Curffe_{Primary,Secondary,Mobility,1,2,3,Ultimate,Pyroblast}.png  (256 px, RGBA ; 1 = Retour de flamme,
    2 = Pilier de flammes, 3 = Flamme vivante, Ultimate = Combustion, Pyroblast = LMB embrasé)
  - T_UI_Glyph_{LMB,RMB,Lock}.png                         (64 px, blanc, teinté en text.primary par le widget)
  - checks/ : niveaux de gris, flou gaussien 2 px, 32 px, planche contact et mesures (§2.11)

Style : aplats « peints » en 2-3 bandes, lumière unique en haut à gauche, palette du feu
(Art Bible §4.3 / §7.4 : cœur #FFF0C2, corps #F5B82E, bord/fumée #7A5230) sur un disque #20160F. Aucun texte.
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageOps

# Palette (Art Bible §7.4) et fond (bg.panel)
CORE = (0xFF, 0xF0, 0xC2, 255)
BODY = (0xF5, 0xB8, 0x2E, 255)
EDGE = (0x7A, 0x52, 0x30, 255)
BG = (0x20, 0x16, 0x0F, 255)
WHITE = (255, 255, 255, 255)

ICON_SIZE = 256
GLYPH_SIZE = 64
SS = 4  # suréchantillonnage pour l'anticrénelage

# Glyphes : 2 px de trait à 20 px d'affichage (§2.11) => 2 * 64 / 20 = 6.4 px à 64 px
GLYPH_STROKE = 6


def project_dir():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def canvas(size):
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def finish(img, size):
    return img.resize((size, size), Image.LANCZOS)


def s(v):
    """Coordonnée logique -> coordonnée suréchantillonnée."""
    return v * SS


def disc(d, cx, cy, r, col):
    d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=col)


def background(d):
    disc(d, 128, 128, 128, BG)


def lit_orb(d, cx, cy, r):
    """Orbe en 3 bandes plates : bord brun, corps ambré décalé vers la lumière, cœur pâle en haut à gauche."""
    disc(d, cx, cy, r, EDGE)
    disc(d, cx - r * 0.08, cy - r * 0.08, r * 0.86, BODY)
    disc(d, cx - r * 0.28, cy - r * 0.28, r * 0.42, CORE)


def tapered_tail(d, hx, hy, r, tx, ty, col):
    """Traînée effilée : tangente au disque (hx, hy, r), pointe en (tx, ty)."""
    ang = math.atan2(ty - hy, tx - hx)
    px, py = -math.sin(ang), math.cos(ang)
    pts = [(hx + px * r, hy + py * r), (tx, ty), (hx - px * r, hy - py * r)]
    d.polygon([(s(x), s(y)) for x, y in pts], fill=col)


def icon_primary():
    """Boule de feu : petite comète ronde, traînée effilée vers le bas à gauche."""
    img, d = canvas(ICON_SIZE)
    background(d)
    hx, hy, r = 156, 100, 40
    tapered_tail(d, hx, hy, r, 40, 216, EDGE)
    tapered_tail(d, hx - 2, hy + 2, r * 0.72, 62, 194, BODY)
    tapered_tail(d, hx - 6, hy + 6, r * 0.34, 92, 164, CORE)
    lit_orb(d, hx, hy, r)
    return finish(img, ICON_SIZE)


def flame_dot(d, cx, cy, r, ang):
    """Petite flamme en goutte, pointe tournée vers l'arrière de son orbite."""
    tx, ty = cx + math.cos(ang) * r * 2.4, cy + math.sin(ang) * r * 2.4
    tapered_tail(d, cx, cy, r, tx, ty, EDGE)
    disc(d, cx, cy, r, EDGE)
    disc(d, cx - r * 0.12, cy - r * 0.12, r * 0.78, BODY)
    disc(d, cx - r * 0.3, cy - r * 0.3, r * 0.34, CORE)


def icon_secondary():
    """Grande boule de feu : gros orbe et 3 flammèches en orbite (elle est « nourrie »)."""
    img, d = canvas(ICON_SIZE)
    background(d)
    cx, cy = 128, 128
    orbit = 90
    # Orbite discrète en brun pour lire la rotation
    d.ellipse([s(cx - orbit), s(cy - orbit), s(cx + orbit), s(cy + orbit)], outline=EDGE, width=s(4))
    lit_orb(d, cx, cy, 60)
    for deg in (-90, 30, 150):
        a = math.radians(deg)
        fx, fy = cx + math.cos(a) * orbit, cy + math.sin(a) * orbit
        # Sens horaire : la traînée part vers l'arrière (sens antihoraire)
        flame_dot(d, fx, fy, 18, a - math.pi / 2)
    return finish(img, ICON_SIZE)


def icon_mobility():
    """Saut du météore : trajectoire en arc et éclat d'atterrissage."""
    img, d = canvas(ICON_SIZE)
    background(d)
    # Parabole du décollage (bas gauche) à l'impact (bas droite), épaisseur croissante
    x0, x1, ground, apex = 38, 178, 186, 54
    steps = 48
    pts = []
    for i in range(steps + 1):
        t = i / steps
        x = x0 + (x1 - x0) * t
        y = ground - (ground - apex) * 4 * t * (1 - t)
        pts.append((x, y))
    for i in range(steps):
        t = i / steps
        w = 8 + 14 * t
        (ax, ay), (bx, by) = pts[i], pts[i + 1]
        d.line([s(ax), s(ay), s(bx), s(by)], fill=EDGE, width=int(s(w + 6)))
    for i in range(steps):
        t = i / steps
        w = 5 + 10 * t
        (ax, ay), (bx, by) = pts[i], pts[i + 1]
        d.line([s(ax), s(ay), s(bx), s(by)], fill=BODY, width=int(s(w)))
    # Sol : ellipse brune sous l'impact
    ix, iy = 182, 192
    d.ellipse([s(ix - 52), s(iy - 10), s(ix + 52), s(iy + 14)], fill=EDGE)
    # Éclat d'atterrissage : étoile à 8 branches, plus longues vers le haut
    spikes = []
    for k in range(16):
        a = math.radians(-90 + k * 22.5)
        up = max(0.0, -math.sin(a))
        r = (46 + 14 * up) if k % 2 == 0 else 16
        spikes.append((ix + math.cos(a) * r, iy - 4 + math.sin(a) * r * (0.62 + 0.38 * up)))
    d.polygon([(s(x), s(y)) for x, y in spikes], fill=BODY)
    disc(d, ix - 4, iy - 10, 15, CORE)
    return finish(img, ICON_SIZE)


def thick_arc(d, cx, cy, r_out, r_in, a0, a1, col, steps=40):
    """Bande en arc de cercle (angles en degrés, sens horaire écran), bouts plats."""
    outer = [(cx + math.cos(math.radians(a0 + (a1 - a0) * i / steps)) * r_out,
              cy + math.sin(math.radians(a0 + (a1 - a0) * i / steps)) * r_out) for i in range(steps + 1)]
    inner = [(cx + math.cos(math.radians(a1 - (a1 - a0) * i / steps)) * r_in,
              cy + math.sin(math.radians(a1 - (a1 - a0) * i / steps)) * r_in) for i in range(steps + 1)]
    d.polygon([(s(x), s(y)) for x, y in outer + inner], fill=col)


def icon_backfire():
    """Retour de flamme : garde levée (arc de bouclier tourné vers le haut à droite), une petite boule de feu s'y brise."""
    img, d = canvas(ICON_SIZE)
    background(d)
    # Garde : arc épais centré en bas à gauche, convexe vers le haut à droite (3 bandes, lumière en haut à gauche)
    gx, gy = 92, 168
    thick_arc(d, gx, gy, 112, 78, -135, 45, EDGE)
    thick_arc(d, gx, gy, 106, 84, -128, 38, BODY)
    thick_arc(d, gx, gy, 106, 96, -122, -40, CORE)
    # Boule de feu qui s'écrase sur la garde (en haut à droite), éclats rejetés vers l'extérieur
    hx, hy, r = 196, 66, 20
    spikes = []
    for k in range(12):
        a = math.radians(-135 + k * 30)
        rr = 40 if k % 2 == 0 else 22
        spikes.append((hx + math.cos(a) * rr, hy + math.sin(a) * rr))
    d.polygon([(s(x), s(y)) for x, y in spikes], fill=EDGE)
    lit_orb(d, hx, hy, r)
    return finish(img, ICON_SIZE)


def icon_flame_pillar():
    """Pilier de flammes : haute colonne de feu verticale qui jaillit d'une ellipse au sol."""
    img, d = canvas(ICON_SIZE)
    background(d)
    # Sol : ellipse brune, bord ambré côté lumière
    d.ellipse([s(40), s(176), s(216), s(226)], fill=EDGE)
    d.ellipse([s(52), s(180), s(204), s(216)], fill=BODY)
    d.ellipse([s(70), s(186), s(170), s(208)], fill=CORE)

    def column(half_w, top, bottom, col, dx=0.0):
        # Colonne effilée vers le haut avec deux ondulations latérales
        pts_l, pts_r = [], []
        steps = 24
        for i in range(steps + 1):
            t = i / steps  # 0 = bas, 1 = haut
            y = bottom - (bottom - top) * t
            w = half_w * (1.0 - 0.85 * t ** 1.4) * (1.0 + 0.12 * math.sin(t * math.pi * 3))
            cx = 128 + dx + 6 * math.sin(t * math.pi * 2)
            pts_l.append((cx - w, y))
            pts_r.append((cx + w, y))
        d.polygon([(s(x), s(y)) for x, y in pts_l + list(reversed(pts_r))], fill=col)

    column(44, 18, 200, EDGE)
    column(32, 34, 196, BODY, dx=-3)
    column(14, 70, 190, CORE, dx=-8)
    return finish(img, ICON_SIZE)


def flame_tongue(d, cx, base_y, half_w, height, col, sway=0.0, steps=24):
    """Langue de flamme : base arrondie, effilée vers le haut avec un léger déhanché (sway en px)."""
    pts_l, pts_r = [], []
    for i in range(steps + 1):
        t = i / steps  # 0 = base, 1 = pointe
        y = base_y - height * t
        w = half_w * math.sin(math.pi * (0.5 + 0.5 * t)) ** 0.8 * (1.0 - t) ** 0.35
        x = cx + sway * math.sin(t * math.pi) * t
        pts_l.append((x - w, y))
        pts_r.append((x + w, y))
    d.polygon([(s(x), s(y)) for x, y in pts_l + list(reversed(pts_r))], fill=col)


def icon_living_flame():
    """Flamme vivante : silhouette debout faite de feu, bras grands ouverts (en Y), anneau d'éclat évidé à sa base."""
    img, d = canvas(ICON_SIZE)
    background(d)
    cx, gy = 128, 200
    # Anneau d'éclat au sol : ellipse évidée (un anneau, jamais un disque)
    d.ellipse([s(cx - 92), s(gy - 24), s(cx + 92), s(gy + 24)], outline=EDGE, width=s(10))
    d.ellipse([s(cx - 88), s(gy - 20), s(cx + 88), s(gy + 20)], outline=BODY, width=s(4))
    # Bras levés en Y : deux langues de flamme obliques, pointes vers le haut à gauche et à droite
    for side in (-1, 1):
        for col, w, tip in ((EDGE, 17, 0), (BODY, 11, 6)):
            pts = [(cx + side * 6, 150 - w), (cx + side * (98 - tip), 38 + tip), (cx + side * 6 + side * w, 150 + w * 0.2)]
            d.polygon([(s(x), s(y)) for x, y in pts], fill=col)
    # Jambes : deux langues vers le sol
    for side in (-1, 1):
        pts = [(cx - 12, 150), (cx + 12, 150), (cx + side * 34, gy - 2)]
        d.polygon([(s(x), s(y)) for x, y in pts], fill=EDGE)
    # Torse : petite flamme
    flame_tongue(d, cx, 166, 20, 82, EDGE)
    flame_tongue(d, cx - 2, 160, 13, 66, BODY)
    # Tête : orbe pâle
    disc(d, cx, 72, 17, EDGE)
    disc(d, cx - 2, 70, 13, BODY)
    disc(d, cx - 5, 66, 6, CORE)
    return finish(img, ICON_SIZE)


def icon_combustion():
    """Combustion : éruption radiale, couronne de langues de flamme séparées du cœur éclatant par un vide sombre."""
    img, d = canvas(ICON_SIZE)
    background(d)
    cx, cy = 128, 128
    n = 8
    for k in range(n):
        a = math.radians(-90 + k * 360 / n)
        ca, sa = math.cos(a), math.sin(a)
        px, py = -sa, ca
        # Langue : base large à r0, pointe à r1, en 2 bandes
        for col, r0, r1, w in ((EDGE, 50, 122, 15), (BODY, 56, 108, 9)):
            pts = [(cx + ca * r0 + px * w, cy + sa * r0 + py * w), (cx + ca * r1, cy + sa * r1), (cx + ca * r0 - px * w, cy + sa * r0 - py * w)]
            d.polygon([(s(x), s(y)) for x, y in pts], fill=col)
    disc(d, cx, cy, 30, BODY)
    disc(d, cx - 3, cy - 3, 22, CORE)
    disc(d, cx - 8, cy - 8, 9, WHITE)
    return finish(img, ICON_SIZE)


def icon_pyroblast():
    """Pyroblast : grosse comète qui monte, tête nettement plus grande que celle de la boule de feu (r 64 contre 40),
    traînée courte et épaisse vers le bas. Le vol vertical la distingue de la diagonale de la boule de feu à 32 px."""
    img, d = canvas(ICON_SIZE)
    background(d)
    hx, hy, r = 176, 100, 64
    tx, ty = 128, 236
    tapered_tail(d, hx, hy, r * 0.95, tx, ty, EDGE)
    tapered_tail(d, hx - 4, hy + 4, r * 0.66, tx + (hx - tx) * 0.18, ty + (hy - ty) * 0.18, BODY)
    tapered_tail(d, hx - 8, hy + 8, r * 0.32, tx + (hx - tx) * 0.42, ty + (hy - ty) * 0.42, CORE)
    lit_orb(d, hx, hy, r)
    return finish(img, ICON_SIZE)


def rounded_rect(d, box, r, **kw):
    x0, y0, x1, y1 = box
    d.rounded_rectangle([s(x0), s(y0), s(x1), s(y1)], radius=s(r), **kw)


def glyph_mouse(left):
    """Souris au trait blanc (2 px à 20 px), bouton gauche ou droit plein."""
    img, d = canvas(GLYPH_SIZE)
    body = (13, 3, 51, 61)
    split_y = 27
    w = GLYPH_STROKE
    # Bouton plein : intersection du corps et du quart haut gauche/droit
    fill_mask, fd = canvas(GLYPH_SIZE)
    rounded_rect(fd, body, 18, fill=WHITE)
    cx = (body[0] + body[2]) / 2
    clip = Image.new("L", fill_mask.size, 0)
    cd = ImageDraw.Draw(clip)
    if left:
        cd.rectangle([0, 0, s(cx), s(split_y)], fill=255)
    else:
        cd.rectangle([s(cx), 0, s(GLYPH_SIZE), s(split_y)], fill=255)
    img.paste(WHITE, (0, 0), Image.composite(fill_mask, Image.new("RGBA", fill_mask.size), clip).split()[3])
    rounded_rect(d, body, 18, outline=WHITE, width=s(w))
    d.line([s(body[0] + 2), s(split_y), s(body[2] - 2), s(split_y)], fill=WHITE, width=s(w))
    d.line([s(cx), s(body[1] + 2), s(cx), s(split_y)], fill=WHITE, width=s(w))
    return finish(img, GLYPH_SIZE)


def glyph_lock():
    """Cadenas : anse au trait, corps plein percé d'un trou de serrure."""
    img, d = canvas(GLYPH_SIZE)
    w = GLYPH_STROKE
    d.arc([s(19), s(5), s(45), s(37)], start=180, end=360, fill=WHITE, width=s(w))
    d.line([s(19 + w / 2), s(21), s(19 + w / 2), s(30)], fill=WHITE, width=s(w))
    d.line([s(45 - w / 2), s(21), s(45 - w / 2), s(30)], fill=WHITE, width=s(w))
    rounded_rect(d, (11, 28, 53, 60), 5, fill=WHITE)
    # Trou de serrure (transparent)
    hole = (0, 0, 0, 0)
    disc(d, 32, 40, 5, hole)
    d.polygon([(s(29.5), s(42)), (s(34.5), s(42)), (s(35.5), s(52)), (s(28.5), s(52))], fill=hole)
    return finish(img, GLYPH_SIZE)


# --- Contrôles §2.11 -------------------------------------------------------------------------

def grey(img):
    """Luminance Rec.709 sur fond noir (alpha prémultiplié)."""
    rgb = Image.new("RGB", img.size, (0, 0, 0))
    rgb.paste(img, (0, 0), img)
    return ImageOps.grayscale(rgb)


def circle_pixels(size):
    c = (size - 1) / 2
    r2 = (size / 2) ** 2
    return [(x, y) for y in range(size) for x in range(size) if (x - c) ** 2 + (y - c) ** 2 <= r2]


def mean_lum(g):
    px = g.load()
    pts = circle_pixels(g.size[0])
    return sum(px[x, y] for x, y in pts) / len(pts)


def silhouette(g, threshold):
    """Masque binaire de la figure (tout ce qui est nettement plus clair que le fond)."""
    px = g.load()
    w, h = g.size
    return [[1 if px[x, y] > threshold else 0 for x in range(w)] for y in range(h)]


def iou(a, b):
    inter = sum(1 for ra, rb in zip(a, b) for va, vb in zip(ra, rb) if va and vb)
    union = sum(1 for ra, rb in zip(a, b) for va, vb in zip(ra, rb) if va or vb)
    return inter / union if union else 1.0


def run_checks(icons, glyphs, out_dir):
    checks = os.path.join(out_dir, "checks")
    os.makedirs(checks, exist_ok=True)
    bg_lum = 0.2126 * BG[0] + 0.7152 * BG[1] + 0.0722 * BG[2]
    threshold = bg_lum + 40

    rows = []
    stats = {}
    for name, img in icons.items():
        g = grey(img)
        disp = img.resize((64, 64), Image.LANCZOS)  # taille d'affichage (§2.11)
        blur = disp.filter(ImageFilter.GaussianBlur(2))
        small = img.resize((32, 32), Image.LANCZOS)
        g.save(os.path.join(checks, name + "_grey.png"))
        blur.save(os.path.join(checks, name + "_blur2px.png"))
        small.save(os.path.join(checks, name + "_32px.png"))
        stats[name] = {
            "mean_lum_grey": mean_lum(g),
            "mean_lum_blur": mean_lum(grey(blur)),
            "sil32": silhouette(grey(small), threshold),
            "sil_blur": silhouette(grey(blur), threshold),
        }
        rows.append((img, g, blur, small))

    # Planche contact : original 128 | gris 128 | flou 2 px (64 -> 128) | 32 px (x1 et x4)
    cell = 128
    pad = 8
    cols = 5
    sheet_w = pad + cols * (cell + pad)
    sheet_h = pad + (len(rows) + 1) * (cell + pad)
    sheet = Image.new("RGB", (sheet_w, sheet_h), (0x8C, 0x8C, 0x8C))  # ref.floorMax
    for r, (img, g, blur, small) in enumerate(rows):
        y = pad + r * (cell + pad)
        cells = [
            img.resize((cell, cell), Image.LANCZOS),
            g.convert("RGBA").resize((cell, cell), Image.LANCZOS),
            blur.resize((cell, cell), Image.NEAREST),
            None,
            small.resize((cell, cell), Image.NEAREST),
        ]
        for c, im in enumerate(cells):
            x = pad + c * (cell + pad)
            if im is None:
                sheet.paste(small, (x + (cell - 32) // 2, y + (cell - 32) // 2), small)
            else:
                sheet.paste(im, (x, y), im if im.mode == "RGBA" else None)
    # Dernière ligne : glyphes à 64 px et à 20 px (taille d'affichage)
    y = pad + len(rows) * (cell + pad)
    for c, (name, gl) in enumerate(glyphs.items()):
        x = pad + c * (cell + pad)
        sheet.paste(gl, (x, y), gl)
        g20 = gl.resize((20, 20), Image.LANCZOS)
        sheet.paste(g20, (x + 80, y + 22), g20)
    sheet_path = os.path.join(checks, "contact_sheet.png")
    sheet.save(sheet_path)

    lines = ["Contrôles §2.11 (luminance moyenne 0-255 dans le disque ; IoU des silhouettes, plus bas = plus distinct)"]
    for name, st in stats.items():
        lines.append(f"  {name}: lum gris={st['mean_lum_grey']:.1f}  lum flou 2px@64={st['mean_lum_blur']:.1f}")
    names = list(stats)
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            a, b = stats[names[i]], stats[names[j]]
            lines.append(f"  {names[i]} vs {names[j]}: IoU 32px={iou(a['sil32'], b['sil32']):.2f}  IoU flou={iou(a['sil_blur'], b['sil_blur']):.2f}"
                         f"  écart lum={abs(a['mean_lum_grey'] - b['mean_lum_grey']):.1f}")
    lines.append("  planche : " + sheet_path)
    report = "\n".join(lines)
    with open(os.path.join(checks, "checks.txt"), "w", encoding="utf-8") as f:
        f.write(report + "\n")
    return report


def main(out_dir=None):
    out_dir = out_dir or os.path.join(project_dir(), "Saved", "UIIcons")
    os.makedirs(out_dir, exist_ok=True)
    icons = {
        "T_UI_Ability_Curffe_Primary": icon_primary(),
        "T_UI_Ability_Curffe_Secondary": icon_secondary(),
        "T_UI_Ability_Curffe_Mobility": icon_mobility(),
        "T_UI_Ability_Curffe_1": icon_backfire(),
        "T_UI_Ability_Curffe_2": icon_flame_pillar(),
        "T_UI_Ability_Curffe_3": icon_living_flame(),
        "T_UI_Ability_Curffe_Ultimate": icon_combustion(),
        "T_UI_Ability_Curffe_Pyroblast": icon_pyroblast(),
    }
    glyphs = {
        "T_UI_Glyph_LMB": glyph_mouse(left=True),
        "T_UI_Glyph_RMB": glyph_mouse(left=False),
        "T_UI_Glyph_Lock": glyph_lock(),
    }
    for name, img in {**icons, **glyphs}.items():
        img.save(os.path.join(out_dir, name + ".png"))
    print(run_checks(icons, glyphs, out_dir))
    return out_dir


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
