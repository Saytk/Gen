"""Montages de sorts depuis les clips choisis au casting (Tools/Anim/sets/<Sort>.json, clé "montages").

Côté éditeur, appelé par Tools/Anim/anim_casting.py --apply / --capture-montages :
    retarget(["Gideon:Primary_Attack_A_Medium"])     clip Paragon -> mannequin (Animations/Paragon/AS_<Qui>_<Nom>)
    apply_montage("/Game/.../AM_Fireball_Charge", [{"src": "Gideon:Primary_Attack_A_Medium", "f": [0, 4], "duration": 0.367}])
    capture_sequence(out_dir, ["/Game/.../AM_Fireball_Charge", ".../AM_Fireball_Cast"])   images du montage tel qu'il joue
Segment : "src" ("Gideon:<clip>", "Serath:<clip>" ou chemin /Game complet), plage "f" (images à 30 i/s) ou "t" (secondes),
et "duration" (durée voulue dans le montage) ou "rate". Les sections, notifies, slot et blends du montage sont gardés.
"""
import json
import os

import unreal

CURFFE_ANIMS = "/Game/Gen/Champions/Curffe/Animations"
PARAGON_OUT = CURFFE_ANIMS + "/Paragon"
LOCAL = PARAGON_OUT + "/_Local"
SOURCES = {
    "Gideon": ("/Game/ParagonGideon/Characters/Heroes/Gideon/Animations/", LOCAL + "/RTG_Gideon_To_Manny"),
    "Serath": ("/Game/ParagonSerath/Characters/Heroes/Serath/Animations/", LOCAL + "/RTG_Serath_To_Manny"),
}
FPS = 30.0


def resolve(src):
    """'Gideon:Clip' -> chemin du clip retargeté ; un chemin /Game est rendu tel quel."""
    if src.startswith("/Game"):
        return src
    who, name = src.split(":", 1)
    return f"{PARAGON_OUT}/AS_{who}_{name}"


def retarget(srcs):
    """Retarget les clips Paragon manquants (déjà faits : ignorés). Renvoie {src: chemin}."""
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("retarget : arrêter le PIE d'abord (le batch échoue sans erreur pendant le PIE)")
    out = {}
    for src in srcs:
        if src.startswith("/Game"):
            out[src] = src
            continue
        dst = resolve(src)
        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            out[src] = dst
            continue
        who, name = src.split(":", 1)
        folder, rtg_path = SOURCES[who]
        rtg = unreal.load_asset(rtg_path)
        ctrl = unreal.IKRetargeterController.get_controller(rtg)
        src_mesh = ctrl.get_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE)
        tgt_mesh = ctrl.get_preview_mesh(unreal.RetargetSourceOrTarget.TARGET)
        data = unreal.EditorAssetLibrary.find_asset_data(folder + name)
        made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            [data], src_mesh, tgt_mesh, rtg, prefix=f"AS_{who}_", target_path=PARAGON_OUT,
            include_referenced_assets=False, overwrite_existing_files=False)
        out[src] = str(made[0].package_name) if made else "FAILED"
        if made:
            sync_ik_bones([out[src]])
    return out


def _range(seg):
    if "f" in seg:
        return seg["f"][0] / FPS, seg["f"][1] / FPS
    return seg["t"][0], seg["t"][1]


def apply_montage(montage, segments, sections=None):
    """Remplace les segments de la piste 0 ; garde notifies, slot, blends, et les sections existantes.
    sections (optionnel) : [[nom, début, suivante ou "" ou nom lui-même pour boucler], ...] crée/déplace/relie.
    Forme dict : {"segments", "sections", "slot" ("DefaultSlot" = corps entier même en marchant, "UpperBody" = haut du
    corps en marchant), "blend_in", "blend_out" (s, mode d'inertialisation gardé)} ; absents = inchangés."""
    M = unreal.AnimMontageService
    opts = {}
    if isinstance(segments, dict):
        opts = segments
        segments, sections = segments["segments"], segments.get("sections")
    if opts.get("slot"):
        M.set_slot_name(montage, 0, opts["slot"])
    if opts.get("blend_in") is not None:
        M.set_blend_in(montage, opts["blend_in"])
    if opts.get("blend_out") is not None:
        M.set_blend_out(montage, opts["blend_out"])
    old = list(M.list_anim_segments(montage, 0))
    old_len = M.get_montage_length(montage)
    cursor = 0.0
    added = []
    for seg in segments:
        anim = resolve(seg["src"])
        a, b = _range(seg)
        # RateScale du clip (certains clips Paragon : 1.2) multiplie la vitesse du segment : on le compense
        rate = (seg.get("rate") or (b - a) / seg["duration"]) / _rate_scale(anim)
        i = M.add_anim_segment(montage, 0, anim, cursor + old_len + 10.0, rate)  # au-delà des anciens, déplacé plus bas
        M.set_segment_start_position(montage, 0, i, a)
        M.set_segment_end_position(montage, 0, i, b)
        M.set_segment_play_rate(montage, 0, i, rate)
        added.append((i, cursor))
        cursor += (b - a) / (rate * _rate_scale(anim))
    for i in reversed(range(len(old))):
        M.remove_anim_segment(montage, 0, i)
    for n, (_, start) in enumerate(added):
        M.set_segment_start_time(montage, 0, n, start)
    if sections:
        have = {str(s.section_name) for s in M.list_sections(montage)}
        for name, start, nxt in sections:
            if name in have:
                M.set_section_start_time(montage, name, start)
            else:
                M.add_section(montage, name, start)
        for name, _, nxt in sections:
            if nxt:
                M.set_next_section(montage, name, nxt)
            else:
                M.clear_section_link(montage, name)
    asset = unreal.load_asset(montage)
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    return {"len": round(M.get_montage_length(montage), 3), "old_len": round(old_len, 3),
            "sections": [(str(s.section_name), round(s.start_time, 3)) for s in M.list_sections(montage)]}


def analyse(src, bones=("hand_r", "hand_l"), ref="spine_05", every=1):
    """Par image : position des mains par rapport au torse (cm, mannequin : avant = +Y, haut = +Z) et vitesse.
    Sert à trouver l'image du lâcher (pic de vitesse / d'extension) pour couper Charge et Cast."""
    path = resolve(src)
    S = unreal.AnimSequenceService
    n = int(round(S.get_animation_length(path) * FPS)) + 1
    rows, prev = [], {}
    for f in range(0, n, every):
        pose = {str(b.bone_name): b.transform.translation for b in S.get_pose_at_frame(path, f, True)}
        row = [f]
        for bn in bones:
            v = pose[bn] - pose[ref]
            spd = 0 if bn not in prev else round((v - prev[bn]).length() * FPS / 100, 1)  # m/s
            prev[bn] = v
            row.append([round(v.x), round(v.y), round(v.z), spd])
        rows.append(row)
    return rows


def _rate_scale(anim_path):
    return unreal.load_asset(anim_path).get_editor_property("rate_scale") or 1.0


def _segments(montage):
    segs = []
    for s in unreal.AnimMontageService.list_anim_segments(montage, 0):
        anim = str(s.anim_sequence_path).split(".")[0]
        segs.append((s.start_time, s.duration, anim, s.anim_start_pos, s.play_rate * _rate_scale(anim)))
    return sorted(segs)


def capture_sequence(out_dir, montages, fps=30, angle="three_quarter", size=320):
    """Images du montage (ou de plusieurs à la suite) tel qu'il joue à vitesse 1 : temps du montage -> pose du clip source."""
    S = unreal.AnimSequenceService
    os.makedirs(out_dir, exist_ok=True)
    timeline, offset = [], 0.0
    for m in montages:
        timeline.extend((t + offset, d, a, p, r) for t, d, a, p, r in _segments(m))
        offset = timeline[-1][0] + timeline[-1][1]
    total = offset
    n = int(total * fps) + 1
    for k in range(n):
        t = min(k / fps, total - 1e-4)
        seg = next(s for s in timeline if s[0] <= t < s[0] + s[1] + 1e-6)
        anim_t = seg[3] + (t - seg[0]) * seg[4]
        S.capture_animation_pose(seg[2], anim_t, os.path.join(out_dir, "f%03d.png" % k).replace("\\", "/"), angle, size, size)
    meta = {"montages": montages, "length": round(total, 3), "fps": fps, "frames": n, "angle": angle}
    with open(os.path.join(out_dir, "meta.json"), "w") as f:
        json.dump(meta, f)
    return meta["length"]


# Sources locales non versionnées : rien de commité ne doit y pointer
LEAK_PREFIXES = ("/Game/Paragon", "/Game/Characters/UEFN_Mannequin", "/Game/Characters/Heroes", "/Game/Blueprints/AnimNotifies", "/Game/Misc/")


def check_leaks(montages):
    """Avant un commit : clips Paragon utilisés par ces montages, et toute référence vers les packs Paragon ou _Local.
    Rescanne d'abord les fichiers (le registre garde sinon les dépendances d'un doublon non sauvé)."""
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    to_file = lambda p: content + p[len("/Game/"):] + ".uasset"
    ar.scan_files_synchronous([to_file(m) for m in montages], True)
    clips = sorted({str(d) for m in montages for d in (ar.get_dependencies(m, opt) or [])
                    if str(d).startswith((PARAGON_OUT + "/", GASP_OUT + "/", CURFFE_ANIMS + "/Lyra/"))})
    ar.scan_files_synchronous([to_file(c) for c in clips], True)
    bad = [(p, str(d)) for p in clips + list(montages) for d in (ar.get_dependencies(p, opt) or [])
           if str(d).startswith(LEAK_PREFIXES) or "/_Local" in str(d)]
    return {"clips": clips, "bad": bad}


# --- GASP (Game Animation Sample) : boucles de locomotion -> mannequin de Gen -------------------------------------
GASP_SRC = "/Game/Characters/UEFN_Mannequin"
GASP_OUT = CURFFE_ANIMS + "/GASP"
RTG_GASP = LOCAL + "/RTG_UEFN_To_Manny"  # ancien, construit à la main : bassin trop bas (genoux pliés), remplacé
RTG_GASP_EPIC = "/Game/Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin"  # copié de GASP (import_gasp.py)
GEN_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"


def gasp_epic_retargeter():
    """Retargeter d'Epic (GASP), cible repointée sur le mannequin de Gen (même squelette que le UE5 Manny de GASP)."""
    rtg = unreal.load_asset(RTG_GASP_EPIC)
    ik = unreal.load_asset("/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget")
    mesh = unreal.load_asset(GEN_MESH)
    ikc = unreal.IKRigController.get_controller(ik)
    if ikc.get_skeletal_mesh() != mesh:
        ikc.set_skeletal_mesh(mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(ik, False)
    c = unreal.IKRetargeterController.get_controller(rtg)
    if c.get_preview_mesh(unreal.RetargetSourceOrTarget.TARGET) != mesh:
        c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(rtg, False)
    return rtg


def gasp_retargeter():
    """Crée (une fois) RTG_UEFN_To_Manny dans _Local : IK_UEFN_Mannequin (GASP) -> IK_Manny, chaînes appariées par nom."""
    if unreal.EditorAssetLibrary.does_asset_exist(RTG_GASP):
        return unreal.load_asset(RTG_GASP)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    rtg = tools.create_asset("RTG_UEFN_To_Manny", LOCAL, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, unreal.load_asset(GASP_SRC + "/Rigs/IK_UEFN_Mannequin"))
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset(LOCAL + "/IK_Manny"))
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, unreal.load_asset(GASP_SRC + "/Meshes/SKM_UEFN_Mannequin"))
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"))
    # UE 5.6+ : un retargeter ne fait rien sans pile d'opérations (Pelvis Motion, FK Chains, IK, Root Motion...)
    c.add_default_ops()
    src_ik = unreal.load_asset(GASP_SRC + "/Rigs/IK_UEFN_Mannequin")
    tgt_ik = unreal.load_asset(LOCAL + "/IK_Manny")
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src_ik)
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, tgt_ik)
    for i in range(c.get_num_retarget_ops()):
        c.run_op_initial_setup(i)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    unreal.EditorAssetLibrary.save_loaded_asset(rtg, False)
    return rtg


def retarget_gasp(names, overwrite=False):
    """names : chemins relatifs à UEFN_Mannequin/Animations sans extension (ex. 'Run/M_Neutral_Run_Loop_F').
    Sortie : Animations/GASP/AS_GASP_<nom>. Déjà faits : ignorés. Arrêter le PIE avant."""
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("retarget_gasp : arrêter le PIE d'abord")
    rtg = gasp_epic_retargeter()
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    out, todo = {}, []
    for n in names:
        dst = GASP_OUT + "/AS_GASP_" + n.split("/")[-1]
        if unreal.EditorAssetLibrary.does_asset_exist(dst) and not overwrite:
            out[n] = dst
        else:
            todo.append(unreal.EditorAssetLibrary.find_asset_data(GASP_SRC + "/Animations/" + n))
    if todo:
        made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            todo, ctrl.get_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE),
            ctrl.get_preview_mesh(unreal.RetargetSourceOrTarget.TARGET), rtg, prefix="AS_GASP_", target_path=GASP_OUT,
            include_referenced_assets=False, overwrite_existing_files=overwrite)
        for a in made:
            p = str(a.package_name)
            anim = unreal.load_asset(p)
            unreal.AnimationLibrary.remove_all_animation_notify_tracks(anim)  # bruitages GASP : absents de Gen
            unreal.EditorAssetLibrary.save_loaded_asset(anim, False)
            out[p.split("AS_GASP_")[-1]] = p
    sync_ik_bones(list(out.values()))
    add_foot_sync_markers([p for p in out.values() if "_Loop" in p])
    return out


def bake_reversed(src, dst):
    """Copie de src jouée à l'envers (toutes les pistes d'os, clés inversées), sans notifies. Un montage ne garde pas
    de vitesse négative (remise à +1 à la validation) : pour lire un geste à rebours, on cuit le clip.
    Ex. la frappe vers le bas de Phase Ability_E à l'envers = les deux mains qui montent (Flame Pillar, 2026-10-09)."""
    EAL, AL, S = unreal.EditorAssetLibrary, unreal.AnimationLibrary, unreal.AnimSequenceService
    if not EAL.does_asset_exist(dst):
        EAL.duplicate_asset(src, dst)
    seq = unreal.load_asset(dst)
    AL.remove_all_animation_notify_tracks(seq)
    n = AL.get_num_keys(seq)
    poses = [{str(b.bone_name): b.transform for b in S.get_pose_at_frame(src, f, False)} for f in range(n)]
    ctrl = seq.controller
    for bone in [str(t) for t in AL.get_animation_track_names(seq)]:
        keys = [poses[n - 1 - f][bone] for f in range(n)]
        ctrl.set_bone_track_keys(bone, [k.translation for k in keys], [k.rotation for k in keys], [k.scale3d for k in keys])
    EAL.save_loaded_asset(seq, False)
    return dst, n


_LOWER_BODY = ("root", "pelvis", "thigh_l", "thigh_r", "calf_l", "calf_r", "foot_l", "foot_r", "ball_l", "ball_r",
               "thigh_twist_01_l", "thigh_twist_02_l", "thigh_twist_01_r", "thigh_twist_02_r",
               "calf_twist_01_l", "calf_twist_02_l", "calf_twist_01_r", "calf_twist_02_r")


def bake_upper_body(src, dst, legs_from=None, lean=None, reverse=False, mix=None, ramps=None, smooth=None):
    """Copie de src où seul le haut du corps bouge : bassin et jambes pris de legs_from (boucle d'idle GASP par
    défaut), donc il reste debout ; reverse=True la joue à l'envers (voir bake_reversed). lean = (os, rotateur
    (roll, pitch, yaw) en degrés) ajouté en local à cet os sur tout le clip, ex. ("spine_01", (0, 0, 8)) pour pencher
    le buste en avant. Ex. Flame Pillar (2026-10-09) : « seul le haut bouge, légèrement penché en avant ».
    mix = {os: k} : rotation locale = k de src + (1 - k) de legs_from (0,5 = mouvement amorti de moitié), ex. torsion du
    buste et bras libre d'un lancer Paragon (LMB, 2026-10-09 : « il se déforme »).
    ramps = [(os, (roll, pitch, yaw), f0, f1)] : rotation locale ajoutée progressivement de f0 à f1 puis gardée, ex. les
    bras qui finissent tendus vers la cible (Flame Pillar : « il contrôle l'explosion »).
    smooth = (f0, f1, passes) : lisse les rotations du haut du corps entre f0 et f1 (moyenne 3 clés, n passes), contre
    les à-coups d'un clip source (élan de Gideon C : main qui saute de 27 cm, LMB « saccadé », 2026-10-09)."""
    EAL, AL, S = unreal.EditorAssetLibrary, unreal.AnimationLibrary, unreal.AnimSequenceService
    legs_from = legs_from or GASP_OUT + "/AS_GASP_M_Neutral_Stand_Idle_Loop"
    if not EAL.does_asset_exist(dst):
        EAL.duplicate_asset(src, dst)
    seq = unreal.load_asset(dst)
    AL.remove_all_animation_notify_tracks(seq)
    n = AL.get_num_keys(seq)
    m = AL.get_num_keys(unreal.load_asset(legs_from))
    poses = [{str(b.bone_name): b.transform for b in S.get_pose_at_frame(src, (n - 1 - f) if reverse else f, False)}
             for f in range(n)]
    legs = [{str(b.bone_name): b.transform for b in S.get_pose_at_frame(legs_from, f % m, False)} for f in range(n)]
    delta = unreal.Rotator(roll=lean[1][0], pitch=lean[1][1], yaw=lean[1][2]).quaternion() if lean else None
    ctrl = seq.controller
    for bone in [str(t) for t in AL.get_animation_track_names(seq)]:
        keys = [(legs[f] if bone in _LOWER_BODY else poses[f])[bone] for f in range(n)]
        rots = [k.rotation for k in keys]
        if mix and bone in mix and bone not in _LOWER_BODY:
            rots = [unreal.MathLibrary.quat_slerp(legs[f][bone].rotation, rots[f], mix[bone]) for f in range(n)]
        for rb, rr, f0, f1 in (ramps or []):
            if rb == bone:
                dq = unreal.Rotator(roll=rr[0], pitch=rr[1], yaw=rr[2]).quaternion()
                ident = unreal.Rotator(0, 0, 0).quaternion()
                w = lambda f: min(max((f - f0) / float(max(f1 - f0, 1)), 0.0), 1.0)
                rots = [unreal.MathLibrary.multiply_quat_quat(rots[f], unreal.MathLibrary.quat_slerp(ident, dq, w(f)))
                        for f in range(n)]
        if lean and bone == lean[0]:
            rots = [unreal.MathLibrary.multiply_quat_quat(r, delta) for r in rots]
        if smooth and bone not in _LOWER_BODY:
            s0, s1, passes = smooth
            for _ in range(passes):
                rots = [rots[f] if not (s0 < f < min(s1, n - 1)) else
                        unreal.MathLibrary.quat_slerp(unreal.MathLibrary.quat_slerp(rots[f - 1], rots[f + 1], 0.5), rots[f], 0.5)
                        for f in range(n)]
        ctrl.set_bone_track_keys(bone, [k.translation for k in keys], rots, [k.scale3d for k in keys])
    EAL.save_loaded_asset(seq, False)
    sync_ik_bones([dst])
    return dst, n


# Os IK du mannequin UE5 : (os IK, os qu'il doit suivre, parent IK). None = garde la pose de référence.
_IK_BONES = [("ik_foot_l", "foot_l", "ik_foot_root"), ("ik_foot_r", "foot_r", "ik_foot_root"),
             ("ik_hand_gun", "hand_r", "ik_hand_root"), ("ik_hand_r", "hand_r", "ik_hand_gun"),
             ("ik_hand_l", "hand_l", "ik_hand_gun")]


def sync_ik_bones(paths):
    """Recalcule ik_foot_* / ik_hand_* pour qu'ils suivent les vrais pieds et mains, à chaque clé.
    Le retarget ne les anime pas (ils restent en pose de référence) ; le Control Rig de foot IK de l'ABP vise ik_foot_*
    et colle alors les pieds sur place : course en « glissade » pieds joints (2026-10-09). Les clips d'Epic les ont.
    Les clips doivent être verrouillés (LFS) ; renvoie {chemin: nb de clés}."""
    S, AL = unreal.AnimSequenceService, unreal.AnimationLibrary
    out = {}
    for p in paths:
        seq = unreal.load_asset(p)
        n = AL.get_num_keys(seq)
        keys = {ik: ([], [], []) for ik, _, _ in _IK_BONES}
        for f in range(n):
            cs = {str(b.bone_name): b.transform for b in S.get_pose_at_frame(p, f, True)}
            new = {}
            for ik, src, parent in _IK_BONES:
                new[ik] = cs[src]
                local = cs[src].make_relative(new.get(parent, cs[parent]))
                keys[ik][0].append(local.translation)
                keys[ik][1].append(local.rotation)
                keys[ik][2].append(local.scale3d)
        ctrl = seq.controller
        names = [str(t) for t in AL.get_animation_track_names(seq)]
        for ik, (pos, rot, scl) in keys.items():
            if ik not in names:
                ctrl.add_bone_curve(ik)
            ctrl.set_bone_track_keys(ik, pos, rot, scl)
        unreal.EditorAssetLibrary.save_loaded_asset(seq, False)
        out[p] = n
    return out


# Sens de déplacement de chaque boucle GASP dans l'espace du mannequin (avant = +Y, gauche = +X).
_TRAVEL = {"F": (0, 1), "B": (0, -1), "LL": (1, 0), "LR": (1, 0), "RL": (-1, 0), "RR": (-1, 0),
           "FL": (0.707, 0.707), "FR": (-0.707, 0.707), "BL": (0.707, -0.707), "BR": (-0.707, -0.707)}


def add_foot_sync_markers(paths, track="Sync"):
    """Marqueurs de synchro "L" / "R" aux appuis (pied gauche / droit le plus en avant dans le sens de déplacement).
    Sans eux, un blend space synchronise ses boucles en pourcentage : les boucles GASP n'ont pas le même nombre de
    foulées (Run_F ~3,9, Run_LL ~4,2), les jambes se décalent et le mélange devient un piétinement (2026-10-09).
    Les boucles doivent être verrouillées (LFS) ; renvoie {chemin: nb de marqueurs}."""
    S, AL = unreal.AnimSequenceService, unreal.AnimationLibrary
    out = {}
    for p in paths:
        seq = unreal.load_asset(p)
        if "Idle" in p:  # sans foulée, mais un blend space ne synchronise par marqueurs que si TOUS ses clips en ont
            AL.remove_animation_sync_markers_by_track(seq, track)
            if track not in [str(t) for t in AL.get_animation_notify_track_names(seq)]:
                AL.add_animation_notify_track(seq, track)
            AL.add_animation_sync_marker(seq, "L", 0.0, track)
            AL.add_animation_sync_marker(seq, "R", seq.get_play_length() / 2, track)
            unreal.EditorAssetLibrary.save_loaded_asset(seq, False)
            out[p] = 2
            continue
        tx, ty = _TRAVEL[p.rsplit("_", 1)[-1]]
        n = int(round(seq.get_play_length() * FPS))
        d = []
        for f in range(n):
            pose = {str(b.bone_name): b.transform.translation for b in S.get_pose_at_frame(p, f, True)}
            v = pose["foot_l"] - pose["foot_r"]
            d.append(v.x * tx + v.y * ty)
        AL.remove_animation_sync_markers_by_track(seq, track)
        if track not in [str(t) for t in AL.get_animation_notify_track_names(seq)]:
            AL.add_animation_notify_track(seq, track)
        marks = 0
        for f in range(n):  # boucle : voisins pris modulo n
            a, b, c = d[f - 1], d[f], d[(f + 1) % n]
            name = "L" if b > a and b >= c else "R" if b < a and b <= c else None
            if name and abs(b) > 20:  # ignore les petits creux du bruit
                AL.add_animation_sync_marker(seq, name, f / FPS, track)
                marks += 1
        unreal.EditorAssetLibrary.save_loaded_asset(seq, False)
        out[p] = marks
    return out


# --- Retarget générique (toute source externe : Lyra, autres packs) -------------------------------------------------
def _exists(path):
    """does_asset_exist renvoie parfois False pour _Local alors que l'asset se charge (2026-10-09) : on charge."""
    return unreal.EditorAssetLibrary.does_asset_exist(path) or unreal.load_asset(path) is not None


def make_retargeter(name, src_ik, tgt_ik, src_mesh, tgt_mesh="/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"):
    """Crée (une fois) LOCAL/<name> avec la pile d'opérations par défaut et les chaînes appariées par nom."""
    path = LOCAL + "/" + name
    if _exists(path):
        return unreal.load_asset(path)
    rtg = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, LOCAL, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    S, T = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
    c.set_ik_rig(S, unreal.load_asset(src_ik))
    c.set_ik_rig(T, unreal.load_asset(tgt_ik))
    c.set_preview_mesh(S, unreal.load_asset(src_mesh))
    c.set_preview_mesh(T, unreal.load_asset(tgt_mesh))
    c.add_default_ops()  # sans pile d'opérations, le retarget produit une pose figée (UE 5.6+)
    c.assign_ik_rig_to_all_ops(S, unreal.load_asset(src_ik))
    c.assign_ik_rig_to_all_ops(T, unreal.load_asset(tgt_ik))
    for i in range(c.get_num_retarget_ops()):
        c.run_op_initial_setup(i)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    unreal.EditorAssetLibrary.save_loaded_asset(rtg, False)
    return rtg


def retarget_external(src_paths, rtg, prefix, out_folder, strip_notifies=True):
    """Clips externes -> mannequin de Gen (out_folder/<prefix><nom>). Déjà faits : ignorés. Arrêter le PIE avant."""
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("retarget_external : arrêter le PIE d'abord")
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    out, todo = {}, []
    for p in src_paths:
        dst = out_folder + "/" + prefix + p.split("/")[-1]
        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            out[p] = dst
        else:
            todo.append(unreal.EditorAssetLibrary.find_asset_data(p))
    if todo:
        made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            todo, ctrl.get_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE),
            ctrl.get_preview_mesh(unreal.RetargetSourceOrTarget.TARGET), rtg, prefix=prefix, target_path=out_folder,
            include_referenced_assets=False, overwrite_existing_files=False)
        for a in made:
            anim = unreal.load_asset(str(a.package_name))
            if strip_notifies:
                unreal.AnimationLibrary.remove_all_animation_notify_tracks(anim)
            unreal.EditorAssetLibrary.save_loaded_asset(anim, False)
            out[str(a.package_name)] = str(a.package_name)
        sync_ik_bones(list(out.values()))  # le retarget laisse ik_foot_* / ik_hand_* en pose de référence
    return out


def retarget_lyra(names):
    """Actions du mannequin Lyra (même squelette que Gen, autre asset) -> Animations/Lyra/AS_Lyra_<nom>."""
    lyra = "/Game/Characters/Heroes/Mannequin"
    rtg = make_retargeter("RTG_Lyra_To_Manny", LOCAL + "/IK_Manny", LOCAL + "/IK_Manny", lyra + "/Meshes/SKM_Manny")
    return retarget_external([lyra + "/Animations/Actions/" + n for n in names], rtg, "AS_Lyra_", CURFFE_ANIMS + "/Lyra")


# --- Tous les héros Paragon : IK rig dérivé de celui de Gideon (même nommage des os Epic) + retargeter par héros ----
def _hero_of(path):
    """'/Game/ParagonLtBelica/...' -> 'LtBelica'."""
    return path.split("/")[2][len("Paragon"):]


def _mesh_for_skeleton(skeleton, folder):
    """Premier SkeletalMesh du dossier du héros qui utilise ce squelette (les noms de mesh varient d'un héros à l'autre)."""
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    want = skeleton.get_path_name()
    for a in ar.get_assets_by_path(folder, recursive=True):
        if str(a.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        tag = a.get_tag_value("Skeleton")
        if tag and want.split(".")[0] in str(tag):
            return str(a.package_name)
    return None


def hero_retargeter(hero, sample_clip):
    """RTG_<Hero>_To_Manny (LOCAL) : IK_<Hero> = copie de IK_Gideon sur le mesh du héros, cible IK_Manny."""
    rtg_name = f"RTG_{hero}_To_Manny"
    if _exists(LOCAL + "/" + rtg_name):
        return unreal.load_asset(LOCAL + "/" + rtg_name)
    skel = unreal.load_asset(sample_clip).get_editor_property("skeleton")
    mesh = _mesh_for_skeleton(skel, f"/Game/Paragon{hero}")
    if not mesh:
        raise RuntimeError(f"pas de mesh pour le squelette de {hero}")
    ik_path = f"{LOCAL}/IK_{hero}"
    if not _exists(ik_path):
        unreal.EditorAssetLibrary.duplicate_asset(LOCAL + "/IK_Gideon", ik_path)
        ik = unreal.load_asset(ik_path)
        unreal.IKRigController.get_controller(ik).set_skeletal_mesh(unreal.load_asset(mesh))
        unreal.EditorAssetLibrary.save_loaded_asset(ik, False)
    return make_retargeter(rtg_name, ik_path, LOCAL + "/IK_Manny", mesh)


def retarget_paragon(clip_paths, overwrite=False):
    """Clips de n'importe quel héros Paragon -> Animations/Paragon/AS_<Hero>_<Clip>. Renvoie {source: cible}."""
    by_hero = {}
    for p in clip_paths:
        by_hero.setdefault(_hero_of(p), []).append(p)
    out = {}
    for hero, clips in by_hero.items():
        rtg = hero_retargeter(hero, clips[0])
        res = retarget_external(clips, rtg, f"AS_{hero}_", PARAGON_OUT)
        for src in clips:
            out[src] = f"{PARAGON_OUT}/AS_{hero}_{src.split('/')[-1]}"
    return out
