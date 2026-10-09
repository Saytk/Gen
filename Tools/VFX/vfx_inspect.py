"""Lecture compacte des réglages d'un système Niagara (outils Niagara d'Epic via vibeue.exec_tool).

Usage : import vfx_inspect as vi ; print(vi.dump(path, ["Glow", "CoreTrail"]))
"""
import json

import vibeue

TS = "NiagaraToolsets.NiagaraToolset_System"
SKIP_MODULES = {"EmitterState"}


def _short(v):
    """Valeur lisible et courte d'une entrée de pile."""
    s = v.get("struct", {}).get("refPath", "").split(".")[-1]
    val = v.get("value", {})
    if "Unsupported" in s:
        return None
    if "Enum" in s:
        return val.get("displayName")
    if "Linked" in s:
        return "@" + val.get("linkedVariable", {}).get("name", "?")
    if "Dynamic" in s or "Expression" in s or "HLSL" in s:
        return s + ":" + json.dumps(val)[:160]
    if isinstance(val, dict) and "value" in val and len(val) == 1:
        x = val["value"]
        return round(x, 3) if isinstance(x, float) else x
    if isinstance(val, dict) and set(val) <= {"x", "y", "z", "w", "r", "g", "b", "a"}:
        return [round(val[k], 3) for k in val]
    return s + ":" + json.dumps(val)[:120]


def summary(system):
    return vibeue.exec_tool(TS, "GetSystemSummary", {"system": {"refPath": system}})


KEY_WORDS = ("Count", "Lifetime", "Size", "Speed", "Color", "Radius", "Scale", "Rate", "Gravity", "Drag",
             "Velocity", "Width", "Offset", "Cone", "Shape Primitive", "Strength", "Frequency", "Value")


def _key(name, val):
    if isinstance(val, str) and val.startswith(("NiagaraExt_StackInputData_HlslExpression", "NiagaraExt_StackInputData_Dyn")):
        return True
    if isinstance(val, str) and val.startswith("@"):
        return False
    return any(w in name for w in KEY_WORDS) and "Mode" not in name and "Randomness" not in name


def dump(system, emitters=None, modules=None, key_only=False):
    """{emitter: {module: {input: valeur}}} sans les entrées non gérées (key_only : réglages utiles seulement)."""
    out = {}
    names = emitters or [e["emitterName"] for e in summary(system)["emitters"]]
    for e in names:
        vals = vibeue.exec_tool(TS, "GetEmitterInputValues", {"emitterRef": {
            "system": {"refPath": system}, "emitterName": e, "scriptName": "", "moduleName": "",
            "rendererIndex": -1, "inputNameStack": []}})
        mods = {}
        for m in vals:
            if m["moduleName"] in SKIP_MODULES or (modules and m["moduleName"] not in modules):
                continue
            ins = {}
            for i in m["inputs"]:
                s = _short(i["value"])
                if s is not None and (not key_only or _key(i["name"], s)):
                    ins[i["name"]] = s
            if ins or not key_only:
                mods[m["moduleName"]] = ins
        out[e] = mods
    return out


def renderer(system, emitter, index=0, keys=None):
    d = vibeue.exec_tool(TS, "GetRendererData", {"rendererRef": {
        "system": {"refPath": system}, "emitterName": emitter, "scriptName": "", "moduleName": "",
        "rendererIndex": index, "inputNameStack": []}})
    pv = json.loads(d["propertyValues"])
    return {k: pv[k] for k in (keys or pv) if k in pv}
