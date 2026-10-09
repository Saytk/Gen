"""Itération rapide sur un matériau Custom HLSL et un système Niagara (session de travail VFX).

- apply_hlsl(material, fichier) : recopie le code d'un .hlsl dans le nœud Custom du matériau et le compile.
- compiled(system) : True quand la compilation Niagara est finie (à appeler dans des appels séparés :
  la compilation avance pendant que l'éditeur tourne, pas pendant un script Python).
Leçon (2026-10-09) : un PIE lancé avant la fin de la compilation Niagara joue l'ANCIENNE version.
"""
import json

import unreal
import vibeue

TS = "NiagaraToolsets.NiagaraToolset_System"


def apply_hlsl(material, hlsl_file):
    mns = unreal.MaterialNodeService
    g = json.loads(mns.export_material_graph(material))
    cid = [e["id"] for e in g["expressions"] if e["class"] == "Custom"][0]
    code = open(hlsl_file, encoding="utf-8").read()
    ok = mns.set_expression_property(material, cid, "Code", code)
    unreal.MaterialService.compile_material(material)
    diag = mns.get_material_diagnostics(material)
    return ok and diag.is_compiled_ok, str(diag.compile_errors)[:800]


def compiled(system):
    st = vibeue.exec_tool(TS, "GetSystemCompileState", {"system": {"refPath": system}})
    return not st.get("bIsCompiling") and not st.get("bHasErrors"), st.get("aggregateStatus")
