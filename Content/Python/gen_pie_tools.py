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
