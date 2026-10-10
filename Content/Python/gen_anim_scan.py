"""Recherche d'un geste dans toute la banque Paragon par le mouvement, pas par le nom.

    import gen_anim_scan as sc; sc.start("raise_slam")   puis sc.status() jusqu'à "done", résultats dans
    Saved/AnimScan/<motif>.json (triés par score). Tourne par petits lots sur le tick de l'éditeur (pas de timeout MCP).
Motifs (MOTIFS) : fonctions (poses par image) -> (score, détails) ou None. Positions en espace composant (Paragon :
haut = +Z). Né le 2026-10-09 : « aucun perso Paragon n'a un sort qui lève quelque chose puis le rabat ? »
"""
import json
import os
import re

import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "AnimScan")
SKIP = re.compile(r"(Jog|Walk|Run|Sprint|Idle|Turn|_AO_|Additive|Aim|Jump|Fall|Land|Death|Dead|HitReact|Hit_|Stun|Knock|"
                  r"Recall|Travel|Lean|Pose|Pivot|Start|Stop|Strafe|Emote_Dance|Mirror|LSA|MSA|Flinch|Level)", re.I)
FLOATING = ("Muriel", "Fey")
_s = {"handle": None}


def _raise_slam(frames):
    """Les deux mains au-dessus de la tête, puis redescente rapide sous la poitrine ; bassin debout."""
    best = None
    head = [f["head"] for f in frames]
    for i, f in enumerate(frames):
        hi = min(f["hand_r"], f["hand_l"])
        if hi < head[i] + 5:
            continue
        for j in range(i + 1, min(i + 10, len(frames))):  # descente en moins de ~0,6 s (images de 2)
            g = frames[j]
            lo = max(g["hand_r"], g["hand_l"])
            drop = hi - lo
            dt = (j - i) * 2 / 30.0
            if lo < g["spine"] and drop > 60:
                speed = drop / 100 / dt
                pelvis_drop = frames[0]["pelvis"] - min(x["pelvis"] for x in frames)
                score = speed - max(pelvis_drop - 25, 0) * 0.1
                if best is None or score > best[0]:
                    best = (round(score, 2), {"up_f": i * 2, "down_f": j * 2, "drop_cm": round(drop),
                                              "speed_ms": round(speed, 1), "pelvis_drop": round(pelvis_drop)})
                break
    return best


MOTIFS = {"raise_slam": _raise_slam}


def _clips():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    out = []
    for a in reg.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "AnimSequence"), False):
        p = str(a.package_name)
        if p.startswith("/Game/Paragon") and not SKIP.search(str(a.asset_name)):
            hero = p.split("/")[2].replace("Paragon", "")
            if hero not in FLOATING:
                out.append(p)
    return sorted(out)


def _frames(path):
    S = unreal.AnimSequenceService
    n = int(S.get_animation_length(path) * 30)
    rows = []
    for f in range(0, n + 1, 2):
        pose = {str(b.bone_name): b.transform.translation for b in S.get_pose_at_frame(path, f, True)}
        if "hand_r" not in pose or "hand_l" not in pose:
            return None
        sp = pose.get("spine_03") or pose.get("spine_02")
        rows.append({"hand_r": pose["hand_r"].z, "hand_l": pose["hand_l"].z, "head": pose.get("head", sp).z,
                     "spine": sp.z if sp else 120, "pelvis": pose.get("pelvis", sp).z})
    return rows


def _tick(_dt):
    if not _s["todo"]:
        unreal.unregister_slate_post_tick_callback(_s["handle"])
        _s["handle"] = None
        res = sorted(_s["hits"], key=lambda r: -r["score"])
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, _s["motif"] + ".json"), "w") as f:
            json.dump(res, f, indent=1)
        _s["state"] = "done"
        return
    for _ in range(6):  # quelques clips par tick
        if not _s["todo"]:
            break
        p = _s["todo"].pop()
        try:
            fr = _frames(p)
            hit = MOTIFS[_s["motif"]](fr) if fr else None
            if hit:
                _s["hits"].append({"path": p, "score": hit[0], **hit[1]})
        except Exception as e:  # clip illisible : ignoré
            _s["errors"] += 1
        _s["done"] += 1


def start(motif="raise_slam"):
    if _s.get("handle"):
        unreal.unregister_slate_post_tick_callback(_s["handle"])
    _s.update(motif=motif, todo=_clips(), hits=[], done=0, errors=0, state="running")
    _s["total"] = len(_s["todo"])
    _s["handle"] = unreal.register_slate_post_tick_callback(_tick)
    return _s["total"]


def status():
    return {k: _s.get(k) for k in ("state", "done", "total", "errors")} | {"hits": len(_s.get("hits", []))}
