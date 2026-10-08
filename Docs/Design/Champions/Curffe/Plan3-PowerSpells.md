# Curffe Plan 3: Power Spells. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Execution mode: fast mode.**
> - Group consecutive C++ tasks into **batches**: write every task of the batch, then run **one build and one unit-test run** for the whole batch. The per-task "Build to verify it fails" steps are optional inside a batch; the per-task "Build, then run the unit tests" steps collapse into the batch's single run. Commit per task when the tasks touch different files, otherwise once per batch (the message lists the task titles).
> - **Per-task review only for risky tasks**, the ones tagged **Review: required** below. Other tasks get one review per batch.
> - Proposed batches:
>
> | Batch | Tasks | Needs the editor? | Review: required |
> |---|---|---|---|
> | A | 1–5: pure rules, fast feeding, energy costs, Untouchable, Resilience | No (editor closed, `Build.bat`) | Task 3 (energy costs), Task 4 (Untouchable), Task 5 (Resilience) |
> | B | 6–9 and the C++ steps of 10 (Steps 1–7): cancel key, explosion, Living Flame, Combustion, ability-bar energy states | No | — (one batch review; Task 10 also runs the UI §9 checklist) |
> | C | Task 10's editor steps (8–10) and Task 11: editor assets | Yes (VibeUE) | — |
> | D | 12: PIE matrix, then the latency reruns | Yes (PIE) | — |

**Goal:** Curffe gets his energy spells, **Living Flame** (R, 25 energy) and **Combustion** (F, 100 energy: Pyroblast, unlimited flames, fast feeding). The game gets four game-wide systems:
- the **Untouchable** state;
- **Resilience**, which grants immunity to hard CC after 2.5 s of it within 5 s;
- **energy costs** paid on release;
- the **cancel key**.

Everything is verified in PIE with a dedicated server and 3 clients.

**Architecture:**
- **Generic systems** go in `Source/Gen/AbilitySystem/`:
  - `UGenGameplayAbility::EnergyCost`, enforced through `CheckCost`/`ApplyCost`. `CommitAbility` runs at release in `UGenGA_Cast`, so an interrupted cast costs nothing. The ability bar shows it (cost arc, "Not enough energy" state, Task 10).
  - `State.Untouchable`, applied in the shared hit and CC chokepoints: `ResolveIncomingHit`, projectile overlap, `ApplyHardCC`, the damage meta-attribute and knockback.
  - Resilience inside `UGenAbilitySystemComponent::ApplyHardCC`.
  - Feeding on an interval snapshotted at feed start (the Plan 1 fixes already schedule ticks from the feed start). Two generic tags change it: `State.FastFeeding` (half the interval) and `State.FreeResource` (fed units are not spent).
  - The cancel key in `UGenInputConfig`, `AGenPlayerController` and the ASC.
- **Curffe-only code** goes in `Source/Gen/Champions/Curffe/`:
  - `UCurffeGA_LivingFlame` and `UCurffeGA_Combustion`;
  - the `State.Curffe.Ablaze` tag.
- **Pyroblast** is data only: `GA_Pyroblast`, a `UGenGA_Projectile` asset on the LMB slot, requires `State.Curffe.Ablaze`, and `GA_Fireball` is blocked by it.

**Tech Stack:** Unreal Engine 5.8, C++ module `Gen`, Gameplay Ability System (LocalPredicted abilities, ASC on the PlayerState, Mixed replication), Unreal Automation tests, VibeUE MCP for editor and PIE work.

**Spec:** `Docs/Design/Champions/Curffe/Curffe.md` (§2, §3 R/F/LMB, §4, §9) and `Docs/Design/CharacterGuidelines.md` (§3.1, §3.3, §3.5, §4.1). Visuals: `Docs/ArtBible.md` §7.2 and §7.6 (current version, status vocabulary). UI: `Docs/UI_Guidelines.md` §2.5, §4.1 and the §9 checklist.

## Global Constraints

- **Starting point.** Plan 2 is done (branch `curffe-plan2`). Work on `curffe-plan3`, cut from its tip. Never push to `main`; merge only with the user's consent.
- **Plan 2 building blocks** this plan relies on:
  - `UGenGA_Cast`, with `ReleaseCast`/`LaunchCast`, `OnCastLaunched`, `FinishAbility`, `SpawnGroundArea`, `MakeGainSpec` and `SetCastLock`;
  - `AGenGroundArea` and `FGenAreaParams`;
  - `UGenAbilitySystemComponent::ApplyHardCC` and `RemoveTimedStates`;
  - `UGenGE_TimedState` and `UGenGE_TimedMoveSpeed`;
  - `AGenCharacterBase::ResolveIncomingHit`;
  - `GenHitRules`;
  - `GenTestWorld.h`;
  - `UGenStatusVisualsComponent` (appear flash, `OwnerMeshMaterial`) and `StatusVisualConfig`;
  - `M_VFX_StatusShape` (`Colour`, `Opacity`, `RimOnly`, `RimPower`, `Flash`);
  - `GenGameplayTags::GetHardCCTags()` and the cast-lock server window (`UGenAbilitySystemComponent::NoteCastLock`);
  - `CurffeGameplayTags`.
- **Latency baseline.** Plan 1's latency checklist and the charge-bar PIE pass (run on `ui-ability-bar`) are the baseline: `C:/Users/Samy D/Documents/Unreal Projects/Gen/.superpowers/sdd/Plan-AbilityBar/task-9-combined-report.md`. Plan 2 Task 11 adds its own latency reruns. This plan doesn't rerun either; Task 12 adds `NetEmulation.PktLag` reruns of its own risky rows.
- **Code comments in French**, matching the existing code. Docs use English (Canadian spelling).
- **Compiling.** Close the editor cleanly first (`unreal.SystemLibrary.quit_editor()`), then build with `Build.bat`. Never run `Plugins/VibeUE/BuildAndLaunchGame.ps1`.
- **Editor Python:** `execute_python_code` always runs with `auto_save: false`. Save only the assets you changed.
- **LFS locks.**
  - Before modifying an existing `.uasset`, run `git fetch origin` and check `git diff --stat HEAD...origin/main -- Content`. Then run `git lfs locks --verify` and `git lfs lock <path>`, skipping the locks you already own. Never use `--force`.
  - **Hot shared files** (Art Bible §3.9): this plan creates one more master, `Content/Gen/Rendering/Masters/M_VFX_GhostDither`. Tell the user before creating it (Task 11 Step 1) so the other person doesn't create it in parallel.
- **PIE verification** uses a dedicated server and **3 clients**. Clients 1 and 3 are team 0 and client 2 is team 1. Restore Standalone with 1 client afterwards.
- **Starting values (spec §3, copied verbatim; feeding updated by the 2026-10-08 decision, spec commit `8096d2c`):**
  - **Feeding (spec §2).** 0.3 s per flame, at most **3 flames (3 thresholds) per spell**; the Hearth keeps 5 flames (Living Flame still refills it to 5). Holding 3 flames takes about 0.9 s, or 0.45 s with fast feeding. The values live in `CurffeTuning` (`MaxFeedPerSpell`, `FeedInterval`, `FastFeedInterval`), from the parallel `ui-ability-bar` change; code and tests reference those constants, never the literals.
  - **R: Living Flame (25 energy).**
    - Cast **0.1 s**, cooldown **16 s**.
    - The mage becomes living fire: **untouchable for 0.5 s**, can't cast (guidelines §3.5 allows ≤ 0.5 s on R).
    - At the end: a **2.5 m** ring (A) deals **8** damage and **knocks back 3 m**; flames **refill to 5**; then **+30 % move speed for 2 s**, during which he can cast.
  - **F: Combustion (100 energy).**
    - Cast **0.5 s**, interruptible (stun, silence, fear, incapacitate). Energy is spent when the cast completes.
    - On cast: the mage **erupts**, a **3 m** nova (A) for **20** damage and a **3 m** knockback.
    - Then **ablaze for 5 s**:
      1. **Pyroblasts:** LMB becomes a bigger projectile (P), cast 0.35 s, **13** damage, explodes in a **1.2 m** area. Works with every other spell on cooldown.
      2. **Unlimited flames:** the Hearth refills after every spell.
      3. **Fast feeding:** 0.15 s per flame instead of 0.3 s; telegraphs never drop below 0.5 s.
    - Not immune to CC.
  - **Resilience (guidelines §3.3):** after 2.5 s of hard CC within 5 s, the target is immune to hard CC for 1.5 s, with a visible effect.
  - **Invulnerability (guidelines §3.5):** 0.5 s or less. Untouchable means projectiles pass through and damage is ignored.
  - **Costs (guidelines §3.1):** cooldown and energy are only spent when the spell actually goes off. A cancelled or interrupted cast costs nothing.
  - **Cancel key (guidelines §3.1):** "A cancel key cancels the current cast."
- **Key slots** (AZERTY): R is `InputTag.Ability.3`, F is `InputTag.Ability.Ultimate`, and Pyroblast shares `InputTag.Ability.Primary` with Fireball.
- **Every new or changed ability sets** `InputTag`, `DisplayName` (French), `CooldownTags`/`CooldownDuration` (none for F), `EnergyCost` and `Icon` (the property exists since the `ui-ability-bar` merge).
- **Hard crowd control** = stun, silence, fear, incapacitate (CharacterGuidelines §3.2). Interrupts, activation blocks, resilience and the bar's Locked state go through `GenGameplayTags::GetHardCCTags()` (Plan 2), never `State.Stunned` alone.
- **Status visuals** follow the current Art Bible §7.6 (Task 11 Step 6): `State.CCImmune` = one white ring flash, then a thin white halo; `State.Untouchable` = the body dithers to a ghosted look (masked, no translucency), the outline and team ring stay; `State.Curffe.Ablaze` = on the body only, no ground area. Each has a unique shape and never the `State.Shielded` shell motif.
- **Folders.** Generic code goes in `Source/Gen/AbilitySystem/**`, `Source/Gen/Character/` or `Source/Gen/Player/`. Curffe-only code goes in `Source/Gen/Champions/Curffe/`.

## Review Focus

1. **Ablaze starts or ends while a spell is feeding.**
   - Expected: each machine keeps the interval it had when its feeding started, and the server validates against its own interval.
   - An honest client never sees `Nourrissage corrigé`. At worst, a feed that began in the last ~0.15 s of ablaze is clamped by one flame.
   - Pinned by Task 1 (`Gen.Feeding.FastInterval`) and Task 12 W9.
2. **Energy exactly at the cost.** For example, 25 energy for Living Flame, 100 for Combustion, or 99.99 after rounding. Expected: 25.0 and 100.0 pass, and anything under fails, with no float drift. Pinned by Task 1 (`Gen.Energy.CanAfford`) and Task 12 W1 and W5.
3. **A hard CC lands while the target is untouchable, while immune, or from two overlapping sources.** Expected: untouchable and immune targets ignore it. Overlapping stuns count once (the union of the intervals) toward the 2.5 s. Pinned by Task 1 (`Gen.Combat.ResilienceHistory`), Task 4 (`Gen.Combat.Untouchable`), Task 5 (`Gen.Combat.Resilience`) and Task 12 W11.
4. **Cancel key edge cases.** Expected:
   - with nothing being cast, nothing happens;
   - during a spell that has already gone off (Backfire window, leap in flight, Living Flame form), nothing happens;
   - on the server after the client's aim has arrived, the cast is not cancelled and the costs stay paid (`CanBeCanceled` is false).

   Pinned by Task 12 W12.
5. **Death while ablaze, untouchable or immune.** Expected: every timed state is removed at death, the resilience history resets, and the respawned mage has a normal LMB (Fireball) and normal feeding. Pinned by Task 5 (`Gen.Combat.Resilience` reset case) and Task 12 W14.
6. **The ability bar agrees with `CheckCost`.** Expected: R and F show "Not enough energy" exactly when `CheckCost` would refuse (same `GenEnergy::CanAfford`), and R's one-segment arc fills at 25 energy. Pinned by Task 10 (`Gen.UI.EnergySlot`) and Task 12 W15.

---

## File map

| File | Responsibility |
|---|---|
| `Source/Gen/AbilitySystem/GenFeeding.h` | Fast feeding interval, explosion radius without feeding |
| `Source/Gen/AbilitySystem/GenEnergy.h` (new) | `CanAfford` |
| `Source/Gen/AbilitySystem/GenResilience.h` (new) | Hard-CC history and immunity duration (pure) |
| `Source/Gen/AbilitySystem/GenHitRules.h` | `Ignored` response for untouchable targets |
| `Source/Gen/GenGameplayTags.h/.cpp` | `State.Untouchable`, `State.CCImmune`, `State.FastFeeding`, `State.FreeResource` |
| `Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h/.cpp` | `EnergyCost`, `CheckCost`, `ApplyCost` |
| `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h/.cpp` | Interval snapshot (fast feeding), free resource |
| `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h/.cpp` | `BaseExplosionRadius` (Pyroblast) |
| `Source/Gen/AbilitySystem/GenAbilitySystemComponent.h/.cpp` | Resilience and immunities in `ApplyHardCC`; `CancelPendingCasts` |
| `Source/Gen/AbilitySystem/GenAttributeSet.cpp` | Damage is ignored while untouchable |
| `Source/Gen/Character/GenCharacterBase.h/.cpp` | `IsUntouchable`; untouchable in `ResolveIncomingHit` and `ApplyKnockback` |
| `Source/Gen/Actors/GenProjectile.cpp` | Projectiles pass through untouchable characters |
| `Source/Gen/Input/GenInputConfig.h` | `CancelAction` |
| `Source/Gen/Player/GenPlayerController.h/.cpp` | Cancel key binding |
| `Source/Gen/Champions/Curffe/CurffeGameplayTags.h/.cpp` | `State.Curffe.Ablaze`, Living Flame, Combustion and Pyroblast tags |
| `Source/Gen/Champions/Curffe/CurffeGA_LivingFlame.h/.cpp` (new) | Living Flame |
| `Source/Gen/Champions/Curffe/CurffeGA_Combustion.h/.cpp` (new) | Combustion |
| `Source/Gen/Tests/GenPowerRulesTests.cpp` (new) | Pure-rule tests |
| `Source/Gen/Tests/GenPowerWorldTests.cpp` (new) | GAS tests (untouchable, resilience, energy cost) |
| `Source/Gen/UI/GenUIRules.h` | `NoEnergy` slot state, `CostSegments` |
| `Source/Gen/UI/GenUIDataAssets.h` | `Cooldown_NoEnergy` token, `NoEnergyBrightness` metric |
| `Source/Gen/UI/GenAbilitySlot.h/.cpp` | Energy-cost arc on R and F, "Not enough energy" state, Locked on any hard CC |
| `Source/Gen/Tests/GenUIRulesTests.cpp` | `Gen.UI.EnergySlot` |
| `Content/Gen/UI/Foundation/DA_UIPalette`, `Content/Gen/UI/Materials/M_UI_AbilityIcon` | `Cooldown_NoEnergy` value, `Brightness` parameter |
| `Content/Gen/Rendering/Masters/M_VFX_GhostDither` (new, hot shared file) | Untouchable body material (masked dither) |
| `Content/Gen/Champions/Curffe/**` | `GA_LivingFlame`, `GA_Combustion`, `GA_Pyroblast`, `BP_Projectile_Pyroblast`, status visuals |
| `Content/Gen/UI/Textures/Icons/Abilities/**` | `T_UI_Ability_Curffe_3`, `T_UI_Ability_Curffe_Ultimate`, `T_UI_Ability_Curffe_Pyroblast` |
| `Content/Gen/Input/**` | `IA_Cancel`, mapping and config |
| `Content/Python/gen_pie_tools.py` | `hard_cc`, `energy` and ability-bar helpers |
| `Content/Python/gen_ui_icons.py` | Three more placeholder icons |

**Build and unit tests:** use the same commands as Plan 2, with `$Root` set to the checkout you run the plan in.

```powershell
$Root = "C:\Users\Samy D\Documents\Unreal Projects\Gen-curffe"
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GenEditor Win64 Development "-Project=$Root\Gen.uproject" -WaitMutex | Select-Object -Last 15
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Root\Gen.uproject" -ExecCmds="Automation RunTests Gen.;Quit" -unattended -nopause -nosplash -nullrhi -NoSound "-abslog=$Root\Saved\Logs\GenTests.log"
Select-String -Path "$Root\Saved\Logs\GenTests.log" -Pattern "Test Completed. Result=|Error:" | Select-Object -Last 60
```

---

### Task 1: Pure rules (fast feeding interval, energy, resilience, untouchable)

**Files:**
- Modify: `Source/Gen/AbilitySystem/GenFeeding.h`, `Source/Gen/AbilitySystem/GenHitRules.h`
- Create: `Source/Gen/AbilitySystem/GenEnergy.h`, `Source/Gen/AbilitySystem/GenResilience.h`
- Modify: `Source/Gen/Tests/GenCombatRulesTests.cpp` (the three `Resolve` calls), `Source/Gen/Character/GenCharacterBase.cpp` (the one `Resolve` call)
- Test: `Source/Gen/Tests/GenPowerRulesTests.cpp`

**Interfaces:**
- Produces:
  - `GenFeeding::FastFeedMultiplier` (0.5: Curffe's 0.3 s becomes `CurffeTuning::FastFeedInterval`, 0.15 s; the test pins the two together)
  - `GenFeeding::GetFeedInterval(float BaseInterval, bool bFastFeeding) -> float`
  - `GenEnergy::CanAfford(float Energy, float Cost) -> bool`
  - `GenResilience::{Window = 5, Threshold = 2.5, ImmunityDuration = 1.5}`
  - `GenResilience::FHardCCHistory::Record(float Now, float Duration) -> float` (the immunity duration to grant now; 0 = none) and `Reset()`
  - `EGenHitResponse::Ignored`
  - `GenHitRules::Resolve(bool bCountering, bool bUntouchable, EGenHitKind) -> EGenHitResponse`. This replaces the two-argument version.

- [ ] **Step 0: Create the branch.** `git switch -c curffe-plan3` (from the tip of `curffe-plan2`). (Done on 2026-10-08 directly on `curffe-plan2`, ahead of Plan 2's Task 5: this task has no `UGenGA_Cast` dependency. Skip it if `Gen.Feeding.FastInterval` already exists.)
  - Needs `CurffeTuning::{FeedInterval, FastFeedInterval, MaxFeedPerSpell}` (the `ui-ability-bar` 3-flame change; `curffe-plan2` carries an identical copy of `CurffeTuning.h` until that change is merged).

- [ ] **Step 1: Write the failing tests** in `Source/Gen/Tests/GenPowerRulesTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenEnergy.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenResilience.h"
#include "Champions/Curffe/CurffeTuning.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFastFeedingTest, "Gen.Feeding.FastInterval",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFastFeedingTest::RunTest(const FString& Parameters)
{
	// GetFeedInterval(BaseInterval, bFastFeeding), avec les valeurs de Curffe (3 seuils, 0.3 s par flamme)
	TestEqual(TEXT("intervalle normal"), GenFeeding::GetFeedInterval(CurffeTuning::FeedInterval, false), CurffeTuning::FeedInterval, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Combustion : 0.15 s par flamme (CurffeTuning::FastFeedInterval)"), GenFeeding::GetFeedInterval(CurffeTuning::FeedInterval, true), CurffeTuning::FastFeedInterval, KINDA_SMALL_NUMBER);
	TestTrue(TEXT("jamais nul"), GenFeeding::GetFeedInterval(0.f, true) > 0.f);

	// Avec l'intervalle rapide, les ticks calés sur le début du nourrissage (GetNextFeedTickDelay,
	// correctif de la dérive du plan 1) tombent à 0.15, 0.30, 0.45 : le 3e seuil (le dernier) à 0.45 s au lieu de 0.9 s
	const float Fast = GenFeeding::GetFeedInterval(CurffeTuning::FeedInterval, true);
	TestEqual(TEXT("3e flamme à 0.45 s"), GenFeeding::GetNextFeedTickDelay(0.f, 2, Fast, 0.3f), 0.15f, 0.0001f);
	TestEqual(TEXT("tick en retard d'une image : pas de dérive"), GenFeeding::GetNextFeedTickDelay(0.f, 1, Fast, 0.167f), 0.133f, 0.0001f);

	// Validation serveur avec l'intervalle rapide : 3 flammes en 0.47 s acceptées
	TestEqual(TEXT("3 en 0.47 s validées"), GenFeeding::ValidateFedCount(3, CurffeTuning::MaxFeedPerSpell, 5.f, 0.47f, Fast), 3);
	TestEqual(TEXT("avec l'intervalle normal, ce serait 2"), GenFeeding::ValidateFedCount(3, CurffeTuning::MaxFeedPerSpell, 5.f, 0.47f, CurffeTuning::FeedInterval), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCanAffordTest, "Gen.Energy.CanAfford",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCanAffordTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("25 pour Flamme vivante"), GenEnergy::CanAfford(25.f, 25.f));
	TestFalse(TEXT("24.99 ne suffit pas"), GenEnergy::CanAfford(24.99f, 25.f));
	TestTrue(TEXT("100 pour Combustion"), GenEnergy::CanAfford(100.f, 100.f));
	TestTrue(TEXT("dérive flottante tolérée"), GenEnergy::CanAfford(99.99999f, 100.f));
	TestFalse(TEXT("99 ne suffit pas"), GenEnergy::CanAfford(99.f, 100.f));
	TestTrue(TEXT("sort gratuit"), GenEnergy::CanAfford(0.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResilienceHistoryTest, "Gen.Combat.ResilienceHistory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResilienceHistoryTest::RunTest(const FString& Parameters)
{
	{
		// 3 étourdissements de 1 s à 0, 1.2 et 2.4 : 3 s cumulées => immunité jusqu'à 1.5 s après la fin du 3e
		GenResilience::FHardCCHistory History;
		TestEqual(TEXT("1er"), History.Record(0.f, 1.f), 0.f);
		TestEqual(TEXT("2e"), History.Record(1.2f, 1.f), 0.f);
		TestEqual(TEXT("3e : immunité (1 s restante + 1.5 s)"), History.Record(2.4f, 1.f), 2.5f, 0.001f);
		TestEqual(TEXT("historique remis à zéro après l'immunité"), History.Record(10.f, 1.f), 0.f);
	}
	{
		// Deux étourdissements qui se chevauchent comptent une fois (union des intervalles)
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 1.f);
		TestEqual(TEXT("chevauchement : 1.5 s seulement"), History.Record(0.5f, 1.f), 0.f);
		TestEqual(TEXT("puis 1 s de plus : 2.5 s atteintes"), History.Record(2.f, 1.f), 2.5f, 0.001f);
	}
	{
		// Fenêtre glissante de 5 s
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 1.f);
		TestEqual(TEXT("le premier est sorti de la fenêtre"), History.Record(6.f, 1.f), 0.f);
		TestEqual(TEXT("2 s dans la fenêtre"), History.Record(7.f, 1.f), 0.f);
	}
	{
		// Un long contrôle seul (ultime) suffit
		GenResilience::FHardCCHistory History;
		TestEqual(TEXT("2.5 s d'un coup"), History.Record(0.f, 2.5f), 4.f, 0.001f);
	}
	{
		// Contrôle à cheval sur le début de la fenêtre : seule la partie dans la fenêtre compte
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 2.f);
		TestEqual(TEXT("1.5 s (0.5 à 2) + 1 s"), History.Record(5.5f, 1.f), 2.5f, 0.001f);
	}
	{
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 2.f);
		History.Reset();
		TestEqual(TEXT("Reset (mort) oublie tout"), History.Record(0.5f, 1.f), 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUntouchableRuleTest, "Gen.Combat.UntouchableRule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUntouchableRuleTest::RunTest(const FString& Parameters)
{
	// Resolve(bCountering, bUntouchable, Kind)
	TestTrue(TEXT("projectile sur un intouchable : ignoré"), GenHitRules::Resolve(false, true, EGenHitKind::Projectile) == EGenHitResponse::Ignored);
	TestTrue(TEXT("zone sur un intouchable : ignorée"), GenHitRules::Resolve(false, true, EGenHitKind::Area) == EGenHitResponse::Ignored);
	TestTrue(TEXT("intouchable prime sur le contre (rien n'est bloqué, pas de récompense)"), GenHitRules::Resolve(true, true, EGenHitKind::Projectile) == EGenHitResponse::Ignored);
	TestTrue(TEXT("sans intouchable : règle du contre"), GenHitRules::Resolve(true, false, EGenHitKind::Melee) == EGenHitResponse::Countered);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Expected: `Cannot open include file: 'AbilitySystem/GenEnergy.h'`.

- [ ] **Step 3: Add the feeding rules** to `GenFeeding.h`, at the end of the namespace:

```cpp
	/**
	 * Nourrissage rapide (State.FastFeeding, ex : Combustion) : intervalle divisé par deux.
	 * Curffe : 0.15 s au lieu de 0.3 s (CurffeTuning::FastFeedInterval, vérifié par Gen.Feeding.FastInterval).
	 */
	inline constexpr float FastFeedMultiplier = 0.5f;

	/** Intervalle de nourrissage, retenu au début du nourrissage (State.FastFeeding). Jamais nul. */
	inline float GetFeedInterval(float BaseInterval, bool bFastFeeding)
	{
		return FMath::Max(BaseInterval * (bFastFeeding ? FastFeedMultiplier : 1.f), 0.01f);
	}
```

- [ ] **Step 4: Create `Source/Gen/AbilitySystem/GenEnergy.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

/** Règles pures de l'énergie (guidelines §4.1). */
namespace GenEnergy
{
	/** L'énergie suffit-elle pour un sort qui coûte Cost ? (tolérance flottante : 100 = 100). */
	inline bool CanAfford(float Energy, float Cost)
	{
		return Cost <= 0.f || Energy + KINDA_SMALL_NUMBER >= Cost;
	}
}
```

- [ ] **Step 5: Create `Source/Gen/AbilitySystem/GenResilience.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * Résilience (guidelines §3.3) : après 2.5 s de contrôle dur sur 5 s, immunité aux contrôles durs
 * pendant 1.5 s. Les longs contrôles restent possibles ; les enchaînements sans fin non.
 * Choix : l'immunité commence dès que le contrôle qui atteint le seuil est appliqué (il s'applique en entier)
 * et dure jusqu'à 1.5 s après sa fin. Deux contrôles simultanés ne comptent qu'une fois (union).
 */
namespace GenResilience
{
	inline constexpr float Window = 5.f;
	inline constexpr float Threshold = 2.5f;
	inline constexpr float ImmunityDuration = 1.5f;

	struct FHardCCHistory
	{
		/** Enregistre un contrôle dur appliqué à Now pendant Duration. Renvoie la durée d'immunité à accorder maintenant (0 = aucune). */
		float Record(float Now, float Duration)
		{
			const float WindowStart = Now - Window;
			Entries.RemoveAll([WindowStart](const FEntry& Entry) { return Entry.Start + Entry.Duration <= WindowStart; });
			Entries.Add({ Now, FMath::Max(Duration, 0.f) });

			TArray<FEntry> Sorted = Entries;
			Sorted.Sort([](const FEntry& A, const FEntry& B) { return A.Start < B.Start; });

			// Union des intervalles, chacun rogné au début de la fenêtre
			float Total = 0.f;
			float SpanStart = 0.f;
			float SpanEnd = -1.f;
			float LatestEnd = Now;
			for (const FEntry& Entry : Sorted)
			{
				const float Start = FMath::Max(Entry.Start, WindowStart);
				const float End = Entry.Start + Entry.Duration;
				LatestEnd = FMath::Max(LatestEnd, End);
				if (End <= Start)
				{
					continue;
				}
				if (Start > SpanEnd)
				{
					Total += FMath::Max(SpanEnd - SpanStart, 0.f);
					SpanStart = Start;
					SpanEnd = End;
				}
				else
				{
					SpanEnd = FMath::Max(SpanEnd, End);
				}
			}
			Total += FMath::Max(SpanEnd - SpanStart, 0.f);

			if (Total + 0.001f >= Threshold)
			{
				Entries.Reset();
				return (LatestEnd - Now) + ImmunityDuration;
			}
			return 0.f;
		}

		void Reset()
		{
			Entries.Reset();
		}

	private:
		struct FEntry
		{
			float Start = 0.f;
			float Duration = 0.f;
		};

		TArray<FEntry> Entries;
	};
}
```

- [ ] **Step 6: Add the untouchable response to `GenHitRules.h`.**
  - In `EGenHitResponse`, after `Countered`:

```cpp
	/** Cible intouchable : le coup passe à travers, rien ne s'applique, aucun contre n'est prévenu. */
	Ignored
```

  - Replace `Resolve` with:

```cpp
	/** Intouchable d'abord (le coup traverse), puis la règle du contre. */
	inline EGenHitResponse Resolve(bool bCountering, bool bUntouchable, EGenHitKind Kind)
	{
		if (bUntouchable)
		{
			return EGenHitResponse::Ignored;
		}
		return bCountering && TriggersCounter(Kind) ? EGenHitResponse::Countered : EGenHitResponse::Hit;
	}
```

  - Update the callers:
    - In `GenCombatRulesTests.cpp`, `Gen.Combat.CounterTrigger`, the three calls become `GenHitRules::Resolve(true, false, EGenHitKind::Projectile)`, `GenHitRules::Resolve(true, false, EGenHitKind::Area)` and `GenHitRules::Resolve(false, false, EGenHitKind::Projectile)`.
    - In `GenCharacterBase.cpp`, `ResolveIncomingHit`, change it temporarily to `GenHitRules::Resolve(bCountering, /*bUntouchable*/ false, Kind)`. Task 4 replaces `false`. (On `curffe-plan2` the review of Plan 2 Tasks 3–4 already replaced it with `IsUntouchable()`, see Task 4.)

- [ ] **Step 7: Build, then run the unit tests.** Expected: `Gen.Feeding.FastInterval`, `Gen.Energy.CanAfford`, `Gen.Combat.ResilienceHistory` and `Gen.Combat.UntouchableRule` pass, and every Plan 1 and Plan 2 test still passes.

- [ ] **Step 8: Commit**

```bash
git add Source/Gen/AbilitySystem/GenFeeding.h Source/Gen/AbilitySystem/GenEnergy.h Source/Gen/AbilitySystem/GenResilience.h Source/Gen/AbilitySystem/GenHitRules.h Source/Gen/Tests/GenPowerRulesTests.cpp Source/Gen/Tests/GenCombatRulesTests.cpp Source/Gen/Character/GenCharacterBase.cpp
git commit -m "Pure rules: fast feeding interval, energy affordability, resilience history, untouchable response"
```

---

### Task 2: Fast feeding and free resource in `UGenGA_Cast`

The Plan 1 feed drift (about one frame per flame from chained timers) is already fixed upstream (commit `8c378e7`): ticks are scheduled from the feed start with `GenFeeding::GetNextFeedTickDelay`. This task makes that schedule, the cast bar and the server validation use the interval **snapshotted at feed start**, so Combustion's 0.15 s feeding stays exact. It also adds the two generic tags Combustion grants.

**Files:**
- Modify: `Source/Gen/GenGameplayTags.h/.cpp`
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h/.cpp`

**Interfaces:**
- Consumes: `GenFeeding::GetFeedInterval` (Task 1); `GenFeeding::GetNextFeedTickDelay` (Plan 1 fixes).
- Produces:
  - Tags `GenGameplayTags::State_FastFeeding` and `State_FreeResource`.
  - `UGenGA_Cast` snapshots the interval at the start of feeding (`ActiveFeedInterval`, private). It uses that interval for the cast bar (`StartFeedCast`), the ticks and server validation.
  - Fed units are not spent if the caster has `State.FreeResource` at release.

- [ ] **Step 1: Add the tags.**
  - In `GenGameplayTags.h`, after `State_CastLocked`:

```cpp
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_FastFeeding);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_FreeResource);
```

  - In `GenGameplayTags.cpp`, after the `State_CastLocked` definition:

```cpp
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_FastFeeding, "State.FastFeeding", "Nourrissage deux fois plus rapide (ex : Combustion)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_FreeResource, "State.FreeResource", "Les unites nourries ne sont pas depensees (ex : flammes illimitees de Combustion)");
```

- [ ] **Step 2: Add the snapshot member.** In `GenGA_Cast.h`, `private:`, after `float FeedStartTime = 0.f;`:

```cpp
	/** Intervalle retenu au début du nourrissage (rapide sous State.FastFeeding), sur chaque machine. Posé par StartFeeding. */
	float ActiveFeedInterval = 0.f;
```

- [ ] **Step 3: Snapshot the interval when feeding starts.** In `GenGA_Cast.cpp`, `StartFeeding`:
  - right after `FeedStartTime = GetWorld()->GetTimeSeconds();`, add:

```cpp
	// Intervalle figé pour tout ce nourrissage (Combustion qui commence ou finit pendant l'appui n'y change rien)
	const UAbilitySystemComponent* FeedASC = GetAbilitySystemComponentFromActorInfo();
	ActiveFeedInterval = GenFeeding::GetFeedInterval(FeedInterval, FeedASC && FeedASC->HasMatchingGameplayTag(GenGameplayTags::State_FastFeeding));
```

  - in the same function, replace `Character->StartFeedCast(GetClass(), FeedSlotsAtPress, FeedInterval, CastTime, CastFX, CastFXSocket);` with:

```cpp
		Character->StartFeedCast(GetClass(), FeedSlotsAtPress, ActiveFeedInterval, CastTime, CastFX, CastFXSocket);
```

- [ ] **Step 4: Schedule ticks on the snapshot.** In `ScheduleFeedTick`, replace `GenFeeding::GetNextFeedTickDelay(FeedStartTime, FedCount, FeedInterval, GetWorld()->GetTimeSeconds())` with:

```cpp
GenFeeding::GetNextFeedTickDelay(FeedStartTime, FedCount, ActiveFeedInterval, GetWorld()->GetTimeSeconds())
```

  `OnFeedTick` does not change: one unit per tick, capped by `FeedSlotsAtPress`.

- [ ] **Step 5: Validate against the snapshot.** In `ResolveFedCount`, replace `ServerFeedElapsed, FeedInterval)` with `ServerFeedElapsed, ActiveFeedInterval)`. The server took its own snapshot at its activation, one uplink latency after the client. The +1-interval tolerance of `ValidateFedCount` absorbs a disagreement only when ablaze starts or ends inside that latency.

- [ ] **Step 6: Free resource at release.** In `ReleaseCast`, replace `SpendResource(Fed);` with:

```cpp
	// Flammes illimitées (State.FreeResource, ex. Combustion) : les unités nourries reviennent au Foyer
	const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC || !OwnerASC->HasMatchingGameplayTag(GenGameplayTags::State_FreeResource))
	{
		SpendResource(Fed);
	}
```

- [ ] **Step 7: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes.

- [ ] **Step 8: Commit**

```bash
git add Source/Gen/GenGameplayTags.h Source/Gen/GenGameplayTags.cpp Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h Source/Gen/AbilitySystem/Abilities/GenGA_Cast.cpp
git commit -m "Fast feeding on a feed-start snapshot of the interval; free resource tag"
```

---

### Task 3: Energy costs paid on release (Review: required)

**Files:**
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h/.cpp`
- Test: `Source/Gen/Tests/GenPowerWorldTests.cpp`

**Interfaces:**
- Consumes: `GenEnergy::CanAfford` (Task 1), `UGenGE_Gain::SetMagnitudes`, `GenTestWorld` (Plan 2).
- Produces:
  - `UGenGameplayAbility::EnergyCost` (`float`, EditDefaultsOnly, BlueprintReadOnly, public; Python `energy_cost`). The ability bar reads it from Task 10 on (cost arc on R and F, "Not enough energy" state).
  - `CheckCost` refuses below the cost (so activation is refused too).
  - `ApplyCost` spends the cost through `UGenGE_Gain` inside `CommitAbility`, which `UGenGA_Cast` calls at release.

- [ ] **Step 1: Write the failing test** in `Source/Gen/Tests/GenPowerWorldTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"
#include "Tests/GenTestWorld.h"

using namespace GenTestWorld;

namespace GenPowerWorldTests
{
	void SetEnergy(UAbilitySystemComponent* ASC, float Energy)
	{
		ASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), Energy);
	}
}

using namespace GenPowerWorldTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenEnergyCostTest, "Gen.Energy.CostCheckAndApply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenEnergyCostTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	// Un sort concret quelconque, son coût réglé le temps du test (CDO restauré à la fin)
	UGenGA_Projectile* Ability = GetMutableDefault<UGenGA_Projectile>();
	const float OldCost = Ability->EnergyCost;
	Ability->EnergyCost = 25.f;

	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(UGenGA_Projectile::StaticClass(), 1));
	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();

	SetEnergy(ASC, 24.f);
	TestFalse(TEXT("24 d'énergie : refusé"), Ability->CheckCost(Handle, ActorInfo));
	SetEnergy(ASC, 25.f);
	TestTrue(TEXT("25 d'énergie : accepté"), Ability->CheckCost(Handle, ActorInfo));

	Ability->ApplyCost(Handle, ActorInfo, FGameplayAbilityActivationInfo());
	TestEqual(TEXT("payé au lancer : 25 -> 0"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 0.f);

	Ability->EnergyCost = OldCost;
	return true;
}

#endif
```

  If `ApplyCost` on the CDO asserts because the ability is instanced, delete the `ApplyCost` lines and their expectation. Note it in the commit; Task 12 W1 then covers the spend.

- [ ] **Step 2: Build to verify it fails.** Expected: `'EnergyCost': is not a member of 'UGenGA_Projectile'`.

- [ ] **Step 3: Add the property and overrides** in `GenGameplayAbility.h`.
  - After `CooldownTags`:

```cpp
	/**
	 * Énergie dépensée au lancer (R : 25, F : 100), lue aussi par la barre de sorts. 0 = gratuit.
	 * Vérifiée à l'activation, payée par CommitAbility : au lancer pour UGenGA_Cast (une incantation
	 * interrompue ne coûte rien, guidelines §3.1).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Cost", meta = (ClampMin = "0.0"))
	float EnergyCost = 0.f;
```

  - After the `CanActivateAbility` override:

```cpp
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
```

- [ ] **Step 4: Implement them** in `GenGameplayAbility.cpp`. Add `#include "AbilitySystem/Effects/GenGE_Gain.h"`, `#include "AbilitySystem/GenAttributeSet.h"` and `#include "AbilitySystem/GenEnergy.h"`, then:

```cpp
bool UGenGameplayAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}
	if (EnergyCost <= 0.f)
	{
		return true;
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const float Energy = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()) : 0.f;
	return GenEnergy::CanAfford(Energy, EnergyCost);
}

void UGenGameplayAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	if (EnergyCost <= 0.f)
	{
		return;
	}

	const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo, UGenGE_Gain::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
	if (Spec.IsValid())
	{
		UGenGE_Gain::SetMagnitudes(*Spec.Data, -EnergyCost, 0.f);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}
}
```

- [ ] **Step 5: Build, then run the unit tests.** Expected: `Gen.Energy.CostCheckAndApply` passes, and all earlier tests still pass.

- [ ] **Step 6: Commit**

```bash
git add Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.cpp Source/Gen/Tests/GenPowerWorldTests.cpp
git commit -m "Energy cost on abilities: checked at activation, paid at commit (release)"
```

---

### Task 4: Untouchable state (Review: required)

**Files:**
- Modify: `Source/Gen/GenGameplayTags.h/.cpp`
- Modify: `Source/Gen/Character/GenCharacterBase.h/.cpp`
- Modify: `Source/Gen/Actors/GenProjectile.cpp`
- Modify: `Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp` (`ApplyHardCC`)
- Modify: `Source/Gen/AbilitySystem/GenAttributeSet.cpp`
- Test: `Source/Gen/Tests/GenPowerWorldTests.cpp`

**Interfaces:**
- Consumes: `GenHitRules::Resolve(bCountering, bUntouchable, Kind)` (Task 1).
- Produces:
  - Tag `GenGameplayTags::State_Untouchable`.
  - `AGenCharacterBase::IsUntouchable() const -> bool` (BlueprintPure).
  - While untouchable:
    - `ResolveIncomingHit` returns `Ignored` and sends no counter event;
    - projectiles pass through and splash skips the target;
    - areas skip the target;
    - `ApplyHardCC` and `ApplyKnockback` do nothing;
    - incoming damage is ignored.

- [ ] **Step 1: Write the failing test.** Add this to `GenPowerWorldTests.cpp` before `#endif`. Add `#include "AbilitySystem/Effects/GenGE_Damage.h"`, `#include "AbilitySystem/Effects/GenGE_TimedState.h"` and `#include "Character/GenTrainingDummy.h"`.

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUntouchableTest, "Gen.Combat.Untouchable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUntouchableTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Target = TestWorld.SpawnDummy();
	AGenTrainingDummy* Attacker = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Target ? Target->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC de la cible"), ASC) || !TestNotNull(TEXT("attaquant"), Attacker))
	{
		return false;
	}

	int32 CounterEvents = 0;
	ASC->GenericGameplayEventCallbacks.FindOrAdd(GenGameplayTags::Event_Counter_Blocked).AddLambda([&](const FGameplayEventData*) { ++CounterEvents; });

	// Forme de feu : 0.5 s intouchable (et contre actif en même temps, pour vérifier la priorité)
	const FGameplayEffectSpecHandle Form = ASC->MakeOutgoingSpec(UGenGE_TimedState::StaticClass(), 1.f, ASC->MakeEffectContext());
	FGameplayTagContainer FormTags;
	FormTags.AddTag(GenGameplayTags::State_Untouchable);
	UGenGE_TimedState::SetDuration(*Form.Data, 0.5f, FormTags);
	ASC->ApplyGameplayEffectSpecToSelf(*Form.Data);
	ASC->AddLooseGameplayTag(GenGameplayTags::State_Countering);

	TestTrue(TEXT("intouchable"), Target->IsUntouchable());
	TestTrue(TEXT("projectile ignoré"), Target->ResolveIncomingHit(Attacker, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Ignored);
	TestTrue(TEXT("zone ignorée"), Target->ResolveIncomingHit(Attacker, EGenHitKind::Area, nullptr) == EGenHitResponse::Ignored);
	TestEqual(TEXT("aucun contre prévenu"), CounterEvents, 0);
	TestFalse(TEXT("étourdissement ignoré"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, Attacker).IsValid());

	const FGameplayEffectSpecHandle Hit = ASC->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, ASC->MakeEffectContext());
	Hit.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Damage, 50.f);
	ASC->ApplyGameplayEffectSpecToSelf(*Hit.Data);
	TestEqual(TEXT("dégâts ignorés"), Get(ASC, UGenAttributeSet::GetHealthAttribute()), 200.f);

	TestWorld.Advance(0.6f);
	ASC->RemoveLooseGameplayTag(GenGameplayTags::State_Countering);
	TestFalse(TEXT("fin de la forme"), Target->IsUntouchable());
	ASC->ApplyGameplayEffectSpecToSelf(*Hit.Data);
	TestEqual(TEXT("de nouveau touchable"), Get(ASC, UGenAttributeSet::GetHealthAttribute()), 150.f);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'State_Untouchable': is not a member of 'GenGameplayTags'`. (Since the review of Plan 2 Tasks 3–4 the tag already exists, so the test compiles and fails on the hard-CC, knockback and damage checks instead.)

> **Already done on `curffe-plan2`** (review of Plan 2 Tasks 3–4, commit "projectiles pass through untouchable targets"): the `State_Untouchable` tag (Step 3), `AGenCharacterBase::IsUntouchable()` and the `ResolveIncomingHit` call with `IsUntouchable()` (Step 4, not the `ApplyKnockback` guard), and the projectile pass-through (Step 5, done differently: `OnSphereOverlap` resolves the direct hit before `Explode` and returns on `Ignored`, so the target is passed through with no explosion, no splash and no salvo claim; `Explode` receives the resolved response). `Gen.Net.ProjectileCounter.Untouchable_PassesThrough_NoExplosion` pins it. Check each step against the source and skip what is there; Steps 6 and 7 and the `ApplyKnockback` guard remain.

- [ ] **Step 3: Add the tag.**
  - In `GenGameplayTags.h`, after `State_FreeResource`: `UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Untouchable);`
  - In `GenGameplayTags.cpp`:

```cpp
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Untouchable, "State.Untouchable", "Intouchable : projectiles traversent, degats et controles ignores (<= 0.5 s, guidelines 3.5)");
```

- [ ] **Step 4: Character.**
  - In `GenCharacterBase.h`, after `IsDead()`:

```cpp
	/** Intouchable (State.Untouchable) : projectiles traversent, dégâts, contrôles et repoussements ignorés. */
	UFUNCTION(BlueprintPure, Category = "Gen|Health")
	bool IsUntouchable() const;
```

  - In `GenCharacterBase.cpp`, add:

```cpp
bool AGenCharacterBase::IsUntouchable() const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable);
}
```

  - In `ResolveIncomingHit`, replace the `Resolve` call with:

```cpp
	const EGenHitResponse Response = GenHitRules::Resolve(bCountering, IsUntouchable(), Kind);
```

  - In `ApplyKnockback`, extend the first guard to `if (!HasAuthority() || bIsDead || Distance <= 0.f || IsUntouchable())`.

- [ ] **Step 5: Projectiles pass through.** In `GenProjectile.cpp`, `OnSphereOverlap`, right after the allies/dead check inside `if (const AGenCharacterBase* HitCharacter = ...)`:

```cpp
		// Intouchable : le projectile le traverse et continue
		if (HitCharacter->IsUntouchable())
		{
			return;
		}
```

  Splash and areas need no change: `ResolveIncomingHit` returns `Ignored` and they already skip anything that is not `Hit`.

  The **direct hit** in `AGenProjectile::Explode` must not treat `Ignored` as a hit either (a target that became untouchable inside an initial overlap, for example). Check that its direct-hit branch reads as Plan 2 Task 4 wrote it, and replace it with this if it doesn't:

```cpp
		const EGenHitResponse Response = DirectTarget->ResolveIncomingHit(GetInstigator(), EGenHitKind::Projectile, this);
		if (Response == EGenHitResponse::Hit)
		{
			Targets.Add(DirectTarget);
		}
		else if (Response == EGenHitResponse::Countered)
		{
			bCountered = true;
		}
```

  A direct `Ignored` therefore deals no damage and no knockback, and the attacker gains nothing from it.

- [ ] **Step 6: No hard CC while untouchable.** In `GenAbilitySystemComponent.cpp`, `ApplyHardCC`, extend the first guard:

```cpp
	if (!IsOwnerActorAuthoritative() || !StateTag.IsValid() || Duration <= 0.f || HasMatchingGameplayTag(GenGameplayTags::State_Untouchable))
	{
		return FActiveGameplayEffectHandle();
	}
```

- [ ] **Step 7: Damage is ignored as a safety net** (for future damage sources such as melee, DoT or scripts). In `GenAttributeSet.cpp`, add `#include "GenGameplayTags.h"`. In `PostGameplayEffectExecute`, `IncomingDamage` branch, replace `if (LocalDamage > 0.f && !bOutOfHealth)` with:

```cpp
		const UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent();
		const bool bUntouchable = OwnerASC && OwnerASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable);
		if (LocalDamage > 0.f && !bOutOfHealth && !bUntouchable)
```

- [ ] **Step 8: Build, then run the unit tests.** Expected: `Gen.Combat.Untouchable` passes, and all earlier tests still pass.

- [ ] **Step 9: Commit**

```bash
git add Source/Gen/GenGameplayTags.h Source/Gen/GenGameplayTags.cpp Source/Gen/Character Source/Gen/Actors/GenProjectile.cpp Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp Source/Gen/AbilitySystem/GenAttributeSet.cpp Source/Gen/Tests/GenPowerWorldTests.cpp
git commit -m "Untouchable state: projectiles pass through, hits, CC, knockback and damage ignored"
```

---

### Task 5: Resilience (immunity to hard CC) (Review: required)

**Files:**
- Modify: `Source/Gen/GenGameplayTags.h/.cpp`
- Modify: `Source/Gen/AbilitySystem/GenAbilitySystemComponent.h/.cpp`
- Test: `Source/Gen/Tests/GenPowerWorldTests.cpp`

**Interfaces:**
- Consumes: `GenResilience::FHardCCHistory` (Task 1), `UGenGE_TimedState` (Plan 2).
- Produces:
  - Tag `GenGameplayTags::State_CCImmune`.
  - `ApplyHardCC` ignores hard CC under `State.CCImmune` and records each applied CC. Once the threshold is reached, it applies `State.CCImmune` for the duration that `Record` returns.
  - `RemoveTimedStates` (at death) also resets the history.

- [ ] **Step 1: Write the failing test.** Add this to `GenPowerWorldTests.cpp` before `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResilienceTest, "Gen.Combat.Resilience",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResilienceTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Target = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Target ? Target->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC de la cible"), ASC))
	{
		return false;
	}

	// Trois étourdissements de 1 s à 0, 1.2 et 2.4 s
	TestTrue(TEXT("1er étourdissement"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestWorld.Advance(1.2f);
	TestTrue(TEXT("2e"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestWorld.Advance(1.2f);
	TestTrue(TEXT("3e (appliqué en entier)"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestEqual(TEXT("immunisé"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 1);

	TestFalse(TEXT("4e ignoré pendant l'immunité"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestWorld.Advance(2.6f);
	TestEqual(TEXT("immunité finie (1 s + 1.5 s)"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 0);
	TestTrue(TEXT("de nouveau contrôlable"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());

	// Mort : immunité et historique effacés
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr);
	ASC->RemoveTimedStates();
	TestTrue(TEXT("historique remis à zéro (2 s de plus ne suffisent pas)"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 2.f, nullptr).IsValid());
	TestEqual(TEXT("pas d'immunité"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 0);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'State_CCImmune': is not a member of 'GenGameplayTags'`.

- [ ] **Step 3: Add the tag.**
  - In `GenGameplayTags.h`, after `State_Untouchable`: `UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_CCImmune);`
  - In `GenGameplayTags.cpp`:

```cpp
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_CCImmune, "State.CCImmune", "Resilience : immunise aux controles durs (guidelines 3.3)");
```

- [ ] **Step 4: Add the history member.** In `GenAbilitySystemComponent.h`, add `#include "AbilitySystem/GenResilience.h"` and, in `protected:`:

```cpp
	/** Serveur : contrôles durs reçus récemment (résilience). */
	GenResilience::FHardCCHistory HardCCHistory;
```

- [ ] **Step 5: Replace `ApplyHardCC`, and reset the history in `RemoveTimedStates`.** In `GenAbilitySystemComponent.cpp`:

```cpp
FActiveGameplayEffectHandle UGenAbilitySystemComponent::ApplyHardCC(FGameplayTag StateTag, float Duration, AActor* Source)
{
	if (!IsOwnerActorAuthoritative() || !StateTag.IsValid() || Duration <= 0.f
		|| HasMatchingGameplayTag(GenGameplayTags::State_Untouchable) || HasMatchingGameplayTag(GenGameplayTags::State_CCImmune))
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddInstigator(Source, Source);

	const FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UGenGE_TimedMoveSpeed::StaticClass(), 1.f, Context);
	if (!Spec.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	// Étourdi ou neutralisé : ne bouge plus. Silence et peur laissent bouger (inchangé depuis le plan 2).
	const bool bImmobile = StateTag.MatchesTagExact(GenGameplayTags::State_Stunned) || StateTag.MatchesTagExact(GenGameplayTags::State_Incapacitated);
	UGenGE_TimedMoveSpeed::SetMagnitudes(*Spec.Data, Duration, bImmobile ? 0.f : 1.f, FGameplayTagContainer(StateTag));
	const FActiveGameplayEffectHandle Handle = ApplyGameplayEffectSpecToSelf(*Spec.Data);

	// Résilience : 2.5 s de contrôle dur sur 5 s => immunité jusqu'à 1.5 s après la fin de celui-ci
	const float Immunity = HardCCHistory.Record(GetWorld()->GetTimeSeconds(), Duration);
	if (Immunity > 0.f)
	{
		const FGameplayEffectSpecHandle ImmunitySpec = MakeOutgoingSpec(UGenGE_TimedState::StaticClass(), 1.f, MakeEffectContext());
		if (ImmunitySpec.IsValid())
		{
			UGenGE_TimedState::SetDuration(*ImmunitySpec.Data, Immunity, FGameplayTagContainer(GenGameplayTags::State_CCImmune));
			ApplyGameplayEffectSpecToSelf(*ImmunitySpec.Data);
		}
	}

	return Handle;
}
```

  In `RemoveTimedStates`, after `RemoveActiveEffects(Query);`, add:

```cpp
	HardCCHistory.Reset();
```

- [ ] **Step 6: Build, then run the unit tests.** Expected: `Gen.Combat.Resilience` passes, and all earlier tests still pass.

- [ ] **Step 7: Commit**

```bash
git add Source/Gen/GenGameplayTags.h Source/Gen/GenGameplayTags.cpp Source/Gen/AbilitySystem/GenAbilitySystemComponent.h Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp Source/Gen/Tests/GenPowerWorldTests.cpp
git commit -m "Resilience: immunity to hard CC after 2.5 s within 5 s"
```

---

### Task 6: Cancel key

**Files:**
- Modify: `Source/Gen/Input/GenInputConfig.h`
- Modify: `Source/Gen/Player/GenPlayerController.h/.cpp`
- Modify: `Source/Gen/AbilitySystem/GenAbilitySystemComponent.h/.cpp`

**Interfaces:**
- Consumes: `UGenGA_Cast::IsCastPending()` (Plan 2).
- Produces:
  - `UGenInputConfig::CancelAction` (`const UInputAction*`; Python `cancel_action`).
  - `AGenPlayerController::CancelCast()`.
  - `UGenAbilitySystemComponent::CancelPendingCasts() -> int32`: cancels every `UGenGA_Cast` still feeding or casting and replicates the cancel. A spell that has already gone off is never cancelled. On the server, a cast whose aim has arrived is not cancellable (`CanBeCanceled` is false), so its costs stay paid.

- [ ] **Step 1: Input config.** In `GenInputConfig.h`, after `MoveAction`:

```cpp
	/** Touche d'annulation : annule l'incantation en cours, nourrissage compris (guidelines §3.1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> CancelAction;
```

- [ ] **Step 2: ASC.**
  - In `GenAbilitySystemComponent.h`, after `RemoveTimedStates`:

```cpp
	/** Touche d'annulation : annule les sorts en incantation (pas un sort déjà parti). Renvoie le nombre annulé. */
	int32 CancelPendingCasts();
```

  - In `GenAbilitySystemComponent.cpp`, add `#include "AbilitySystem/Abilities/GenGA_Cast.h"` and append:

```cpp
int32 UGenAbilitySystemComponent::CancelPendingCasts()
{
	TArray<UGenGA_Cast*, TInlineAllocator<4>> Pending;
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		UGenGA_Cast* CastAbility = Spec.IsActive() ? Cast<UGenGA_Cast>(Spec.GetPrimaryInstance()) : nullptr;
		if (CastAbility && CastAbility->IsCastPending())
		{
			Pending.Add(CastAbility);
		}
	}

	// Annulation prédite, répliquée au serveur (qui la refuse si la visée est déjà arrivée : CanBeCanceled)
	for (UGenGA_Cast* CastAbility : Pending)
	{
		CastAbility->CancelAbility(CastAbility->GetCurrentAbilitySpecHandle(), CastAbility->GetCurrentActorInfo(), CastAbility->GetCurrentActivationInfo(), true);
	}
	return Pending.Num();
}
```

- [ ] **Step 3: Player controller.**
  - In `GenPlayerController.h`, `protected:`, after `AbilityInputReleased`, add `void CancelCast();`.
  - In `GenPlayerController.cpp`, `SetupInputComponent`, after the `MoveAction` binding:

```cpp
	if (InputConfig->CancelAction)
	{
		EnhancedInput->BindAction(InputConfig->CancelAction, ETriggerEvent::Started, this, &ThisClass::CancelCast);
	}
```

    and add:

```cpp
void AGenPlayerController::CancelCast()
{
	if (UGenAbilitySystemComponent* ASC = GetGenAbilitySystemComponent())
	{
		ASC->CancelPendingCasts();
	}
}
```

- [ ] **Step 4: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes. The behaviour is checked in Task 12 W12.

- [ ] **Step 5: Commit**

```bash
git add Source/Gen/Input/GenInputConfig.h Source/Gen/Player Source/Gen/AbilitySystem/GenAbilitySystemComponent.h Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp
git commit -m "Cancel key: cancels the pending cast, never a spell that already went off"
```

---

### Task 7: An explosion without feeding (Pyroblast support)

**Files:**
- Modify: `Source/Gen/AbilitySystem/GenFeeding.h`, `Source/Gen/Tests/GenPowerRulesTests.cpp`
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h/.cpp`

**Interfaces:**
- Produces:
  - `GenFeeding::GetShotExplosionRadius(int32 Fed, int32 ExplosionMinFeed, float FedRadius, float BaseRadius) -> float`
  - `UGenGA_Projectile::BaseExplosionRadius` (Python `base_explosion_radius`)

- [ ] **Step 1: Write the failing test.** Add this to `GenPowerRulesTests.cpp` before `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenShotExplosionTest, "Gen.Feeding.ShotExplosionRadius",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenShotExplosionTest::RunTest(const FString& Parameters)
{
	// GetShotExplosionRadius(Fed, ExplosionMinFeed, FedRadius, BaseRadius)
	TestEqual(TEXT("Pyroblast : explose toujours (1.2 m)"), GenFeeding::GetShotExplosionRadius(0, 0, 150.f, 120.f), 120.f);
	TestEqual(TEXT("grosse boule de feu, 3 flammes"), GenFeeding::GetShotExplosionRadius(3, 3, 150.f, 0.f), 150.f);
	TestEqual(TEXT("grosse boule de feu, 2 flammes"), GenFeeding::GetShotExplosionRadius(2, 3, 150.f, 0.f), 0.f);
	TestEqual(TEXT("boule de feu"), GenFeeding::GetShotExplosionRadius(0, 0, 150.f, 0.f), 0.f);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'GetShotExplosionRadius': is not a member of 'GenFeeding'`.

- [ ] **Step 3: Add the rule** to `GenFeeding.h`, after `ReachesThreshold`:

```cpp
	/** Rayon d'explosion d'un tir : FedRadius dès ExplosionMinFeed unités, sinon BaseRadius (0 = pas d'explosion). */
	inline float GetShotExplosionRadius(int32 Fed, int32 ExplosionMinFeed, float FedRadius, float BaseRadius)
	{
		return ReachesThreshold(Fed, ExplosionMinFeed) ? FedRadius : FMath::Max(BaseRadius, 0.f);
	}
```

- [ ] **Step 4: Use it in the projectile ability.**
  - In `GenGA_Projectile.h`, after `SpawnForwardOffset`:

```cpp
	/** Explosion de zone même sans nourrissage (ex : Pyroblast, 120 cm). 0 = seulement via ExplosionMinFeed. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile", meta = (ClampMin = "0.0", Units = "cm"))
	float BaseExplosionRadius = 0.f;
```

  - In `GenGA_Projectile.cpp`, `SpawnProjectile`, replace the `ShotParams.ExplosionRadius = ...` line with:

```cpp
	ShotParams.ExplosionRadius = GenFeeding::GetShotExplosionRadius(Fed, ExplosionMinFeed, ExplosionRadius, BaseExplosionRadius);
```

- [ ] **Step 5: Build, then run the unit tests.** Expected: `Gen.Feeding.ShotExplosionRadius` passes, and every test passes.

- [ ] **Step 6: Commit**

```bash
git add Source/Gen/AbilitySystem/GenFeeding.h Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.cpp Source/Gen/Tests/GenPowerRulesTests.cpp
git commit -m "Projectiles can explode without feeding (base explosion radius)"
```

---

### Task 8: Living Flame (R)

**Files:**
- Modify: `Source/Gen/Champions/Curffe/CurffeGameplayTags.h/.cpp`
- Create: `Source/Gen/Champions/Curffe/CurffeGA_LivingFlame.h`, `Source/Gen/Champions/Curffe/CurffeGA_LivingFlame.cpp`

**Interfaces:**
- Consumes:
  - `UGenGA_Cast::{OnCastLaunched, FinishAbility, SpawnGroundArea}` and `FGenAreaParams` (Plan 2);
  - `UGenGE_TimedState` and `UGenGE_TimedMoveSpeed` (Plan 2);
  - `State_Untouchable` (Task 4) and `State_CastLocked` (Plan 2);
  - `UCurffeGE_HearthFill` (Plan 1).
- Produces:
  - Tags `CurffeGameplayTags::State_Ablaze`, `Ability_LivingFlame`, `Cooldown_Ability_LivingFlame`, `Ability_Combustion` and `Ability_Pyroblast`.
  - `UCurffeGA_LivingFlame : UGenGA_Cast`, with properties `FormDuration`, `BurstAreaClass`, `BurstRadius`, `BurstDamage`, `BurstKnockback`, `RefillEffect`, `HasteMultiplier` and `HasteDuration`.
  - The sequence:
    1. Launch (after the 0.1 s cast; the energy and cooldown are paid by the commit) applies the form, a timed state of `FormDuration` with `State.Untouchable` and `State.CastLocked`. It is predicted. The server also calls `UGenAbilitySystemComponent::NoteCastLock(FormDuration)`, so it refuses a remote client's casts only during the first `FormDuration − CastTimeTolerance` of its own copy of the form (Plan 2 Task 3).
    2. When the form ends, the server spawns the 2.5 m ring (A) with an 8 damage and 3 m knockback, refills the Hearth, and grants +30 % speed for 2 s.

- [ ] **Step 1: Add the Curffe tags.**
  - In `CurffeGameplayTags.h`:

```cpp
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_LivingFlame);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Combustion);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Pyroblast);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_LivingFlame);

	/** Embrasé (Combustion) : boule de feu remplacée par Pyroblast. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ablaze);
```

  - In `CurffeGameplayTags.cpp`:

```cpp
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_LivingFlame, "Ability.LivingFlame", "Sort : flamme vivante (R)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Combustion, "Ability.Combustion", "Sort : combustion (ultime)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Pyroblast, "Ability.Pyroblast", "Sort : pyroblast (clic gauche pendant la combustion)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_LivingFlame, "Cooldown.Ability.LivingFlame", "Recharge de la flamme vivante");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Ablaze, "State.Curffe.Ablaze", "Embrase (combustion) : pyroblasts, flammes illimitees, nourrissage rapide");
```

- [ ] **Step 2: Create `Source/Gen/Champions/Curffe/CurffeGA_LivingFlame.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "CurffeGA_LivingFlame.generated.h"

class AGenGroundArea;
class UGameplayEffect;

/**
 * Flamme vivante (R, 25 d'énergie) : Curffe devient flamme vivante, intouchable 0.5 s et sans sort
 * (guidelines §3.5). À la fin : anneau de 2.5 m (zone, 8 dégâts, repousse 3 m), Foyer rempli à 5,
 * puis +30 % de vitesse pendant 2 s, pendant lesquelles il peut lancer ses sorts.
 */
UCLASS()
class GEN_API UCurffeGA_LivingFlame : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UCurffeGA_LivingFlame();

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION()
	void OnFormEnded();

	/** Durée de la forme de feu (intouchable, sans sort). Guidelines §3.5 : 0.5 s au plus. */
	UPROPERTY(EditDefaultsOnly, Category = "Living Flame", meta = (ClampMin = "0.05", ClampMax = "0.5", Units = "s"))
	float FormDuration = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst")
	TSubclassOf<AGenGroundArea> BurstAreaClass;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst", meta = (ClampMin = "1.0", Units = "cm"))
	float BurstRadius = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst")
	float BurstDamage = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst", meta = (ClampMin = "0.0", Units = "cm"))
	float BurstKnockback = 300.f;

	/** Remplit le Foyer (UCurffeGE_HearthFill). */
	UPROPERTY(EditDefaultsOnly, Category = "Living Flame")
	TSubclassOf<UGameplayEffect> RefillEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Haste", meta = (ClampMin = "1.0"))
	float HasteMultiplier = 1.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Haste", meta = (ClampMin = "0.0", Units = "s"))
	float HasteDuration = 2.f;

private:
	FActiveGameplayEffectHandle FormEffectHandle;
};
```

- [ ] **Step 3: Create `Source/Gen/Champions/Curffe/CurffeGA_LivingFlame.cpp`**

```cpp
#include "Champions/Curffe/CurffeGA_LivingFlame.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "Actors/GenGroundArea.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeLivingFlame, Log, All);

UCurffeGA_LivingFlame::UCurffeGA_LivingFlame()
{
	CastTime = 0.1f;
	bTurnToAim = false;
	RefillEffect = UCurffeGE_HearthFill::StaticClass();
}

void UCurffeGA_LivingFlame::OnCastLaunched(const FGenCastRelease& Release)
{
	// Forme de feu prédite chez le client : intouchable et sans sort, vu par tous (forme d'état)
	FGameplayEffectSpecHandle FormSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedState::StaticClass(), GetAbilityLevel());
	if (FormSpec.IsValid())
	{
		FGameplayTagContainer FormTags;
		FormTags.AddTag(GenGameplayTags::State_Untouchable);
		FormTags.AddTag(GenGameplayTags::State_CastLocked);
		UGenGE_TimedState::SetDuration(*FormSpec.Data, FormDuration, FormTags);
		FormEffectHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, FormSpec);
	}

	// Serveur : la forme du serveur commence ~½ RTT après celle du client ; son verrou ne refuse les sorts du client
	// distant qu'au début (sans effet chez le client, NoteCastLock ne fait rien hors autorité)
	if (UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		GenASC->NoteCastLock(FormDuration);
	}

	UAbilityTask_WaitDelay* FormTask = UAbilityTask_WaitDelay::WaitDelay(this, FormDuration);
	FormTask->OnFinish.AddDynamic(this, &ThisClass::OnFormEnded);
	FormTask->ReadyForActivation();

	UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("%s : forme de feu (%.2fs)"), *GetName(), FormDuration);
}

void UCurffeGA_LivingFlame::OnFormEnded()
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority())
	{
		// Anneau (zone : traverse les contres)
		FGenAreaParams Params;
		Params.Radius = BurstRadius;
		Params.KnockbackDistance = BurstKnockback;
		SpawnGroundArea(BurstAreaClass, Avatar->GetActorLocation(), Params, UGenGE_Damage::StaticClass(), BurstDamage, 0.f);

		// Foyer plein
		if (RefillEffect)
		{
			ApplyGameplayEffectToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, RefillEffect->GetDefaultObject<UGameplayEffect>(), GetAbilityLevel());
		}

		// Hâte : appliquée par le serveur (hors fenêtre de prédiction), répliquée au propriétaire
		FGameplayEffectSpecHandle HasteSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedMoveSpeed::StaticClass(), GetAbilityLevel());
		if (HasteSpec.IsValid())
		{
			UGenGE_TimedMoveSpeed::SetMagnitudes(*HasteSpec.Data, HasteDuration, HasteMultiplier, FGameplayTagContainer());
			ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, HasteSpec);
		}

		UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("%s : anneau, Foyer rempli, hâte x%.2f %.1fs"), *GetName(), HasteMultiplier, HasteDuration);
	}

	// La forme a expiré d'elle-même (même durée) : le sort se termine, il peut de nouveau lancer
	FormEffectHandle.Invalidate();
	FinishAbility();
}

void UCurffeGA_LivingFlame::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Fin anticipée (mort) : la forme s'arrête avec le sort
	if (FormEffectHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(FormEffectHandle);
		FormEffectHandle.Invalidate();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
```

- [ ] **Step 4: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes.

- [ ] **Step 5: Commit**

```bash
git add Source/Gen/Champions/Curffe/CurffeGameplayTags.h Source/Gen/Champions/Curffe/CurffeGameplayTags.cpp Source/Gen/Champions/Curffe/CurffeGA_LivingFlame.h Source/Gen/Champions/Curffe/CurffeGA_LivingFlame.cpp
git commit -m "Living Flame: untouchable fire form, burst ring, Hearth refill, haste"
```

---

### Task 9: Combustion (F)

**Files:**
- Create: `Source/Gen/Champions/Curffe/CurffeGA_Combustion.h`, `Source/Gen/Champions/Curffe/CurffeGA_Combustion.cpp`

**Interfaces:**
- Consumes:
  - `UGenGA_Cast` (cast interrupted by any hard CC: stun, silence, fear, incapacitate, through `GetHardCCTags()`; the commit at release pays `EnergyCost`);
  - `SpawnGroundArea`;
  - `UGenGE_TimedState`;
  - `CurffeGameplayTags::State_Ablaze` (Task 8);
  - `GenGameplayTags::{State_FastFeeding, State_FreeResource}` (Task 2);
  - `UCurffeGE_HearthFill`.
- Produces:
  - `UCurffeGA_Combustion : UGenGA_Cast`, with properties `NovaAreaClass`, `NovaRadius`, `NovaDamage`, `NovaKnockback`, `AblazeDuration` and `RefillEffect`.
  - At launch:
    - the Hearth refills;
    - "ablaze" is applied, a timed state of `AblazeDuration` with `State.Curffe.Ablaze`, `State.FastFeeding` and `State.FreeResource`;
    - the server spawns the 3 m nova (20 damage, 3 m knockback).
  - The Pyroblast swap is data only (Task 11): `GA_Pyroblast` requires `State.Curffe.Ablaze` and `GA_Fireball` is blocked by it.
  - "Telegraphs never drop below 0.5 s" needs no new code. Fast feeding only shortens the feed phase, and delayed areas already clamp their delay to `MinTelegraph` (0.6 s, the CharacterGuidelines §3.1 floor for delayed areas, which also covers the spec's 0.5 s) through `GenAreaRules::GetImpactDelay` (Plan 2). Flame Pillar keeps 0.4 s + 0.8 s.

- [ ] **Step 1: Create `Source/Gen/Champions/Curffe/CurffeGA_Combustion.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "CurffeGA_Combustion.generated.h"

class AGenGroundArea;
class UGameplayEffect;

/**
 * Combustion (F, ultime, 100 d'énergie payés à la fin de l'incantation de 0.5 s, interrompue par un
 * contrôle dur). Au lancer : nova de 3 m (zone, 20 dégâts, repousse 3 m), puis embrasé 5 s :
 * Pyroblast au clic gauche (GA_Pyroblast exige State.Curffe.Ablaze), flammes illimitées
 * (State.FreeResource : le Foyer revient plein après chaque sort), nourrissage rapide (State.FastFeeding).
 * Pas d'immunité aux contrôles.
 */
UCLASS()
class GEN_API UCurffeGA_Combustion : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UCurffeGA_Combustion();

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova")
	TSubclassOf<AGenGroundArea> NovaAreaClass;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova", meta = (ClampMin = "1.0", Units = "cm"))
	float NovaRadius = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova")
	float NovaDamage = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova", meta = (ClampMin = "0.0", Units = "cm"))
	float NovaKnockback = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion", meta = (ClampMin = "0.0", Units = "s"))
	float AblazeDuration = 5.f;

	/** Remplit le Foyer au lancer (UCurffeGE_HearthFill). */
	UPROPERTY(EditDefaultsOnly, Category = "Combustion")
	TSubclassOf<UGameplayEffect> RefillEffect;
};
```

- [ ] **Step 2: Create `Source/Gen/Champions/Curffe/CurffeGA_Combustion.cpp`**

```cpp
#include "Champions/Curffe/CurffeGA_Combustion.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "Actors/GenGroundArea.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeCombustion, Log, All);

UCurffeGA_Combustion::UCurffeGA_Combustion()
{
	CastTime = 0.5f;
	bTurnToAim = false;
	RefillEffect = UCurffeGE_HearthFill::StaticClass();
}

void UCurffeGA_Combustion::OnCastLaunched(const FGenCastRelease& Release)
{
	const int32 Level = GetAbilityLevel();

	// Foyer plein, puis embrasé (prédit chez le client : Pyroblast et nourrissage rapide sans attendre)
	if (RefillEffect)
	{
		ApplyGameplayEffectToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, RefillEffect->GetDefaultObject<UGameplayEffect>(), Level);
	}

	FGameplayEffectSpecHandle AblazeSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedState::StaticClass(), Level);
	if (AblazeSpec.IsValid())
	{
		FGameplayTagContainer AblazeTags;
		AblazeTags.AddTag(CurffeGameplayTags::State_Ablaze);
		AblazeTags.AddTag(GenGameplayTags::State_FastFeeding);
		AblazeTags.AddTag(GenGameplayTags::State_FreeResource);
		UGenGE_TimedState::SetDuration(*AblazeSpec.Data, AblazeDuration, AblazeTags);
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, AblazeSpec);
	}

	// Éruption : nova (zone, traverse les contres)
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority())
	{
		FGenAreaParams Params;
		Params.Radius = NovaRadius;
		Params.KnockbackDistance = NovaKnockback;
		SpawnGroundArea(NovaAreaClass, Avatar->GetActorLocation(), Params, UGenGE_Damage::StaticClass(), NovaDamage, 0.f);
	}

	UE_LOG(LogCurffeCombustion, Verbose, TEXT("%s : éruption, embrasé %.1fs"), *GetName(), AblazeDuration);
	FinishAbility();
}
```

- [ ] **Step 3: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes.

- [ ] **Step 4: Commit**

```bash
git add Source/Gen/Champions/Curffe/CurffeGA_Combustion.h Source/Gen/Champions/Curffe/CurffeGA_Combustion.cpp
git commit -m "Combustion: nova, ablaze state (Pyroblast, unlimited flames, fast feeding)"
```

---

### Task 10: Ability bar: energy-cost arc and "Not enough energy" (UI_Guidelines §4.1)

The bar from `ui-ability-bar` draws the cost arc on the ultimate slot only and has no energy state. UI_Guidelines §4.1 asks for:
- the **energy-cost arc** on the two energy slots and only there: R shows **one** segment (its 25 cost), F shows all four (100). Funded segments use `energy.charging`, unfunded ones are a hollow outline in `text.secondary`, and only a fully funded **ultimate** arc uses `energy.full`;
- the **Not enough energy** state on R and F: a `cooldown.noEnergy` wash (`#2E4A78`, α 0.45) and the icon at **55 %** brightness, plus the hollow arc segments as the non-colour cue;
- the **Locked** state on every CC that blocks casting (stun, silence, fear, incapacitate), not only on `State.Stunned`.

Before writing anything, read UI_Guidelines §2.5, §4.1, §8.3, §8.4 and the §9 checklist. No widget hard-codes a colour, font or size: everything comes from `DA_UIPalette` and `DA_UIMetrics`.

**Files:**
- Modify: `Source/Gen/UI/GenUIRules.h`, `Source/Gen/UI/GenUIDataAssets.h`, `Source/Gen/UI/GenAbilitySlot.h`, `Source/Gen/UI/GenAbilitySlot.cpp`
- Test: `Source/Gen/Tests/GenUIRulesTests.cpp`
- Assets (Steps 8–10, editor): `Content/Gen/UI/Foundation/DA_UIPalette` (token value), `Content/Gen/UI/Materials/M_UI_AbilityIcon` (`Brightness` parameter). `WBP_AbilitySlot` needs no change: `ArcImage` is already in the slot widget used by every slot (checked in Step 10).

**Interfaces:**
- Consumes: `UGenGameplayAbility::EnergyCost` (Task 3), `GenEnergy::CanAfford` (Task 1), `GenGameplayTags::GetHardCCTags()` (Plan 2), `GenUIRules::FundedSegments`.
- Produces:
  - `EGenAbilitySlotState::NoEnergy` (appended after `Locked`).
  - `GenUIRules::ResolveSlotState(bool bHasAbility, bool bLocked, float CooldownRemaining, bool bCanAfford = true)`. Priority: Empty > Locked > Cooldown > NoEnergy > Ready. The existing three-argument calls keep compiling.
  - `GenUIRules::CostSegments(float EnergyCost, float MaxEnergy, int32 Segments) -> int32` (R = 1, F = 4, a free spell = 0).
  - `UGenUIPalette::Cooldown_NoEnergy` (Python `cooldown_no_energy`) and `UGenUIMetrics::NoEnergyBrightness` (0.55).
  - The slot sets `Brightness` on the icon MID (`M_UI_AbilityIcon`) and `SegmentSlots` on the arc MID.

- [ ] **Step 1: Write the failing test.** Append this to `GenUIRulesTests.cpp`, before `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUIEnergySlotTest, "Gen.UI.EnergySlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUIEnergySlotTest::RunTest(const FString& Parameters)
{
	// CostSegments(EnergyCost, MaxEnergy, Segments) : un segment par tranche de Max/Segments (§4.1)
	TestEqual(TEXT("R : 25 -> 1 segment"), GenUIRules::CostSegments(25.f, 100.f, 4), 1);
	TestEqual(TEXT("F : 100 -> 4 segments"), GenUIRules::CostSegments(100.f, 100.f, 4), 4);
	TestEqual(TEXT("sort gratuit : pas d'arc"), GenUIRules::CostSegments(0.f, 100.f, 4), 0);
	TestEqual(TEXT("coût entre deux segments : arrondi au-dessus"), GenUIRules::CostSegments(30.f, 100.f, 4), 2);
	TestEqual(TEXT("énergie max inconnue : pas d'arc"), GenUIRules::CostSegments(25.f, 0.f, 4), 0);

	// ResolveSlotState(bHasAbility, bLocked, CooldownRemaining, bCanAfford)
	TestEqual(TEXT("pas assez d'énergie"), GenUIRules::ResolveSlotState(true, false, 0.f, false), EGenAbilitySlotState::NoEnergy);
	TestEqual(TEXT("la recharge prime sur l'énergie"), GenUIRules::ResolveSlotState(true, false, 2.f, false), EGenAbilitySlotState::Cooldown);
	TestEqual(TEXT("bloqué prime sur l'énergie"), GenUIRules::ResolveSlotState(true, true, 0.f, false), EGenAbilitySlotState::Locked);
	TestEqual(TEXT("assez d'énergie : prêt"), GenUIRules::ResolveSlotState(true, false, 0.f, true), EGenAbilitySlotState::Ready);
	TestEqual(TEXT("sans le 4e argument : prêt"), GenUIRules::ResolveSlotState(true, false, 0.f), EGenAbilitySlotState::Ready);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'CostSegments': is not a member of 'GenUIRules'` and `'NoEnergy': is not a member of 'EGenAbilitySlotState'`.

- [ ] **Step 3: Rules.** In `GenUIRules.h`:
  - add `NoEnergy` at the end of `EGenAbilitySlotState`:

```cpp
	Locked,
	/** Pas assez d'énergie pour le coût du sort (R, F) : voile cooldown.noEnergy, icône à 55 % (§4.1). */
	NoEnergy
```

  - replace `ResolveSlotState` and its comment with:

```cpp
	/** Priorité : vide > bloqué (contrôle dur) > recharge > pas assez d'énergie > prêt. */
	inline EGenAbilitySlotState ResolveSlotState(bool bHasAbility, bool bLocked, float CooldownRemaining, bool bCanAfford = true)
	{
		if (!bHasAbility)
		{
			return EGenAbilitySlotState::Empty;
		}
		if (bLocked)
		{
			return EGenAbilitySlotState::Locked;
		}
		if (CooldownRemaining > 0.f)
		{
			return EGenAbilitySlotState::Cooldown;
		}
		return bCanAfford ? EGenAbilitySlotState::Ready : EGenAbilitySlotState::NoEnergy;
	}
```

  - after `FundedSegments`, add:

```cpp
	/** Segments d'arc d'un coût (§4.1, un segment par tranche de Max/Segments) : R (25) = 1, F (100) = 4, sort gratuit = 0. */
	inline int32 CostSegments(float EnergyCost, float MaxEnergy, int32 Segments)
	{
		if (EnergyCost <= 0.f || MaxEnergy <= 0.f || Segments <= 0)
		{
			return 0;
		}
		return FMath::Clamp(FMath::CeilToInt32(EnergyCost / (MaxEnergy / Segments) - KINDA_SMALL_NUMBER), 1, Segments);
	}
```

- [ ] **Step 4: Tokens.** In `GenUIDataAssets.h`:
  - in `UGenUIPalette`, after `Cooldown_Locked`:

```cpp
	/** cooldown.noEnergy #2E4A78 α 0.45 (§2.5) : voile « pas assez d'énergie ». Valeur posée dans DA_UIPalette via HexToLinear. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_NoEnergy = FLinearColor::Black;
```

  - in `UGenUIMetrics`, after `CooldownDesaturation`:

```cpp
	/** Luminosité de l'icône quand l'énergie manque (§4.1 : 55 %). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float NoEnergyBrightness = 0.55f;
```

- [ ] **Step 5: Slot header.** In `GenAbilitySlot.h`:
  - replace `void OnStunTagChanged(const FGameplayTag Tag, int32 NewCount);` with:

```cpp
	/** Un contrôle dur (étourdi, silence, peur, neutralisé) apparaît ou disparaît : état Locked (§4.1). */
	void OnStunTagChanged(const FGameplayTag Tag, int32 NewCount);
	/** L'énergie couvre-t-elle le coût du sort (même règle que CheckCost) ? Vrai pour un sort gratuit. */
	bool CanAffordAbility() const;
```

  - replace the `UpdateUltimateArc` declaration and its comment with:

```cpp
	/**
	 * Arc de coût (§4.1) : sur l'ultime, et sur tout sort qui coûte de l'énergie (R : 1 segment, F : 4).
	 * Gère aussi l'impulsion de l'ultime ; renvoie vrai si l'ultime est pleine ET lançable (anneau à α 1.0).
	 */
	bool UpdateCostArc();
```

- [ ] **Step 6: Slot implementation.** In `GenAbilitySlot.cpp`, add `#include "AbilitySystem/GenEnergy.h"`, then:
  - in `Bind`, replace the stun block:

```cpp
	// Étourdi => bloqué (§4.1 Locked)
	FDelegateHandle StunHandle = ASC->RegisterGameplayTagEvent(GenGameplayTags::State_Stunned, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::OnStunTagChanged);
	TagHandles.Emplace(GenGameplayTags::State_Stunned, StunHandle);
	bLocked = ASC->HasMatchingGameplayTag(GenGameplayTags::State_Stunned);
```

    with:

```cpp
	// Contrôle dur qui empêche de lancer (étourdi, silence, peur, neutralisé) => bloqué (§4.1 Locked)
	for (const FGameplayTag& HardCCTag : GenGameplayTags::GetHardCCTags())
	{
		FDelegateHandle LockHandle = ASC->RegisterGameplayTagEvent(HardCCTag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::OnStunTagChanged);
		TagHandles.Emplace(HardCCTag, LockHandle);
	}
	bLocked = ASC->HasAnyMatchingGameplayTags(GenGameplayTags::GetHardCCTags());
```

  - in `Bind`, replace the `if (bIsUltimate) { ... }` energy block with:

```cpp
	// Énergie : arc de coût et état « pas assez d'énergie » (R, F), impulsion de l'ultime. Le coût du sort n'est connu
	// qu'après ResolveAbility (réessais) : chaque emplacement écoute, un événement par changement d'énergie (§8.4).
	EnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);
	MaxEnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetMaxEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);

	if (bIsUltimate)
	{
		// Déjà pleine au moment du Bind (respawn, rebind) : pas d'impulsion
		const int32 Segments = GetUIMetrics()->UltimateSegments;
		bUltimateWasReadyFull = Segments > 0 && GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), Segments) == Segments;
	}
```

  - in `RefreshVisuals`, replace `State = GenUIRules::ResolveSlotState(AbilityCDO.IsValid(), bLocked, Remaining);` with:

```cpp
	State = GenUIRules::ResolveSlotState(AbilityCDO.IsValid(), bLocked, Remaining, CanAffordAbility());
```

  - in `RefreshVisuals`, replace `const bool bUltimateReady = bIsUltimate && UpdateUltimateArc();` with:

```cpp
	const bool bUltimateReady = UpdateCostArc();
```

  - in `RefreshVisuals`, replace the icon block:

```cpp
	if (IconMID)
	{
		IconMID->SetScalarParameterValue(TEXT("DimAmount"), State == EGenAbilitySlotState::Cooldown ? Metrics->CooldownDesaturation : 0.f);
	}
```

    with:

```cpp
	if (IconMID)
	{
		IconMID->SetScalarParameterValue(TEXT("DimAmount"), State == EGenAbilitySlotState::Cooldown ? Metrics->CooldownDesaturation : 0.f);
		// Pas assez d'énergie : icône à 55 % de luminosité (§4.1)
		IconMID->SetScalarParameterValue(TEXT("Brightness"), State == EGenAbilitySlotState::NoEnergy ? Metrics->NoEnergyBrightness : 1.f);
	}
```

  - in `RefreshVisuals`, replace `const float Progress = (State == EGenAbilitySlotState::Cooldown && CooldownDuration > 0.f) ? Remaining / CooldownDuration : 0.f;` with:

```cpp
		// Pas assez d'énergie : voile cooldown.noEnergy sur tout le disque (balayage plein), sans chiffre
		const bool bNoEnergy = State == EGenAbilitySlotState::NoEnergy;
		const float Progress = bNoEnergy ? 1.f : ((State == EGenAbilitySlotState::Cooldown && CooldownDuration > 0.f) ? Remaining / CooldownDuration : 0.f);
```

  - in `RefreshVisuals`, replace `SweepMID->SetVectorParameterValue(TEXT("OverlayColour"), State == EGenAbilitySlotState::Locked ? Palette->Cooldown_Locked : Palette->Cooldown_Overlay);` with:

```cpp
		SweepMID->SetVectorParameterValue(TEXT("OverlayColour"), State == EGenAbilitySlotState::Locked ? Palette->Cooldown_Locked
			: (bNoEnergy ? Palette->Cooldown_NoEnergy : Palette->Cooldown_Overlay));
```

  - replace `OnStunTagChanged` with:

```cpp
void UGenAbilitySlot::OnStunTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	// Plusieurs tags bloquent : on relit l'ensemble plutôt que le compte de celui qui change
	bLocked = ASC.IsValid() && ASC->HasAnyMatchingGameplayTags(GenGameplayTags::GetHardCCTags());
	RefreshVisuals();
}

bool UGenAbilitySlot::CanAffordAbility() const
{
	const float Cost = AbilityCDO.IsValid() ? AbilityCDO->EnergyCost : 0.f;
	return !ASC.IsValid() || GenEnergy::CanAfford(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), Cost);
}
```

  - replace the whole `UpdateUltimateArc` function with:

```cpp
bool UGenAbilitySlot::UpdateCostArc()
{
	const UGenUIMetrics* Metrics = GetUIMetrics();
	const UGenUIPalette* Palette = GetUIPalette();

	// Toujours sur l'ultime ; ailleurs seulement si le sort coûte de l'énergie (R : 1 segment). Un segment garde
	// la taille d'un segment de l'ultime (SegmentSlots).
	const float MaxEnergy = ASC.IsValid() ? ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()) : 0.f;
	const float Cost = AbilityCDO.IsValid() ? AbilityCDO->EnergyCost : 0.f;
	const int32 CostSegments = GenUIRules::CostSegments(Cost, MaxEnergy, Metrics->UltimateSegments);
	const int32 Segments = CostSegments > 0 ? CostSegments : (bIsUltimate ? Metrics->UltimateSegments : 0);

	if (ArcImage)
	{
		ArcImage->SetVisibility(Segments > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!ASC.IsValid() || Segments == 0)
	{
		return false;
	}

	const int32 Funded = FMath::Min(GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), MaxEnergy, Metrics->UltimateSegments), Segments);
	const bool bFull = Funded == Segments;
	// energy.full et son contour : seulement l'arc complet de l'ultime (§4.1)
	const bool bUltimateFull = bIsUltimate && bFull;

	if (ArcMID)
	{
		ArcMID->SetScalarParameterValue(TEXT("SegmentSlots"), Metrics->UltimateSegments);
		ArcMID->SetScalarParameterValue(TEXT("Segments"), Segments);
		ArcMID->SetScalarParameterValue(TEXT("Funded"), Funded);
		ArcMID->SetScalarParameterValue(TEXT("FullOutline"), bUltimateFull ? 1.f : 0.f);
		ArcMID->SetVectorParameterValue(TEXT("Colour"), bUltimateFull ? Palette->Energy_Full : Palette->Energy_Charging);
		// Segments non financés : contour creux text.secondary ; contour de l'arc plein : text.primary (§4.1)
		ArcMID->SetVectorParameterValue(TEXT("HollowColour"), Palette->Text_Secondary);
		ArcMID->SetVectorParameterValue(TEXT("OutlineColour"), Palette->Text_Primary);
	}

	if (!bIsUltimate)
	{
		return false;
	}

	// Ultime prête : une seule impulsion de 300 ms via le RimFlash du balayage (indépendante de l'arc), jamais de boucle (§4.1).
	// Seulement quand elle est lançable : pleine pendant un contrôle dur ou une recharge => impulsion quand elle le redevient.
	// Tant que le sort n'est pas résolu (Empty), on garde l'état précédent.
	if (State != EGenAbilitySlotState::Empty)
	{
		const bool bReadyFull = bFull && State == EGenAbilitySlotState::Ready;
		if (bReadyFull && !bUltimateWasReadyFull)
		{
			StartFlash(Metrics->UltimatePulseDuration);
		}
		bUltimateWasReadyFull = bReadyFull;
	}

	// État courant, sans le verrou de l'impulsion : une ultime vide, non lançable ou sans assez d'énergie n'a jamais l'anneau à α 1.0
	return bFull && State == EGenAbilitySlotState::Ready;
}
```

  - in `ApplyLayout`, leave the `ArcImage` visibility line as it is: it shows the ultimate's arc before the ability resolves, and `UpdateCostArc` sets the final visibility on every refresh.

- [ ] **Step 7: Build, then run the unit tests.** Expected: `Gen.UI.EnergySlot` passes, and every `Gen.UI.*` test and all earlier tests still pass (`Gen.UI.SlotState` keeps its three-argument calls).

  Commit the C++ part:

```bash
git add Source/Gen/UI/GenUIRules.h Source/Gen/UI/GenUIDataAssets.h Source/Gen/UI/GenAbilitySlot.h Source/Gen/UI/GenAbilitySlot.cpp Source/Gen/Tests/GenUIRulesTests.cpp
git commit -m "Ability bar: energy-cost arc on R and F, Not enough energy state, Locked on any hard CC"
```

- [ ] **Step 8: Lock the two UI assets** (editor open with the new binaries; runs in batch C).

```bash
git fetch origin
git diff --stat HEAD...origin/main -- Content/Gen/UI
git log --oneline HEAD..ui-ability-bar -- Content/Gen/UI
mine=$(git lfs locks --verify | awk '$1 == "O" { print $2 }')
for f in Content/Gen/UI/Foundation/DA_UIPalette.uasset Content/Gen/UI/Materials/M_UI_AbilityIcon.uasset; do
  if printf '%s\n' "$mine" | grep -qxF "$f"; then echo "déjà à moi : $f"; continue; fi
  git lfs lock "$f" || { echo "STOP : $f est verrouillé par quelqu'un d'autre"; break; }
done
```

  If `git diff` or `git log` lists one of them, or a lock fails, stop and ask the user (the bar is still being worked on in `ui-ability-bar`).

- [ ] **Step 9: The token and the icon brightness** (`execute_python_code`, `auto_save: false`).

```python
import unreal
EAL = unreal.EditorAssetLibrary
MNS = unreal.MaterialNodeService
MEL = unreal.MaterialEditingLibrary

# Jeton cooldown.noEnergy (§2.5) : toujours depuis le hex, jamais collé
PAL = "/Game/Gen/UI/Foundation/DA_UIPalette"
pal = unreal.load_asset(PAL)
print("avant :", pal.get_editor_property("cooldown_no_energy"))
pal.set_editor_property("cooldown_no_energy", unreal.GenUILibrary.hex_to_linear("#2E4A78", 0.45))
print("palette", EAL.save_asset(PAL, only_if_is_dirty=False), pal.get_editor_property("cooldown_no_energy"))

# Paramètre Brightness sur l'icône : sortie finale x Brightness (1 par défaut, 0.55 sans énergie)
MAT = "/Game/Gen/UI/Materials/M_UI_AbilityIcon"
mat = unreal.load_asset(MAT)
if "Brightness" in [str(n) for n in MEL.get_scalar_parameter_names(mat)]:
    print("Brightness déjà présent")
else:
    print(MNS.export_material_graph_summary(MAT))
    outs = {o.property_name: o.connected_expression_id for o in MNS.get_output_connections(MAT) if o.is_connected}
    print("sorties :", outs)
    src = outs["EmissiveColor"]
    mul = MNS.batch_create_expressions(MAT, ["Multiply"], [-150], [0])[0]
    b = MNS.create_parameter(MAT, "Scalar", "Brightness", "Icon", "1", -400, 250)
    print("connexions :", MNS.batch_connect_expressions(MAT, [src, b.id], ["", ""], [mul.id, mul.id], ["A", "B"]), "/ 2")
    print("emissive", MNS.connect_expression_to_output(MAT, mul.id, "", "EmissiveColor"))
print("compile", unreal.MaterialService.compile_material(MAT), MNS.get_material_diagnostics(MAT))
print("save", EAL.save_asset(MAT, only_if_is_dirty=False))
```

  - Expected: `palette True` with about `(0.027, 0.068, 0.188, 0.45)`; `connexions : 2 / 2`, `emissive True`, `compile True` with no error, `save True`.
  - If the summary shows that `EmissiveColor` comes from a named output of the Custom node (not output 0), pass that name instead of `""` for `src`. If the material's output property is listed under another name (the UI domain's Final Color), use the name `get_output_connections` prints.
  - The opacity (the circle mask) is untouched, so the 55 % applies to the colour only.

- [ ] **Step 10: Check in the editor** (Standalone, 1 player, Curffe; `gen_pie_tools` helpers from Plan 2 work in Standalone through the server world).
  - Confirm that `WBP_AbilitySlot`, the widget every slot uses, has `ArcImage` (material `M_UI_SegmentArc`): look for it in `unreal.WidgetService.list_components("/Game/Gen/UI/HUD/WBP_AbilitySlot")`. If it is missing, stop and report it: the arc then needs a WBP change on `ui-ability-bar`.
  - With 20 energy: R and F show the blue-grey wash and a dimmed icon; R has one hollow segment under it, F four hollow segments. Capture the bar (`capture_image source=game`).
  - With 25 energy: R is bright with one funded `energy.charging` segment; F still shows the wash, with one funded and three hollow segments.
  - With 100 energy: R and F are ready; F's arc is complete in `energy.full` with the ring at α 1.0 and one 300 ms pulse.
  - Stun yourself (`hard_cc` from Task 12, or a dummy's pillar): every slot shows the lock (Locked beats NoEnergy).
  - If R's single segment isn't the size of one ultimate segment, centred under the slot, report how `M_UI_SegmentArc` draws `Segments` < `SegmentSlots`; don't change the material in this task.
  - **UI_Guidelines §9 checklist:** go through it for this change and list each item in the report (tokens only, no hard-coded colour or size; the NoEnergy state has a non-colour cue, the brightness drop and the hollow arc; the wash has no motion; one event per energy change, no tick).

  Commit the assets:

```bash
git add Content/Gen/UI/Foundation/DA_UIPalette.uasset Content/Gen/UI/Materials/M_UI_AbilityIcon.uasset
git commit -m "UI palette: cooldown.noEnergy token; ability icon Brightness parameter"
```

---

### Task 11: Editor assets: Living Flame, Combustion, Pyroblast, cancel key, status visuals, BP_Curffe

The editor must be open with the new binaries. Load the VibeUE skills first: `VibeUE_blueprints`, `VibeUE_materials`, `VibeUE_gas`, `VibeUE_gameplay_tags`, `VibeUE_enhanced_input`. Before touching visuals, read the Art Bible §3.0, §3.9, §7.2 and §7.6 (the current version) and UI_Guidelines §2.11.

**Files (assets):**
- Create:
  - `Content/Gen/Champions/Curffe/Abilities/GA_LivingFlame`
  - `Content/Gen/Champions/Curffe/Abilities/GA_Combustion`
  - `Content/Gen/Champions/Curffe/Abilities/GA_Pyroblast`
  - `Content/Gen/Champions/Curffe/Projectiles/BP_Projectile_Pyroblast`
  - `Content/Gen/Champions/Curffe/Areas/BP_Area_Nova`
  - `Content/Gen/Input/IA_Cancel`
  - `Content/Gen/Rendering/Masters/M_VFX_GhostDither` (hot shared file, announced in Step 1)
  - `Content/Gen/UI/Textures/Icons/Abilities/T_UI_Ability_Curffe_3`, `T_UI_Ability_Curffe_Ultimate`, `T_UI_Ability_Curffe_Pyroblast`
- Modify:
  - `GA_Fireball` (blocked while ablaze) and `GA_GreatFireball` (must stay castable);
  - `BP_Curffe` and `BP_Champion` (status visuals, startup abilities);
  - `IMC_Arena` and `DA_InputConfig` (cancel key).

- [ ] **Step 1: Announce the new master, then lock the assets.**
  - **Tell the user** before Step 6: "I'm about to create `Content/Gen/Rendering/Masters/M_VFX_GhostDither` (Art Bible hot shared file) on `curffe-plan3`. Is anyone else creating it?" Check `git ls-tree -r --name-only origin/main -- Content/Gen/Rendering` and `origin/docs/art-bible` first, and wait for the go-ahead.
  - Lock the existing assets this task modifies. `git lfs locks --verify` marks your own locks with `O`; skip those (Plan 2 may already hold `BP_Curffe` and `BP_Champion`):

```bash
git fetch origin
git diff --stat HEAD...origin/main -- Content
git lfs locks --verify
mine=$(git lfs locks --verify | awk '$1 == "O" { print $2 }')
for f in Content/Gen/Champions/Curffe/Abilities/GA_Fireball.uasset Content/Gen/Champions/Curffe/Abilities/GA_GreatFireball.uasset Content/Gen/Champions/Curffe/BP_Curffe.uasset Content/Gen/Characters/BP_Champion.uasset Content/Gen/Input/IMC_Arena.uasset Content/Gen/Input/DA_InputConfig.uasset; do
  if printf '%s\n' "$mine" | grep -qxF "$f"; then echo "déjà à moi : $f"; continue; fi
  git lfs lock "$f" || { echo "STOP : $f est verrouillé par quelqu'un d'autre"; break; }
done
```

  - If a lock fails because someone else holds it, stop and tell the user. Never use `--force`.
  - If `git diff` lists any of these files changed on `main`, stop and ask the user which order to merge in.

- [ ] **Step 2: Cancel key.** The key is `X`. The spec does not name one: X is free on AZERTY and close to the left hand. It becomes rebindable once a key settings screen exists.

```python
import unreal
IS = unreal.InputService
EAL = unreal.EditorAssetLibrary
if not IS.input_action_exists("/Game/Gen/Input/IA_Cancel"):
    print(IS.create_action("IA_Cancel", "/Game/Gen/Input", "Boolean"))
if not IS.key_mapping_exists("/Game/Gen/Input/IMC_Arena", "/Game/Gen/Input/IA_Cancel"):
    print("mapping", IS.add_key_mapping("/Game/Gen/Input/IMC_Arena", "/Game/Gen/Input/IA_Cancel", "X"))
cfg = unreal.load_asset("/Game/Gen/Input/DA_InputConfig")
cfg.set_editor_property("cancel_action", unreal.load_asset("/Game/Gen/Input/IA_Cancel"))
for p in ["/Game/Gen/Input/IA_Cancel", "/Game/Gen/Input/IMC_Arena", "/Game/Gen/Input/DA_InputConfig"]:
    print(p, EAL.save_asset(p, only_if_is_dirty=False))
print("relu :", cfg.get_editor_property("cancel_action"))
```

  Also check that `IA_Ability_3` maps to `R` with `InputTag.Ability.3`, and that `IA_Ability_Ultimate` maps to `F` with `InputTag.Ability.Ultimate`, as in Plan 2 Task 10 Step 11. Add any missing mapping.

- [ ] **Step 3: Pyroblast projectile and nova area.**

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
def cdo(bp):
    return unreal.get_default_object(BEL.generated_class(bp))

PYRO = "/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_Pyroblast"
if not EAL.does_asset_exist(PYRO):
    EAL.duplicate_asset("/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_GreatFireball", PYRO)
pyro = unreal.load_asset(PYRO)
# Visuel de la grosse boule de feu (plus gros), portée et vitesse de la boule de feu (spec : "bigger projectile") :
# lues sur BP_Projectile_Fireball, jamais recopiées à la main
fireball = cdo(unreal.load_asset("/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_Fireball"))
speed, max_range = fireball.get_editor_property("speed"), fireball.get_editor_property("max_range")
print("boule de feu : vitesse", speed, "portée", max_range)
cdo(pyro).set_editor_property("speed", speed)
cdo(pyro).set_editor_property("max_range", max_range)
BEL.compile_blueprint(pyro)
print(PYRO, EAL.save_asset(PYRO, only_if_is_dirty=False), cdo(pyro).get_editor_property("speed"), cdo(pyro).get_editor_property("max_range"))

NOVA = "/Game/Gen/Champions/Curffe/Areas/BP_Area_Nova"
if not EAL.does_asset_exist(NOVA):
    EAL.duplicate_asset("/Game/Gen/Champions/Curffe/Areas/BP_Area_FlamePillar", NOVA)
nova = unreal.load_asset(NOVA)
BEL.compile_blueprint(nova)
print(NOVA, EAL.save_asset(NOVA, only_if_is_dirty=False))
```

  - Expected: two `True` lines; the Pyroblast's speed and range equal the printed Fireball values.
  - `BP_Area_Nova` keeps `NS_ST_GreatFireballImpact`. With no delay it shows no telegraph, only the impact. The ultimate's "unmistakable" VFX is a later art pass; see Open points.

- [ ] **Step 4: Fireball blocked while ablaze; Great Fireball unaffected; Pyroblast.**
  - `GA_GreatFireball` inherits from `GA_Fireball`, so record its tags first and set them back afterwards.
  - Pyroblast is a direct child of the C++ class, so it does not inherit the block.

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
GTS = unreal.GameplayTagService
AB = "/Game/Gen/Champions/Curffe/Abilities/"
def bpc(name):
    bp = unreal.load_asset(AB + name)
    return bp, unreal.get_default_object(BEL.generated_class(bp))

BGTL = unreal.BlueprintGameplayTagLibrary
def tag_names(container):
    return [str(BGTL.get_tag_name(t)) for t in BGTL.break_gameplay_tag_container(container)]

fb_bp, fb = bpc("GA_Fireball")
gf_bp, gf = bpc("GA_GreatFireball")
gf_blocked = gf.get_editor_property("activation_blocked_tags")
fb_names = tag_names(fb.get_editor_property("activation_blocked_tags"))
print("GA_Fireball bloqué par :", fb_names, "| GA_GreatFireball :", tag_names(gf_blocked))
fb.set_editor_property("activation_blocked_tags", GTS.request_tag_container(fb_names + ["State.Curffe.Ablaze"]))
BEL.compile_blueprint(fb_bp)
# Compiler le parent régénère la classe de l'enfant : son CDO d'avant est périmé, on le relit
gf_bp, gf = bpc("GA_GreatFireball")
gf.set_editor_property("activation_blocked_tags", gf_blocked)   # l'enfant garde ses tags d'avant
BEL.compile_blueprint(gf_bp)

def make_bp(path, parent):
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else BEL.create_blueprint_asset_with_parent(path, parent)
py_bp = make_bp(AB + "GA_Pyroblast", unreal.GenGA_Projectile.static_class())
py = unreal.get_default_object(BEL.generated_class(py_bp))
for k, v in {
    "input_tag": GTS.request_tag("InputTag.Ability.Primary"), "display_name": unreal.Text("Pyroblast"),
    "activation_policy": unreal.GenAbilityActivationPolicy.WHILE_INPUT_ACTIVE,
    "activation_required_tags": GTS.request_tag_container(["State.Curffe.Ablaze"]),
    "ability_tags": GTS.request_tag_container(["Ability.Pyroblast"]),
    "cast_time": 0.35, "cast_move_speed_multiplier": fb.get_editor_property("cast_move_speed_multiplier"),
    "cast_fx": unreal.load_asset("/Game/Gen/VFX/Stylized/NS_ST_GreatFireball_Cast"),
    "charge_montage": fb.get_editor_property("charge_montage"), "cast_montage": fb.get_editor_property("cast_montage"),
    "projectile_class": BEL.generated_class(unreal.load_asset("/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_Pyroblast")),
    "damage": unreal.ScalableFloat(value=13.0), "base_explosion_radius": 120.0,
    "energy_on_hit": 2.0, "resource_on_hit": 1.0,
    "cooldown_duration": unreal.ScalableFloat(value=0.0),
}.items():
    py.set_editor_property(k, v)
BEL.compile_blueprint(py_bp)

for name in ["GA_Fireball", "GA_GreatFireball", "GA_Pyroblast"]:
    print(name, EAL.save_asset(AB + name, only_if_is_dirty=False))
_, fb = bpc("GA_Fireball"); _, gf = bpc("GA_GreatFireball")
print("relu GA_Fireball :", fb.get_editor_property("activation_blocked_tags"))
print("relu GA_GreatFireball :", gf.get_editor_property("activation_blocked_tags"))
```

  - Expected: three `True` lines.
  - Read-back: `GA_Fireball`'s blocked tags include `State.Curffe.Ablaze`; `GA_GreatFireball`'s **do not**. They still contain `State.Dead` and `State.Stunned`.
  - If `break_gameplay_tag_container` returns a tuple in this Python binding, take its first element.

- [ ] **Step 5: Living Flame and Combustion.**

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
GTS = unreal.GameplayTagService
AB = "/Game/Gen/Champions/Curffe/Abilities/"
AREAS = "/Game/Gen/Champions/Curffe/Areas/"
def make_bp(path, parent):
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else BEL.create_blueprint_asset_with_parent(path, parent)
def gc(path):
    return BEL.generated_class(unreal.load_asset(path))

lf_bp = make_bp(AB + "GA_LivingFlame", unreal.CurffeGA_LivingFlame.static_class())
lf = unreal.get_default_object(BEL.generated_class(lf_bp))
for k, v in {
    "input_tag": GTS.request_tag("InputTag.Ability.3"), "display_name": unreal.Text("Flamme vivante"),
    "activation_policy": unreal.GenAbilityActivationPolicy.ON_INPUT_TRIGGERED,
    "energy_cost": 25.0, "cast_time": 0.1,
    "cast_fx": unreal.load_asset("/Game/Gen/VFX/Stylized/NS_ST_Fireball_Cast"),
    "form_duration": 0.5, "burst_area_class": gc(AREAS + "BP_Area_FireBurst"),
    "burst_radius": 250.0, "burst_damage": 8.0, "burst_knockback": 300.0,
    "haste_multiplier": 1.3, "haste_duration": 2.0,
    "cooldown_duration": unreal.ScalableFloat(value=16.0),
    "cooldown_tags": GTS.request_tag_container(["Cooldown.Ability.LivingFlame"]),
    "ability_tags": GTS.request_tag_container(["Ability.LivingFlame"]),
}.items():
    lf.set_editor_property(k, v)

cb_bp = make_bp(AB + "GA_Combustion", unreal.CurffeGA_Combustion.static_class())
cb = unreal.get_default_object(BEL.generated_class(cb_bp))
for k, v in {
    "input_tag": GTS.request_tag("InputTag.Ability.Ultimate"), "display_name": unreal.Text("Combustion"),
    "activation_policy": unreal.GenAbilityActivationPolicy.ON_INPUT_TRIGGERED,
    "energy_cost": 100.0, "cast_time": 0.5, "cast_move_speed_multiplier": 0.5,
    "cast_fx": unreal.load_asset("/Game/Gen/VFX/Stylized/NS_ST_GreatFireball_Cast"),
    "nova_area_class": gc(AREAS + "BP_Area_Nova"), "nova_radius": 300.0, "nova_damage": 20.0, "nova_knockback": 300.0,
    "ablaze_duration": 5.0,
    "cooldown_duration": unreal.ScalableFloat(value=0.0),
    "ability_tags": GTS.request_tag_container(["Ability.Combustion"]),
}.items():
    cb.set_editor_property(k, v)

for name, bp in [("GA_LivingFlame", lf_bp), ("GA_Combustion", cb_bp)]:
    BEL.compile_blueprint(bp)
    print(name, EAL.save_asset(AB + name, only_if_is_dirty=False))
print("relu :", lf.get_editor_property("energy_cost"), cb.get_editor_property("energy_cost"), cb.get_editor_property("input_tag"))
```

  Expected: two `True` lines, then `relu : 25.0 100.0 InputTag.Ability.Ultimate`.

- [ ] **Step 6: Status visuals** (Art Bible §7.6, current version). Every shape is unique and none is a filled shell (the `State.Shielded` motif). All are placeholders until the GameplayCue status library.
  - Generic, on `BP_Champion`:
    - `State.CCImmune` (resilience): **one white ring flash, then a thin white halo** for the immunity. A rim-only (Fresnel) halo hugging the body, `#FFFFFF`, with a 0.15 s appear flash.
    - `State.Untouchable`: **the body dithers to a ghosted look** (masked, no translucency); the outline and the team ring stay. No shape: the body's material is swapped for `M_VFX_GhostDither` while the tag lasts (`owner_mesh_material`). Untouchable is game-wide, so it lives on `BP_Champion`.
  - On `BP_Curffe`, which copies the champion list and adds to it:
    - `State.Curffe.Ablaze`: **on the body only, no ground area** (§7.2 self-buff rule). Placeholder: a flame-shaped cone of `M_ST_FireOrb` around the body. The target is stepped flames over the body and enlarged orbiting flames; see Open points.
  - The spec's "living fire" form (Curffe §3 R) is **not** drawn here: §7.6 asks for a ghosted body for `State.Untouchable`. The conflict is flagged to the user in Open points (Art Bible §13); don't settle it here.

  First create `M_VFX_GhostDither` (after the user's go-ahead from Step 1): unlit, **masked**, used with skeletal meshes, a neutral `Colour`, and the engine's `DitherTemporalAA` function on the opacity mask.

```python
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
MNS = unreal.MaterialNodeService
EAL = unreal.EditorAssetLibrary
GD = "/Game/Gen/Rendering/Masters/M_VFX_GhostDither"
if EAL.does_asset_exist(GD):
    print("M_VFX_GhostDither existe déjà :", unreal.MaterialEditingLibrary.get_scalar_parameter_names(unreal.load_asset(GD)))
else:
    m = tools.create_asset("M_VFX_GhostDither", "/Game/Gen/Rendering/Masters", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("used_with_skeletal_mesh", True)
    # Neutre (Art Bible §7.6 : pas de teinte d'équipe) : text.secondary #C2A893
    c = MNS.create_parameter(GD, "Vector", "Colour", "Ghost", "0.539,0.392,0.292,1", -500, -100)
    a = MNS.create_parameter(GD, "Scalar", "GhostOpacity", "Ghost", "0.5", -800, 150)
    d = MNS.create_function_call(GD, "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA", -450, 150)
    pin = next(p.name for p in MNS.get_expression_pins(GD, d.id) if p.direction.lower().startswith("in") and p.name.startswith("Alpha"))
    print("connexion :", MNS.batch_connect_expressions(GD, [a.id], [""], [d.id], [pin]), "/ 1 (", pin, ")")
    print(MNS.connect_expression_to_output(GD, c.id, "", "EmissiveColor"), MNS.connect_expression_to_output(GD, d.id, "", "OpacityMask"))
    print("compile", unreal.MaterialService.compile_material(GD), MNS.get_material_diagnostics(GD))
    print("save", EAL.save_asset(GD, only_if_is_dirty=False))
```

  - Expected: `connexion : 1 / 1 ( Alpha Threshold )` (or the function's alpha input name), `True True`, `compile True` with no error, `save True`.
  - The `Colour` default is `text.secondary` computed with `unreal.GenUILibrary.hex_to_linear("#C2A893")`; check it before saving.

  Then the visuals:

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
GTS = unreal.GameplayTagService
shape = unreal.load_asset("/Game/Gen/Rendering/Masters/M_VFX_StatusShape")
ghost = unreal.load_asset("/Game/Gen/Rendering/Masters/M_VFX_GhostDither")
fire = unreal.load_asset("/Game/Gen/VFX/Stylized/M_ST_FireOrb")
hex_lin = unreal.GenUILibrary.hex_to_linear
def visual(tag, mesh=None, material=None, hex_colour="#FFFFFF", opacity=1.0, offset=(0, 0, 0), scale=(1, 1, 1),
           rim_only=False, rim_power=3.0, flash=0.0, owner_material=None):
    v = unreal.GenStatusVisual()
    v.set_editor_property("tag", GTS.request_tag(tag))
    if mesh:
        v.set_editor_property("mesh", unreal.load_asset(mesh))
    v.set_editor_property("material", material)
    v.set_editor_property("colour", hex_lin(hex_colour, 1.0))
    v.set_editor_property("opacity", opacity)
    v.set_editor_property("rim_only", rim_only)
    v.set_editor_property("rim_power", rim_power)
    v.set_editor_property("appear_flash_duration", flash)
    v.set_editor_property("offset", unreal.Vector(*offset))
    v.set_editor_property("scale", unreal.Vector(*scale))
    v.set_editor_property("owner_mesh_material", owner_material)
    return v

champ_bp = unreal.load_asset("/Game/Gen/Characters/BP_Champion")
champ = unreal.get_default_object(BEL.generated_class(champ_bp))
base = [v for v in champ.get_editor_property("status_visual_config")
        if str(v.get_editor_property("tag")) not in ("State.CCImmune", "State.Untouchable")]
base += [
    # Résilience : un flash d'anneau blanc, puis un halo blanc fin qui épouse le corps (contour seul, jamais une coque pleine)
    visual("State.CCImmune", "/Engine/BasicShapes/Sphere", shape, "#FFFFFF", 0.5, (0, 0, 0), (1.15, 1.15, 1.9), rim_only=True, rim_power=6.0, flash=0.15),
    # Intouchable : corps tramé « fantôme » (masqué), le contour et l'anneau d'équipe restent
    visual("State.Untouchable", owner_material=ghost),
]
champ.set_editor_property("status_visual_config", base)
BEL.compile_blueprint(champ_bp)

curffe_bp = unreal.load_asset("/Game/Gen/Champions/Curffe/BP_Curffe")
curffe = unreal.get_default_object(BEL.generated_class(curffe_bp))
curffe.set_editor_property("status_visual_config", base + [
    # Embrasé (PROVISOIRE) : flamme en cône sur le corps, rien au sol
    visual("State.Curffe.Ablaze", "/Engine/BasicShapes/Cone", fire, "#FFFFFF", 1.0, (0, 0, 0), (0.9, 0.9, 1.8)),
])
BEL.compile_blueprint(curffe_bp)
print(EAL.save_asset("/Game/Gen/Characters/BP_Champion", only_if_is_dirty=False), EAL.save_asset("/Game/Gen/Champions/Curffe/BP_Curffe", only_if_is_dirty=False))
print([str(v.get_editor_property("tag")) for v in curffe.get_editor_property("status_visual_config")])
```

  - Expected: `True True`, then 5 tags: Countering, Stunned, CCImmune, Untouchable, Ablaze.
  - **Readability capture** (`capture_image source=game`, gameplay camera, Standalone): put each state on the player in turn (`hard_cc`/`gain` helpers or the spells) and capture: the countering band, the stun disc, the CCImmune flash then halo, the ghosted body (the outline and the team ring still visible) and the ablaze cone. Each must read as a different shape in greyscale too (Art Bible §10). If two look alike, change the placeholder's scale or rim power, not its hue.

- [ ] **Step 7: BP_Curffe's abilities.**

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
AB = "/Game/Gen/Champions/Curffe/Abilities/"
P = "/Game/Gen/Champions/Curffe/BP_Curffe"
bp = unreal.load_asset(P)
d = unreal.get_default_object(BEL.generated_class(bp))
gc = lambda n: BEL.generated_class(unreal.load_asset(AB + n))
d.set_editor_property("startup_abilities", [gc(n) for n in ["GA_Fireball", "GA_Pyroblast", "GA_GreatFireball", "GA_FlameLeap",
                                                            "GA_Backfire", "GA_FlamePillar", "GA_LivingFlame", "GA_Combustion"]])
print(BEL.compile_blueprint(bp), EAL.save_asset(P, only_if_is_dirty=False), [c.get_name() for c in d.get_editor_property("startup_abilities")])
```

  - Expected: `True True` and the 8 names.
  - `GA_Fireball` comes before `GA_Pyroblast`, so the ability bar's Primary slot (first spec with that InputTag) shows the Fireball. Showing Pyroblast during Combustion is a UI follow-up; see Open points.

- [ ] **Step 8: Icons** (UI_Guidelines §2.11).
  - In `Content/Python/gen_ui_icons.py`, add `icon_living_flame()`, `icon_combustion()` and `icon_pyroblast()` in the script's style (256 px, flat bands, top-left light, the fire palette on the `#20160F` disc, no text), and add them to the `icons` dict in `main()` as `T_UI_Ability_Curffe_3`, `T_UI_Ability_Curffe_Ultimate` and `T_UI_Ability_Curffe_Pyroblast`. Give each a silhouette none of the five existing icons has:
    - Living Flame: a standing flame figure with a burst ring at its base;
    - Combustion: a radial eruption (a star of flame tongues around a bright core);
    - Pyroblast: a large comet whose head is clearly bigger than the Primary icon's, with a short, thick tail.
  - Generate them with the system Python: `python Content/Python/gen_ui_icons.py`.
  - **`run_checks` must still pass:** in `Saved/UIIcons/checks/checks.txt`, every pair of the eight icons has a silhouette IoU at 32 px of **≤ 0.45** (Pyroblast against Primary is the pair to watch), and each new icon reads in the greyscale and 2 px blur cells. If a pair fails, change the new icon's shape and regenerate.
  - Import them into **`/Game/Gen/UI/Textures/Icons/Abilities/`** with the existing icons' settings (compression **UserInterface2D** `TC_EDITOR_ICON`, **no mips**, texture group **UI**, **sRGB on**) and set `icon` on the abilities, as in Plan 2 Task 10 Step 12:

```python
import unreal, os
EAL = unreal.EditorAssetLibrary
BEL = unreal.BlueprintEditorLibrary
SRC = os.path.join(unreal.Paths.project_dir(), "Saved", "UIIcons")
DST = "/Game/Gen/UI/Textures/Icons/Abilities"
AB = "/Game/Gen/Champions/Curffe/Abilities/"
for name, ability in [("T_UI_Ability_Curffe_3", "GA_LivingFlame"), ("T_UI_Ability_Curffe_Ultimate", "GA_Combustion"),
                      ("T_UI_Ability_Curffe_Pyroblast", "GA_Pyroblast")]:
    path, err = unreal.AssetDiscoveryService.import_asset(os.path.join(SRC, name + ".png"), DST, name)
    tex = unreal.load_asset(DST + "/" + name)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("srgb", True)
    print(name, path, err, EAL.save_asset(DST + "/" + name, only_if_is_dirty=False))
    bp = unreal.load_asset(AB + ability)
    d = unreal.get_default_object(BEL.generated_class(bp))
    d.set_editor_property("icon", tex)
    BEL.compile_blueprint(bp)
    print(ability, EAL.save_asset(AB + ability, only_if_is_dirty=False), d.get_editor_property("icon"))
```

  - Expected: three imports with an empty error and `True`, then each ability saved with its icon.

- [ ] **Step 9: Smoke test** (Standalone, 1 player).
  - With 25 energy at the start, R works: the body turns to the ghost dither for 0.5 s (the outline stays), then the ring, and speed 715 for 2 s. Energy goes to 0, and the R slot shows "Not enough energy" (Task 10).
  - Set energy to 100 with `gain(1, energy=75)` from the PIE helpers (or the HUD). F: nova, then 5 s of Pyroblasts on LMB, with the ablaze cone on the body and nothing on the ground.
  - Hold RMB: 3 flames (the cap) in about 0.45 s instead of 0.9 s, and the Hearth stays at 5.
  - X during a long RMB feed cancels it.

- [ ] **Step 10: Commit** (no push, no unlock until merge). Tell the user that `M_VFX_GhostDither` now exists on `curffe-plan3` and is not pushed yet.

```bash
git add Content/Gen/Champions/Curffe Content/Gen/Characters/BP_Champion.uasset Content/Gen/Input Content/Gen/Rendering/Masters/M_VFX_GhostDither.uasset Content/Gen/UI/Textures/Icons/Abilities Content/Python/gen_ui_icons.py
git commit -m "Living Flame, Combustion, Pyroblast and cancel-key assets; resilience, untouchable and ablaze visuals; three ability icons"
```

---

### Task 12: PIE verification matrix (dedicated server + 3 clients)

**Files:**
- Modify: `Content/Python/gen_pie_tools.py`

- [ ] **Step 1: Add the helpers** at the end of `gen_pie_tools.py`:

```python
def hard_cc(client_index, seconds, tag_name="State.Stunned"):
    """Serveur : contrôle dur via ApplyHardCC (résilience comprise). Aucun sort actif chez la cible (RPC sous Python)."""
    return server_asc(client_index).apply_hard_cc(unreal.GameplayTagService.request_tag(tag_name), seconds, None)


def set_energy(client_index, value):
    """Serveur : met l'énergie du joueur à value."""
    return gain(client_index, energy=value - state(client_index)["energy"])


def is_ablaze(client_index):
    return has_tag(server_pawn_for(client_index), "State.Curffe.Ablaze")


def status_flashing(viewer_index, subject_index, tag_name):
    """Flash d'apparition de la forme d'état de subject en cours chez le client viewer."""
    p = client_pawn(viewer_index, subject_index)
    comp = p.get_editor_property("status_visuals") if p else None
    return bool(comp) and comp.is_status_flashing(unreal.GameplayTagService.request_tag(tag_name))


def body_material(viewer_index, subject_index, slot=0):
    """Matériau de la section slot du corps de subject chez le client viewer (corps fantôme d'Intouchable)."""
    p = client_pawn(viewer_index, subject_index)
    mesh = p.get_editor_property("mesh") if p else None
    m = mesh.get_material(slot) if mesh else None
    return m.get_name() if m else None


def slot_states(client_index):
    """États de la barre de sorts chez un client : {InputTag: état} (Ready, Cooldown, Locked, NoEnergy...)."""
    _, clients = worlds()
    slots = unreal.WidgetBlueprintLibrary.get_all_widgets_of_class(clients[client_index - 1], unreal.GenAbilitySlot, False)
    return {str(s.get_editor_property("input_tag")): str(s.get_state()) for s in slots}
```

- [ ] **Step 2: Set up the session**, exactly as in Plan 2 Task 11 Step 2 (Plan 2's `set_pkt_lag`, `client_walls` and the other helpers are already in `gen_pie_tools.py`).
  - Read the latency baseline first (Global Constraints) and Plan 2's latency results; don't rerun them.
  - Also turn on `log LogCurffeLivingFlame Verbose` and `log LogCurffeCombustion Verbose`.
  - Same frame of reference: c1 at (-700,-1250), with **+X** forward.

- [ ] **Step 3: Run the matrix.**
  - Make one `execute_python_code` call per action and read the results in a later call.
  - A row passes only if the expected result is observed.
  - Use `clear_cooldowns(i)` between attempts.
  - Kill with a projectile or an area, never with Python, while a spell is active.

| # | Scenario | Setup | Expected |
|---|---|---|---|
| W1 | **Living Flame cost** | (a) `set_energy(1, 24)`, then tap `IA_Ability_3`. (b) `set_energy(1, 25)`, then tap. (c) `set_energy(1, 60)`; c2's pillar stuns c1 during the 0.1 s cast (use `time_dilation(0.1)`) | (a) No `Activé` log and energy stays 24. (b) Energy goes 25→0 at release, and `Cooldown.Ability.LivingFlame` lasts 16 s. (c) The cast is cancelled, energy stays 60 and there is no cooldown |
| W2 | **Fire form** | c1 casts R. During the 0.5 s form: c2 fires a Fireball through c1 at a dummy behind c1, and c2's pillar lands on c1 | The Fireball **passes through** c1 (hp unchanged) and hits the dummy. The pillar deals no damage and no stun to c1. `status_shown(2, 1, "State.Untouchable")` is true and `body_material(2, 1)` is `M_VFX_GhostDither`: the body stays visible, dithered, with its outline and team ring; after the form it is back to its own material. Client 1 taps LMB: no activation. c1 can move |
| W3 | **Living Flame's end** | Enemy dummy at +200, ally c3 at -200, c1 at 1 flame | After 0.5 s: the dummy takes −8 and is knocked back by about 300 cm; **c3 is untouched**. c1 flames 1→5 (and `client_view_flames(2,1) == 5`). Speed is 715 for 2 s, then 550. Client 1 casts a Fireball during the haste and it fires |
| W4 | **Living Flame versus Backfire** | c2 counters; c1 casts R next to c2 | The ring (A) hits c2 through the counter: −8 and a knockback. c2 gains nothing |
| W5 | **Combustion cost and interrupt** | (a) `set_energy(1, 99)`, then tap `IA_Ability_Ultimate`. (b) `set_energy(1, 100)`; c2's pillar stuns c1 during the 0.5 s cast. (c) `set_energy(1, 100)` with no interruption | (a) No activation. (b) The cast is cancelled and energy stays 100. (c) Energy goes 100→0 **when the cast completes** (not at the press), then the nova fires |
| W6 | **Nova** | Enemy dummies at +150, +330 and +360. Ally c3 at +100. c2 countering at -200 | The +150 and +330 dummies take −20 and are knocked back 300 cm. The +360 dummy is untouched: a capsule is hit when its centre is within 300 + 42 cm. c3 is untouched. **c2 is hit through the counter** |
| W7 | **Pyroblast swap** | After W5 (c): hold LMB 2 s aimed at two enemy dummies 80 cm apart | `GA_Pyroblast` `Activé`, never `GA_Fireball`. Each shot: the direct target −13, the other dummy −13 (1.2 m splash). About 5 s after F, the log shows `GA_Fireball Activé` again and no more Pyroblast |
| W8 | **Unlimited flames** | During ablaze: hold RMB 0.6 s (3 flames, the cap) at a dummy, then E fed 3, then Space fed 3 | Each spell gets its full fed effect (Great Fireball −44 with knockback, pillar radius 350, a 3-Fireball ring: a triangle). The Hearth reads **5 after each spell**, on the server and on client 2 |
| W9 | **Fast feeding and validation** | (a) During ablaze, hold RMB for 0.55 s. (b) Start an RMB feed 4.8 s after F (ablaze ends mid-feed) | (a) `[CLIENT] Nourrissage terminé : 3 (0.45s)` (±1 frame), `Nourrissage validé : 3`, no `corrigé`. (b) The interval stays 0.15 s for that feed, and there is no `corrigé` |
| W10 | **Normal cadence (regression of the Plan 1 drift fix)** | Outside ablaze, hold RMB 1.2 s, with `watch_start()` running | `fed` reaches 1, 2 and 3 at 0.3, 0.6 and 0.9 s (±1 frame each, **no cumulative drift**). The 3rd arrives at 0.90–0.92 s and the count stays at 3 (the cap) for the rest of the hold |
| W11 | **Resilience** | No spell active on c2. Using `time_dilation(0.25)`, call `hard_cc(2, 1.0)` at t=0, 1.2 and 2.4 (game time); then `hard_cc(2, 1.0)` at t=3.0; then wait | After the third call: `State.CCImmune` is present and the white halo is visible on clients 1 and 3 (`status_shown(1, 2, "State.CCImmune")`), and `status_flashing(1, 2, "State.CCImmune")` is true for the first 0.15 s only (the ring flash). The call at 3.0 returns an invalid handle and no new stun. `State.CCImmune` ends around t=4.9. A pillar on c2 at t=5.2 stuns again |
| W12 | **Cancel key** | (a) c1 feeds RMB (3 flames), then taps `IA_Cancel`. (b) X with nothing in progress. (c) X during the Backfire window. (d) X during a leap in flight. (e) X during the Combustion cast | (a) `Fin (annulé=1)`, flames 5, no cooldown, preview or fed display cleared everywhere. (b) Nothing in the log. (c) and (d): the spell continues (`State.Countering` stays; the leap lands with its ring). (e) Cancelled, and energy stays 100 |
| W13 | **Untouchable versus counter** | c2 counters and c1 casts R; c3's Fireball at c1 during the form | The Fireball passes through c1. c1 gains nothing (no counter reward): untouchable comes first |
| W14 | **Death in a power state** | c1 ablaze (or immune after W11-type stuns) at 10 hp; c2 kills c1 with a pillar | After respawn: no `State.Curffe.Ablaze`, `State.CCImmune` or `State.FastFeeding` (`inspect_tags`). LMB fires a Fireball (not a Pyroblast). The feed rate is 0.3 s per flame. A fresh 1 s stun does not trigger immunity |

- [ ] **Step 3a: Ability bar row** (Task 10, client 1's HUD).

| # | Scenario | Setup | Expected |
|---|---|---|---|
| W15 | **Energy states on the bar** | `set_energy(1, 20)`, then 25, then 100; then `hard_cc(1, 1.0)` at 20 energy. Read `slot_states(1)` after each step and capture the bar | 20: `InputTag.Ability.3` and `InputTag.Ability.Ultimate` are `NoEnergy` (blue-grey wash, dimmed icon), R shows one hollow segment and F four. 25: R `Ready` with one funded segment; F still `NoEnergy` with one funded segment. 100: both `Ready`, F's arc complete in `energy.full` with one pulse. Stunned: every slot `Locked` |

- [ ] **Step 3b: Latency reruns.** Plan 1's and Plan 2's latency rows are the baseline; here only this plan's risky rows run again.
  - `set_pkt_lag(120)` (any value from 100 to 150 ms), then rerun **W3, W7, W9 and W12(a)**.
  - Expected: the same results as without latency, plus:
    - W3: client 1's Fireball pressed right at the end of the form is accepted by the server (the form's cast-lock window, Task 8); the flames read 5 on the server and on client 2 within about one RTT;
    - W3, **R then RMB**: from an empty Hearth, cast R, then press RMB within 100 ms of the form's end. The Great Fireball shows 3 notches and goes out fed 3 (the refill is predicted at the form's start and shown at its end; review of Tasks 8–10, I2);
    - W3, **haste edges** (review of Tasks 8–10, M3): the haste is still applied by the server only, so client 1 may see a small movement correction (about `0.3 × speed × RTT`, 20 cm at 120 ms) when the haste starts and ends. Note its size; it is a known limit, not a failure, until the per-machine speed multiplier (curffe-plan2, `f457e2b`) is merged and used for the haste;
    - W3, **form tags** (review of Tasks 8–10, M1): on client 1, `State.Untouchable` and `State.Curffe.LivingFlame` (and the ghost and fire layers) last about one RTT past client 1's own form, while client 1 can already cast. Display only: the server decides every hit. The Hearth flames come back at client 1's own form end;
    - W5 under latency (review of Tasks 8–10, M8): if a stun lands on the server just before Combustion's aim arrives, client 1 shows ablaze, the refill and −100 energy for about one RTT, then rolls back (no cost, no ablaze). A Pyroblast pressed in that RTT is refused. This is the correct rollback, not a bug;
    - W7, **at the end of ablaze**: hold LMB across the end. The server accepts a Pyroblast that client 1 starts in its last RTT of ablaze (required-tag grace of `GenFeeding::ServerTagGrace`, 0.25 s; review of Tasks 8–10, I1): no refused shot and no ghost cast bar, then `GA_Fireball` fires again. At most one Pyroblast is fired up to 0.25 s after the server's own ablaze ended;
    - W9: no `Nourrissage corrigé`, except at most one flame when the feed starts within one latency of the ablaze end (Review Focus 1);
    - W12(a): the cancel reaches the server before any aim: flames 5 and no cooldown on the server.
  - `set_pkt_lag(0)` afterwards.

- [ ] **Step 4: Clean up.**
  - `set_pkt_lag(0)`, `watch_stop()`, `PIEActorService.destroy_all()`, then `StopPIE`.
  - Restore Standalone with 1 client and turn background throttling back on.
  - Set the log categories back to `Log`.

- [ ] **Step 5: Fix any failure.** Each failing row is a bug. Use the systematic-debugging skill, fix it in the owning task's files, rebuild, and rerun that row and every row after it.

- [ ] **Step 6: Commit**

```bash
git add Content/Python/gen_pie_tools.py
git commit -m "PIE helpers for energy, resilience, status visuals, ability bar and Combustion verification"
```

  In the report, list the VibeUE skills and services you used (CLAUDE.md rule 4).

---

## Open points (not in this plan)

**Spec gaps and taste calls (questions for the user; this plan doesn't invent answers):**
- **"Living fire" versus Art Bible §7.6 (flag per Art Bible §13).** The spec (Curffe §3 R) says the mage *becomes living fire* for 0.5 s; the earlier draft of this plan hid the body behind a fire orb. The current Art Bible §7.6 says `State.Untouchable` **dithers the body to a ghosted look (masked, no translucency) and keeps the outline and team ring**, the same for every champion. This plan follows §7.6 (Task 11 Step 6) and does not decide the fantasy. Options for the user: keep the generic ghost; add a Curffe-only flame layer on top of the ghost (body still readable, outline and ring kept); or amend §7.6 for champion-specific untouchable forms. Any change goes through the §13 procedure (taste log, [TASTE] rules, change log) and must keep the §3.0 budget and the §10 readability tests.
- **Pyroblast in the Primary slot during Combustion.** The Primary slot shows the first spec with `InputTag.Ability.Primary` (the Fireball). Whether the slot swaps to Pyroblast (icon, name) while `State.Curffe.Ablaze` is active, and how, is a UI decision; then it is a follow-up for `UGenAbilitySlot`.
- **Combustion's audio and VFX.** The spec says Combustion has "unmistakable visuals and audio", and guidelines §1 say an ultimate has an audio cue enemies can hear. No sound assets exist yet, and the ablaze cone and the nova burst are placeholders below the Ultimate tier in Art Bible §7.2 (target per §7.6: stepped flames over the body and enlarged orbiting flames).
- **The 25-energy round start** (guidelines §4.1: 25 energy at the start of each round) belongs to the round system, not to Curffe. The spec doesn't say whether Curffe starts a round able to cast R at once; today the energy at spawn comes from the start-up effects.

**Art Bible and UI follow-ups:**
- **Art Bible §7.2** still lists `GA_GreatFireball` at VisualWeight 9 (spec §8: it should scale 5 → 8 with flames, and Combustion should be 9–10). That is a doc update for the Art Bible owner.
- §7.6 names the tag `State.Ablaze`; the code uses `State.Curffe.Ablaze` (a champion custom state). Align the Art Bible's table or the tag when the status library is built.
- The status shapes (countering band, CCImmune halo, ablaze cone) and `M_VFX_GhostDither` are placeholders until the GameplayCue status library and the VFX pass.

**Playtest and cheat notes:**
- **Cast time not enforced for Living Flame (cheat only).** Its `CastTime` of 0.1 s equals `GenFeeding::CastTimeTolerance`, so the server can't tell an honest client from one that skips the cast (same as Backfire in Plan 2). Honest clients are unaffected.
- **The form's tags outlast the form on the owner by about one RTT (review of Tasks 8–10, M1).** The cast lock is a local tag set and cleared by each machine (`SetCastLock`), so the owner can cast at its own form end. The form GE, though, is predicted and then replaced by the server's copy, whose removal reaches the owner about ½ RTT after the server's form ends: `State.Untouchable` and `State.Curffe.LivingFlame` stay on the owner until about `T_launch + 0.5 s + RTT`. It is display only (hits are server-side). The Hearth flames follow the owner's lock; driving the owner's ghost and fire layers from the local lock too is a Task 11 follow-up.
- **Living Flame's haste is server-only (review of Tasks 8–10, M3).** The refill is now predicted at launch, but the haste GE is applied by the server at its form end, so the owner gets small movement corrections at the haste's start and end (W3 under latency). curffe-plan2 (`f457e2b`) replaces predicted slows with a per-machine local move-speed multiplier on `AGenCharacterBase`; when the branches merge, move the haste to that pattern (each machine sets it at its own form end and clears it at its own haste end).
- **The `State.FreeResource` grace still favours low-RTT clients (review of Tasks 8–10, M2).** Any release that reaches the server within 0.25 s of the server's ablaze end is free, whether or not the client still had the tag, so a 50 ms client can predict a spend that the server doesn't make (the flames reappear on catch-up). Generic fix: send the client's view (`bFreeResource`, feed interval) in the aim target data and honour it only when the server's own tag change is within grace. The deferred launch also still uses the launch time, not the aim's arrival, as the grace reference.
- **Energy bounce on the owner after a predicted spend.** `Gen.Net.PowerSpells.Combustion_CostAtCastEnd_Nova_Ablaze5s` records the owner's energy as `150 → 50 → 150 → 50`: the predicted spend is removed when the prediction key catches up, one update before the server's new base value arrives. The cost is never paid twice (the test checks the minimum and the net), but the energy bar can flash back to the old value for one network update. It is generic GAS behaviour for every predicted cost (the ASC's key and the attribute set replicate separately); check it on the bar in W5(c) under latency, and if it shows, hold the displayed energy until the base value arrives.
- **Required-tag grace at ablaze start (review of Tasks 8–10, I1).** The server's grace covers a required tag that was *removed* recently (the end of ablaze). On the deferred-launch path (Combustion's aim arrives before the server's cast end), the client gains ablaze before the server does, and a Pyroblast pressed in that gap is still refused. Rare (it needs an early aim); fixing it means queueing the activation behind the deferred launch.
