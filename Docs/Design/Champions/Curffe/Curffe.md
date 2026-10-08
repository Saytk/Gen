# Curffe (fire mage): champion spec v0

Follows `Docs/Design/CharacterGuidelines.md`. Status: approved in design review on 2026-10-07.
**All numbers are starting values** to tune in playtests; the structure and interactions are the
approved part.

## 1. Identity

- **Name:** Curffe. **Archetype:** fire mage.
- **Role:** Ranged, the game's reference champion (mid skill: easy to pick up, depth in the
  interactions). **Pure offence** in the base kit; support leanings only through builds (deferred).
- **Twist:** *Five flames orbit the mage. Holding a flame spell feeds it flames: each one adds
  0.3 s of casting and one level of power, up to 3 levels.*
- **Fantasy:** the WoW-style fire mage who builds heat and erupts, ablaze, in Combustion.
- **Weakness:** low health (200); the more he feeds, the slower and more predictable he is; an
  **empty mage (0 flames)** is visible to everyone and is the moment to dive him; ground areas go
  through his counter.

## 2. Passive trait: the Hearth (custom state)

- **5 flames** orbit the mage, visible to everyone (opponents can count them).
- **Gain:** +1 per Fireball (LMB) hit on an enemy; +2 per hit blocked by Backfire (Q); +1 every 3 s
  passively while below 5. Living Flame (R) refills to 5. Combustion (F) makes them unlimited.
- **Feeding rule:** hold a spell marked with a flame (RMB, Space, E) to feed it. Every **0.3 s** held,
  one more flame moves from the orbit into the spell, up to **3 thresholds** (max 3 flames per spell, or
  fewer if the mage has fewer). Release to cast. Feeding is visible: the flames visibly leave the orbit,
  the charge bar shows one tick per threshold, and the spell's indicator grows at each threshold.
  (Decision 2026-10-08: 3 thresholds max per spell, so every threshold is easy to hit on purpose. The Hearth
  keeps 5 flames, so a full Hearth pays for one 3-flame spell plus a 2-flame one.)
- Fed flames are **spent on release**. A cancelled or interrupted cast returns them (guidelines §3.1).
- This is the kit's only custom state (complexity budget met). Objects on screen: Flame Pillar only.

## 3. Spells

Counter trigger: **P** = projectile (triggers counters), **A** = ground area (does not).

### LMB: Fireball
- Projectile (P), cast **0.35 s**, then a **0.30 s cooldown that starts at the end of the cast** (Battlerite
  rhythm, like Alysia's M1): one shot every 0.65 s (~1.5/s). Range **11 m**, **16** damage (~25 per second).
  (Decision 2026-10-08, user playtest: 2.5 shots/s still felt too fast; the user confirmed Battlerite's
  cooldown starts at the end of the cast. A click during the cooldown is buffered and fires when it ends.)
- +1 flame on enemy hit; energy **+2** per hit.
- During Combustion it becomes a **Pyroblast** (see F).
- VisualWeight: Filler (2).

### RMB: Great Fireball (feedable)
- Charged projectile (P). Cast **0.5 s + 0.3 s per flame** (max 1.4 s). Cooldown **6 s**, range **13 m**.
- Damage **14 + 10 per flame** (max 44).
- **2+ flames:** explodes on impact in a **1.5 m** area (A for the splash; the direct hit is P).
- **3 flames:** the explosion also knocks back **4 m**.
- Projectile grows with flames and slows from **25 m/s** (0 flames) to **16 m/s** (3 flames).
- Energy **+6, +2 per flame** on hit.
- VisualWeight: Skillshot (5) at 0–1 flame, CC/burst (7–8) at 2–3 flames.

### Space: Meteor Leap (feedable)
- Leap with a visible arc, **7 m**, cooldown **10 s**. Take-off **0.1 s + 0.3 s per fed flame**.
- **8** damage in a small area on landing (A).
- **Each fed flame bursts out as a Fireball** (P, 8 damage) in an even ring around the landing point
  (3 flames = a triangle). Ring Fireballs follow the Fireball rules (range, walls, counters).
- An enemy can be hit by **one ring projectile at most** (ring geometry). Primary purpose: **escape**
  (landing away screens off pursuers); engage is the weaker secondary use (≤ 1 projectile per enemy).
- **A ring Fireball blocked by a counter still counts as that enemy's one** (rule, review of Plan 2 Tasks 3–4):
  the next ring Fireballs pass through him. One interaction per enemy per ring, so a Backfire on the landing
  point earns one block (+2 flames), not one per overlapping Fireball, and a window that ends between two
  ring Fireballs can't turn the block into a hit.
- Energy **+2** on landing hit.
- VisualWeight: Skillshot (4–6).

### Q: Backfire (counter)
- Cast **0.1 s**, window **1.2 s**, cooldown **10 s**. The mage is slowed 50 % during the window.
- Triggered by projectiles and melee hits (P), not ground areas (A). The blocked hit deals nothing.
- On a block: **+2 flames per blocked hit**, **+10 energy** (once per cast), melee attackers are
  **knocked back 3 m**.
- A blocked Meteor Leap ring Fireball is that ring's one hit on the counterer: the other ring Fireballs pass
  through him (one block per ring, see Space). Tooltip: "a blocked ring Fireball counts as his one".
- VisualWeight: CC/burst (7) on trigger.

### E: Flame Pillar (feedable)
- Ground-targeted delayed area (A). Cast **0.4 s** (+0.3 s per fed flame), then **0.8 s** telegraph
  before impact. Range **9 m**, cooldown **12 s**.
- Radius **2 m + 0.5 m per flame** (max 3.5 m). **12** damage and a **1 s stun**.
- CC score (guidelines §3.3): telegraph ≥ 1 s (2) + normal area (1) + long range (0) + 12 s cooldown
  (1) + several targets (0) = **4 → middle of the stun range**.
- **The kit's only hard CC.** Energy **+8** on hit.
- VisualWeight: CC/burst (7–8).

### R: Living Flame (25 energy)
- Cast **0.1 s**, cooldown **16 s**.
- The mage becomes living fire: **untouchable for 0.5 s**, can't cast (guidelines §3.5 allows ≤ 0.5 s
  on R).
- At the end: a **2.5 m** ring (A) deals **8** damage and **knocks back 3 m**; flames **refill to 5** (the refill is applied at launch, so a spell cast on the first frame after the form is fully fed; he can't cast during the form, and the Hearth shows the refill at the end; decision 2026-10-08);
  then **+30 % move speed for 2 s**, during which he can cast.
- Selfish and offensive: dodge the burst, come out with a full Hearth for a 5-flame Great Fireball.
- Overlap with Q is intentional and bounded: Backfire is a timing parry against projectiles/melee
  that pays flames; Living Flame answers everything else (areas, ultimates) and costs energy.

### F: Combustion (100 energy)
- Cast **0.5 s**, interruptible (stun, silence, fear, incapacitate). Energy is spent when the cast
  completes.
- On cast: the mage **erupts**, a **3 m** nova (A) for **20** damage and a **3 m** knockback.
- Then **ablaze for 5 s**:
  1. **Pyroblasts:** LMB becomes a bigger projectile (P), cast 0.35 s + 0.30 s cooldown after the cast, **20** damage, explodes in a
     **1.2 m** area. Works with every other spell on cooldown.
  2. **Unlimited flames:** the Hearth refills after every spell.
  3. **Fast feeding:** 0.15 s per flame instead of 0.3 s; telegraphs never drop below 0.5 s.
- Not immune to CC. Unmistakable visuals and audio.
- (Decision: "cooldowns twice as fast" was dropped; it broke the time-to-kill cap.)
- VisualWeight: Ultimate (9–10).

## 4. Interactions (all through the visible Hearth)

| Source | Effect on the kit |
|---|---|
| Fireball hits | +1 flame each, feeds RMB / Space / E |
| Backfire block | +2 flames per hit, +10 energy |
| Living Flame | Hearth refills to 5 |
| Combustion | Unlimited flames, faster feeding, LMB → Pyroblast |
| Feeding Great Fireball | Damage, then area (2+), then knockback (3) |
| Feeding Meteor Leap | Ring of Fireballs at landing (1 per flame) |
| Feeding Flame Pillar | Bigger radius, longer telegraph |

## 5. Combo routes

- **All in:** Fireballs up to 5 flames → Flame Pillar (stun) → Great Fireball fed 2–3 flames to land
  inside the stun. With energy: Living Flame (refill) → 3-flame Great Fireball.
- **Poke and stall:** Fireball stream + Great Fireballs with 1 flame, keeping flames for the leap.
- **Disengage:** Backfire (+2 flames) → fed Meteor Leap whose ring screens pursuers; or Living Flame
  → speed away.
- **Combustion window:** nova pushes enemies away → Pyroblast stream → full Great Fireball with fast
  feeding → fed Flame Pillar.

## 6. Counterplay

- Count the flames; dive an empty mage.
- Bait Backfire with a ground area, or wait it out (it slows him).
- Punish a fed leap or a long Great Fireball charge during the feeding delay.
- Dodge the Great Fireball: the bigger it is, the slower it flies.
- Interrupt the Combustion cast, or back off for 5 s.

## 7. Checks against the guidelines

- **Time to kill** (any 3 s window, vs 210 HP):
  - without F: Flame Pillar 12 + Great Fireball (3) 44 + Meteor Leap 8 + 1 ring 8 = **72 (34 %)**, cap 35 %;
  - with F: nova 20 + Great Fireball (3 flames, fast fed) 44 + 2 Pyroblasts 40 = **104 (50 %)**, cap 55 % (0.5 s nova + 0.95 s Great Fireball leaves 1.55 s: 2 Pyroblasts at one per 0.65 s);
  - full 5 s Combustion ≈ 130 (62 %): strong, can't kill alone.
- **Hard CC:** Flame Pillar only (knockbacks are not hard CC).
- **Energy pace:** ≈ 50 s to the ultimate without R, ≈ 65 s with one R (estimate: 50 % active combat,
  50 % LMB hit rate).
- **Complexity:** 1 custom state (Hearth), 1 object (Flame Pillar).

## 8. Art notes

- `Docs/ArtBible.md` §7.2 lists `GA_GreatFireball` as VisualWeight 9 (provisional). With this kit it
  scales **5 → 8** with flames; Combustion is the 9–10 effect. The art bible should be updated.
- The orbiting flames need to be readable at the gameplay camera from any team's view, including the
  0-flame state (art bible §10 readability checklist).

## 9. Implementation notes

**Folders:** everything specific to Curffe lives in its own folder:
- design: `Docs/Design/Champions/Curffe/`
- assets: `Content/Gen/Champions/Curffe/` (abilities, projectiles, VFX, animations, character BP)
- C++: `Source/Gen/Champions/Curffe/` (Curffe-only abilities and actors). Generic systems (feeding,
  counters, knockback, energy, delayed areas) stay in the shared `Source/Gen/AbilitySystem/` folders
  so the next champions can reuse them.

**Reused**
- `UGenGA_Projectile` (cast time, cast slow, target data under the cursor, costs paid on release,
  cancel tags) for Fireball, Great Fireball, Pyroblast.
- `AGenProjectile` (replicated, passes allies, explodes on enemies and obstacles).
- `UGenAttributeSet` already has `Energy` / `MaxEnergy`.
- Existing assets: `GA_Fireball`, `GA_GreatFireball`, `GA_FlameLeap` (becomes Meteor Leap), the
  fireball VFX, `AM_*` montages.

**New systems**
- **Flames** as a replicated attribute (`Flames`, `MaxFlames`) changed through gameplay effects, plus
  a cosmetic orbit component.
- **Feeding:** hold-to-charge on top of `UGenGA_Projectile`. The client sends the fed count with its
  target data; the server clamps it to its own flames and hold time.
- **Energy gain** per hit (SetByCaller on the damage effect) and **energy costs** (R, F).
- **Counter framework:** `State.Countering` tag; projectiles and melee check it before applying damage
  and notify the counter ability.
- **Untouchable** state (`State.Untouchable`): projectiles pass through, damage is ignored.
- **Knockback** utility (server-authoritative launch with a distance in metres).
- **Delayed ground area** actor with a telegraph (Flame Pillar).
- **Combustion** state tag that swaps LMB to Pyroblast and changes feeding.
- **Resilience** (guidelines §3.3) and the **cancel key** are game-wide systems.

## 10. Parked ideas

- **Talents (build system deferred):** Smoulder, Ignite, Hearthfire (support), Pyroclasm, Concussive
  Blast, Comet Trail, Kindled Landing, Reflective Flames, Wide Pillar, Guardian Flame (support),
  Supernova. Presets: Pyromancer (damage), Flamewarden (control), Hearthkeeper (support).
- **Rejected twists:** Cinders, Backdraft, Kindling/Flashpoint, Fire spirit, Cauterize.
- **Rejected spells:** Ember Aegis (R, too supportive), Ember Echo / Recoil Blast (Space), Meteor Crown,
  Sun and Phoenix (F), Molten Shell and Firewall (R).
