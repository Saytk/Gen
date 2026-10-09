# Handoff: next session (Curffe)

> Written by Claude (AI) on 2026-10-08 at end of day. Check before acting on it.

## Read first

- `CLAUDE.md`, sections "Processus d'implémentation : vite d'abord" and "Moins d'allers-retours MCP". The
  user wants speed:
  - short plans, feature batches, one targeted review;
  - tests only for gameplay/network rules;
  - a single visual check at the end;
  - one editor restart per C++ batch;
  - no blocking questions.
- `Docs/Design/Champions/Curffe/Remaining.md`: the C++ and measurement leftovers (flaky test, energy flicker,
  ping-based grace, VFX perf in a packaged build, final 3-client matrix), plus the decisions to confirm.
- Memory: subagents on Opus; VFX never replaced without a before/after capture.

## Repo state

- `main` = `ui-ability-bar` on GitHub, latest pushed commit `b82f039` (Erwan's VFX pass, fast-forwarded).
- Local commits not pushed:
  - `abe22b1`: ignore the Paragon packs;
  - `4a30eee`: ignore the exploration packs;
  - this handoff.
- No LFS lock held. The worktree `../Gen-curffe` is clean, on `b82f039`. It can be removed; it's only
  useful for C++ batches in parallel with the editor.
- **Uncommitted, to decide:**
  - `Gen.uproject`: the launcher changed `EngineAssociation` from the GUID to `"5.8"` when the packs were
    added. That fixes the mismatch between machines; agree with Erwan before committing.
  - `Content/Gen/Maps/L_Arena.umap`: modified at 23:30, probably by the animation agent during its tests.
    Inspect it, and restore it (`git restore`) unless the change is wanted.
  - 37 retargeted animations, untracked, in `Content/Gen/Champions/Curffe/Animations/Paragon/`
    (`AS_Gideon_*`, `AS_Serath_*`, plus a few `*_Mirror`). Commit only those the montages use.

## Animation work in progress (resume here)

**Goal:** AAA animations for Curffe from Paragon, retargeted onto the UE5 mannequin.

**Step 1, done (local):**
- IK rigs `IK_Gideon`, `IK_Serath`, `IK_Manny` and retargeters `RTG_Gideon_To_Manny`, `RTG_Serath_To_Manny`
  in `Animations/Paragon/_Local/` (git-ignored: they reference the ignored packs).
- 37 clips retargeted into `Animations/Paragon/`. Retarget quality (feet, hands) hasn't been checked
  visually yet.

**Step 2, not started: rebuild Curffe's montages on these clips.**
- Keep the existing names and paths (`AM_*` in `Content/Gen/Champions/Curffe/Animations/`), so no code or
  GA changes are needed.
- Take an LFS lock on each `AM_*` first.

| Spell | Clips |
|---|---|
| LMB Fireball | `Primary_Attack_A/B/C_Medium` |
| RMB Great Fireball | `RMB_Intro` (feed + charge) → `RMB_Cast` |
| E Flame Pillar | `Cosmic_Rift_Intro` → `Cosmic_Rift_Targeting` → `CosmicRift_Execute` |
| F Combustion | `Blackhole_Start` / `BlackHole_Loop` → `PortalBlast` (or portal_blast_fast/medium) |
| R Living Flame | `Torn_Space_intro` → `Torn_Space` → `Torn_Space_Outro` |
| Q Backfire | `Burden_Start` / `Burden_Loop` |
| Space Meteor Leap | Serath `Q_Ability_Intro` / `Jump_Start` → `Jump_Apex` → `RMB_Dive` → `RMB_Land`, root motion OFF |
| Bonus | `Stun_*`, `HitReact_*`, `Death_*`, only if Curffe already has slots for them |

**Montage contract** (`Plan-Visuals.md` E5, `Curffe-Visuals.md`):
- `Feed_1`/`Feed_2`/`Feed_3` exactly 0.3 s each, plus a looping `Feed_3_Hold`.
- `AM_FlamePillar_Charge_Fed` with `Fed_1..3`.
- All phase montages of one spell in the same slot group.
- Blend-out trigger 0 on the charge phases.
- Cast times (`Curffe.md`): Fireball 0.35 s plus a 0.30 s cooldown after the cast; Great Fireball
  0.5 + 0.3/flame; Pillar 0.4 + 0.3/flame; leap 0.1 + 0.3/flame and 0.45 s flight; Backfire 0.1 + 1.2 s
  window; Living Flame 0.1 + 0.5 s form; Combustion 0.5 s.

**Step 3:** one visual check at the end, with the contact sheet (`Tools/VFX/vfx_capture.py` +
`MakeContactSheet.ps1`, which work for animation too). Output in `.superpowers/sdd/Anim/`.

**Step 4:**
- Tests: `StartsWith:Gen.Visuals` and `StartsWith:Gen.Net.FeedVisuals` only.
- Commit only the `AM_*` files and the clips they use.
- Before committing, check that no committed asset references `/Game/ParagonGideon`, `/Game/ParagonSerath`
  or `_Local`.

**Next: locomotion.** Use the GASP project (`../GameAnimationSample`, 7 GB, made for the UE5 mannequin,
motion matching) instead of the mannequin's basic locomotion. Copy only the clips Curffe uses into Gen.
Gideon's own jog set is the fallback if a consistent style matters more.

## Packs available (all git-ignored, all readable)

| Pack | Location | Contents |
|---|---|---|
| Paragon Gideon | `Content/ParagonGideon` (2.8 GB) | 224 anims, Cascade FX |
| Paragon Serath | `Content/ParagonSerath` (2.1 GB) | 194 anims, Cascade FX |
| Free_Magic | `Content/Free_Magic` (516 MB) | 17 Niagara, UE 5.0 |
| Vefects Easy Shockwaves | `Content/Vefects` (164 MB) | 84 Niagara shockwaves |
| Mega Magic VFX Bundle | `Content/MegaMagicVFXBundle` (125 MB) | 61 Niagara; Erwan's 4 files stay versioned |
| FX Variety Pack | `Content/FXVarietyPack` (88 MB) | 21 Cascade systems; convert to Niagara before adapting |
| Vefects Essential Trails | `Content/ThirdParty/Vefects` | versioned, Erwan's |
| GASP | `../GameAnimationSample` | separate project, outside the repo |

- **Not yet verified:** that everything loads in the editor (asset registry, opening one system per pack).
  Do it at the start of the session.
- **VFX rule discussed with the user:**
  - packs are ingredients;
  - while the style is still being explored, try things in a sandbox (`Content/Developers/<name>/`);
  - once a direction is chosen, convert to the master materials and commit only what's used.
- The visual style isn't settled: taste #5 (Stylized) and Erwan's #6 (flame trails) are the current direction.
- The user asked whether cel-shading could be applied to pack VFX. Yes: posterized material, hard alpha, dark
  rim. A `Content/Gen/VFX/CelShade` prototype already exists.

## Editor

- `Gen` editor open on L_Arena (it may have unsaved changes from the stopped agent).
- MCP: the `unreal-mcp` tools may not connect in the session. Use a JSON-RPC client on
  `http://127.0.0.1:8010/mcp`. `.mcp.json` now sets a 15 min timeout; it takes effect after a `/mcp` reconnect.
