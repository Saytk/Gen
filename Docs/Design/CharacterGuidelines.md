# Gen: Character Design Guidelines v0

Game-wide rules for designing champions in Gen, a 3v3 top-down skillshot arena brawler inspired by
Battlerite. Every champion spec (see `Docs/Design/Champions/`) must follow these rules or list its
exceptions explicitly.

- **Status:** v0, approved in design review on 2026-10-07. Numbers are starting values to validate in playtests.
- **Visual rules** (VisualWeight, telegraph rendering, status vocabulary, timing) live in
  `Docs/ArtBible.md` §7 and are not repeated here.
- **Sources:** web research on Battlerite (Stunlock dev blogs, patch notes, wiki extracts, Steam
  threads), Riot/Blizzard/Supercell design writing, and fire-mage kits across ~15 games. Key links are
  in §9.

---

## 1. Design pillars

1. **Readability first.** How much attention a spell grabs matches how much it matters. The hitbox is
   exactly what is drawn; a ground telegraph shows the real area. An ultimate is the loudest thing on
   screen and has an audio cue enemies can hear. (Riot Clarity; Stunlock Dev Blog 003.)
2. **Counterplay scales with impact.** High-impact effects (hard crowd control, burst, ultimates) must
   be dodgeable or counterable: at least **0.5 s** from the start of the cast to the hit at the normal
   engagement distance. Low-impact sustained damage (LMB, small slows) may be reliable; otherwise the
   game feels unreliable to play. (Riot "Quick Gameplay Thoughts", May 2021.)
3. **Decisions over dexterity, with situational combos.** Each kit offers several combo routes for
   different situations (all-in, poke and stall, disengage, peel). Routes share spells, so choosing
   one spends what another needs. The skill is choosing the route for the moment, not executing a
   fixed sequence. No route is mandatory and every spell works on its own.
4. **Every button earns its place.** No two buttons in one kit answer the same question (e.g. if Space
   escapes, Q does not). Synergies are a bonus on top of spells that are useful alone.
5. **Simple to play against.** After one round, an opponent can read a champion's threats from the
   visuals alone. Complexity is opt-in for the player who picks the champion, never forced on the
   players facing it. (Riot complexity audit, October 2020.)
6. **Rounds escalate.** Energy, the centre orb and the shrinking arena push teams to fight. Stalling
   must never be the best strategy.

## 2. Kit structure

Every champion has **5 basic spells, 1 energy spell (R) and 1 ultimate (F)**. There are no EX variants.

| Slot | Role | Cooldown band | Energy |
|---|---|---|---|
| **LMB** | Basic attack, main sustained damage, reliable | none (or ammo/charges) | gains |
| **RMB** | Signature skillshot | 4–8 s | gains |
| **Space** | Mobility. Either engage or escape as its primary purpose; always a visible trajectory | 8–12 s | gains (small) |
| **Q** | The kit's single defensive tool: counter, block, reflect or shield | 8–12 s | gains on success |
| **E** | Utility: crowd control, zone or buff | 8–12 s | gains |
| **R** | Champion-specific spell | 12–20 s | **costs 25** |
| **F** | Ultimate. Usable only at full energy, spends all of it | none | **costs 100** |

**Ability shapes:** projectile, area, melee, channelled, charged, self-buff, placed object.

**The twist** (§5) can live in any spell or in a passive trait. It does not have to be the R.

## 3. Combat rules

### 3.1 Cast time is the telegraph

| Band | Cast time | Used for |
|---|---|---|
| Near-instant | 0–0.15 s | Mobility, counters |
| Short | 0.2–0.4 s | LMB, light skillshots, slows |
| Medium | 0.5–0.9 s | Hard crowd control, big hits |
| Long | 1.0–1.6 s | Biggest damage and control, ultimates |

- Hard crowd control: **cast time + travel time ≥ 0.5 s** at the normal engagement distance.
- Delayed ground areas show their telegraph **0.6–1.0 s** before impact.
- The caster can move slowly while casting. A **cancel key** cancels the current cast.
- **Costs are paid on release.** Cooldown and energy are only spent when the spell actually goes off.
  A cancelled or interrupted cast costs nothing. (Already implemented for projectile spells in
  `UGenGA_Projectile`.) Channelled spells drain their energy over the channel and keep the rest if
  interrupted.
- Damage over time cannot kill: it stops at 1 HP.

### 3.2 Crowd control

**Types and allowed ranges.** A spell's design decides where it lands in its range (§3.3).

| Type | Effect | Min | Max (basic, R) | Max (ultimate) |
|---|---|---|---|---|
| Stun | Can't move or cast | 0.5 s | 1.75 s | 2.5 s |
| Root | Can't move, can cast | 1 s | 2.5 s | 3 s |
| Slow | −20 to −50 % move speed | 1 s | 3 s | 4 s |
| Silence | Can move, can't cast | 1 s | 2.5 s | 3 s |
| Fear | Runs straight away from the source, can't cast, ends after a damage threshold | 1 s | 2 s | 2.5 s |
| Incapacitate | Like a stun, **ends on any damage** | 1.5 s | 3 s | 4 s |
| Weaken | −25 % damage dealt | 2 s | 4 s | 5 s |
| Knockback | Pushed away | 3 m | 7 m | 10 m |

**Hard crowd control** = stun, silence, fear, incapacitate.

### 3.3 Crowd-control duration grid

Add up the points:

| Factor | 0 pts | +1 pt | +2 pts |
|---|---|---|---|
| Telegraph (cast + travel or delay) | < 0.5 s | 0.5–0.9 s | ≥ 1 s |
| Hit difficulty | Wide area, large hitbox | Normal projectile | Thin projectile, precise area |
| Risk for the caster | Long range | Mid range | Melee or point blank |
| Cooldown / cost | ≤ 8 s | 9–14 s | ≥ 15 s or costs energy |
| Targets | Several | — | One |

- **0–2 pts:** bottom of the type's range.
- **3–5 pts:** middle.
- **6+ pts:** top.

Example: a thin, slow stun projectile with a 0.8 s cast, mid range, 12 s cooldown, single target scores
1 + 2 + 1 + 1 + 2 = 7, so a stun of about 1.5–1.75 s.

**Limits**
- **At most one hard CC among the 5 basic spells.** R and F may each add one.
- **Resilience:** after 2.5 s of hard CC within 5 s, the target is immune to hard CC for 1.5 s, with a
  visible effect. Long stuns are allowed; endless chains are not.
  - "Within 5 s" is strict: count the union of the hard CC (overlaps count once) inside the 5 s that end when the
    latest CC ends. Two 1.5 s stuns about 5 s apart don't trigger it. The CC that reaches 2.5 s applies in full, and
    the immunity lasts until 1.5 s after it ends. A refused CC (untouchable, already immune) doesn't count.
- **Ultimates** are interrupted by stun, silence, fear and incapacitate, not by knockback.

### 3.4 Defensive spells (Q)

- A visible stance with a sound cue, **1–1.5 s**, on an **8–12 s** cooldown. The user is slowed during
  it, so a wasted Q costs something.
- **One trigger rule for every counter, shown in the tooltip:** projectiles and melee hits trigger it;
  ground areas do not. (Battlerite players complained this was never written down.)
- The reward scales with what was blocked.

### 3.5 Invulnerability

Invulnerability (untouchable, immaterial) is rare: **0.5 s or less**, and only during visible mobility
travel, on the **R**, or on the **F**.

### 3.6 Mobility (Space)

- The movement is always visible (dash, leap, flight). A teleport needs at least 0.3 s of wind-up
  with the destination shown.
- Each mobility spell has a primary purpose, engage or escape. If it can do both, the secondary use
  must be clearly weaker, and the spec must say why.

### 3.7 Health and time to kill

- Health: ranged about **200–220**, melee about **240–270**.
- **No single champion can kill from full health on their own.** Within **any 3-second window**, one
  champion deals at most **35 %** of a ranged champion's health without their ultimate, and
  **55 %** with it. Kills come from focus fire or sustained pressure.

## 4. Energy and rounds

### 4.1 Energy (0–100)

- **Earned by** hitting with spells (scaled to how hard the spell is to land: about **2–4** per LMB
  hit, **6–15** per skillshot), by healing or shielding allies (same scale), and **+10** for a
  successful counter.
- **Not earned** passively or by taking damage.
- **Spent by** R (25) and F (100, needs a full bar). Each R pushes the ultimate back, which keeps a
  "spend now or save up" decision.
- **Each round starts at 25 energy.** Nothing carries over between rounds (carry-over is a later test).
- **Target pace:** ultimate around 60–75 s into a round in normal play, about once per round.

### 4.2 Round format

- 3v3, first to 3 rounds won, no respawn within a round. Health and cooldowns reset each round.
- Target round length about **90 s**. The arena starts shrinking at **75 s** and is fully closed
  around **120 s**.
- **Centre orb:** appears from 30 s, then every 20 s. The team that lands the last hit gets +20 energy
  and a small heal.
- **Death orb:** a dying player drops half their energy; only their allies can pick it up.
- **Recoverable health:** healing only restores recent damage, up to a 40 HP buffer. The rest is lost
  for the round. This stops healing from stalling fights.

## 5. Champion identity

- **Roles:** Melee, Ranged, Support. A champion has one main role and can lean toward 1–2 secondary
  roles through builds (§6).
- **Recipe:** familiar actions plus **one twist rule** that fits in one sentence and is visible on
  screen. The twist connects at least 2 spells. (Supercell's Brawl Stars pillars: "feels familiar but
  doesn't feel like a copy".)
- **Weakness:** each champion has an explicit, moderate weakness that is visible in the kit, not a
  hard counter.
- **Interactions:** at least 2 interactions inside the kit, always through a **visible state** (a
  mark, a zone, an object, an orbiting resource), never a hidden number.
- **Combo routes:** each kit documents at least **3 routes** (all-in, poke and stall, disengage) and
  what each one uses up.
- **Complexity budget:** at most **1 custom state** beyond cooldowns and energy, and at most **2
  objects on screen** per champion at once.
- **Cross-champion interactions** (e.g. fire against frost) are deferred until there are several
  champions.

## 6. Build system (deferred: not in the first test)

Decision recorded for later; the first playtest uses base kits only.

- **Fixed loadout:** 5 talents picked before the match, at most 2 per spell (1 for R, 1 for F, 2 for a
  passive trait). Locked for the match and visible to everyone.
- **Talent tiers:** adjustment (numbers), added effect, transformation (changes shape or role, takes
  **2 slots**). Transformed spells must look different.
- Each talent is valuable on its own (Stunlock dropped their loadout because players "had fewer real
  choices than they seemed"). Each champion ships with 2–3 suggested presets, one per role.
- Talents can't break §3: CC is rescored, the hard-CC limit and time-to-kill caps still apply.
- About 10–12 talents per champion to start.

## 7. Champion spec template

Each champion spec in `Docs/Design/Champions/` contains:

1. **Identity:** role, twist sentence, weakness, fantasy.
2. **Passive trait** (if any) and the kit's custom state.
3. **One sheet per spell:**
   - slot, shape, cast time / delay, cooldown, range, damage;
   - crowd control with its §3.3 score;
   - energy gained or spent;
   - triggers counters? (projectile/melee = yes, ground area = no);
   - interactions with the rest of the kit;
   - VisualWeight tier and per-viewer notes (`Docs/ArtBible.md` §7.2).
4. **Combo routes** (≥ 3) and what each uses up.
5. **Counterplay** for opponents.
6. **Checks:** time-to-kill caps (§3.7), hard-CC limit (§3.3), energy pace (§4.1), complexity budget (§5).
7. **Implementation notes:** existing code and assets reused, new systems needed.

## 8. Playtest metrics

Track per ability and per round:
- skillshot hit and dodge rates; counter success rate;
- CC time inflicted and received;
- energy at death, when each ultimate is cast;
- round length;
- "I died and didn't know why" reports (clarity problems).

## 9. Key sources

- Riot, counterplay and mobility: https://www.leagueoflegends.com/en-us/news/dev/quick-gameplay-thoughts-may-14/ · https://www.leagueoflegends.com/en-us/news/dev/quick-gameplay-thoughts-may-28/
- Riot, complexity: https://www.leagueoflegends.com/en-us/news/dev/quick-gameplay-thoughts-october-30
- Riot, clarity: https://www.leagueoflegends.com/en-us/news/dev/clarity-in-league/
- Stunlock Dev Blog 003 (Raigon): https://blog.stunlock.com/dev-blog-003/
- Stunlock, patch 0.14 and The Big Patch: https://blog.stunlock.com/patch-0-14/ · https://blog.stunlock.com/the-big-patch/
- Stunlock, The Future of Battlerite: https://blog.stunlock.com/the-future-of-battlerite/
- Heroes of the Storm stun-duration changes: https://news.blizzard.com/en-us/article/19996517/heroes-of-the-storm-balance-update-notes-january-20-2016
- Brawl Stars design pillars: http://www.pocketgamer.biz/5-creative-pillars-for-designing-brawlers-in-supercells-brawl-stars/
- Battlerite counterplay feedback (Steam): https://steamcommunity.com/app/504370/discussions/0/2119355556482203881/

Battlerite numbers were gathered from wiki extracts of the final (2019) patches and changed often;
treat them as references, not targets.
