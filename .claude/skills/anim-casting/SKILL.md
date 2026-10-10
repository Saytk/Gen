---
name: anim-casting
description: Use when choosing or replacing a champion spell's animation (Curffe or any champion) - brief the spell's feeling, compare candidate clips from the animation bank side by side, pick, then fine-tune into the AM_* montages.
---

# Animation casting (spell → right clip → fine-tune)

Process agreed with the user on 2026-10-09. One spell at a time. Decisions so far, with the reasons and exact values:
`Docs/Design/Champions/Curffe/Animation-Casting.md` (keep it up to date: the user wants every choice documented).

All commands: from the project root, editor open on 8010, `PYTHONIOENCODING=utf-8 python Tools/Anim/anim_casting.py <Set> ...`.
`<Set>` = `Tools/Anim/sets/<Set>.json` (brief, candidates, pick, montage spec, sequences). The JSON is the source of truth.

1. **Brief.** From the champion spec and the visuals spec (§ Animation of each spell): feeling, key poses, gameplay window.
   Put it in the set's `brief`; get the user's OK on the feeling (unless they delegated the picks).
2. **Candidates.** Search by MOTION first, not only by name: `gen_anim_scan.start(motif)` scans every Paragon spell clip in the editor (add a motif function to `MOTIFS`, e.g. `raise_slam`) → `Saved/AnimScan/<motif>.json`. Naming-only search missed Serath's raise-and-slam for E (user: 'no Paragon hero has a spell that raises something and puts it down?'). Then preview the top hits on the character in PIE (`play_slot_animation_as_dynamic_montage`).
    `clips` {label: source path}. Bank: Gideon (mage) `/Game/ParagonGideon/Characters/Heroes/Gideon/Animations/`,
   Serath (winged melee, jumps/dives) `/Game/ParagonSerath/Characters/Heroes/Serath/Animations/`, mannequin
   `/Game/Characters/Mannequins/Anims/`, GASP (`../GameAnimationSample`, locomotion, other project). Always include the
   current clip as `0_Current`. Source clips need no retarget to compare. Many Paragon clips have a `_Mirror` twin.
3. **Compare.** `anim_casting.py <Set>` → `.superpowers/sdd/Anim/<Set>/candidates_<angle>.png|.gif|_half.gif`.
   Read the sheets (three_quarter = feeling, top = what players read); open the GIFs for the user (`start "" <gif>`).
   Silhouettes on purpose (the capture service renders the mesh black). Wings (Serath) and Gideon's ring are mesh, not
   motion: to judge a Serath pose without wings, `--analyse` it (retargets) and add the retargeted path as a candidate.
4. **Pick and measure.** `--analyse Gideon:Clip[,Serath:Clip] [--bones hand_r,hand_l:spine_05] [--every 2]` retargets the
   clip if needed and prints bone positions per frame (mannequin: side = X, forward = +Y, up = +Z) and speed: find the
   release (speed peak / extension), the crouch depth (`pelvis:root`), loop points (near-equal frames).
   Check `CastFXSocket` (default `hand_r`): use the `_Mirror` clip if the source throws with the left hand.
5. **Fine-tune.** Fill `montages` in the set: `{AM_name: [segment...]}` or `{AM_name: {"segments": [...], "sections":
   [[name, start, next], ...]}}`; segment = `src` (`Gideon:Clip`, `Serath:Clip` or a `/Game` path), `f` [from, to] frames
   at 30 fps (or `t` seconds), `duration` (target length) or `rate`. Clip `RateScale` is compensated. Keep the montage
   contract (visuals spec §1.4): charge ends at the release, cast action on frames 0–2, feed sections 0.3 s, section
   names unchanged, blend-out trigger 0 on charge phases. Dict form also takes `slot` (`DefaultSlot` = full body even
   while moving, e.g. a glide; `UpperBody` = upper body over the run) and `blend_in` / `blend_out` (s). A gesture played
   backwards: montages refuse negative rates, so bake it with `gen_anim_montage.bake_reversed(src, dst)`.
   `bake_upper_body(src, dst, lean=, reverse=, mix=, ramps=)`: only the top moves (hips and legs from the idle), lean,
   damp a bone's motion (`mix`, e.g. a Paragon throw's torso twist) or steer a limb toward the target (`ramps`). Find
   ramp angles by FK search on the clip's poses (see the casting log). Preview baked clips in PIE with
   `play_slot_animation_as_dynamic_montage` before touching the montages. Then:
   - `--montages before` (capture the current sequences, `sequences` in the set),
   - `--apply` (git lfs lock, retarget, rebuild, save; stops if someone else holds a lock),
   - `--montages after` → `montage_<angle>.png|.gif`: before/after. If the old one reads better, put it back (a `/Game`
     path segment with the old ranges) and say so in the log.
6. **Judge IN GAME — this is the real pick, the offline sheets only shortlist** (round 1 was rejected by the user because
   it was judged on silhouettes). `gen_anim_pie.rig_capture(name, steps, duration, slomo=0.25, aim=..., view=...)` with
   `gap.press(action, hold_game_s, slomo)` / `gap.move(x, y, s, slomo)` steps records the real render (follow camera,
   synchronous), then `python Tools/Anim/ingame_sheet.py <name>` (sheet + GIFs) and `--compare before after` for the
   user. Look at the whole body, standing AND moving, and the transitions. Keep the editor window restored (minimized =
   PIE frozen). `gap.timeline()` lists montages/sections with times. Then `Gen.Visuals` / `Gen.Net.FeedVisuals` tests.
   Retarget any hero with `gen_anim_montage.retarget_paragon([...])` (IK rig from Gideon's, op stack added; PIE stopped),
   Lyra with `retarget_lyra`, GASP with `retarget_gasp` (Epic's retargeter). All of them end with `sync_ik_bones`:
   a retargeted clip whose `ik_foot_*` stay in ref pose gets its feet pinned by the ABP's foot IK (it happened: "he
   doesn't walk anymore"). Any clip imported another way: run `gen_anim_montage.sync_ik_bones([...])` on it. To check a
   pose in numbers in PIE: `gen_anim_probe.start(name, seconds, move=(x, y))` → `Saved/AnimProbe/<name>.json`. Exclude floating heroes (Muriel, Fey) for
   full-body clips. Charge = natural-speed wind-up + hold, never a slow-motion stretch.
7. **Commit** only the `AM_*` and the clips they use. `--check` lists them and any reference to `/Game/ParagonGideon`,
   `/Game/ParagonSerath` or `_Local` (it rescans first: the registry keeps stale dependencies of unsaved duplicates).
   Update the casting log.

Editor side: `Content/Python/gen_anim_capture.py` (candidate frames), `gen_anim_montage.py` (retarget, analyse, apply,
montage capture, leak check), `gen_anim_pie.py` (PIE timeline). When a step repeats and is not scripted, add it here.
