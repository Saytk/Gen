"""Sonde de pose en PIE : écart entre les pieds (espace composant) et variables de l'ABP à chaque image.

    import gen_anim_probe as pr; pr.start("nom", seconds=2.0, move=(1, 0))   puis lire Saved/AnimProbe/<nom>.json
Sert à vérifier en chiffres ce que fait vraiment la locomotion (ex. pieds collés alors que le clip fait 1,8 m d'écart).
"""
import json
import os

import unreal

import gen_anim_pie as gap

OUT = os.path.join(unreal.Paths.project_saved_dir(), "AnimProbe")
_p = {}


def _tick(dt):
    _p["t"] += dt
    try:
        mesh = _p["mesh"]
        tr = lambda b: mesh.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        fl, fr, pv = tr("foot_l"), tr("foot_r"), tr("pelvis")
        row = {"t": round(_p["t"], 3), "dx": round(fl.x - fr.x), "dy": round(fl.y - fr.y), "pz": round(pv.z)}
        ai = mesh.get_anim_instance()
        for v in ("GroundSpeed", "Direction"):
            try:
                row[v] = round(float(ai.get_editor_property(v)))
            except Exception:
                pass
        _p["rows"].append(row)
    except Exception as e:
        _p["rows"].append({"err": str(e)[:80]})
    if _p["t"] >= _p["seconds"]:
        unreal.unregister_slate_post_tick_callback(_p["h"])
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, _p["name"] + ".json"), "w") as f:
            json.dump(_p["rows"], f)


def start(name, seconds=2.0, move=None, single_anim=None):
    pawn = unreal.GameplayStatics.get_player_pawn(gap._world(), 0)
    mesh = pawn.get_editor_property("mesh")
    if single_anim:
        mesh.play_animation(unreal.load_asset(single_anim), True)
    _p.update(name=name, seconds=seconds, t=0.0, rows=[], mesh=mesh)
    if move:
        gap.move(move[0], move[1], seconds, 1.0)
    _p["h"] = unreal.register_slate_post_tick_callback(_tick)
    return os.path.join(OUT, name + ".json")
