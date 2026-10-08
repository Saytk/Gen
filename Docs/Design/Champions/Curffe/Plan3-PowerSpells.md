# Curffe Plan 3: Power Spells. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Curffe gets his energy spells, **Living Flame** (R, 25 energy) and **Combustion** (F, 100 energy: Pyroblast, unlimited flames, fast feeding). The game gets four game-wide systems:
- the **Untouchable** state;
- **Resilience**, which grants immunity to hard CC after 2.5 s of it within 5 s;
- **energy costs** paid on release;
- the **cancel key**.

Everything is verified in PIE with a dedicated server and 3 clients.

**Architecture:**
- **Generic systems** go in `Source/Gen/AbilitySystem/`:
  - `UGenGameplayAbility::EnergyCost`, enforced through `CheckCost`/`ApplyCost`. `CommitAbility` runs at release in `UGenGA_Cast`, so an interrupted cast costs nothing.
  - `State.Untouchable`, applied in the shared hit and CC chokepoints: `ResolveIncomingHit`, projectile overlap, `ApplyHardCC`, the damage meta-attribute and knockback.
  - Resilience inside `UGenAbilitySystemComponent::ApplyHardCC`.
  - Feeding on an interval snapshotted at feed start (the Plan 1 fixes already schedule ticks from the feed start). Two generic tags change it: `State.FastFeeding` (half the interval) and `State.FreeResource` (fed units are not spent).
  - The cancel key in `UGenInputConfig`, `AGenPlayerController` and the ASC.
- **Curffe-only code** goes in `Source/Gen/Champions/Curffe/`:
  - `UCurffeGA_LivingFlame` and `UCurffeGA_Combustion`;
  - the `State.Curffe.Ablaze` tag.
- **Pyroblast** is data only: `GA_Pyroblast`, a `UGenGA_Projectile` asset on the LMB slot, requires `State.Curffe.Ablaze`, and `GA_Fireball` is blocked by it.

**Tech Stack:** Unreal Engine 5.8, C++ module `Gen`, Gameplay Ability System (LocalPredicted abilities, ASC on the PlayerState, Mixed replication), Unreal Automation tests, VibeUE MCP for editor and PIE work.

**Spec:** `Docs/Design/Champions/Curffe/Curffe.md` (§2, §3 R/F/LMB, §4, §9) and `Docs/Design/CharacterGuidelines.md` (§3.1, §3.3, §3.5, §4.1). Visuals: `Docs/ArtBible.md` §7.2 and §7.6.

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
  - `UGenStatusVisualsComponent` and `StatusVisualConfig`;
  - `CurffeGameplayTags`.
- **Code comments in French**, matching the existing code. Docs use English (Canadian spelling).
- **Compiling.** Close the editor cleanly first (`unreal.SystemLibrary.quit_editor()`), then build with `Build.bat`. Never run `Plugins/VibeUE/BuildAndLaunchGame.ps1`.
- **Editor Python:** `execute_python_code` always runs with `auto_save: false`. Save only the assets you changed.
- **LFS locks.**
  - Before modifying an existing `.uasset`, run `git fetch origin` and check `git diff --stat HEAD...origin/main -- Content`. Then run `git lfs locks` and `git lfs lock <path>`. Never use `--force`.
  - `IMC_Arena`, `DA_InputConfig`, `GA_Fireball` and `GA_GreatFireball` are also edited on `ui-ability-bar`. If they changed on `main` or are locked by someone else, stop and ask the user about the merge order.
- **PIE verification** uses a dedicated server and **3 clients**. Clients 1 and 3 are team 0 and client 2 is team 1. Restore Standalone with 1 client afterwards.
- **Starting values (spec §3, copied verbatim):**
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
      3. **Fast feeding:** 0.1 s per flame instead of 0.2 s; telegraphs never drop below 0.5 s.
    - Not immune to CC.
  - **Resilience (guidelines §3.3):** after 2.5 s of hard CC within 5 s, the target is immune to hard CC for 1.5 s, with a visible effect.
  - **Invulnerability (guidelines §3.5):** 0.5 s or less. Untouchable means projectiles pass through and damage is ignored.
  - **Costs (guidelines §3.1):** cooldown and energy are only spent when the spell actually goes off. A cancelled or interrupted cast costs nothing.
  - **Cancel key (guidelines §3.1):** "A cancel key cancels the current cast."
- **Key slots** (AZERTY): R is `InputTag.Ability.3`, F is `InputTag.Ability.Ultimate`, and Pyroblast shares `InputTag.Ability.Primary` with Fireball.
- **Every new or changed ability sets** `InputTag`, `DisplayName` (French), `CooldownTags`/`CooldownDuration` (none for F) and `EnergyCost`. It also sets `Icon` when the property exists (it arrives with the `ui-ability-bar` merge).
- **Folders.** Generic code goes in `Source/Gen/AbilitySystem/**`, `Source/Gen/Character/` or `Source/Gen/Player/`. Curffe-only code goes in `Source/Gen/Champions/Curffe/`.

## Review Focus

1. **Ablaze starts or ends while a spell is feeding.**
   - Expected: each machine keeps the interval it had when its feeding started, and the server validates against its own interval.
   - An honest client never sees `Nourrissage corrigé`. At worst, a feed that began in the last ~0.1 s of ablaze is clamped by one flame.
   - Pinned by Task 1 (`Gen.Feeding.FastInterval`) and Task 11 W9.
2. **Energy exactly at the cost.** For example, 25 energy for Living Flame, 100 for Combustion, or 99.99 after rounding. Expected: 25.0 and 100.0 pass, and anything under fails, with no float drift. Pinned by Task 1 (`Gen.Energy.CanAfford`) and Task 11 W1 and W5.
3. **A hard CC lands while the target is untouchable, while immune, or from two overlapping sources.** Expected: untouchable and immune targets ignore it. Overlapping stuns count once (the union of the intervals) toward the 2.5 s. Pinned by Task 1 (`Gen.Combat.ResilienceHistory`), Task 4 (`Gen.Combat.Untouchable`), Task 5 (`Gen.Combat.Resilience`) and Task 11 W11.
4. **Cancel key edge cases.** Expected:
   - with nothing being cast, nothing happens;
   - during a spell that has already gone off (Backfire window, leap in flight, Living Flame form), nothing happens;
   - on the server after the client's aim has arrived, the cast is not cancelled and the costs stay paid (`CanBeCanceled` is false).

   Pinned by Task 11 W12.
5. **Death while ablaze, untouchable or immune.** Expected: every timed state is removed at death, the resilience history resets, and the respawned mage has a normal LMB (Fireball) and normal feeding. Pinned by Task 5 (`Gen.Combat.Resilience` reset case) and Task 11 W14.

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
| `Content/Gen/Champions/Curffe/**` | `GA_LivingFlame`, `GA_Combustion`, `GA_Pyroblast`, `BP_Projectile_Pyroblast`, status visuals |
| `Content/Gen/Input/**` | `IA_Cancel`, mapping and config |
| `Content/Python/gen_pie_tools.py` | `hard_cc`, `energy` helpers |

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
  - `GenFeeding::FastFeedMultiplier` (0.5)
  - `GenFeeding::GetFeedInterval(float BaseInterval, bool bFastFeeding) -> float`
  - `GenEnergy::CanAfford(float Energy, float Cost) -> bool`
  - `GenResilience::{Window = 5, Threshold = 2.5, ImmunityDuration = 1.5}`
  - `GenResilience::FHardCCHistory::Record(float Now, float Duration) -> float` (the immunity duration to grant now; 0 = none) and `Reset()`
  - `EGenHitResponse::Ignored`
  - `GenHitRules::Resolve(bool bCountering, bool bUntouchable, EGenHitKind) -> EGenHitResponse`. This replaces the two-argument version.

- [ ] **Step 0: Create the branch.** `git switch -c curffe-plan3` (from the tip of `curffe-plan2`).

- [ ] **Step 1: Write the failing tests** in `Source/Gen/Tests/GenPowerRulesTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenEnergy.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenResilience.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFastFeedingTest, "Gen.Feeding.FastInterval",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFastFeedingTest::RunTest(const FString& Parameters)
{
	// GetFeedInterval(BaseInterval, bFastFeeding)
	TestEqual(TEXT("intervalle normal"), GenFeeding::GetFeedInterval(0.2f, false), 0.2f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Combustion : 0.1 s par flamme"), GenFeeding::GetFeedInterval(0.2f, true), 0.1f, KINDA_SMALL_NUMBER);
	TestTrue(TEXT("jamais nul"), GenFeeding::GetFeedInterval(0.f, true) > 0.f);

	// Avec l'intervalle rapide, les ticks calés sur le début du nourrissage (GetNextFeedTickDelay,
	// correctif de la dérive du plan 1) tombent à 0.1, 0.2... : la 5e flamme à 0.5 s
	const float Fast = GenFeeding::GetFeedInterval(0.2f, true);
	TestEqual(TEXT("5e flamme à 0.5 s"), GenFeeding::GetNextFeedTickDelay(0.f, 4, Fast, 0.4f), 0.1f, 0.0001f);
	TestEqual(TEXT("tick en retard d'une image : pas de dérive"), GenFeeding::GetNextFeedTickDelay(0.f, 2, Fast, 0.217f), 0.083f, 0.0001f);

	// Validation serveur avec l'intervalle rapide : 5 flammes en 0.5 s acceptées
	TestEqual(TEXT("5 en 0.5 s validées"), GenFeeding::ValidateFedCount(5, 5, 5.f, 0.5f, Fast), 5);
	TestEqual(TEXT("avec l'intervalle normal, ce serait 3"), GenFeeding::ValidateFedCount(5, 5, 5.f, 0.5f, 0.2f), 3);
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
	/** Combustion : nourrissage deux fois plus rapide (spec : 0.1 s au lieu de 0.2 s). */
	inline constexpr float FastFeedMultiplier = 0.5f;

	/** Intervalle de nourrissage, retenu au début du nourrissage (State.FastFeeding). */
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
    - In `GenCharacterBase.cpp`, `ResolveIncomingHit`, change it temporarily to `GenHitRules::Resolve(bCountering, false, Kind)`. Task 4 replaces `false`.

- [ ] **Step 7: Build, then run the unit tests.** Expected: `Gen.Feeding.FastInterval`, `Gen.Energy.CanAfford`, `Gen.Combat.ResilienceHistory` and `Gen.Combat.UntouchableRule` pass, and every Plan 1 and Plan 2 test still passes.

- [ ] **Step 8: Commit**

```bash
git add Source/Gen/AbilitySystem/GenFeeding.h Source/Gen/AbilitySystem/GenEnergy.h Source/Gen/AbilitySystem/GenResilience.h Source/Gen/AbilitySystem/GenHitRules.h Source/Gen/Tests Source/Gen/Character/GenCharacterBase.cpp
git commit -m "Pure rules: fast feeding interval, energy affordability, resilience history, untouchable response"
```

---

### Task 2: Fast feeding and free resource in `UGenGA_Cast`

The Plan 1 feed drift (about one frame per flame from chained timers) is already fixed upstream (commit `8c378e7`): ticks are scheduled from the feed start with `GenFeeding::GetNextFeedTickDelay`. This task makes that schedule, the cast bar and the server validation use the interval **snapshotted at feed start**, so Combustion's 0.1 s feeding stays exact. It also adds the two generic tags Combustion grants.

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
	/** Intervalle retenu au début du nourrissage (rapide sous State.FastFeeding), sur chaque machine. */
	float ActiveFeedInterval = 0.2f;
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

### Task 3: Energy costs paid on release

**Files:**
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h/.cpp`
- Test: `Source/Gen/Tests/GenPowerWorldTests.cpp`

**Interfaces:**
- Consumes: `GenEnergy::CanAfford` (Task 1), `UGenGE_Gain::SetMagnitudes`, `GenTestWorld` (Plan 2).
- Produces:
  - `UGenGameplayAbility::EnergyCost` (`float`, EditDefaultsOnly, BlueprintReadOnly, public; Python `energy_cost`). The ability bar reads it.
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

  If `ApplyCost` on the CDO asserts because the ability is instanced, delete the `ApplyCost` lines and their expectation. Note it in the commit; Task 11 W1 then covers the spend.

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

### Task 4: Untouchable state

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

- [ ] **Step 2: Build to verify it fails.** Expected: `'State_Untouchable': is not a member of 'GenGameplayTags'`.

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

### Task 5: Resilience (immunity to hard CC)

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

	// Étourdi : ne bouge plus. Les autres contrôles durs (silence...) laisseront bouger.
	const float MoveSpeedMultiplier = StateTag.MatchesTagExact(GenGameplayTags::State_Stunned) ? 0.f : 1.f;
	UGenGE_TimedMoveSpeed::SetMagnitudes(*Spec.Data, Duration, MoveSpeedMultiplier, FGameplayTagContainer(StateTag));
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

- [ ] **Step 4: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes. The behaviour is checked in Task 11 W12.

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
    1. Launch (after the 0.1 s cast; the energy and cooldown are paid by the commit) applies the form, a timed state of `FormDuration` with `State.Untouchable` and `State.CastLocked`. It is predicted.
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
  - `UGenGA_Cast` (cast interrupted by a stun; the commit at release pays `EnergyCost`);
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
  - The Pyroblast swap is data only (Task 10): `GA_Pyroblast` requires `State.Curffe.Ablaze` and `GA_Fireball` is blocked by it.
  - "Telegraphs never drop below 0.5 s" needs no new code. Fast feeding only shortens the feed phase, and delayed areas already clamp their delay to `MinTelegraph` (0.5 s) through `GenAreaRules::GetImpactDelay` (Plan 2). Flame Pillar keeps 0.4 s + 0.8 s.

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

### Task 10: Editor assets: Living Flame, Combustion, Pyroblast, cancel key, status visuals, BP_Curffe

The editor must be open with the new binaries. Load the VibeUE skills first: `VibeUE_blueprints`, `VibeUE_gas`, `VibeUE_gameplay_tags`, `VibeUE_enhanced_input`. Before touching visuals, read the Art Bible §7.2 and §7.6.

**Files (assets):**
- Create:
  - `Content/Gen/Champions/Curffe/Abilities/GA_LivingFlame`
  - `Content/Gen/Champions/Curffe/Abilities/GA_Combustion`
  - `Content/Gen/Champions/Curffe/Abilities/GA_Pyroblast`
  - `Content/Gen/Champions/Curffe/Projectiles/BP_Projectile_Pyroblast`
  - `Content/Gen/Champions/Curffe/Areas/BP_Area_Nova`
  - `Content/Gen/Input/IA_Cancel`
- Modify:
  - `GA_Fireball` (blocked while ablaze) and `GA_GreatFireball` (must stay castable);
  - `BP_Curffe` and `BP_Champion` (status visuals, startup abilities);
  - `IMC_Arena` and `DA_InputConfig` (cancel key).

- [ ] **Step 1: Lock the assets.**

```bash
git fetch origin
git diff --stat HEAD...origin/main -- Content
git lfs locks
for f in Content/Gen/Champions/Curffe/Abilities/GA_Fireball.uasset Content/Gen/Champions/Curffe/Abilities/GA_GreatFireball.uasset Content/Gen/Champions/Curffe/BP_Curffe.uasset Content/Gen/Characters/BP_Champion.uasset Content/Gen/Input/IMC_Arena.uasset Content/Gen/Input/DA_InputConfig.uasset; do git lfs lock "$f"; done
```

  If `git diff` lists any of these files changed on `main` (by `ui-ability-bar`, for example), stop and ask the user which order to merge in.

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
# Visuel de la grosse boule de feu (plus gros), portée et vitesse de la boule de feu (spec : "bigger projectile")
cdo(pyro).set_editor_property("speed", 1200.0)
cdo(pyro).set_editor_property("max_range", 1100.0)
BEL.compile_blueprint(pyro)
print(PYRO, EAL.save_asset(PYRO, only_if_is_dirty=False))

NOVA = "/Game/Gen/Champions/Curffe/Areas/BP_Area_Nova"
if not EAL.does_asset_exist(NOVA):
    EAL.duplicate_asset("/Game/Gen/Champions/Curffe/Areas/BP_Area_FlamePillar", NOVA)
nova = unreal.load_asset(NOVA)
BEL.compile_blueprint(nova)
print(NOVA, EAL.save_asset(NOVA, only_if_is_dirty=False))
```

  - Expected: two `True` lines.
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

- [ ] **Step 6: Status visuals.**
  - Generic, on `BP_Champion`: resilience gets a white-gold shell (the motif of `State.Shielded` in Art Bible §7.6).
  - On `BP_Curffe`, which copies the champion list and adds to it:
    - the fire form: the body is hidden and replaced by a fire orb;
    - ablaze: a fire disc at the feet. This is a placeholder for the ultimate's VFX, see Open points.

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
GTS = unreal.GameplayTagService
shape = unreal.load_asset("/Game/Gen/Rendering/Masters/M_VFX_StatusShape")
fire = unreal.load_asset("/Game/Gen/VFX/Stylized/M_ST_FireOrb")
def visual(tag, mesh, material, colour, opacity, offset, scale, hide=False):
    v = unreal.GenStatusVisual()
    v.set_editor_property("tag", GTS.request_tag(tag))
    v.set_editor_property("mesh", unreal.load_asset(mesh))
    v.set_editor_property("material", material)
    v.set_editor_property("colour", unreal.LinearColor(*colour, 1.0))
    v.set_editor_property("opacity", opacity)
    v.set_editor_property("offset", unreal.Vector(*offset))
    v.set_editor_property("scale", unreal.Vector(*scale))
    v.set_editor_property("hide_owner_mesh", hide)
    return v

champ_bp = unreal.load_asset("/Game/Gen/Characters/BP_Champion")
champ = unreal.get_default_object(BEL.generated_class(champ_bp))
base = list(champ.get_editor_property("status_visual_config"))
base.append(visual("State.CCImmune", "/Engine/BasicShapes/Sphere", shape, (0.888, 0.79, 0.54), 0.25, (0, 0, 0), (1.7, 1.7, 2.3)))
champ.set_editor_property("status_visual_config", base)
BEL.compile_blueprint(champ_bp)

curffe_bp = unreal.load_asset("/Game/Gen/Champions/Curffe/BP_Curffe")
curffe = unreal.get_default_object(BEL.generated_class(curffe_bp))
curffe.set_editor_property("status_visual_config", base + [
    visual("State.Untouchable", "/Engine/BasicShapes/Sphere", fire, (1, 1, 1), 1.0, (0, 0, 0), (1.3, 1.3, 2.0), hide=True),
    visual("State.Curffe.Ablaze", "/Engine/BasicShapes/Cylinder", fire, (1, 1, 1), 1.0, (0, 0, -90), (2.2, 2.2, 0.05)),
])
BEL.compile_blueprint(curffe_bp)
print(EAL.save_asset("/Game/Gen/Characters/BP_Champion", only_if_is_dirty=False), EAL.save_asset("/Game/Gen/Champions/Curffe/BP_Curffe", only_if_is_dirty=False))
print([str(v.get_editor_property("tag")) for v in curffe.get_editor_property("status_visual_config")])
```

  Expected: `True True`, then 5 tags: Countering, Stunned, CCImmune, Untouchable, Ablaze.

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

- [ ] **Step 8: Icons.** If the `icon` property and `Content/Python/gen_ui_icons.py` exist, add `Curffe_3` (Living Flame), `Curffe_Ultimate` (Combustion) and `Curffe_Pyroblast`, generate them, and set them. Otherwise skip this step and list it as pending in the report.

- [ ] **Step 9: Smoke test** (Standalone, 1 player).
  - With 25 energy at the start, R works: a fire orb, then a ring, and speed 715 for 2 s. Energy goes to 0.
  - Set energy to 100 with `gain(1, energy=75)` from the PIE helpers (or the HUD). F: nova, then 5 s of Pyroblasts on LMB.
  - Hold RMB: 5 flames in about 0.5 s, and the Hearth stays at 5.
  - X during a long RMB feed cancels it.

- [ ] **Step 10: Commit** (no push, no unlock until merge).

```bash
git add Content/Gen/Champions/Curffe Content/Gen/Characters/BP_Champion.uasset Content/Gen/Input
git commit -m "Living Flame, Combustion, Pyroblast and cancel-key assets; resilience and fire-form visuals"
```

---

### Task 11: PIE verification matrix (dedicated server + 3 clients)

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
```

- [ ] **Step 2: Set up the session**, exactly as in Plan 2 Task 11 Step 2.
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
| W2 | **Fire form** | c1 casts R. During the 0.5 s form: c2 fires a Fireball through c1 at a dummy behind c1, and c2's pillar lands on c1 | The Fireball **passes through** c1 (hp unchanged) and hits the dummy. The pillar deals no damage and no stun to c1. `status_shown(2, 1, "State.Untouchable")` is true and the body is hidden. Client 1 taps LMB: no activation. c1 can move |
| W3 | **Living Flame's end** | Enemy dummy at +200, ally c3 at -200, c1 at 1 flame | After 0.5 s: the dummy takes −8 and is knocked back by about 300 cm; **c3 is untouched**. c1 flames 1→5 (and `client_view_flames(2,1) == 5`). Speed is 715 for 2 s, then 550. Client 1 casts a Fireball during the haste and it fires |
| W4 | **Living Flame versus Backfire** | c2 counters; c1 casts R next to c2 | The ring (A) hits c2 through the counter: −8 and a knockback. c2 gains nothing |
| W5 | **Combustion cost and interrupt** | (a) `set_energy(1, 99)`, then tap `IA_Ability_Ultimate`. (b) `set_energy(1, 100)`; c2's pillar stuns c1 during the 0.5 s cast. (c) `set_energy(1, 100)` with no interruption | (a) No activation. (b) The cast is cancelled and energy stays 100. (c) Energy goes 100→0 **when the cast completes** (not at the press), then the nova fires |
| W6 | **Nova** | Enemy dummies at +150, +330 and +360. Ally c3 at +100. c2 countering at -200 | The +150 and +330 dummies take −20 and are knocked back 300 cm. The +360 dummy is untouched: a capsule is hit when its centre is within 300 + 42 cm. c3 is untouched. **c2 is hit through the counter** |
| W7 | **Pyroblast swap** | After W5 (c): hold LMB 2 s aimed at two enemy dummies 80 cm apart | `GA_Pyroblast` `Activé`, never `GA_Fireball`. Each shot: the direct target −13, the other dummy −13 (1.2 m splash). About 5 s after F, the log shows `GA_Fireball Activé` again and no more Pyroblast |
| W8 | **Unlimited flames** | During ablaze: hold RMB 1 s (5 flames) at a dummy, then E fed 5, then Space fed 5 | Each spell gets its full fed effect (Great Fireball −44 with knockback, pillar radius 350, a 5-Fireball ring). The Hearth reads **5 after each spell**, on the server and on client 2 |
| W9 | **Fast feeding and validation** | (a) During ablaze, hold RMB for 0.55 s. (b) Start an RMB feed 4.8 s after F (ablaze ends mid-feed) | (a) `[CLIENT] Nourrissage terminé : 5 (0.50s)` (±1 frame), `Nourrissage validé : 5`, no `corrigé`. (b) The interval stays 0.1 s for that feed, and there is no `corrigé` |
| W10 | **Normal cadence (regression of the Plan 1 drift fix)** | Outside ablaze, hold RMB 1.2 s, with `watch_start()` running | `fed` reaches 1, 2, 3, 4 and 5 at 0.2, 0.4, 0.6, 0.8 and 1.0 s (±1 frame each, **no cumulative drift**). The 5th arrives at 1.00–1.02 s |
| W11 | **Resilience** | No spell active on c2. Using `time_dilation(0.25)`, call `hard_cc(2, 1.0)` at t=0, 1.2 and 2.4 (game time); then `hard_cc(2, 1.0)` at t=3.0; then wait | After the third call: `State.CCImmune` is present and the shell is visible on clients 1 and 3. The call at 3.0 returns an invalid handle and no new stun. `State.CCImmune` ends around t=4.9. A pillar on c2 at t=5.2 stuns again |
| W12 | **Cancel key** | (a) c1 feeds RMB (3 flames), then taps `IA_Cancel`. (b) X with nothing in progress. (c) X during the Backfire window. (d) X during a leap in flight. (e) X during the Combustion cast | (a) `Fin (annulé=1)`, flames 5, no cooldown, preview or fed display cleared everywhere. (b) Nothing in the log. (c) and (d): the spell continues (`State.Countering` stays; the leap lands with its ring). (e) Cancelled, and energy stays 100 |
| W13 | **Untouchable versus counter** | c2 counters and c1 casts R; c3's Fireball at c1 during the form | The Fireball passes through c1. c1 gains nothing (no counter reward): untouchable comes first |
| W14 | **Death in a power state** | c1 ablaze (or immune after W11-type stuns) at 10 hp; c2 kills c1 with a pillar | After respawn: no `State.Curffe.Ablaze`, `State.CCImmune` or `State.FastFeeding` (`inspect_tags`). LMB fires a Fireball (not a Pyroblast). The feed rate is 0.2 s per flame. A fresh 1 s stun does not trigger immunity |

- [ ] **Step 4: Clean up.**
  - `watch_stop()`, `PIEActorService.destroy_all()`, then `StopPIE`.
  - Restore Standalone with 1 client and turn background throttling back on.
  - Set the log categories back to `Log`.

- [ ] **Step 5: Fix any failure.** Each failing row is a bug. Use the systematic-debugging skill, fix it in the owning task's files, rebuild, and rerun that row and every row after it.

- [ ] **Step 6: Commit**

```bash
git add Content/Python/gen_pie_tools.py
git commit -m "PIE helpers for energy, resilience and Combustion verification"
```

  In the report, list the VibeUE skills and services you used (CLAUDE.md rule 4).

---

## Open points (not in this plan)

- **Audio.** The spec says Combustion has "unmistakable visuals and audio", and guidelines §1 say an ultimate has an audio cue enemies can hear. No sound assets exist yet. The current ablaze visual is a placeholder fire disc, below the Ultimate tier in Art Bible §7.2.
- **Ability bar during Combustion.** The Primary slot shows the first spec with `InputTag.Ability.Primary` (the Fireball). Showing Pyroblast while `State.Curffe.Ablaze` is active is a follow-up for `UGenAbilitySlot` on `ui-ability-bar`.
- **Art Bible §7.2** still lists `GA_GreatFireball` at VisualWeight 9 (spec §8: it should scale 5 → 8 with flames, and Combustion should be 9–10). That is a doc update for the Art Bible owner.
- **Energy reset per round** (guidelines §4.1: 25 at the start of each round) belongs to the round system, not to Curffe.
