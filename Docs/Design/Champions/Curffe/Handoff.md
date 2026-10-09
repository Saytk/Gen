# Handoff: next session (Curffe animations)

> Written by Claude (AI) on 2026-10-09. Check before acting on it.

## Read first

- `CLAUDE.md`, sections "Processus d'implémentation : vite d'abord" and "Moins d'allers-retours MCP". The user wants
  speed: short plans, feature batches, one visual check at the end, one editor restart per C++ batch, no blocking questions.
- Memory: subagents on Opus; **VFX is Erwan's** (don't touch Niagara systems or VFX materials, pass leads to him).
- The user wants to agree on the plan before work starts: post a short plan, then go.

## Repo state

- Everything is on `main` and pushed (merge of Erwan's `dev-panel` + this session's work). Gen. suite 168/168.
- No lock held by Samy. Erwan holds `M_Curffe_FlameComet` and `NS_Curffe_ST_GreatFireballV4`.
- Local only, on purpose:
  - `Plugins/VibeUE`: the unattended-Python patch (`Tools/Patches/VibeUE-unattended-python.patch`).
  - `Content/Gen/Champions/Curffe/Animations/Paragon/`: 37 retargeted clips + `_Local/` (IK rigs and retargeters,
    git-ignored). **Commit only the clips the montages use.**
- `Gen.uproject` now says `"EngineAssociation": "5.8"` (committed).

## Talking to the editor

- The `unreal-mcp` tools did not connect in the last two sessions (ECONNREFUSED at session start, the editor wasn't up).
  Use `python Tools/Mcp/mcp_call.py` (JSON-RPC on `127.0.0.1:8010/mcp`): `--py file.py` for `execute_python_code`,
  `call_tool '{"toolset_name":..., "tool_name":..., "arguments":{}}'` for engine toolsets. Set `PYTHONIOENCODING=utf-8`.
- Editor path: `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "<project>\Gen.uproject"`.
  Ready when the newest `Saved/VibeUE/Signals/editor-<pid>-health.json` says `"mcpListening": true`.
- Gotchas met this session:
  - Assets can't be saved while PIE runs (`save_asset` returns False): stop PIE first.
  - Changing a **parent** Blueprint default propagates to children that don't override it (GA_Fireball →
    GA_GreatFireball; BP_Projectile_Fireball → GreatFireball, Pyroblast). Check the children after editing a parent.
  - PIE is Standalone, 1 player. `InputService.inject_action(_for)` works with PIE instance 0; aim indicators
    don't show with injected input (no real mouse over the viewport). `pawn.add_movement_input` from a
    `register_slate_post_tick_callback` drives movement reliably.
  - For motion-sensitive captures use the console `shot` (viewport frame, TSR history kept), not `HighResShot`.

## Animation work (resume here)

**Goal:** AAA animations for Curffe from Paragon (Gideon, Serath), retargeted onto the UE5 mannequin.

**Step 1, done (local):** IK rigs `IK_Gideon`, `IK_Serath`, `IK_Manny`, retargeters `RTG_Gideon_To_Manny`,
`RTG_Serath_To_Manny` in `Animations/Paragon/_Local/`; 37 clips retargeted into `Animations/Paragon/`.
Retarget quality (feet, hands) **not checked visually yet**: check it on 2–3 clips first.

**Step 2, to do: rebuild Curffe's montages on these clips.**
- Keep the names and paths of the 21 `AM_*` in `Content/Gen/Champions/Curffe/Animations/` (no code or GA change).
  Lock each one before editing (`git lfs lock`).
- Proposed order: **Fireball first, show the user a before/after capture**, then the other spells.

| Spell | Montages | Clips |
|---|---|---|
| LMB Fireball | `AM_Fireball`, `_Charge`, `_Cast` | `Primary_Attack_A/B/C_Medium` |
| RMB Great Fireball | `AM_GreatFireball`, `_Charge`, `_Cast`, feed `AM_Curffe_FeedHand` | `RMB_Intro` (feed + charge) → `RMB_Cast` |
| E Flame Pillar | `AM_FlamePillar_Charge`, `_Charge_Fed`, `_Cast` (feed shared `AM_Curffe_FeedHand`) | `Cosmic_Rift_Intro` → `Cosmic_Rift_Targeting` → `CosmicRift_Execute` |
| F Combustion | `AM_Combustion_Charge`, `_Erupt` | `Blackhole_Start` / `BlackHole_Loop` → `PortalBlast` (or `portal_blast_fast/medium`) |
| R Living Flame | `AM_LivingFlame_Ignite`, `_Form`, `_Burst` | `Torn_Space_intro` → `Torn_Space` → `Torn_Space_Outro` |
| Q Backfire | `AM_Backfire_Raise`, `_Guard` | `Burden_Start` / `Burden_Loop` |
| Space Meteor Leap | `AM_MeteorLeap_Feed`, `_Takeoff`, `AM_FlameLeap`, `AM_FlameLeap_Land` | Serath `Q_Ability_Intro` / `Jump_Start` → `Jump_Apex` → `RMB_Dive` → `RMB_Land`, root motion scale 0 |
| Bonus | only if Curffe has slots | `Stun_*`, `HitReact_*`, `Death_*` |

**Montage contract** (`Curffe-Visuals.md` §1.4, `Plan-Visuals.md` E5):
- Play rate = `clamp(L / T, 0.5, 2.5)`, warning outside [0.8, 1.25]: **retime the sections to the gameplay times**
  (segment play rate or trimming), don't rely on the runtime rate. `L` is read with `GetSectionLength`.
- Feed: `Feed_1`/`Feed_2`/`Feed_3` exactly 0.3 s each, linked in order, `Feed_3` → looping `Feed_3_Hold` (0.6 s,
  seamless). `AM_FlamePillar_Charge_Fed` has `Fed_1..3`.
- Charge ends exactly at the release; Cast has its action pose on frames 0–2, follow-through ≤ 0.3–0.4 s.
- Times (`Curffe.md`): Fireball 0.35 s cast + 0.30 s cooldown after the cast; Great Fireball 0.5 + 0.3/flame; Pillar
  0.4 + 0.3/flame; leap 0.1 + 0.3/flame and 0.45 s flight; Backfire 0.1 + 1.2 s window; Living Flame 0.1 + 0.5 s form;
  Combustion 0.5 s.
- Slots: hand spells and Backfire `UpperBody`; leap and Living Flame burst `DefaultSlot`; Combustion `UpperBody`.
  All phase montages of one spell in the same slot group. Blend-out trigger 0 on charge phases.
- Notifies: cosmetic only (`AnimNotify_GameplayCue` on frame 0 of each Cast montage, `bTriggerOnDedicatedServer` off).
- Backfire now keeps facing the cursor for the whole window (commit `1dd46cc`): the guard pose can be frontal.

**Step 3:** one visual check at the end, contact sheet (`Tools/VFX/vfx_capture.py` + `MakeContactSheet.ps1` work for
animation too), output in `.superpowers/sdd/Anim/`.

**Step 4:** tests `StartsWith:Gen.Visuals` and `StartsWith:Gen.Net.FeedVisuals`; commit only the `AM_*` and the clips
they use; before committing, check no committed asset references `/Game/ParagonGideon`, `/Game/ParagonSerath` or `_Local`.

**Next after that: locomotion** from GASP (`../GameAnimationSample`, motion matching, made for the UE5 mannequin);
copy only the clips Curffe uses. Gideon's own jog set is the fallback.

## For Erwan (VFX, not ours)

- The Fireball looks blurry in flight. Lead: its three trail ribbons (`FlameTrail`, `FlameTrailHot`, `CoreTrail`) use
  the Vefects master `M_VFX_AdvTrail`, which has **refraction wired** (distortion). The head sprites are translucent or
  additive with no velocity output, so TSR may soften them too. Not measured to the end (stopped on the user's request).
- Project-wide changes that affect VFX: motion blur off (`r.DefaultFeature.MotionBlur=False`); `MPC_TeamColours.Enemy`
  is now red; telegraphs have no keyline and self uses the ally blue (Art Bible taste #13).
