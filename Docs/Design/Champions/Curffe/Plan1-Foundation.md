# Curffe Plan 1: Foundation and Core Loop. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Curffe exists as a playable champion with his Hearth (5 orbiting flames), energy and flame gains on hit, and a fed Great Fireball (hold to feed flames), verified in PIE with a dedicated server and several clients.

**Architecture:**
- **Generic systems** live in the shared `Source/Gen/AbilitySystem` folders so later champions reuse them:
  - a champion `Resource` attribute (Curffe uses it as flames);
  - a gain/spend effect;
  - hold-to-feed on `UGenGA_Projectile`;
  - splash and knockback on `AGenProjectile`;
  - aim target data that carries the fed count.
- **Curffe-only code** (Hearth effects, orbit visuals) lives in `Source/Gen/Champions/Curffe`.
- **Curffe assets** move to `Content/Gen/Champions/Curffe`.
- **Authority:** the client decides how many flames it fed; the server clamps that number to its own flames and to the time it measured, then spends them when the spell is released.

**Tech Stack:** Unreal Engine 5.8, C++ module `Gen`, Gameplay Ability System (LocalPredicted abilities, ASC on PlayerState, Mixed replication), Unreal Automation tests, VibeUE MCP for editor and PIE work.

**Spec:** `Docs/Design/Champions/Curffe/Curffe.md`, plus `Docs/Design/CharacterGuidelines.md`.

## Global Constraints

- **Code comments in French**, matching the existing code. Docs in English (Canadian spelling).
- **Compiling:** close the editor cleanly first (`unreal.SystemLibrary.quit_editor()`), then build with `Build.bat GenEditor Win64 Development`. Never run `Plugins/VibeUE/BuildAndLaunchGame.ps1`.
- **Editor Python:** `execute_python_code` always runs with `auto_save: false`. Save only the assets you changed.
- **LFS locks:** before modifying any `.uasset` or `.umap`, run `git pull` then `git lfs lock <path>`. Unlock after the commit is pushed (see `CLAUDE.md`).
- **PIE verification** uses a dedicated server and **3 clients**, so that two players share a team (clients 1 and 3 are team 0, client 2 is team 1). Restore the user's PIE settings afterwards (Standalone, 1 client).
- **Starting values (spec §3):**
  - Fireball: 0.35 s cast, no cooldown, 11 m, 10 damage, +1 flame and +2 energy on hit.
  - Great Fireball: 0.5 s + 0.2 s per flame, 6 s cooldown, 13 m, 14 + 6 per flame.
    - Explosion of 1.5 m from 3 flames; knockback of 4 m at 5 flames.
    - Speed 25 m/s at 0 flames, 16 m/s at 5 flames.
    - +6 energy, +1 per flame.
- **Hearth:** 5 flames, +1 every 3 s passively.
- **Energy** starts at 25.
- **Costs are paid on release:** a cancelled or interrupted cast spends no cooldown and no flames.

## Review Focus

1. **The client and server disagree on the fed count** (packet jitter, or a flame gain still in flight). Expected: the server clamps the count, logs a correction, never spends more flames than it has, and the client's prediction reconciles. Pinned by Task 9, step V3.
2. **Holding LMB (auto-repeat) while pressing RMB.** Expected: the Great Fireball starts and is not cancelled by the next auto-repeat. A *fresh* LMB press still cancels it. Pinned by Task 6, step V5.
3. **Splash near walls, allies and dead characters.** Expected: no damage through walls, none to allies or the dead, and knockback stops at walls. Pinned by Task 9, steps V6–V7.
4. **Cancel or stun during feeding.** Expected: no flames spent, no cooldown, the orbit is restored and the slow is removed. Pinned by Task 9, step V4.
5. **Death or respawn while feeding or empty.** Expected: no leftover slow or cast bar, and flames back to 5 on respawn. Pinned by Task 9, step V8.

---

## File map

| File | Responsibility |
|---|---|
| `Source/Gen/AbilitySystem/GenFeeding.h` (new) | Pure feeding rules (limits, server validation, scaling) |
| `Source/Gen/AbilitySystem/GenKnockback.h` (new) | Pure knockback launch-velocity maths |
| `Source/Gen/AbilitySystem/GenTargetData.h/.cpp` (new) | `FGenTargetData_Aim`: cursor hit plus fed count, net-serialised |
| `Source/Gen/AbilitySystem/Effects/GenGE_Gain.h/.cpp` (new) | Instant gain/spend of Energy and Resource (SetByCaller) |
| `Source/Gen/AbilitySystem/GenAttributeSet.h/.cpp` | Adds `Resource` / `MaxResource`; energy starts at 25 |
| `Source/Gen/GenGameplayTags.h/.cpp` | `SetByCaller.Energy`, `SetByCaller.Resource` |
| `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h/.cpp` | Feeding, gains, scaled shots |
| `Source/Gen/AbilitySystem/Tasks/GenAbilityTask_TargetDataUnderCursor.h/.cpp` | Sends `FGenTargetData_Aim` with the fed count |
| `Source/Gen/Actors/GenProjectile.h/.cpp` | Per-shot speed and scale, splash with line of sight, knockback, instigator gains |
| `Source/Gen/Character/GenCharacterBase.h/.cpp` | Resource getters, replicated fed count, knockback |
| `Source/Gen/Character/GenTrainingDummy.cpp` | Dummy can be knocked back (physics without a controller) |
| `Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp` | Auto-repeat never interrupts another cast |
| `Source/Gen/Player/GenPlayerController.h/.cpp` | Debug aim override for PIE tests |
| `Source/Gen/UI/GenHUD.cpp` | Shows the flame count |
| `Source/Gen/Champions/Curffe/CurffeTuning.h` (new) | Curffe constants |
| `Source/Gen/Champions/Curffe/CurffeEffects.h/.cpp` (new) | Hearth setup, fill and regen effects |
| `Source/Gen/Champions/Curffe/CurffeHearthComponent.h/.cpp` (new) | Orbiting flames (cosmetic) |
| `Source/Gen/Tests/GenFeedingTests.cpp` (new) | Pure-rule tests |
| `Source/Gen/Tests/GenResourceTests.cpp` (new) | GAS attribute and effect tests in a test world |
| `Content/Python/gen_pie_tools.py` (new) | PIE test helpers (worlds, pawns, aim, input) |
| `Content/Gen/Champions/Curffe/**` | Curffe's moved and new assets |

**Running the unit tests** (editor closed, after a successful build):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\Samy D\Documents\Unreal Projects\Gen\Gen.uproject" -ExecCmds="Automation RunTests Gen.;Quit" -unattended -nopause -nosplash -nullrhi -NoSound "-abslog=C:\Users\Samy D\Documents\Unreal Projects\Gen\Saved\Logs\GenTests.log"
Select-String -Path "C:\Users\Samy D\Documents\Unreal Projects\Gen\Saved\Logs\GenTests.log" -Pattern "Test Completed. Result=|Error:" | Select-Object -Last 40
```

**Building** (editor closed):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GenEditor Win64 Development "-Project=C:\Users\Samy D\Documents\Unreal Projects\Gen\Gen.uproject" -WaitMutex | Select-Object -Last 15
```

---

### Task 0: Finish the pending cast fix (prerequisite)

The previous session left uncommitted, already-compiled changes: `GA_Fireball` and `GA_GreatFireball` cancel each other, and `UGenGA_Projectile` commits on release.

**Files:** `Content/Gen/Abilities/GA_Fireball.uasset`, `Content/Gen/Abilities/GA_GreatFireball.uasset`, `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h/.cpp` (already modified, LFS locks held).

- [ ] **Step 1: Verify in PIE** (editor open). Set PIE to `Client` with 2 clients. Hold RMB, then press LMB during the cast. Inspect client 1's server pawn with `AbilitySystemInspectorToolset.GetActiveTags`.
  Expected: no `Cooldown.Ability.GreatFireball` tag. A full Great Fireball cast **does** add it.
- [ ] **Step 2: Commit and push, then unlock**

```bash
git add Content/Gen/Abilities/GA_Fireball.uasset Content/Gen/Abilities/GA_GreatFireball.uasset Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.cpp
git commit -m "Fireball/Great Fireball cancel each other; costs paid on release"
git push
git lfs unlock Content/Gen/Abilities/GA_Fireball.uasset
git lfs unlock Content/Gen/Abilities/GA_GreatFireball.uasset
```

---

### Task 1: Pure rules (feeding, knockback) and tests

**Files:**
- Create: `Source/Gen/AbilitySystem/GenFeeding.h`
- Create: `Source/Gen/AbilitySystem/GenKnockback.h`
- Test: `Source/Gen/Tests/GenFeedingTests.cpp`

**Interfaces:**
- Produces:
  - `GenFeeding::GetFeedLimit(int32 MaxFeed, float AvailableResource) -> int32`
  - `GenFeeding::ValidateFedCount(int32 ClientFed, int32 MaxFeed, float AvailableResource, float ElapsedFeedTime, float FeedInterval) -> int32`
  - `GenFeeding::ScaleByFeed(float AtZero, float AtMax, int32 Fed, int32 MaxFeed) -> float`
  - `GenFeeding::ReachesThreshold(int32 Fed, int32 Threshold) -> bool`
  - `GenKnockback::UpSpeed`
  - `GenKnockback::ComputeLaunchVelocity(const FVector& Direction2D, float Distance, float GravityZ) -> FVector`

- [ ] **Step 1: Write the failing tests** in `Source/Gen/Tests/GenFeedingTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenKnockback.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingLimitTest, "Gen.Feeding.FeedLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingLimitTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("5 flammes, max 5"), GenFeeding::GetFeedLimit(5, 5.f), 5);
	TestEqual(TEXT("2 flammes dispo"), GenFeeding::GetFeedLimit(5, 2.f), 2);
	TestEqual(TEXT("valeur non entière arrondie vers le bas"), GenFeeding::GetFeedLimit(5, 2.5f), 2);
	TestEqual(TEXT("aucune flamme"), GenFeeding::GetFeedLimit(5, 0.f), 0);
	TestEqual(TEXT("ressource négative"), GenFeeding::GetFeedLimit(5, -3.f), 0);
	TestEqual(TEXT("plus de ressource que le max"), GenFeeding::GetFeedLimit(5, 8.f), 5);
	TestEqual(TEXT("sort non nourrissable"), GenFeeding::GetFeedLimit(0, 5.f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingValidateTest, "Gen.Feeding.ServerValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingValidateTest::RunTest(const FString& Parameters)
{
	// ValidateFedCount(ClientFed, MaxFeed, Available, ElapsedFeedTime, FeedInterval)
	TestEqual(TEXT("annonce honnête"), GenFeeding::ValidateFedCount(5, 5, 5.f, 1.0f, 0.2f), 5);
	TestEqual(TEXT("borné par la ressource serveur"), GenFeeding::ValidateFedCount(5, 5, 3.f, 1.0f, 0.2f), 3);
	TestEqual(TEXT("borné par le temps (2 intervalles + 1 de tolérance)"), GenFeeding::ValidateFedCount(5, 5, 5.f, 0.4f, 0.2f), 3);
	TestEqual(TEXT("gigue : 0.58 s pour 3 flammes accepté"), GenFeeding::ValidateFedCount(3, 5, 5.f, 0.58f, 0.2f), 3);
	TestEqual(TEXT("annonce négative"), GenFeeding::ValidateFedCount(-1, 5, 5.f, 1.0f, 0.2f), 0);
	TestEqual(TEXT("annonce au-delà du max"), GenFeeding::ValidateFedCount(9, 5, 9.f, 5.0f, 0.2f), 5);
	TestEqual(TEXT("tap immédiat"), GenFeeding::ValidateFedCount(0, 5, 5.f, 0.0f, 0.2f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingScaleTest, "Gen.Feeding.Scaling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingScaleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 flamme"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 0, 5), 2500.f);
	TestEqual(TEXT("5 flammes"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 5, 5), 1600.f);
	TestEqual(TEXT("au-delà du max"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 10, 5), 1600.f);
	TestEqual(TEXT("MaxFeed nul"), GenFeeding::ScaleByFeed(1.f, 2.f, 3, 0), 1.f);
	TestTrue(TEXT("seuil atteint"), GenFeeding::ReachesThreshold(3, 3));
	TestFalse(TEXT("seuil non atteint"), GenFeeding::ReachesThreshold(2, 3));
	TestFalse(TEXT("seuil désactivé"), GenFeeding::ReachesThreshold(5, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenKnockbackVelocityTest, "Gen.Knockback.LaunchVelocity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenKnockbackVelocityTest::RunTest(const FString& Parameters)
{
	const float GravityZ = -980.f;
	const FVector Velocity = GenKnockback::ComputeLaunchVelocity(FVector(1.f, 0.f, 0.f), 400.f, GravityZ);
	const float AirTime = 2.f * GenKnockback::UpSpeed / 980.f;
	TestEqual(TEXT("distance parcourue en vol = 400 cm"), Velocity.X * AirTime, 400.f, 1.f);
	TestEqual(TEXT("pas de composante latérale"), Velocity.Y, 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("petit saut vertical"), Velocity.Z, GenKnockback::UpSpeed, KINDA_SMALL_NUMBER);

	const FVector Diagonal = GenKnockback::ComputeLaunchVelocity(FVector(1.f, 1.f, 5.f), 400.f, GravityZ);
	TestEqual(TEXT("direction aplatie et normalisée"), FVector(Diagonal.X, Diagonal.Y, 0.f).Size() * AirTime, 400.f, 1.f);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify the tests fail to compile** (the headers don't exist yet). Run the build command.
  Expected: `fatal error C1083: Cannot open include file: 'AbilitySystem/GenFeeding.h'`.

- [ ] **Step 3: Implement `Source/Gen/AbilitySystem/GenFeeding.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * Règles pures du "nourrissage" : un sort maintenu absorbe la ressource du champion
 * (ex : flammes de Curffe), une unité par intervalle. Sans état, testées hors monde.
 */
namespace GenFeeding
{
	/** Nombre d'unités que le sort peut absorber avec la ressource disponible. */
	inline int32 GetFeedLimit(int32 MaxFeed, float AvailableResource)
	{
		return FMath::Clamp(FMath::FloorToInt32(AvailableResource), 0, FMath::Max(MaxFeed, 0));
	}

	/**
	 * Serveur : borne le nombre annoncé par le client à la ressource du serveur et au temps
	 * qu'il a lui-même mesuré entre l'activation et la fin du nourrissage.
	 * Un intervalle de tolérance absorbe la gigue réseau.
	 */
	inline int32 ValidateFedCount(int32 ClientFed, int32 MaxFeed, float AvailableResource, float ElapsedFeedTime, float FeedInterval)
	{
		int32 Result = FMath::Clamp(ClientFed, 0, GetFeedLimit(MaxFeed, AvailableResource));
		if (FeedInterval > 0.f)
		{
			const int32 TimeLimit = FMath::FloorToInt32(FMath::Max(ElapsedFeedTime, 0.f) / FeedInterval) + 1;
			Result = FMath::Min(Result, TimeLimit);
		}
		return Result;
	}

	/** Interpolation linéaire : AtZero sans nourrissage, AtMax à MaxFeed unités (bornée). */
	inline float ScaleByFeed(float AtZero, float AtMax, int32 Fed, int32 MaxFeed)
	{
		const float Alpha = MaxFeed > 0 ? FMath::Clamp(static_cast<float>(Fed) / MaxFeed, 0.f, 1.f) : 0.f;
		return FMath::Lerp(AtZero, AtMax, Alpha);
	}

	/** Effet débloqué à partir de Threshold unités (0 = jamais). */
	inline bool ReachesThreshold(int32 Fed, int32 Threshold)
	{
		return Threshold > 0 && Fed >= Threshold;
	}
}
```

- [ ] **Step 4: Implement `Source/Gen/AbilitySystem/GenKnockback.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

/** Maths du repoussement : un petit saut balistique qui parcourt la distance voulue. */
namespace GenKnockback
{
	/** Vitesse verticale du saut (cm/s). Basse : le vol dure ~0.3 s. */
	inline constexpr float UpSpeed = 150.f;

	/** Vitesse de lancement pour parcourir Distance (cm) à plat pendant le vol. GravityZ est négatif. */
	inline FVector ComputeLaunchVelocity(const FVector& Direction2D, float Distance, float GravityZ)
	{
		const float Gravity = FMath::Abs(GravityZ);
		const float AirTime = Gravity > KINDA_SMALL_NUMBER ? 2.f * UpSpeed / Gravity : 0.3f;
		return Direction2D.GetSafeNormal2D() * (Distance / AirTime) + FVector(0.f, 0.f, UpSpeed);
	}
}
```

- [ ] **Step 5: Build, then run the unit tests.**
  Expected: 4 lines `Test Completed. Result={Success} Name={FeedLimit|ServerValidation|Scaling|LaunchVelocity}`.

- [ ] **Step 6: Commit**

```bash
git add Source/Gen/AbilitySystem/GenFeeding.h Source/Gen/AbilitySystem/GenKnockback.h Source/Gen/Tests/GenFeedingTests.cpp
git commit -m "Add pure feeding and knockback rules with automation tests"
```

---

### Task 2: Resource attribute, gain effect, tags and Curffe Hearth effects

**Files:**
- Modify: `Source/Gen/GenGameplayTags.h`, `Source/Gen/GenGameplayTags.cpp`
- Modify: `Source/Gen/AbilitySystem/GenAttributeSet.h`, `Source/Gen/AbilitySystem/GenAttributeSet.cpp`
- Create: `Source/Gen/AbilitySystem/Effects/GenGE_Gain.h/.cpp`
- Create: `Source/Gen/Champions/Curffe/CurffeTuning.h`, `Source/Gen/Champions/Curffe/CurffeEffects.h/.cpp`
- Test: `Source/Gen/Tests/GenResourceTests.cpp`

**Interfaces:**
- Produces:
  - `GenGameplayTags::SetByCaller_Energy`, `GenGameplayTags::SetByCaller_Resource`
  - `UGenAttributeSet::GetResourceAttribute()`, `GetMaxResourceAttribute()`, `GetResource()`, `GetMaxResource()`
  - `UGenGE_Gain` (instant) and `static void UGenGE_Gain::SetMagnitudes(FGameplayEffectSpec& Spec, float Energy, float Resource)`
  - `CurffeTuning::MaxFlames` (5), `CurffeTuning::FlameRegenPeriod` (3.f)
  - `UCurffeGE_HearthSetup` (infinite, MaxResource = 5), `UCurffeGE_HearthFill` (instant, Resource = 5), `UCurffeGE_HearthRegen` (infinite, +1 Resource every 3 s)

- [ ] **Step 1: Write the failing tests** in `Source/Gen/Tests/GenResourceTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Character/GenTrainingDummy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace GenResourceTests
{
	/** Monde de jeu minimal (standalone, autorité) pour tester le GAS sans PIE. */
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GenResourceTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** Fait avancer le temps du monde (timers des effets périodiques). */
		void Advance(float Seconds, float Step = 0.05f)
		{
			for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += Step)
			{
				World->Tick(LEVELTICK_All, Step);
			}
		}

		UAbilitySystemComponent* SpawnDummyASC()
		{
			AGenTrainingDummy* Dummy = World->SpawnActor<AGenTrainingDummy>();
			if (Dummy && !Dummy->HasActorBegunPlay())
			{
				Dummy->DispatchBeginPlay();
			}
			return Dummy ? Dummy->GetAbilitySystemComponent() : nullptr;
		}
	};

	float Get(UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute)
	{
		return ASC->GetNumericAttribute(Attribute);
	}

	FActiveGameplayEffectHandle ApplyClass(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass)
	{
		return ASC->ApplyGameplayEffectToSelf(EffectClass->GetDefaultObject<UGameplayEffect>(), 1.f, ASC->MakeEffectContext());
	}

	void ApplyGain(UAbilitySystemComponent* ASC, float Energy, float Resource)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGenGE_Gain::StaticClass(), 1.f, ASC->MakeEffectContext());
		UGenGE_Gain::SetMagnitudes(*Spec.Data, Energy, Resource);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

using namespace GenResourceTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResourceClampTest, "Gen.Resource.ClampedToMax",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResourceClampTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	TestEqual(TEXT("pas de ressource par défaut"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 0.f);
	ApplyGain(ASC, 0.f, 3.f);
	TestEqual(TEXT("sans max, la ressource reste à 0"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 0.f);

	ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthFill::StaticClass());
	TestEqual(TEXT("Foyer : max 5"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 5.f);
	TestEqual(TEXT("Foyer : plein à 5"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);

	ApplyGain(ASC, 0.f, 3.f);
	TestEqual(TEXT("gain au-delà du max borné"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);
	ApplyGain(ASC, 0.f, -2.f);
	TestEqual(TEXT("dépense de 2"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 3.f);
	ApplyGain(ASC, 0.f, -10.f);
	TestEqual(TEXT("dépense excessive bornée à 0"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResourceSetupRemovedTest, "Gen.Resource.DropsWhenSetupRemoved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResourceSetupRemovedTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	const FActiveGameplayEffectHandle Setup = ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthFill::StaticClass());
	ASC->RemoveActiveGameplayEffect(Setup);

	TestEqual(TEXT("max retombé à 0"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 0.f);
	TestEqual(TEXT("ressource bornée au nouveau max"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenHearthRegenTest, "Gen.Curffe.HearthRegen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenHearthRegenTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthFill::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthRegen::StaticClass());
	ApplyGain(ASC, 0.f, -2.f);
	TestEqual(TEXT("après dépense"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 3.f);

	TestWorld.Advance(CurffeTuning::FlameRegenPeriod + 0.1f);
	TestEqual(TEXT("+1 après une période"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 4.f);

	TestWorld.Advance(CurffeTuning::FlameRegenPeriod * 3.f);
	TestEqual(TEXT("jamais au-delà de 5"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenEnergyGainTest, "Gen.Resource.EnergyGain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenEnergyGainTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	TestEqual(TEXT("énergie de départ (règle des 25)"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 25.f);
	ApplyGain(ASC, 6.f, 0.f);
	TestEqual(TEXT("gain d'énergie"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 31.f);
	ApplyGain(ASC, 500.f, 0.f);
	TestEqual(TEXT("bornée à 100"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 100.f);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.**
  Expected: errors for missing `GenGE_Gain.h`, `CurffeEffects.h` and `GetResourceAttribute`.

- [ ] **Step 3: Add the tags.**
  - In `GenGameplayTags.h`, after `SetByCaller_MoveSpeedMultiplier`:

```cpp
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Energy);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Resource);
```

  - In `GenGameplayTags.cpp`, after the `SetByCaller_MoveSpeedMultiplier` definition:

```cpp
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Energy, "SetByCaller.Energy", "Energie gagnee (negatif = depensee)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Resource, "SetByCaller.Resource", "Ressource du champion gagnee (negatif = depensee), ex : flammes de Curffe");
```

- [ ] **Step 4: Add `Resource` / `MaxResource` to `GenAttributeSet.h`**, after the `MaxEnergy` block:

```cpp
	/**
	 * Ressource propre au champion (son "état" unique, cf. guidelines §5). Curffe : ses flammes.
	 * MaxResource vaut 0 pour un champion sans ressource : Resource reste alors à 0.
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Resource, Category = "Attributes|Resource")
	FGameplayAttributeData Resource;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, Resource)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxResource, Category = "Attributes|Resource")
	FGameplayAttributeData MaxResource;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, MaxResource)
```

  and in the `protected:` section, after `OnRep_MaxEnergy`:

```cpp
	UFUNCTION()
	void OnRep_Resource(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxResource(const FGameplayAttributeData& OldValue);
```

- [ ] **Step 5: Update `GenAttributeSet.cpp`.**
  - In the constructor, replace `InitEnergy(0.f);` with:

```cpp
	InitEnergy(25.f); // Chaque manche commence à 25 (guidelines §4.1)
	InitMaxEnergy(100.f);
	InitResource(0.f);
	InitMaxResource(0.f);
```

    (remove the old `InitMaxEnergy(100.f);` line so it isn't duplicated)
  - In `GetLifetimeReplicatedProps`, add:

```cpp
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, Resource, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, MaxResource, COND_None, REPNOTIFY_Always);
```

  - In `ClampAttribute`, before the `MoveSpeed` branch:

```cpp
	else if (Attribute == GetResourceAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxResource());
	}
	else if (Attribute == GetMaxResourceAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
```

  - In `PostAttributeChange`, after the `MaxEnergy` branch:

```cpp
	else if (Attribute == GetMaxResourceAttribute() && ASC && GetResource() > NewValue)
	{
		ASC->ApplyModToAttribute(GetResourceAttribute(), EGameplayModOp::Override, NewValue);
	}
```

  - At the end of the file:

```cpp
void UGenAttributeSet::OnRep_Resource(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, Resource, OldValue);
}

void UGenAttributeSet::OnRep_MaxResource(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, MaxResource, OldValue);
}
```

- [ ] **Step 6: Create `Source/Gen/AbilitySystem/Effects/GenGE_Gain.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GenGE_Gain.generated.h"

/**
 * Gain (ou dépense, en négatif) instantané d'énergie et de ressource du champion.
 * Toujours renseigner les deux magnitudes via SetMagnitudes (0 accepté), sinon le GAS
 * signale un SetByCaller manquant.
 */
UCLASS()
class GEN_API UGenGE_Gain : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGenGE_Gain();

	static void SetMagnitudes(FGameplayEffectSpec& Spec, float Energy, float Resource);
};
```

- [ ] **Step 7: Create `Source/Gen/AbilitySystem/Effects/GenGE_Gain.cpp`**

```cpp
#include "AbilitySystem/Effects/GenGE_Gain.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"

UGenGE_Gain::UGenGE_Gain()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	auto AddModifier = [this](const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::AddBase;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		Modifiers.Add(Modifier);
	};

	AddModifier(UGenAttributeSet::GetEnergyAttribute(), GenGameplayTags::SetByCaller_Energy);
	AddModifier(UGenAttributeSet::GetResourceAttribute(), GenGameplayTags::SetByCaller_Resource);
}

void UGenGE_Gain::SetMagnitudes(FGameplayEffectSpec& Spec, float Energy, float Resource)
{
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Energy, Energy);
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Resource, Resource);
}
```

- [ ] **Step 8: Create `Source/Gen/Champions/Curffe/CurffeTuning.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

/** Valeurs de départ de Curffe (spec Docs/Design/Champions/Curffe/Curffe.md, à régler en playtest). */
namespace CurffeTuning
{
	/** Flammes du Foyer. */
	inline constexpr int32 MaxFlames = 5;

	/** Une flamme regagnée passivement toutes les N secondes. */
	inline constexpr float FlameRegenPeriod = 3.f;
}
```

- [ ] **Step 9: Create `Source/Gen/Champions/Curffe/CurffeEffects.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CurffeEffects.generated.h"

/** Foyer de Curffe (infini) : MaxResource = 5 flammes. À mettre en premier dans StartupEffects. */
UCLASS()
class GEN_API UCurffeGE_HearthSetup : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCurffeGE_HearthSetup();
};

/** Remplit le Foyer (instantané). Après HearthSetup dans StartupEffects. */
UCLASS()
class GEN_API UCurffeGE_HearthFill : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCurffeGE_HearthFill();
};

/** +1 flamme toutes les FlameRegenPeriod secondes (infini, périodique ; le max borne). */
UCLASS()
class GEN_API UCurffeGE_HearthRegen : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCurffeGE_HearthRegen();
};
```

- [ ] **Step 10: Create `Source/Gen/Champions/Curffe/CurffeEffects.cpp`**

```cpp
#include "Champions/Curffe/CurffeEffects.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "Champions/Curffe/CurffeTuning.h"

UCurffeGE_HearthSetup::UCurffeGE_HearthSetup()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetMaxResourceAttribute();
	Modifier.ModifierOp = EGameplayModOp::Override;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(CurffeTuning::MaxFlames));
	Modifiers.Add(Modifier);
}

UCurffeGE_HearthFill::UCurffeGE_HearthFill()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetResourceAttribute();
	Modifier.ModifierOp = EGameplayModOp::Override;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(CurffeTuning::MaxFlames));
	Modifiers.Add(Modifier);
}

UCurffeGE_HearthRegen::UCurffeGE_HearthRegen()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(CurffeTuning::FlameRegenPeriod);
	bExecutePeriodicEffectOnApplication = false;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetResourceAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	Modifiers.Add(Modifier);
}
```

- [ ] **Step 11: Build, then run the unit tests.**
  Expected: `Gen.Resource.ClampedToMax`, `Gen.Resource.DropsWhenSetupRemoved`, `Gen.Curffe.HearthRegen` and `Gen.Resource.EnergyGain` all show `Result={Success}`, and the Task 1 tests still pass.
  - If `World->BeginPlay()` crashes without a GameMode: set the world's game mode first with `World->SetGameMode(FURL())` before `InitializeActorsForPlay`, rerun, and note it in the commit message.

- [ ] **Step 12: Commit**

```bash
git add Source/Gen/GenGameplayTags.h Source/Gen/GenGameplayTags.cpp Source/Gen/AbilitySystem/GenAttributeSet.h Source/Gen/AbilitySystem/GenAttributeSet.cpp Source/Gen/AbilitySystem/Effects/GenGE_Gain.h Source/Gen/AbilitySystem/Effects/GenGE_Gain.cpp Source/Gen/Champions/Curffe Source/Gen/Tests/GenResourceTests.cpp
git commit -m "Add champion Resource attribute, gain effect and Curffe Hearth effects"
```

---

### Task 3: Character support (resource getters, fed count, knockback, debug aim)

**Files:**
- Modify: `Source/Gen/Character/GenCharacterBase.h/.cpp`
- Modify: `Source/Gen/Character/GenTrainingDummy.cpp`
- Modify: `Source/Gen/Player/GenPlayerController.h/.cpp`

**Interfaces:**
- Consumes: `GenKnockback::ComputeLaunchVelocity` (Task 1), `UGenAttributeSet::GetResource/GetMaxResource` (Task 2).
- Produces:
  - `AGenCharacterBase::GetResource() const -> float`
  - `AGenCharacterBase::GetMaxResource() const -> float`
  - `AGenCharacterBase::GetFedResource() const -> int32`
  - `AGenCharacterBase::SetFedResource(uint8)`
  - `AGenCharacterBase::ApplyKnockback(const FVector& Direction, float Distance)` (server only)
  - `AGenPlayerController::bDebugAimOverride` (bool), `AGenPlayerController::DebugAimLocation` (FVector)

- [ ] **Step 1: In `GenCharacterBase.h`**, after `GetMaxEnergy()`:

```cpp
	UFUNCTION(BlueprintPure, Category = "Gen|Resource")
	float GetResource() const;

	UFUNCTION(BlueprintPure, Category = "Gen|Resource")
	float GetMaxResource() const;

	/** Unités de ressource en train d'être nourries dans un sort : elles quittent l'orbite à l'écran. */
	UFUNCTION(BlueprintPure, Category = "Gen|Resource")
	int32 GetFedResource() const { return FedResource; }

	/** Appelé par le sort sur le serveur et le client propriétaire (prédiction). */
	void SetFedResource(uint8 Count) { FedResource = Count; }

	/**
	 * Serveur : repousse le personnage de Distance (cm) dans Direction (aplatie à l'horizontale).
	 * Le client propriétaire reçoit le même lancement pour éviter une correction brutale.
	 */
	void ApplyKnockback(const FVector& Direction, float Distance);
```

  In the `protected:` section, after `CastInfo`:

```cpp
	/** Non répliqué au propriétaire : il le prédit lui-même. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gen|Resource")
	uint8 FedResource = 0;

	UFUNCTION(Client, Reliable)
	void ClientApplyKnockback(FVector_NetQuantize10 LaunchVelocity);
```

- [ ] **Step 2: In `GenCharacterBase.cpp`**
  - Add `#include "AbilitySystem/GenKnockback.h"`.
  - In `GetLifetimeReplicatedProps`, add:

```cpp
	DOREPLIFETIME_CONDITION(AGenCharacterBase, FedResource, COND_SkipOwner);
```

  - After `GetMaxEnergy()`:

```cpp
float AGenCharacterBase::GetResource() const
{
	return AttributeSet ? AttributeSet->GetResource() : 0.f;
}

float AGenCharacterBase::GetMaxResource() const
{
	return AttributeSet ? AttributeSet->GetMaxResource() : 0.f;
}

void AGenCharacterBase::ApplyKnockback(const FVector& Direction, float Distance)
{
	if (!HasAuthority() || bIsDead || Distance <= 0.f)
	{
		return;
	}

	FVector Direction2D = Direction.GetSafeNormal2D();
	if (Direction2D.IsNearlyZero())
	{
		Direction2D = -GetActorForwardVector().GetSafeNormal2D();
	}

	const FVector LaunchVelocity = GenKnockback::ComputeLaunchVelocity(Direction2D, Distance, GetCharacterMovement()->GetGravityZ());
	LaunchCharacter(LaunchVelocity, true, true);

	if (IsPlayerControlled() && !IsLocallyControlled())
	{
		ClientApplyKnockback(LaunchVelocity);
	}
}

void AGenCharacterBase::ClientApplyKnockback_Implementation(FVector_NetQuantize10 LaunchVelocity)
{
	LaunchCharacter(LaunchVelocity, true, true);
}
```

- [ ] **Step 3: In `GenTrainingDummy.cpp`**, in the constructor after `bRagdollOnDeath = false;`:

```cpp
	// Sans contrôleur, le CharacterMovement ne simule rien : nécessaire pour être repoussé
	GetCharacterMovement()->bRunPhysicsWithNoController = true;
```

  Add `#include "GameFramework/CharacterMovementComponent.h"`.

- [ ] **Step 4: In `GenPlayerController.h`**, in the public section after `GetCursorLocationOnPlane`:

```cpp
	/** Tests PIE : viser DebugAimLocation au lieu du curseur (ignoré en Shipping). */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Gen|Debug")
	bool bDebugAimOverride = false;

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Gen|Debug")
	FVector DebugAimLocation = FVector::ZeroVector;
```

  In `GenPlayerController.cpp`, at the top of `GetCursorLocationOnPlane`:

```cpp
#if !UE_BUILD_SHIPPING
	if (bDebugAimOverride)
	{
		OutLocation = FVector(DebugAimLocation.X, DebugAimLocation.Y, PlaneZ);
		return true;
	}
#endif
```

- [ ] **Step 5: Build.**
  Expected: `Result: Succeeded`. Run the unit tests; all of them pass.

- [ ] **Step 6: Commit**

```bash
git add Source/Gen/Character Source/Gen/Player
git commit -m "Character resource getters, fed-resource replication, knockback, debug aim"
```

---

### Task 4: Aim target data carrying the fed count

**Files:**
- Create: `Source/Gen/AbilitySystem/GenTargetData.h/.cpp`
- Modify: `Source/Gen/AbilitySystem/Tasks/GenAbilityTask_TargetDataUnderCursor.h/.cpp`

**Interfaces:**
- Produces:
  - `FGenTargetData_Aim` (derives `FGameplayAbilityTargetData_SingleTargetHit`, member `uint8 FedCount`)
  - `UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(UGameplayAbility* OwningAbility, uint8 FedCount = 0)`

- [ ] **Step 1: Create `GenTargetData.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "GenTargetData.generated.h"

/**
 * Visée envoyée par le client : point sous le curseur + nombre d'unités nourries.
 * Le serveur borne FedCount (GenFeeding::ValidateFedCount) avant de l'utiliser.
 */
USTRUCT()
struct GEN_API FGenTargetData_Aim : public FGameplayAbilityTargetData_SingleTargetHit
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 FedCount = 0;

	virtual UScriptStruct* GetScriptStruct() const override { return FGenTargetData_Aim::StaticStruct(); }

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGenTargetData_Aim> : public TStructOpsTypeTraitsBase2<FGenTargetData_Aim>
{
	enum
	{
		WithNetSerializer = true
	};
};
```

- [ ] **Step 2: Create `GenTargetData.cpp`**

```cpp
#include "AbilitySystem/GenTargetData.h"

bool FGenTargetData_Aim::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayAbilityTargetData_SingleTargetHit::NetSerialize(Ar, Map, bOutSuccess);
	Ar << FedCount;
	return true;
}
```

- [ ] **Step 3: Update the task header.** Change the factory and add the member:

```cpp
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (DisplayName = "Target Data Under Cursor", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static UGenAbilityTask_TargetDataUnderCursor* CreateTargetDataUnderCursor(UGameplayAbility* OwningAbility, uint8 FedCount = 0);
```

  and in `private:`:

```cpp
	/** Unités nourries par le sort, transmises au serveur avec la visée. */
	uint8 FedCount = 0;
```

- [ ] **Step 4: Update the task cpp.**
  - Add `#include "AbilitySystem/GenTargetData.h"`.
  - Factory:

```cpp
UGenAbilityTask_TargetDataUnderCursor* UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(UGameplayAbility* OwningAbility, uint8 FedCount)
{
	UGenAbilityTask_TargetDataUnderCursor* Task = NewAbilityTask<UGenAbilityTask_TargetDataUnderCursor>(OwningAbility);
	Task->FedCount = FedCount;
	return Task;
}
```

  - In `SendCursorData`, replace the two lines creating `FGameplayAbilityTargetData_SingleTargetHit* Data` and setting `Data->HitResult` with:

```cpp
	FGenTargetData_Aim* Data = new FGenTargetData_Aim();
	Data->HitResult = CursorHit;
	Data->FedCount = FedCount;
```

- [ ] **Step 5: Build.** Expected: `Result: Succeeded`. Unit tests still pass.
- [ ] **Step 6: Commit**

```bash
git add Source/Gen/AbilitySystem/GenTargetData.h Source/Gen/AbilitySystem/GenTargetData.cpp Source/Gen/AbilitySystem/Tasks
git commit -m "Aim target data carries the fed count"
```

---

### Task 5: Projectile shots (per-shot speed and scale, splash, knockback, instigator gains)

**Files:**
- Modify: `Source/Gen/Actors/GenProjectile.h/.cpp`

**Interfaces:**
- Consumes: `AGenCharacterBase::ApplyKnockback` (Task 3).
- Produces:
  - `struct FGenProjectileShotParams { float Speed = 0.f; float Scale = 1.f; float ExplosionRadius = 0.f; float KnockbackDistance = 0.f; }`
  - `AGenProjectile::InitializeShot(const FGenProjectileShotParams&)` (server, before `FinishSpawning`)
  - `AGenProjectile::GetSpeed() const -> float`
  - `AGenProjectile::InstigatorOnHitSpecHandle` (public `FGameplayEffectSpecHandle`)

- [ ] **Step 1: Header changes in `GenProjectile.h`**
  - Before the class, add:

```cpp
/** Réglages propres à un tir, fixés par le sort avant FinishSpawning (serveur). */
struct FGenProjectileShotParams
{
	/** 0 = vitesse de la classe. */
	float Speed = 0.f;
	float Scale = 1.f;
	/** 0 = pas d'explosion de zone. */
	float ExplosionRadius = 0.f;
	/** 0 = pas de repoussement. */
	float KnockbackDistance = 0.f;
};
```

  - In `public:`, after `DamageEffectSpecHandle`:

```cpp
	/** Gains du lanceur (énergie, ressource), appliqués s'il touche au moins un ennemi (serveur). */
	FGameplayEffectSpecHandle InstigatorOnHitSpecHandle;

	/** Serveur, avant FinishSpawning. */
	void InitializeShot(const FGenProjectileShotParams& Params);

	float GetSpeed() const { return Speed; }
```

  - Change the `Speed` property to replicate to clients at spawn:

```cpp
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Projectile")
	float Speed = 1800.f;
```

  - Add in `protected:`:

```cpp
	/** Échelle du tir (visuel + collision), répliquée à l'apparition. */
	UPROPERTY(Replicated)
	float ShotScale = 1.f;

	/** Serveur uniquement. */
	float ExplosionRadius = 0.f;
	float KnockbackDistance = 0.f;

	bool IsValidTarget(const class AGenCharacterBase* Character) const;

	/** Ennemis vivants dans le rayon, en ligne de vue depuis Origin (pas à travers les murs). */
	void AddExplosionTargets(const FVector& Origin, TArray<class AGenCharacterBase*>& InOutTargets) const;

	/** Dégâts + repoussement éventuel sur une cible. */
	void ApplyHit(class AGenCharacterBase* Target, const FVector& Origin, bool bDirectHit);
```

- [ ] **Step 2: In `GenProjectile.cpp`, replication and shot setup**
  - Add `#include "Engine/OverlapResult.h"` and `#include "CollisionQueryParams.h"`.
  - In `GetLifetimeReplicatedProps`, add:

```cpp
	DOREPLIFETIME_CONDITION(AGenProjectile, Speed, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenProjectile, ShotScale, COND_InitialOnly);
```

  - Add `InitializeShot`:

```cpp
void AGenProjectile::InitializeShot(const FGenProjectileShotParams& Params)
{
	if (Params.Speed > 0.f)
	{
		Speed = Params.Speed;
	}
	ShotScale = FMath::Max(Params.Scale, 0.1f);
	ExplosionRadius = FMath::Max(Params.ExplosionRadius, 0.f);
	KnockbackDistance = FMath::Max(Params.KnockbackDistance, 0.f);
	SetActorScale3D(FVector(ShotScale));
}
```

  - In `BeginPlay`, at the top after `Super::BeginPlay();`:

```cpp
	// Clients : l'échelle n'est pas répliquée par le mouvement, on l'applique depuis ShotScale
	SetActorScale3D(FVector(ShotScale));
```

- [ ] **Step 3: Replace `Explode` and add the helpers in `GenProjectile.cpp`**

```cpp
bool AGenProjectile::IsValidTarget(const AGenCharacterBase* Character) const
{
	return Character && !Character->IsDead() && AGenCharacterBase::AreEnemies(GetInstigator(), Character);
}

void AGenProjectile::AddExplosionTargets(const FVector& Origin, TArray<AGenCharacterBase*>& InOutTargets) const
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GenProjectileExplosion), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(ExplosionRadius), QueryParams);

	FCollisionObjectQueryParams BlockingObjects;
	BlockingObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	BlockingObjects.AddObjectTypesToQuery(ECC_WorldDynamic);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AGenCharacterBase* Character = Cast<AGenCharacterBase>(Overlap.GetActor());
		if (!Character || InOutTargets.Contains(Character) || !IsValidTarget(Character))
		{
			continue;
		}

		FCollisionQueryParams LineParams(SCENE_QUERY_STAT(GenProjectileExplosionLOS), false, this);
		LineParams.AddIgnoredActor(Character);
		if (GetWorld()->LineTraceTestByObjectType(Origin, Character->GetActorLocation(), BlockingObjects, LineParams))
		{
			continue; // un mur protège la cible
		}

		InOutTargets.Add(Character);
	}
}

void AGenProjectile::ApplyHit(AGenCharacterBase* Target, const FVector& Origin, bool bDirectHit)
{
	if (DamageEffectSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent())
		{
			const FHitResult HitResult(Target, nullptr, Origin, -GetActorForwardVector());
			DamageEffectSpecHandle.Data->GetContext().AddHitResult(HitResult, true);
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
		}
	}

	if (KnockbackDistance > 0.f)
	{
		// Coup direct : dans le sens du tir ; éclaboussure : en s'éloignant du centre de l'explosion
		const FVector Direction = bDirectHit ? GetActorForwardVector() : Target->GetActorLocation() - Origin;
		Target->ApplyKnockback(Direction, KnockbackDistance);
	}
}

void AGenProjectile::Explode(AActor* HitActor, const FVector& Location)
{
	UE_LOG(LogGenProjectile, Verbose, TEXT("%s explose sur %s en %s (%.2fs après spawn)"), *GetName(), *GetNameSafe(HitActor), *Location.ToCompactString(), GetGameTimeSinceCreation());

	// Centre de l'explosion légèrement en retrait de la surface touchée : les tests de ligne
	// de vue partent ainsi du bon côté d'un mur
	const FVector Origin = Location - GetActorForwardVector() * CollisionSphere->GetScaledSphereRadius();

	TArray<AGenCharacterBase*> Targets;
	AGenCharacterBase* DirectTarget = Cast<AGenCharacterBase>(HitActor);
	if (IsValidTarget(DirectTarget))
	{
		Targets.Add(DirectTarget);
	}
	if (ExplosionRadius > 0.f && HitActor)
	{
		AddExplosionTargets(Origin, Targets);
	}

	for (AGenCharacterBase* Target : Targets)
	{
		ApplyHit(Target, Origin, Target == DirectTarget);
	}

	UE_LOG(LogGenProjectile, Verbose, TEXT("%s : %d cible(s) touchée(s)"), *GetName(), Targets.Num());

	if (Targets.Num() > 0 && InstigatorOnHitSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* InstigatorASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetInstigator()))
		{
			InstigatorASC->ApplyGameplayEffectSpecToSelf(*InstigatorOnHitSpecHandle.Data.Get());
		}
	}

	ImpactLocation = Location;
	bExploded = true;
	OnRep_Exploded(); // Les RepNotify ne s'exécutent pas sur le serveur : appel manuel (listen server)

	ProjectileMovement->StopMovementImmediately();
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Laisse le temps à bExploded d'être répliqué avant la destruction
	SetLifeSpan(0.5f);
}
```

  Note: `HitActor == nullptr` means the range ran out, so there is no splash (spec: "explodes on impact"). Hitting a wall *is* an impact, so the splash applies.

- [ ] **Step 4: Build.** Expected: `Result: Succeeded`. Unit tests still pass.
- [ ] **Step 5: Commit**

```bash
git add Source/Gen/Actors
git commit -m "Projectile shots: per-shot speed/scale, splash with line of sight, knockback, instigator gains"
```

---

### Task 6: Feeding in `UGenGA_Projectile`, and auto-repeat that doesn't interrupt casts

**Files:**
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h/.cpp` (full rewrite below)
- Modify: `Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp`

**Interfaces:**
- Consumes: Tasks 1–5 (`GenFeeding`, `UGenGE_Gain`, `FGenTargetData_Aim`, `CreateTargetDataUnderCursor(this, FedCount)`, `FGenProjectileShotParams`, `InitializeShot`, `InstigatorOnHitSpecHandle`, `SetFedResource`).
- Produces (editable in Blueprints):
  - `bFeedable`, `FeedInterval`, `MaxFeed`, `DamagePerFeed`, `EnergyPerFeed`
  - `SpeedMultiplierAtMaxFeed`, `ScaleAtMaxFeed`
  - `ExplosionMinFeed`, `ExplosionRadius`, `KnockbackMinFeed`, `KnockbackDistance`
  - `EnergyOnHit`, `ResourceOnHit`
  - `SpawnProjectile(const FVector& TargetLocation, int32 Fed = 0)`

- [ ] **Step 1: Replace `GenGA_Projectile.h`** with:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "GenGA_Projectile.generated.h"

class AGenProjectile;
class UAbilityTask_WaitDelay;
class UAbilityTask_WaitInputRelease;
class UAnimMontage;
class UGameplayEffect;
class UNiagaraSystem;
struct FGameplayAbilityTargetData;

/**
 * Sort de projectile tiré vers le curseur (ex: boule de feu).
 *
 * Déroulé :
 *  0. Si bFeedable : nourrissage. Tant que la touche est maintenue, une unité de ressource
 *     (flammes de Curffe) passe dans le sort toutes les FeedInterval s, jusqu'à MaxFeed.
 *     Relâcher, atteindre le max ou épuiser la ressource enchaîne sur l'incantation.
 *     Le client décide du nombre ; le serveur le borne à sa ressource et au temps mesuré.
 *  1. Si CastTime > 0 : incantation (barre de cast, ralenti), annulée si le lanceur est étourdi ou meurt
 *     (ou par un autre sort via CancelAbilitiesWithTag)
 *  2. Le client récupère le point visé sous la souris et l'envoie au serveur (target data,
 *     avec le nombre d'unités nourries) => on vise à la FIN de l'incantation
 *  3. CommitAbility + dépense de la ressource nourrie au lancer : une incantation interrompue ne coûte rien.
 *  4. Le personnage se tourne vers la cible, joue un montage optionnel
 *  5. Le serveur fait apparaître le projectile répliqué, mis à l'échelle par le nourrissage
 *     (dégâts, vitesse, taille, explosion de zone, repoussement), porteur du GE de dégâts
 *     et des gains du lanceur (énergie, ressource) s'il touche.
 */
UCLASS()
class GEN_API UGenGA_Projectile : public UGenGameplayAbility
{
	GENERATED_BODY()

public:
	UGenGA_Projectile();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Fin de l'incantation (ou tout de suite si CastTime = 0) : on récupère la visée. */
	UFUNCTION()
	void OnCastFinished();

	/** Étourdi pendant le nourrissage ou l'incantation. */
	UFUNCTION()
	void OnCastInterrupted();

	UFUNCTION()
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle);

	UFUNCTION()
	void OnFeedTick();

	UFUNCTION()
	void OnFeedInputReleased(float TimeHeld);

	/** Fin du nourrissage, synchronisée client -> serveur. */
	UFUNCTION()
	void OnFeedSynced();

	/** Serveur uniquement : fait apparaître le projectile en direction de TargetLocation. */
	UFUNCTION(BlueprintCallable, Category = "Gen|Projectile")
	void SpawnProjectile(const FVector& TargetLocation, int32 Fed = 0);

	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<AGenProjectile> ProjectileClass;

	/** GE appliqué à la cible touchée. Par défaut : UGenGE_Damage (SetByCaller.Damage). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** Dégâts infligés (peut varier selon le niveau du sort). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	FScalableFloat Damage;

	/** Distance devant le lanceur où apparaît le projectile. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	float SpawnForwardOffset = 70.f;

	/** Durée d'incantation en secondes (après le nourrissage). 0 = sort instantané. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (ClampMin = "0.0", Units = "s"))
	float CastTime = 0.f;

	/** Vitesse de déplacement pendant le nourrissage et l'incantation (1 = pas de ralenti, 0 = immobile). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CastMoveSpeedMultiplier = 0.5f;

	/** Effet sur le lanceur pendant l'incantation (vu par tous) : annonce le sort et sa direction. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast")
	TObjectPtr<UNiagaraSystem> CastFX;

	/** Socket du mesh où attacher CastFX. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast")
	FName CastFXSocket = TEXT("hand_r");

	/**
	 * Montage d'incantation (optionnel, répliqué par le GAS) : joué dès le début de l'incantation,
	 * préparation puis geste de lancer. Le régler pour que le lancer tombe à CastTime.
	 * Coupé si l'incantation est interrompue. Ignoré si CastTime = 0 (utiliser CastMontage).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Animation", meta = (EditCondition = "CastTime > 0"))
	TObjectPtr<UAnimMontage> ChargeMontage;

	/** Montage de lancer (optionnel, répliqué aux autres joueurs par le GAS). Joué à la fin de l'incantation. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Animation")
	TObjectPtr<UAnimMontage> CastMontage;

	/** Maintenir la touche nourrit le sort avec la ressource du champion (attribut Resource). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding")
	bool bFeedable = false;

	/** Une unité absorbée toutes les FeedInterval secondes. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.05", Units = "s"))
	float FeedInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "1"))
	int32 MaxFeed = 5;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable"))
	float DamagePerFeed = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable"))
	float EnergyPerFeed = 0.f;

	/** Vitesse du projectile à MaxFeed, en multiple de sa vitesse de base. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.1"))
	float SpeedMultiplierAtMaxFeed = 1.f;

	/** Taille du projectile (visuel + collision) à MaxFeed. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.1"))
	float ScaleAtMaxFeed = 1.f;

	/** Explosion de zone à partir de N unités nourries (0 = jamais). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0"))
	int32 ExplosionMinFeed = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", Units = "cm"))
	float ExplosionRadius = 150.f;

	/** Repoussement à partir de N unités nourries (0 = jamais). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0"))
	int32 KnockbackMinFeed = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", Units = "cm"))
	float KnockbackDistance = 400.f;

	/** Énergie gagnée par le lanceur si le projectile touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Gains")
	float EnergyOnHit = 0.f;

	/** Ressource (flammes...) gagnée par le lanceur si le projectile touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Gains")
	float ResourceOnHit = 0.f;

private:
	void StartFeeding();
	void ScheduleFeedTick();
	/** Client (ou hôte) : fin du nourrissage => prévient le serveur puis incante. */
	void StopFeedingLocal();
	void EndFeedTasks();
	void SetFedVisual(int32 Count);
	int32 GetAvailableFeed() const;

	void StartCasting();
	void ApplyCastSlow();
	void StartInterruptWatch();
	/** Retire le ralenti et la barre de cast (garde le visuel des unités nourries). */
	void EndCastPresentation();
	/** Nettoyage complet (fin ou annulation du sort). */
	void StopCasting();

	/** Nombre d'unités nourries retenu pour ce tir (borné côté serveur). */
	int32 ResolveFedCount(const FGameplayAbilityTargetData* Data) const;
	void SpendResource(int32 Amount);

	FActiveGameplayEffectHandle CastSlowHandle;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitDelay> FeedTickTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputRelease> FeedReleaseTask;

	int32 FedCount = 0;
	int32 FedVisualCount = 0;
	bool bIsFeeding = false;
	bool bInterruptWatchStarted = false;
	float FeedStartTime = 0.f;
	/** Serveur : durée du nourrissage mesurée entre l'activation et le signal du client. */
	float ServerFeedElapsed = 0.f;
};
```

- [ ] **Step 2: Replace `GenGA_Projectile.cpp`** with:

```cpp
#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "Abilities/Tasks/AbilityTask_NetworkSyncPoint.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenTargetData.h"
#include "AbilitySystem/Tasks/GenAbilityTask_TargetDataUnderCursor.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenProjectileAbility, Log, All);

#define GEN_ABILITY_LOG(Verbosity, Format, ...) UE_LOG(LogGenProjectileAbility, Verbosity, TEXT("[%s] %s: " Format), (CurrentActorInfo && CurrentActorInfo->IsNetAuthority()) ? TEXT("SERVEUR") : TEXT("CLIENT"), *GetName(), ##__VA_ARGS__)

UGenGA_Projectile::UGenGA_Projectile()
{
	ProjectileClass = AGenProjectile::StaticClass();
	DamageEffectClass = UGenGE_Damage::StaticClass();
	Damage = FScalableFloat(20.f);

	ActivationOwnedTags.AddTag(GenGameplayTags::State_Casting);
}

void UGenGA_Projectile::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Pas de CommitAbility ici : le cooldown et le coût ne sont appliqués qu'au lancer
	// (OnTargetDataReady). CanActivateAbility les a déjà vérifiés avant l'activation.
	GEN_ABILITY_LOG(Verbose, "Activé (clé %s), incantation %.2fs%s", *ActivationInfo.GetActivationPredictionKey().ToString(), CastTime, bFeedable ? TEXT(", nourrissable") : TEXT(""));

	FedCount = 0;
	FedVisualCount = 0;
	ServerFeedElapsed = 0.f;
	bInterruptWatchStarted = false;

	if (bFeedable)
	{
		StartFeeding();
	}
	else if (CastTime > 0.f)
	{
		StartCasting();
	}
	else
	{
		OnCastFinished();
	}
}

void UGenGA_Projectile::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (IsActive())
	{
		GEN_ABILITY_LOG(Verbose, "Fin (annulé=%d, répliqué=%d)", bWasCancelled, bReplicateEndAbility);
	}

	// Annulation en plein nourrissage ou incantation (étourdi, mort, autre sort...) : on nettoie.
	// La ressource nourrie n'est dépensée qu'au lancer : rien à rendre.
	StopCasting();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

int32 UGenGA_Projectile::GetAvailableFeed() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;
	return GenFeeding::GetFeedLimit(MaxFeed, Available);
}

void UGenGA_Projectile::StartFeeding()
{
	bIsFeeding = true;
	FeedStartTime = GetWorld()->GetTimeSeconds();

	ApplyCastSlow();
	StartInterruptWatch();

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		// Barre de cast : durée maximale possible, remplacée par la vraie incantation à la fin du nourrissage
		Character->StartCast(GetClass(), GetAvailableFeed() * FeedInterval + CastTime, CastFX, CastFXSocket);
	}

	if (IsLocallyControlled())
	{
		if (GetAvailableFeed() == 0)
		{
			StopFeedingLocal(); // rien à nourrir : on incante directement
			return;
		}

		FeedReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, /*bTestAlreadyReleased*/ true);
		FeedReleaseTask->OnRelease.AddDynamic(this, &ThisClass::OnFeedInputReleased);
		FeedReleaseTask->ReadyForActivation();

		// Un simple appui (déjà relâché) a pu terminer le nourrissage pendant ReadyForActivation
		if (bIsFeeding)
		{
			ScheduleFeedTick();
		}
	}
	else
	{
		// Serveur pour un client distant : c'est le client qui annonce la fin du nourrissage
		UAbilityTask_NetworkSyncPoint* SyncTask = UAbilityTask_NetworkSyncPoint::WaitNetSync(this, EAbilityTaskNetSyncType::OnlyServerWait);
		SyncTask->OnSync.AddDynamic(this, &ThisClass::OnFeedSynced);
		SyncTask->ReadyForActivation();

		// Estimation cosmétique pour les autres joueurs (les flammes quittent l'orbite)
		ScheduleFeedTick();
	}
}

void UGenGA_Projectile::ScheduleFeedTick()
{
	FeedTickTask = UAbilityTask_WaitDelay::WaitDelay(this, FeedInterval);
	FeedTickTask->OnFinish.AddDynamic(this, &ThisClass::OnFeedTick);
	FeedTickTask->ReadyForActivation();
}

void UGenGA_Projectile::OnFeedTick()
{
	if (!bIsFeeding)
	{
		return;
	}

	const int32 Limit = GetAvailableFeed();
	if (FedCount < Limit)
	{
		++FedCount;
		SetFedVisual(FedCount);
	}

	if (FedCount >= Limit)
	{
		if (IsLocallyControlled())
		{
			StopFeedingLocal(); // plus rien à absorber : l'incantation enchaîne même si la touche reste enfoncée
		}
		return; // serveur : attend le signal du client
	}

	ScheduleFeedTick();
}

void UGenGA_Projectile::OnFeedInputReleased(float TimeHeld)
{
	if (bIsFeeding)
	{
		StopFeedingLocal();
	}
}

void UGenGA_Projectile::StopFeedingLocal()
{
	if (!bIsFeeding)
	{
		return;
	}

	EndFeedTasks();

	// Client : envoie le signal au serveur et continue sans attendre. Hôte : se termine aussitôt.
	UAbilityTask_NetworkSyncPoint* SyncTask = UAbilityTask_NetworkSyncPoint::WaitNetSync(this, EAbilityTaskNetSyncType::OnlyServerWait);
	SyncTask->OnSync.AddDynamic(this, &ThisClass::OnFeedSynced);
	SyncTask->ReadyForActivation();
}

void UGenGA_Projectile::OnFeedSynced()
{
	if (!bIsFeeding)
	{
		return;
	}

	bIsFeeding = false;
	EndFeedTasks();

	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		ServerFeedElapsed = GetWorld()->GetTimeSeconds() - FeedStartTime;
	}

	GEN_ABILITY_LOG(Verbose, "Nourrissage terminé : %d (%.2fs)", FedCount, GetWorld()->GetTimeSeconds() - FeedStartTime);

	if (CastTime > 0.f)
	{
		StartCasting();
	}
	else
	{
		OnCastFinished();
	}
}

void UGenGA_Projectile::EndFeedTasks()
{
	if (FeedTickTask)
	{
		FeedTickTask->EndTask();
		FeedTickTask = nullptr;
	}
	if (FeedReleaseTask)
	{
		FeedReleaseTask->EndTask();
		FeedReleaseTask = nullptr;
	}
}

void UGenGA_Projectile::SetFedVisual(int32 Count)
{
	FedVisualCount = Count;
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->SetFedResource(static_cast<uint8>(FMath::Clamp(Count, 0, 255)));
	}
}

void UGenGA_Projectile::StartCasting()
{
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StartCast(GetClass(), CastTime, CastFX, CastFXSocket);
	}

	ApplyCastSlow(); // sans effet s'il est déjà actif depuis le nourrissage
	StartInterruptWatch();

	// Le geste continue après la fin normale du sort (le lancer tombe à la fin de l'incantation).
	// Une annulation (étourdi, mort) le coupe quand même : la tâche écoute OnGameplayAbilityCancelled.
	if (ChargeMontage)
	{
		UAbilityTask_PlayMontageAndWait* ChargeTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, ChargeMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false);
		ChargeTask->ReadyForActivation();
	}

	UAbilityTask_WaitDelay* CastTask = UAbilityTask_WaitDelay::WaitDelay(this, CastTime);
	CastTask->OnFinish.AddDynamic(this, &ThisClass::OnCastFinished);
	CastTask->ReadyForActivation();
}

void UGenGA_Projectile::ApplyCastSlow()
{
	// Ralenti appliqué dans la fenêtre de prédiction de l'activation
	if (CastSlowHandle.IsValid() || CastMoveSpeedMultiplier >= 1.f)
	{
		return;
	}

	FGameplayEffectSpecHandle SlowSpec = MakeOutgoingGameplayEffectSpec(UGenGE_MoveSpeedMultiplier::StaticClass(), GetAbilityLevel());
	if (SlowSpec.IsValid())
	{
		SlowSpec.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_MoveSpeedMultiplier, CastMoveSpeedMultiplier);
		CastSlowHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, SlowSpec);
	}
}

void UGenGA_Projectile::StartInterruptWatch()
{
	if (bInterruptWatchStarted)
	{
		return;
	}
	bInterruptWatchStarted = true;

	// Un étourdissement interrompt le nourrissage et l'incantation (la mort annule déjà tous les sorts)
	UAbilityTask_WaitGameplayTagAdded* StunTask = UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(this, GenGameplayTags::State_Stunned, nullptr, true);
	StunTask->Added.AddDynamic(this, &ThisClass::OnCastInterrupted);
	StunTask->ReadyForActivation();
}

void UGenGA_Projectile::EndCastPresentation()
{
	if (CastSlowHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(CastSlowHandle);
		CastSlowHandle.Invalidate();
	}

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StopCast(GetClass()); // sans effet si un autre sort a pris la barre
	}
}

void UGenGA_Projectile::StopCasting()
{
	bIsFeeding = false;
	EndFeedTasks();

	if (FedVisualCount > 0)
	{
		SetFedVisual(0);
	}

	EndCastPresentation();
}

void UGenGA_Projectile::OnCastInterrupted()
{
	GEN_ABILITY_LOG(Verbose, "Incantation interrompue (étourdi)");
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void UGenGA_Projectile::OnCastFinished()
{
	GEN_ABILITY_LOG(Verbose, "Incantation terminée, attente de la visée (nourri : %d)", FedCount);
	EndCastPresentation();

	// Visée lue maintenant : le joueur peut ajuster pendant toute l'incantation
	UGenAbilityTask_TargetDataUnderCursor* TargetTask = UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(this, static_cast<uint8>(FMath::Clamp(FedCount, 0, 255)));
	TargetTask->ValidData.AddDynamic(this, &ThisClass::OnTargetDataReady);
	TargetTask->ReadyForActivation();
}

int32 UGenGA_Projectile::ResolveFedCount(const FGameplayAbilityTargetData* Data) const
{
	if (!bFeedable)
	{
		return 0;
	}

	const FGenTargetData_Aim* AimData = (Data && Data->GetScriptStruct() == FGenTargetData_Aim::StaticStruct())
		? static_cast<const FGenTargetData_Aim*>(Data)
		: nullptr;
	const int32 Reported = AimData ? AimData->FedCount : 0;

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;

	// Serveur pour un client distant : le client ne peut annoncer ni plus que ce qu'il a, ni plus que le temps écoulé
	if (CurrentActorInfo->IsNetAuthority() && !IsLocallyControlled())
	{
		const int32 Validated = GenFeeding::ValidateFedCount(Reported, MaxFeed, Available, ServerFeedElapsed, FeedInterval);
		if (Validated != Reported)
		{
			GEN_ABILITY_LOG(Warning, "Nourrissage corrigé par le serveur : %d -> %d (ressource %.0f, %.2fs)", Reported, Validated, Available, ServerFeedElapsed);
		}
		return Validated;
	}

	return FMath::Min(Reported, GenFeeding::GetFeedLimit(MaxFeed, Available));
}

void UGenGA_Projectile::SpendResource(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UGenGE_Gain::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		UGenGE_Gain::SetMagnitudes(*Spec.Data, 0.f, -static_cast<float>(Amount));
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, Spec);
	}
}

void UGenGA_Projectile::OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const FGameplayAbilityTargetData* Data = DataHandle.Get(0);
	const FHitResult* Hit = Data ? Data->GetHitResult() : nullptr;

	if (!Avatar || !Hit)
	{
		GEN_ABILITY_LOG(Warning, "Visée invalide (avatar=%d, hit=%d)", Avatar != nullptr, Hit != nullptr);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const int32 Fed = ResolveFedCount(Data);

	// Le sort part : on applique cooldown, coût et dépense des flammes maintenant, pour qu'une
	// incantation interrompue (annulée, étourdi, mort) ne coûte rien. Le client est dans la fenêtre
	// de prédiction ouverte par la tâche de visée, le serveur dans celle de la clé reçue.
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		GEN_ABILITY_LOG(Verbose, "CommitAbility a échoué au lancer");
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	SpendResource(Fed);
	SetFedVisual(0);

	GEN_ABILITY_LOG(Verbose, "Visée reçue : %s, nourri : %d", *Hit->Location.ToCompactString(), Fed);

	// Se tourner vers la cible (client et serveur, pour que la prédiction concorde)
	FVector Direction = (Hit->Location - Avatar->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = Avatar->GetActorForwardVector();
	}
	Avatar->SetActorRotation(Direction.Rotation());

	if (CastMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, CastMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false);
		MontageTask->ReadyForActivation();
	}

	SpawnProjectile(Avatar->GetActorLocation() + Direction * 1000.f, Fed);

	// Le client ne réplique PAS la fin du sort : son incantation finit avant celle du serveur
	// (qui a démarré plus tard), et un EndAbility répliqué tuerait le sort côté serveur avant
	// qu'il ait fait apparaître le projectile. C'est le serveur qui termine et prévient le client.
	const bool bReplicateEnd = CurrentActorInfo && CurrentActorInfo->IsNetAuthority();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicateEnd, false);
}

void UGenGA_Projectile::SpawnProjectile(const FVector& TargetLocation, int32 Fed)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || !ProjectileClass)
	{
		return;
	}

	const FVector Origin = Avatar->GetActorLocation();
	FVector Direction = (TargetLocation - Origin).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = Avatar->GetActorForwardVector();
	}

	const FTransform SpawnTransform(Direction.Rotation(), Origin + Direction * SpawnForwardOffset);

	AGenProjectile* Projectile = GetWorld()->SpawnActorDeferred<AGenProjectile>(
		ProjectileClass, SpawnTransform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Projectile)
	{
		GEN_ABILITY_LOG(Warning, "Échec du spawn de %s", *GetNameSafe(ProjectileClass));
		return;
	}

	const float ClassSpeed = ProjectileClass->GetDefaultObject<AGenProjectile>()->GetSpeed();
	FGenProjectileShotParams ShotParams;
	ShotParams.Speed = GenFeeding::ScaleByFeed(ClassSpeed, ClassSpeed * SpeedMultiplierAtMaxFeed, Fed, MaxFeed);
	ShotParams.Scale = GenFeeding::ScaleByFeed(1.f, ScaleAtMaxFeed, Fed, MaxFeed);
	ShotParams.ExplosionRadius = GenFeeding::ReachesThreshold(Fed, ExplosionMinFeed) ? ExplosionRadius : 0.f;
	ShotParams.KnockbackDistance = GenFeeding::ReachesThreshold(Fed, KnockbackMinFeed) ? KnockbackDistance : 0.f;
	Projectile->InitializeShot(ShotParams);

	GEN_ABILITY_LOG(Verbose, "Projectile %s créé en %s (nourri %d, vitesse %.0f, échelle %.2f, zone %.0f, repoussement %.0f)",
		*Projectile->GetName(), *SpawnTransform.GetLocation().ToCompactString(), Fed, ShotParams.Speed, ShotParams.Scale, ShotParams.ExplosionRadius, ShotParams.KnockbackDistance);

	const int32 Level = GetAbilityLevel();

	if (DamageEffectClass)
	{
		FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, Level);
		if (SpecHandle.IsValid())
		{
			SpecHandle.Data->GetContext().AddSourceObject(Projectile);
			SpecHandle.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Damage, Damage.GetValueAtLevel(Level) + DamagePerFeed * Fed);
			Projectile->DamageEffectSpecHandle = SpecHandle;
		}
	}

	const float EnergyGain = EnergyOnHit + EnergyPerFeed * Fed;
	if (EnergyGain > 0.f || ResourceOnHit > 0.f)
	{
		FGameplayEffectSpecHandle GainSpec = MakeOutgoingGameplayEffectSpec(UGenGE_Gain::StaticClass(), Level);
		if (GainSpec.IsValid())
		{
			UGenGE_Gain::SetMagnitudes(*GainSpec.Data, EnergyGain, ResourceOnHit);
			Projectile->InstigatorOnHitSpecHandle = GainSpec;
		}
	}

	Projectile->FinishSpawning(SpawnTransform);
}
```

- [ ] **Step 3: In `GenAbilitySystemComponent.cpp`**, in `ProcessAbilityInput`, change the held-input condition so auto-repeat never interrupts another cast (a fresh press still does):

```cpp
	// Sorts "WhileInputActive" : se relancent tant que la touche est maintenue (ex: M1 en auto).
	// La répétition automatique n'interrompt jamais une autre incantation (State.Casting) ;
	// seul un nouvel appui le fait (via CancelAbilitiesWithTag).
	const bool bCastingOtherAbility = HasMatchingGameplayTag(GenGameplayTags::State_Casting);
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputHeldSpecHandles)
	{
		if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(SpecHandle))
		{
			const UGenGameplayAbility* AbilityCDO = Cast<UGenGameplayAbility>(Spec->Ability);
			if (AbilityCDO && !Spec->IsActive() && !bCastingOtherAbility && AbilityCDO->ActivationPolicy == EGenAbilityActivationPolicy::WhileInputActive)
			{
				AbilitiesToActivate.AddUnique(Spec->Handle);
			}
		}
	}
```

- [ ] **Step 4: Build.** Expected: `Result: Succeeded`. Run the unit tests; all pass.
- [ ] **Step 5: Commit**

```bash
git add Source/Gen/AbilitySystem/Abilities Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp
git commit -m "Hold-to-feed projectiles with server validation; auto-repeat never interrupts a cast"
```

- [ ] **Step 6 (verified in Task 9, V5):** holding LMB, then pressing RMB, casts the Great Fireball, and the next LMB auto-repeat does not cancel it.

---

### Task 7: Hearth orbit and HUD

**Files:**
- Create: `Source/Gen/Champions/Curffe/CurffeHearthComponent.h/.cpp`
- Modify: `Source/Gen/UI/GenHUD.cpp`

**Interfaces:**
- Consumes: `AGenCharacterBase::GetResource/GetFedResource/IsDead` (Task 3), `CurffeTuning::MaxFlames`.
- Produces:
  - `UCurffeHearthComponent` (BlueprintSpawnable, editable `FlameMesh`, `FlameMaterial`, `OrbitRadius`, `OrbitHeight`, `OrbitSpeedDegrees`, `FlameScale`)
  - `UCurffeHearthComponent::GetVisibleFlameCount() -> int32`

- [ ] **Step 1: Create `CurffeHearthComponent.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CurffeHearthComponent.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Le Foyer de Curffe : ses flammes (attribut Resource) tournent autour de lui, visibles par tous.
 * Les flammes en cours de nourrissage (GetFedResource) quittent l'orbite.
 * Purement cosmétique : rien sur un serveur dédié.
 */
UCLASS(ClassGroup = (Gen), meta = (BlueprintSpawnableComponent))
class GEN_API UCurffeHearthComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCurffeHearthComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Flammes affichées dans l'orbite (lu par les tests PIE). */
	UFUNCTION(BlueprintPure, Category = "Curffe|Hearth")
	int32 GetVisibleFlameCount() const { return VisibleFlames; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	TObjectPtr<UStaticMesh> FlameMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	TObjectPtr<UMaterialInterface> FlameMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "cm"))
	float OrbitRadius = 70.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "cm"))
	float OrbitHeight = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	float OrbitSpeedDegrees = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	float FlameScale = 0.18f;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FlameComponents;

	float OrbitAngle = 0.f;
	int32 VisibleFlames = 0;
};
```

- [ ] **Step 2: Create `CurffeHearthComponent.cpp`**

```cpp
#include "Champions/Curffe/CurffeHearthComponent.h"

#include "Champions/Curffe/CurffeTuning.h"
#include "Character/GenCharacterBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

UCurffeHearthComponent::UCurffeHearthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// L'orbite ne tourne pas avec le personnage quand il se retourne
	SetUsingAbsoluteRotation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	FlameMesh = SphereMesh.Object;
}

void UCurffeHearthComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}

	for (int32 Index = 0; Index < CurffeTuning::MaxFlames; ++Index)
	{
		UStaticMeshComponent* Flame = NewObject<UStaticMeshComponent>(GetOwner());
		Flame->SetStaticMesh(FlameMesh);
		if (FlameMaterial)
		{
			Flame->SetMaterial(0, FlameMaterial);
		}
		Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flame->SetCastShadow(false);
		Flame->SetupAttachment(this);
		Flame->SetRelativeScale3D(FVector(FlameScale));
		Flame->SetVisibility(false);
		Flame->RegisterComponent();
		FlameComponents.Add(Flame);
	}
}

void UCurffeHearthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AGenCharacterBase* Character = Cast<AGenCharacterBase>(GetOwner());
	VisibleFlames = (Character && !Character->IsDead())
		? FMath::Clamp(FMath::FloorToInt32(Character->GetResource()) - Character->GetFedResource(), 0, FlameComponents.Num())
		: 0;

	OrbitAngle = FMath::Fmod(OrbitAngle + OrbitSpeedDegrees * DeltaTime, 360.f);

	for (int32 Index = 0; Index < FlameComponents.Num(); ++Index)
	{
		UStaticMeshComponent* Flame = FlameComponents[Index];
		const bool bVisible = Index < VisibleFlames;
		Flame->SetVisibility(bVisible);
		if (bVisible)
		{
			const float Angle = FMath::DegreesToRadians(OrbitAngle + 360.f * Index / VisibleFlames);
			Flame->SetRelativeLocation(FVector(FMath::Cos(Angle) * OrbitRadius, FMath::Sin(Angle) * OrbitRadius, OrbitHeight));
		}
	}
}
```

- [ ] **Step 3: In `GenHUD.cpp`, `DrawLocalPlayerPanel`**, after the energy bar block (`Y += 16.f;`):

```cpp
	// Ressource du champion (Curffe : flammes du Foyer)
	const float MaxResource = LocalCharacter->GetMaxResource();
	if (MaxResource > 0.f)
	{
		const FString ResourceText = FString::Printf(TEXT("Flammes : %.0f / %.0f"), LocalCharacter->GetResource(), MaxResource);
		DrawText(ResourceText, FLinearColor(1.f, 0.6f, 0.2f), X, Y, Font);
		Y += 22.f;
	}
```

- [ ] **Step 4: Build.** Expected: `Result: Succeeded`.
- [ ] **Step 5: Commit**

```bash
git add Source/Gen/Champions/Curffe/CurffeHearthComponent.h Source/Gen/Champions/Curffe/CurffeHearthComponent.cpp Source/Gen/UI/GenHUD.cpp
git commit -m "Curffe Hearth orbit component and flame count on the HUD"
```

---

### Task 8: Curffe assets (editor): folder, BP_Curffe, ability configuration

Needs the editor open with the new binaries. Load VibeUE skills `VibeUE_blueprints` and `VibeUE_gas` first.

**Files (assets):**
- Move into `/Game/Gen/Champions/Curffe/`:
  - `Abilities/`: `GA_Fireball`, `GA_GreatFireball`, `GA_FlameLeap`, and the `FlameLeap/` cue folder
  - `Projectiles/`: `BP_Projectile_Fireball`, `BP_Projectile_GreatFireball`
  - `Animations/`: `AM_Fireball`, `AM_GreatFireball`, `AM_FlameLeap`, `AM_FlameLeap_Land`, `AS_Fireball_Cast`, `AS_GreatFireball_Cast`
- Create: `/Game/Gen/Champions/Curffe/BP_Curffe` (child of `BP_Champion`)
- Modify: `/Game/Gen/Core/BP_GenGameMode` (default pawn)
- The VFX look-dev folders (`Content/Gen/VFX/*`) stay where they are. `Docs/ArtBible.md` §7.7 refers to them by path.

- [ ] **Step 1: Lock the assets.**

```bash
git pull --ff-only
git lfs lock Content/Gen/Abilities/GA_FlameLeap.uasset
git lfs lock Content/Gen/Abilities/BP_Projectile_Fireball.uasset
git lfs lock Content/Gen/Abilities/BP_Projectile_GreatFireball.uasset
git lfs lock Content/Gen/Core/BP_GenGameMode.uasset
for f in Content/Gen/Abilities/FlameLeap/*.uasset Content/Gen/Animations/AM_Fireball.uasset Content/Gen/Animations/AM_GreatFireball.uasset Content/Gen/Animations/AM_FlameLeap.uasset Content/Gen/Animations/AM_FlameLeap_Land.uasset Content/Gen/Animations/AS_Fireball_Cast.uasset Content/Gen/Animations/AS_GreatFireball_Cast.uasset; do git lfs lock "$f"; done
```

  (`GA_Fireball` and `GA_GreatFireball` were locked in Task 0. Lock them again if Task 0 unlocked them.)

- [ ] **Step 2: Move the assets** (`execute_python_code`, `auto_save: false`):

```python
import unreal
moves = {
    "/Game/Gen/Abilities/GA_Fireball": "/Game/Gen/Champions/Curffe/Abilities/GA_Fireball",
    "/Game/Gen/Abilities/GA_GreatFireball": "/Game/Gen/Champions/Curffe/Abilities/GA_GreatFireball",
    "/Game/Gen/Abilities/GA_FlameLeap": "/Game/Gen/Champions/Curffe/Abilities/GA_FlameLeap",
    "/Game/Gen/Abilities/BP_Projectile_Fireball": "/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_Fireball",
    "/Game/Gen/Abilities/BP_Projectile_GreatFireball": "/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_GreatFireball",
    "/Game/Gen/Animations/AM_Fireball": "/Game/Gen/Champions/Curffe/Animations/AM_Fireball",
    "/Game/Gen/Animations/AM_GreatFireball": "/Game/Gen/Champions/Curffe/Animations/AM_GreatFireball",
    "/Game/Gen/Animations/AM_FlameLeap": "/Game/Gen/Champions/Curffe/Animations/AM_FlameLeap",
    "/Game/Gen/Animations/AM_FlameLeap_Land": "/Game/Gen/Champions/Curffe/Animations/AM_FlameLeap_Land",
    "/Game/Gen/Animations/AS_Fireball_Cast": "/Game/Gen/Champions/Curffe/Animations/AS_Fireball_Cast",
    "/Game/Gen/Animations/AS_GreatFireball_Cast": "/Game/Gen/Champions/Curffe/Animations/AS_GreatFireball_Cast",
}
for asset in unreal.EditorAssetLibrary.list_assets("/Game/Gen/Abilities/FlameLeap", recursive=False):
    name = asset.split(".")[0]
    moves[name] = name.replace("/Game/Gen/Abilities/FlameLeap", "/Game/Gen/Champions/Curffe/Abilities/FlameLeap")
tools = unreal.AssetToolsHelpers.get_asset_tools()
data = [unreal.AssetRenameData(unreal.load_asset(src), dst.rsplit("/", 1)[0], dst.rsplit("/", 1)[1]) for src, dst in moves.items()]
print("renamed:", tools.rename_assets(data))
redirectors = unreal.EditorAssetLibrary.list_assets("/Game/Gen", recursive=True)
redirectors = [unreal.load_asset(p) for p in redirectors if unreal.EditorAssetLibrary.find_asset_data(p).asset_class_path.asset_name == "ObjectRedirector"]
tools.fixup_referencers(redirectors)
print("redirectors fixed:", len(redirectors))
```

  Expected: `renamed: True` and no redirectors left under `/Game/Gen` (rerun the redirector listing to confirm `0`).

- [ ] **Step 3: Configure the abilities.** Python property names drop the `b` prefix (`bFeedable` becomes `feedable`). If a name fails, run `discover_python_class` on `GenGA_Projectile`.

```python
import unreal
def cdo(path):
    return unreal.get_default_object(unreal.BlueprintEditorLibrary.generated_class(unreal.load_asset(path)))
fb = cdo("/Game/Gen/Champions/Curffe/Abilities/GA_Fireball")
fb.set_editor_property("cast_time", 0.35)
fb.set_editor_property("damage", unreal.ScalableFloat(value=10.0))
fb.set_editor_property("cooldown_duration", unreal.ScalableFloat(value=0.0))
fb.set_editor_property("energy_on_hit", 2.0)
fb.set_editor_property("resource_on_hit", 1.0)
gf = cdo("/Game/Gen/Champions/Curffe/Abilities/GA_GreatFireball")
for k, v in {"feedable": True, "feed_interval": 0.2, "max_feed": 5, "cast_time": 0.5, "damage_per_feed": 6.0,
             "energy_on_hit": 6.0, "energy_per_feed": 1.0, "speed_multiplier_at_max_feed": 0.64, "scale_at_max_feed": 1.75,
             "explosion_min_feed": 3, "explosion_radius": 150.0, "knockback_min_feed": 5, "knockback_distance": 400.0,
             "activation_policy": unreal.GenAbilityActivationPolicy.ON_INPUT_TRIGGERED}.items():
    gf.set_editor_property(k, v)
gf.set_editor_property("damage", unreal.ScalableFloat(value=14.0))
gf.set_editor_property("cooldown_duration", unreal.ScalableFloat(value=6.0))
for p, speed, rng in [("/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_Fireball", None, 1100.0),
                      ("/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_GreatFireball", 2500.0, 1300.0)]:
    proj = cdo(p)
    if speed: proj.set_editor_property("speed", speed)
    proj.set_editor_property("max_range", rng)
for p in ["/Game/Gen/Champions/Curffe/Abilities/GA_Fireball", "/Game/Gen/Champions/Curffe/Abilities/GA_GreatFireball",
          "/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_Fireball", "/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_GreatFireball"]:
    bp = unreal.load_asset(p); bp.modify(); unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    print(p, unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False))
```

  Expected: four `True` lines. Read the values back after compiling to confirm they stuck.

- [ ] **Step 4: Create `BP_Curffe`** as a child of `BP_Champion`, in `/Game/Gen/Champions/Curffe`:
  - **StartupAbilities:** `[GA_Fireball, GA_GreatFireball, GA_FlameLeap]`.
  - **StartupEffects:** `[CurffeGE_HearthSetup, CurffeGE_HearthFill, CurffeGE_HearthRegen]`, in that order. The fill must come after the setup.
  - **Hearth component:** add a `CurffeHearthComponent` named `Hearth`, attached to the root, with `FlameMaterial = /Game/Gen/VFX/Stylized/M_ST_FireOrb`.
  - **How:** use `unreal.BlueprintService` (add a component, set a component property) per the `VibeUE_blueprints` skill. Then compile and save.
  - **Check:** read back `startup_abilities` and `startup_effects` on the CDO. Expected: 3 abilities and 3 effects, in order.

- [ ] **Step 5: Point the game mode at Curffe.** On the `BP_GenGameMode` CDO, set `default_pawn_class` to the `BP_Curffe` generated class, then compile and save.
  Expected: reading back gives `BP_Curffe_C`.

- [ ] **Step 6: Smoke test in PIE** (Standalone, 1 player).
  - Flames show 5/5 on the HUD and 5 orbiting spheres are visible (use `capture_image source=game`).
  - LMB fires repeatedly with no cooldown.
  - Holding RMB for about 1 s drops the HUD count. Expected: a reading of 5 → 0, then regeneration back up.

- [ ] **Step 7: Commit, push, unlock.** Git sees the moves as delete plus add; stage both.

```bash
git add -A Content/Gen/Abilities Content/Gen/Animations Content/Gen/Champions Content/Gen/Core/BP_GenGameMode.uasset
git commit -m "Curffe assets: own folder, BP_Curffe with Hearth, abilities configured"
git push
git lfs unlock Content/Gen/Core/BP_GenGameMode.uasset
git lfs locks
```

  The moved files' old paths no longer exist, so release those locks by ID. They are your own locks, so no `--force` is needed: for each ID listed against an old `Content/Gen/Abilities/...` or `Content/Gen/Animations/...` path, run `git lfs unlock --id=<ID>`. Expected: `git lfs locks` prints nothing.

---

### Task 9: PIE verification matrix (dedicated server + 3 clients)

**Files:**
- Create: `Content/Python/gen_pie_tools.py`

- [ ] **Step 1: Create the PIE helper** at `Content/Python/gen_pie_tools.py`:

```python
"""Outils de test PIE pour Gen (serveur dédié + clients). Usage : import gen_pie_tools as t"""
import unreal
import vibeue

INPUT = "/Game/Gen/Input/"


def worlds():
    w = vibeue.pie_worlds()
    return w["server"], w["clients"]


def server_pawns():
    server, _ = worlds()
    pawns = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.GenPlayerCharacter)
    return sorted(pawns, key=lambda p: p.get_player_state().get_player_id())


def client_controller(client_index):
    """client_index : 1, 2, 3 (ordre des fenêtres PIE)."""
    _, clients = worlds()
    return unreal.GameplayStatics.get_player_controller(clients[client_index - 1], 0)


def server_pawn_for(client_index):
    pc = client_controller(client_index)
    player_id = pc.get_player_state().get_player_id()
    return next(p for p in server_pawns() if p.get_player_state().get_player_id() == player_id)


def place(client_index, x, y, yaw=0.0):
    pawn = server_pawn_for(client_index)
    pawn.set_actor_location_and_rotation(unreal.Vector(x, y, pawn.get_actor_location().z), unreal.Rotator(0, 0, yaw), False, True)


def aim(client_index, x, y):
    pc = client_controller(client_index)
    pc.set_editor_property("debug_aim_override", True)
    pc.set_editor_property("debug_aim_location", unreal.Vector(x, y, 0))


def tap(action, client_index):
    return unreal.InputService.inject_action(INPUT + action, 1.0, 0.0, 0.0, client_index)


def hold(action, seconds, client_index):
    return unreal.InputService.inject_action_for(INPUT + action, seconds, client_index)


def state(client_index):
    p = server_pawn_for(client_index)
    return {"hp": p.get_health(), "energy": p.get_energy(), "flames": p.get_resource(), "fed": p.get_fed_resource(),
            "loc": p.get_actor_location(), "team": p.k2_get_team_id()}


def client_view_flames(viewer_index, subject_index):
    """Flammes de subject vues depuis le client viewer (réplication)."""
    _, clients = worlds()
    subject_id = client_controller(subject_index).get_player_state().get_player_id()
    for p in unreal.GameplayStatics.get_all_actors_of_class(clients[viewer_index - 1], unreal.GenPlayerCharacter):
        if p.get_player_state().get_player_id() == subject_id:
            return p.get_resource()
    return None
```

  If an API name differs, check it with `discover_python_class` and fix the helper. Keep the helper's function names.

- [ ] **Step 2: Session setup.**
  - `EngineSettingsService.set_pie_settings("Client", 3, True)`
  - `PerformanceService.set_background_throttling(False)`
  - `StartPIE` with a 3 s warm-up
  - `SystemLibrary.execute_console_command(None, "log LogGenProjectileAbility Verbose")`, and the same for `LogGenProjectile`
  - Confirm the teams: `state(1)["team"] == state(3)["team"] != state(2)["team"]`

- [ ] **Step 3: Run the matrix.** One `execute_python_code` call per action; read results in a later call. Each row passes only if the expected result is observed.

| # | Scenario | Setup | Expected |
|---|---|---|---|
| V1 | **Spawn state, replication** | — | `state(i)["flames"] == 5` for i=1..3, `energy == 25`. `client_view_flames(2, 1) == 5` |
| V2 | **Fireball gains** | Place c1 at (0,0), c2 (enemy) at (600,0), c3 (ally) at (300,0). Aim c1 at (600,0). Spend 2 flames first with a fed RMB (hold 0.45 s) so gains are visible. | Fireball **passes through ally c3** (c3's hp unchanged) and hits c2 (−10 hp). c1 gets **+1 flame, +2 energy**. A Fireball aimed at an empty spot gives nothing. |
| V3 | **Fed Great Fireball, counts agree** | c1 at (0,0), c2 at (800,0), c1 full flames. `hold("IA_Ability_Secondary", 0.65, 1)` | Log: `Nourrissage terminé : 3` on CLIENT and SERVEUR. No `corrigé` warning. c1 flames 5 → 2 (server and client 1). c2 hp −(14+18)=−32. Energy +6+3. |
| V3b | **Clamping** | c1 with 2 flames, `hold(…, 1.5, 1)` | Feeding stops at 2 (log), the cast continues automatically while held, flames → 0, no warning. |
| V4 | **Cancel while feeding** | c1 full, hold RMB 1.2 s, tap LMB at ~0.5 s | Log: Great Fireball `Fin (annulé=1)`. **Flames still 5**. No `Cooldown.Ability.GreatFireball` tag (inspector). `fed == 0`. Move speed back to 550. |
| V5 | **Held LMB + RMB** | `hold("IA_Ability_Primary", 3.0, 1)`, then at +0.5 s `hold("IA_Ability_Secondary", 0.45, 1)` | Great Fireball reaches `Projectile … créé` (not cancelled). LMB resumes firing after it. |
| V6 | **Splash, allies, wall** | c1 (0,0) aims (800,0). Enemy dummy A at (800,0), enemy dummy B at (800,120), ally c3 at (800,-120). Wall cube (`PIEActorService.spawn_actor("server", "/Script/Engine.StaticMeshActor", …)`, set mesh `/Engine/BasicShapes/Cube`, scale (0.2,2,2)) at (800,240) and dummy C at (800,330). Feed 3+. | A takes the direct hit; **B takes splash**; **ally c3 untouched**; **dummy C behind the wall untouched**. Log: `2 cible(s) touchée(s)`. |
| V7 | **Knockback 5 flames** | c1 (0,0), c2 (600,0), hold RMB 1.2 s (5 flames) | c2 moves **+X by 320–480 cm**. Repeat with a wall at (750,0): c2 stops at the wall (X < 750). |
| V8 | **Death and respawn** | Hold RMB 1 s on c1, then from the server apply lethal damage to c1 (`UGenGE_Damage` 999 via the ASC) | After respawn: flames 5, `fed == 0`, speed 550, no cast bar. |
| V9 | **Regen** | Spend to 0 | +1 flame every ~3 s, up to 5, on server and client 2's view. |
| V10 | **Orbit visuals** | Capture `capture_image source=game` for client 2 while c1 feeds | c1's orbit shows fewer flames during feeding (`fed` replicated to c2). |

- [ ] **Step 4: Clean up.**
  - `PIEActorService.destroy_all()`, `StopPIE`.
  - Restore the PIE settings to Standalone, 1 client; re-enable background throttling; set log verbosity back to Log.

- [ ] **Step 5: Fix any failure.** Each failing row is a bug: use the systematic-debugging skill, fix it in the owning task's files, rebuild, and rerun that row and every row after it.
- [ ] **Step 6: Commit the helper and any fixes.**

```bash
git add Content/Python/gen_pie_tools.py
git commit -m "PIE test helpers for Curffe verification"
git push
```

---

## Plans 2 and 3 (next)

- **Plan 2, control and defence:**
  - counter framework (`State.Countering`, projectiles and melee notify the counter) and **Backfire**;
  - delayed ground-area actor with telegraph and **Flame Pillar** (stun);
  - **Meteor Leap** (feeding reused, ring of Fireballs).
- **Plan 3, power spells:**
  - **Untouchable** state;
  - **Living Flame**;
  - **Combustion** (Pyroblast swap, unlimited flames, fast feeding);
  - **Resilience**;
  - energy costs for R and F, and the cancel key.
