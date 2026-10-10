"""Captures hors viewport des clips d'animation candidats (casting des animations de Curffe).

Usage (execute_python_code) :
    import gen_anim_capture as gac
    gac.capture_set("Fireball", {"Gideon_A": "/Game/.../Primary_Attack_A_Medium", ...}, fps=30, angle="three_quarter")
Écrit .superpowers/sdd/Anim/<set>/<label>/f000.png... + meta.json (durée, fps), puis
Tools/Anim/make_casting_gif.py assemble les candidats côte à côte.
"""
import json
import os

import unreal

ROOT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), ".superpowers", "sdd", "Anim")


def capture_clip(anim_path, out_dir, fps=30, angle="three_quarter", size=320, max_len=3.0):
    S = unreal.AnimSequenceService
    length = S.get_animation_length(anim_path)
    if length <= 0:
        return {"error": "not found", "path": anim_path}
    os.makedirs(out_dir, exist_ok=True)
    n = int(min(length, max_len) * fps) + 1
    ok = 0
    for i in range(n):
        t = min(i / fps, length)
        r = S.capture_animation_pose(anim_path, t, os.path.join(out_dir, "f%03d.png" % i).replace("\\", "/"), angle, size, size)
        ok += 1 if (r and r.success) else 0
    meta = {"path": anim_path, "length": round(length, 3), "fps": fps, "frames": n, "ok": ok, "angle": angle}
    with open(os.path.join(out_dir, "meta.json"), "w") as f:
        json.dump(meta, f)
    return meta


def capture_set(set_name, clips, fps=30, angle="three_quarter", size=320, max_len=3.0):
    """clips : {label: anim_path}. Renvoie un résumé compact."""
    out = {}
    for label, path in clips.items():
        m = capture_clip(path, os.path.join(ROOT, set_name, angle, label), fps, angle, size, max_len)
        out[label] = m.get("length", m.get("error"))
    return out
