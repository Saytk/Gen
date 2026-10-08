# Curffe: remaining items

> Written by Claude (AI) on 2026-10-08, when the work was paused. Check before acting on it.

State at the pause: everything is pushed to `main` (451da75). The full in-editor suite passes 167/167.
All 7 spells and Pyroblast work in multiplayer. No LFS lock is held.

Follow the process in `CLAUDE.md`, section "Processus d'implémentation : vite d'abord":
- a C++ batch in the `Gen-curffe` worktree;
- assets and checks in the main editor;
- a single visual check at the end.

## C++ (one batch in `Gen-curffe`)

1. **Flaky Living Flame multiplayer test.**
   - `Gen.Net.PowerSpells.LivingFlame_Form_Burst_Refill_Haste_EnemiesOnly` timed out once, then passed twice.
   - Find the race (a fixed wait against replication, or spawn/floor timing) and switch it to `.Until` conditions.
   - Done when it passes 10 runs in a row.
2. **Energy bar flicker after a big spend.**
   - After a predicted spend (e.g. Combustion 100), the owner's energy goes 150 → 50 → 150 → 50.
   - Cause: GAS removes the predicted effect one update before the server value arrives.
   - Fix: hold the predicted value until the authoritative value replicates, either on the UI side or by keeping the predicted spend.
   - Test: the owner's displayed energy never rises back up during a Combustion cast.
3. **Grace windows based on ping.**
   - `ServerTagGrace` (FastFeeding, FreeResource, required tags such as Ablaze) is fixed at 0.25 s.
   - Base it on the remote client's measured round-trip time (PlayerState ping), clamped between 0.1 and 0.4 s.
   - Also handle the deferred-launch reference time and the ablaze-start case listed in Plan 3's open points.
   - Update the tests that expect 0.25.

## Measurements and checks (main editor)

4. **VFX performance in a packaged build** (Art Bible §3.0, §7.8, §10 #11).
   - Build: a cooked Development build. If cooking takes too long, use a standalone `-game` process.
   - Scene: the worst case at 1080p from the gameplay camera, with these effects at the same moment:
     - Great Fireball at 3 flames: impact, splash and streaks;
     - Flame Pillar at 3.5 m;
     - Combustion eruption;
     - Meteor Leap landing and its ring;
     - Backfire block;
     - held left click;
     - Living Flame burst;
     - ablaze body.
   - Measures:
     - `stat gpu`: translucency and VFX time against the §3.0 budget;
     - `stat particles`;
     - `stat unit`;
     - a Quad Overdraw capture.
   - Trim spawn rates, spark lifetimes and sizes only if a budget is exceeded, keeping the Stylized look.
   - Write the numbers and the decision into taste entry #5 of the Art Bible, replacing "kept until the measurement".
5. **Final multiplayer matrix on the final code.**
   - Setup: dedicated server and 3 clients, half the runs at `NetEmulation.PktLag 60`, `p.NetShowCorrections 1`.
   - Cover every spell end to end (indicators, VFX, Hearth, tooltips, energy, status effects).
   - Re-check in particular the two changes that only had a short check:
     - the new left-click rhythm: 0.35 s cast plus 0.30 s cooldown after the cast, about 1.5 shots/s, with a click during the cooldown buffered;
     - the compact Alt panel, about 23 % of a 1080p screen.
   - Results go in `.superpowers/sdd/Final/` (local), and the fixes are made in the same session.

## Decisions to confirm (made by Claude on delegation, can be revised)

- Taste #5:
  - the Stylized orange-red fire is kept, and enemy reading relies on the team-coloured ground marker;
  - the white projectile cores are accepted;
  - hitbox rings are gold.
- Flame Pillar: exception to the 8 % screen cap at 3 flames (3.5 m).
- Values to confirm in playtest:
  - Fireball 16 damage, Pyroblast 20;
  - Living Flame refill applied at launch;
  - input buffer: 0.2 s lead, 0.3 s life.

## Minor items, in no hurry

- **Hearth:** on clients, a regen gain and a +1 hit gain look the same (V6-V8 review, M-7).
- **Placeholder poses** that could be hand-keyed one day:
  - Backfire guard;
  - Combustion gather;
  - the 3-frame snap on the Backfire raise.
- **Particles over the guides:** the Great Fireball impact plus splash at 2–3 flames is about 175 particles against the CC guide of 150. Item 4 settles it.
