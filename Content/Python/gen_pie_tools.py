"""Outils de test PIE pour Gen (serveur dédié + clients). Usage : import gen_pie_tools as t

Les fonctions renvoient des objets PIE : ne pas les garder dans des variables globales
au moment d'arrêter le PIE (sinon l'éditeur plante au démontage du monde).
"""
import json

import unreal
import vibeue

INPUT = "/Game/Gen/Input/"


def worlds():
    w = vibeue.pie_worlds()
    return w["server"], w["clients"]


def _player_id(actor):
    """PlayerId du PlayerState d'un pion ou d'un contrôleur."""
    ps = actor.get_editor_property("player_state")
    return ps.get_editor_property("player_id") if ps else None


def server_pawns():
    server, _ = worlds()
    pawns = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.GenPlayerCharacter)
    pawns = [p for p in pawns if _player_id(p) is not None]
    return sorted(pawns, key=_player_id)


def client_controller(client_index):
    """client_index : 1, 2, 3 (ordre des fenêtres PIE)."""
    _, clients = worlds()
    return unreal.GameplayStatics.get_player_controller(clients[client_index - 1], 0)


def server_pawn_for(client_index):
    player_id = _player_id(client_controller(client_index))
    return next(p for p in server_pawns() if _player_id(p) == player_id)


def client_pawn(viewer_index, subject_index):
    """Copie du pion de subject dans le monde du client viewer."""
    _, clients = worlds()
    subject_id = _player_id(client_controller(subject_index))
    for p in unreal.GameplayStatics.get_all_actors_of_class(clients[viewer_index - 1], unreal.GenPlayerCharacter):
        if _player_id(p) == subject_id:
            return p
    return None


def place(client_index, x, y, yaw=0.0):
    pawn = server_pawn_for(client_index)
    pawn.set_actor_location_and_rotation(unreal.Vector(x, y, pawn.get_actor_location().z), unreal.Rotator(0, 0, yaw), False, True)
    pawn.get_movement_component().set_editor_property("velocity", unreal.Vector(0, 0, 0))


def aim(client_index, x, y):
    pc = client_controller(client_index)
    pc.set_editor_property("debug_aim_override", True)
    pc.set_editor_property("debug_aim_location", unreal.Vector(x, y, 0))


def tap(action, client_index):
    return unreal.InputService.inject_action(INPUT + action, 1.0, 0.0, 0.0, client_index)


def hold(action, seconds, client_index):
    return unreal.InputService.inject_action_for(INPUT + action, seconds, 1.0, 0.0, 0.0, client_index)


def state(client_index):
    p = server_pawn_for(client_index)
    return {"hp": p.get_health(), "energy": p.get_energy(), "flames": p.get_resource(), "fed": p.get_fed_resource(),
            "loc": p.get_actor_location(), "team": p.k2_get_team_id()}


def client_view_flames(viewer_index, subject_index):
    """Flammes de subject vues depuis le client viewer (réplication)."""
    p = client_pawn(viewer_index, subject_index)
    return p.get_resource() if p else None


def speed(client_index):
    """Vitesse max de déplacement du pion serveur (550 hors incantation)."""
    return server_pawn_for(client_index).get_movement_component().get_editor_property("max_walk_speed")


def _apply_set_by_caller(actor, effect_class, magnitudes):
    """Serveur : applique effect_class à l'ASC d'actor avec des SetByCaller {tag: valeur}."""
    asc = unreal.AbilitySystemLibrary.get_ability_system_component(actor)
    spec = asc.make_outgoing_spec(effect_class, 1.0, asc.make_effect_context())
    for tag_name, value in magnitudes.items():
        spec = unreal.AbilitySystemLibrary.assign_tag_set_by_caller_magnitude(spec, unreal.GameplayTagService.request_tag(tag_name), value)
    return asc.apply_gameplay_effect_spec_to_self(spec)


def gain(client_index, energy=0.0, flames=0.0):
    """Serveur : ajoute (ou retire si négatif) énergie et flammes au joueur client_index (UGenGE_Gain)."""
    return _apply_set_by_caller(server_pawn_for(client_index), unreal.GenGE_Gain,
                                {"SetByCaller.Energy": energy, "SetByCaller.Resource": flames})


def damage(actor, amount):
    """Serveur : inflige amount dégâts à actor (UGenGE_Damage).

    Attention : un appel Python s'exécute sous FEditorScriptExecutionGuard, et les RPC client
    déclenchés pendant cet appel (ex. ClientCancelAbility quand la mort annule un sort) partent
    en local sur le serveur au lieu d'aller au client. Pour tester une mort avec un sort en cours,
    tuer avec un vrai projectile (dans le tick du jeu), pas avec damage()."""
    return _apply_set_by_caller(actor, unreal.GenGE_Damage, {"SetByCaller.Damage": amount})


def inspect_tags(actor):
    """Tags actifs de l'ASC d'un acteur (inspecteur GAS d'Epic)."""
    ref = json.dumps({"actor": {"refPath": actor.get_path_name()}})
    r = unreal.ToolsetRegistry.execute_tool("GASToolsets.AbilitySystemInspectorToolset", "GetActiveTags", ref)
    return r.get_value_as_json_string()


def spawn_dummy(x, y, team=255, label=""):
    """Serveur : mannequin d'entraînement (équipe 255 = ennemi de tous)."""
    server, _ = worlds()
    res = json.loads(unreal.PIEActorService.spawn_actor("server", "/Script/Gen.GenTrainingDummy",
                                                        unreal.Transform(location=unreal.Vector(x, y, 100)), "AdjustIfPossibleButAlwaysSpawn", False, label))
    if res.get("success") and team != 255:
        actor = unreal.find_object(None, res["actor_path"])
        actor.set_editor_property("team_id", team)
    return res


def spawn_wall(x, y, scale=(0.2, 2.0, 2.0), label=""):
    """Serveur : cube statique (bloque projectiles et personnages), répliqué."""
    res = json.loads(unreal.PIEActorService.spawn_actor("server", "/Script/Engine.StaticMeshActor",
                                                        unreal.Transform(location=unreal.Vector(x, y, 100), scale=unreal.Vector(*scale)), "AlwaysSpawn", False, label))
    if res.get("success"):
        actor = unreal.find_object(None, res["actor_path"])
        smc = actor.static_mesh_component
        smc.set_mobility(unreal.ComponentMobility.MOVABLE)
        smc.set_static_mesh(unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube"))
        # Sans réplication du composant, le client reçoit un acteur sans maillage ni collision : sa prédiction
        # (repoussement) traverse le mur puis le serveur le corrige
        smc.set_is_replicated(True)
        actor.set_replicates(True)
    return res


def actor_by_path(path):
    return unreal.find_object(None, path)


# --- Surveillance image par image -------------------------------------------------------------
# Python bloque le jeu pendant un appel : impossible d'échantillonner dans un seul appel.
# Ce rappel par tick écrit dans le log (lignes "GENWATCH") chaque changement des personnages du
# serveur, avec le temps de jeu. Il ne garde que des chemins (aucune référence PIE en mémoire).
# Toujours appeler watch_stop() avant d'arrêter le PIE.

_watch = {"handle": None, "world": None, "actors": [], "last": {}}


def _watch_tick(delta_seconds):
    try:
        _watch_sample()
    except Exception as e:  # un rappel qui lève une exception à chaque image noierait le log
        unreal.log_warning("GENWATCH arrêté : %s" % e)
        watch_stop()


def _watch_sample():
    world = unreal.find_object(None, _watch["world"]) if _watch["world"] else None
    if world is None:
        return
    now = unreal.GameplayStatics.get_time_seconds(world)
    for label, path in _watch["actors"]:
        a = unreal.find_object(None, path)
        if a is None:
            continue
        loc = a.get_actor_location()
        move = a.get_movement_component()
        snap = (round(a.get_health(), 1), round(a.get_energy(), 1), round(a.get_resource(), 1), a.get_fed_resource(),
                round(loc.x), round(loc.y), round(move.get_editor_property("max_walk_speed")) if move else 0,
                a.get_cast_progress() >= 0, a.get_editor_property("is_dead"))
        if _watch["last"].get(label) != snap:
            _watch["last"][label] = snap
            unreal.log("GENWATCH t=%.3f %s hp=%s en=%s fl=%s fed=%s x=%s y=%s spd=%s cast=%s dead=%s" % ((now, label) + snap))


def watch_start(extra=()):
    """Surveille c1..c3 (pions serveur) et les acteurs extra [(label, actor)]."""
    watch_stop()
    server, _ = worlds()
    _watch["world"] = server.get_path_name()
    _watch["actors"] = [("c%d" % i, server_pawn_for(i).get_path_name()) for i in (1, 2, 3)]
    _watch["actors"] += [(label, actor.get_path_name()) for label, actor in extra]
    _watch["last"] = {}
    _watch["handle"] = unreal.register_slate_post_tick_callback(_watch_tick)
    return [label for label, _ in _watch["actors"]]


def watch_add(label, actor):
    _watch["actors"].append((label, actor.get_path_name()))


def watch_stop():
    if _watch["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(_watch["handle"])
        _watch["handle"] = None
    _watch["last"] = {}


def time_dilation(value):
    """Ralentit le jeu sur le serveur ET chaque client (la dilatation n'est pas répliquée en PIE).
    Attention : les durées de inject_action_for restent en temps réel."""
    server, clients = worlds()
    for w in [server] + clients:
        unreal.GameplayStatics.set_global_time_dilation(w, value)


# --- Ordonnanceur d'actions minutées ------------------------------------------------------------
# Un appel Python bloque le jeu : une séquence minutée passe par un rappel de tick. Temps réel.
# Attendre la fin (schedule_running() faux) avant un reload du module ou un StopPIE.

_sched = {"handle": None, "t0": 0.0, "steps": [], "trigger": None}


def _sched_tick(delta_seconds):
    import time
    try:
        if _sched["trigger"] is not None:
            cond, steps = _sched["trigger"]
            if not cond():
                return
            _sched["trigger"] = None
            _sched["t0"] = time.monotonic()
            _sched["steps"] = sorted(steps, key=lambda s: s[0])
        now = time.monotonic() - _sched["t0"]
        while _sched["steps"] and now >= _sched["steps"][0][0]:
            _sched["steps"].pop(0)[1]()
        if not _sched["steps"]:
            schedule_stop()
    except Exception as e:
        unreal.log_warning("GENSCHED arrêté : %s" % e)
        schedule_stop()


def run_schedule(steps, when=None):
    """steps : [(secondes réelles, fonction sans argument)]. when : condition sans argument ;
    si fournie, le temps 0 est la première image où elle est vraie."""
    import time
    schedule_stop()
    _sched["t0"] = time.monotonic()
    _sched["steps"] = sorted(steps, key=lambda s: s[0])
    _sched["trigger"] = (when, steps) if when else None
    _sched["handle"] = unreal.register_slate_post_tick_callback(_sched_tick)


def schedule_stop():
    if _sched["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(_sched["handle"])
        _sched["handle"] = None
    _sched["steps"] = []
    _sched["trigger"] = None


def schedule_running():
    return _sched["handle"] is not None


def mark(text):
    return lambda: unreal.log("GENMARK %s" % text)


def net_emulation(lag_ms, variance_ms=0, loss_pct=0, client_indices=None, server=True):
    """NetEmulation sur les net drivers des mondes choisis (tous les clients par défaut)."""
    srv, clients = worlds()
    targets = ([srv] if server else []) + [clients[i - 1] for i in (client_indices or range(1, len(clients) + 1))]
    for w in targets:
        for c in ("NetEmulation.PktLag %d" % lag_ms, "NetEmulation.PktLagVariance %d" % variance_ms,
                  "NetEmulation.PktLoss %d" % loss_pct):
            unreal.SystemLibrary.execute_console_command(w, c)


def ping_ms(client_index):
    return server_pawn_for(client_index).get_editor_property("player_state").get_ping_in_milliseconds()


# --- Barre de sorts (UMG, monde client) ---------------------------------------------------------

SLOT_NAMES = ("SlotPrimary", "SlotSecondary", "SlotMobility", "Slot1", "Slot2", "Slot3", "SlotUltimate")


def client_slots(client_index):
    """{nom : UGenAbilitySlot} du WBP_AbilityBar local du client client_index."""
    pc = client_controller(client_index)
    found = unreal.WidgetLibrary.get_all_widgets_of_class(pc, unreal.GenAbilitySlot, False)
    return {s.get_name(): s for s in found}


def world_slot_count(world):
    return len(unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.GenAbilitySlot, False))


def _sub_widget(slot, name):
    for tree in ("WidgetTree_0", "WidgetTree"):
        w = unreal.find_object(None, "%s.%s.%s" % (slot.get_path_name(), tree, name))
        if w is not None:
            return w
    return None


def _brush_object(image):
    return image.get_editor_property("brush").get_editor_property("resource_object") if image else None


def _scalar(image, param):
    mid = _brush_object(image)
    return mid.get_scalar_parameter_value(param) if isinstance(mid, unreal.MaterialInstanceDynamic) else None


def _shown(widget):
    vis = unreal.SlateVisibility
    return widget is not None and widget.get_visibility() in (vis.VISIBLE, vis.HIT_TEST_INVISIBLE, vis.SELF_HIT_TEST_INVISIBLE)


def slot_info(slot):
    """État lisible d'un emplacement : état, chiffre, libellé, glyphe, icône, paramètres des matériaux."""
    icon_mid = _brush_object(_sub_widget(slot, "IconImage"))
    icon = icon_mid.get_texture_parameter_value("Icon") if isinstance(icon_mid, unreal.MaterialInstanceDynamic) else None
    key_text = _sub_widget(slot, "KeyText")
    glyph = _sub_widget(slot, "KeyGlyphImage")
    sweep = _sub_widget(slot, "SweepImage")
    arc = _sub_widget(slot, "ArcImage")
    glyph_tex = _brush_object(glyph) if _shown(glyph) else None
    return {
        "state": slot.get_state().name,
        "cd": slot.get_cooldown_text(),
        "key": str(key_text.get_text()) if _shown(key_text) else "",
        "glyph": glyph_tex.get_name() if glyph_tex else "",
        "icon": (icon.get_name() if icon else "") if _shown(_sub_widget(slot, "IconImage")) else "(hidden)",
        "lock": _shown(_sub_widget(slot, "LockImage")),
        "progress": _scalar(sweep, "Progress"),
        "rim_flash": _scalar(sweep, "RimFlash"),
        "funded": _scalar(arc, "Funded") if slot.get_name() == "SlotUltimate" else None,
    }


def layout_report(client_index):
    """Barre de sorts du client : viewport, échelle DPI et rectangles en pixels, contrôles §3.3 (marge de 32 px,
    zone centrale x/y 20-80 %, emplacements sans chevauchement ni débordement).
    FGeometry ne passe pas en Python (champs non UPROPERTY) : les rectangles sont déduits de la structure de
    WBP_HUDLayout (SafeZone plein écran, barre centrée en bas, marge = Padding.Bottom de la barre) et des tailles
    souhaitées des widgets, mesurées dans l'image."""
    pc = client_controller(client_index)
    vp = unreal.WidgetLayoutLibrary.get_viewport_size(pc)
    s = unreal.WidgetLayoutLibrary.get_viewport_scale(pc)
    bar = unreal.WidgetLibrary.get_all_widgets_of_class(pc, unreal.GenAbilityBar, False)[0]
    size = bar.get_desired_size()
    margin = bar.get_editor_property("padding").get_editor_property("bottom")
    vw, vh = vp.x / s, vp.y / s
    x0, y0 = (vw - size.x) / 2, vh - size.y
    px = lambda r: tuple(round(v * s, 1) for v in r)
    slots = client_slots(client_index)
    rects, x = {}, x0
    gap = 12.0
    for i, n in enumerate(SLOT_NAMES):
        d = slots[n].get_desired_size()
        rects[n] = px((x, y0, x + d.x, y0 + d.y))
        x += d.x + (gap if i < len(SLOT_NAMES) - 1 else 0)
    bar_px = px((x0, y0, x0 + size.x, vh - margin))
    order = [rects[n] for n in SLOT_NAMES]
    return {
        "viewport": (vp.x, vp.y), "dpi_scale": round(s, 3), "bar_desired": (size.x, size.y), "bottom_margin_layout": margin,
        "bar_px": bar_px, "row_width_layout": round(x - x0, 1),
        "inside_margin": bar_px[0] >= 32 * s and bar_px[2] <= vp.x - 32 * s and bar_px[3] <= vp.y - 32 * s + 0.5,
        "clear_zone_bottom_px": round(0.8 * vp.y, 1), "outside_clear_zone": bar_px[1] >= 0.8 * vp.y,
        "slots_overlap": any(order[i][2] > order[i + 1][0] for i in range(len(order) - 1)),
        "slots_clipped": any(r[0] < 0 or r[1] < 0 or r[2] > vp.x or r[3] > vp.y for r in order),
        "slots_px": rects,
    }


def bar_info(client_index):
    slots = client_slots(client_index)
    return {n: slot_info(slots[n]) for n in SLOT_NAMES if n in slots}


# --- Surveillance de la barre et des barres de cast, image par image --------------------------
# Lignes "GENSLOT" (changements d'un emplacement), "GENSRVCD" (tag de recharge sur le serveur) et
# "GENCAST" (barre de cast vue par un monde), en temps réel rt. Chemins seulement, aucune référence
# PIE gardée. Toujours appeler stop_all_watches() avant StopPIE.

_ui = {"handle": None, "slots": [], "casts": [], "server_cd": [], "last": {}}


def _cast_snapshot(p):
    fill = p.get_cast_progress()
    if fill < 0:
        return ("none",)
    ci = p.get_editor_property("cast_info")
    slots_n = ci.get_editor_property("feed_slots")
    ended = ci.get_editor_property("feed_end_time") > 0
    live = ci.get_editor_property("fed_count") if ended else p.get_fed_resource()
    counter = max(0, min(live, slots_n)) if slots_n else 0
    ticks = (ci.get_editor_property("fed_count") if ended else slots_n) if slots_n else 0
    ab = ci.get_editor_property("ability")
    return (ab.get_name() if ab else "?", round(ci.get_editor_property("start_time"), 3), slots_n,
            ended, counter, ticks, round(fill, 3), round(p.get_resource(), 1))


def _ui_tick(delta_seconds):
    import time
    try:
        rt = time.monotonic()
        for label, path in _ui["slots"]:
            s = unreal.find_object(None, path)
            if s is None:
                continue
            i = slot_info(s)
            prog = None if i["progress"] is None else round(i["progress"], 1)
            flash = None if i["rim_flash"] is None else round(i["rim_flash"], 2)
            snap = (i["state"], i["cd"], i["key"], i["glyph"], i["icon"], i["lock"], prog, flash, i["funded"])
            if _ui["last"].get(label) != snap:
                _ui["last"][label] = snap
                wt = unreal.GameplayStatics.get_time_seconds(s)
                unreal.log("GENSLOT rt=%.3f wt=%.3f %s state=%s cd='%s' key='%s' glyph=%s icon=%s lock=%s prog=%s flash=%s funded=%s" % ((rt, wt, label) + snap))
        for label, path, tag_name in _ui["server_cd"]:
            a = unreal.find_object(None, path)
            if a is None:
                continue
            asc = unreal.AbilitySystemLibrary.get_ability_system_component(a)
            has = asc.has_matching_gameplay_tag(unreal.GameplayTagService.request_tag(tag_name))
            if _ui["last"].get(label) != has:
                _ui["last"][label] = has
                unreal.log("GENSRVCD rt=%.3f %s %s=%s" % (rt, label, tag_name, has))
        for label, path in _ui["casts"]:
            p = unreal.find_object(None, path)
            if p is None:
                continue
            snap = _cast_snapshot(p)
            if _ui["last"].get(label) != snap:
                _ui["last"][label] = snap
                if snap[0] == "none":
                    unreal.log("GENCAST rt=%.3f %s none" % (rt, label))
                else:
                    unreal.log("GENCAST rt=%.3f %s ab=%s start=%s slots=%s ended=%s counter=%s ticks=%s fill=%s fl=%s" % ((rt, label) + snap))
    except Exception as e:
        unreal.log_warning("GENSLOT arrêté : %s" % e)
        ui_watch_stop()


def ui_watch_start(slot_clients=(1,), slot_names=SLOT_NAMES, casts=(), server_cd=()):
    """slot_clients : clients dont on suit la barre. casts : [(label, pion)] dont on suit la barre de cast.
    server_cd : [(label, pion serveur, "Cooldown.Ability.X")] présence du tag de recharge sur le serveur."""
    ui_watch_stop()
    _ui["slots"] = []
    for c in slot_clients:
        slots = client_slots(c)
        _ui["slots"] += [("cl%d.%s" % (c, n), slots[n].get_path_name()) for n in slot_names if n in slots]
    _ui["casts"] = [(label, p.get_path_name()) for label, p in casts]
    _ui["server_cd"] = [(label, p.get_path_name(), tag) for label, p, tag in server_cd]
    _ui["last"] = {}
    _ui["handle"] = unreal.register_slate_post_tick_callback(_ui_tick)
    return [l for l, _ in _ui["slots"]] + [l for l, _ in _ui["casts"]] + [l for l, _, _ in _ui["server_cd"]]


def ui_watch_stop():
    if _ui["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(_ui["handle"])
        _ui["handle"] = None
    _ui["last"] = {}


def stop_all_watches():
    """À appeler avant StopPIE ou un reload du module."""
    schedule_stop()
    ui_watch_stop()
    watch_stop()
