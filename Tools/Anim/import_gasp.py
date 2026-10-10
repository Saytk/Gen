"""Copie des animations du Game Animation Sample (GASP, projet voisin) dans Gen, ÉDITEUR FERMÉ.

Usage : python Tools/Anim/import_gasp.py [--dry-run]
Copie aux mêmes chemins de paquet (/Game/Characters/UEFN_Mannequin/...) pour que les références internes restent valides :
le squelette UEFN, son IK Rig, et les boucles de marche/course dans les 8 directions + l'idle. Ce dossier est une SOURCE
locale (ignorée par git, comme les packs Paragon) : on retarget ensuite sur le mannequin de Gen
(Content/Python/gen_anim_montage.py retarget_gasp) et seuls les clips retargetés sont commités.
"""
import argparse
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
GASP = os.path.join(os.path.dirname(PROJECT), "GameAnimationSample", "Content")
DST = os.path.join(PROJECT, "Content")

DIRS = ["Run", "Walk"]
LOOPS = ["F", "FL", "FR", "B", "BL", "BR", "LL", "LR", "RL", "RR"]
FILES = (
    ["Characters/UEFN_Mannequin/Meshes/" + f for f in ("SK_UEFN_Mannequin.uasset", "SKM_UEFN_Mannequin.uasset", "Character_LodSettings.uasset")]
    + ["Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin.uasset",
       # retargeter UEFN -> UE5 réglé par Epic (poses de retarget, bassin) ; cible repointée sur le mannequin de Gen
       "Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin.uasset",
       "Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget.uasset"]
    + [f"Characters/UEFN_Mannequin/Animations/{d}/M_Neutral_{d}_Loop_{s}.uasset" for d in DIRS for s in LOOPS]
    + ["Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.uasset",
       "Misc/SandboxAnimCurveCompressionSettings.uasset"]
    # Références laissées manquantes exprès : notifies de bruitage GASP (/Game/Blueprints/AnimNotifies/...) et bases
    # Pose Search (PSD_*) ; les clips retargetés n'en gardent pas (gen_anim_montage.check_leaks le vérifie).
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    missing = [f for f in FILES if not os.path.exists(os.path.join(GASP, f))]
    if missing:
        print("missing in GASP:", missing)
    n = 0
    for f in FILES:
        src, dst = os.path.join(GASP, f), os.path.join(DST, f)
        if not os.path.exists(src):
            continue
        if os.path.exists(dst):
            continue  # déjà copié : ne jamais écraser
        n += 1
        print(("would copy " if a.dry_run else "copy ") + f)
        if not a.dry_run:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
    print(f"{n} file(s) {'to copy' if a.dry_run else 'copied'}")


if __name__ == "__main__":
    main()
