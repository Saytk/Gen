"""Passage PIE des animations de sorts : lance chaque sort et journalise les montages joués (lignes "GENANIM").

Usage (PIE lancé, Standalone ou client 1) :
    import gen_anim_pie as gap
    gap.run(gap.CURFFE)           # programme les sorts ; attendre que gap.done() soit vrai
    gap.timeline()                # [(t, montage, section)] lus depuis le journal en mémoire
Un appel Python bloque le jeu : tout passe par des rappels de tick (gen_pie_tools.run_schedule).
Aucune référence PIE n'est gardée (seulement des chemins), arrêter avec stop() avant StopPIE.
"""
import time

import unreal

import gen_pie_tools as t

INPUT = "/Game/Gen/Input/"
# (début s, action, durée de maintien s) ; énergie et flammes remplies avant chaque sort.
CURFFE = [
    (0.5, "IA_Ability_Primary", 0.1),
    (2.0, "IA_Ability_Secondary", 1.2),
    (5.0, "IA_Ability_1", 1.0),
    (8.0, "IA_Ability_2", 1.0),
    (11.0, "IA_Ability_3", 1.0),
    (14.0, "IA_Ability_Mobility", 0.8),
    (17.0, "IA_Ability_Ultimate", 0.1),
]

_s = {"handle": None, "pawn": None, "last": None, "log": [], "t0": 0.0, "end": 0.0}


def _pawn():
    p = unreal.find_object(None, _s["pawn"]) if _s["pawn"] else None
    return p


def _tick(_dt):
    try:
        p = _pawn()
        if p is None:
            return
        inst = p.get_editor_property("mesh").get_anim_instance()
        m = inst.get_current_active_montage() if inst else None
        cur = (m.get_name(), str(inst.montage_get_current_section(m))) if m else None
        if cur != _s["last"]:
            _s["last"] = cur
            now = round(time.monotonic() - _s["t0"], 3)
            _s["log"].append((now,) + (cur or ("-", "-")))
            unreal.log("GENANIM t=%.3f %s" % (now, cur))
        if time.monotonic() - _s["t0"] > _s["end"]:
            stop()
    except Exception as e:
        unreal.log_warning("GENANIM arrêté : %s" % e)
        stop()


def _fill():
    p = _pawn()
    if p:
        t._apply_set_by_caller(p, unreal.GenGE_Gain, {"SetByCaller.Energy": 100.0, "SetByCaller.Resource": 5.0})


def _press(action, seconds):
    def go():
        _fill()
        unreal.InputService.inject_action_for(INPUT + action, seconds, 1.0, 0.0, 0.0, 0)
    return go


def run(plan, shots=()):
    """shots : [(secondes, nom)] -> commande console 'shot' (image du viewport, historique TSR gardé)."""
    stop()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    _s["pawn"] = unreal.GameplayStatics.get_player_pawn(world, 0).get_path_name()
    _s["log"], _s["last"], _s["t0"] = [], None, time.monotonic()
    _s["end"] = max(s for s, _, d in plan) + 3.0
    steps = [(s, _press(a, d)) for s, a, d in plan]
    # sans contexte de monde, 'shot' ne part pas en PIE : on passe le monde de jeu à chaque fois
    shot = lambda: unreal.SystemLibrary.execute_console_command(
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), "shot")
    steps += [(s, shot) for s, _ in shots]
    t.run_schedule(steps)
    _s["handle"] = unreal.register_slate_post_tick_callback(_tick)


# --- Capture en jeu d'un sort (rendu réel, caméra de jeu) -----------------------------------------
# Ralentit le jeu, maintient la touche le temps voulu (en temps de jeu), et prend une image haute résolution du
# viewport toutes les `interval` s de temps de jeu, avec la position écran du personnage (pour recadrer ensuite).
# Sortie : .superpowers/sdd/Anim/InGame/<nom>/f000.png + meta.json ; planche/GIF : Tools/Anim/ingame_sheet.py.
import json
import os

_c = {"handle": None}


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def cast_capture(name, action, hold=0.1, duration=1.5, interval=1 / 30.0, slomo=0.2, aim=(0.0, 400.0), scale=2.0):
    """aim : point visé, décalé par rapport au personnage (x, y en cm, monde) : il doit se tourner vers lui."""
    cast_stop()
    w = _world()
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    _fill_pawn(pawn)
    loc = pawn.get_actor_location()
    pc.set_editor_property("debug_aim_override", True)
    pc.set_editor_property("debug_aim_location", unreal.Vector(loc.x + aim[0], loc.y + aim[1], 0))
    out = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), ".superpowers", "sdd", "Anim", "InGame", name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"):
            os.remove(os.path.join(out, f))
    size = unreal.WidgetLayoutLibrary.get_viewport_size(w)
    unreal.PerformanceService.set_background_throttling(False)
    unreal.SystemLibrary.execute_console_command(w, "slomo {}".format(slomo))
    t0 = unreal.GameplayStatics.get_time_seconds(w)
    _c.update(out=out, t0=t0, next=t0, interval=interval, end=t0 + duration, frames=[], busy=False,
              res=(int(size.x * scale), int(size.y * scale)), scale=scale, pawn=pawn.get_path_name(),
              pc=pc.get_path_name(), name=name, action=action, hold=hold, yaw0=round(pawn.get_actor_rotation().yaw, 1))
    unreal.InputService.inject_action_for(INPUT + action, hold / slomo, 1.0, 0.0, 0.0, 0)
    _c["handle"] = unreal.register_slate_post_tick_callback(_cap_tick)
    del w, pc, pawn
    return out


def _fill_pawn(pawn):
    t._apply_set_by_caller(pawn, unreal.GenGE_Gain, {"SetByCaller.Energy": 100.0, "SetByCaller.Resource": 5.0})


def _cap_tick(_dt):
    if _c.get("busy") or _c.get("handle") is None:
        return
    w = _world()
    if not w:
        cast_stop()
        return
    now = unreal.GameplayStatics.get_time_seconds(w)
    if now < _c["next"]:
        return
    _c["busy"] = True
    try:
        pawn = unreal.find_object(None, _c["pawn"])
        pc = unreal.find_object(None, _c["pc"])
        scr = unreal.GameplayStatics.project_world_to_screen(pc, pawn.get_actor_location(), False)
        inst = pawn.get_editor_property("mesh").get_anim_instance()
        m = inst.get_current_active_montage() if inst else None
        i = len(_c["frames"])
        _c["frames"].append({"t": round(now - _c["t0"], 3), "sx": scr.x * _c["scale"], "sy": scr.y * _c["scale"],
                             "yaw": round(pawn.get_actor_rotation().yaw, 1), "montage": m.get_name() if m else "-"})
        unreal.AutomationLibrary.take_high_res_screenshot(_c["res"][0], _c["res"][1], os.path.join(_c["out"], "f%03d.png" % i))
        _c["next"] += _c["interval"]
        del pawn, pc, inst, m
    except Exception as e:
        unreal.log_warning("GENCAST arrêté : %s" % e)
        _c["busy"] = False
        cast_stop()
        return
    finally:
        _c["busy"] = False
    if now >= _c["end"]:
        cast_stop()


def cast_stop():
    h = _c.get("handle")
    if h is not None:
        unreal.unregister_slate_post_tick_callback(h)
        _c["handle"] = None
        w = _world()
        if w:
            unreal.SystemLibrary.execute_console_command(w, "slomo 1")
        meta = {k: _c[k] for k in ("name", "action", "hold", "interval", "yaw0", "frames", "res")}
        with open(os.path.join(_c["out"], "meta.json"), "w") as f:
            json.dump(meta, f)


# --- Caméra d'animation en jeu (fiable) ----------------------------------------------------------
# Une SceneCapture2D suit le personnage depuis un angle fixe (le lacet ne suit PAS l'acteur : on voit s'il se tourne),
# rend sur une render target et l'exporte en PNG dans le même tick (synchrone, contrairement aux captures d'écran).
# Scénario : liste d'étapes (temps de jeu, fonction). Sortie : .superpowers/sdd/Anim/InGame/<nom>/f000.png + meta.json.

_r = {"handle": None}


def move(x, y, seconds_game, slomo):
    """Maintient IA_Move (x, y) pendant seconds_game de temps de jeu."""
    unreal.InputService.inject_action_for(INPUT + "IA_Move", seconds_game / slomo, x, y, 0.0, 0)


def press(action, hold_game, slomo):
    pawn = unreal.GameplayStatics.get_player_pawn(_world(), 0)
    _fill_pawn(pawn)
    del pawn
    unreal.InputService.inject_action_for(INPUT + action, max(hold_game, 0.05) / slomo, 1.0, 0.0, 0.0, 0)


def rig_capture(name, steps, duration, fps=30, slomo=0.25, aim=(400.0, 0.0), view=(-40.0, -135.0, 520.0), size=(720, 720),
                start=(0.0, 0.0)):
    """steps : [(temps de jeu s, fonction sans argument)]. aim : point visé relatif à la position de départ (cm).
    view : (tangage, lacet, distance) de la caméra autour du personnage. La visée reste fixe pendant tout le scénario."""
    rig_stop()
    w = _world()
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    if start is not None:  # point de départ dégagé (centre de L_Arena par défaut)
        z = pawn.get_actor_location().z
        pawn.set_actor_location(unreal.Vector(start[0], start[1], z), False, True)
    loc = pawn.get_actor_location()
    pc.set_editor_property("debug_aim_override", True)
    pc.set_editor_property("debug_aim_location", unreal.Vector(loc.x + aim[0], loc.y + aim[1], 0))
    rt = unreal.RenderingLibrary.create_render_target2d(w, size[0], size[1], unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
    res = json.loads(unreal.PIEActorService.spawn_actor("server", "/Script/Engine.SceneCapture2D", unreal.Transform(loc, unreal.Rotator(0, 0, 0))))
    cap = _rig_cam(w, res["actor_name"])
    comp = cap.get_editor_property("capture_component2d")
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property("capture_every_frame", False)
    comp.set_editor_property("capture_on_movement", False)
    comp.set_editor_property("fov_angle", 40.0)
    out = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), ".superpowers", "sdd", "Anim", "InGame", name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png") or f == "meta.json":  # meta.json = fin de capture : celui d'une capture précédente tromperait l'attente
            os.remove(os.path.join(out, f))
    unreal.PerformanceService.set_background_throttling(False)
    unreal.SystemLibrary.execute_console_command(w, "slomo {}".format(slomo))
    t0 = unreal.GameplayStatics.get_time_seconds(w)
    _r.update(out=out, t0=t0, next=t0, interval=1.0 / fps, end=t0 + duration, frames=[], busy=False, view=view,
              steps=sorted(steps, key=lambda s: s[0]), pawn=pawn.get_path_name(), cap=res["actor_name"],
              rt=rt.get_path_name(), name=name, slomo=slomo, aim=aim)
    _r["rt_obj"] = rt  # la render target doit vivre jusqu'à la fin ; libérée dans rig_stop
    _r["handle"] = unreal.register_slate_post_tick_callback(_rig_tick)
    del w, pc, pawn, cap, comp
    return out


def _rig_cam(world, name):
    """find_object ne retrouve pas les acteurs du monde PIE par chemin : recherche par nom."""
    return next((c for c in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D) if c.get_name() == name), None)


def _rig_tick(_dt):
    if _r.get("busy") or _r.get("handle") is None:
        return
    _r["busy"] = True
    try:
        w = _world()
        if not w:
            rig_stop()
            return
        now = unreal.GameplayStatics.get_time_seconds(w) - _r["t0"]
        while _r["steps"] and now >= _r["steps"][0][0]:
            _r["steps"].pop(0)[1]()
        if now + _r["t0"] >= _r["next"]:
            pawn = unreal.find_object(None, _r["pawn"])
            cap = _rig_cam(w, _r["cap"])
            pitch, yaw, dist = _r["view"]
            target = pawn.get_actor_location() + unreal.Vector(0, 0, 40)
            rot = unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)
            cap.set_actor_location_and_rotation(target - rot.get_forward_vector() * dist, rot, False, True)
            comp = cap.get_editor_property("capture_component2d")
            comp.capture_scene()
            i = len(_r["frames"])
            unreal.RenderingLibrary.export_render_target(w, _r["rt_obj"], _r["out"], "f%03d.png" % i)
            inst = pawn.get_editor_property("mesh").get_anim_instance()
            m = inst.get_current_active_montage() if inst else None
            v = pawn.get_velocity()
            try:  # angle de déplacement par rapport au regard (entrée X du blend space de locomotion)
                dirn = round(float(inst.get_editor_property("Direction")))
            except Exception:
                dirn = None
            _r["frames"].append({"t": round(now, 3), "yaw": round(pawn.get_actor_rotation().yaw, 1),
                                 "speed": round((v.x ** 2 + v.y ** 2) ** 0.5), "dir": dirn,
                                 "montage": m.get_name() if m else "-"})
            _r["next"] += _r["interval"]
            del pawn, cap, comp, inst, m
        if now + _r["t0"] >= _r["end"]:
            rig_stop()
        del w
    except Exception as e:
        unreal.log_warning("GENRIG arrêté : %s" % e)
        rig_stop()
    finally:
        _r["busy"] = False


def rig_stop():
    h = _r.get("handle")
    if h is None:
        return
    unreal.unregister_slate_post_tick_callback(h)
    _r["handle"] = None
    w = _world()
    if w:
        unreal.SystemLibrary.execute_console_command(w, "slomo 1")
        cap = _rig_cam(w, _r["cap"])
        if cap:
            cap.destroy_actor()
    _r.pop("rt_obj", None)
    meta = {"name": _r["name"], "interval": _r["interval"], "slomo": _r["slomo"], "aim": _r["aim"],
            "frames": _r["frames"], "res": None}
    with open(os.path.join(_r["out"], "meta.json"), "w") as f:
        json.dump(meta, f)


def rig_done():
    return _r.get("handle") is None


def cast_done():
    return _c.get("handle") is None


def done():
    return _s["handle"] is None and not t.schedule_running()


def timeline():
    return list(_s["log"])


def stop():
    if _s["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(_s["handle"])
        _s["handle"] = None
