"""Planche contact VFX en PIE.

Ralentit le temps de jeu, puis capture N images du viewport PIE à intervalle fixe de temps de jeu,
depuis un callback de tick (le Python bloque le game thread : impossible de capturer une séquence
dans un seul appel). Ensuite `Tools/VFX/MakeContactSheet.ps1 <dossier>` assemble la planche.

Usage (execute_python_code, PIE lancé) :
    import sys; sys.path.append(r"<projet>/Tools/VFX"); import vfx_capture as vc
    vc.spawn_across_view("/Game/.../BP_Projectile_Fireball.BP_Projectile_Fireball_C")
    vc.start("fireball", frames=12, interval=0.1, slomo=0.1)
    ... appel suivant : vc.status()
Aucune référence à un objet PIE n'est gardée entre deux ticks (sinon crash à la fin du PIE).
"""
import json
import os

import unreal

_S = {}


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _out_dir(name):
    return os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "VFXCaptures", name)


def view_basis():
    """Position du joueur et axes écran (droite, haut) projetés au sol, depuis la caméra de jeu."""
    w = _world()
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    cam = unreal.GameplayStatics.get_player_camera_manager(w, 0)
    rot = cam.get_camera_rotation()
    right = rot.get_right_vector()
    right.z = 0.0
    right = right.normal()
    up = rot.get_forward_vector()  # haut de l'écran au sol : avant de la caméra, à plat
    up.z = 0.0
    up = up.normal()
    loc = pawn.get_actor_location()
    del pawn, cam
    return loc, right, up


def spawn_across_view(class_path, side_offset=550.0, depth_offset=150.0, height=0.0, yaw_offset=0.0):
    """Fait apparaître un acteur (projectile) à gauche du joueur, orienté vers la droite de l'écran."""
    loc, right, up = view_basis()
    start = loc - right * side_offset + up * depth_offset + unreal.Vector(0, 0, height)
    rot = unreal.MathLibrary.conv_vector_to_rotator(right)
    rot.yaw += yaw_offset
    res = json.loads(unreal.PIEActorService.spawn_actor("server", class_path, unreal.Transform(start, rot)))
    return res


def swap_fx(actor_path, system_path, component="ProjectileFX"):
    """Remplace le système Niagara d'un composant d'un acteur PIE (test sans toucher au Blueprint)."""
    a = unreal.find_object(None, actor_path)
    fx = a.get_editor_property(component)
    fx.set_asset(unreal.load_asset(system_path))
    fx.activate(True)
    del a, fx


def start(name, frames=12, interval=0.1, slomo=0.1, first_delay=0.0, scale=1.0):
    """Lance la capture : `frames` images toutes les `interval` secondes de temps de jeu.
    `scale` > 1 rend les captures en plus haute résolution que le viewport (détail pour recadrer)."""
    stop(silent=True)
    w = _world()
    if not w:
        return "PIE non lancé"
    out = _out_dir(name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.lower().endswith(".png"):
            os.remove(os.path.join(out, f))
    unreal.PerformanceService.set_background_throttling(False)
    unreal.SystemLibrary.execute_console_command(w, "slomo {}".format(slomo))
    t0 = unreal.GameplayStatics.get_time_seconds(w)
    size = unreal.WidgetLayoutLibrary.get_viewport_size(w)
    _S.update(name=name, out=out, frames=frames, interval=interval, t0=t0, next=t0 + first_delay, i=0,
              res=(int(size.x * scale), int(size.y * scale)), done=False, times=[])
    del w
    _S["handle"] = unreal.register_slate_post_tick_callback(_tick)
    return out


def _tick(_dt):
    # La capture fait tourner un tick Slate, qui rappelle ce callback : garde contre la réentrance
    if _S.get("busy") or "handle" not in _S:
        return
    w = _world()
    if not w:
        stop(silent=True)
        return
    t = unreal.GameplayStatics.get_time_seconds(w)
    del w
    if t < _S["next"]:
        return
    _S["busy"] = True
    try:
        i = _S["i"]
        _S["i"] = i + 1
        _S["next"] += _S["interval"]
        _S["times"].append(round(t - _S["t0"], 3))
        fn = os.path.join(_S["out"], "f{:02d}.png".format(i))
        unreal.AutomationLibrary.take_high_res_screenshot(_S["res"][0], _S["res"][1], fn)
    finally:
        _S["busy"] = False
    if _S["i"] >= _S["frames"]:
        stop(silent=True)


def stop(silent=False):
    h = _S.pop("handle", None)
    if h is not None:
        unreal.unregister_slate_post_tick_callback(h)
        w = _world()
        if w:
            unreal.SystemLibrary.execute_console_command(w, "slomo 1")
        del w
        _S["done"] = True
        with open(os.path.join(_S["out"], "times.json"), "w") as f:
            json.dump(_S["times"], f)
    if not silent:
        return status()


def status():
    return {k: _S.get(k) for k in ("name", "out", "i", "frames", "done", "times")}
