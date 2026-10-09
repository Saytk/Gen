"""Recettes Niagara réutilisables (outils Niagara d'Epic via vibeue.exec_tool).

Recette « traînée de flammes » validée sur la boule de feu (NS_Curffe_ST_FireballV3, 2026-10-09) :
deux rubans Vefects (NE_Trail + MI_VFX_Trail_Fire_01), émis par seconde (SpawnPerUnit n'émet rien sur un
composant de projectile), recolorés par la courbe orange → rouge de NS_Trail_Fire (sinon ils sortent blancs).
"""
import json

import unreal
import vibeue

TS = "NiagaraToolsets.NiagaraToolset_System"
FLT = "/Script/Niagara.NiagaraFloat"
COL = "/Script/CoreUObject.LinearColor"
NE_TRAIL = "/Game/ThirdParty/Vefects/Essential_Trails/Shared/Particles/_Emitters/NE_Trail.NE_Trail"
VEFECTS_FIRE = "/Game/ThirdParty/Vefects/Essential_Trails/Shared/Particles/NS_Trail_Fire.NS_Trail_Fire"
MI_FIRE = "/Game/ThirdParty/Vefects/Essential_Trails/Shared/Materials/MI_VFX_Trail_Fire_01.MI_VFX_Trail_Fire_01"


def ref(system, emitter="", script="", module="", renderer=-1, inputs=None):
    return {"system": {"refPath": system}, "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": inputs or []}


def set_input(system, emitter, script, module, name, struct, value):
    vibeue.exec_tool(TS, "SetStackInputData", {"stackInputRef": ref(system, emitter, script, module, -1, [name]),
                                               "inputData": {"struct": {"refPath": struct}, "value": value}})


def set_float(system, emitter, script, module, name, value):
    set_input(system, emitter, script, module, name, FLT, {"value": value})


def create_from(template, asset_path, name):
    """Crée (si absent) un système copié de `template` ; renvoie son chemin d'objet."""
    if not unreal.EditorAssetLibrary.does_asset_exist("{}/{}".format(asset_path, name)):
        vibeue.exec_tool(TS, "CreateNiagaraSystem", {"assetName": name, "assetPath": asset_path,
                                                     "templateSystem": {"refPath": template}})
    return "{}/{}.{}".format(asset_path, name, name)


def set_bp_niagara(bp_path, component, system_asset_path):
    """Pointe le composant Niagara `component` d'un Blueprint sur un système, compile et enregistre (verrou LFS avant !)."""
    bp = unreal.load_asset(bp_path)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        o = lib.get_associated_object(lib.get_data(h))
        if o and o.get_name() == component:
            o.set_editor_property("asset", unreal.load_asset(system_asset_path))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    now = cdo.get_editor_property(component).get_editor_property("asset").get_name()
    saved = unreal.EditorAssetLibrary.save_asset(bp_path, False)
    return now, saved


def emitter_names(system):
    return [e["emitterName"] for e in vibeue.exec_tool(TS, "GetSystemSummary", {"system": {"refPath": system}})["emitters"]]


def _fire_curve():
    vals = vibeue.exec_tool(TS, "GetEmitterInputValues", {"emitterRef": ref(VEFECTS_FIRE, "NE_Trail")})
    return [i for m in vals if m["moduleName"] == "ScaleColor" for i in m["inputs"] if i["name"] == "Linear Color Curve"][0]["value"]


def add_flame_trail(system, width=64, life=0.35, rate=70, hot_width=28, hot_life=0.2, hot_rate=50, tiling=320,
                    hot_color=(1.4, 0.75, 0.25)):
    """Ajoute FlameTrail (large, orange → rouge) et FlameTrailHot (étroit, plus chaud) à `system`."""
    existing = emitter_names(system)
    curve = _fire_curve()
    pv = json.loads(curve["value"]["propertyValues"])
    hot = json.loads(json.dumps(pv))
    for k, g in zip(hot["GreenCurve"]["keys"], [0.75, 0.4, 0.12]):
        k["value"] = g
    for k, b in zip(hot["BlueCurve"]["keys"], [0.3, 0.05, 0.05]):
        k["value"] = b
    for name, w, l, r, til, curve_pv, color in [("FlameTrail", width, life, rate, tiling, pv, None),
                                                ("FlameTrailHot", hot_width, hot_life, hot_rate, int(tiling * 0.7), hot, hot_color)]:
        if name not in existing:
            vibeue.exec_tool(TS, "AddEmitter", {"system": {"refPath": system}, "templateEmitter": {"refPath": NE_TRAIL},
                                                "emitterName": name})
        set_float(system, name, "EmitterUpdateScript", "SpawnRate", "SpawnRate", r)
        set_float(system, name, "EmitterUpdateScript", "SpawnPerUnit", "Spawn Spacing", 1000)
        set_float(system, name, "ParticleSpawnScript", "InitializeRibbon", "Lifetime", l)
        set_float(system, name, "ParticleSpawnScript", "InitializeRibbon", "Ribbon Width", w)
        if color:
            set_input(system, name, "ParticleSpawnScript", "InitializeRibbon", "Color", COL,
                      {"r": color[0], "g": color[1], "b": color[2], "a": 1})
        vibeue.exec_tool(TS, "SetStackInputData", {
            "stackInputRef": ref(system, name, "ParticleUpdateScript", "ScaleColor", -1, ["Linear Color Curve"]),
            "inputData": {"struct": curve["struct"], "value": {"propertyValues": json.dumps(curve_pv)}}})
        d = vibeue.exec_tool(TS, "GetRendererData", {"rendererRef": ref(system, name, renderer=0)})
        uv0 = json.loads(d["propertyValues"])["UV0Settings"]
        uv0["tilingLength"] = til
        vibeue.exec_tool(TS, "SetRendererData", {"renderer": ref(system, name, renderer=0), "rendererData": {
            "propertyValues": json.dumps({"Material": {"refPath": MI_FIRE}, "UV0Settings": uv0})}})
    return emitter_names(system)


FLAME_MI = "/Game/MegaMagicVFXBundle/VFX/MagicShieldsVFX/VFX/DefaultVersions/FlameShield/Materials/MI_FlameShieldFlames.MI_FlameShieldFlames"
SUBUV = "/Niagara/Modules/Update/SubUV/V2/SubUVAnimation.SubUVAnimation"
FOUNTAIN = "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain"


def _set_enum_by_label(system, emitter, script, module, name, wanted):
    """Règle une entrée enum par son libellé (essaie NewEnumerator0..9 et relit le libellé affiché)."""
    def current():
        vals = vibeue.exec_tool(TS, "GetEmitterInputValues", {"emitterRef": ref(system, emitter)})
        return [i for m in vals if m["moduleName"] == module for i in m["inputs"] if i["name"] == name][0]["value"]
    cur = current()
    enum = cur["value"]["enum"]
    for n in range(10):
        v = {"struct": cur["struct"], "value": {"enum": enum, "enumName": "{}::NewEnumerator{}".format(enum["refPath"].split(".")[-1], n)}}
        try:
            vibeue.exec_tool(TS, "SetStackInputData", {"stackInputRef": ref(system, emitter, script, module, -1, [name]), "inputData": v})
        except Exception:
            continue
        if wanted.lower() in current()["value"]["displayName"].lower():
            return True
    return False


def add_flame_column(system, name="FlameColumn", radius=40, rate=220, loop=0.2, life=(0.3, 0.5), size=(70, 125),
                     speed=(320, 720), cone=10, rise=250, drag=1.5, color=(20, 1.92, 0), torus_radius=None):
    """Bouffée de flammes animées (planche 6x6 du FlameShield) qui monte : colonne (sphère) ou anneau (tore).
    Dimensions à l'échelle 1 du système (les zones mettent le composant à rayon / rayon de référence)."""
    if name not in emitter_names(system):
        vibeue.exec_tool(TS, "AddEmitter", {"system": {"refPath": system}, "templateEmitter": {"refPath": FOUNTAIN}, "emitterName": name})
    vibeue.exec_tool(TS, "SetRendererData", {"renderer": ref(system, name, renderer=0), "rendererData": {"propertyValues": json.dumps(
        {"Material": {"refPath": FLAME_MI}, "SubImageSize": {"x": 6, "y": 6}, "bSubImageBlend": True})}})
    mods = [m["moduleName"] for m in vibeue.exec_tool(TS, "GetEmitterInputValues", {"emitterRef": ref(system, name)})]
    if "SubUVAnimation" not in mods:
        vibeue.exec_tool(TS, "AddModule", {"moduleLocationRef": ref(system, name, "ParticleUpdateScript", "SolveForcesAndVelocity"),
                                           "moduleAsset": {"refPath": SUBUV}})
    EU, PS, PU = "EmitterUpdateScript", "ParticleSpawnScript", "ParticleUpdateScript"
    _set_enum_by_label(system, name, EU, "EmitterState", "Loop Behavior", "Once")
    set_float(system, name, EU, "EmitterState", "Loop Duration", loop)
    set_float(system, name, EU, "SpawnRate", "SpawnRate", rate)
    set_float(system, name, PS, "InitializeParticle", "Lifetime Min", life[0])
    set_float(system, name, PS, "InitializeParticle", "Lifetime Max", life[1])
    set_float(system, name, PS, "InitializeParticle", "Uniform Sprite Size Min", size[0])
    set_float(system, name, PS, "InitializeParticle", "Uniform Sprite Size Max", size[1])
    if torus_radius:
        _set_enum_by_label(system, name, PS, "ShapeLocation", "Shape Primitive", "Torus")
        set_float(system, name, PS, "ShapeLocation", "Large Radius", torus_radius)
        set_float(system, name, PS, "ShapeLocation", "Handle Radius", radius)
    else:
        set_float(system, name, PS, "ShapeLocation", "Sphere Radius", radius)
    set_float(system, name, PS, "AddVelocity", "Cone Angle", cone)
    vibeue.exec_tool(TS, "SetStackInputData", {"stackInputRef": ref(system, name, PS, "AddVelocity", -1, ["Velocity Speed", "Minimum"]),
                                               "inputData": {"struct": {"refPath": FLT}, "value": {"value": speed[0]}}})
    vibeue.exec_tool(TS, "SetStackInputData", {"stackInputRef": ref(system, name, PS, "AddVelocity", -1, ["Velocity Speed", "Maximum"]),
                                               "inputData": {"struct": {"refPath": FLT}, "value": {"value": speed[1]}}})
    set_input(system, name, PU, "GravityForce", "Gravity", "/Script/CoreUObject.Vector3f", {"x": 0, "y": 0, "z": rise})
    set_float(system, name, PU, "Drag", "Drag", drag)
    set_input(system, name, PU, "ScaleColor", "Scale RGB", "/Script/CoreUObject.Vector3f", {"x": color[0], "y": color[1], "z": color[2]})
    return emitter_names(system)


def set_ribbon_width_curve(system, emitter, keys):
    """Courbe de largeur du ruban sur l'âge normalisé (NE_Trail : 1 → 3, il s'élargit). keys = [(age, échelle), ...]."""
    chain = vibeue.exec_tool(TS, "GetDynamicInputChain", {
        "stackInputRef": ref(system, emitter, "ParticleUpdateScript", "ScaleRibbonWidth", -1, ["Ribbon Width Scale"])})
    curve_in = [c for c in chain["value"]["inputs"] if c["value"]["name"] == "FloatCurve"][0]["value"]["value"]
    pv = json.loads(curve_in["value"]["propertyValues"])
    template = pv["Curve"]["keys"][0]
    pv["Curve"]["keys"] = [dict(template, time=t, value=v) for t, v in keys]
    vibeue.exec_tool(TS, "SetStackInputData", {
        "stackInputRef": ref(system, emitter, "ParticleUpdateScript", "ScaleRibbonWidth", -1, ["Ribbon Width Scale", "FloatCurve"]),
        "inputData": {"struct": curve_in["struct"], "value": {"propertyValues": json.dumps(pv)}}})
