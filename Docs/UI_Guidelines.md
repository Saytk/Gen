# Gen — UI Guidelines

_Generated with AI-assisted research (Claude) on 2026-10-07 — review before treating as final._

> **Status:** Reference for all UI work, by people and AI agents. **Scope:** in-match HUD, overhead (world-anchored) UI, ground telegraphs, menus, champion select and settings.
> **Engine:** UE 5.8.3, as recorded in the Art Bible header (from `Saved/Logs/Gen.log`). The shared `EngineAssociation` is still open (Art Bible §12 Q1). Rules that depend on engine behaviour we have not checked locally are marked **(verify on our engine)**.
> **Reference resolution:** 1920×1080 at DPI scale 1.0. Every pixel value in this document is a layout unit at that reference (1 unit = 1 physical px at 1080p) unless stated otherwise. All §3 pixel budgets assume HUD scale 100% and text size 100%.
> **Language:** Canadian English in all UI text and docs (colour, centre, grey).
> **Tunable values:** values marked **(tunable)** were chosen where the research gave no number. Change them only after a PIE playtest (2 clients + dedicated server), and record the change here. Taste feedback ("too loud", "the bars look cheap") goes through the Art Bible §13 procedure (log it, find the rule, check precedence, tune the knob).

### Relationship to the Art Bible

`Docs/ArtBible.md` is the **authoritative** visual document. This guide only adds UI-specific rules on top of it.

- **The Art Bible owns:** team colours and their presets (`DA_TeamColours` and the one team-colour MPC, Art Bible §4.3), the mandatory team shape cues, the reserved-hue table (fire, energy, heal, stun, poison/silence, frost), status shape motifs (§7.6), ground telegraph rules (§7.5), the UI and HUD direction (§9) and the frame budget (§3.0, **UI ≤ 0.3 ms**).
- **This guide owns:** HUD layout, component specs, UI-only tokens (panels, text, bars, motion), accessibility settings and the UMG/CommonUI implementation.
- **On any conflict, the Art Bible wins.** Fix this guide in the same PR. The Art Bible's precedence order also applies to UI rules: performance budget (§3.0), then readability tests (§10), then [TASTE] entries (§13), then everything else.
- Where this guide repeats an Art Bible value (a hex code, a size), it is a **copy for convenience**, marked "= Art Bible §x". If the two differ, the Art Bible value is correct.
- This guide's typography, icon style and stroke weights (§2.6, §2.11) are a **proposal that answers Art Bible §12 Q17**. They become final only when the team settles Q17 in the UI pass after the visual-target hero.

---

## 1. Purpose and design pillars

Gen is a top-down 3v3 arena brawler inspired by Battlerite. The UI has to show combat state instantly to players and spectators while staying quiet and easy on the eyes. Stunlock said the priority for Battlerite's visuals was to "support and visually explain the gameplay". We follow the same goal.

### 1.1 Pillars

| # | Pillar | What it means in practice |
|---|---|---|
| P1 | **Gameplay first** | Every element explains gameplay state. If removing an element costs the player no decision, remove it. Ornament is never a reason for an element to exist. |
| P2 | **Information lives where the eyes are** | Combat-critical information sits on the character (overhead stack) and on the floor (telegraphs, pickup timers). Screen edges hold only stable, learned information: frames, score and the ability bar. The centre of the screen stays clear. |
| P3 | **Quiet chrome, loud meaning** | All UI chrome is **dark and warm** ([TASTE #1], [TASTE #2], [TASTE #3]): low-chroma umber (red-brown) panels, warm peach-cream text and 1 px copper-bronze lines. Combat HUD panels are translucent; menus use the same family, made opaque (Art Bible §9). Saturated colour appears only on small marks that carry meaning: team, health, energy, ready and danger. |
| P4 | **Attention matches importance** | Only critical events get motion, saturation or size: own low HP, crowd control on self, an enemy cast starting, ultimate ready. Routine updates change value with no fanfare. Every animation has a gameplay cause. |
| P5 | **Readable by everyone** | Colour is never the only cue. The UI meets minimum text size and contrast, can be scaled, has colour presets, and offers reduced motion and reduced flashes. |

### 1.2 Attention tiers

Tiers apply to **states and events**, not to widgets. One widget can show events from different tiers. For example, the cast bar is Important while it fills, but an enemy cast *starting* or an interrupt is Critical.

| Tier | States and events | Allowed treatment |
|---|---|---|
| **Critical** | Own HP crosses 30% downward, and the low-HP state that follows. Crowd control applied to self. An enemy cast or channel starting. An interrupt or cancel. An enemy state word appearing (STUNNED, COUNTER…). Own ultimate becoming ready. An enemy ultimate wind-up while that enemy is off-screen. The round banner and round intro. | Saturated colour, one-shot motion, larger size, a paired sound |
| **Important** | Own cooldown and energy changes. Overhead HP changes. Cast and channel progress. The round timer during the last 10 s. Own status-row changes. Pings. | Clear contrast and value changes. At most one state flash of 200 ms or less. |
| **Informational** | Names, team frames, buff timers, round pips, off-screen ally indicators, the kill feed (spectator or opt-in only) | Small, secondary text colour. A fade, plus an optional slide of 8 px or less. |
| **Ambient** | Panel chrome, dividers, the network latency and FPS readout | Low contrast. Never animates. |

### 1.3 Battlerite: what we keep and what we avoid

**Keep (documented Battlerite behaviour):**
- A compact **overhead stack** on each champion: name, a segmented health bar, a **4-segment energy bar** (in Battlerite: orange while charging, yellow when full; Gen uses amber, §2.4), and a thin **cast bar that everyone can see** and that flashes on interrupt or cancel (red in Battlerite; Gen uses `status.danger`, §2.5).
- **One short all-caps state word** (for example "PARRY") above the health bar, with a draining duration bar. Priority rules decide which single state is shown.
- A **small top-centre plaque** with the round timer, rounds won shown as blue and red dots, and team HP %. Gen adds a different pip shape per team (Art Bible §9).
- A **bottom-centre row of round ability icons**, each with its key label above it and a large cooldown number.
- **No green team colour.** Battlerite moved allied markers from green to blue for red/green colourblind players, and made energy orange so it would not compete with the team colours. Gen's enemy colour is vermillion (Art Bible §4.3), so in Gen **orange means enemy** and energy moves to amber.
- **Objective and pickup timers drawn in the world** as a filling ring, not as HUD timers.
- **A darker arena floor**, which gives feedback effects room to stand out.
- A **HUD-scale slider**.

**Gen choices (not claimed as Battlerite facts):**
- **Team colours are the Art Bible's** (§4.3): self off-white, ally sky blue, enemy vermillion (the last two from the Okabe-Ito palette). Self moved from yellow to off-white so it no longer shares a hue with fire, stun and heal VFX. Battlerite's own team hues are unverified (Art Bible §12 Q5).
- **Team frames in the top corners**: allies on the left, enemies on the right. Battlerite's exact frame placement is unverified. Its top plaque is documented.
- **Counter sits below hard CC, silence and root** in the state-word priority list (§4.6).
- **Untouchable characters are dimmed, not hidden** (§4.4).

**Avoid (documented Battlerite criticisms):**
- An ability bar too small to read in peripheral vision. We use larger slots and a one-shot ready flash.
- HUD scale available only through a config edit. We ship the slider from day one.
- A health bar whose maximum visibly shrinks, which confused newcomers. Our bar keeps a fixed width and hatches the permanently lost portion.
- "Generic" fonts and "gloomy", "empty" menus. We use one deliberate type family and flat, dark, warm menu panels over a lit, blurred arena backdrop with 3D champions (Art Bible §9). Dark, but never an empty black screen.
- Royale-style filigree chrome and heavy fantasy-RPG frames, which "told the wrong story" in Stunlock's testing (Art Bible §9). We use a 1 px bronze line.
- A minimap. Battlerite Arena had none, and the arena fits on one screen.
- Cosmetics that change an ability's silhouette, colour family or telegraph.

---

## 2. Design tokens

UI tokens live in **one place**: the `DA_UIPalette` and `DA_UIMetrics` data assets plus the CommonUI style assets (§8.3). No widget, material or VFX may hard-code a colour, font, size or radius.

Two exceptions, both owned by the Art Bible:
- **Team colours** (`team.*`) are **not** UI tokens. They come from `DA_TeamColours` and the one team-colour MPC (Art Bible §4.3), read at runtime through `UGenUISubsystem` (§8.3). `DA_UIPalette` never stores its own copy.
- **Reserved hues** (heal, energy, stun, poison/silence, fire, frost) are copied from the Art Bible §4.3 reserved-hue table and marked "= Art Bible §4.3". A change starts in the Art Bible, never here.

**Linear vs sRGB:**
- Hex values are **sRGB**. `FLinearColor` values are **linear**, which is what C++ `FLinearColor(...)` and the UMG colour pickers store.
- Convert with `FLinearColor::FromSRGBColor(FColor::FromHex(...))`, or use: linear = ((c/255 + 0.055)/1.055)^2.4 when c/255 > 0.04045, otherwise c/255/12.92.
- **Never paste sRGB numbers into `FLinearColor`.** The prototype `AGenHUD` did exactly that by accident (§8.12).
- Token names map to C++ properties by turning dots into underscores and using PascalCase: `text.primary` → `Text_Primary`, `space.0.5` → `Space_0_5`.

**How contrast is measured:**
- Ratios are WCAG contrast ratios (relative luminance).
- Translucent layers are alpha-composited in sRGB before measuring.
- The worst-case references are:
  - `ref.floorMax` = `#8C8C8C`, linear (0.262, 0.262, 0.262), HSV V 55%. This is the top of the Art Bible §4.1 floor band (V 25–55%), so it is the brightest arena floor allowed (§4.12). HUD panels are measured against it.
  - `ref.vfxMax` = `#FFFFFF`. Text drawn directly over the world is measured against it. That text always carries a 1 px `line.outline`, so its contrast is measured against the outline.

### 2.1 Colour: surfaces and lines

| Token | Hex (sRGB) | FLinearColor (R, G, B) | Alpha | Use |
|---|---|---|---|---|
| `bg.panel` | `#20160F` | (0.014, 0.008, 0.005) | 0.80 | Persistent HUD panels: bottom bar, top plaque, frames |
| `bg.panelRaised` | `#2B1E16` | (0.024, 0.013, 0.008) | 0.88 | Tooltips, menu cards, scoreboard, kill-feed rows |
| `bg.hover` | `#3A291F` | (0.042, 0.022, 0.014) | 0.92 | Hover state of enabled cards and buttons only |
| `bg.scrim` | `#110C09` | (0.006, 0.004, 0.003) | 0.60 | Behind modals |
| `bar.track` | `#141411` | (0.007, 0.007, 0.006) | 0.90 | Empty part of **every** bar. Always a neutral near-black, **never a dark tint of the team colour** |
| `line.bronze` | `#B07A52` | (0.434, 0.195, 0.084) | 0.35 | 1 px panel outline (the warm copper-bronze edge, [TASTE #3]) |
| `line.outline` | `#070705` | (0.002, 0.002, 0.002) | 0.90 | 1 px outline around world-space bars, text outlines, the dark separator inside the self bar frame (§4.4), and the dark keyline on team strokes (Art Bible §4.3) |
| `line.keylineLight` | `#FFFFFF` | (1.000, 1.000, 1.000) | 1.0 | Light keyline on world team strokes (rings, telegraph borders) on arenas whose floor band max is V ≤ 40%. Polarity is set per arena by Art Bible §4.3. Never used for text |
| `line.highlight` | `#FFFFFF` | (1.000, 1.000, 1.000) | 0.06 | Optional 1 px inner top rim on panels |

**Menu surfaces** (menus, champion select, settings, post-match; **never the combat HUD**). **Dark, warm theme [TASTE #1, #2, #3]:** menus use the same dark, warm, low-chroma umber family as the HUD ([TASTE #2]; values sampled from the colour reference in [TASTE #3]), but opaque, so the UI reads as one system and the screen never flashes bright between menus and matches. All are flat: no gradients, no textures, no frames. To keep dark menus from feeling "gloomy" or "empty" (a Battlerite criticism, §1.3), the life comes from the **backdrop**: 3D champions with a cool rim light over a lit, blurred arena (§4.13), never from brighter panels.

| Token | Hex (sRGB) | FLinearColor (R, G, B) | Contrast | Use |
|---|---|---|---|---|
| `menu.scrim` | `#150F0B` | (0.007, 0.005, 0.003) | — | α 0.45 (tunable) over the blurred arena backdrop, behind panels only, never over the champions |
| `menu.panel` | `#241912` | (0.018, 0.010, 0.006) | — | Menu cards, side panels, settings rows. Opaque |
| `menu.panelHover` | `#33241A` | (0.033, 0.018, 0.010) | 1.15:1 vs `menu.panel` (the hover is a secondary cue; focus uses `menu.accent`) | Hover state of enabled cards and buttons |
| `menu.text.primary` | = `text.primary` `#F3DEC9` | (0.896, 0.730, 0.584) | 13.2:1 on `menu.panel`, 11.4:1 on hover | Titles, body text |
| `menu.text.secondary` | = `text.secondary` `#C2A893` | (0.539, 0.392, 0.292) | 7.6:1 on `menu.panel`, 6.6:1 on hover | Labels, descriptions |
| `menu.text.disabled` | `#907A6A` | (0.279, 0.195, 0.144) | 4.2:1 on `menu.panel` | Inactive items only (disabled controls never take hover) |
| `menu.line` | `#4E3828` | (0.076, 0.040, 0.021) | 1.57:1 on `menu.panel` (decorative divider, no contrast floor) | 1 px dividers and card edges |
| `menu.accent` | = `accent.brass` `#D6A47C` | (0.672, 0.371, 0.202) | 7.7:1 on `menu.panel` | 2 px focus and selection outline. See `accent.brass` (§2.2) for its reserved-hue note |
| `menu.cta` | `#7A4A2E` | (0.195, 0.068, 0.027) | `menu.text.primary` on it 5.7:1; 2.33:1 vs `menu.panel` | Fill of the **one** primary call-to-action per screen (Play, Ready, Confirm), with `menu.text.primary` label and a 1 px `line.bronze` edge. Flat, no gradient. OKLCH C 0.077, below the 0.10 reserved-hue threshold [TASTE #3] |

Team colours on `menu.panel`: self 15.3:1, ally 7.4:1, enemy 4.4:1, so relation stripes need no extra keyline in menus.

### 2.2 Colour: text and accent

| Token | Hex | FLinearColor | On `bg.panel` (opaque) | On `bg.panel` α 0.70 over `ref.floorMax` | Use |
|---|---|---|---|---|---|
| `text.primary` | `#F3DEC9` | (0.896, 0.730, 0.584) | 13.6:1 | 8.7:1 | Resting text and numbers. Never pure white |
| `text.secondary` | `#C2A893` | (0.539, 0.392, 0.292) | 7.9:1 | 5.0:1 | Labels, "/200", buff borders |
| `text.disabled` | `#8C7666` | (0.262, 0.181, 0.133) | 4.1:1 (3.8 on `bg.panelRaised`, 3.2 on `bg.hover`) | 2.6:1, so **not allowed on HUD panels** | Disabled or inactive menu items only |
| `accent.brass` | `#D6A47C` | (0.672, 0.371, 0.202) | 8.0:1 | — | The **only** brand accent on dark surfaces: focus, selection and links in the in-match game menu and tooltips. Menus use the same value as `menu.accent` (§2.1). A copper-brass ([TASTE #3]): OKLCH chroma 0.08 (hue 60°), below the 0.10 reserved-hue threshold. It sits between enemy vermillion (hue 48°, chroma 0.17) and energy amber `#FFC233` (hue 83°, chroma 0.16) but is half as saturated as either, and it **never appears in the ability bar, any bar or any world element**, so it can't read as enemy or energy. Never push its chroma above 0.10: the saturated gold and orange highlights of the colour reference stay out of UI chrome for this reason |
| `flash.white` | `#FFFFFF` | (1.000, 1.000, 1.000) | — | — | Momentary flashes only (ready flash, hit flash). Never resting text |

**Relation-tinted text** (names in overheads, team frames, the kill feed and the scoreboard, all on dark surfaces) always uses the same mix: `text.primary` blended 60% toward the relation colour in sRGB. `UGenUISubsystem` derives these at runtime from the active `DA_TeamColours` preset; the values below are the Default preset, for reference:

| Token | Hex | FLinearColor | On `bg.panelRaised` | On `line.outline` |
|---|---|---|---|---|
| `text.relation.self` | `#F2EAE2` | (0.888, 0.823, 0.761) | 13.6:1 | 16.9:1 |
| `text.relation.ally` | `#95C5DC` | (0.301, 0.558, 0.716) | 8.7:1 | 10.8:1 |
| `text.relation.enemy` | `#E19150` | (0.753, 0.283, 0.080) | 6.4:1 | 8.0:1 |
| `text.relation.neutral` | `#D0BD9C` | (0.631, 0.509, 0.332) | 8.8:1 | 11.0:1 |

- Presets recompute these with the same 60% rule.
- The self tint is almost `text.primary`, because self is off-white. That is intended: self is identified by position (first team-frame slot, no overhead name) and shape, never by name colour.
- Menu panels are dark too, so names in champion select and post-match use the same relation tint (on `menu.panel`: self 14.4:1, ally 9.2:1, enemy 6.8:1, neutral 9.4:1), always next to the relation stripe or ring (§4.13), never as the only cue.

### 2.3 Colour: team relation

Team colour is a **relation from the local viewer's point of view**: Self, Ally, Enemy or Neutral. It is never a fixed team A/B colour.

**Spectators** (default until Art Bible §12 Q20 decides spectator colours):
- A spectator who follows a player sees that player's point of view.
- In free camera, team 1 uses the Ally colours and team 2 uses the Enemy colours. Self is unused.

**Source of the values.** The team colours, the presets and the shape cues are defined in **Art Bible §4.3** and stored in **`DA_TeamColours`** and the one team-colour MPC. The UI reads them; it never defines them. The Default values are listed here for convenience only:

| Relation | Default (= Art Bible §4.3) | FLinearColor | Relative luminance | vs `bar.track` | Mandatory non-colour cue (Art Bible §4.3) and its UI form |
|---|---|---|---|---|---|
| `team.self` | Off-white `#F2F2F2` | (0.888, 0.888, 0.888) | 0.888 | 16.5:1 | Ground ring: thicker + direction notch. UI: off-white **frame** on the overhead bar (§4.4), no overhead name, first team-frame slot |
| `team.ally` | Sky blue `#56B4E9` | (0.093, 0.456, 0.815) | 0.405 | 8.0:1 | Solid ring. UI: plain bars, circle pips, solid arrowhead edge indicators |
| `team.enemy` | Vermillion `#D55E00` | (0.665, 0.112, 0.000) | 0.222 | 4.8:1 | Notched or chevron ring, hazard stripes on telegraphs. UI: chevron-notched bar end cap, diamond pips, open-chevron edge indicators |
| `team.neutral` | `#B9A67E` (**UI proposal**, not yet in Art Bible §4.3) | (0.485, 0.381, 0.209) | 0.391 | 7.8:1 | Summons, training dummy, objectives. OKLCH chroma 0.059, below the 0.10 reserved-hue threshold. To be added to `DA_TeamColours` once the Art Bible accepts it |

Contrast between relations (from the values above, WCAG): ally vs enemy **1.68:1**, self vs ally 2.06:1, self vs enemy 3.45:1. Ally vs enemy is under the 3:1 non-text target, which is why the Art Bible makes the shape cue **mandatory**, not optional.

**Decision:**
- Green is not a team colour; it is reserved for healing (Art Bible §4.3).
- Ally and enemy are separated by hue (OKLCH 236° vs 48°), by lightness and by shape.
- Self is separated from ally and enemy **by luminance and shape**: the off-white frame on its bar, no overhead name, the thicker notched ground ring, and the first slot in the team frames.

**Hard rules for every preset** (the UI checks these; the Art Bible sets the values):
- Ally vs enemy WCAG contrast must be **≥ 1.5:1**, **and** their ΔE2000 must be **≥ 20**, in normal vision and under each colour-vision-deficiency simulation.
  - Default (and the three CVD presets while they share its team hues): 1.68:1, ΔE 52.3. Under simulation (Machado 2009, severity 1): deuteranope ΔE 52.8, protanope ΔE 52.1, tritanope ΔE 62.6.
  - High contrast (proposal below): 1.64:1, ΔE 48.4.
- Every saturated (OKLCH chroma ≥ 0.10) UI hue sits **≥ 30° in OKLCH hue** from each team hue of the active preset (Art Bible §4.3 hue-separation rule). If a preset moves a team hue, re-check `status.danger`, `energy.*` and `heal` against it.
- **No team distinction by colour alone** (Art Bible §4.3). Every UI element that shows a relation also shows its shape cue.
- If a Custom preset is ever added (see the table below), it runs the same checks live and warns below either threshold.

**Presets** (Settings → Accessibility → Team colours). The Art Bible requires these five on day one: **Default, Deuteranopia-friendly, Protanopia-friendly, Tritanopia-friendly and High contrast**. Validate them with the UE colour-vision-deficiency simulation (§7.4) and with at least one colourblind tester; a tritan tester is still needed.

The Default pair is Okabe-Ito sky blue and vermillion, chosen because it survives all three common deficiencies (Art Bible §4.3). The three CVD presets therefore **start with the Default team hues**. They exist as separate rows in `DA_TeamColours` so the art pass can tune them after simulation and tester feedback without touching Default. The UI-side overrides each preset needs are listed here:

| Preset | Team hues (from `DA_TeamColours`) | UI-side overrides (in `DA_UIPalette`) |
|---|---|---|
| Default | Self `#F2F2F2`, ally `#56B4E9`, enemy `#D55E00` (= Art Bible §4.3) | — |
| Deuteranopia-friendly | Same as Default until the art pass changes them | None needed. Checked under simulation: `energy.charging` vs enemy ΔE 20.0 and 2.23:1; `status.danger` vs enemy ΔE 26.0 and 1.73:1; `heal` keeps its "+" prefix and plus glyph |
| Protanopia-friendly | Same as Default until the art pass changes them | None needed (`energy.charging` vs enemy ΔE 26.9; `status.danger` vs enemy ΔE 36.8) |
| Tritanopia-friendly | Same as Default until the art pass changes them | None needed for team hues (ΔE 62.6). `status.danger` vs enemy is only ΔE 16.8 under tritan simulation, so it relies on its luminance gap (1.71:1 under simulation) and its form cues (§2.5) |
| High contrast | **Proposal for `DA_TeamColours`:** self `#F2F2F2` (0.888, 0.888, 0.888), ally `#9CD3FF` (0.332, 0.651, 1.000), enemy `#FF7A1F` (1.000, 0.195, 0.014). Enemy keeps the Default OKLCH hue (48°), so the reserved-hue separation still holds | The high-contrast overrides below |
| Custom | **Not in v1.** Art Bible §12 Q12 defaults to presets only | If Q12 adds a picker: Self, Ally, Enemy, with live preview and the checks above |

Never "fix" a CVD preset by moving `heal` or `status.*` into blue: that lands within 30° of the ally hue.

**High-contrast overrides:**

| Token | HC value | FLinearColor | On `#000000` |
|---|---|---|---|
| `bg.panel`, `bg.panelRaised` | `#000000`, α 1.0 | (0.000, 0.000, 0.000) | — |
| `bar.track` | `#000000`, α 1.0 | (0.000, 0.000, 0.000) | — |
| `text.primary` | `#FFFFFF` | (1.000, 1.000, 1.000) | 21:1 |
| `text.secondary` | `#E6D2BE` | (0.791, 0.644, 0.515) | 14.3:1 |
| `text.disabled` | `#C2A893` | (0.539, 0.392, 0.292) | 9.3:1 |
| `line.bronze` | `#E2C4A4`, α 1.0 | (0.761, 0.552, 0.371) | 12.7:1 |
| `status.debuff` | 2 px `#FFFFFF` border plus the down-chevron | (1.000, 1.000, 1.000) | 21:1 |
| `status.buff` | `#E6D2BE` | (0.791, 0.644, 0.515) | 14.3:1 |
| `hp.recoverable` | `#635E58` | (0.125, 0.112, 0.098) | 3.3:1 (exempt; see below) |
| `cooldown.overlay` | `#000000`, α 0.80 | (0.000, 0.000, 0.000) | — |

- No HC override is needed for the reserved hues: `energy.*` `#FFC233` is 13.0:1 on black, `heal` `#7FD14F` 11.1:1, `status.danger` `#FF8FB0` 9.8:1.
- All outlines are 2 px in HC. Team colours on black (HC proposal): self 18.8:1, ally 13.2:1, enemy 8.1:1.

**HC exemptions:**
- `hp.recoverable` and `hp.lost` are secondary layers. They rely on the fill-end divider and the hatch (§4.3) instead of 7:1.
- The swept cooldown region relies on the cooldown number (§4.1).
- Telegraph fills have no contrast floor (§4.12).
- HC text is `#FFFFFF`, an explicit exception to Don't #2.

**One source of truth:** `DA_TeamColours` (through the one team-colour MPC and `UGenUISubsystem`) drives overhead bars, team frames, the kill feed, round pips, telegraph borders and fills, ground rings, character outlines, projectile ground markers, pings and edge indicators (§8.6). It never tints a hero's body or a spell's core (Art Bible §4.3). A colourblind mode that only recolours health bars is a known failure (LoL, Overwatch).

### 2.4 Colour: health, energy and cast

| Token | Hex | FLinearColor | Alpha | Use |
|---|---|---|---|---|
| `hp.fill.*` | = `team.*` (from `DA_TeamColours`) | — | 1.0 | Current HP. The fill colour **is** the relation colour (Art Bible §9). The self fill is therefore off-white |
| `hp.recoverable` | `#37322C` | (0.038, 0.032, 0.025) | 1.0 | Healable missing HP, up to the heal cap. Dark grey |
| `hp.lost` | `bar.track` with a 45° hatch in `hp.recoverable`, 1 px lines at 4 px pitch | — | 1.0 | Permanently lost HP (beyond the heal cap). Dark hatch |
| `hp.trail` | `#A8A39D` | (0.392, 0.366, 0.337) | 1.0 | Recent-damage ghost. Neutral grey: 5.07:1 vs `hp.recoverable`, 1.94:1 vs `hp.shield`, 2.24:1 vs `team.self`, 7.4:1 vs track. It is close to `team.ally` (1.08:1) and `team.neutral` (1.05:1) in lightness but differs in hue, and the 1 px fill-end divider separates them |
| `hp.shield` | `#F0E2B6` with **−45° stripes** of `line.outline`, 1 px at 4 px pitch | (0.871, 0.761, 0.468) | 1.0 | Shield or absorb overlay. White-gold, low chroma (OKLCH C 0.059), matching the Art Bible §7.6 `State.Shielded` hue. It is only 1.15:1 against the off-white self fill, so the **stripes are the shield cue**, not the colour |
| `hp.pending` | — | — | — | **Reserved, not in v1.** Add it only when a mechanic needs it, with a named data source |
| `hp.tick` | `line.outline` | — | 0.55 thin / 0.90 heavy | Segment ticks (§4.3) |
| `energy.charging` | `#FFC233` (= Art Bible §4.3 energy orb) | (1.000, 0.539, 0.033) | 1.0 | Energy below 100% (11.4:1 on track). **Amber means energy.** OKLCH hue 83°, 35° from the enemy vermillion (48°), so it clears the Art Bible's ≥ 30° reserved-hue separation |
| `energy.full` | = `energy.charging` + a 1 px `text.primary` outline on every segment | — | 1.0 | Energy at 100%, ultimate ready, ultimate ring. Same hue **by design**: a separate "full" yellow would sit next to stun `#FFE07A`, and a darker "charging" amber collapses into the enemy vermillion under deuteranope simulation (ΔE 7). "Full" is shown by the outline, the complete segment count and the one-shot brighten (§4.2) |
| `heal` | `#7FD14F` (= Art Bible §4.3 heal) | (0.212, 0.638, 0.078) | 1.0 | Heal numbers, the heal part of the centre-orb reward, `State.Healing` icon. **Never** a team colour. Always paired with a "+" prefix or plus glyph |
| `cast.fill` | `#E9DFC8` | (0.815, 0.738, 0.578) | 1.0 | Cast and channel fill for every relation (13.9:1 on track). Low chroma (OKLCH C 0.033). Always in its own bar row, never inside an HP bar, so it cannot be mistaken for the off-white self fill (1.18:1) |
| `cast.interrupted` | = `status.danger` | — | 1.0 | Interrupt or cancel flash |

**The bar rule (replaces "white means shield"):** inside any health bar, a **solid** fill is current HP in the relation colour (off-white for self), a **striped bright** segment is shield, and a **dark hatched** segment is lost HP. Nothing else in a bar is striped or hatched. Self is never identified by its fill colour alone: the overhead self bar always carries the off-white frame (§4.4).

Contrast notes:
- `team.enemy` vs `hp.recoverable` is **3.28:1**, ally 5.50:1, self 11.3:1. A 1 px `line.outline` divider still always marks the end of the HP fill.
- `hp.recoverable` vs `bar.track` is **1.45:1**. To compensate, the lost portion is hatched.
- `hp.shield` vs the self fill is **1.15:1**. To compensate, the shield is striped.
- These are the non-colour cues.

### 2.5 Colour: status, cooldown and world

| Token | Hex | FLinearColor | Alpha | Use |
|---|---|---|---|---|
| `status.duration` | = `text.primary` `#F3DEC9` | (0.896, 0.730, 0.584) | 0.80 | Draining line under the overhead state word. Neutral, so it never reads as a team mark |
| `status.buff` | = `text.secondary` `#C2A893` | (0.539, 0.392, 0.292) | 1.0 | Buff icon border (1 px) |
| `status.debuff` | = `text.primary` `#F3DEC9` | (0.896, 0.730, 0.584) | 1.0 | Debuff icon border (**2 px**), plus an 8 px down-chevron. Neutral **by design**: the icon glyph already carries its Art Bible §7.6 hue, and an orange border would read as "enemy". Buff vs debuff is told apart by border width and the chevron |
| `status.danger` | `#FF8FB0` | (1.000, 0.275, 0.434) | 1.0 | Self low-HP outline and HP number, interrupt flash, damage-taken numbers, errors. Rose, OKLCH hue 2°: 46° from the enemy vermillion, 62° from poison/silence, 1.81:1 lighter than enemy. 5.2:1 on `bg.panel` at α 0.70 over `ref.floorMax`, 8.6:1 on track, 9.8:1 on black (so it also serves High contrast). Never shown without a form cue (pulse, glyph, "−" prefix or the interrupt snap) |
| `status.success` | = `heal` | — | 1.0 | Confirmations |
| `cooldown.overlay` | `#110C09` | (0.006, 0.004, 0.003) | **0.75** | Radial cooldown sweep. The swept region's luminance is ≤ 35% of the unswept region (7–16% measured) |
| `cooldown.noEnergy` | `#2E4A78` | (0.027, 0.068, 0.188) | 0.45 | "Not enough energy" wash. The C++ default and `DA_UIPalette` read it from `GenUITokens::CooldownNoEnergyHex` / `CooldownNoEnergyAlpha` (`GenUIRules.h`); `Gen.UI.EnergySlot` checks both. Change the token there too |
| `cooldown.locked` | `#110C09` with a 45° hatch | (0.006, 0.004, 0.003) | 0.70 | Silenced or stunned slot |
| `world.boundary` | = `text.primary` `#F3DEC9` | (0.896, 0.730, 0.584) | 0.90 | Sudden-death or shrinking-zone edge line, drawn **dashed** (8 px dash, 8 px gap) so a long near-white line never reads as the off-white self ring. Neutral, never "mine". Round-flow visuals are still open (Art Bible §12 Q19) |
| `pickup.centreOrb` | = `energy.charging` `#FFC233`, with a bolt-and-plus glyph | — | — | Centre orb UI marks (spawn timer ring, icon): +20 energy and a small heal for the team that lands the last hit (CharacterGuidelines §4.2). Gen has no health orb |
| `pickup.energy` | = `energy.charging` `#FFC233` (Art Bible §4.3 energy orbs), with a bolt glyph | — | — | Death orb UI marks: half the dead player's energy, for their allies only (CharacterGuidelines §4.2) |
| `proximity.far` / `.mid` / `.near` | = `text.primary` | (0.880, 0.791, 0.723) | 0.50 / 0.75 / 1.00 | Stealth proximity. Shown as **1 / 2 / 3 chevrons**; the count is the cue, not a colour ramp |

There is **no `status.warning` token.** At 10 s or less, the round timer stays `text.primary` and switches its format to `s.s`. Other warnings use `text.primary` plus a glyph. This keeps orange for the enemy and amber for energy only.

**Reserved hues used by the UI** (all = Art Bible §4.3; the UI never adds a saturated hue that is not in this list or in the team colours):

| Art Bible use | Hex | OKLCH hue | Where the UI uses it |
|---|---|---|---|
| Energy orbs (centre, death) | `#FFC233` | 83° | `energy.*`, `pickup.energy`, `pickup.centreOrb` |
| Heal | `#7FD14F` | 136° | `heal`, `status.success`, `State.Healing` icon |
| Stun | `#FFE07A` | 93° | `State.Stunned` status icon glyph |
| Poison / silence | `#9E7BD9` | 300° | `State.Silenced` (and poison) status icon glyph |
| Fire body | `#F5B82E` | 82° | `State.Burning` status icon glyph |
| Frost | `#D7F2FF` | (228°, low chroma) | `State.Slowed` status icon glyph |

`status.danger` `#FF8FB0` is a UI-only hue. It follows the same rule: ≥ 30° from each team hue and from each status hue above.

### 2.6 Typography

> **Proposal for Art Bible §12 Q17** (UI typography, icon style, stroke weight, damage-number style). The Art Bible leaves these open until the UI pass after the visual-target hero. Treat this section, §2.11 and §4.7 as the proposal to review then; until Q17 is settled, prototypes may use them as-is.

**Family:**
- **Barlow** and **Barlow Semi Condensed** (SIL OFL, free to ship). Barlow is DIN-like and was chosen for clarity and numbers.
- **Barlow Condensed** is used only for titles and banners. No other families.
- Do not ship Bahnschrift. It is a Microsoft system font and is not free to embed.
- No fantasy or decorative display face anywhere in the combat HUD.

**Weights:** Regular 400, Medium 500, SemiBold 600, Bold 700. Use at most 3 weights on any one screen.

**Numbers must not wobble:**
- UMG has no per-text-block OpenType feature switch (such as `tnum`).
- Before locking the font, verify that its default figures are equal-width.
- If they are not, put every live number (cooldowns, timer, HP) in a fixed-width, right-aligned box sized for the widest value.

**Units:**
- "px" means layout px at 1080p.
- The "Slate size" column is `FSlateFontInfo::Size`, which Slate renders at 96 DPI: **Slate size = px × 0.75**. Use this value in C++ and in data.
- Some UE5 editor versions show the Details-panel font size in 72-DPI points instead. In that case the displayed number equals px. Check the editor's font-size DPI display preference before typing values **(verify on our engine)**.

**Minimum size:**
- Every essential HUD string must measure **≥ 18 physical px at 1080p**. Measure from the font metrics at the style size as cap height plus descender depth, not from the actual string. Check it by rendering "Hg".
- With Barlow this needs a **20 px** em size (cap height about 14 px plus descender about 4 px). Verify with a screenshot.
- Only non-essential text (network latency, FPS, build string) may be smaller.

| Style asset | Use | px @1080 | Slate size | Weight | Case and tracking | Colour |
|---|---|---|---|---|---|---|
| `TS_Micro` | Latency, FPS, build string (non-essential) | 16 | 12 | Medium | Sentence | `text.secondary` |
| `TS_Label` | Key labels, small labels, status durations | 20 | 15 | SemiBold | CAPS, +6% | `text.primary` |
| `TS_LabelSecondary` | "/max" after HP, secondary labels | 20 | 15 | SemiBold | CAPS, +6% | `text.secondary` |
| `TS_Name` | Names (overheads, frames, kill feed, scoreboard) | 20 | 15 | Medium (Semi Condensed) | As typed. Max 10 characters overhead, 12 elsewhere, then "…" | `text.relation.*` |
| `TS_Body` | Tooltips, menus, captions | 20 | 15 | Regular | Sentence case, 1.5× line height, ≤ 80 characters per line | `text.primary` |
| `TS_StatusWord` | Overhead state word | 20 | 15 | Bold | CAPS, +8%, outlined | `text.primary` |
| `TS_Value` | HP and energy numbers, team HP %, scoreboard stats | 24 | 18 | SemiBold | Equal-width digits | `text.primary` |
| `TS_DamageNumber` | Damage and heal numbers | 24 | 18 | Bold | Equal-width digits, outlined | see §4.7 |
| `TS_Cooldown` | Cooldown seconds on slots | 28 | 21 | Bold | Equal-width digits, outlined | `text.primary` |
| `TS_Timer` | Round timer | 28 | 21 | SemiBold | Equal-width digits | `text.primary` |
| `TS_Title` | Menu titles | 36 | 27 | SemiBold (Condensed) | CAPS, +4% | `text.primary` |
| `TS_Banner` | Round banner, round-intro countdown | 48 | 36 | Bold (Condensed) | CAPS, +6%, outlined | relation colour or `text.primary` |

- The in-match HUD uses **four sizes only**: 20, 24, 28 and 48, plus 16 for non-essential text.
- Use CAPS only for strings of 1–3 words. Anything longer is sentence case and left-aligned.

### 2.7 Spacing scale

Base unit 4 px. In `DA_UIMetrics`: `Space_0_5` = 2, `Space_1` = 4, `Space_2` = 8, `Space_3` = 12, `Space_4` = 16, `Space_6` = 24, `Space_8` = 32, `Space_12` = 48.

- **Inside a group** (HP bar to energy bar, icon to its label): 2–4 px.
- **Between groups:** 2–3 times the in-group gap, so 8–12 px. Separate groups with whitespace first. Add a panel only when the group sits over busy art.
- **Screen margin:** 32 px from every edge, inside the SafeZone.
- **Panel padding:** 8 px for HUD panels (compact), 16 px for menus.
- **Minimum graphic sizes:**
  - Bars are ≥ 4 px tall.
  - Gaps are ≥ 2 px.
  - Lines (outlines, ticks, the 2 px duration line) are ≥ 1 **physical** px. Round them to whole physical px after DPI, with a minimum of 1.

### 2.8 Corner radius

| Token | Value | Use |
|---|---|---|
| `radius.bar` | 2 px | All bars and tracks |
| `radius.panel` | 4 px | Panels, cards, buttons, tooltips, status icons |
| `radius.round` | 50% | Ability slots, portraits, pips (circles) |
| — | 0 | Nothing else. One shape family: circles for "things you press or are", 2–4 px rounded rectangles for everything else |

### 2.9 Borders, outlines and shadows

- **Borders and outlines are ≤ 2 px.** Fills, stripes and arcs may be thicker (for example the 4 px champion-select stripe and the 4 px cost arcs).
  - Default border: 1 px.
  - 2 px is reserved for state rings (ultimate, recast or active, focus or selected), debuff borders and high-contrast mode.
- **No glows** anywhere.
- **Panels:** 1 px `line.bronze` at α 0.35, plus an optional 1 px `line.highlight` top rim. **No drop shadows on panels.** An optional vertical gradient of at most 6% value is allowed.
- **World-space bars:** a 1 px `line.outline` around the whole bar, so it reads over bright floors and VFX.
- **Text over the arena:** every string drawn over the 3D world gets a 1 px `line.outline` outline (UMG Font → Outline Settings, Size 1). The alternative is a 1 px offset shadow at α 0.6. Text never sits on the arena without an outline or a backing plate.
- **Frame vs fill:** a bar's stated size is its **track** (the fill area). Outlines, and the self frame (§4.4), sit **outside** the track and never eat into the fill. Inside the track, only 1 px dividers, ticks and the shield stripes are allowed.

### 2.10 Opacity rules

| Element | Default α | Range (setting) |
|---|---|---|
| HUD panel backing (`bg.panel`) | 0.80 | **0.70–1.00**, "Panel opacity" (forced to 1.00 in HC) |
| Bar track | 0.90 | fixed (1.00 in HC) |
| Round banner and round intro strip | 0.90 | fixed; ignores "Panel opacity" so the enemy-coloured banner text keeps ≥ 3:1 (§4.11) |
| Menu panels (`menu.*`) | 1.00 | fixed (flat, Art Bible §9) |
| Own and ally telegraph fill / border | 0.20 / 0.90 | fill 0.15–0.25 (tunable per ability class; inside the Art Bible §7.5 range of 15–30%) |
| Enemy telegraph fill / border (opt-in abilities) | 0.15 / 0.90 | fixed. 0.15 is the Art Bible §7.5 minimum; the hazard-stripe pattern (§4.12) adds the enemy cue |
| Cooldown sweep | 0.75 | fixed (0.80 in HC) |
| State-duration line | 0.80 | fixed |
| Off-screen edge indicators | 0.70 | fixed (enemy ultimate arrow 1.0) |
| Disabled controls | Text uses `text.disabled`. Never lower opacity instead | — |
| Untouchable character's overhead stack | 0.30 | fixed (§4.4) |

**Measuring HUD panel text:** set the panel to its minimum opacity (0.70) and measure over `ref.floorMax` `#8C8C8C`. At that setting, `text.primary` is 9.0:1 and `text.secondary` is 4.9:1. Lowering opacity must never push essential information below **3:1** (graphics) or **4.5:1** (text).

### 2.11 Iconography

**Ability icons:**
- Author at 256 px. Display at 64 px, or 72 px for the ultimate.
- Each set uses one light direction (top-left), one rendering style and a limited palette per champion.
- Before approval, an icon must pass three tests: **greyscale**, **Gaussian blur 2 px** and **32 px**.
- Show the ability type with frame or corner marks, never with colour alone. For example, the energy spell (R) carries its cost arc (§4.1). Gen has no EX variants (CharacterGuidelines §2).

**Status icons** (Art Bible §9: "status icons repeat the §7.6 shape motifs, so the world and the HUD speak one vocabulary"):
- 32 px single-colour solid glyphs on a `bg.panelRaised` tile.
- No hairlines under 2 px at display size.
- Each glyph is a flat 2D reading of the status's Art Bible §7.6 world motif, in its §7.6 hue:

  | Tag | Glyph (from the §7.6 motif) | Glyph colour |
  |---|---|---|
  | `State.Stunned` | Ring of orbiting stars | Stun `#FFE07A` |
  | `State.Rooted` | Floor ring with a rising cylinder | Desaturated earth (low chroma, exempt) |
  | `State.Slowed` | Low dragging wisps | Frost `#D7F2FF` |
  | `State.Silenced` | Crossed glyph | Purple `#9E7BD9` |
  | `State.Airborne` / knockback | Vertical streaks | `text.primary` (Art Bible: neutral white) |
  | `State.Shielded` | Shell with descending lines | `hp.shield` white-gold `#F0E2B6` |
  | `State.Healing` | Rising motes | Heal `#7FD14F` |
  | `State.Burning` | Stepped flame flicker | Fire body `#F5B82E` |
  | Other buffs and debuffs | A shape that does not reuse any motif above | `text.primary` |

- Each status has a unique **shape**, never just a unique hue (Art Bible §7.6). No status glyph uses a team colour.

**UI glyphs** (lock, mouse buttons, plus, bolt, skull, chevrons, plug, warning, ping glyphs):
- One line-weight family: 2 px strokes at a 20 px glyph size, in `text.primary`.
- Mouse buttons use LMB and RMB glyphs, not words. They come from `DA_UIKeyGlyphs` (§8.4).

**Text:**
- **No baked text** in any texture except logos.
- All text is a `CommonTextBlock` (or `UGenTextBlock`, §8.8).

**Pickups and objectives:** one colour plus one glyph per type. Show value through the object's size, not through text.

---

## 3. HUD layout

### 3.1 Screen map (1920×1080 reference, HUD 100%, text 100%)

```
x=0                                                                   x=1920
+--------------------------------------------------------------------------+ y=0
| ALLY FRAMES (TL)          TOP PLAQUE (TC)             ENEMY FRAMES (TR)  |
| x32 y32, 240x148          x720 y32, 480x56            x1648 y32, 240x148 |
| (o) you   [ab c]          | o o .  1:42  . o o |     [ab c] ====== (o)   |
| (o) ally ====== [abc]     | 62%             48%|     [abc] ====== (o)    |
| (o) ally ====== [abc]                                [abc] ====== (o)    |
|                                          (spectator only: KILL FEED      |
|                                           x1588 y196, 300x112)           |
|           +------------- CENTRE CLEAR ZONE --------------+               |
|           | x 20-80%, y 20-80% of the layout viewport    |               |
|           | (x384-1536, y216-864 at 1080p 16:9)          |               |
|           | No persistent screen-space widgets.          |               |
|           | Allowed: overhead stacks, telegraphs, damage |               |
|           | numbers, pings, round banner/intro (y=324),  |               |
|           | scoreboard (while held), captions band.      |               |
|           +----------------------------------------------+               |
|                CAPTIONS (only when active, y 792-868)                    |
|                SELF CAST BAR (optional, 240x8, y 876-884)                |
|          STATUS ROW: [buffs ...] | [debuffs ...]  (y 892-924, 32 px)     |
|        +------------- BOTTOM BAR (BC) x580 y932, 760x116 ------+         |
|        | 164 /200       LMB  RMB  SPC   A    E    R     F      |         |
|        | ==========     (o)  (o)  (o)  (o)  (o)  (o)  ((O))    |         |
|        | [##|##|##|  ]                                 ~~~~    |         |
|        +-------------------------------------------------------+         |
|                         bottom margin 32 px                              |
+--------------------------------------------------------------------------+ y=1080
```

- Key labels show the player's current binding through `DA_UIKeyGlyphs` (§8.4).
- Layout-aware defaults (for example AZERTY) are an input task, not a UI task.

**Bottom stack** (absolute y at 100%, 1080p; every element centred horizontally):

| Element | Height | y range |
|---|---|---|
| Bottom margin | 32 | 1048–1080 |
| Bottom bar | 116 | 932–1048 |
| Gap | 8 | 924–932 |
| Status row | 32 | 892–924 |
| Gap | 8 | 884–892 |
| Self cast bar (optional; space always reserved) | 8 | 876–884 |
| Gap | 8 | 868–876 |
| Captions (2 × 30 line + 16 padding, 640 wide) | 76 | 792–868 |

**Bottom bar internals** (8 px padding, 760 × 116, x 580–1340):
- Vitals block: x 588–788.
- 16 px gap.
- Ability row: x 804–1332 (528 px).
- Vertical: 8 padding + 20 key label + 2 + 72 (ultimate slot) + 6 (cost-arc band) + 8 padding = 116.

### 3.2 What lives where

| Region | Anchor | Contents |
|---|---|---|
| Overhead stack | World (head socket), screen-projected | Name or state word, HP, energy, cast bar |
| Floor | World | Telegraphs, aim preview, pickup and objective timer rings, boundary line, team ground rings (owned by Art Bible §4.3 and §7.5), ping glyphs |
| Screen edges (inset 48 px inside the SafeZone) | Screen-projected | Off-screen indicators (§4.14) |
| Bottom centre | Bottom-centre, 32 px margin | Own HP numbers and bar, 4-segment energy, ability bar (7 slots). Collapses to the spectating line while dead (§4.17) |
| Above the bottom bar | Bottom-centre | Own status row (buffs left, debuffs right), optional self cast bar, captions |
| Top centre | Top-centre | Round plaque: timer, round pips, team HP % |
| Top-left / top-right | Corners | Ally frames (self first) / enemy frames |
| Top-right, under the enemy frames | Top-right | Kill feed: **spectator mode or opt-in only**, at most 3 rows |
| Centre, upper third | Centre | Round banner and round intro (transient) |
| Centre | Centre | Scoreboard (only while Tab is held) |
| — | — | **No minimap** |

### 3.3 Safe zones and clear areas

- **SafeZone:** the HUD clusters sit inside a UMG **SafeZone**, plus a fixed **32 px** margin on all sides. PC has no platform title-safe area, so the game supplies one.
  - Test with `r.DebugSafeZone.Mode 1` and `r.DebugSafeZone.TitleRatio 0.9`. The HUD must still fit, which keeps us ready for console and TV.
- **Centre clear zone:** x 20–80% and y 20–80% of the layout viewport, measured at HUD scale 100%. At 1080p 16:9 that is x 384–1536, y 216–864.
  - No persistent screen-space widget may enter it.
  - **Exceptions:**
    - World-anchored UI: overhead stacks, damage numbers, pings, telegraphs.
    - Transient UI: round banner, round intro, scoreboard while held, and the captions band (y 792–868, which intrudes 72 px).
  - **At HUD scale above 100%,** corner and bottom clusters may intrude because they grow away from their anchor edge. At 150% the frames reach y 254 and x 392. This is allowed. Nothing else may enter.
- **Ultrawide:** the "HUD width" setting (Full / 16:9, default **16:9**) keeps the corner clusters inside a centred 16:9 rectangle. The clear zone stays 60% × 60% of the full viewport.
- **Bottom bar budget:** ≤ 116 px tall (about 11% of the screen height) and ≤ 760 px wide.
- Overhead stacks, damage numbers, pings and telegraphs live **outside** the SafeZone because they follow world positions. Edge indicators are clamped inside it.
- In a real match, check that telegraphs near the bottom edge stay visible above the bottom bar.

---

## 4. Component specs

### 4.1 Ability bar and slots

**Slot order:** LMB, RMB, Space, Q, E, R, F, the kit from CharacterGuidelines §2: basic attack, signature skillshot, mobility, defensive tool, utility, energy spell (costs 25) and ultimate (costs 100). They map to `InputTag.Ability.Primary / Secondary / Mobility / Defensive / Utility / Energy / Ultimate`. Primary, Secondary, Mobility and Ultimate exist in code; the other three names are proposals.

**Geometry:**

| Item | Value |
|---|---|
| Slot | 64 px circle. Ultimate 72 px |
| Gap between slots | 12 px |
| Ability row width | 6×64 + 72 + 6×12 = **528 px** |
| Key label | `TS_Label` (20 px), centred **above** the slot, 2 px gap |
| Label source | The live Enhanced Input mapping, resolved through `DA_UIKeyGlyphs`. Mouse buttons use glyphs. Space shows "SPC" (tunable). Never hard-coded |
| Energy-cost arc | Under the slot: a 120° arc, 4 px thick, 4 segments (one per 25% energy) with 4° gaps. **Always shown on the two energy slots, and only there:** R shows one segment (its 25 cost), F shows all four (100). Funded segments use `energy.charging`. Unfunded segments are a 1 px hollow outline in `text.secondary`. A fully funded ultimate arc uses `energy.full` |
| Slot rim | 1 px `line.bronze`. Ultimate: 2 px ring in `energy.full` at α 0.5 |

**States.** These are mutually exclusive except where marked "+".

| State | Visual | Non-colour cue | Motion |
|---|---|---|---|
| **Ready** | Icon at full brightness | — | — |
| **Activating** (late-commit abilities only: between `TryActivateAbility` and `CommitAbility`) | Icon at 60% brightness, no number | Brightness | Within 100 ms of the press, client-side |
| **Cooldown** | `cooldown.overlay` radial sweep: a dark wedge that starts at 12 o'clock and shrinks **clockwise**, the same direction everywhere. Icon desaturated 70% | **Mandatory:** a number in `TS_Cooldown`, centred and outlined, plus the desaturation. Whole seconds rounded up while ≥ 1 s; one decimal under 1 s ("0.6"). The number is hidden for total cooldowns under 2 s (tunable); desaturation and sweep remain | Sweep is linear (real time) |
| **Not enough energy** | `cooldown.noEnergy` wash; icon brightness 55% | Brightness drop, plus hollow unfunded arc segments (R and F) | None |
| **Locked** (CC that blocks casting: `State.Stunned`, `State.Incapacitated`, `State.Feared`, `State.Silenced`; CharacterGuidelines §3.2. Root does not lock) | `cooldown.locked` hatch | 20 px lock glyph, centred | None |
| **Charges** + | — | Up to 3 pips (6 px circles, `text.primary`) on the top-right of the rim. 4 or more charges show a number in `TS_Label`. While at least 1 charge is left, the icon stays bright and only the sweep shows the next charge | — |
| **Active / recast window** + | 2 px `text.primary` ring on the rim that drains **counter-clockwise**. The interior stays bright | Ring on the rim vs sweep on the interior: the two never look alike | Ring drains linearly |
| **Ultimate ready** | Ring goes from α 0.5 to `energy.full` at α 1.0. The cost arc is complete. **No glow** | Ring opacity and the complete arc | One 300 ms pulse on becoming ready, plus a sound. **No idle loop** |

**Ready flash:**
- When a slot becomes castable, the rim brightens to `flash.white` and eases out over **200 ms**, paired with a soft UI tick.
  - It fires on the change **into Ready** from Cooldown or Not enough energy: a cooldown that ends with enough energy, or energy that reaches the cost.
  - It never fires when a cooldown ends into Not enough energy, or when a lock (CC) lifts. A late cooldown event on a slot that is already Ready doesn't fire it again.
- No scale pop: drive it with a material parameter, not a render transform.
- The flash may lead the real ready time by 0–100 ms (default **0 ms**, tunable). The ASC remains the authority.

**Optional, off by default:** a cursor cooldown ring (§4.18).

**Ability tooltip** (tier 3, on demand):
- **When:** hovering a slot's disc with the cursor, after a **0.3 s** delay (`DA_UIMetrics.TooltipHoverDelay`), or for every slot while the "show details" key is held (`UGenInputConfig::ShowTooltipsAction`, no delay). It fades in and out over `motion.fast`. It never takes input: hover is polled, so the game keeps the mouse.
- **Content, generated from the ability's live data** (`GenAbilityTooltip::Build`, the same functions the game uses for damage, radius and shot parameters, so the tooltip can't drift from the tuning):
  - Header: name, key label (`accent.brass`), then cast time, cooldown, energy cost, range and, for fed spells, the feed interval and cap, on one line.
  - Description: the ability's `Description` (`FText`, authored per asset), with named arguments filled from its values (`{Damage}`, `{Radius}`, `{CastTime}`...).
  - Fed spells: one line per threshold from 0 to the cap, with the hold time ("2 flammes (0,6 s) : 34 dégâts + explosion 1,5 m"). Other spells: their effect, then their windows, durations and states.
  - Numbers use the current culture (`FText::AsNumber`); distances in metres, durations in seconds.
- **Look:** `Common/WBP_Tooltip` (C++ base `UGenAbilityTooltip`). `bg.panelRaised` panel, `radius.panel` corners, 1 px `line.bronze` edge, 8 px padding (HUD panel), text in `TS_Body` (sentence case, left-aligned, wraps at `TooltipMaxWidth`, 480 px at text size 100%: about 80 characters). Tooltips scale fully with the text-size setting (§7.2).
- **Placement:** above the hovered slot's disc, centred, 8 px gap, in a zero-size canvas inside `WBP_AbilitySlot` (it never changes the bar's layout and is never added to the viewport). With the details key, one card per ability in the bar's `DetailsPanel` (a wrap box above the row).

### 4.2 Own health and energy (bottom bar, left block)

- **Block:** 200 px wide, about 50 px tall, vertically centred on the slot row, with a 16 px gap to the first slot.
- **Numbers:** current HP in `TS_Value` (24 px), followed by "/max" in `TS_LabelSecondary`.
  - Numbers snap to the new value; they never count up.
  - At HP ≤ 30%, the current-HP number turns `status.danger`.
- **HP bar:** 200 × 12 px. Uses the full layer order from §4.3, including thin ticks.
- **Energy bar:** 200 × 6 px, **4 segments** separated by 2 px gaps.
  - The fill is `energy.charging`, drawn as one continuous fill clipped by the gaps.
  - At 100%, every segment switches to `energy.full` with a single 200 ms brighten.
  - The energy number appears in a tooltip only. The segments carry the meaning.
- Self health here always uses `team.self` (off-white). This bar needs no self frame: its position already says "you". The shield on it is striped (§2.4).
- This block is the **only** place own HP numbers appear. The self team frame shows no bars (§4.9).

### 4.3 Health bar (shared base: overhead, bottom bar, team frames)

Every health bar in the game uses one base widget (`WBP_HealthBar`) with one fixed **layer order**, from back to front:

1. **Track:** `bar.track`, `radius.bar`.
2. **Lost** (only if recoverable health is enabled): the permanently lost portion, hatched (`hp.lost`), at the **right** end.
   - **Decision:** the bar width stays fixed at base max HP. It does **not** shrink, because shrinking confused Battlerite newcomers.
3. **Recoverable:** `hp.recoverable`, from the end of the current fill to the heal cap.
4. **Damage trail:** `hp.trail`. Uses `motion.trail`: hold **400 ms** at the old value, then drain to the new value over **300 ms** ease-in-out. Rapid hits extend the hold; they do not restart the drain.
5. **Current fill:** the relation colour from `DA_TeamColours`, solid, with a 1 px `line.outline` divider at its leading edge.
6. **Shield:** `hp.shield` with its −45° stripes, from the end of the fill toward the bar end. If shield plus HP exceeds max, the shield overlays inward from the right, with a 1 px `line.outline` edge. The stripes are mandatory on every bar, because white-gold is almost the same luminance as the off-white self fill.
7. **Ticks:** placed at fixed HP values, not as a fixed count, so tankier champions show more ticks.
   - **Heavy tick at every multiple of 100 HP:** 1 px, full height, α 0.90.
   - **Thin tick at multiples of 25 that are not multiples of 100:** 1 px, 60% of bar height, α 0.55. **Thin ticks appear on the self vitals bar only.**
   - At the current 200 max HP, the vitals bar shows 6 thin ticks and 1 heavy tick. Overhead bars show 1 heavy tick.
   - Bars under 8 px tall (team frames), summons and neutral objects get **no ticks**.
8. **Outline:** 1 px `line.outline` around the whole bar.

**Layers allowed on overhead bars:** track, lost, recoverable, trail, fill, shield and heavy ticks only.

**Behaviour:**
- **Heal:** the fill rises over 150 ms ease-out. A `heal` number appears only if damage numbers are enabled.
- **Hit flash:** the fill colour lerps 25% toward `flash.white` (in linear) for **80 ms**. The bar flashes; the HUD never does. On self bars the fill is already off-white, so the flash inverts: the fill dips 25% toward `bar.track` for the same 80 ms.
- **Low HP (self only):** at ≤ 30% (tunable), the outer 1 px outline (the vitals bar outline, and on the overhead self bar the line **outside** the off-white frame) becomes `status.danger` and pulses at **1 Hz** (sine) between α 0.6 and 1.0. After 5 s it holds a static outline. The off-white self frame itself never changes colour. Other characters get no low-HP treatment, because bar length already carries it.
- **MaxHealth changes** update the bar immediately. Bind MaxHealth as well as Health.

### 4.4 Overhead stack (nameplate)

This is the most important HUD element. It is projected to screen space and keeps a **fixed pixel size at every camera zoom**: bars never scale with zoom (Art Bible §9). The only size change is the player's own "Overhead scale" accessibility setting (§7.2), which is never linked to the camera. The stack has no badge.

Bar sizes follow Art Bible §9: health bar **~80 × 8 px at 1080p**, energy bar **4 px tall directly below it**, fill in the team colour from `DA_TeamColours`, and an **off-white frame on the self bar**. Sizes below are track sizes (§2.9); the 1 px `line.outline` around each bar sits inside the 2 px gaps.

Rows, from top to bottom. Every row is **always reserved**: hide content with `Hidden`, never `Collapsed`, so the stack never shifts.

| Row | Others (ally / enemy / neutral) | Self | Height |
|---|---|---|---|
| Name **or** state word | `TS_Name` 20 px in `text.relation.*`, outlined, max 10 characters then "…". **Replaced** by the `TS_StatusWord` while a state is active | No name. State word only (row reserved) | 24 (line box) |
| Gap | | | 2 |
| State duration | 2 px `status.duration` line, 60 px wide, centred, drains linearly. Only while a state with a known duration is active | Same | 2 |
| Gap | | | 2 |
| HP bar | **80 × 8 px** track, solid relation fill. **Enemy:** a 4 px chevron point outside the right end of the track, in `team.enemy` with the `line.outline` edge (the Art Bible enemy notch/chevron cue). **Ally and neutral:** plain ends | **80 × 8 px** track, then a 1 px `line.outline` separator, then a **2 px `team.self` off-white frame**, then the outer 1 px `line.outline`. Footprint 86 × 14 px plus the outer line. The separator keeps the off-white frame from merging with the off-white fill | 8 / 14 |
| Gap | | | 2 |
| Energy | **80 × 4 px**, 4 segments with 2 px gaps, directly below the HP bar | 80 × 4 px, aligned to the HP track | 4 |
| Gap | | | 2 |
| Cast or channel | 80 × 4 px, visible only while casting (§4.5) | 80 × 4 px | 4 |
| **Total** | **50 px** | **56 px** | |

**Rules:**
- **Anchoring:**
  - The HP bar is horizontally centred on the head-socket projection.
  - The bottom of the reserved cast row sits **20 px** (tunable) above the projected head point.
  - Positions snap to whole physical px (§8.5). **No smoothing or lag.**
- **Z-order when stacks overlap:** allies at the bottom, enemies above them, self on top. **No automatic declutter or offset.**
- **No HP numbers overhead.** Numbers appear only in the vitals block. The prototype's overhead HP number is removed.
- **Untouchable** (`State.Untouchable`, ≤ 0.5 s, CharacterGuidelines §3.5): the whole stack drops to α 0.30. We dim instead of hiding so that a moving character can still be tracked. `State.Leaping` dims only if the ability also grants `State.Untouchable`.
- **Enemy ultimate-ready marker:** a 6 px `energy.full` dot at the right end of the energy bar while that champion's energy is at 100%. It can be toggled in settings.
- **Off-screen characters:** the stack is hidden and an edge indicator appears instead (§4.14).
- **Dead characters:** the stack is hidden (`State.Dead`).
- **Ground rings** (world space) belong to the Art Bible, not the UI: every hero gets a team ring drawn with `M_VFX_Telegraph` (Art Bible §4.3, §7.5). Self: **thicker ring + direction notch**. Ally: solid ring. Enemy: notched or chevron ring. Each gets the 1 px keyline. The UI only supplies the relation through the team-colour MPC (§8.6). The self ring is the second self cue after the framed bar.
- Killstreak, MVP and cosmetic flair **never** appear on overhead bars.
- Slot numbers (1–3) are not shown overhead. If callouts need them later, show them in the team frame only, in `TS_Label` 20 px.

### 4.5 Cast and channel bars

**Decision:**
- The prototype's centred 260 px cast bar is **removed**.
- Casts show in the overhead stack for **everyone**. Enemies need to see them to react; this follows Battlerite.
- Optional setting "Large self cast bar" (off by default): 240 × 8 px at y 876–884, 8 px above the status row. It shows no ability name unless "Show ability name" is on.

| Rule | Value |
|---|---|
| Show threshold | Hidden for casts under **150 ms** |
| Cast | `cast.fill` fills **left to right**, linear |
| Channel | `cast.fill` drains **right to left**, linear. Direction is the non-colour cue |
| Complete | Hide with a 100 ms fade (the row stays reserved) |
| Interrupted or cancelled | The bar snaps to full width in `cast.interrupted` for **300 ms solid**, then fades over **150 ms** (450 ms total). Plays the interrupt sound |
| Charge-up abilities | Same bar, with a 1 px `text.primary` tick at the minimum-charge point |
| Source of truth | `State.Casting` plus a replicated `FGenCastInfo` (start time in **server world time**, duration, type, interrupted flag). Progress = (`GetWorld()->GetGameState()->GetServerWorldTimeSeconds()` − start) / duration, computed locally each frame |

**Time sync:**
- The game state resyncs server time every `ServerWorldTimeSecondsUpdateFrequency` seconds (engine default 5 s, **verify on our engine**).
- Up to **50 ms** of drift is acceptable (tunable).

### 4.6 Status effects

**Overhead (all characters):**
- One state word at a time, chosen by priority.
- Words are short CAPS: STUNNED, FEARED, SILENCED, ROOTED, COUNTER, SLOWED, WEAKENED, HASTE. The word is the non-colour cue.

| Priority | Category | Tags (create missing ones under `State.*`) | Word |
|---|---|---|---|
| 1 | Hard CC | `State.Stunned` (exists), `State.Incapacitated` (ends on any damage), `State.Feared` (CharacterGuidelines §3.2) | STUNNED (stun and incapacitate), FEARED |
| 2 | Silence | `State.Silenced` | SILENCED |
| 3 | Root | `State.Rooted` | ROOTED |
| 4 | Counter or parry stance | `State.Countering` | COUNTER |
| 5 | Snare or slow | `State.Slowed` (driven by the move-speed GE) | SLOWED |
| 6 | Weaken (−25 % damage dealt) | `State.Weakened` | WEAKENED |
| 7 | Notable buffs | `State.Buff.*` | e.g. HASTE |
| — | Airborne (leap) | `State.Leaping` (exists) | No word. Dims only with `State.Untouchable` |
| — | Untouchable | `State.Untouchable` | No word. The stack dims (§4.4) |
| — | CC immunity (Resilience) | `State.CCImmune` (CharacterGuidelines §3.3) | IMMUNE for the first 1 s, then no word; the world halo (Art Bible §7.6) shows the rest of the 1.5 s |

- **Decision (Gen):** Counter sits under hard CC, silence and root, because imposed control matters more to the reader than a chosen stance.
- The table lives in `DA_UIStatusPriority` (Gameplay Tag → priority, label `FText`, category, icon), not in widget code.

**Own status row** (y 892–924, centred on x 960):
- **Size and order:** icons are 32 px with 4 px gaps. Buffs grow leftward from the centre line; debuffs grow rightward. At most 6 per side, then a "+n" in `TS_Label`.
- **Glyph:** the status's Art Bible §7.6 motif in its §7.6 hue (§2.11).
- **Buff:** `radius.panel` tile with a 1 px `status.buff` border.
- **Debuff:** 2 px `status.debuff` (neutral) border, plus an 8 px down-chevron corner mark. Border width and chevron are the cue; the border never uses a hue.
- **Duration:**
  - Whole seconds in `TS_Label` (20 px, outlined), drawn **inside** the icon at the bottom-right.
  - Hidden for effects of 30 s or longer.
  - **No sweep on status icons.** The sweep means cooldown only (Don't #5).
- **Motion:** appear with a 100 ms fade, expire with a 75 ms fade. No empty placeholder slots.

**Other players' durations:**
- Enemy and ally states come from **replicated tags only**.
- In Mixed replication mode, GE durations are not replicated to non-owners. The overhead duration line therefore needs a replicated duration (a cue parameter or an attribute). Without one, the line is omitted.

### 4.7 Damage and heal numbers (restrained)

- **Setting:** Off / **Reduced (default)** / On, plus "Show damage taken" (off).
  - Reduced and On show damage **dealt by** the local player and heals they give or receive.
  - Damage **taken** is off by default, because the bar already shows it.
- **Style:** `TS_DamageNumber`, 24 px, fixed size. No scaling for big hits; there are no crits. (Damage-number style is part of the Art Bible §12 Q17 proposal.)
  - Damage dealt: `text.primary`.
  - Heal: `heal` `#7FD14F`, with a "+" prefix.
  - Damage taken (if enabled): `status.danger`, with a "−" prefix.
- **Position:** store the **world** hit point and re-project it every frame on the overhead layer, so numbers stay attached to the world as the camera follows.
  - **Never over the hero** (Art Bible §9: combat text is short-lived and never covers the hero). Clamp the spawn point's screen y to above the top of the target's overhead stack, offset 24 px sideways toward the side the hit came from. The hero body is about 110 px tall at 1080p (Art Bible §0), so a number at the hit point would cover it.
- **Motion:**
  - Reduced: no rise, 500 ms lifetime with a fade.
  - On: rises 24 px (as a screen offset) over a **700 ms** lifetime, ease-out. Opacity holds, then fades over the last 250 ms.
- **Limits:** at most **12** live numbers. Ticks on the same target within 150 ms merge into one number.

### 4.8 Round plaque (top centre)

- **Panel:** 480 × 56 px, `bg.panel`, `radius.panel`, 1 px `line.bronze`.
- **Layout:** `[ally pips] [ally team HP %] [timer] [enemy team HP %] [enemy pips]`.
- **Timer:**
  - `TS_Timer` 28 px, format `m:ss`.
  - At ≤ 10 s it switches to `s.s` (one decimal), stays `text.primary` and stays static (no pulse, no colour change).
- **Pips:**
  - 3 per side (best of 5, first to 3), 10 px, 6 px gaps.
  - **Different pip shape per team** (Art Bible §9): ally pips are circles; enemy pips are diamonds (a square rotated 45°), echoing the angular enemy cue.
  - Won rounds are filled in the **active preset's** Ally or Enemy colour from `DA_TeamColours` (§2.3).
  - Unwon rounds are a 1 px `text.secondary` outline. Filled vs hollow is the non-colour cue for won vs unwon; shape is the cue for the team.
- **Team HP %:** `TS_Value`, `text.secondary` (tunable: can be hidden).
- **No word labels.** Icons and numbers only.

### 4.9 Team frames (top corners)

- **Frames:** 3 per side, each **240 × 44 px**, with 8 px gaps (block height 148 px, y 32–180).
  - Allies sit top-left, **self first**, marked by a 2 px `team.self` (off-white) left edge.
  - Enemies sit top-right, mirrored.
- **Ally and enemy frame contents** (40 + 4 + 128 + 4 + 64 = 240 px):

  | Element | Spec |
  |---|---|
  | Portrait | 40 px circle |
  | Ultimate-ready pip | 8 px, `energy.full`, overlaid on the portrait's bottom-right |
  | Name | `TS_Name` 20 px |
  | HP bar | 128 × 6 px (base widget, no ticks). Enemy frames add the 4 px chevron point on the outer end, as on overhead bars |
  | Energy bar | 128 × 4 px, 4 segments |
  | Loadout icons | 3 × 20 px with 2 px gaps (64 px) on the outer side. Enemy picks appear here during the round intro (§4.11) |

  Vertically, name 20 + 2 + HP 6 + 2 + energy 4 = 34 px, centred in the 44 px frame.
- **Self frame:** portrait, name, ultimate pip and loadout **only**. No HP or energy bars; those live in the vitals block.
- **Dead:** portrait desaturated to 0% at α 0.5, a skull glyph, and the bars hidden.
- **Disconnected:** a plug glyph.

### 4.10 Kill feed (spectator or opt-in only)

In a 3v3, a death is already shown by the skull on the team frame, the overhead stack disappearing and the death flash and puff (Art Bible §7.9). Gen has no death time-slow: world time dilation is ruled out (Art Bible §8). Battlerite's feed is undocumented, so the feed is **not** part of the default player HUD.

- **Availability:** always on in spectator mode. For players, "Kill feed" is a setting, **off by default**.
- **Position:** top-right, under the enemy frames: x 1588, y 196, 300 px wide. This clears the clear zone at both 16:9 and 16:10.
- **Rows:** at most **3**, each 32 px tall with 8 px gaps (y 196–308), on `bg.panelRaised`.
- **Format:** `[killer 24 px portrait with relation ring] [name] ▸ [victim portrait] [name]`.
  - Relation rings follow the Art Bible §4.3 cues: self thicker, ally solid, enemy notched.
  - Names use `TS_Name` in `text.relation.*` (≥ 6.4:1 on the row).
  - No weapon or ability icons.
- **Lifetime:** 5 s. Enters over 150 ms (fade plus an 8 px slide from the right) and exits with a 100 ms fade.

### 4.11 Round banner and round intro

| | Round banner (end of round, sudden death) | Round intro (start of each round) |
|---|---|---|
| Position | Centred horizontally, vertically centred on y = 324 | Same |
| Strip | 640 × 64 px `bg.panel`, fixed α 0.90 | 640 × 96 px `bg.panel`, fixed α 0.90 |
| Content | `TS_Banner` 48 px: "ROUND WON" (`team.ally`, 6.7:1), "ROUND LOST" (`team.enemy`, 4.0:1; large text needs ≥ 3:1), "SUDDEN DEATH" (`text.primary`). The banner strip is fixed at α 0.90 and ignores the panel-opacity setting: at α 0.70 the vermillion would drop to 2.9:1. Ratios are measured over `ref.floorMax` | `TS_Label` "ROUND 2" above a `TS_Banner` countdown 3 → 2 → 1, in `text.primary` |
| Timing | Fade in over 200 ms with an 8 px rise, hold **1200 ms**, fade out over 150 ms (1.55 s) | 1000 ms per digit. Digits cut with a 100 ms cross-fade. The strip fades in during the first 200 ms and fades out over the last 150 ms. **Total ≤ 3 s** |
| Also | Paired with the round sound cue and the cosmetic last-kill hit-stop (Art Bible §7.9, ≤ 120 ms) | Enemy loadout icons fade into the enemy team frames (200 ms) during the intro |

- Neither sequence exceeds 3 s.
- Neither is a full-screen panel. Rewards and stats live on the post-match screen only.

### 4.12 Telegraphs and ground indicators

Ground indicators are specified by **Art Bible §7.5** (and §4.3 for colours and keylines). This section only adds the UI-side parameters. On any difference, §7.5 wins.

- **Rendering:** `M_VFX_Telegraph`, **unlit only** (emissive-only decal or unlit translucent projected mesh), at a fixed value. **No lit (GBuffer) decals**: with the Blendable GBuffer they would change value between the sun band and the shadow band (Art Bible §7.5, §10 #13).
- **Relation colour from the viewer's point of view** (from `DA_TeamColours`):
  - Your own aim preview uses `team.self`.
  - Your allies' indicators use `team.ally`.
  - Enemy indicators use `team.enemy`, **plus the chevron or hazard-stripe pattern** so they read without colour, sorted above ally decals (Art Bible §4.3, §7.5).
  - All follow the active preset.
- **Fill:** Art Bible §7.5 range **15–30%**.
  - Own and ally: α **0.20** (range 0.15–0.25).
  - Enemy: α **0.15**, carrying the static hazard stripes.
  - The fill is a secondary cue and has **no contrast floor**.
- **Border:**
  - **2 screen px** at α 0.90 (Art Bible §7.5 allows 2–4 px at 1080p; 2 px is the default, wider only if a playtest asks), in the relation colour, equal to the hitbox radius.
  - Plus the Art Bible §4.3 **1 px keyline**, which must reach **≥ 3:1 against the floor**. Polarity is set per arena: dark (`line.outline`) if the floor band's minimum V ≥ 40%, light (`line.keylineLight`) if its maximum V ≤ 40%, both (double keyline) if the band straddles 40%. Vermillion alone is only ≈ 1.5:1 against a V 40% floor, so the keyline is never optional.
  - Width is constant at any camera distance; compute it in the material with `fwidth` / screen-space derivatives.
  - Against the dark keyline, the border colours measure: self 18.0:1, ally 8.7:1, enemy 5.2:1 (High contrast enemy 7.7:1).
  - Overlapping previews in a 3v3 stay separable because the border is crisp and the fill is faint.
- **Hitbox match:** the aim preview must match the real hitbox exactly. Any mismatch is a bug (Art Bible §7.1 rule 1, §10 #3).
- **Who sees what** (Art Bible §7.2 and §7.5; enemy skillshot decals are still Art Bible §12 Q10):
  - Self: the full aim preview while aiming (aim line for skillshots).
  - Enemies: no ground decal for skillshots. Telegraphs only for **delayed AoEs and ultimates** (opt-in per ability).
  - Everything else is read from animation.
- **Vocabulary** (Art Bible §7.5): circle = get out, cone = side-step, line = projectile or charge, donut = go to the centre. Never reuse a shape for another meaning.
- **Hazard stripes vs photosensitivity:** the enemy stripes the Art Bible requires are **static** and drawn inside the 15% fill, so they stay low-contrast. Never animate or scroll them, and never draw them at full opacity (§5.4).
- **Pickup and objective timers:** a filling ring around the spawn point, in the world.
- **Boundary:** the sudden-death boundary is one crisp `world.boundary` line (`text.primary` at α 0.9). The area outside it is darkened or desaturated.
- **Stealth proximity:**
  - A small indicator, shown only to the threatened player.
  - Three steps by distance band, shown with `proximity.*` (`text.primary` at α 0.5 / 0.75 / 1.0) **and** 1 / 2 / 3 chevrons.
- **Floor rule** (owned by Art Bible §4.1 and §4.2; repeated here because HUD contrast is measured against it):
  - Arena floors sit at HSV V 25–55% overall, each arena within a band ≤ 20 points wide, saturation ≤ 35%.
  - In a greyscale screenshot of the brightest lit area, no floor exceeds `ref.floorMax` `#8C8C8C` (V 55%).
  - Every arena must pass the Art Bible §10 #1 greyscale test, in which telegraphs, team colours and pickups stay distinguishable.

### 4.13 Menus and champion select

Menus follow **Art Bible §9**: flat, **dark, warm** panels ([TASTE #1], [TASTE #2], [TASTE #3]), so the champion models stand out against the backdrop; heroes shown in 3D over a blurred arena; **no heavy fantasy-RPG frames**.

- **Backdrop:** a warm-lit blurred arena (low sun or torchlight; UMG BackgroundBlur strength 10, tunable), lit at the arena's normal exposure, behind the 3D champions. Mood per [TASTE #3]: dusk plum-brown sky, torches and drifting embers; the saturated gold and orange live **here, in the 3D scene**, never in the panels. Champions get a soft, slightly cool rim light so they separate from the warm, dark panels. Never a flat black, empty screen.
  - `menu.scrim` sits behind panel areas only, so the champions stay at full brightness.
  - Nav bars and text bars over imagery use a flat `menu.panel` backing.
- **Panels and cards:** flat `menu.panel`, `radius.panel`, 16 px padding, 12 px gutters, 1 px `menu.line` edge. No gradients, bevels, filigree, ornamental corners or painted textures.
  - **Text:** `menu.text.primary` and `menu.text.secondary` (§2.1).
  - **Hover:** `menu.panelHover`, 100 ms.
  - **Selected or focused:** 2 px `menu.accent` outline.
  - **Primary action:** one `menu.cta` button per screen (§2.1); every other button is a `menu.panel` card.
  - **Disabled:** `menu.text.disabled` plus a lock glyph or "Not available" text. **Disabled controls never take the hover state.**
- **In-match game menu** (`WBP_GameMenu`, over live gameplay): uses the HUD tokens (`bg.panelRaised`, `text.*`, `accent.brass`), translucent over the match instead of opaque.
- **Navigation:**
  - Full keyboard and controller navigation from day one.
  - Every activatable screen defines `GetDesiredFocusTarget`.
  - Back is **Esc**.
- **Champion select:**
  - Both teams' slots side by side: allies left, enemies right.
  - Each slot has a 4 px relation-colour top stripe (a fill, not a border) and a lock-in state (filled stripe plus a check glyph).
  - On the dark `menu.panel` the stripes need no keyline (self 15.3:1, ally 7.4:1, enemy 4.4:1). Shape cues as elsewhere: the self stripe is 6 px instead of 4 (thicker), ally stripes are plain, enemy stripes end in the chevron point.
  - Names on slots use the relation tint (§2.2), next to the stripe.
  - Loadout presets are selectable in one click.
  - Champion name in `TS_Title`; descriptions in `TS_Body`, sentence case, left-aligned.
- **Notifications:** prefer non-blocking toasts. Use a modal (`UI.Layer.Modal`) only for destructive confirmations such as "Leave match?".
- **New information:** tutorials and new-UI explanations live in the lobby or in practice, **never mid-combat**.

### 4.14 Off-screen edge indicators

Drawn by the overhead layer and clamped **48 px inside the SafeZone** rectangle. No animation, except where stated below.

| Indicator | Shape and size | Colour | α | Shown when | Tier | Max |
|---|---|---|---|---|---|---|
| Ally | 16 px **solid** arrowhead (filled triangle) pointing at the ally | `team.ally` | 0.70 | Ally off-screen and alive | Informational | 2 |
| Enemy | 16 px **open chevron** (the Art Bible enemy cue) | `team.enemy` | 0.70 | Enemy off-screen **and visible to the team** (setting, default on) | Informational | 3 |
| Enemy ultimate wind-up | 24 px arrow | `team.enemy` | 1.00 | During the wind-up, if that enemy is off-screen and visible to the team | Critical | 3 |
| Ping | 16 px ping glyph | Pinger's relation colour | 0.70 | Ping location off-screen (§4.15) | Important | 3 |

- The ultimate arrow fades in over 200 ms and is paired with the ultimate wind-up sound.
- Off-screen indicators never reveal information the team does not already have (Don't #9).

### 4.15 Pings and quick-chat

- **World glyph:** 24 px, in the pinger's relation colour, outlined, drawn at the pinged location on the overhead layer.
- **Caption:** one line in `TS_Label` in the caption band, for example "KAEL: ENEMY HERE".
- **Lifetime:** 3 s, then a 150 ms fade.
- **Limits:** at most 3 active pings per player. Rate limit: 1 ping per 1 s per player; extra presses are ignored.
- **Sound:** one soft UI sound per ping.
- **Wheel:** quick-chat options use the same glyph family. Strings are `FText`.

### 4.16 Scoreboard

- **Trigger:** hold Tab. It is a **hold-to-show overlay inside `WBP_HUDLayout`** (`HUD.Slot.Scoreboard`), not a menu.
  - InputMode stays Game, nothing takes focus, and movement is **not** blocked.
- **Panel:** 720 × 320 px, centred, `bg.panelRaised`, `radius.panel`, 16 px padding.
- **Contents:** six rows, one per player (allies first), each 40 px: portrait, name (`TS_Name`, relation-tinted), loadout icons, damage, healing, kills, deaths (`TS_Value`). Column headers are glyphs or `TS_LabelSecondary`. **No graphs.**
- **Motion:** 100 ms fade in, 75 ms fade out.

### 4.17 Dead and spectating state

While the local player is dead (`UI.State.Dead` → `UI.State.Spectating`):
- The bottom bar collapses to **one `TS_Label` line**: "SPECTATING <ALLY NAME>", with LMB and RMB glyphs to cycle allies.
- The round timer stays in the plaque.
- The status row and the self cast bar are hidden. Captions stay.
- The camera follows the chosen ally. Relation colours stay from the local player's team point of view.
- No other new widgets.

### 4.18 Cursor

- **Implementation:** a software cursor: Project Settings → User Interface → Software Cursors, mapping `Default` → `WBP_Cursor`.
- **Default look:** a 24 px crosshair-arrow in `text.primary` with a 1 px `line.outline` outline.
- **Settings:** colour (any relation-safe colour), size 100–200%, high-visibility outline (2 px).
- **Cursor cooldown ring** (optional, off by default): a 2 px ring with a 28 px radius around the cursor, showing the last-pressed ability's cooldown. It drains clockwise, like the slot sweep. Battlerite players asked for this because the bar was hard to read in peripheral vision.

---

## 5. Motion and feedback

### 5.1 Motion classes and tokens

| Class | Definition | Ceiling |
|---|---|---|
| **Transition** | Enter, exit or state change | ≤ 300 ms |
| **One-shot flash** | Flash with hold and fade | ≤ 500 ms in total |
| **Hold** | Static state (banner hold, low-HP outline, trail hold) | Any length |
| **Lifetime** | Self-expiring element (damage number, ping, kill-feed row) | As specified per component |
| **Real-time drain** | Cooldown sweep, cast bar, duration line, timer | Real time, linear |

| Token | Duration | Easing | Use |
|---|---|---|---|
| `motion.exit` | 75 ms | ease-in | Status icon expire, scoreboard out |
| `motion.instant` | 100 ms | ease-out (enter) / ease-in (exit) | Hover, press feedback, status icon appear, cast-bar hide, scoreboard in, kill-feed out, digit cross-fade |
| `motion.fast` | 150 ms | ease-out (enter) / ease-in (exit) | Heal fill rise, tooltip fade, kill-feed in, banner out, interrupt fade |
| `motion.standard` | 200 ms | ease-out | Ready flash, energy-full brighten, panel show, banner in, ultimate arrow in |
| `motion.slow` | 300 ms | ease-out | Menu panel open, ultimate-ready pulse |
| `motion.trail` | hold 400 ms + drain 300 ms | ease-in-out | Damage trail only |
| `motion.banner.hold` | 1200 ms | — | Round banner |

**Easing in UE:**
- Entrances use **Cubic Out**, about CSS `(0, 0, 0.2, 1)`: a UMG curve asset or `FMath::InterpEaseOut`, exponent 3.
- Exits use **Quadratic In**, about `(0.4, 0, 1, 1)`. Exits take about 75% of the entrance duration and never exceed 150 ms.
- **Linear only** for anything that represents real time.
- **Named exceptions:** the damage trail is ease-in-out; the low-HP pulse is a 1 Hz sine.

**Input feedback** (press, cast start, failed-cast dim) starts within **100 ms** of the input. It is predicted client-side and corrected if the server rejects it.

### 5.2 Specific timings

| Event | Class | Timing |
|---|---|---|
| Hit flash on health bar | Flash | 80 ms |
| Damage trail | Hold + transition | Hold 400 ms, drain 300 ms ease-in-out |
| Interrupt flash | Flash | 300 ms solid + 150 ms fade |
| Cooldown ready flash | Flash | 200 ms |
| Ultimate ready pulse | Flash | 300 ms, once |
| Low-HP outline pulse (self) | Hold | 1 Hz sine, α 0.6–1.0, stops after 5 s and goes static |
| Damage number | Lifetime | Reduced: 500 ms fade. On: 700 ms with a 24 px rise |
| Kill-feed row | Lifetime | 150 ms in, 5 s life, 100 ms out |
| Round banner | Transition + hold | 200 in / 1200 hold / 150 out |
| Round intro | Lifetime | 3 × 1000 ms, total ≤ 3 s |
| Status icon | Transition | 100 ms in, 75 ms out |
| Ping | Lifetime | 3 s + 150 ms fade |
| World hit-stop (if used) | — | Owned by Art Bible §8: basic projectile ≈ 0 ms, heavy melee 40–60 ms, ultimate or kill 80–120 ms, **120 ms hard cap**, cosmetic and per client. The UI adds no latency on top |

### 5.3 Must never animate

- HP, energy or score numbers counting up or down. Values **snap**. No `InterpolateToValue` in combat.
- HUD panels shaking, bobbing or moving with the camera.
- Idle loops, ambient sparkles, shimmer or breathing glows on any frame, slot or bar.
- The position or size of HUD panels during combat. Render-transform scale pops on slots are forbidden; use material flashes instead.
- The overhead stack position. It is pinned to the head with no easing, and its rows never collapse.
- Cooldown digits bouncing or scaling on every tick.
- Colour cycling or rainbow effects of any kind.

### 5.4 Flash and pulse limits

- Flashes follow the **Art Bible §9.1** photosensitivity rule (WCAG 2.3.1): at most **3 flashes in any 1 s** within any **341 × 256 px** box at 1080p, unless the flash covers ≤ 25 % of that box (≈ 21 800 px, about 1 % of the screen). UI flashes count **together** with world impact flashes, and the Art Bible flash governor applies to both. A 64 px slot-rim flash is well under the area limit; a full-width banner must fade, never flash.
- No element may pulse faster than **3 Hz**. Default pulses are 1 Hz.
- **No saturated full-screen red flashes.** An optional damage vignette is edge-only, at most α 0.15, lasts 300 ms, and can be turned off ("Full-screen effects" 0%).
- Use eased fades, never hard on/off blinks.
- No flashing sequence may last longer than 5 s.
- No moving, high-contrast stripe patterns larger than a single slot.

### 5.5 Audio pairing

- **Timing:** trigger each key UI sound and its visual in the **same game-thread frame**. Never delay or pre-trigger audio. Verify once with a high-speed capture.
- **No audio-only cues:** every UI sound has a visual.
- **Mix:** UI sounds use the `SC_UI` sound class. A sound mix keeps them **8 dB below** `SC_Combat` (range 6–10 dB, tunable). UI sounds are less frequent than combat sounds.

| Event | Sound | Visual |
|---|---|---|
| Ability off cooldown | Soft tick | Ready flash |
| Ultimate ready | Low chime | Ring pulse |
| Cast interrupted (self or target) | Short muted thud | Interrupted cast bar |
| Crowd control applied to self | Dull hit | State word + locked slots |
| Own HP crosses 30% downward | One-shot heartbeat (no loop) | Danger outline |
| Not enough energy, or ability on cooldown (press) | Muted click, rate-limited to 1 per 300 ms | 100 ms slot dim (no shake) |
| Enemy ultimate wind-up off-screen | Ultimate wind-up sound | Edge arrow |
| Ping | Soft ping | Ping glyph + caption |
| Round start / win / loss | Match-begin / round-win / round-loss cues | Round intro / banner |
| Kill | Subtle sting | Skull on the team frame (plus a kill-feed row if enabled) |

---

## 6. Clarity rules (hard rules)

**Do:**
1. Put combat-critical state on the character or the floor. Edges hold only stable information.
2. Use the relation colour (Self, Ally, Enemy, Neutral) from `DA_TeamColours` (Art Bible §4.3, read through `UGenUISubsystem`) for every team-related pixel: bars, frames, pips, feed, telegraph borders, ground rings, outlines, projectile ground markers, pings and edge indicators. Never on a hero's body or a spell's core.
3. Pair every colour cue with a shape, glyph, text or direction cue: the self frame, the enemy chevron, pip shape, hatching, shield stripes, lock glyph, border width, fill direction.
4. Put a near-black neutral track behind every bar and a 1 px dark outline around every world-space bar.
5. Use fixed-value HP ticks (heavy every 100 HP everywhere; thin every 25 HP on the self vitals bar only).
6. Show one state word per character, chosen by the priority table.
7. Keep the centre 60% × 60% free of persistent widgets.
8. Make time-based motion linear. The damage trail uses ease-in-out and the low-HP pulse a sine; everything else eases out on enter and in on exit.
9. Measure contrast against the defined worst cases (`ref.floorMax`, `ref.vfxMax` with outline) and record the ratios in the PR.
10. Test every change in PIE (2 clients + dedicated server) on the brightest and darkest arena areas.

**Don't:**
1. Don't use green as a team colour, or put red against green for any team or state pair (Art Bible §4.3).
2. Don't use pure white for resting text, or pure black for panels. **Exceptions:** `flash.white` (momentary flashes only), and text, panels and tracks in the High-contrast preset.
3. Don't add an element whose constant presence you cannot justify. Don't add empty placeholder slots.
4. Don't show HP numbers overhead. Own HP appears in exactly two places: the overhead stack and the vitals block.
5. Don't use the same visual for cooldown and duration. The sweep means cooldown; the rim ring and digits mean duration.
6. Don't use ornament, filigree, glows, textures with painted bevels or decorative fonts in the combat HUD, or heavy fantasy-RPG frames in menus (Art Bible §9).
7. Don't use full-screen overlays (other than an optional edge vignette at α 0.15 or less), screen-wide flashes or HUD shake.
8. Don't introduce new UI or tutorials mid-combat.
9. Don't let customization reveal information that is otherwise hidden.
10. Don't bake text into textures.
11. Don't hard-code colours, fonts, sizes or key names in widgets, materials or VFX.
12. Don't let cosmetics change an ability's silhouette, colour family or telegraph.
13. Don't reuse a hue for a second meaning. Orange-vermillion is the enemy; amber is energy; green is heal; off-white is self. Inside a health bar, a solid fill is HP, a striped bright segment is shield and a dark hatch is lost HP.
14. Don't add a saturated UI hue (OKLCH chroma ≥ 0.10) within 30° of a team hue, or a status hue within 30° of another status hue (Art Bible §4.3).

---

## 7. Accessibility requirements

### 7.1 Minimums (release blockers)

| Requirement | Value |
|---|---|
| Essential text height | ≥ 18 physical px (cap height + descender from font metrics) at every supported resolution, enforced by the DPI floor (§8.8). Applies to key labels, cooldown digits, status durations and names |
| Text contrast | ≥ 4.5:1 against the backing plate (at minimum panel opacity over `ref.floorMax`) or, for world text, against its outline. ≥ 3:1 for text ≥ 36 px and for inactive menu text |
| Graphic contrast | ≥ 3:1 for bar fill vs track, telegraph keyline vs the floor (Art Bible §4.3), and icon vs tile (WCAG 1.4.11; 2.99:1 fails). For the cooldown sweep: swept-region luminance ≤ 35% of the unswept region, **plus** the mandatory number and desaturation. Where 3:1 is impossible (e.g. recoverable vs track, shield vs the self fill, ally vs enemy), a non-colour cue is mandatory |
| Minimum graphic sizes | Bars ≥ 4 px tall, gaps ≥ 2 px, lines ≥ 1 physical px |
| High-contrast mode | ≥ 7:1 for text and for every bar fill, outline and glyph against its immediate background, except the documented exemptions (§2.3) |
| Colour | No essential information by colour alone (§6, Art Bible §4.3). In every preset, ally vs enemy is ≥ 1.5:1 contrast and ΔE2000 ≥ 20, and every saturated UI hue is ≥ 30° (OKLCH) from each team hue |
| Flashes | ≤ 3 flashes in any 1 s per 341 × 256 px box (Art Bible §9.1), pulses ≤ 3 Hz, no saturated red flashes |
| Text blocks | Sentence case, left-aligned, 1.5× line height, ≤ 80 characters per line |
| Captions | ≤ 40 characters per line, ≤ 2 lines, a speaker prefix, backing opacity 0–100% (default 80%; text keeps its outline at any opacity). **On by default** for announcer events |
| Critical audio cues | Each has a visual equivalent: the edge arrow for off-screen enemy ultimate wind-ups (§4.14), a caption for objective spawns |

### 7.2 Settings to expose (first UMG HUD)

| Setting | Default | Range |
|---|---|---|
| HUD scale | 100% | 75–150%, 5% steps, with live preview and a numeric label. Applied per cluster (§8.8) |
| HUD width | 16:9 | Full / 16:9 |
| Overhead scale | 100% | 75–150%. A fixed user choice: overhead bars still never scale with camera zoom (Art Bible §9) |
| Text size | 100% | 100–200%. Menus, tooltips, captions and names scale fully. Key labels, cooldown digits, HP numbers and the timer are capped at 150%. **Icons do not scale with text.** Clusters grow away from their anchor edge |
| Team colours | Default | Default / Deuteranopia-friendly / Protanopia-friendly / Tritanopia-friendly / High contrast, read from `DA_TeamColours` (Art Bible §4.3). Custom only if Art Bible §12 Q12 adds it |
| High-contrast HUD | Off | On / Off |
| Panel opacity | 80% | 70–100% |
| Damage numbers | Reduced | Off / Reduced / On, plus "Show damage taken" (off) |
| Kill feed | Off | On / Off (always on in spectator mode) |
| Ally / enemy names | On | On / Off |
| Enemy ultimate-ready markers | On | On / Off |
| Off-screen enemy indicators | On | On / Off (ally indicators and ultimate arrows are always on) |
| Large self cast bar | Off | On / Off, plus ability name |
| Cursor | Default | Colour, size 100–200%, high-visibility outline |
| Cursor cooldown ring | Off | On / Off |
| Reduce motion | Off | Damage-number rise, kill-feed slide and banner rise → fades. Low-HP pulse → static outline. Ultimate-ready pulse → a single 200 ms brighten |
| Flash effects intensity | 100% | 0–100% (label: "Flash effects intensity", never "epilepsy mode") |
| Screen shake | 100% | 0–100%. **One** global scaler for every shake source, with no per-ability exceptions |
| Full-screen effects | 100% | 0–100% (vignettes, damage overlays) |
| Captions | Announcer | Off / Announcer / All, plus size and backing opacity |

Store all of these in the `UGameUserSettings` subclass. They are **client-only** and never replicate.

### 7.3 Accessibility tiers

- **Ship-blocking:** everything in §7.1, plus team colour presets, HUD scale, text size, screen-shake and flash sliders, reduce motion, and the cursor options.
- **Later:** a rearrangeable HUD, and custom colour pickers (team colours only if Art Bible §12 Q12 decides for a picker).

### 7.4 Test workflow

1. Capture screenshots on the brightest and darkest arena areas.
2. Measure key pairs with the Colour Contrast Analyser: text vs backing, fill vs track, telegraph outline vs edge, team colour vs floor.
3. View each new screen under `UWidgetBlueprintLibrary::SetColorVisionDeficiencyType` in **simulation** mode (`bCorrectDeficiency = false`, Severity 1 = full simulation on the 0–1 scale, Art Bible §4.4) for Deuteranope, Protanope and Tritanope. This is the Art Bible §10 #6 test applied to the UI.
   - This is a QA tool only. Do **not** ship global daltonization as the player fix.
   - Avoid Retainer Boxes in the HUD. A reported engine issue (UE-315856, §10) shows wrong colours under CVD simulation with Retainer Boxes. Re-check it on our engine version.
4. Record the ratios in the PR.
5. Before release, playtest with at least one colourblind player. With about 8% of men colourblind, about **4 in 10 all-male 3v3 lobbies** (6 players) include at least one colourblind player.

---

## 8. Unreal implementation rules

### 8.1 Architecture (CommonUI, modelled on Lyra)

**Engine setup:**
- Game Viewport Client Class = `CommonGameViewportClient`.
- Enable CommonUI's **Enhanced Input support** (Project Settings → Common UI Input Settings), because the game already uses Enhanced Input.
- Define Click and Back as `UInputAction`s in the Common UI Input Data asset.

**Root:**
- One `UGenPrimaryGameLayout` per local player. It is **our own** `UCommonUserWidget` modelled on Lyra's; we do not depend on Lyra's CommonGame or UIExtension plugins.
- It holds four `CommonActivatableWidgetStack`s registered by tag:
  - `UI.Layer.Game` (HUD)
  - `UI.Layer.GameMenu` (in-match interactive panels)
  - `UI.Layer.Menu` (game menu, settings)
  - `UI.Layer.Modal` (confirmations, errors)
- **Never call `AddToViewport` ad hoc.**

**HUD layout:**
- `WBP_HUDLayout` (C++ base `UGenHUDLayout`) is the **only** widget ever pushed onto `UI.Layer.Game`. A stack shows only its top widget.
- Widget tree:
  - Root Canvas
    - `WBP_OverheadLayer` (z 0, full screen, outside the SafeZone; a plain `CommonUserWidget`)
    - SafeZone (z 1)
      - Overlay: each cluster is an Overlay slot aligned to its edge or corner, with 32 px padding
- Named slots: `HUD.Slot.AbilityBar`, `HUD.Slot.Vitals`, `HUD.Slot.StatusRow`, `HUD.Slot.TeamFrames.Ally`, `HUD.Slot.TeamFrames.Enemy`, `HUD.Slot.RoundInfo`, `HUD.Slot.KillFeed`, `HUD.Slot.Captions`, `HUD.Slot.Scoreboard`, `HUD.Slot.Banner`.
- Slots are filled through **`DA_UIStateMap`** (tag → widget class) only, so practice, 3v3 and spectator modes can swap widgets.
- Esc pushes the **game menu** (`WBP_GameMenu`, which **does not pause**: online matches keep running) onto `UI.Layer.Menu`.

**Activatable vs plain widgets:**
- Activatable: the HUD layout, menus, modals.
- Plain `CommonUserWidget`: every HUD piece (frames, slots, bars, overhead layer, nameplates, numbers, scoreboard overlay).

**Input configs:**
- HUD layout: `FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown, EMouseLockMode::LockAlways, /*bHideCursorDuringViewportCapture*/ false)`. Also set `bShowMouseCursor = true` on `AGenPlayerController`; Game mode otherwise hides it.
- Game menu and settings: `InputMode = Menu`.
- The scoreboard is not a menu and does not change input mode (§4.16).
- Never return null from `GetDesiredInputConfig`.
- Never call `SetIgnoreMoveInput`, `SetMouseCaptureMode` or `SetHideCursorDuringCapture` from widgets.
- Debug with `CommonUI.DumpActivatableTree`.

**Decoupling:**
- Gameplay code never includes UMG headers or holds widget classes.
- It broadcasts UI state tags (`UI.State.RoundIntro`, `UI.State.Dead`, `UI.State.Spectating`) through `UGenUIStateSubsystem`.
- `DA_UIStateMap` decides which widget fills which slot and which slots hide (for example, collapse the bottom bar to the spectating line while dead).
- Log every push, pop and slot change under a `LogGenUI` category.

### 8.2 Multiplayer rules

- **Local only:**
  - Widgets exist only on the owning client.
  - Create them only after `IsLocalController()`, and guard with `!IsRunningDedicatedServer()`.
  - Never create widgets or WidgetComponents on the dedicated server.
- **Initialization:**
  - Fire an "ASC ready" event from both `OnRep_PlayerState` (clients) and `PossessedBy` (server or listen host).
  - The ASC lives on `AGenPlayerState`, in Mixed mode.
- **Other players' state:** ally, enemy and overhead UI use **only replicated attributes and tags**. In Mixed mode, GameplayEffects and their durations reach only the owning client.
- **PlayerState replication:** `AGenPlayerState` already calls `SetNetUpdateFrequency(100)`. Keep it that way, and don't write the `NetUpdateFrequency` member directly.
- **Cooldowns:**
  - `CommitAbility` applies the cooldown GE **predictively** on the owning client, so the `Cooldown.Ability.*` tag and GE exist locally on the same frame. Nothing waits for the server.
  - Bind `OnActiveGameplayEffectAddedDelegateToSelf` and `RegisterGameplayTagEvent(Cooldown.Ability.X, EGameplayTagEventType::NewOrRemoved)`.
  - Read the time with `GetActiveEffectsTimeRemainingAndDuration`.
  - Recompute when the server's GE replicates and replaces the predicted one. Follow the `AsyncTaskCooldownChanged` pattern from tranek's GASDocumentation.
  - Count down locally from start time and duration; never query every frame.
  - "Activating" (§4.1) covers only the window between `TryActivateAbility` and a late commit.
  - The ASC is the authority on availability.

### 8.3 Where tokens live

| Asset | Type | Contents |
|---|---|---|
| `DA_TeamColours` | Owned by the Art Bible (§3.9, §4.3), in `Content/Gen/Rendering/` | Self, Ally, Enemy (and Neutral, if accepted) per preset: Default, Deuteranopia-, Protanopia-, Tritanopia-friendly, High contrast. **The only source of team colours.** The UI never copies them into its own assets |
| Team-colour MPC | Owned by the Art Bible (`MPC_*` in `Content/Gen/Rendering/`, §3.9; proposed name `MPC_TeamColours`) | The active preset's linear colours, read by `M_VFX_Telegraph`, the stencil outline, ground rings and ground markers |
| `DA_UIPalette` | `UGenUIPalette : UPrimaryDataAsset` (C++) | All §2.1–2.5 **UI** colour tokens as `FLinearColor` (e.g. `Text_Primary`), including the menu tokens and the copies of Art Bible reserved hues, plus **UI-side overrides per team-colour preset** (status, text, panel, track and outline; e.g. the High-contrast table in §2.3). No team colours |
| `DA_UIMetrics` | `UPrimaryDataAsset` | Spacing (`Space_0_5`…), radius, bar sizes, tick values (25 / 100 HP), thresholds (self low HP 30%, cast show 150 ms), motion durations, layout y values from §3.1 |
| `DA_UIStatusPriority` | `UPrimaryDataAsset` | Gameplay Tag → priority, word (`FText`), category (buff / debuff / CC), icon |
| `DA_UIStateMap` | `UPrimaryDataAsset` | UI state tag → slot contents and visibility |
| `DA_UIKeyGlyphs` | `UPrimaryDataAsset` | `FKey` → glyph texture or short `FText` (e.g. SpaceBar → "SPC"), falling back to `FKey::GetDisplayName(false)` |
| `TS_*` | `UCommonTextStyle` | §2.6 styles |
| `BS_*` | `UCommonButtonStyle` | `BS_Primary`, `BS_Ghost`, `BS_Card`, sounds included. The disabled style has no hover state |
| `BRS_*` | `UCommonBorderStyle` | `BRS_Panel`, `BRS_PanelRaised`, `BRS_Track`. Textureless **RoundedBox** brushes (radius plus a 1 px outline via `FSlateBrushOutlineSettings`) |
| `CF_UI_DPI` | Curve | DPI curve (§8.8) |

`UGenUISubsystem` (a LocalPlayer subsystem) resolves the active preset from settings and does three things:
1. Exposes `GetRelationColour(EGenRelation)` (read from `DA_TeamColours`), `GetToken(FName)` (read from `DA_UIPalette`, with the preset's UI overrides applied) and the derived `text.relation.*` values.
2. Pushes the team colours into the one team-colour MPC and into Niagara user parameters. There is **no separate UI MPC**.
3. Broadcasts `OnPaletteChanged`, so widgets re-tint without a restart.

**Button text gotcha:** override `NativeOnCurrentTextStyleChanged` and call `SetStyle(GetCurrentTextStyleClass())` on the bound `CommonTextBlock`.

### 8.4 Event-driven data flow

- **Bindings:** set Project Settings → Widget Designer (Team) → **Property Binding Rule = Prevent and Error**, so existing bindings surface too. No UMG property bindings, and no `NativeTick` for gameplay values.
- **Initialization order:** `NativeConstruct` subscribes to the "ASC ready" event (§8.2), which fires immediately if the ASC is already valid. Attributes are bound **inside that callback**:
  - Read Health, MaxHealth, Energy, MaxEnergy (and RecoverableHealth if added) once.
  - Bind `GetGameplayAttributeValueChangeDelegate` for each.
  - Bind `RegisterGameplayTagEvent(Tag, NewOrRemoved)` for **each tag listed in `DA_UIStatusPriority`** and for each `Cooldown.Ability.*` tag. Registering the parent `State.*` fires only on 0↔1 and does not say which child changed. `RegisterGenericGameplayTagEvent` is the alternative.
  - Remove every binding in `NativeDestruct`.
- **Overhead stacks** for other characters bind when the simulated proxy's `OnRep_PlayerState` fires.
- Do not feed the UI only from `PostGameplayEffectExecute`. Duration GEs bypass it.
- **Viewmodels (optional):**
  - Classes: `UGenVM_Vitals` (Health, MaxHealth, RecoverableHealth, Energy, MaxEnergy), `UGenVM_Ability` (icon, key label, cooldown start and duration, charges, energy cost, state enum) and `UGenVM_Cast` (start, duration, type, interrupted).
  - A C++ binder on the local PlayerController listens to the ASC and writes into the viewmodels. Widgets bind one-way and never include GAS headers.
  - Check the MVVM plugin's status in our engine version before adopting it **(verify on our engine)**. Without MVVM, apply the same rule through C++ setter functions on the widgets.
- **Key labels:**
  - Read the current mapping for each `InputTag.Ability.*` from the Enhanced Input user settings, resolve it through `DA_UIKeyGlyphs`, and update on rebind.
  - UE keys are virtual-key based: a binding to `EKeys::Q` shows "Q" on any layout.
- **Cast bar:** progress is computed locally from `FGenCastInfo` in server world time (§4.5). Only the fill widget is Volatile.

### 8.5 Overhead stack implementation

**Primary approach:** `WBP_OverheadLayer` (C++ `UGenOverheadLayer`), a plain child of `WBP_HUDLayout`, below every HUD cluster and outside the SafeZone (§8.1). Each frame, in C++, it:
- projects every character's head socket with `UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition` (or divides viewport pixels by `GetViewportScale`);
- snaps positions in **physical** px: `round(pos × DPI) / DPI`;
- positions pooled `WBP_OverheadStack` instances in the z-order from §4.4;
- hides off-screen or dead stacks;
- draws edge indicators (§4.14), damage numbers (re-projected each frame) and ping glyphs.

This is cheap for 6 actors.

**Fallback:** a `UWidgetComponent` in **Screen** space with a fixed draw size and Draw at Desired Size off. No activatable widgets or `CommonButton`s inside; `CommonTextBlock` is allowed.

**Never** use World-space widget components for overhead bars: they are blurry and tilted, and each needs its own render target.

**Relation:** computed client-side with `AGenCharacterBase::AreTeamsEnemies` against the local player's team (or the followed player's, in spectator mode). It is never replicated as a colour.

### 8.6 Team colour through VFX and world

- **Telegraph, ground-ring and decal material:** `M_VFX_Telegraph` (Unlit; owned by Art Bible §3.2 and §7.5). It reads the relation colour from the team-colour MPC using a per-instance scalar `RelationIndex` that matches the Art Bible §3.4 stencil values: **1 self, 2 ally, 3 enemy**, plus 4 neutral (UI proposal).
  - Each client sets the index locally for its own point of view.
  - Parameters the UI needs on it: FillAlpha, BorderAlpha, BorderWidthPx (2, computed through `fwidth`), KeylineWidthPx (1), KeylinePolarity (dark / light / both, per arena, Art Bible §4.3), EnemyPattern (chevron or hazard stripes, static).
- **Character outlines:** the Art Bible §3.4 stencil post-process reads the same MPC.
- **Niagara:** systems take `User.RelationColour`, used **only** on the Art Bible team channels: the projectile ground marker (with its solid or chevron shape cue and keyline, §7.1 rule 6) and the `M_VFX_Erode` team-tint rim. Never on a spell's core (Art Bible §4.3). Never hard-code red or blue into ability VFX.
- **Pings and edge indicators** use the same subsystem.

### 8.7 Performance

- **Canvas Panel:** exactly one, at the `WBP_HUDLayout` root (§8.1).
  - Everything below uses Overlay, HorizontalBox, VerticalBox or SizeBox, with **Spacers** for gaps.
  - Never combine a Scale Box with a Size Box, except for the per-cluster HUD-scale Scale Box (§8.8).
  - No Rich Text in the combat HUD.
- **Invalidation:**
  - Wrap static chrome (frames, slot rims, key labels) in Invalidation Boxes, or validate `Slate.EnableGlobalInvalidation=1`.
  - Mark the cast-bar fill, active sweeps and the overhead layer **Volatile**.
- **Change values through material parameters or colour** (paint invalidation), not through size or render transform (layout).
  - Never add or remove children to show state.
  - Toggle `Hidden` / `Collapsed` (overhead rows: `Hidden` only), or use pools.
- **Materials:**

  | Material | Parameters |
  |---|---|
  | `M_UI_Bar` (Domain User Interface, Translucent) | Fill, TrailFill, RecoverableFill, LostFill, ShieldFill (drawn with its −45° stripes), TickSmall (25, 0 = off), TickLarge (100), MaxValue, FillColour, Flash, FlashInvert (self), SelfFrame (0/1), EnemyCap (0/1) |
  | `M_UI_CooldownSweep` | Progress, DimAmount, RimFlash, ActiveRing |
  | `M_UI_SegmentArc` | Segments, Funded, Colour, FullOutline (0/1) |
  | `M_VFX_Telegraph` | Owned by the Art Bible; UI parameters in §8.6 |

  - Create **one MID per widget** in `NativeConstruct`, cache it, and call `SetScalarParameterValue` only when a value changes.
  - Ready flashes, pulses and trail drains run in the material or as a short code tween. UMG Sequencer animations are only for one-off menu transitions.
- **Pooling:** damage numbers, overhead stacks, edge indicators and pings use `FUserWidgetPool`, with Slate resources released in `ReleaseSlateResources`. Never call `CreateWidget` per hit.
- **Retainer Boxes:** none, unless Slate Insights proves draw calls are the bottleneck.
- **Async loading:** menus, settings and the post-match screen use soft class references.
- **Budget:** the cap is the Art Bible §3.0 **UI pass budget: ≤ 0.3 ms GPU** at 1080p, High tier, inside the 8.3 ms frame at 120 fps. It covers the whole HUD plus the overhead layer with 6 characters, in the §10 #4 clutter scene. The Art Bible's precedence applies: if the UI goes over, simplify the implementation (fewer layers, cheaper materials, more invalidation), never the budget.
  - **Game thread (tunable, UI-side):** ≤ 0.3 ms for HUD plus overhead, out of the Art Bible's ≤ 4 ms CPU and game-thread budget for 6 heroes.
  - **Measure in a packaged Development or Test build**, not PIE (Art Bible §3.0; PIE carries editor overhead), with `stat unit`, `stat gpu`, Slate Insights and the Widget Reflector (Ctrl+Shift+W). Attach the capture to the PR when the HUD changes significantly.
  - Team channels in the UI (bars, frames, pips, edge indicators) never scale down on lower tiers (Art Bible §3.8). If they cost too much on Low, make them cheaper; never drop them.

### 8.8 DPI, scale and safe zone

- **DPI:**
  - Scale Rule = **Shortest Side**.
  - Curve `CF_UI_DPI`: 720 → **0.9** (floor), 1080 → **1.0**, 1440 → 1.333, 2160 → 2.0. Author everything at 1920×1080, DPI 1.0.
  - The 0.9 floor keeps `TS_Label` at ≥ 18 physical px. At 1280×720 the layout space is about 1422×800; at 1280×800 (Steam Deck) the DPI is about 0.92. Both must fit.
- **HUD scale:**
  - Apply it **per cluster** on the HUD layout only: a Scale Box (Stretch = User Specified, `UserSpecifiedScale` = HUD scale) inside each anchored cluster.
  - Do **not** use Application Scale. It would scale menus, the settings slider while it is being dragged, and the overhead layer.
- **Overhead scale** is applied to the overhead layer only.
- **Text size:** implemented by `UGenTextBlock : UCommonTextBlock`, which multiplies its style's font size by `UGenUISubsystem::TextScale` within the §7.2 caps. `UCommonTextStyle` is a class default object and must not be edited at runtime.
- **SafeZone:** a child of the root Canvas, next to the overhead layer (§8.1). The 32 px margin sits inside it.
- **Anchors:** each cluster aligns to its own edge or corner. Clusters grow away from that edge at larger scales. Never anchor everything to the centre.
- **HUD width 16:9:** the cluster Overlay is constrained to a centred 16:9 rectangle (Size Box with a computed max width).
- **Before merging, check:**
  - Aspect ratios: 16:9, 16:10 (1920×1200), 21:9 (both HUD width modes).
  - Resolutions: 1280×720, 1280×800, 1920×1080, 2560×1440, 3840×2160.
  - Scales: HUD 75% and 150%; text 200% at HUD 100%; text 150% at HUD 150%.

### 8.9 Fonts

- Import Barlow, Barlow Semi Condensed and Barlow Condensed as **Runtime** composite fonts with shared Font Faces (`FNT_Barlow`, `FF_Barlow_*`).
- Keep small HUD text (≤ 28 px) on hinted bitmap rendering.
- **MSDF (tunable):** consider it only for `TS_Banner`, `TS_DamageNumber` and `TS_StatusWord`.
  - Distance-field rendering must be enabled in the project settings and is set per Font Face.
  - Verify that the outline settings render correctly with it **(verify on our engine)**.
  - Otherwise keep these styles as bitmap text with an outline.

### 8.10 Naming and folders

```
Content/Gen/UI/
  Foundation/    DA_UIPalette, DA_UIMetrics, DA_UIStatusPriority, DA_UIStateMap, DA_UIKeyGlyphs,
                 TS_*, BS_*, BRS_*, CF_UI_DPI
  Fonts/         FNT_Barlow*, FF_Barlow_*
  Materials/     M_UI_Bar, M_UI_CooldownSweep, M_UI_SegmentArc, MI_UI_*
Content/Gen/Rendering/   DA_TeamColours, the team-colour MPC, Masters/M_VFX_Telegraph
                 (owned by the Art Bible, §3.9; listed here so UI work knows where to read them)
  Textures/      T_UI_Glyph_*, T_UI_Portrait_*, Icons/Abilities/T_UI_Ability_<Champion>_<Slot>
  HUD/           WBP_HUDLayout, WBP_AbilityBar, WBP_AbilitySlot, WBP_Vitals, WBP_HealthBar,
                 WBP_EnergyBar, WBP_StatusRow, WBP_StatusIcon, WBP_TeamFrame, WBP_RoundPlaque,
                 WBP_KillFeed, WBP_KillFeedRow, WBP_RoundBanner, WBP_RoundIntro, WBP_Captions,
                 WBP_Scoreboard, WBP_SpectatorBar
  Overhead/      WBP_OverheadLayer, WBP_OverheadStack, WBP_DamageNumber, WBP_EdgeIndicator, WBP_Ping
  Menus/         WBP_MainMenu, WBP_ChampionSelect, WBP_Settings, WBP_GameMenu, WBP_PostMatch
  Common/        WBP_Button, WBP_Card, WBP_Tooltip, WBP_Modal, WBP_Toast, WBP_Cursor
Source/Gen/UI/   UGenPrimaryGameLayout, UGenHUDLayout, UGenUserWidget (base), UGenOverheadLayer,
                 UGenTextBlock, UGenUIPalette, UGenUISubsystem, UGenUIStateSubsystem, UGenVM_*
```

| Asset kind | Prefix |
|---|---|
| Widget Blueprints | `WBP_` |
| Text, button and border styles | `TS_`, `BS_`, `BRS_` |
| UI materials and instances | `M_UI_`, `MI_UI_` |
| Textures | `T_UI_` |
| Data assets | `DA_UI` |
| Fonts and font faces | `FNT_`, `FF_` |
| Curves | `CF_UI_` (no UI MPCs: team colours use the Art Bible's one `MPC_*`) |

- Widget variables bound from C++ use `BindWidget`, with PascalCase names matching the C++ property (e.g. `HealthBar`, `CooldownText`).
- C++ base classes own the logic. Blueprint subclasses own layout and style only.
- **Localization:** all strings are `FText`. No string concatenation; use `FText::Format`. Leave about 30% headroom for text expansion.

### 8.11 Process (assets and agents)

**LFS locking.** Every `.uasset` and `.umap` is LFS-lockable. This covers widgets, data assets, materials, `TS_`/`BS_`/`BRS_` styles, fonts, textures, curves and MPCs. `DA_TeamColours`, the team-colour MPC and `M_VFX_Telegraph` are Art Bible **hot shared files** (§3.9): lock them only for short, focused edits, push, then unlock immediately.
1. `git pull`.
2. `git lfs lock` **every** `.uasset` / `.umap` you will modify, or replace by creating a new file at its path.
3. If someone else holds a lock (`git lfs locks`), stop and tell the user. Never force-unlock without their agreement.
4. Make the change.
5. After commit and push, `git lfs unlock` each file. If the user is not committing yet, keep the locks and say so in the summary.

**AI agents:** editor work goes through VibeUE (`execute_python_code` with `auto_save: false`) after loading the matching VibeUE skill, as described in `CLAUDE.md`.

**Verification:** a UI change is "working" only after PIE with **2 clients + dedicated server**. Check the self, ally and enemy views.

### 8.12 Mapping from the prototype `AGenHUD`

`AGenHUD`'s `FLinearColor` literals are **linear** values, so they render much more pastel than they read. For example, `(0.25, 0.9, 0.3)` is `#89F395` in sRGB.

| `AGenHUD` member / literal | Current (linear → sRGB) | Replace with | Note |
|---|---|---|---|
| `SelfColor (0.25, 0.9, 0.3)` | `#89F395` green | `team.self` from `DA_TeamColours` (Default `#F2F2F2`, linear (0.888, 0.888, 0.888)) + the off-white self frame | Green is reserved for heal (Art Bible §4.3) |
| `AllyColor (0.2, 0.55, 1.0)` | `#7CC4FF` | `team.ally` from `DA_TeamColours` (Default `#56B4E9`, linear (0.093, 0.456, 0.815)) | Okabe-Ito sky blue |
| `EnemyColor (0.95, 0.2, 0.2)` | `#F97C7C` | `team.enemy` from `DA_TeamColours` (Default `#D55E00`, linear (0.665, 0.112, 0.000)) + the chevron cap | Okabe-Ito vermillion. Never hard-code any of the three: read them through `UGenUISubsystem` |
| `EnergyColor (1.0, 0.75, 0.1)` | `#FFE159` | `energy.charging` `#FFC233`, linear (1.000, 0.539, 0.033) (= Art Bible energy orb); `energy.full` = same + segment outline | Now 4 segments. Amber, 35° (OKLCH) from the enemy hue |
| `CastColor (1.0, 0.55, 0.1)` | `#FFC459` | `cast.fill` `#E9DFC8`, `cast.interrupted` (= `status.danger` `#FF8FB0`) | No longer competes with energy or the enemy colour |
| `DrawBar` background `(0, 0, 0, 0.7)` | black 70% | `bar.track` `#141411` α 0.90 + 1 px `line.outline` | Never pure black |
| `FLinearColor::White` text | `#FFFFFF` | `text.primary` `#F3DEC9` | |
| Cooldown text `(0.6, 0.6, 0.6)` | `#CBCBCB` | Slot states (§4.1); labels use `TS_LabelSecondary` | |
| Death text in `EnemyColor` | red | `UI.State.Dead` → spectating line (§4.17) in `text.primary` | Don't use the enemy colour for your own state |
| `OverheadBarSize (90, 9)` | — | 80 × 8 track for everyone (Art Bible §9); self adds the 1 px separator + 2 px off-white frame (86 × 14 footprint); enemy adds the 4 px chevron cap. Energy 80 × 4 directly below | `DA_UIMetrics` |
| `OverheadOffsetZ 130` | world units | Head socket + 20 px screen offset | Tunable |
| Overhead HP number text | shown | **Removed** | Numbers only in the vitals block |
| Local panel 360 px, HP 18 px tall, energy 8 px | — | Vitals block 200 px: HP 200 × 12, energy 200 × 6 (4 segments) | §4.2 |
| Centred local cast bar 260 × 14 | — | **Removed** → overhead cast bar (+ optional large self bar 240 × 8) | §4.5 |
| Spell list as text `[Slot] Name 3.2s` | — | `WBP_AbilityBar` with 7 circular slots | §4.1 |
| `TActorIterator` per frame + Canvas draws | — | Event-driven widgets + `UGenOverheadLayer` | §8.4–8.5 |

`AGenHUD` stays only as the owner that creates the primary layout on the local client. All drawing moves to UMG. Max HP is currently 200 (`InitMaxHealth(200)`), which gives 1 heavy tick per bar and 6 thin ticks on the vitals bar.

---

## 9. Review checklist for any UI PR

Copy this into the PR description and tick each item.

**Design**
- [ ] **Purpose:** each new element explains gameplay state, and its states and events have a tier (§1.2). Nothing decorative was added to the combat HUD.
- [ ] **Placement:** nothing persistent in the centre clear zone at HUD 100%. Inside the SafeZone with a 32 px margin. Bottom bar ≤ 116 px tall and ≤ 760 px wide. Only `WBP_HUDLayout` is on the `UI.Layer.Game` stack.
- [ ] **Tokens:** no hard-coded colour, font, size, radius or key name. Everything comes from `DA_TeamColours` (team colours only), `DA_UIPalette`, `DA_UIMetrics`, `DA_UIKeyGlyphs` and the `TS_`/`BS_`/`BRS_` styles. New tokens are added to `DA_UIPalette` **and** to this doc. No team colour is copied into a UI asset.
- [ ] **Linear vs sRGB:** every `FLinearColor` was converted from its hex token, not pasted.
- [ ] **Art Bible:** nothing in the PR contradicts `Docs/ArtBible.md`. If it does, the Art Bible wins and this doc is fixed in the same PR. Taste feedback is logged through Art Bible §13.

**Colour and readability**
- [ ] **Relation colours:** self (off-white `#F2F2F2`), ally (sky blue `#56B4E9`) and enemy (vermillion `#D55E00`) come from `DA_TeamColours` and are correct from **all three** PIE viewpoints and in spectator view. Telegraphs, ground rings, ground markers, pips and edge indicators follow the active preset. No team colour on a hero's body or a spell's core.
- [ ] **Shape cues (Art Bible §4.3):** self bar has the off-white frame with its dark separator; enemy bars and stripes have the chevron cap; pips are circles (ally) and diamonds (enemy); edge indicators are solid arrowheads (ally) and open chevrons (enemy); ground rings are thick + notched (self), solid (ally), notched/chevron (enemy).
- [ ] **Bars (Art Bible §9):** overhead HP 80 × 8 track, energy 80 × 4 directly below, fill from `DA_TeamColours`, fixed size at every camera zoom. Shield is striped; lost HP is hatched; nothing else in a bar is.
- [ ] **Reserved hues (Art Bible §4.3):** energy `#FFC233`, heal `#7FD14F`, stun `#FFE07A`, poison/silence `#9E7BD9`, fire `#F5B82E` used only for their meaning. Every saturated UI hue (OKLCH C ≥ 0.10) is ≥ 30° from each team hue of every preset; status hues are ≥ 30° apart. No orange or red-orange UI mark other than the enemy colour.
- [ ] **Status icons:** each glyph reuses its Art Bible §7.6 shape motif and hue. Debuff borders are neutral (width + chevron).
- [ ] **Non-colour cue:** every colour-coded state also has a shape, glyph, text or direction cue.
- [ ] **Text:** essential text ≥ 18 physical px (font metrics). Contrast ≥ 4.5:1 at minimum panel opacity over `ref.floorMax`, or against the outline for world text (≥ 3:1 for large or inactive text). On menus, against `menu.panel`. Ratios listed in the PR.
- [ ] **Graphics:** fill vs track ≥ 3:1, telegraph keyline ≥ 3:1 against the floor with the arena's polarity (Art Bible §4.3), cooldown swept region ≤ 35% luminance, or a documented non-colour cue. Bars ≥ 4 px, lines ≥ 1 physical px.
- [ ] **Telegraphs (Art Bible §7.5):** `M_VFX_Telegraph`, unlit, border 2–4 px, fill 15–30%, enemy chevron or static hazard stripes. Same value in the sun band and the shadow band (Art Bible §10 #13).
- [ ] **Colourblind:** screenshots under Deuteranope, Protanope and Tritanope simulation (Severity 1, correction off). No collisions between enemy and status, debuff, heal or energy in any preset. Ally vs enemy ≥ 1.5:1 and ΔE ≥ 20 in every preset.
- [ ] **High contrast:** ≥ 7:1 measured for text, fills, outlines and glyphs (exemptions per §2.3).
- [ ] **Menus (Art Bible §9):** flat, dark, warm, opaque `menu.*` panels ([TASTE #1], [TASTE #2], [TASTE #3]); `bg.*` and `menu.*` stay low-chroma umber, never navy or pure grey; UI chrome chroma stays below 0.10 (saturated gold and orange only in the 3D backdrop); one `menu.cta` per screen; heroes in 3D over a lit, blurred arena, never an empty black screen; no heavy fantasy-RPG frames, filigree or painted bevels.

**Scale and layout**
- [ ] **Scale:** HUD 75% and 150%; text 200% at HUD 100% and text 150% at HUD 150%, within the §7.2 scopes.
- [ ] **Resolutions:** 1280×720, 1280×800, 1920×1080, 1920×1200 (16:10), 2560×1440, 3840×2160, and 21:9 in both HUD-width modes. No overlap or clipping.
- [ ] **Overhead clump test:** all 6 characters stacked within one screen area. Z-order is correct (self on top) and nothing shifts while casting.

**Motion and audio**
- [ ] **Motion:** durations, classes and easing match §5.1–5.2. Nothing from §5.3 animates. Pulses ≤ 3 Hz; flashes within the Art Bible §9.1 limit. Reduce-motion and flash-intensity settings are respected.
- [ ] **Audio:** key events trigger sound and visual in the same frame, and every sound has a visual. UI sounds use `SC_UI`.

**Engineering**
- [ ] **Data flow:** no UMG property bindings (rule = Prevent and Error), no gameplay `NativeTick`. Bindings are made in the "ASC ready" callback and removed in `NativeDestruct`. MaxHealth and MaxEnergy changes are handled.
- [ ] **Network:** no widgets on the dedicated server. Other players' UI uses only replicated attributes and tags. Cooldown display is correct under prediction with `Net PktLag=150`. Cast bars use server world time.
- [ ] **Telegraphs:** the aim preview matches the hitbox exactly.
- [ ] **Performance:** within the Art Bible §3.0 **UI budget, ≤ 0.3 ms GPU** (and ≤ 0.3 ms game thread), measured in a **packaged** Development or Test build in the clutter scene, not in PIE. One root Canvas Panel, Volatile only where needed, MIDs cached, pools for numbers, stacks, indicators and pings, no Retainer Boxes. Attach the `stat gpu` / Slate Insights capture if the HUD changed significantly.
- [ ] **Navigation:** menus are fully usable with keyboard and with controller. Disabled controls take no hover state.
- [ ] **Localization:** `FText` only, no string concatenation, about 30% expansion headroom, French pass done.
- [ ] **Naming and folders:** match §8.10.
- [ ] **Engine version:** noted in the PR if a rule marked "verify on our engine" was relied on.

**Process**
- [ ] **LFS:** every touched `.uasset` / `.umap` is listed. Locked before editing and unlocked after push, or the locks are kept and the reason stated.
- [ ] **PIE:** verified with 2 clients + dedicated server on the brightest and darkest arena areas. A screenshot of each relation view is attached.
- [ ] **Docs:** if a (tunable) value changed, this document is updated in the same PR.

---

## 10. Sources

### Project documents
- `Docs/ArtBible.md`: authoritative for colours, team cues, reserved hues, status motifs, telegraphs, the menu and HUD direction and the frame budget (see "Relationship to the Art Bible" at the top)
- Okabe-Ito palette (team colours): https://siegal.bio.nyu.edu/color-palette/
- CVD simulation used for the ΔE figures in §2.3: Machado, Oliveira and Fernandes (2009), "A Physiologically-based Model for Simulation of Color Vision Deficiency", severity 1.0 matrices

### Battlerite (primary reference)
- https://www.gamepressure.com/battlerite/user-interface-and-controls/z19611
- https://www.gamepressure.com/battlerite/useful-hints/z29612
- https://battlerite.fandom.com/wiki/Energy
- https://battlerite.fandom.com/wiki/Interrupt
- https://battlerite.fandom.com/wiki/Abilities
- https://battlerite.fandom.com/wiki/Health
- https://battlerite.fandom.com/wiki/Orbs
- https://battlerite.fandom.com/wiki/Energy_Rune
- https://battlerite.fandom.com/wiki/Death
- https://battlerite.fandom.com/wiki/Debuffs
- https://battlerite.fandom.com/wiki/Sudden_Death
- https://battlerite.fandom.com/wiki/Category:Ability_energy_bar_images
- https://battlerite.fandom.com/wiki/Patch_Notes_0.6.187
- https://battlerite.fandom.com/wiki/Patch_Notes_0.8
- https://battlerite.fandom.com/wiki/Patch_Notes_0.10
- https://battlerite.fandom.com/wiki/Patch_Notes_0.11
- https://battlerite.fandom.com/wiki/Patch_Notes_0.12
- https://battlerite.fandom.com/wiki/Patch_Notes_0.12.1.0
- https://battlerite.fandom.com/wiki/Patch_Notes_0.13.0.0
- https://battlerite.fandom.com/wiki/Patch_Notes_0.13.1.0
- https://battlerite.fandom.com/wiki/Patch_Notes_0.14
- https://battlerite.fandom.com/wiki/Patch_Notes_0.14.0.2
- https://battlerite.fandom.com/wiki/Patch_Notes_0.14.0.3
- https://battlerite.fandom.com/wiki/Patch_Notes_1.0 (could not be fetched, HTTP 402; nothing in this document relies on it)
- https://battlerite.fandom.com/wiki/Patch_Notes_1.0.1
- https://blog.stunlock.com/dev-blog-32/
- https://blog.stunlock.com/dev-blog-028/
- https://blog.stunlock.com/battlerite-arena-patch-2-0/
- https://store.steampowered.com/app/879160/
- https://steamcommunity.com/app/504370/discussions/0/343787283760043400
- https://steamcommunity.com/app/504370/discussions/0/152390648084262794/
- https://steamcommunity.com/app/504370/discussions/0/343787920136299213
- https://steamcommunity.com/app/504370/discussions/0/1333474229085838885
- https://steamcommunity.com/app/504370/discussions/0/133259855834309629/?ctp=2
- https://steamcommunity.com/sharedfiles/filedetails/?id=770402959
- https://www.destructoid.com/reviews/review-battlerite/
- https://destructoid.com/?p=208801
- https://mmohuts.com/review/battlerite
- https://www.keengamer.com/articles/reviews/battlerite-free-to-play-review/
- https://www.tentonhammer.com/articles/battlerite-review
- https://pcgamer.com/battlerite-review
- https://forums.penny-arcade.com/discussion/207411/battlerite-i-wanna-rock-n-roll-all-fight
- https://www.pcgamesn.com/battlerite/battlerite-champions
- https://www.fandom.com/articles/battlerite-review
- https://80.lv/articles/battlerite-interiew
- https://en.wikipedia.org/wiki/Battlerite

### Comparable games (LoL, Dota 2, Overwatch, HotS, Valorant, Deadlock, others)
- https://www.surrenderat20.net/2015/07/red-post-collection-hud-update-coming.html?m=1
- https://www.surrenderat20.net/2015/06/63-pbe-update.html?m=1
- https://www.surrenderat20.net/2015/06/red-post-collection-june-4th-patch.html
- https://www.surrenderat20.net/2013/02/improved-health-bars-coming-soon-to.html
- https://destructoid.com/?p=189503
- https://www.leagueoflegends.com/en-us/news/dev/clarity-in-league/
- https://www.sportskeeda.com/esports/riot-dev-talks-clarity-league-legends
- https://wiki.leagueoflegends.com/en-us/Life
- https://mp1st.com/news/lol-update-7-24-features-health-bar-overhaul-new-snowdown-skins-more
- https://www.millenium.org/news/279455.html
- https://en.number13.de/league-of-legends-change-these-settings-as-a-new-player/
- https://labs.invenglobal.com/articles/15067/three-in-game-settings-that-you-need-to-change-in-league-of-legends-ft-geng-nemesis
- https://www.esports.net/news/lol/first-stands-controversial-new-overlay/
- https://dota2.fandom.com/wiki/Health
- https://overwatch.fandom.com/wiki/Hit_points
- https://us.forums.blizzard.com/en/overwatch/t/how-do-different-health-bars-work/850387
- https://us.forums.blizzard.com/en/overwatch/t/health-bar-visibility/566960
- https://www.slashgear.com/overwatch-gets-updated-colorblind-feature-with-nine-color-options-22546973
- https://www.playerassist.com/overwatch-2-change-ui-colors-2/
- https://news.blizzard.com/en-gb/article/20581816/patch-preview-party-frames-portrait-flames-and-killstreak-fame
- https://playvalorant.com/en-us/news/game-updates/preview-the-future-of-valorant-s-interface/
- https://www.oneesports.gg/valorant/valorant-beginners-guide-understanding-the-hud/
- https://esports.gg/guides/valorant/how-to-change-enemy-color-in-valorant/
- https://forums.playdeadlock.com/threads/a11y-colorblind-setting-for-enemy-colors.10548/latest
- https://thegamehaus.com/deadlock/deadlock-major-update-adds-new-heroes-map-changes-hero-reworks-and-more/2026/09/29/
- https://all.gg/news/deadlock-just-got-a-massive-quality-of-life-overhaul/
- https://teamplay.gg/blog/deadlock-city-never-sleeps-update
- https://esports.gg/guides/deadlock/how-to-get-updated-health-bars-in-deadlock
- https://deadlockgame.wiki/settings/health-bar-command/
- https://supervive.wiki.gg/wiki/Armor
- https://www.allclash.com/?p=87432
- https://itch.io/post/2418816
- https://forums.elderscrollsonline.com/en/discussion/comment/2212449
- https://forums.crateentertainment.com/t/cooldowns-unclear-and-inconsistent-plus-irritating-icons/144250
- https://www.curseforge.com/wow/addons/nugscooldownpulse/files/8543804
- https://warcraft.wiki.gg/wiki/UIOBJECT_Cooldown
- https://www.wowace.com/projects/cooldowncount?comment=23
- https://www.gamebanshee.com/kwky8
- https://smite.fandom.com/wiki/File:HUDChat.png
- https://eklipse.gg/help/what-is-hud-streaming/

### UI/UX principles, typography, colour and motion
- https://bonndoc.ulb.uni-bonn.de/xmlui/handle/20.500.11811/14207
- https://www.strayspark.studio/blog/game-ui-ux-design-principles
- https://bugnet.io/blog/how-to-design-a-readable-hud
- https://www.abratabia.com/game-ui-design/hud-design.php
- https://www.nexusmods.com/witcher2/mods/1179
- https://publications.lib.chalmers.se/records/fulltext/111921.pdf
- https://www.gamedeveloper.com/design/user-interface-design-in-video-games
- https://www.gamedeveloper.com/design/game-design-and-gestalt-laws
- https://web.mit.edu/6.813/www/sp16/classes/15-layout/
- https://dl.designresearchsociety.org/iasdr/iasdr2023/fullpapers/6
- https://m2.material.io/design/color/dark-theme
- https://codelabs.developers.google.com/codelabs/design-material-darktheme/
- https://blog.superhuman.com/how-to-design-delightful-dark-themes/
- https://uxplanet.org/8-tips-for-dark-theme-design-8dfc2f8f7ab6
- https://learnui.design/blog/din-similar-fonts.html
- https://fontalternatives.com/compare/barlow-vs-din/
- https://asphalt.fandom.com/wiki/Rajdhani
- https://pimpmytype.com/spacing-all-caps/
- https://codeshack.io/references/css/letter-spacing/
- https://www.gamedeveloper.com/design/icon-design-rules-you-should-know
- https://madegooddesigns.com/?p=5494
- https://www.wayline.io/learn/pixel-art/4
- https://kidscancode.org/godot_recipes/4.x/ui/cooldown_button/
- https://www.oreilly.com/library/view/practical-game-design/9781787121799/a8f56b23-64b3-40ca-a9cc-2ea02d4c1c71.xhtml
- https://feel-docs.moremountains.com/API/class_lofelt_1_1_nice_vibrations_1_1_m_m_progress_bar.html
- https://m1.material.io/motion/duration-easing.html
- https://github.com/material-components/material-components-android/blob/master/docs/theming/Motion.md
- https://lists.w3.org/Archives/Public/public-css-archive/2025Apr/0702.html
- https://developer.apple.com/design/human-interface-guidelines/motion
- https://developer.apple.com/tutorials/data/design/human-interface-guidelines/motion.json
- https://www.nngroup.com/videos/3-response-time-limits-interaction-design/
- https://jakobnielsenphd.substack.com/p/time-scale-ux
- https://ken.ieice.org/ken/paper/20230315OCSt/eng/
- https://scitepress.org/PublishedPapers/2024/124614
- https://srk.shib.live/w/Street_Fighter_6/Game_Data
- https://eastondev.com/blog/es/posts/dev/20260521-game-feedback-feel/
- https://itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.1359-0-199802-S!!PDF-E.pdf
- https://www.tvtechnology.com/opinions/av-synchronization-how-bad-is-bad
- https://aes2.org/publications/elibrary-page/?id=12881
- https://dl.gi.de/items/4ea932e9-ff53-45ed-a4f5-c507a1da561a/full
- https://www.designative.info/2019/03/28/the-ux-of-fortnite-celia-hodent/
- https://www.blinkist.com/books/the-gamers-brain-en
- https://www.gamedeveloper.com/game-platforms/the-cognitive-science-behind-games-user-research
- https://www.gdcvault.com/play/1020867/Developing-UX-Practices-at-Epic
- https://developer.roku.com/dev/docs/graphics

### Accessibility
- https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-101-text-display.md
- https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-102-contrast.md
- https://devdocs.xbox.com/build/core-features/graphics/overviews/screen-areas
- https://devdocs.xbox.com/build/gdk-and-engines/handheld/handheld-guidelines-and-testcases
- https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/101
- https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/102
- https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/103
- https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/104
- https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117
- https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/118
- https://gameaccessibilityguidelines.com/full-list/
- https://gameaccessibilityguidelines.com/ensure-no-essential-information-is-conveyed-by-a-fixed-colour-alone/
- https://gameaccessibilityguidelines.com/ensure-no-essential-information-is-conveyed-by-a-colour-alone/
- https://gameaccessibilityguidelines.com/use-an-easily-readable-default-font-size/
- https://gameaccessibilityguidelines.com/allow-interfaces-to-be-resized/
- https://gameaccessibilityguidelines.com/provide-high-contrast-between-text-and-background/
- https://gameaccessibilityguidelines.com/avoid-flickering-images-and-repetitive-patterns/
- https://gameaccessibilityguidelines.com/avoid-placing-essential-temporary-information-outside-the-players-eye-line/
- https://igda-gasig.org/how/game-accessibility-top-ten-se/
- https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html
- https://developers.meta.com/horizon/design/accessibility-checklist/
- https://www.colourblindawareness.org/colour-blindness/
- https://www.gamedeveloper.com/design/colour-blind-colour-bind-
- https://www.gamepressure.com/newsroom/lol-colorblind-mode-is-useles-for-players-suffering-from-actual-c/za2e03
- https://siegal.bio.nyu.edu/color-palette/
- https://conceptviz.app/blog/okabe-ito-palette-hex-codes-complete-reference
- https://siege.gg/su/team-colors-in-rainbow-six-siege-x-how-to-change-to-purple-green-yellow-and-more
- https://news.ubisoft.com/en-ca/article/r2MFilRathMnz0nZ3zosN/rainbow-six-siege-x-accessibility-spotlight
- https://caniplaythat.com/2020/03/07/visually-impaired-review-splatoon-2/
- https://caniplaythat.com/2020/05/13/top-five-reasons-games-need-subtitles/
- https://caniplaythat.com/?p=9940
- https://overwatch.blizzard.com/en-gb/news/patch-notes/ptr/2018/09
- https://dotesports.com/valorant/news/how-to-change-enemy-highlight-color-in-valorant
- https://eu.forums.blizzard.com/en/overwatch/t/colour-blind-accessibility-issues/30890
- https://us.forums.blizzard.com/en/overwatch/t/overwatch-2-accessibility-and-screen-shake/671231/31
- https://accessible.games/wp-content/uploads/2018/11/AbleGamers_Includification.pdf
- https://accessible.games/accessible-player-experiences/access-patterns/distinguish-this-from-that/
- https://www.pcgamer.com/includification/
- https://docs.unrealengine.com/4.26/en-US/API/Runtime/UMG/Blueprint/UWidgetBlueprintLibrary/SetColorVisionDeficiencyType/index.html
- https://issues.unrealengine.com/issue/UE-315856
- https://forums.unrealengine.com/t/color-vision-deficiency-causing-gpu-performance-issue-related-to-retainer-panel-usage/2745487

### Unreal Engine implementation
- https://x157.github.io/UE5/LyraStarterGame/CommonUI/
- https://x157.github.io/UE5/LyraStarterGame/Input/HUDLayout.html
- https://x157.github.io/UE5/LyraStarterGame/Input/LAS_ShooterGame_StandardHUD.html
- https://x157.github.io/UE5/UIExtension/
- https://medium.com/@donxu29/understanding-modular-ui-system-in-projects-such-as-unreal-lyra-starter-project-no-programming-6f42e266f70a
- https://www.uxisfine.com/blog/search-for-a-better-ui-architecture
- https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/CommonUI/UCommonActivatableWidgetStack
- https://dev.epicgames.com/documentation/en-us/unreal-engine/design-guidelines-for-using-commonui-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/input-fundamentals-for-commonui-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-quickstart-guide-for-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/commonui-input-technical-guide-for-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-best-practices-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-viewmodel-for-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-insights-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/UMG/FUserWidgetPool
- https://unreal-garden.com/tutorials/userwidget-pool/
- https://unreal-garden.com/tutorials/common-ui-button/
- https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/CommonTextStyle
- https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/CommonButtonStyle
- https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/CommonUI/UCommonNumericTextBlock
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FSlateRoundedBoxBrush
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FSlateBrushOutlineSettings
- https://dev.epicgames.com/documentation/en-us/uefn/material-assets-in-unreal-editor-for-fortnite
- https://dev.epicgames.com/documentation/en-us/fortnite/meter-material-in-unreal-editor-for-fortnite
- https://dev.epicgames.com/documentation/fortnite/ui-materials-collection-in-fortnite
- https://tomlooman.com/unreal-engine-umg-circular-progress-bar/
- https://uhiyama-lab.com/en/notes/ue/material-instance-dynamic-effects/
- https://uhiyama-lab.com/en/notes/ue/widget-component-damage-numbers/
- https://www.gamedeveloper.com/programming/ue4cookery-cpp008-widget-component-insides-part-1-screen-space-
- https://sabotane.itch.io/dev-blog/devlog/1089386/utilizing-lyras-indicator-system-in-ue5
- https://forums.unrealengine.com/t/performant-3d-ui/2683721
- https://dev.epicgames.com/documentation/en-us/unreal-engine/font-asset-and-editor-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/using-signed-distance-field-text-rendering-in-unreal-engine
- https://docs.unrealengine.com/4.27/API/Runtime/SlateCore/Fonts/EFontCacheAtlasDataType/index.html
- https://dev.epicgames.com/documentation/en-us/unreal-engine/dpi-scaling-in-unreal-engine
- https://bugnet.io/blog/how-to-fix-unreal-umg-widget-not-scaling-with-dpi-curve
- https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-safe-zones-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/setting-up-tv-safe-zone-debugging-in-unreal-engine
- https://answers.unrealengine.com/questions/343967/view.html
- https://raw.githubusercontent.com/tranek/GASDocumentation/master/README.md
- https://answers.unrealengine.com/questions/881739/view.html
- https://answers.unrealengine.com/questions/748978/view.html
- https://forums.unrealengine.com/t/dedicated-server-trying-to-read-property-hud/422319

## 11. Change log

| Date | Section | Change |
|---|---|---|
| 2026-10-08 | §4.1 Ready flash | Clarified: the flash fires on the change into Ready from Cooldown or Not enough energy, never from Cooldown into Not enough energy (review of Plan 3 Tasks 8–10, M4) |
| 2026-10-08 | §4.1 Ability tooltip | Added: hover (0.3 s) and held "show details" key, content generated from the ability's live data, `bg.panelRaised` / `TS_Body` / `radius.panel` / `motion.fast`. New `DA_UIPalette` tokens `Bg_PanelRaised`, `Accent_Brass` and `DA_UIMetrics` values `TooltipHoverDelay`, `MotionFast`, `HudPanelPadding`, `RadiusPanel`, `PanelOutlineWidth`, `TooltipMaxWidth`, `TooltipGap` |
| 2026-10-08 | §2.5 `cooldown.noEnergy` | Noted the shared C++ constant and the test that keeps the C++ default and `DA_UIPalette` on the token (review of Plan 3 Tasks 8–10, M9) |
