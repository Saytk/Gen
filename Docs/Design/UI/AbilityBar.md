# Ability bar and UI foundation slice: design spec v0

Status: approved in design review on 2026-10-07. Implements part of `Docs/UI_Guidelines.md` (the
authority for every value below; on conflict, `Docs/ArtBible.md` wins). This spec only decides
**scope and structure**; exact values are read from the guide sections cited.

Branch: `ui-ability-bar` (from `curffe-plan1`, so Curffe's abilities are available to test).

## 1. Goal

Replace the prototype's text spell line (`[Primary] Boule de feu  3.2s`) with the real ability bar of
UI_Guidelines §4.1, built on the minimum UI foundation that §8 requires, so the player sees each
spell's key, icon, cooldown and availability at a glance.

## 2. Scope

**In**
- UI foundation slice (§8.1, §8.3, §8.9, §8.10): CommonUI setup, root layout, HUD layout, the token
  data assets and styles the bar needs, Barlow fonts, the cooldown sweep and segment-arc materials.
- The 7-slot ability bar (§4.1, geometry from §3.1) with these states: Ready, Cooldown (sweep +
  desaturation + number), Locked (stunned), Empty (no ability bound), Ultimate arc and Ultimate ready,
  ready flash.
- Key labels from the live Enhanced Input mapping (§8.4), mouse buttons as glyphs, Space as "SPC".
- Generated placeholder ability icons for Curffe (§2.11).
- Removing the text spell line from `AGenHUD`.

**Out (later UI work)**
- Vitals block, overhead stack, cast bar, status row, team frames, round plaque (they stay drawn by
  `AGenHUD`'s Canvas for now, unchanged).
- Settings: HUD scale, text scale, HUD width, colour presets (fixed at 100% / Default).
- EX variants and Shift cost arcs on basic slots, charges, active/recast ring, Activating state,
  Not-enough-energy state (no basic spell costs energy yet; R/F aren't implemented).
- MVVM (C++ setters instead, allowed by §8.4).
- Packaged-build performance capture (stays a checklist item, §9).

## 3. Architecture

| Piece | Kind | Responsibility |
|---|---|---|
| CommonUI plugin + `CommonGameViewportClient` + CommonUI Enhanced Input support + input data asset (Click, Back) | Project setup | §8.1 engine setup |
| `UGenPrimaryGameLayout` | C++ `UCommonUserWidget` (+ `WBP_PrimaryGameLayout`) | Root per local player; 4 `CommonActivatableWidgetStack`s registered by tag `UI.Layer.Game/GameMenu/Menu/Modal`; push/pop API; logs under `LogGenUI` |
| `UGenHUDLayout` | C++ `UCommonActivatableWidget` (+ `WBP_HUDLayout`) | Only widget on `UI.Layer.Game`; Game input config (§8.1, cursor visible); root Canvas → SafeZone → Overlay with the `HUD.Slot.AbilityBar` cluster bottom-centre, 32 px margin. Other named slots are created empty |
| `UGenUISubsystem` | `ULocalPlayerSubsystem` | Loads the token assets; `GetToken(FName)`, metrics access, `GetKeyLabel(UInputAction)` (live mapping → `DA_UIKeyGlyphs`); broadcasts an "ASC ready" event for the local pawn |
| `UGenUIPalette`, `UGenUIMetrics`, `UGenUIKeyGlyphs` | C++ `UPrimaryDataAsset` + `DA_UIPalette`, `DA_UIMetrics`, `DA_UIKeyGlyphs` | Tokens (§2.1–2.5 subset the bar uses), sizes and motion durations, key → glyph/short text |
| `UGenTextBlock` | C++ `UCommonTextBlock` | Base text block (text scale fixed at 1.0 for now) |
| `UGenAbilityBar` | C++ `UCommonUserWidget` (+ `WBP_AbilityBar`) | Builds 7 slots in §4.1 order from `InputTag.Ability.*`; binds on "ASC ready" |
| `UGenAbilitySlot` | C++ `UCommonUserWidget` (+ `WBP_AbilitySlot`) | One slot: icon, key label, cooldown number, state; drives one cached MID per material |
| `M_UI_CooldownSweep`, `M_UI_SegmentArc` | Materials (UI domain) | Sweep (Progress, DimAmount, RimFlash) and ultimate arc (Segments, Funded, Colour) |
| `AGenHUD` | Existing | Creates the primary layout on the local client and pushes `WBP_HUDLayout`; keeps its Canvas drawing for the not-yet-migrated elements; **text spell line removed** |

Gameplay code never includes UMG headers (§8.1 decoupling). Widgets exist only on the owning client,
never on a dedicated server (§8.2).

## 4. Data flow (§8.2, §8.4)

- "ASC ready" fires from `OnRep_PlayerState` (clients) and `PossessedBy` (server/listen host) for the
  local pawn, and immediately for late subscribers. Each slot binds inside that callback and unbinds in
  `NativeDestruct`.
- Slot ↔ ability: the granted spec whose dynamic source tags contain the slot's `InputTag`. Its CDO
  gives the display name, icon (new `Icon` soft texture property on `UGenGameplayAbility`) and cooldown
  tags. Re-resolved when abilities change (respawn).
- Cooldown: `RegisterGameplayTagEvent(CooldownTag, NewOrRemoved)` + `OnActiveGameplayEffectAddedDelegateToSelf`;
  read start/duration with `GetActiveEffectsTimeRemainingAndDuration`; count down locally (predicted
  cooldowns appear the same frame, §8.2). The number updates on a short timer while cooling, not
  `NativeTick`.
- Locked: `State.Stunned` tag event. Ultimate arc: Energy / MaxEnergy attribute change delegates.
- Key labels: the slot's `UInputAction` (from `UGenInputConfig`, InputTag → action) → its current key
  in the active mapping context → `DA_UIKeyGlyphs` (glyph texture or short text, fallback
  `FKey::GetDisplayName(false)`). Never hard-coded.

## 5. Visuals

All from UI_Guidelines: geometry §4.1 and §3.1 (64 px slots, 72 px ultimate, 12 px gaps, 528 px row,
key label 20 px above with 2 px gap); states table §4.1; ready flash 200 ms and ultimate pulse 300 ms
once (§5.2); tokens `text.primary`, `text.secondary`, `line.bronze`, `line.outline`, `bg.panel`,
`cooldown.overlay`, `cooldown.locked`, `energy.charging`, `energy.full`, `flash.white`; styles
`TS_Label` and `TS_Cooldown` (§2.6, Barlow). Colours are converted from the hex tokens to linear, never
pasted. Icons: 256 px, top-left light, one style, flat placeholder art generated by script, passing the
greyscale, 2 px blur and 32 px tests (§2.11); stored as `T_UI_Ability_Curffe_<Slot>`.

## 6. Verification

- Unit tests (pure logic): cooldown number format (§4.1 rules), slot state precedence, key-label
  resolution and fallback, hex → linear conversion of tokens.
- PIE, dedicated server + 2 clients (§8.11): self view shows the bar; cooldown sweep and number correct
  for LMB (no number, <2 s) and RMB (6 s); correct under `Net PktLag=150`; stun → Locked; respawn
  re-binds; no widgets on the server; no persistent widget in the centre clear zone; checked at
  1280×720, 1920×1080, 2560×1440 with screenshots.
- UI review checklist §9 filled in for the items in scope.
