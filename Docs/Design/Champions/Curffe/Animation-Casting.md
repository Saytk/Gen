# Curffe: animation casting log

> Written by Claude (AI) on 2026-10-09, reviewed in the session with the user (who delegated the picks). Check before
> acting on it.

How these were chosen: one brief per spell (feeling, key poses, gameplay window), candidates from the bank compared
side by side, pick, then fine-tune into the existing `AM_*` (names and paths unchanged, so no code or GA change).
Process and tools: `.claude/skills/anim-casting/SKILL.md`. Exact values (frames, durations, sections) live in
`Tools/Anim/sets/<Spell>.json`, which is the source of truth: edit it and re-run
`python Tools/Anim/anim_casting.py <Spell> --apply` to change a spell. Comparison sheets and GIFs (local, not committed):
`.superpowers/sdd/Anim/<Spell>/`.

Bank: Paragon Gideon (mage), Paragon Serath (winged melee), UE5 mannequin. Paragon clips are retargeted onto the
mannequin with `RTG_Gideon_To_Manny` / `RTG_Serath_To_Manny` (local, `Animations/Paragon/_Local/`); only the clips the
montages use are committed. Frames below are at 30 fps in the source clip.

## Round 2 (2026-10-09, judged IN GAME, current state)

Round 1 below was picked from offline silhouettes and the user rejected it in game ("clunky, underwhelming", rigid
legs, a leap with no continuity, a weird walk). Round 2 rules, all judged in PIE with the follow camera
(`Content/Python/gen_anim_pie.py` `rig_capture`, before/after GIFs via `Tools/Anim/ingame_sheet.py --compare`):
- **Always face the cursor** (`gen.AlwaysFaceAim`), so locomotion strafes.
- **Locomotion = GASP mocap** (8-direction walk/run loops + idle, `BS_Curffe_Locomotion`), retargeted with Epic's
  `RTG_UEFN_to_UE5_Mannequin` (my own retargeter had the hips too low: he ran kneeling).
- **Ready stance: removed** (2026-10-09, user: the hands-up guard read as a fist fighter, he should be a nonchalant
  fire mage). Plain GASP locomotion and idle: loose arms, natural swing. Was Lt. Belica `Rmb_Loop` layered at 0.75.
  Idle candidates for a mage flavour later: `Tools/Anim/sets/IdleStance.json`.
- **Full body when standing:** the `UpperBody` slot plays full body when `ShouldMove` is false, layered when moving.
- **Inertialization** everywhere (output node, the standing/moving switch, all state transitions, all Curffe montages).
- **ABP fix:** `DefaultSlot` and the foot-IK Control Rig were not connected (Main States went straight to the `Body`
  cache), so no full-body montage ever played and foot IK was off. Reconnected.
- **No slow-motion stretching:** a charge plays its wind-up at natural speed, then holds the last pose.
- Bank extended to every Paragon hero (Aurora, Countess, Dekker, Fey, Iggy & Scorch, Kallari, Lt. Belica, Muriel,
  Phase), Lyra and GASP, all free. Muriel and Fey are excluded for full-body use (they float).

| Spell | Pick (round 2) |
|---|---|
| LMB Fireball | **Round 4** (Phase: 'deforms him', ~127° torso twist; Muriel reversed flick: 'very stiff'): Gideon `Primary_Attack_C_Medium` baked upper-body-only, torso twist damped (spine 70 %, off arm 50 %) and the throwing shoulder ramped forward during the throw so the arm sweeps toward the target (`Upper/AS_Gideon_Primary_Attack_C_Throw_Upper`): charge f0–4 (fireball brought to the shoulder) + hold, cast f4–14 (release f4–5, 18 m/s, follow-through). **4b** (user: 'jittery'): the charge hold restarted one frame back (20 cm pop) and Gideon's wind-up wobbles: one wind-up segment f0–3 over the charge, bake smoothed f0–5 (`smooth`), cast f3–14 |
| RMB Great Fireball | **Round 3:** Phase (Belica's overhead heave didn't fit, user): feed `RMB_Intro` f0–22 in 3 × 0.3 s + `RMB_Loop` hold, charge = `RMB_Throw` f0–1 held 0.5 s, cast `RMB_Throw` f1–13 (whip). Same hero as LMB and E |
| E Flame Pillar | **Round 5** (user: 'no Paragon hero raises something and puts it down?'): found by motion with `Content/Python/gen_anim_scan.py` (364 spell clips scanned for 'both hands above the head, then a fast drop below the chest'). Serath `R_Ability_Intro`, upper body only: feed and charge = both hands rise overhead (f0–6); release = pull-back held as tension 0.64 s, then the slam (f10–14, 13–15 m/s, torso bends forward) landing at ~0.77 s, just before the explosion (`ImpactDelay` 0.8 s after launch); held low and forward. Runner-up Phase `Ability_R_Alt` (crouch + lunge). Rejected: Aurora `Ability_RMB` (backflip), Dekker `R_Ability_wBoost` (jump). Rounds 3–4 superseded (reversed Phase strike, built by hand) |
| Space Flame Dash | feed: Serath `Q_Ability_Intro` crouch; take-off + dash: Lyra `MM_Dash_Forward` f0–3 / f4–14 |
| Q Backfire | Kallari `Ability_Block`: raise f0–3, guard loop f3–40 |
| R Living Flame | ignite + form: Lt. Belica `R_Ability` f0–15 (arms rise and open); burst: Phase `R_Ability_Intro` f2–11 |
| F Combustion | Aurora `Ability_R_InPlace` (her ultimate): gather f0–18, arms flung wide f20, slam f30–36 (erupt f19–38). **Round 3** (user: "smooth even when walking, he should glide"): both montages on `DefaultSlot` (full body even while moving: the legs hold the pose and he slides at the slowed speed), inertial blend in 0.2 s (charge) and out 0.35 s (eruption) |

### Fix (2026-10-09, late): feet pinned by the foot IK
Symptom: after the GASP switch he "didn't walk anymore": torso ran, feet stayed side by side and slid. Cause, measured
in PIE (`Content/Python/gen_anim_probe.py`): the UE5 mannequin's `ik_foot_l/r` bones are what the ABP's foot-IK
Control Rig aims at. Epic's clips animate them (= the feet); **retargeted clips leave them in the reference pose**, so
the IK planted both feet at (±14, 0) every frame. Same bug, milder (22–104 cm off), in every Paragon/Lyra spell clip:
part of the "rigid legs" during full-body spells. Fix: `gen_anim_montage.sync_ik_bones(paths)` rewrites
`ik_foot_*` and `ik_hand_*` from the real feet/hands on every key. All retarget paths now call it. Applied to the 21
GASP clips and every clip the Curffe montages use.
Also: GASP loops got foot sync markers (`add_foot_sync_markers`, "L"/"R" at each plant, idle gets matching dummies)
so the 8-direction blend lines up steps instead of loop percentages.

## Round 1 (superseded)

| Spell | Pick | Changed montages |
|---|---|---|
| LMB Fireball | Gideon `Primary_Attack_A_Medium` | `AM_Fireball_Charge`, `AM_Fireball_Cast` |
| RMB Great Fireball | Gideon `RMB_Intro_Mirror` → `RMB_Cast_Mirror` | `AM_Curffe_FeedHand` (shared with E), `AM_GreatFireball_Charge`, `_Cast` |
| E Flame Pillar | Gideon `Cosmic_Rift_Intro_Mirror` → `CosmicRift_Execute` | `AM_FlamePillar_Charge`, `_Charge_Fed`, `_Cast` |
| Space Meteor Leap | Serath `Q_Ability_Intro` → `RMB_Dive` → `RMB_Land` | `AM_MeteorLeap_Feed`, `_Takeoff`, `AM_FlameLeap`, `AM_FlameLeap_Land` |
| Q Backfire | **kept** (authored guard) | none |
| R Living Flame | **hybrid**: authored Ignite/Form + Gideon `CosmicRift` burst | `AM_LivingFlame_Burst` (Ignite/Form re-applied unchanged) |
| F Combustion | **kept** (authored) | none (re-applied unchanged) |

## Per spell

### LMB Fireball
- **Brief:** light, snappy one-hand flick, effortless; repeats every 0.65 s, so never tiring. Upper body.
- **Candidates:** current (it was the mannequin boxing jab `MM_Attack_01`), Gideon `Primary_Attack_A/B/C_Medium`, Gideon
  `Cast`, Serath `Primary_Attack_A/B/D_Fast`, mannequin `MM_Attack_01/02`.
- **Pick: Gideon A.** One-arm forward throw; from the top the arm points straight at the target. Serath's are heavy melee
  swings cluttered by the wings; the jab read as a punch.
- **Tune:** Charge = frames 0–4 (off hand forward aiming, throwing hand cocked) stretched to 0.367 s. Cast = frames 4–14 at
  rate 1: the hand snaps out between source frames 3 and 5 (cast frame 1), held extended, 0.333 s.
- **Not done:** alternating A/B/C per shot like Paragon (needs a small GA change). Pyroblast reuses both montages.

### RMB Great Fireball
- **Brief:** building power, then a heavy push; each flame makes the pose stronger.
- **Candidates:** current (Control Rig poses + `AS_GreatFireball_Cast`), Gideon `RMB_Intro/Targeting/Cast`, `Burden_*`,
  mannequin `MM_ChargedAttack`.
- **Pick: Gideon's RMB chain, mirrored** so the right hand throws (the charge VFX sits on `hand_r`, `CastFXSocket`).
  `Burden_Start` was the first idea for the feed (wide two-arm gather) but it spins the torso ~180°, which breaks on the
  upper-body slot.
- **Tune:** Feed_1/2/3 = `RMB_Intro_Mirror` frames 0–4 / 4–8 / 8–13 (0.3 s each; the throwing hand rises up and back),
  Feed_3_Hold = `RMB_Cast_Mirror` frames 0–5 over 0.6 s (near still, seamless loop). Charge = `RMB_Cast_Mirror` 0–13 over
  0.5 s. Cast = 14–26 at rate 1 (release peaks on cast frames 2–4), 0.4 s.
- **Shared feed:** `AM_Curffe_FeedHand` is also the Flame Pillar feed (spec choice). Giving E its own feed is a possible
  readability gain (needs `GA_FlamePillar.FeedMontage` changed).

### E Flame Pillar
- **Brief:** summoning, not throwing: point at the distant spot, then call the pillar down.
- **Candidates:** current (`MM_Attack_02` + Control Rig fed poses), Gideon Cosmic Rift (his own delayed ground AoE)
  `Intro/Targeting/Execute/Start/CosmicRift`, `PortalBlast_*`.
- **Pick: Cosmic Rift chain.** `Cosmic_Rift_Intro_Mirror` (right hand points at the spot) and `CosmicRift_Execute` (both
  hands press down and forward).
- **Tune:** Charge = Intro_Mirror 0–16 over 0.4 s. Fed_1/2/3 = Intro_Mirror from frame 0 / 3 / 6 to 16, each over 0.4 s:
  the more flames, the sooner the point lands and the longer it holds. Cast = Execute 0–9, 0.3 s.
- **Note:** `Cosmic_Rift_Intro_Mirror` has RateScale 1.2 (the tool compensates), and had a stale additive base pose
  pointing at Gideon's `Idle` (cleared; the clip is not additive).

### Space Meteor Leap
- **Brief:** coiled crouch while feeding, explosive take-off, arc, heavy meteor landing. Full body, root motion × 0.
- **Candidates:** current (`MM_Jump`, `MM_WallJump`, `MM_Land`), Serath `Jump_*`, `RMB_Rise/Dive/Land`, `Q_Ability_Intro/Land`,
  Gideon `Jump_In_Place_*`.
- **Pick: Serath.** `Q_Ability_Intro` (deep coiled crouch then spring), `RMB_Dive` (arm raised overhead, a falling meteor),
  `RMB_Land` (superhero slam; its first frame matches the end of `RMB_Dive`).
- **Tune:** Feed_1/2/3 = Q_Intro 0–8 / 8–15 / 15–21 (pelvis 94 → 67 cm), **new section `Feed_3_Hold`** = frames 21–22 over
  0.6 s looping (was `Feed_3` → `Feed_3`, which would pop). Take-off = Q_Intro 24–33 in 0.1 s. Air = `RMB_Dive` 0–29 over
  0.45 s. Land = `RMB_Land` 0–8 in 0.15 s + 20–34 in 0.15 s (slam, then a quick rise so it ends standing: no pop into
  locomotion), 0.3 s total.

### Q Backfire
- **Brief:** defiant frontal guard snapped in 0.1 s, held 1.2 s.
- **Candidates:** current, Serath `E_Ability_Tucked_Pose`, `E_Intro`, `Emote_BringItOn`, `Q_Ability_Targeting_Intro`, Gideon
  `Burden_aim_pose`, `Burden_Intro2`, `Bound`, `Emote_Bravo_Intro`. Serath's were also checked on the mannequin.
- **Pick: keep the current authored guard** (crossed forearms in front of the chest, E7). It reads best from the top;
  Serath's read through her wings, and on the mannequin become a mid-air tuck or an open stance.

### R Living Flame
- **Brief:** dissolving into fire, then bursting outward.
- **Candidates:** current, Gideon `Torn_Space_intro/Torn_Space/Outro/v2` (his blink), `Emote_Master_Levitate`, Serath
  `R_Ability_Intro/Outro`, `E_Ability_Wing_Flap_Holy`.
- **Pick: hybrid.** Ignite + Form keep the authored clip (arms half-open, reads from the top). `Torn_Space` was applied and
  compared: its forward reach reads as a dive. Burst = Gideon `CosmicRift` frames 2–11 at rate 1 (0.3 s, full body), a
  symmetric arms-wide fling, more forceful than the authored burst.

### F Combustion
- **Brief:** gather inward, then erupt outward, head back; the biggest moment of the kit.
- **Candidates:** current, Gideon `Blackhole_Start/Loop` (his ultimate), `PortalBlast*`, `Emote_InfinitePortal`,
  `LevelStart`, Serath `E_Ability_Wing_Flap_Holy`, mannequin `MM_ChargedAttack`.
- **Pick: keep the current authored clip.** `Blackhole_Start` (overhead gather, then arms whipped open with the head back)
  was applied and compared: its torso twist and lunge muddle the top-down silhouette, while the authored hunched
  wide-elbow gather → arms flung flat out is crisp.

## Leads for later
- Alternate Gideon A/B/C on successive Fireballs (Paragon rhythm), small GA change.
- A Flame Pillar feed of its own (`GA_FlamePillar.FeedMontage`), so E and RMB differ from the first frame.
- Locomotion from GASP (`../GameAnimationSample`), Gideon's jog set as the fallback.
- Hit reacts, stun, death (Gideon `HitReact_*`, `Stun_*`, `Death_*`) once Curffe has slots for them.
