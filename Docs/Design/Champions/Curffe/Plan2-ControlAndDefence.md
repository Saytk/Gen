# Curffe Plan 2: Control and Defence. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Curffe gets his defensive and control spells: the counter framework and **Backfire** (A key), a delayed ground area with a telegraph and **Flame Pillar** (E, the kit's only stun), and **Meteor Leap** (Space, feedable, ring of Fireballs on landing). Everything is verified in PIE with a dedicated server and 3 clients.

**Architecture:**
- **Generic systems** go in the shared `Source/Gen/AbilitySystem/` and `Source/Gen/Actors/` folders so later champions reuse them:
  - `UGenGA_Cast`, extracted from `UGenGA_Projectile`. It holds feeding, the cast, the aim, costs paid on release and the server's cast-time check. Projectile, ground area, leap and counter abilities all derive from it.
  - The counter rule (`GenHitRules`): `AGenCharacterBase::ResolveIncomingHit` is called by projectiles, melee and areas before they apply anything. Projectiles and melee trigger a counter; areas don't.
  - Timed states: `UGenGE_TimedState`, `UGenGE_TimedMoveSpeed` and `UGenAbilitySystemComponent::ApplyHardCC` (stun).
  - `AGenGroundArea`, a delayed or instant area with a team-relative telegraph and line of sight.
  - `UGenGA_GroundArea`, `UGenGA_Counter` and `UGenGA_Leap`.
  - A status visuals component.
- **Curffe-only code** goes in `Source/Gen/Champions/Curffe/`: Curffe's tags and `UCurffeGA_MeteorLeap`, which adds the Fireball ring. Backfire and Flame Pillar are assets of the generic classes.
- **Authority** stays as in Plan 1:
  - the client decides the fed count and the server clamps it;
  - the server alone applies damage, stuns and gains;
  - costs are paid when the spell goes off, never while it is being cast.

**Tech Stack:** Unreal Engine 5.8, C++ module `Gen`, Gameplay Ability System (LocalPredicted abilities, ASC on the PlayerState, Mixed replication), Unreal Automation tests, VibeUE MCP for editor and PIE work.

**Spec:** `Docs/Design/Champions/Curffe/Curffe.md` (§2, §3 Q/E/Space, §4, §9) and `Docs/Design/CharacterGuidelines.md` (§3.1–§3.4, §3.6). Telegraph visuals: `Docs/ArtBible.md` §4.3, §7.1, §7.5, §7.6 and `Docs/UI_Guidelines.md` §4.12, §8.6.

## Global Constraints

- **Starting point.**
  - Plan 1 is done, including its final fixes: the `curffe-plan1` branch at commit `7799691` or later, where `UGenGA_Projectile` has `ReleaseShot`/`LaunchShot`, `ApplyReportedFedCount`, `FeedSlotsAtPress` and `MarkFeedEnded`, and `AGenCharacterBase` has `StartFeedCast`/`MarkFeedEnded` (one cast bar from press to throw).
  - Work on a new branch, `curffe-plan2`, cut from that tip.
  - Never push to `main`. Merge only with the user's consent.
- **Code comments in French**, matching the existing code (tabs, Allman braces, UE naming). Docs use English (Canadian spelling).
- **Compiling.**
  - Close the editor cleanly first (`unreal.SystemLibrary.quit_editor()`), then build with `Build.bat`.
  - Never run `Plugins/VibeUE/BuildAndLaunchGame.ps1`.
  - The agent may relaunch `Gen.uproject` itself.
- **Editor Python:** `execute_python_code` always runs with `auto_save: false`. Save only the assets you changed.
- **LFS locks.**
  - Before modifying any existing `.uasset` or `.umap`, run `git fetch origin` and check `git diff --stat HEAD...origin/main -- Content`. Then check `git lfs locks` and run `git lfs lock <path>`.
  - If a file is locked by someone else, stop and tell the user. Never use `--force`.
  - **Collision risk:** the `ui-ability-bar` branch edits `GA_Fireball`, `GA_GreatFireball`, `GA_FlameLeap` (icons) and `IMC_Arena`. Coordinate with the user before Task 10 if those assets are locked or have changed on `main`.
- **PIE verification** uses a dedicated server and **3 clients**. Clients 1 and 3 are team 0 and client 2 is team 1. Restore the user's PIE settings afterwards (Standalone, 1 client).
- **Starting values (spec §3, copied verbatim):**
  - **Q: Backfire.**
    - Cast **0.1 s**, window **1.2 s**, cooldown **10 s**. The mage is slowed 50 % during the window.
    - Triggered by projectiles and melee hits (P), not ground areas (A). The blocked hit deals nothing.
    - On a block: **+2 flames per blocked hit**, **+10 energy** (once per cast), melee attackers are **knocked back 3 m**.
  - **E: Flame Pillar.**
    - Ground-targeted delayed area (A). Cast **0.4 s** (+0.2 s per fed flame), then **0.8 s** telegraph before impact. Range **9 m**, cooldown **12 s**.
    - Radius **2 m + 0.3 m per flame**. **12** damage and a **1 s stun**. Energy **+8** on hit.
  - **Space: Meteor Leap.**
    - Leap with a visible arc, **7 m**, cooldown **10 s**. Take-off **0.1 s + 0.2 s per fed flame**.
    - **8** damage in a small area on landing (A). Energy **+2** on landing hit.
    - Each fed flame bursts out as a Fireball (P, 8 damage) in an even ring around the landing point. Ring Fireballs follow the Fireball rules (range, walls, counters). An enemy can be hit by **one ring projectile at most**.
- **Key slots.** The project uses AZERTY. The spec's Q is `InputTag.Ability.1` (key A), E is `InputTag.Ability.2`, and Space is `InputTag.Ability.Mobility`.
- **Every new or changed ability sets** `InputTag`, `DisplayName` (French, like the existing ones), `CooldownTags` and `CooldownDuration`. It also sets `Icon` when the property exists (it arrives with the `ui-ability-bar` merge). The ability bar reads these.
- **Costs are paid on release.** A cancelled or interrupted cast spends no cooldown and no flames.
- **Telegraphs** (Art Bible §7.5, UI §4.12):
  - unlit only;
  - the border equals the hitbox radius;
  - fill at α 0.20 for self and ally, and α 0.15 with static hazard stripes for an enemy;
  - border α 0.90;
  - relation colour from the team-colour MPC through `RelationIndex` (1 self, 2 ally, 3 enemy, 4 neutral).
- **Folders.** Generic code goes in `Source/Gen/AbilitySystem/**`, `Source/Gen/Actors/` or `Source/Gen/Character/`. Curffe-only code goes in `Source/Gen/Champions/Curffe/` and Curffe assets in `Content/Gen/Champions/Curffe/`.

## Review Focus

1. **Two feedable spells back to back.** For example, feed the Great Fireball, then press Space, or feed the leap, then press E. Expected:
   - the first cast is cancelled and spends nothing;
   - only the new spell feeds;
   - the orbit shows the right count on every client;
   - a late clear from the cancelled spell never wipes the new spell's display.

   Pinned by Task 1 (`Gen.Feeding.FedDisplay`) and Task 11 V15.
2. **A countered projectile with splash next to the counterer's allies.** Expected:
   - the countering mage takes nothing: no damage, splash or knockback;
   - his allies inside the explosion still take the splash;
   - the attacker gains energy only if somebody was actually hit.

   Pinned by Task 3 (`Gen.Combat.CounterResolve`) and Task 11 V3.
3. **The caster dies or respawns between a Flame Pillar's cast and its impact.** Expected: the pillar still lands and hits only the caster's enemies. The team is stored at spawn, so the caster's allies are never hit even when the instigator pawn is gone. Pinned by Task 11 V9.
4. **A stun during the leap's take-off versus in flight, and a stun during the Backfire window.** Expected:
   - take-off is cancelled with no cost;
   - flight continues and the ring still bursts;
   - Backfire ends and `State.Countering` is removed.

   Pinned by Task 11 V5 and V14.
5. **Walls next to the leap's landing point and inside a pillar's radius.** Expected:
   - ring Fireballs never spawn behind a wall; one aimed at a wall explodes on it;
   - areas never hit through a wall;
   - a target that is only partly behind cover is still hit (multi-point line of sight).

   Pinned by Task 1 (`Gen.Area.LineOfSightSamples`) and Task 11 V10 and V17.

---

## File map

| File | Responsibility |
|---|---|
| `Source/Gen/AbilitySystem/GenHitRules.h` (new) | Pure counter rule (`EGenHitKind`, `EGenHitResponse`), counter reward |
| `Source/Gen/AbilitySystem/GenAreaRules.h` (new) | Pure area maths: range clamp, ring directions, line-of-sight samples, viewer relation, telegraph timing |
| `Source/Gen/AbilitySystem/GenSalvo.h` (new) | `FGenProjectileSalvo`: one hit per target for a ring of projectiles |
| `Source/Gen/AbilitySystem/GenFeeding.h` | Adds `FFedDisplay` (one spell owns the fed-count display) |
| `Source/Gen/AbilitySystem/GenWorldQueries.h/.cpp` (new) | Wall trace, multi-point line of sight, floor trace (shared by projectiles and areas) |
| `Source/Gen/AbilitySystem/Effects/GenGE_TimedState.h/.cpp` (new) | Timed state (tags for N s) and timed move-speed state (slow, haste, stun) |
| `Source/Gen/AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.cpp` | Multipliers compound instead of adding up |
| `Source/Gen/AbilitySystem/GenAbilitySystemComponent.h/.cpp` | `ApplyHardCC`, `RemoveTimedStates` |
| `Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h/.cpp` | `State.CastLocked` blocks activation |
| `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h/.cpp` (new) | Feeding, cast, aim, release (moved out of `UGenGA_Projectile`), spawn helpers |
| `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h/.cpp` | Shrinks to the projectile-specific part |
| `Source/Gen/AbilitySystem/Abilities/GenGA_GroundArea.h/.cpp` (new) | Ground-targeted area spell with an aim preview (Flame Pillar) |
| `Source/Gen/AbilitySystem/Abilities/GenGA_Counter.h/.cpp` (new) | Counter stance (Backfire) |
| `Source/Gen/AbilitySystem/Abilities/GenGA_Leap.h/.cpp` (new) | Feedable leap with a landing area |
| `Source/Gen/Actors/GenGroundArea.h/.cpp` (new) | Replicated area actor: telegraph, impact, stun, knockback |
| `Source/Gen/Actors/GenProjectile.h/.cpp` | Counter on direct hit, salvo, shared wall rule, multi-point splash line of sight |
| `Source/Gen/Character/GenCharacterBase.h/.cpp` | `ResolveIncomingHit`, fed display owner, status visuals, timed states removed at death |
| `Source/Gen/Character/GenStatusVisualsComponent.h/.cpp` (new) | One placeholder shape per state tag (countering, stunned...) |
| `Source/Gen/GenGameplayTags.h/.cpp` | `State.Countering`, `State.CastLocked`, `Event.Counter.Blocked`, `SetByCaller.Duration` |
| `Source/Gen/Champions/Curffe/CurffeGameplayTags.h/.cpp` (new) | Backfire and Flame Pillar tags, the block cue tag |
| `Source/Gen/Champions/Curffe/CurffeGA_MeteorLeap.h/.cpp` (new) | Ring of Fireballs on landing |
| `Source/Gen/Tests/GenTestWorld.h` (new) | Shared test world (moved out of `GenResourceTests.cpp`) |
| `Source/Gen/Tests/GenCombatRulesTests.cpp` (new) | Pure-rule tests |
| `Source/Gen/Tests/GenCombatWorldTests.cpp` (new) | GAS tests in a test world (stun, slows, counter, timed states) |
| `Content/Gen/Rendering/**` (new) | `MPC_TeamColours`, `Masters/M_VFX_Telegraph`, `Masters/M_VFX_StatusShape` |
| `Content/Gen/Champions/Curffe/**` | `GA_Backfire`, `GA_FlamePillar`, reparented `GA_FlameLeap`, area Blueprints, block cue |
| `Content/Python/gen_pie_tools.py` | Tag, cooldown and ability helpers for the matrix |

**Running the unit tests and the build** (editor closed). `$Root` is the checkout you run the plan in. By default it's the `Gen-curffe` worktree.

```powershell
$Root = "C:\Users\Samy D\Documents\Unreal Projects\Gen-curffe"
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GenEditor Win64 Development "-Project=$Root\Gen.uproject" -WaitMutex | Select-Object -Last 15
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Root\Gen.uproject" -ExecCmds="Automation RunTests Gen.;Quit" -unattended -nopause -nosplash -nullrhi -NoSound "-abslog=$Root\Saved\Logs\GenTests.log"
Select-String -Path "$Root\Saved\Logs\GenTests.log" -Pattern "Test Completed. Result=|Error:" | Select-Object -Last 60
```

The ~20 `Condition failed` lines at frame 0 come from the engine's own start-up self-test. Ignore them (see the Plan 1 ledger).

---

### Task 1: Pure combat and area rules, with tests

**Files:**
- Create: `Source/Gen/AbilitySystem/GenHitRules.h`, `Source/Gen/AbilitySystem/GenAreaRules.h`, `Source/Gen/AbilitySystem/GenSalvo.h`
- Modify: `Source/Gen/AbilitySystem/GenFeeding.h`
- Test: `Source/Gen/Tests/GenCombatRulesTests.cpp`

**Interfaces:**
- Produces:
  - `enum class EGenHitKind : uint8 { Projectile, Melee, Area }`
  - `enum class EGenHitResponse : uint8 { Hit, Countered }`
  - `GenHitRules::TriggersCounter(EGenHitKind) -> bool`
  - `GenHitRules::Resolve(bool bCountering, EGenHitKind) -> EGenHitResponse`
  - `GenHitRules::FCounterReward { float Resource; float Energy; }`
  - `GenHitRules::GetCounterReward(int32 BlockIndex, float ResourcePerBlock, float EnergyOnFirstBlock) -> FCounterReward`
  - `enum class EGenViewerRelation : uint8 { Self = 1, Ally = 2, Enemy = 3, Neutral = 4 }`
  - `GenAreaRules::ClampToRange(const FVector& Origin, const FVector& Target, float Range) -> FVector`
  - `GenAreaRules::GetRingDirections(int32 Count, const FVector& Forward) -> TArray<FVector>`
  - `GenAreaRules::GetLineOfSightSamples(const FVector& Origin, const FVector& TargetCenter, float Radius, float HalfHeight) -> TArray<FVector>`
  - `GenAreaRules::GetViewerRelation(bool bViewerIsSource, uint8 ViewerTeam, uint8 SourceTeam, uint8 NoTeam) -> EGenViewerRelation`
  - `GenAreaRules::GetImpactDelay(float Delay, float MinTelegraph) -> float`
  - `GenAreaRules::GetTelegraphFill(float Elapsed, float Delay) -> float`
  - `FGenProjectileSalvo::HasHit(const UObject*) const -> bool`
  - `FGenProjectileSalvo::TryClaim(const UObject*) -> bool`
  - `GenFeeding::FFedDisplay { uint8 Count; FObjectKey Source; bool Set(FObjectKey, uint8); }`

- [ ] **Step 0: Check the starting point.**
  - Run `git log --oneline -3` and `git status --short`.
  - Confirm the tree is clean and contains commit `7799691`.
  - Run `git log --oneline 7799691..HEAD -- Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.cpp`. Note any later commit: Task 5 must port it into `UGenGA_Cast`.
  - Create the branch: `git switch -c curffe-plan2`.

- [ ] **Step 1: Write the failing tests** in `Source/Gen/Tests/GenCombatRulesTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenSalvo.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCounterTriggerTest, "Gen.Combat.CounterTrigger",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCounterTriggerTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("un projectile déclenche le contre"), GenHitRules::TriggersCounter(EGenHitKind::Projectile));
	TestTrue(TEXT("la mêlée déclenche le contre"), GenHitRules::TriggersCounter(EGenHitKind::Melee));
	TestFalse(TEXT("une zone au sol traverse le contre"), GenHitRules::TriggersCounter(EGenHitKind::Area));

	TestTrue(TEXT("projectile sur un contre : bloqué"), GenHitRules::Resolve(true, EGenHitKind::Projectile) == EGenHitResponse::Countered);
	TestTrue(TEXT("zone sur un contre : touché"), GenHitRules::Resolve(true, EGenHitKind::Area) == EGenHitResponse::Hit);
	TestTrue(TEXT("projectile sans contre : touché"), GenHitRules::Resolve(false, EGenHitKind::Projectile) == EGenHitResponse::Hit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCounterRewardTest, "Gen.Combat.CounterReward",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCounterRewardTest::RunTest(const FString& Parameters)
{
	// GetCounterReward(BlockIndex, ResourcePerBlock, EnergyOnFirstBlock)
	const GenHitRules::FCounterReward First = GenHitRules::GetCounterReward(1, 2.f, 10.f);
	TestEqual(TEXT("1er blocage : +2 flammes"), First.Resource, 2.f);
	TestEqual(TEXT("1er blocage : +10 énergie"), First.Energy, 10.f);

	const GenHitRules::FCounterReward Second = GenHitRules::GetCounterReward(2, 2.f, 10.f);
	TestEqual(TEXT("2e blocage : +2 flammes"), Second.Resource, 2.f);
	TestEqual(TEXT("2e blocage : énergie une seule fois par incantation"), Second.Energy, 0.f);

	const GenHitRules::FCounterReward None = GenHitRules::GetCounterReward(0, 2.f, 10.f);
	TestEqual(TEXT("aucun blocage : rien"), None.Resource + None.Energy, 0.f);

	const GenHitRules::FCounterReward Negative = GenHitRules::GetCounterReward(1, -2.f, -10.f);
	TestEqual(TEXT("valeurs négatives ignorées"), Negative.Resource + Negative.Energy, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenClampToRangeTest, "Gen.Area.ClampToRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenClampToRangeTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.f, 0.f, 0.f);
	const FVector Target(300.f, 400.f, 10.f); // à 500 cm
	TestEqual(TEXT("dans la portée : inchangé"), GenAreaRules::ClampToRange(Origin, Target, 900.f), Target, 0.01f);
	TestEqual(TEXT("ramené à 250 cm, Z conservé"), GenAreaRules::ClampToRange(Origin, Target, 250.f), FVector(150.f, 200.f, 10.f), 0.01f);
	TestEqual(TEXT("portée nulle : sur le lanceur"), GenAreaRules::ClampToRange(Origin, Target, 0.f), FVector(0.f, 0.f, 10.f), 0.01f);
	TestEqual(TEXT("cible sur le lanceur"), GenAreaRules::ClampToRange(Origin, FVector(0.f, 0.f, 5.f), 900.f), FVector(0.f, 0.f, 5.f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenRingDirectionsTest, "Gen.Area.RingDirections",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenRingDirectionsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 flamme : pas d'anneau"), GenAreaRules::GetRingDirections(0, FVector::ForwardVector).Num(), 0);

	const TArray<FVector> One = GenAreaRules::GetRingDirections(1, FVector(0.f, 1.f, 0.f));
	TestEqual(TEXT("1 flamme : une direction"), One.Num(), 1);
	TestEqual(TEXT("1 flamme : droit devant"), One[0], FVector(0.f, 1.f, 0.f), 0.001f);

	const TArray<FVector> Star = GenAreaRules::GetRingDirections(5, FVector(1.f, 0.f, 0.5f));
	TestEqual(TEXT("5 flammes : étoile à 5 branches"), Star.Num(), 5);
	TestEqual(TEXT("première branche selon l'avant aplati"), Star[0], FVector(1.f, 0.f, 0.f), 0.001f);
	FVector Sum = FVector::ZeroVector;
	for (int32 Index = 0; Index < Star.Num(); ++Index)
	{
		TestEqual(TEXT("direction unitaire"), static_cast<float>(Star[Index].Size()), 1.f, 0.001f);
		TestEqual(TEXT("horizontale"), static_cast<float>(Star[Index].Z), 0.f, 0.001f);
		const FVector& Next = Star[(Index + 1) % Star.Num()];
		TestEqual(TEXT("72° entre deux branches"), static_cast<float>(FVector::DotProduct(Star[Index], Next)), FMath::Cos(FMath::DegreesToRadians(72.f)), 0.001f);
		Sum += Star[Index];
	}
	TestEqual(TEXT("anneau régulier (somme nulle)"), Sum, FVector::ZeroVector, 0.001f);

	TestEqual(TEXT("avant nul : axe X"), GenAreaRules::GetRingDirections(1, FVector::ZeroVector)[0], FVector(1.f, 0.f, 0.f), 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenLineOfSightSamplesTest, "Gen.Area.LineOfSightSamples",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenLineOfSightSamplesTest::RunTest(const FString& Parameters)
{
	// Cible à +X : les bords sont sur l'axe Y (perpendiculaires à la ligne de vue)
	const TArray<FVector> Samples = GenAreaRules::GetLineOfSightSamples(FVector::ZeroVector, FVector(500.f, 0.f, 0.f), 40.f, 90.f);
	TestEqual(TEXT("4 points"), Samples.Num(), 4);
	TestEqual(TEXT("centre"), Samples[0], FVector(500.f, 0.f, 0.f), 0.01f);
	TestEqual(TEXT("bord gauche"), Samples[1], FVector(500.f, 40.f, 0.f), 0.01f);
	TestEqual(TEXT("bord droit"), Samples[2], FVector(500.f, -40.f, 0.f), 0.01f);
	TestEqual(TEXT("haut"), Samples[3], FVector(500.f, 0.f, 90.f), 0.01f);

	const TArray<FVector> SameSpot = GenAreaRules::GetLineOfSightSamples(FVector::ZeroVector, FVector(0.f, 0.f, 50.f), 40.f, 90.f);
	TestEqual(TEXT("cible à la verticale de l'origine : bords sur Y"), SameSpot[1], FVector(0.f, 40.f, 50.f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenViewerRelationTest, "Gen.Area.ViewerRelation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenViewerRelationTest::RunTest(const FString& Parameters)
{
	// GetViewerRelation(bViewerIsSource, ViewerTeam, SourceTeam, NoTeam)
	TestTrue(TEXT("son propre sort"), GenAreaRules::GetViewerRelation(true, 0, 0, 255) == EGenViewerRelation::Self);
	TestTrue(TEXT("sort d'un allié"), GenAreaRules::GetViewerRelation(false, 0, 0, 255) == EGenViewerRelation::Ally);
	TestTrue(TEXT("sort d'un ennemi"), GenAreaRules::GetViewerRelation(false, 0, 1, 255) == EGenViewerRelation::Enemy);
	TestTrue(TEXT("sort neutre (mannequin)"), GenAreaRules::GetViewerRelation(false, 0, 255, 255) == EGenViewerRelation::Neutral);
	TestTrue(TEXT("spectateur sans équipe : ennemi"), GenAreaRules::GetViewerRelation(false, 255, 1, 255) == EGenViewerRelation::Enemy);
	TestEqual(TEXT("valeurs = RelationIndex du matériau"), static_cast<int32>(EGenViewerRelation::Enemy), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenTelegraphTimingTest, "Gen.Area.TelegraphTiming",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenTelegraphTimingTest::RunTest(const FString& Parameters)
{
	// GetImpactDelay(Delay, MinTelegraph)
	TestEqual(TEXT("zone instantanée"), GenAreaRules::GetImpactDelay(0.f, 0.5f), 0.f);
	TestEqual(TEXT("pilier : 0.8 s"), GenAreaRules::GetImpactDelay(0.8f, 0.5f), 0.8f);
	TestEqual(TEXT("jamais sous le minimum"), GenAreaRules::GetImpactDelay(0.3f, 0.5f), 0.5f);

	// GetTelegraphFill(Elapsed, Delay)
	TestEqual(TEXT("mi-parcours"), GenAreaRules::GetTelegraphFill(0.4f, 0.8f), 0.5f, 0.001f);
	TestEqual(TEXT("avant le début"), GenAreaRules::GetTelegraphFill(-1.f, 0.8f), 0.f);
	TestEqual(TEXT("après l'impact"), GenAreaRules::GetTelegraphFill(2.f, 0.8f), 1.f);
	TestEqual(TEXT("sans délai"), GenAreaRules::GetTelegraphFill(0.f, 0.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenSalvoTest, "Gen.Combat.Salvo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenSalvoTest::RunTest(const FString& Parameters)
{
	// Deux objets distincts quelconques jouent le rôle de deux ennemis
	const UObject* EnemyA = GetDefault<UObject>();
	const UObject* EnemyB = UObject::StaticClass();

	FGenProjectileSalvo Salvo;
	TestFalse(TEXT("personne n'est touché au départ"), Salvo.HasHit(EnemyA));
	TestTrue(TEXT("premier projectile sur A"), Salvo.TryClaim(EnemyA));
	TestFalse(TEXT("deuxième projectile sur A : traverse"), Salvo.TryClaim(EnemyA));
	TestTrue(TEXT("A est marqué"), Salvo.HasHit(EnemyA));
	TestFalse(TEXT("B reste touchable"), Salvo.HasHit(EnemyB));
	TestTrue(TEXT("premier projectile sur B"), Salvo.TryClaim(EnemyB));
	TestFalse(TEXT("cible nulle"), Salvo.TryClaim(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFedDisplayTest, "Gen.Feeding.FedDisplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFedDisplayTest::RunTest(const FString& Parameters)
{
	const FObjectKey GreatFireball(GetDefault<UObject>());
	const FObjectKey Leap(UObject::StaticClass());

	GenFeeding::FFedDisplay Display;
	TestTrue(TEXT("la grosse boule de feu affiche 3"), Display.Set(GreatFireball, 3));
	TestEqual(TEXT("3 affichées"), static_cast<int32>(Display.Count), 3);
	TestFalse(TEXT("le bond, qui n'affichait rien, ne l'efface pas"), Display.Set(Leap, 0));
	TestEqual(TEXT("toujours 3"), static_cast<int32>(Display.Count), 3);
	TestTrue(TEXT("son propriétaire l'efface"), Display.Set(GreatFireball, 0));
	TestEqual(TEXT("0 affichée"), static_cast<int32>(Display.Count), 0);
	TestTrue(TEXT("le bond prend la main"), Display.Set(Leap, 2));
	TestTrue(TEXT("un nouveau sort qui nourrit remplace l'affichage"), Display.Set(GreatFireball, 1));
	TestFalse(TEXT("l'ancien ne l'efface plus"), Display.Set(Leap, 0));
	TestEqual(TEXT("1 affichée"), static_cast<int32>(Display.Count), 1);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify the tests fail to compile.**
  Expected: `fatal error C1083: Cannot open include file: 'AbilitySystem/GenAreaRules.h'`.

- [ ] **Step 3: Create `Source/Gen/AbilitySystem/GenHitRules.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

/** Nature d'un coup : décide s'il déclenche les contres (guidelines §3.4). */
enum class EGenHitKind : uint8
{
	Projectile,
	Melee,
	/** Zone au sol (éclaboussure, pilier, nova...) : traverse les contres. */
	Area
};

/** Ce que la cible fait d'un coup ennemi. */
enum class EGenHitResponse : uint8
{
	/** Le coup s'applique (dégâts, contrôle, repoussement). */
	Hit,
	/** Bloqué par un contre : le coup n'inflige rien, le contre est prévenu. */
	Countered
};

/** Règles pures des coups et des contres. Sans état, testées hors monde. */
namespace GenHitRules
{
	/** Une seule règle pour tous les contres : projectiles et mêlée oui, zones au sol non. */
	inline bool TriggersCounter(EGenHitKind Kind)
	{
		return Kind == EGenHitKind::Projectile || Kind == EGenHitKind::Melee;
	}

	inline EGenHitResponse Resolve(bool bCountering, EGenHitKind Kind)
	{
		return bCountering && TriggersCounter(Kind) ? EGenHitResponse::Countered : EGenHitResponse::Hit;
	}

	/** Récompense d'un coup bloqué. */
	struct FCounterReward
	{
		float Resource = 0.f;
		float Energy = 0.f;
	};

	/**
	 * BlockIndex : 1 pour le premier coup bloqué de l'incantation, 2 pour le suivant...
	 * La ressource est gagnée à chaque blocage, l'énergie au premier seulement (spec Backfire).
	 */
	inline FCounterReward GetCounterReward(int32 BlockIndex, float ResourcePerBlock, float EnergyOnFirstBlock)
	{
		FCounterReward Reward;
		if (BlockIndex >= 1)
		{
			Reward.Resource = FMath::Max(ResourcePerBlock, 0.f);
			Reward.Energy = BlockIndex == 1 ? FMath::Max(EnergyOnFirstBlock, 0.f) : 0.f;
		}
		return Reward;
	}
}
```

- [ ] **Step 4: Create `Source/Gen/AbilitySystem/GenAreaRules.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

/** Relation entre le joueur qui regarde et la source d'un effet. Valeurs = RelationIndex de M_VFX_Telegraph (UI §8.6). */
enum class EGenViewerRelation : uint8
{
	Self = 1,
	Ally = 2,
	Enemy = 3,
	Neutral = 4
};

/** Règles pures des zones au sol et des anneaux de projectiles. Sans état, testées hors monde. */
namespace GenAreaRules
{
	/** Point visé ramené à Range (cm) du lanceur, mesuré à plat. Le Z de Target est conservé. */
	inline FVector ClampToRange(const FVector& Origin, const FVector& Target, float Range)
	{
		const FVector Offset2D(Target.X - Origin.X, Target.Y - Origin.Y, 0.f);
		const double Distance = Offset2D.Size();
		if (Range <= 0.f)
		{
			return FVector(Origin.X, Origin.Y, Target.Z);
		}
		if (Distance <= Range)
		{
			return Target;
		}
		const FVector Clamped = Offset2D * (Range / Distance);
		return FVector(Origin.X + Clamped.X, Origin.Y + Clamped.Y, Target.Z);
	}

	/** Count directions horizontales régulières (360° / Count), la première selon Forward aplati. */
	inline TArray<FVector> GetRingDirections(int32 Count, const FVector& Forward)
	{
		TArray<FVector> Directions;
		if (Count <= 0)
		{
			return Directions;
		}

		FVector Base = Forward.GetSafeNormal2D();
		if (Base.IsNearlyZero())
		{
			Base = FVector::ForwardVector;
		}

		Directions.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Directions.Add(Base.RotateAngleAxis(360.f * Index / Count, FVector::UpVector));
		}
		return Directions;
	}

	/**
	 * Points de la capsule testés pour la ligne de vue : centre, deux bords perpendiculaires à la ligne,
	 * haut. Une cible à moitié derrière un coin reste touchable (pas seulement son centre).
	 */
	inline TArray<FVector> GetLineOfSightSamples(const FVector& Origin, const FVector& TargetCenter, float Radius, float HalfHeight)
	{
		FVector Side = FVector::CrossProduct(FVector::UpVector, (TargetCenter - Origin).GetSafeNormal2D());
		if (Side.IsNearlyZero())
		{
			Side = FVector::RightVector;
		}
		return { TargetCenter, TargetCenter + Side * Radius, TargetCenter - Side * Radius, TargetCenter + FVector(0.f, 0.f, HalfHeight) };
	}

	/** Point de vue d'un joueur sur l'effet d'une source (couleur du télégraphe). */
	inline EGenViewerRelation GetViewerRelation(bool bViewerIsSource, uint8 ViewerTeam, uint8 SourceTeam, uint8 NoTeam)
	{
		if (bViewerIsSource)
		{
			return EGenViewerRelation::Self;
		}
		if (SourceTeam == NoTeam)
		{
			return EGenViewerRelation::Neutral;
		}
		return ViewerTeam == SourceTeam ? EGenViewerRelation::Ally : EGenViewerRelation::Enemy;
	}

	/** Délai d'impact : 0 = immédiat ; sinon jamais sous MinTelegraph (spec Combustion : télégraphes ≥ 0.5 s). */
	inline float GetImpactDelay(float Delay, float MinTelegraph)
	{
		return Delay <= 0.f ? 0.f : FMath::Max(Delay, MinTelegraph);
	}

	/** Remplissage du télégraphe : 0 à l'apparition, 1 à l'impact. */
	inline float GetTelegraphFill(float Elapsed, float Delay)
	{
		return Delay > 0.f ? FMath::Clamp(Elapsed / Delay, 0.f, 1.f) : 1.f;
	}
}
```

- [ ] **Step 5: Create `Source/Gen/AbilitySystem/GenSalvo.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

/**
 * Salve de projectiles tirés ensemble (anneau du Bond météore) : une cible n'est touchée que par un
 * projectile de la salve, les suivants la traversent. Partagée par les projectiles (serveur uniquement).
 */
struct FGenProjectileSalvo
{
	bool HasHit(const UObject* Target) const
	{
		return Target && HitTargets.Contains(FObjectKey(Target));
	}

	/** Vrai si Target n'avait pas encore été touchée par la salve (elle l'est désormais). */
	bool TryClaim(const UObject* Target)
	{
		if (!Target)
		{
			return false;
		}
		bool bAlreadyHit = false;
		HitTargets.Add(FObjectKey(Target), &bAlreadyHit);
		return !bAlreadyHit;
	}

private:
	TSet<FObjectKey> HitTargets;
};
```

- [ ] **Step 6: Add `FFedDisplay` to `Source/Gen/AbilitySystem/GenFeeding.h`.**
  - Add `#include "UObject/ObjectKey.h"` after `#include "CoreMinimal.h"`.
  - Add this at the end of the `GenFeeding` namespace, after `ReachesThreshold`:

```cpp
	/**
	 * Affichage des unités nourries (elles quittent l'orbite) : un seul sort à la fois en est propriétaire.
	 * Un sort qui n'affiche rien (annulé avant de nourrir) ne peut pas effacer l'affichage d'un autre.
	 */
	struct FFedDisplay
	{
		uint8 Count = 0;
		FObjectKey Source;

		/** Renvoie vrai si l'affichage change. */
		bool Set(FObjectKey InSource, uint8 InCount)
		{
			if (InCount == 0 && Source != FObjectKey() && Source != InSource)
			{
				return false;
			}
			const bool bChanged = Count != InCount || (InCount > 0 && Source != InSource);
			Count = InCount;
			Source = InCount > 0 ? InSource : FObjectKey();
			return bChanged;
		}
	};
```

- [ ] **Step 7: Build, then run the unit tests.**
  Expected: these all show `Result={Success}`: `Gen.Combat.CounterTrigger`, `Gen.Combat.CounterReward`, `Gen.Area.ClampToRange`, `Gen.Area.RingDirections`, `Gen.Area.LineOfSightSamples`, `Gen.Area.ViewerRelation`, `Gen.Area.TelegraphTiming`, `Gen.Combat.Salvo` and `Gen.Feeding.FedDisplay`. The Plan 1 tests still pass.

- [ ] **Step 8: Commit**

```bash
git add Source/Gen/AbilitySystem/GenHitRules.h Source/Gen/AbilitySystem/GenAreaRules.h Source/Gen/AbilitySystem/GenSalvo.h Source/Gen/AbilitySystem/GenFeeding.h Source/Gen/Tests/GenCombatRulesTests.cpp
git commit -m "Add pure counter, area, salvo and fed-display rules with tests"
```

---

### Task 2: Timed states, stun, compounding slows, shared test world

**Files:**
- Modify: `Source/Gen/GenGameplayTags.h`, `Source/Gen/GenGameplayTags.cpp`
- Create: `Source/Gen/AbilitySystem/Effects/GenGE_TimedState.h`, `Source/Gen/AbilitySystem/Effects/GenGE_TimedState.cpp`
- Modify: `Source/Gen/AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h/.cpp`
- Modify: `Source/Gen/AbilitySystem/GenAbilitySystemComponent.h/.cpp`
- Modify: `Source/Gen/Character/GenCharacterBase.cpp` (`HandleOutOfHealth`)
- Create: `Source/Gen/Tests/GenTestWorld.h`
- Modify: `Source/Gen/Tests/GenResourceTests.cpp`
- Test: `Source/Gen/Tests/GenCombatWorldTests.cpp`

**Interfaces:**
- Consumes: `UGenGE_Gain`, `UGenAttributeSet::GetMoveSpeedAttribute()`, `AGenTrainingDummy`.
- Produces:
  - Tags `GenGameplayTags::State_Countering`, `State_CastLocked`, `Event_Counter_Blocked`, `SetByCaller_Duration`.
  - `UGenGE_TimedState`, with `static void SetDuration(FGameplayEffectSpec&, float Duration, const FGameplayTagContainer& GrantedTags)`.
  - `UGenGE_TimedMoveSpeed : UGenGE_TimedState`, with `static void SetMagnitudes(FGameplayEffectSpec&, float Duration, float MoveSpeedMultiplier, const FGameplayTagContainer& GrantedTags)`.
  - `UGenAbilitySystemComponent::ApplyHardCC(FGameplayTag StateTag, float Duration, AActor* Source) -> FActiveGameplayEffectHandle` (UFUNCTION, server only).
  - `UGenAbilitySystemComponent::RemoveTimedStates()` (server).
  - `GenTestWorld::FScopedTestWorld`, with `SpawnDummy() -> AGenTrainingDummy*`, `SpawnDummyASC()`, `Advance(float)`, `Get(ASC, Attribute)` and `ApplyClass(ASC, Class)`.

- [ ] **Step 1: Move the test world into a shared header.** Create `Source/Gen/Tests/GenTestWorld.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Character/GenTrainingDummy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

/** Outils partagés des tests GAS hors PIE (monde de jeu minimal, autorité). */
namespace GenTestWorld
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GenTestWorld"));
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

		/** Fait avancer le temps du monde (timers des effets à durée et périodiques). */
		void Advance(float Seconds, float Step = 0.05f)
		{
			for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += Step)
			{
				// FTimerManager::Tick ignore un second appel dans la même frame (GFrameCounter) : on simule donc une frame par pas.
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Step);
			}
		}

		AGenTrainingDummy* SpawnDummy()
		{
			AGenTrainingDummy* Dummy = World->SpawnActor<AGenTrainingDummy>();
			if (Dummy && !Dummy->HasActorBegunPlay())
			{
				Dummy->DispatchBeginPlay();
			}
			return Dummy;
		}

		UAbilitySystemComponent* SpawnDummyASC()
		{
			AGenTrainingDummy* Dummy = SpawnDummy();
			return Dummy ? Dummy->GetAbilitySystemComponent() : nullptr;
		}
	};

	inline float Get(const UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute)
	{
		return ASC->GetNumericAttribute(Attribute);
	}

	inline FActiveGameplayEffectHandle ApplyClass(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass)
	{
		return ASC->ApplyGameplayEffectToSelf(EffectClass->GetDefaultObject<UGameplayEffect>(), 1.f, ASC->MakeEffectContext());
	}
}

#endif
```

- [ ] **Step 2: Point `GenResourceTests.cpp` at it.**
  - Delete the `FScopedTestWorld` struct and the `Get` and `ApplyClass` functions from `namespace GenResourceTests`. Keep `ApplyGain`.
  - Remove the includes that are now redundant (`Character/GenTrainingDummy.h`, `Engine/Engine.h`, `Engine/World.h`). Add `#include "Tests/GenTestWorld.h"`.
  - Replace `using namespace GenResourceTests;` with:

```cpp
using namespace GenTestWorld;
using namespace GenResourceTests;
```

  The test bodies do not change.

- [ ] **Step 3: Write the failing world tests** in `Source/Gen/Tests/GenCombatWorldTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "GenGameplayTags.h"
#include "Tests/GenTestWorld.h"

using namespace GenTestWorld;

namespace GenCombatWorldTests
{
	void ApplySlow(UAbilitySystemComponent* ASC, float Multiplier)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGenGE_MoveSpeedMultiplier::StaticClass(), 1.f, ASC->MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_MoveSpeedMultiplier, Multiplier);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	FActiveGameplayEffectHandle ApplyTimedState(UAbilitySystemComponent* ASC, float Duration, const FGameplayTag& Tag)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGenGE_TimedState::StaticClass(), 1.f, ASC->MakeEffectContext());
		UGenGE_TimedState::SetDuration(*Spec.Data, Duration, FGameplayTagContainer(Tag));
		return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

using namespace GenCombatWorldTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenStunTest, "Gen.Combat.StunTimedState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenStunTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Dummy ? Dummy->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	TestTrue(TEXT("étourdissement appliqué"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestEqual(TEXT("tag State.Stunned"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 1);
	TestEqual(TEXT("immobile"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 0.f);
	TestFalse(TEXT("durée nulle ignorée"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 0.f, nullptr).IsValid());

	TestWorld.Advance(1.1f);
	TestEqual(TEXT("fin de l'étourdissement"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 0);
	TestEqual(TEXT("vitesse rendue"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 550.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenSlowsCompoundTest, "Gen.Combat.SlowsCompound",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenSlowsCompoundTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	ApplySlow(ASC, 0.5f);
	ApplySlow(ASC, 0.5f);
	TestEqual(TEXT("deux ralentis de moitié : 550 x 0.5 x 0.5"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 137.5f, 0.01f);

	const FGameplayEffectSpecHandle Haste = ASC->MakeOutgoingSpec(UGenGE_TimedMoveSpeed::StaticClass(), 1.f, ASC->MakeEffectContext());
	UGenGE_TimedMoveSpeed::SetMagnitudes(*Haste.Data, 2.f, 1.3f, FGameplayTagContainer());
	ASC->ApplyGameplayEffectSpecToSelf(*Haste.Data);
	TestEqual(TEXT("hâte x1.3 par-dessus"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 178.75f, 0.01f);

	TestWorld.Advance(2.1f);
	TestEqual(TEXT("fin de la hâte"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 137.5f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenRemoveTimedStatesTest, "Gen.Combat.RemoveTimedStates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenRemoveTimedStatesTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Dummy ? Dummy->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyTimedState(ASC, 5.f, GenGameplayTags::State_Countering);
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 5.f, nullptr);

	ASC->RemoveTimedStates();
	TestEqual(TEXT("contre retiré"), ASC->GetTagCount(GenGameplayTags::State_Countering), 0);
	TestEqual(TEXT("étourdissement retiré"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 0);
	TestEqual(TEXT("vitesse rendue"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 550.f);
	TestEqual(TEXT("le Foyer (effet infini) reste"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 5.f);
	return true;
}

#endif
```

- [ ] **Step 4: Build to verify it fails.**
  Expected: errors for the missing `GenGE_TimedState.h`, `ApplyHardCC` and `State_Countering`.

- [ ] **Step 5: Add the tags.**
  - In `GenGameplayTags.h`, after `UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Casting);`:

```cpp
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Countering);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_CastLocked);

	// --- Événements (gameplay events) ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Counter_Blocked);
```

    and after `UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Resource);`:

```cpp
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Duration);
```

  - In `GenGameplayTags.cpp`, after the `State_Casting` definition:

```cpp
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Countering, "State.Countering", "Posture de contre : projectiles et melee sont bloques, pas les zones au sol");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_CastLocked, "State.CastLocked", "Ne peut lancer aucun sort (bond en vol, forme de feu...)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Counter_Blocked, "Event.Counter.Blocked", "Un coup a ete bloque par le contre de la cible (serveur)");
```

    and after the `SetByCaller_Resource` definition:

```cpp
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Duration, "SetByCaller.Duration", "Duree passee aux etats temporaires (contre, etourdissement...)");
```

- [ ] **Step 6: Create `Source/Gen/AbilitySystem/Effects/GenGE_TimedState.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GenGE_TimedState.generated.h"

/**
 * État temporaire générique (contre, intouchable, immunité...) : durée = SetByCaller.Duration,
 * tags accordés passés dans le spec (DynamicGrantedTags), comme le cooldown générique.
 * Toujours renseigner la durée via SetDuration.
 */
UCLASS()
class GEN_API UGenGE_TimedState : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGenGE_TimedState();

	/** Durée (s) et tags accordés tant que l'effet dure. */
	static void SetDuration(FGameplayEffectSpec& Spec, float Duration, const FGameplayTagContainer& GrantedTags);
};

/**
 * État temporaire qui multiplie aussi la vitesse (multiplicateurs composés) :
 * ralenti (0.5), hâte (1.3), étourdissement (0). Toujours renseigner via SetMagnitudes.
 */
UCLASS()
class GEN_API UGenGE_TimedMoveSpeed : public UGenGE_TimedState
{
	GENERATED_BODY()

public:
	UGenGE_TimedMoveSpeed();

	static void SetMagnitudes(FGameplayEffectSpec& Spec, float Duration, float MoveSpeedMultiplier, const FGameplayTagContainer& GrantedTags);
};
```

- [ ] **Step 7: Create `Source/Gen/AbilitySystem/Effects/GenGE_TimedState.cpp`**

```cpp
#include "AbilitySystem/Effects/GenGE_TimedState.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"

UGenGE_TimedState::UGenGE_TimedState()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_Duration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}

void UGenGE_TimedState::SetDuration(FGameplayEffectSpec& Spec, float Duration, const FGameplayTagContainer& GrantedTags)
{
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Duration, FMath::Max(Duration, 0.01f));
	Spec.DynamicGrantedTags.AppendTags(GrantedTags);
}

UGenGE_TimedMoveSpeed::UGenGE_TimedMoveSpeed()
{
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_MoveSpeedMultiplier;

	// MultiplyCompound : les effets se multiplient entre eux (0.5 et 0.5 => 0.25, 0 => immobile)
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetMoveSpeedAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyCompound;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	Modifiers.Add(Modifier);
}

void UGenGE_TimedMoveSpeed::SetMagnitudes(FGameplayEffectSpec& Spec, float Duration, float MoveSpeedMultiplier, const FGameplayTagContainer& GrantedTags)
{
	SetDuration(Spec, Duration, GrantedTags);
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_MoveSpeedMultiplier, FMath::Max(MoveSpeedMultiplier, 0.f));
}
```

- [ ] **Step 8: Make the cast slow compound too.** In `GenGE_MoveSpeedMultiplier.cpp`, replace the comment and the op:

```cpp
	// MultiplyCompound : 0.5 => vitesse x0.5, et deux ralentis se multiplient (au lieu de s'additionner à 0)
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetMoveSpeedAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyCompound;
```

  Leave the header's comment as it is: it already says "multiplie".

- [ ] **Step 9: Add `ApplyHardCC` and `RemoveTimedStates` to `UGenAbilitySystemComponent`.**
  - In `GenAbilitySystemComponent.h`, after `IsAnotherAbilityCasting`:

```cpp
	/**
	 * Serveur : applique un contrôle dur (StateTag = State.Stunned...) pendant Duration secondes.
	 * Étourdi : vitesse à 0 et sorts bloqués (ActivationBlockedTags). Point d'entrée unique de tous
	 * les contrôles durs (immunités et résilience s'y branchent). Handle invalide si rien n'est appliqué.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gen|CrowdControl")
	FActiveGameplayEffectHandle ApplyHardCC(FGameplayTag StateTag, float Duration, AActor* Source);

	/** Serveur : retire les états temporaires (UGenGE_TimedState et dérivés), ex. à la mort. */
	void RemoveTimedStates();
```

  - In `GenAbilitySystemComponent.cpp`, add `#include "AbilitySystem/Effects/GenGE_TimedState.h"`, then append:

```cpp
FActiveGameplayEffectHandle UGenAbilitySystemComponent::ApplyHardCC(FGameplayTag StateTag, float Duration, AActor* Source)
{
	if (!IsOwnerActorAuthoritative() || !StateTag.IsValid() || Duration <= 0.f)
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
	return ApplyGameplayEffectSpecToSelf(*Spec.Data);
}

void UGenAbilitySystemComponent::RemoveTimedStates()
{
	if (!IsOwnerActorAuthoritative())
	{
		return;
	}

	FGameplayEffectQuery Query;
	Query.CustomMatchDelegate.BindLambda([](const FActiveGameplayEffect& Effect)
	{
		return Effect.Spec.Def && Effect.Spec.Def->IsA<UGenGE_TimedState>();
	});
	RemoveActiveEffects(Query);
}
```

- [ ] **Step 10: Remove timed states at death.** In `GenCharacterBase.cpp`, `HandleOutOfHealth`, after `AbilitySystemComponent->CancelAllAbilities();`:

```cpp
		// Un mort ne garde ni contre, ni étourdissement, ni état temporaire (l'ASC survit au respawn)
		AbilitySystemComponent->RemoveTimedStates();
```

- [ ] **Step 11: Build, then run the unit tests.**
  Expected: `Gen.Combat.StunTimedState`, `Gen.Combat.SlowsCompound` and `Gen.Combat.RemoveTimedStates` pass. All earlier tests still pass, including the four `Gen.Resource.*` tests that now use `GenTestWorld.h`.

- [ ] **Step 12: Commit**

```bash
git add Source/Gen/GenGameplayTags.h Source/Gen/GenGameplayTags.cpp Source/Gen/AbilitySystem/Effects Source/Gen/AbilitySystem/GenAbilitySystemComponent.h Source/Gen/AbilitySystem/GenAbilitySystemComponent.cpp Source/Gen/Character/GenCharacterBase.cpp Source/Gen/Tests
git commit -m "Timed states, stun through ApplyHardCC, compounding slows, shared test world"
```

---

### Task 3: Hit resolution on the character, fed display owner, cast lock

**Files:**
- Modify: `Source/Gen/Character/GenCharacterBase.h/.cpp`
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h/.cpp`
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.cpp` (one call site)
- Test: `Source/Gen/Tests/GenCombatWorldTests.cpp`

**Interfaces:**
- Consumes: `GenHitRules::Resolve`, `GenFeeding::FFedDisplay` (Task 1); `State_Countering`, `State_CastLocked`, `Event_Counter_Blocked` (Task 2).
- Produces:
  - `EGenHitResponse AGenCharacterBase::ResolveIncomingHit(AActor* Attacker, EGenHitKind Kind, const UObject* Source)`. Server only. Every damage source calls it before applying anything. A counter that blocks receives `Event.Counter.Blocked` with `Instigator` = Attacker, `OptionalObject` = Source and `EventMagnitude` = `(float)Kind`.
  - `void AGenCharacterBase::SetFedResource(const UObject* Source, uint8 Count)`, which replaces `SetFedResource(uint8)`.
  - `UGenGameplayAbility::CanActivateAbility` refuses while `State.CastLocked` is present.

- [ ] **Step 1: Write the failing test.** Add this to `GenCombatWorldTests.cpp`, before the final `#endif`, and add `#include "Character/GenTrainingDummy.h"` to the includes:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCounterResolveTest, "Gen.Combat.CounterResolve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCounterResolveTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Defender = TestWorld.SpawnDummy();
	AGenTrainingDummy* Attacker = TestWorld.SpawnDummy();
	UAbilitySystemComponent* ASC = Defender ? Defender->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du défenseur"), ASC) || !TestNotNull(TEXT("attaquant"), Attacker))
	{
		return false;
	}

	int32 Received = 0;
	const AActor* ReceivedInstigator = nullptr;
	float ReceivedKind = -1.f;
	ASC->GenericGameplayEventCallbacks.FindOrAdd(GenGameplayTags::Event_Counter_Blocked).AddLambda([&](const FGameplayEventData* Payload)
	{
		++Received;
		ReceivedInstigator = Payload->Instigator;
		ReceivedKind = Payload->EventMagnitude;
	});

	TestTrue(TEXT("sans contre : touché"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Hit);

	ASC->AddLooseGameplayTag(GenGameplayTags::State_Countering);
	TestTrue(TEXT("projectile bloqué"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Countered);
	TestTrue(TEXT("zone : traverse le contre"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Area, nullptr) == EGenHitResponse::Hit);
	TestTrue(TEXT("mêlée bloquée"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Melee, nullptr) == EGenHitResponse::Countered);

	TestEqual(TEXT("le contre est prévenu de chaque blocage"), Received, 2);
	TestTrue(TEXT("instigateur transmis"), ReceivedInstigator == static_cast<const AActor*>(Attacker));
	TestEqual(TEXT("nature du dernier coup bloqué"), ReceivedKind, static_cast<float>(EGenHitKind::Melee));
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'ResolveIncomingHit': is not a member of 'AGenTrainingDummy'`.

- [ ] **Step 3: Update `GenCharacterBase.h`.**
  - Add the includes after `#include "GameplayAbilitySpecHandle.h"`:

```cpp
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
```

  - Replace the declaration `void SetFedResource(uint8 Count) { FedResource = Count; }` and its comment with:

```cpp
	/**
	 * Appelé par le sort qui nourrit (Source), sur le serveur et le client propriétaire (prédiction).
	 * Un seul sort possède l'affichage : Count = 0 n'efface que l'affichage posé par Source.
	 */
	void SetFedResource(const UObject* Source, uint8 Count);
```

  - After the `ApplyKnockback` declaration, add:

```cpp
	/**
	 * Serveur : un coup ennemi de nature Kind arrive (Source = projectile, zone...). À appeler par toute
	 * source de dégâts AVANT d'appliquer quoi que ce soit. Countered : le coup n'inflige rien, et le
	 * contre actif reçoit Event.Counter.Blocked (Instigator = Attacker, EventMagnitude = Kind).
	 * Futures attaques de mêlée : Kind = Melee.
	 */
	EGenHitResponse ResolveIncomingHit(AActor* Attacker, EGenHitKind Kind, const UObject* Source);
```

  - In the `protected:` section, after `uint8 FedResource = 0;`:

```cpp
	/** Propriétaire de l'affichage des unités nourries (serveur et client propriétaire, non répliqué). */
	GenFeeding::FFedDisplay FedDisplay;
```

- [ ] **Step 4: Update `GenCharacterBase.cpp`.** Add `#include "Abilities/GameplayAbilityTypes.h"`, then add after `GetMaxResource()`:

```cpp
void AGenCharacterBase::SetFedResource(const UObject* Source, uint8 Count)
{
	FedDisplay.Set(FObjectKey(Source), Count);
	FedResource = FedDisplay.Count;
}

EGenHitResponse AGenCharacterBase::ResolveIncomingHit(AActor* Attacker, EGenHitKind Kind, const UObject* Source)
{
	if (!HasAuthority() || !AbilitySystemComponent)
	{
		return EGenHitResponse::Hit;
	}

	const bool bCountering = AbilitySystemComponent->HasMatchingGameplayTag(GenGameplayTags::State_Countering);
	const EGenHitResponse Response = GenHitRules::Resolve(bCountering, Kind);

	if (Response == EGenHitResponse::Countered)
	{
		// Le sort de contre écoute cet événement (récompense, repoussement de la mêlée, effet)
		FGameplayEventData Payload;
		Payload.EventTag = GenGameplayTags::Event_Counter_Blocked;
		Payload.Instigator = Attacker;
		Payload.Target = this;
		Payload.OptionalObject = Source;
		Payload.EventMagnitude = static_cast<float>(Kind);
		AbilitySystemComponent->HandleGameplayEvent(Payload.EventTag, &Payload);
	}

	return Response;
}
```

- [ ] **Step 5: Update the projectile's one call site.** In `GenGA_Projectile.cpp`, `SetFedVisual`, replace `Character->SetFedResource(static_cast<uint8>(FMath::Clamp(Count, 0, 255)));` with:

```cpp
		Character->SetFedResource(this, static_cast<uint8>(FMath::Clamp(Count, 0, 255)));
```

- [ ] **Step 6: Block casting under `State.CastLocked`.**
  - In `GenGameplayAbility.h`, after the `ApplyCooldown` override:

```cpp
	/** Refuse aussi pendant State.CastLocked (bond en vol, forme de feu...), posé par code et non par les assets. */
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
```

  - In `GenGameplayAbility.cpp`, after `GetCooldownTags()`, add the following and `#include "AbilitySystemComponent.h"`:

```cpp
bool UGenGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	return !ASC || !ASC->HasMatchingGameplayTag(GenGameplayTags::State_CastLocked);
}
```

- [ ] **Step 7: Build, then run the unit tests.** Expected: `Gen.Combat.CounterResolve` passes, and all earlier tests still pass.

- [ ] **Step 8: Commit**

```bash
git add Source/Gen/Character Source/Gen/AbilitySystem/Abilities Source/Gen/Tests/GenCombatWorldTests.cpp
git commit -m "Resolve incoming hits through counters, single owner for the fed display, cast lock"
```

---

### Task 4: Shared wall and line-of-sight queries; projectiles trigger counters and support salvos

**Files:**
- Create: `Source/Gen/AbilitySystem/GenWorldQueries.h`, `Source/Gen/AbilitySystem/GenWorldQueries.cpp`
- Modify (full replacement): `Source/Gen/Actors/GenProjectile.h`, `Source/Gen/Actors/GenProjectile.cpp`

**Interfaces:**
- Consumes: `GenAreaRules::GetLineOfSightSamples`, `FGenProjectileSalvo` (Task 1); `AGenCharacterBase::ResolveIncomingHit` (Task 3).
- Produces:
  - `GenWorldQueries::FindWallHit(const UWorld*, const FVector& Start, const FVector& End, const TArray<const AActor*>& Ignored, FHitResult& OutHit) -> bool`
  - `GenWorldQueries::HasLineOfSight(const UWorld*, const FVector& Origin, const AActor* Target, const TArray<const AActor*>& Ignored) -> bool`
  - `GenWorldQueries::FindFloor(const UWorld*, const FVector& Point, const TArray<const AActor*>& Ignored) -> FVector`
  - `AGenProjectile::Salvo` (`TSharedPtr<FGenProjectileSalvo>`, server only, set before `FinishSpawning`)
  - Behaviour:
    - a direct hit is P and a splash is A;
    - a countered direct hit deals nothing to the counterer (no splash on him either), but the explosion still splashes others;
    - the instigator gains only if at least one target was actually hit.

- [ ] **Step 1: Create `Source/Gen/AbilitySystem/GenWorldQueries.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
struct FHitResult;

/**
 * Requêtes de monde partagées par les projectiles et les zones au sol.
 * Un "mur" = un objet statique ou dynamique qui bloque physiquement un personnage
 * (ni un projectile, ni un volume sans blocage Pawn).
 */
namespace GenWorldQueries
{
	/** Premier mur entre Start et End. */
	GEN_API bool FindWallHit(const UWorld* World, const FVector& Start, const FVector& End, const TArray<const AActor*>& IgnoredActors, FHitResult& OutHit);

	/** Vrai si au moins un point de la capsule de Target (centre, deux bords, haut) est visible depuis Origin. */
	GEN_API bool HasLineOfSight(const UWorld* World, const FVector& Origin, const AActor* Target, const TArray<const AActor*>& IgnoredActors);

	/** Sol sous Point (trace vers le bas sur WorldStatic) ; Point lui-même si rien n'est trouvé. */
	GEN_API FVector FindFloor(const UWorld* World, const FVector& Point, const TArray<const AActor*>& IgnoredActors);
}
```

- [ ] **Step 2: Create `Source/Gen/AbilitySystem/GenWorldQueries.cpp`**

```cpp
#include "AbilitySystem/GenWorldQueries.h"

#include "AbilitySystem/GenAreaRules.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace GenWorldQueries
{
	namespace
	{
		/** Les bords testés sont un peu à l'intérieur de la capsule (pas de "touché" en frôlant un coin). */
		constexpr float SampleInset = 0.8f;
		constexpr float FloorTraceUp = 50.f;
		constexpr float FloorTraceDown = 1000.f;

		FCollisionQueryParams MakeParams(const TArray<const AActor*>& IgnoredActors)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GenWorldQuery), false);
			for (const AActor* Actor : IgnoredActors)
			{
				if (Actor)
				{
					Params.AddIgnoredActor(Actor);
				}
			}
			return Params;
		}
	}

	bool FindWallHit(const UWorld* World, const FVector& Start, const FVector& End, const TArray<const AActor*>& IgnoredActors, FHitResult& OutHit)
	{
		if (!World)
		{
			return false;
		}

		FCollisionObjectQueryParams WallObjects;
		WallObjects.AddObjectTypesToQuery(ECC_WorldStatic);
		WallObjects.AddObjectTypesToQuery(ECC_WorldDynamic);

		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, Start, End, WallObjects, MakeParams(IgnoredActors));
		for (const FHitResult& Hit : Hits)
		{
			// Un projectile (Overlap sur Pawn) ou un volume qui ne bloque pas les Pawns n'est pas un mur
			const UPrimitiveComponent* HitComponent = Hit.GetComponent();
			if (HitComponent && HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
			{
				OutHit = Hit;
				return true;
			}
		}
		return false;
	}

	bool HasLineOfSight(const UWorld* World, const FVector& Origin, const AActor* Target, const TArray<const AActor*>& IgnoredActors)
	{
		if (!World || !Target)
		{
			return false;
		}

		float Radius = 0.f;
		float HalfHeight = 0.f;
		Target->GetSimpleCollisionCylinder(Radius, HalfHeight);

		FHitResult WallHit;
		for (const FVector& Sample : GenAreaRules::GetLineOfSightSamples(Origin, Target->GetActorLocation(), Radius * SampleInset, HalfHeight * SampleInset))
		{
			if (!FindWallHit(World, Origin, Sample, IgnoredActors, WallHit))
			{
				return true;
			}
		}
		return false;
	}

	FVector FindFloor(const UWorld* World, const FVector& Point, const TArray<const AActor*>& IgnoredActors)
	{
		if (!World)
		{
			return Point;
		}

		FHitResult Hit;
		const FCollisionObjectQueryParams Floors(ECC_WorldStatic);
		if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, FloorTraceUp), Point - FVector(0.f, 0.f, FloorTraceDown), Floors, MakeParams(IgnoredActors)))
		{
			return FVector(Hit.ImpactPoint);
		}
		return Point;
	}
}
```

- [ ] **Step 3: Replace `Source/Gen/Actors/GenProjectile.h`.** Only these parts change from the current file:
  - the `GenSalvo.h` include and the `Salvo` member;
  - the `AddExplosionTargets` signature;
  - the private `FindWallHit` is removed;
  - the class comment.

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GenSalvo.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "GenProjectile.generated.h"

class AGenCharacterBase;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USoundBase;
class USphereComponent;

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

/**
 * Projectile répliqué. Créé par le serveur (UGenGA_Cast::SpawnProjectileShot), il porte le spec du GE de dégâts.
 *
 * - Traverse les alliés et les morts, explose sur un ennemi ou un obstacle.
 * - Coup direct = projectile (déclenche les contres) ; éclaboussure = zone (ne les déclenche pas).
 * - Seul le serveur applique les dégâts ; l'explosion est répliquée (bExploded) pour les FX.
 */
UCLASS()
class GEN_API AGenProjectile : public AActor
{
	GENERATED_BODY()

public:
	AGenProjectile();

	/** Rempli par le sort avant FinishSpawning (serveur uniquement, non répliqué). */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile", meta = (ExposeOnSpawn = true))
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

	/** Gains du lanceur (énergie, ressource), appliqués s'il touche au moins un ennemi (serveur). */
	FGameplayEffectSpecHandle InstigatorOnHitSpecHandle;

	/** Salve dont fait partie ce projectile (anneau) : une cible touchée par la salve est traversée (serveur). */
	TSharedPtr<FGenProjectileSalvo> Salvo;

	/** Serveur, avant FinishSpawning. */
	void InitializeShot(const FGenProjectileShotParams& Params);

	float GetSpeed() const { return Speed; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Serveur : applique les dégâts éventuels puis déclenche l'explosion. */
	void Explode(AActor* HitActor, const FVector& Location);

	UFUNCTION()
	void OnRep_Exploded();

	/** Joue les FX/sons d'impact et cache le projectile (toutes les machines). */
	void PlayImpactEffects();

	/** Hook Blueprint pour des FX additionnels à l'impact. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Projectile", meta = (DisplayName = "On Impact"))
	void K2_OnImpact(const FVector& Location);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** FX de traînée/visuel du projectile (assignez un Niagara System dans le Blueprint). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> ProjectileFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Projectile")
	float Speed = 1800.f;

	/** Portée max : arrivé au bout, le projectile explose (FX d'impact, sans dégâts). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	float MaxRange = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX")
	TObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(ReplicatedUsing = OnRep_Exploded)
	bool bExploded = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize ImpactLocation;

	/** Échelle du tir (visuel + collision), répliquée à l'apparition. */
	UPROPERTY(Replicated)
	float ShotScale = 1.f;

	/** Serveur uniquement. */
	float ExplosionRadius = 0.f;
	float KnockbackDistance = 0.f;

	bool IsValidTarget(const AGenCharacterBase* Character) const;

	/**
	 * Éclaboussure : ennemis vivants dans le rayon, en ligne de vue depuis Origin (pas à travers les murs),
	 * hors Excluded (cible directe, touchée ou bloquée) et qui ne l'ignorent pas (nature : zone).
	 */
	void AddExplosionTargets(const FVector& Origin, const AGenCharacterBase* Excluded, TArray<AGenCharacterBase*>& InOutTargets) const;

	/** Dégâts + repoussement éventuel sur une cible. */
	void ApplyHit(AGenCharacterBase* Target, const FVector& Origin, bool bDirectHit);

private:
	bool bImpactEffectsPlayed = false;
};
```

- [ ] **Step 4: Replace `Source/Gen/Actors/GenProjectile.cpp`.** These parts are unchanged: the constructor, `GetLifetimeReplicatedProps`, `InitializeShot`, `BeginPlay`, `IsValidTarget`, `ApplyHit`, `OnRep_Exploded` and `PlayImpactEffects`. These parts change: `OnSphereOverlap` (salvo), `AddExplosionTargets` and `Explode` (counter, shared queries). `FindWallHit` is removed.

```cpp
#include "Actors/GenProjectile.h"

#include "AbilitySystem/GenWorldQueries.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/GenCharacterBase.h"
#include "CollisionQueryParams.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenProjectile, Log, All);

AGenProjectile::AGenProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicatingMovement(true);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(20.f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
	SetRootComponent(CollisionSphere);

	ProjectileFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileFX"));
	ProjectileFX->SetupAttachment(CollisionSphere);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->InitialSpeed = Speed;
	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->ProjectileGravityScale = 0.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
}

void AGenProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGenProjectile, bExploded);
	DOREPLIFETIME(AGenProjectile, ImpactLocation);
	DOREPLIFETIME_CONDITION(AGenProjectile, Speed, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenProjectile, ShotScale, COND_InitialOnly);
}

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

void AGenProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Clients : l'échelle n'est pas répliquée par le mouvement, on l'applique depuis ShotScale
	SetActorScale3D(FVector(ShotScale));

	// La vitesse éditée dans le Blueprint prime sur la valeur du constructeur
	ProjectileMovement->InitialSpeed = Speed;
	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->Velocity = GetActorForwardVector() * Speed;

	if (APawn* InstigatorPawn = GetInstigator())
	{
		CollisionSphere->IgnoreActorWhenMoving(InstigatorPawn, true);
	}

	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnSphereOverlap);

	if (HasAuthority())
	{
		// En bout de portée, le projectile explose dans le vide (FX d'impact, sans dégâts)
		// plutôt que de disparaître d'un coup
		if (Speed > 0.f)
		{
			FTimerHandle RangeTimer;
			GetWorldTimerManager().SetTimer(RangeTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!bExploded)
				{
					Explode(nullptr, GetActorLocation());
				}
			}), MaxRange / Speed, false);
		}

		// Les overlaps initiaux (spawn à bout portant dans un ennemi ou contre un mur) sont calculés
		// avant BeginPlay, donc avant que le delegate soit branché : on les traite ici
		TArray<UPrimitiveComponent*> OverlappingComponents;
		CollisionSphere->GetOverlappingComponents(OverlappingComponents);
		for (UPrimitiveComponent* Component : OverlappingComponents)
		{
			OnSphereOverlap(CollisionSphere, Component ? Component->GetOwner() : nullptr, Component, INDEX_NONE, false, FHitResult());
			if (bExploded)
			{
				break;
			}
		}
	}
}

void AGenProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Seul le serveur décide des impacts ; les clients reçoivent bExploded
	if (!HasAuthority() || bExploded || !OtherActor || OtherActor == this || OtherActor == GetInstigator())
	{
		return;
	}

	// Les projectiles ne se percutent pas entre eux
	if (OtherActor->IsA<AGenProjectile>())
	{
		return;
	}

	if (const AGenCharacterBase* HitCharacter = Cast<AGenCharacterBase>(OtherActor))
	{
		// On traverse les alliés et les morts
		if (HitCharacter->IsDead() || !AGenCharacterBase::AreEnemies(GetInstigator(), HitCharacter))
		{
			return;
		}

		// Salve (anneau) : une cible déjà touchée par un autre projectile de la salve est traversée
		if (Salvo && Salvo->HasHit(HitCharacter))
		{
			return;
		}
	}
	else if (OtherActor->IsA<APawn>())
	{
		return;
	}

	Explode(OtherActor, bFromSweep ? FVector(SweepResult.ImpactPoint) : GetActorLocation());
}

bool AGenProjectile::IsValidTarget(const AGenCharacterBase* Character) const
{
	return Character && !Character->IsDead() && AGenCharacterBase::AreEnemies(GetInstigator(), Character);
}

void AGenProjectile::AddExplosionTargets(const FVector& Origin, const AGenCharacterBase* Excluded, TArray<AGenCharacterBase*>& InOutTargets) const
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GenProjectileExplosion), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(ExplosionRadius), QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AGenCharacterBase* Character = Cast<AGenCharacterBase>(Overlap.GetActor());
		if (!Character || Character == Excluded || InOutTargets.Contains(Character) || !IsValidTarget(Character))
		{
			continue;
		}

		// Un mur protège la cible, sauf si une partie de sa capsule est visible
		if (!GenWorldQueries::HasLineOfSight(GetWorld(), Origin, Character, { this, Character }))
		{
			continue;
		}

		// L'éclaboussure est une zone : elle traverse les contres
		if (Character->ResolveIncomingHit(GetInstigator(), EGenHitKind::Area, this) != EGenHitResponse::Hit)
		{
			continue;
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

	if (KnockbackDistance > 0.f && !Target->IsDead())
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
	AGenCharacterBase* DirectTarget = Cast<AGenCharacterBase>(HitActor);
	const FVector Forward = GetActorForwardVector();
	const float ScaledRadius = CollisionSphere->GetScaledSphereRadius();
	FVector Origin = Location - Forward * ScaledRadius;

	// Impact sur un mur : la sphère ne fait que chevaucher, le projectile peut déjà être dans le mur.
	// On retrouve la surface en remontant la trajectoire, et on part 5 cm devant elle
	if (!DirectTarget)
	{
		FHitResult SurfaceHit;
		if (GenWorldQueries::FindWallHit(GetWorld(), Location - Forward * (ScaledRadius + Speed * 0.05f + 50.f), Location, { this }, SurfaceHit))
		{
			Origin = FVector(SurfaceHit.ImpactPoint) - Forward * 5.f;
		}
	}

	TArray<AGenCharacterBase*> Targets;
	bool bCountered = false;
	if (IsValidTarget(DirectTarget))
	{
		if (Salvo)
		{
			Salvo->TryClaim(DirectTarget);
		}

		// Coup direct = projectile : un contre le bloque entièrement (pas d'éclaboussure sur lui non plus)
		if (DirectTarget->ResolveIncomingHit(GetInstigator(), EGenHitKind::Projectile, this) == EGenHitResponse::Countered)
		{
			bCountered = true;
		}
		else
		{
			Targets.Add(DirectTarget);
		}
	}

	if (ExplosionRadius > 0.f && HitActor)
	{
		AddExplosionTargets(Origin, DirectTarget, Targets);
	}

	for (AGenCharacterBase* Target : Targets)
	{
		ApplyHit(Target, Origin, Target == DirectTarget);
	}

	UE_LOG(LogGenProjectile, Verbose, TEXT("%s : %d cible(s) touchée(s)%s"), *GetName(), Targets.Num(), bCountered ? TEXT(", coup direct bloqué par un contre") : TEXT(""));

	// Gains du lanceur seulement si quelqu'un a vraiment été touché (un coup bloqué ne rapporte rien)
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

void AGenProjectile::OnRep_Exploded()
{
	if (bExploded)
	{
		PlayImpactEffects();
	}
}

void AGenProjectile::PlayImpactEffects()
{
	if (bImpactEffectsPlayed)
	{
		return;
	}
	bImpactEffectsPlayed = true;

	const FVector Location = ImpactLocation;

	if (ImpactFX)
	{
		// Suit l'échelle du projectile (grosse boule de feu => grosse explosion)
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, Location, GetActorRotation(), GetActorScale3D());
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Location);
	}

	// On masque les composants plutôt que l'acteur : un acteur caché sans collision cesse
	// d'être répliqué, et bExploded n'arriverait jamais aux clients
	ProjectileFX->Deactivate();
	CollisionSphere->SetVisibility(false, true);

	K2_OnImpact(Location);
}
```

- [ ] **Step 5: Build, then run the unit tests.** Expected: `Result: Succeeded`, and all tests pass. (Splash behaviour is checked in PIE, in Task 11 V2 and V3.)

- [ ] **Step 6: Commit**

```bash
git add Source/Gen/AbilitySystem/GenWorldQueries.h Source/Gen/AbilitySystem/GenWorldQueries.cpp Source/Gen/Actors/GenProjectile.h Source/Gen/Actors/GenProjectile.cpp
git commit -m "Shared wall and multi-point line-of-sight queries; projectiles trigger counters and support salvos"
```

---

### Task 5: Extract `UGenGA_Cast` from `UGenGA_Projectile` (no behaviour change for projectiles)

Feeding, the cast, the aim, the server's cast-time check and costs on release become the base class of every spell that "goes off". `UGenGA_Projectile` keeps only what is specific to projectiles.

A UPROPERTY that moves to a parent class keeps its saved value in the Blueprints, because properties are serialized by name. `GA_Fireball` and `GA_GreatFireball` therefore keep their configuration, and their Python names stay the same (`cast_time`, `feedable`...).

**Files:**
- Create: `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h`, `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.cpp`
- Modify (full replacement): `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h`, `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.cpp`
- Modify: `Source/Gen/Character/GenCharacterBase.cpp` (`ServerReportFedResource`)

**Interfaces:**
- Consumes:
  - `AGenCharacterBase::SetFedResource(const UObject*, uint8)` (Task 3);
  - `FGenProjectileSalvo` and `AGenProjectile::Salvo` (Tasks 1 and 4);
  - `State_CastLocked` (Task 2);
  - `GenFeeding::{GetFeedLimit, ValidateFedCount, ClampReportedFed, GetServerCastWait, CastTimeTolerance, ScaleByFeed, ReachesThreshold}`.
- Produces:
  - `struct FGenCastRelease { FVector AimLocation; FVector AimDirection; int32 Fed; }`
  - `UGenGA_Cast` (Abstract), with:
    - public: `ApplyReportedFedCount(int32)`, `IsCastPending() const -> bool`, `CanBeCanceled()`;
    - protected virtual: `OnCastLaunched(const FGenCastRelease&)` (default: `FinishAbility()`) and `IsInterruptedByHardCC() const` (default `true`);
    - protected helpers:
      - `FinishAbility()`
      - `SetCastLock(bool)`
      - `MakeDamageSpec(TSubclassOf<UGameplayEffect>, float Amount, UObject* SourceObject) const -> FGameplayEffectSpecHandle`
      - `MakeGainSpec(float Energy, float Resource) const -> FGameplayEffectSpecHandle`
      - `SpawnProjectileShot(TSubclassOf<AGenProjectile>, const FVector& Origin, const FVector& Direction, const FGenProjectileShotParams&, TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain, float ResourceGain, const TSharedPtr<FGenProjectileSalvo>& Salvo = nullptr) -> AGenProjectile*`
    - protected properties: `CastTime`, `CastMoveSpeedMultiplier`, `CastFX`, `CastFXSocket`, `ChargeMontage`, `CastMontage`, `bFeedable`, `FeedInterval`, `MaxFeed`, `bTurnToAim`.
  - `UGenGA_Projectile : UGenGA_Cast`, which keeps `SpawnProjectile(const FVector&, int32)` and every projectile property.
  - The log category becomes `LogGenCast`. PIE steps now use `log LogGenCast Verbose`.

- [ ] **Step 1: Check for upstream changes.** Run `git log --oneline 7799691..HEAD -- Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.cpp Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h`. If it lists commits, read their diff and port each change into the matching function below before you continue.

- [ ] **Step 2: Create `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "GenGA_Cast.generated.h"

class AGenProjectile;
class UAbilityTask_WaitDelay;
class UAbilityTask_WaitInputRelease;
class UAnimMontage;
class UGameplayEffect;
class UNiagaraSystem;
struct FGenProjectileSalvo;
struct FGenProjectileShotParams;

/** Ce que le sort sait quand il part : après nourrissage, incantation et visée, coûts déjà payés. */
struct FGenCastRelease
{
	/** Point visé sous le curseur (plan horizontal du lanceur). */
	FVector AimLocation = FVector::ZeroVector;
	/** Direction horizontale lanceur -> visée, jamais nulle. */
	FVector AimDirection = FVector::ForwardVector;
	/** Unités nourries, validées par le serveur et déjà dépensées. */
	int32 Fed = 0;
};

/**
 * Sort à incantation : base de tous les sorts qui "partent" (projectile, zone au sol, bond, contre...).
 *
 * Déroulé :
 *  0. Si bFeedable : nourrissage. Tant que la touche est maintenue, une unité de ressource
 *     (flammes de Curffe) passe dans le sort toutes les FeedInterval s, jusqu'à MaxFeed.
 *     Relâcher, atteindre le max ou épuiser la ressource enchaîne sur l'incantation.
 *     Le client décide du nombre ; le serveur le borne à sa ressource et au temps mesuré.
 *     Une seule barre de cast de l'appui au lancer : un cran par flamme, repliée à la fin du nourrissage.
 *  1. Si CastTime > 0 : incantation (barre de cast, ralenti), annulée si le lanceur est étourdi ou meurt
 *     (ou par un autre sort : lancer un sort à incantation annule l'incantation en cours du joueur).
 *  2. Le client envoie le point visé au serveur (target data, avec le nombre d'unités nourries).
 *     Serveur pour un client distant : c'est l'arrivée de la visée qui termine l'incantation
 *     (durée vérifiée à CastTimeTolerance près, voir GenFeeding::GetServerCastWait).
 *  3. Lancer (ReleaseCast) : CommitAbility (cooldown, coût) + dépense de la ressource nourrie,
 *     le lanceur se tourne vers la visée, montage. Une incantation interrompue ne coûte rien.
 *  4. Départ (OnCastLaunched, sous-classe) : ce que fait le sort. La sous-classe appelle FinishAbility().
 *     Visée arrivée trop tôt sur le serveur : le lancer a lieu tout de suite, le départ attend la fin
 *     de l'incantation mesurée par le serveur, et le sort n'est plus annulable par un autre sort d'ici là.
 */
UCLASS(Abstract)
class GEN_API UGenGA_Cast : public UGenGameplayAbility
{
	GENERATED_BODY()

public:
	UGenGA_Cast();

	/** Serveur : compte exact annoncé par le client distant à la fin de son nourrissage (affichage seulement). */
	void ApplyReportedFedCount(int32 Reported);

	/** Vrai pendant le nourrissage et l'incantation, avant le lancer. */
	bool IsCastPending() const { return IsActive() && !bReleased; }

	//~ UGameplayAbility
	/** Faux sur le serveur entre la visée et le départ du sort : le client a déjà lancé. */
	virtual bool CanBeCanceled() const override;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * Le sort part (client et serveur ; coûts déjà payés). Appeler FinishAbility() quand il a fini.
	 * Par défaut : termine tout de suite.
	 */
	virtual void OnCastLaunched(const FGenCastRelease& Release);

	/** Faux pendant une phase qu'un étourdissement ne coupe pas (ex : bond en vol). */
	virtual bool IsInterruptedByHardCC() const { return true; }

	/** Termine le sort. Seul le serveur réplique la fin (voir le commentaire dans le .cpp). */
	void FinishAbility();

	/** Verrou de lancement (State.CastLocked, tag local sur le serveur et le client) ; retiré à la fin du sort. */
	void SetCastLock(bool bLocked);

	/** Spec du GE de dégâts (SetByCaller.Damage = Amount). Invalide si rien à infliger. */
	FGameplayEffectSpecHandle MakeDamageSpec(TSubclassOf<UGameplayEffect> EffectClass, float Amount, UObject* SourceObject) const;

	/** Spec des gains du lanceur (UGenGE_Gain). Invalide si rien à gagner. */
	FGameplayEffectSpecHandle MakeGainSpec(float Energy, float Resource) const;

	/** Serveur : projectile tiré depuis Origin vers Direction, porteur des dégâts et des gains. */
	AGenProjectile* SpawnProjectileShot(TSubclassOf<AGenProjectile> ShotClass, const FVector& Origin, const FVector& Direction, const FGenProjectileShotParams& ShotParams,
		TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain, float ResourceGain, const TSharedPtr<FGenProjectileSalvo>& Salvo = nullptr);

	/** Fin de l'incantation (ou tout de suite si CastTime = 0) : on récupère la visée. */
	UFUNCTION()
	void OnCastFinished();

	/** Étourdi pendant le nourrissage, l'incantation ou une phase interruptible après le départ. */
	UFUNCTION()
	void OnCastInterrupted();

	UFUNCTION()
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur pour un client distant : visée reçue pendant l'incantation. */
	UFUNCTION()
	void OnServerAimReceived(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur : visée arrivée trop tôt, fin de l'incantation => le sort part. */
	UFUNCTION()
	void OnServerLaunchDelayFinished();

	UFUNCTION()
	void OnFeedTick();

	UFUNCTION()
	void OnFeedInputReleased(float TimeHeld);

	/** Fin du nourrissage, synchronisée client -> serveur. */
	UFUNCTION()
	void OnFeedSynced();

	/** Durée d'incantation en secondes (après le nourrissage). 0 = sort instantané. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast", meta = (ClampMin = "0.0", Units = "s"))
	float CastTime = 0.f;

	/** Vitesse de déplacement pendant le nourrissage et l'incantation (1 = pas de ralenti, 0 = immobile). */
	UPROPERTY(EditDefaultsOnly, Category = "Cast", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CastMoveSpeedMultiplier = 0.5f;

	/** Effet sur le lanceur pendant l'incantation (vu par tous) : annonce le sort et sa direction. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast")
	TObjectPtr<UNiagaraSystem> CastFX;

	/** Socket du mesh où attacher CastFX. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast")
	FName CastFXSocket = TEXT("hand_r");

	/** Le lanceur se tourne vers la visée au lancer. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast")
	bool bTurnToAim = true;

	/**
	 * Montage d'incantation (optionnel, répliqué par le GAS) : joué dès le début de l'incantation,
	 * préparation puis geste de lancer. Le régler pour que le lancer tombe à CastTime.
	 * Coupé si l'incantation est interrompue. Ignoré si CastTime = 0 (utiliser CastMontage).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation", meta = (EditCondition = "CastTime > 0"))
	TObjectPtr<UAnimMontage> ChargeMontage;

	/** Montage de lancer (optionnel, répliqué aux autres joueurs par le GAS). Joué au lancer. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation")
	TObjectPtr<UAnimMontage> CastMontage;

	/** Maintenir la touche nourrit le sort avec la ressource du champion (attribut Resource). */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Feeding")
	bool bFeedable = false;

	/** Une unité absorbée toutes les FeedInterval secondes. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.05", Units = "s"))
	float FeedInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Cast|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "1"))
	int32 MaxFeed = 5;

private:
	void StartFeeding();
	void ScheduleFeedTick();
	/** Client (ou hôte) : fin du nourrissage => prévient le serveur puis incante. */
	void StopFeedingLocal();
	void EndFeedTasks();
	void SetFedVisual(int32 Count);
	/** Barre de cast : fin du nourrissage avec Count unités (rappelable pour corriger le compte). */
	void MarkFeedEnded(int32 Count);
	int32 GetAvailableFeed() const;

	void StartCasting();
	void ApplyCastSlow();
	void StartInterruptWatch();
	/** Retire le ralenti et la barre de cast (garde le visuel des unités nourries). */
	void EndCastPresentation();
	/** Nettoyage complet (fin ou annulation du sort). */
	void StopCasting();

	/** Lancer d'un autre sort à incantation du joueur : celui-ci remplace l'incantation en cours. */
	void CancelOtherPendingCasts();

	/** Serveur qui exécute le sort d'un client distant (ni hôte, ni autonome, ni IA). */
	bool IsServerForRemoteClient() const;

	/**
	 * Lancer : borne le nourrissage, CommitAbility (cooldown, coût), dépense la ressource, tourne le lanceur,
	 * joue le montage. Faux si le sort a été terminé (visée invalide, commit refusé).
	 */
	bool ReleaseCast(const FGameplayAbilityTargetDataHandle& DataHandle, FGenCastRelease& OutRelease);
	/** Départ : OnCastLaunched, puis le sort redevient annulable (mort, autre sort) s'il reste actif. */
	void LaunchCast(const FGenCastRelease& Release);

	/** Nombre d'unités nourries retenu pour ce lancer (borné côté serveur). */
	int32 ResolveFedCount(const FGameplayAbilityTargetData* Data) const;
	void SpendResource(int32 Amount);

	FActiveGameplayEffectHandle CastSlowHandle;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitDelay> FeedTickTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputRelease> FeedReleaseTask;

	int32 FedCount = 0;
	int32 FedVisualCount = 0;
	/** Unités disponibles à l'appui (crans de la barre) : plafond du nourrissage. */
	int32 FeedSlotsAtPress = 0;
	bool bIsFeeding = false;
	bool bInterruptWatchStarted = false;
	/** Le sort a été lancé (coûts payés) : il n'est plus "en incantation". */
	bool bReleased = false;
	bool bCastLockApplied = false;
	float FeedStartTime = 0.f;
	/** Serveur : durée du nourrissage mesurée entre l'activation et le signal du client. */
	float ServerFeedElapsed = 0.f;
	/** Serveur : compte brut annoncé par le client (ServerReportFedResource), sinon INDEX_NONE. */
	int32 ReportedFedCount = INDEX_NONE;

	/** Début de l'incantation (après le nourrissage), en temps du monde. */
	float CastStartTime = 0.f;
	/** Serveur : visée du client reçue, le sort est parti côté client (plus annulable par un autre sort). */
	bool bServerShotLocked = false;
	/** Serveur : départ différé (visée reçue en avance), lancer déjà fait. */
	FGenCastRelease PendingRelease;
};
```

- [ ] **Step 3: Create `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.cpp`.** The feeding, cast and aim code is the current `GenGA_Projectile.cpp` code (as of `7799691`), moved here. Four things change:
  - `GEN_ABILITY_LOG` becomes `GEN_CAST_LOG`;
  - `ReleaseShot`/`LaunchShot` become `ReleaseCast`/`LaunchCast`, so any subclass can act on launch;
  - other pending casts are cancelled at activation;
  - `OnCastInterrupted` asks `IsInterruptedByHardCC()`.

```cpp
#include "AbilitySystem/Abilities/GenGA_Cast.h"

#include "Abilities/Tasks/AbilityTask_NetworkSyncPoint.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogGenCast, Log, All);

#define GEN_CAST_LOG(Verbosity, Format, ...) UE_LOG(LogGenCast, Verbosity, TEXT("[%s] %s: " Format), (CurrentActorInfo && CurrentActorInfo->IsNetAuthority()) ? TEXT("SERVEUR") : TEXT("CLIENT"), *GetName(), ##__VA_ARGS__)

UGenGA_Cast::UGenGA_Cast()
{
	ActivationOwnedTags.AddTag(GenGameplayTags::State_Casting);
}

void UGenGA_Cast::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Pas de CommitAbility ici : le cooldown et le coût ne sont appliqués qu'au lancer
	// (ReleaseCast). CanActivateAbility les a déjà vérifiés avant l'activation.
	GEN_CAST_LOG(Verbose, "Activé (clé %s), incantation %.2fs%s", *ActivationInfo.GetActivationPredictionKey().ToString(), CastTime, bFeedable ? TEXT(", nourrissable") : TEXT(""));

	FedCount = 0;
	FedVisualCount = 0;
	FeedSlotsAtPress = 0;
	ServerFeedElapsed = 0.f;
	ReportedFedCount = INDEX_NONE;
	bInterruptWatchStarted = false;
	bServerShotLocked = false;
	bReleased = false;
	PendingRelease = FGenCastRelease();

	// Un seul sort incanté à la fois : celui-ci remplace l'incantation en cours (un sort déjà parti continue)
	CancelOtherPendingCasts();

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

void UGenGA_Cast::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (IsActive())
	{
		GEN_CAST_LOG(Verbose, "Fin (annulé=%d, répliqué=%d)", bWasCancelled, bReplicateEndAbility);
	}

	// Annulation en plein nourrissage ou incantation (étourdi, mort, autre sort...) : on nettoie.
	// La ressource nourrie n'est dépensée qu'au lancer : rien à rendre.
	StopCasting();
	SetCastLock(false);

	bServerShotLocked = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGenGA_Cast::CanBeCanceled() const
{
	// Serveur, départ différé (visée reçue en avance) : le client a déjà lancé. La boule de feu relancée
	// par son clic maintenu (ou tout autre sort lancé après) arrive juste derrière la visée et ne doit
	// pas annuler ce sort. La mort est gérée au départ (OnServerLaunchDelayFinished).
	return !bServerShotLocked && Super::CanBeCanceled();
}

bool UGenGA_Cast::IsServerForRemoteClient() const
{
	return CurrentActorInfo && CurrentActorInfo->IsNetAuthority() && !IsLocallyControlled();
}

void UGenGA_Cast::CancelOtherPendingCasts()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	TArray<UGenGA_Cast*, TInlineAllocator<4>> ToCancel;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Handle == CurrentSpecHandle || !Spec.IsActive())
		{
			continue;
		}
		UGenGA_Cast* Other = Cast<UGenGA_Cast>(Spec.GetPrimaryInstance());
		if (Other && Other->IsCastPending())
		{
			ToCancel.Add(Other);
		}
	}

	for (UGenGA_Cast* Other : ToCancel)
	{
		// Sans effet sur un sort verrouillé côté serveur (déjà lancé par le client, CanBeCanceled faux)
		Other->CancelAbility(Other->GetCurrentAbilitySpecHandle(), Other->GetCurrentActorInfo(), Other->GetCurrentActivationInfo(), true);
	}
}

void UGenGA_Cast::ApplyReportedFedCount(int32 Reported)
{
	if (!bFeedable || !IsServerForRemoteClient())
	{
		return;
	}

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;

	// À ±1 de l'estimation du serveur, borné à la ressource et aux unités disponibles à l'appui (crans de la barre)
	const int32 Accepted = GenFeeding::ClampReportedFed(Reported, FedCount, FMath::Min(MaxFeed, FeedSlotsAtPress), Available);
	if (Accepted != Reported)
	{
		GEN_CAST_LOG(Verbose, "Compte annoncé par le client borné : %d -> %d (estimation %d, ressource %.0f)", Reported, Accepted, FedCount, Available);
	}

	// On garde l'annonce brute : si l'estimation avance encore, elle est rebornée au tick suivant
	ReportedFedCount = Reported;
	SetFedVisual(Accepted);

	// L'annonce (RPC du personnage) et le signal de fin (RPC de l'ASC) n'ont pas d'ordre garanti :
	// arrivée après la fin du nourrissage, elle corrige la barre vue par les autres joueurs
	if (!bIsFeeding)
	{
		MarkFeedEnded(Accepted);
	}
}

int32 UGenGA_Cast::GetAvailableFeed() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;
	return GenFeeding::GetFeedLimit(MaxFeed, Available);
}

void UGenGA_Cast::StartFeeding()
{
	bIsFeeding = true;
	FeedStartTime = GetWorld()->GetTimeSeconds();
	// Unités disponibles à l'appui : autant de crans sur la barre, et jamais plus d'unités nourries
	// (une flamme régénérée pendant l'appui ne s'ajoute pas)
	FeedSlotsAtPress = GetAvailableFeed();

	ApplyCastSlow();
	StartInterruptWatch();

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		// Une seule barre de l'appui au lancer : un cran par unité disponible, repliée à la fin du
		// nourrissage (OnFeedSynced) puis prolongée par l'incantation sans redémarrer
		Character->StartFeedCast(GetClass(), FeedSlotsAtPress, FeedInterval, CastTime, CastFX, CastFXSocket);
	}

	if (IsLocallyControlled())
	{
		if (FeedSlotsAtPress == 0)
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

void UGenGA_Cast::ScheduleFeedTick()
{
	// Calé sur le début du nourrissage : les retards des ticks ne s'additionnent pas
	const float Delay = GenFeeding::GetNextFeedTickDelay(FeedStartTime, FedCount, FeedInterval, GetWorld()->GetTimeSeconds());
	FeedTickTask = UAbilityTask_WaitDelay::WaitDelay(this, Delay);
	FeedTickTask->OnFinish.AddDynamic(this, &ThisClass::OnFeedTick);
	FeedTickTask->ReadyForActivation();
}

void UGenGA_Cast::OnFeedTick()
{
	if (!bIsFeeding)
	{
		return;
	}

	const int32 Limit = FMath::Min(GetAvailableFeed(), FeedSlotsAtPress);
	if (FedCount < Limit)
	{
		++FedCount;

		// Serveur : le compte annoncé par le client (ServerReportFedResource) a pu arriver avant ce
		// tick de l'estimation : c'est lui qui fait foi pour l'affichage (reborné à la nouvelle estimation)
		if (ReportedFedCount != INDEX_NONE)
		{
			ApplyReportedFedCount(ReportedFedCount);
		}
		else
		{
			SetFedVisual(FedCount);
		}
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

void UGenGA_Cast::OnFeedInputReleased(float TimeHeld)
{
	if (bIsFeeding)
	{
		StopFeedingLocal();
	}
}

void UGenGA_Cast::StopFeedingLocal()
{
	if (!bIsFeeding)
	{
		return;
	}

	EndFeedTasks();

	// Client distant : le serveur n'a qu'une estimation des unités nourries (son minuteur peut avoir un
	// tick de retard, surtout quand le client s'arrête pile au maximum). On lui annonce le compte exact
	// pour que les autres joueurs voient le bon nombre de flammes quitter l'orbite pendant l'incantation.
	// Envoyé même à 0 : l'estimation du serveur peut déjà en être à 1.
	if (CurrentActorInfo && !CurrentActorInfo->IsNetAuthority())
	{
		if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
		{
			Character->ServerReportFedResource(GetClass(), static_cast<uint8>(FMath::Clamp(FedCount, 0, 255)));
		}
	}

	// Client : envoie le signal au serveur et continue sans attendre. Hôte : se termine aussitôt.
	UAbilityTask_NetworkSyncPoint* SyncTask = UAbilityTask_NetworkSyncPoint::WaitNetSync(this, EAbilityTaskNetSyncType::OnlyServerWait);
	SyncTask->OnSync.AddDynamic(this, &ThisClass::OnFeedSynced);
	SyncTask->ReadyForActivation();
}

void UGenGA_Cast::OnFeedSynced()
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

	// Serveur pour un client distant : FedCount n'est que son estimation, le compte validé est
	// journalisé au lancer (ResolveFedCount)
	GEN_CAST_LOG(Verbose, "Nourrissage terminé%s : %d (%.2fs)", IsServerForRemoteClient() ? TEXT(" (estimation du serveur)") : TEXT(""),
		FedCount, GetWorld()->GetTimeSeconds() - FeedStartTime);

	// Barre : les segments inutilisés se replient. Compte affiché = unités qui quittent l'orbite
	// (serveur pour un client distant : son estimation, ou l'annonce du client si elle est déjà arrivée).
	// L'annonce du client arrivée après coup corrige la barre (ApplyReportedFedCount).
	MarkFeedEnded(FedVisualCount);

	if (CastTime > 0.f)
	{
		StartCasting();
	}
	else
	{
		OnCastFinished();
	}
}

void UGenGA_Cast::EndFeedTasks()
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

void UGenGA_Cast::MarkFeedEnded(int32 Count)
{
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->MarkFeedEnded(GetClass(), Count); // sans effet si la barre n'est plus la nôtre
	}
}

void UGenGA_Cast::SetFedVisual(int32 Count)
{
	FedVisualCount = Count;
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->SetFedResource(this, static_cast<uint8>(FMath::Clamp(Count, 0, 255)));
	}
}

void UGenGA_Cast::StartCasting()
{
	// Sort nourri : la barre du nourrissage continue (StartFeedCast), jamais de seconde barre
	if (!bFeedable)
	{
		if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
		{
			Character->StartCast(GetClass(), CastTime, CastFX, CastFXSocket);
		}
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

	CastStartTime = GetWorld()->GetTimeSeconds();

	if (IsServerForRemoteClient())
	{
		// Serveur pour un client distant : pas de minuteur séparé. Le serveur démarre l'incantation une
		// latence après le client, donc un minuteur finissait à peu près quand la visée arrivait ; la boule
		// de feu relancée par le clic maintenu, envoyée juste après, annulait alors un sort déjà lancé côté
		// client. On écoute la visée dès maintenant : c'est elle qui termine l'incantation (OnServerAimReceived).
		UGenAbilityTask_TargetDataUnderCursor* AimTask = UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(this);
		AimTask->ValidData.AddDynamic(this, &ThisClass::OnServerAimReceived);
		AimTask->ReadyForActivation();
		return;
	}

	UAbilityTask_WaitDelay* CastTask = UAbilityTask_WaitDelay::WaitDelay(this, CastTime);
	CastTask->OnFinish.AddDynamic(this, &ThisClass::OnCastFinished);
	CastTask->ReadyForActivation();
}

void UGenGA_Cast::OnServerAimReceived(const FGameplayAbilityTargetDataHandle& DataHandle)
{
	// Le client a lancé à la fin de SON incantation. Les RPC du joueur arrivent dans l'ordre : un nouvel
	// appui qui annule ce sort côté client arrive avant toute visée, un sort lancé après arrive
	// après. À partir d'ici, un autre sort du joueur ne peut donc plus annuler ce lancer.
	bServerShotLocked = true;

	// Le client a fini d'incanter : ralenti et barre de cast s'arrêtent maintenant (déplacements cohérents)
	EndCastPresentation();

	const float Elapsed = GetWorld()->GetTimeSeconds() - CastStartTime;
	const float Wait = GenFeeding::GetServerCastWait(CastTime, Elapsed, GenFeeding::CastTimeTolerance);
	if (Wait <= 0.f)
	{
		// Cas normal : le sort part pendant le RPC de la visée, avant tout sort envoyé après
		GEN_CAST_LOG(Verbose, "Visée reçue après %.3fs d'incantation (%.2fs demandées)", Elapsed, CastTime);
		OnTargetDataReady(DataHandle);
		return;
	}

	// Visée trop tôt (triche, ou activation retardée par une perte de paquet) : le lancer a lieu maintenant,
	// dans la fenêtre de prédiction de la visée comme chez le client (cooldown, coût, flammes : pas de
	// correction visible), mais le départ n'a lieu qu'à CastTime - tolérance. Le sort reste actif
	// d'ici là : un client ne peut ni sauter l'incantation ni enchaîner les sorts plus vite.
	// Limite connue : un sort qui applique des effets au lanceur au départ (fenêtre de contre...) les
	// applique hors prédiction dans ce cas rare (léger saut visuel chez le client).
	GEN_CAST_LOG(Log, "Visée en avance : %.3fs d'incantation sur %.2fs, départ différé de %.3fs", Elapsed, CastTime, Wait);
	if (!ReleaseCast(DataHandle, PendingRelease))
	{
		return; // sort déjà terminé (visée invalide, CommitAbility refusé)
	}

	UAbilityTask_WaitDelay* WaitTask = UAbilityTask_WaitDelay::WaitDelay(this, Wait);
	WaitTask->OnFinish.AddDynamic(this, &ThisClass::OnServerLaunchDelayFinished);
	WaitTask->ReadyForActivation();
}

void UGenGA_Cast::OnServerLaunchDelayFinished()
{
	// Mort pendant l'attente : rien ne part (le verrou a empêché CancelAllAbilities de couper le sort)
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ASC->HasMatchingGameplayTag(GenGameplayTags::State_Dead))
	{
		GEN_CAST_LOG(Verbose, "Départ différé abandonné (mort)");
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	LaunchCast(PendingRelease);
}

void UGenGA_Cast::ApplyCastSlow()
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

void UGenGA_Cast::StartInterruptWatch()
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

void UGenGA_Cast::EndCastPresentation()
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

void UGenGA_Cast::StopCasting()
{
	bIsFeeding = false;
	EndFeedTasks();

	// Toutes les écritures de l'affichage (estimation, compte annoncé par le client) passent par SetFedVisual
	if (FedVisualCount > 0)
	{
		SetFedVisual(0);
	}

	EndCastPresentation();
}

void UGenGA_Cast::OnCastInterrupted()
{
	if (bServerShotLocked)
	{
		// Serveur : étourdi après la visée. Le client a déjà lancé et le coût est payé ; seul le départ
		// attendait la fin de l'incantation mesurée par le serveur. Il a lieu quand même.
		GEN_CAST_LOG(Verbose, "Étourdi après le lancer : le départ différé a lieu quand même");
		return;
	}

	if (!IsInterruptedByHardCC())
	{
		GEN_CAST_LOG(Verbose, "Étourdi pendant une phase non interruptible : ignoré");
		return;
	}

	GEN_CAST_LOG(Verbose, "Incantation interrompue (étourdi)");
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void UGenGA_Cast::OnCastFinished()
{
	GEN_CAST_LOG(Verbose, "Incantation terminée, attente de la visée (nourri : %d)", FedCount);
	EndCastPresentation();

	// Visée lue maintenant : le joueur peut ajuster pendant toute l'incantation
	UGenAbilityTask_TargetDataUnderCursor* TargetTask = UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(this, static_cast<uint8>(FMath::Clamp(FedCount, 0, 255)));
	TargetTask->ValidData.AddDynamic(this, &ThisClass::OnTargetDataReady);
	TargetTask->ReadyForActivation();
}

int32 UGenGA_Cast::ResolveFedCount(const FGameplayAbilityTargetData* Data) const
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

	// Jamais plus d'unités que de crans sur la barre (unités disponibles à l'appui)
	const int32 FeedCap = FMath::Min(MaxFeed, FeedSlotsAtPress);

	// Serveur pour un client distant : le client ne peut annoncer ni plus que ce qu'il a, ni plus que le temps écoulé
	if (IsServerForRemoteClient())
	{
		const int32 Validated = GenFeeding::ValidateFedCount(Reported, FeedCap, Available, ServerFeedElapsed, FeedInterval);
		if (Validated != Reported)
		{
			GEN_CAST_LOG(Warning, "Nourrissage corrigé par le serveur : %d -> %d (ressource %.0f, %.2fs)", Reported, Validated, Available, ServerFeedElapsed);
		}
		else
		{
			GEN_CAST_LOG(Verbose, "Nourrissage validé : %d (estimation du serveur %d, %.2fs)", Validated, FedCount, ServerFeedElapsed);
		}
		return Validated;
	}

	return FMath::Min(Reported, GenFeeding::GetFeedLimit(FeedCap, Available));
}

void UGenGA_Cast::SpendResource(int32 Amount)
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

void UGenGA_Cast::OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle)
{
	FGenCastRelease Release;
	if (ReleaseCast(DataHandle, Release))
	{
		LaunchCast(Release);
	}
}

bool UGenGA_Cast::ReleaseCast(const FGameplayAbilityTargetDataHandle& DataHandle, FGenCastRelease& OutRelease)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const FGameplayAbilityTargetData* Data = DataHandle.Get(0);
	const FHitResult* Hit = Data ? Data->GetHitResult() : nullptr;

	if (!Avatar || !Hit)
	{
		GEN_CAST_LOG(Warning, "Visée invalide (avatar=%d, hit=%d)", Avatar != nullptr, Hit != nullptr);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return false;
	}

	const int32 Fed = ResolveFedCount(Data);

	// Le sort part : on applique cooldown, coût et dépense de la ressource maintenant, pour qu'une
	// incantation interrompue (annulée, étourdi, mort) ne coûte rien. Le client est dans la fenêtre
	// de prédiction ouverte par la tâche de visée, le serveur dans celle de la clé reçue.
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		GEN_CAST_LOG(Verbose, "CommitAbility a échoué au lancer");
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return false;
	}

	SpendResource(Fed);
	SetFedVisual(0);
	bReleased = true;

	GEN_CAST_LOG(Verbose, "Visée reçue : %s, nourri : %d", *Hit->Location.ToCompactString(), Fed);

	OutRelease.AimLocation = Hit->Location;
	OutRelease.AimDirection = (Hit->Location - Avatar->GetActorLocation()).GetSafeNormal2D();
	if (OutRelease.AimDirection.IsNearlyZero())
	{
		OutRelease.AimDirection = Avatar->GetActorForwardVector().GetSafeNormal2D();
	}
	OutRelease.Fed = Fed;

	// Se tourner vers la cible (client et serveur, pour que la prédiction concorde)
	if (bTurnToAim)
	{
		Avatar->SetActorRotation(OutRelease.AimDirection.Rotation());
	}

	if (CastMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, CastMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false);
		MontageTask->ReadyForActivation();
	}

	return true;
}

void UGenGA_Cast::LaunchCast(const FGenCastRelease& Release)
{
	OnCastLaunched(Release);

	// Le sort est parti : s'il reste actif (fenêtre de contre, bond...), la mort et les autres sorts
	// peuvent de nouveau l'annuler côté serveur
	bServerShotLocked = false;
}

void UGenGA_Cast::OnCastLaunched(const FGenCastRelease& Release)
{
	FinishAbility();
}

void UGenGA_Cast::FinishAbility()
{
	if (!IsActive())
	{
		return;
	}

	// Le client ne réplique PAS la fin du sort : un EndAbility répliqué pourrait arriver au serveur
	// avant qu'il ait traité la visée. C'est le serveur qui termine de son côté et prévient le client.
	const bool bReplicateEnd = CurrentActorInfo && CurrentActorInfo->IsNetAuthority();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicateEnd, false);
}

void UGenGA_Cast::SetCastLock(bool bLocked)
{
	if (bCastLockApplied == bLocked)
	{
		return;
	}

	// Tag local (non répliqué) : le serveur et le client exécutent tous deux le sort et le posent chacun
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (bLocked)
		{
			ASC->AddLooseGameplayTag(GenGameplayTags::State_CastLocked);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(GenGameplayTags::State_CastLocked);
		}
		bCastLockApplied = bLocked;
	}
}

FGameplayEffectSpecHandle UGenGA_Cast::MakeDamageSpec(TSubclassOf<UGameplayEffect> EffectClass, float Amount, UObject* SourceObject) const
{
	if (!EffectClass || Amount <= 0.f)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(EffectClass, GetAbilityLevel());
	if (Spec.IsValid())
	{
		if (SourceObject)
		{
			Spec.Data->GetContext().AddSourceObject(SourceObject);
		}
		Spec.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Damage, Amount);
	}
	return Spec;
}

FGameplayEffectSpecHandle UGenGA_Cast::MakeGainSpec(float Energy, float Resource) const
{
	if (Energy <= 0.f && Resource <= 0.f)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UGenGE_Gain::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		UGenGE_Gain::SetMagnitudes(*Spec.Data, Energy, Resource);
	}
	return Spec;
}

AGenProjectile* UGenGA_Cast::SpawnProjectileShot(TSubclassOf<AGenProjectile> ShotClass, const FVector& Origin, const FVector& Direction, const FGenProjectileShotParams& ShotParams,
	TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain, float ResourceGain, const TSharedPtr<FGenProjectileSalvo>& Salvo)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || !ShotClass)
	{
		return nullptr;
	}

	FVector Forward = Direction.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = Avatar->GetActorForwardVector();
	}

	const FTransform SpawnTransform(Forward.Rotation(), Origin);
	AGenProjectile* Projectile = GetWorld()->SpawnActorDeferred<AGenProjectile>(
		ShotClass, SpawnTransform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Projectile)
	{
		GEN_CAST_LOG(Warning, "Échec du spawn de %s", *GetNameSafe(ShotClass));
		return nullptr;
	}

	Projectile->InitializeShot(ShotParams);
	Projectile->Salvo = Salvo;
	Projectile->DamageEffectSpecHandle = MakeDamageSpec(DamageClass, DamageAmount, Projectile);
	Projectile->InstigatorOnHitSpecHandle = MakeGainSpec(EnergyGain, ResourceGain);

	GEN_CAST_LOG(Verbose, "Projectile %s créé en %s (vitesse %.0f, échelle %.2f, zone %.0f, repoussement %.0f, dégâts %.0f%s)",
		*Projectile->GetName(), *Origin.ToCompactString(), ShotParams.Speed, ShotParams.Scale, ShotParams.ExplosionRadius, ShotParams.KnockbackDistance, DamageAmount,
		Salvo ? TEXT(", salve") : TEXT(""));

	Projectile->FinishSpawning(SpawnTransform);
	return Projectile;
}
```

- [ ] **Step 4: Replace `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "ScalableFloat.h"
#include "GenGA_Projectile.generated.h"

class AGenProjectile;
class UGameplayEffect;

/**
 * Sort de projectile tiré vers le curseur (ex : boule de feu, grosse boule de feu).
 * Nourrissage, incantation, visée et coûts : voir UGenGA_Cast.
 * Au départ, le serveur fait apparaître le projectile répliqué, mis à l'échelle par le nourrissage
 * (dégâts, vitesse, taille, explosion de zone, repoussement), porteur du GE de dégâts et des gains
 * du lanceur (énergie, ressource) s'il touche.
 */
UCLASS()
class GEN_API UGenGA_Projectile : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Projectile();

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

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
};
```

- [ ] **Step 5: Replace `Source/Gen/AbilitySystem/Abilities/GenGA_Projectile.cpp`**

```cpp
#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenFeeding.h"
#include "Actors/GenProjectile.h"

UGenGA_Projectile::UGenGA_Projectile()
{
	ProjectileClass = AGenProjectile::StaticClass();
	DamageEffectClass = UGenGE_Damage::StaticClass();
	Damage = FScalableFloat(20.f);
}

void UGenGA_Projectile::OnCastLaunched(const FGenCastRelease& Release)
{
	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		SpawnProjectile(Avatar->GetActorLocation() + Release.AimDirection * 1000.f, Release.Fed);
	}
	FinishAbility();
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

	const float ClassSpeed = ProjectileClass->GetDefaultObject<AGenProjectile>()->GetSpeed();
	FGenProjectileShotParams ShotParams;
	ShotParams.Speed = GenFeeding::ScaleByFeed(ClassSpeed, ClassSpeed * SpeedMultiplierAtMaxFeed, Fed, MaxFeed);
	ShotParams.Scale = GenFeeding::ScaleByFeed(1.f, ScaleAtMaxFeed, Fed, MaxFeed);
	ShotParams.ExplosionRadius = GenFeeding::ReachesThreshold(Fed, ExplosionMinFeed) ? ExplosionRadius : 0.f;
	ShotParams.KnockbackDistance = GenFeeding::ReachesThreshold(Fed, KnockbackMinFeed) ? KnockbackDistance : 0.f;

	const int32 Level = GetAbilityLevel();
	SpawnProjectileShot(ProjectileClass, Origin + Direction * SpawnForwardOffset, Direction, ShotParams,
		DamageEffectClass, Damage.GetValueAtLevel(Level) + DamagePerFeed * Fed, EnergyOnHit + EnergyPerFeed * Fed, ResourceOnHit);
}
```

- [ ] **Step 6: Route the fed report through the base class.** In `GenCharacterBase.cpp`:
  - replace `#include "AbilitySystem/Abilities/GenGA_Projectile.h"` with `#include "AbilitySystem/Abilities/GenGA_Cast.h"`;
  - in `ServerReportFedResource_Implementation`, replace the inner block with:

```cpp
			if (UGenGA_Cast* CastAbility = Cast<UGenGA_Cast>(Spec.GetPrimaryInstance()))
			{
				CastAbility->ApplyReportedFedCount(Count);
			}
```

  - in the comment of `ServerReportFedResource` in `GenCharacterBase.h`, replace `UGenGA_Projectile::ApplyReportedFedCount` with `UGenGA_Cast::ApplyReportedFedCount`.

- [ ] **Step 7: Build, then run the unit tests.**
  - Expected: `Result: Succeeded`, and every test passes.
  - If the build complains about `C4458` (a declaration hides a class member) in a subclass, rename the subclass's local variable. Don't rename the base members.

- [ ] **Step 8: Blueprint smoke check** (editor open, new binaries, Standalone 1 player).
  - Compile `GA_Fireball` and `GA_GreatFireball`. Read back `cast_time`, `feedable`, `max_feed` and `explosion_min_feed` on both CDOs. Expected: the Plan 1 values (0.35/False and 0.5/True/5/3).
  - Fire both in PIE. LMB fires repeatedly; holding RMB for 1 s spends 5 flames.
  - The full regression runs in Task 11.

- [ ] **Step 9: Commit**

```bash
git add Source/Gen/AbilitySystem/Abilities Source/Gen/Character/GenCharacterBase.h Source/Gen/Character/GenCharacterBase.cpp
git commit -m "Extract UGenGA_Cast (feeding, cast, aim, release) from UGenGA_Projectile; a new cast replaces the pending one"
```

---

### Task 6: Ground area actor with a telegraph, and the ground-area ability (Flame Pillar)

**Files:**
- Create: `Source/Gen/Actors/GenGroundArea.h`, `Source/Gen/Actors/GenGroundArea.cpp`
- Create: `Source/Gen/AbilitySystem/Abilities/GenGA_GroundArea.h`, `Source/Gen/AbilitySystem/Abilities/GenGA_GroundArea.cpp`
- Modify: `Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h/.cpp` (adds `SpawnGroundArea`)

**Interfaces:**
- Consumes:
  - `GenAreaRules::{ClampToRange, GetViewerRelation, GetImpactDelay, GetTelegraphFill}` (Task 1);
  - `GenWorldQueries::{HasLineOfSight, FindFloor}` (Task 4);
  - `ResolveIncomingHit`, `ApplyHardCC` (Tasks 2 and 3);
  - `MakeDamageSpec`, `MakeGainSpec`, `FinishAbility` (Task 5);
  - `AGenPlayerController::GetCursorLocationOnPlane`.
- Produces:
  - `struct FGenAreaParams { float Radius; float Delay; float StunDuration; float KnockbackDistance; }`
  - `AGenGroundArea`:
    - `InitializeArea(const FGenAreaParams&, uint8 SourceTeam)` (server, before `FinishSpawning`)
    - `StartPreview(AGenPlayerController*, float Range, float BaseRadius, float RadiusAtMaxFeed, int32 MaxFeed)` (local, before `FinishSpawning`)
    - `DamageEffectSpecHandle`, `InstigatorOnHitSpecHandle`
    - `GetRadius()`, `HasDetonated()`
    - editable: `TelegraphMaterial`, `FillAlpha`, `EnemyFillAlpha`, `BorderAlpha`, `ImpactFX`, `ImpactFXReferenceRadius`, `ImpactSound`, `LingerAfterImpact`
  - `UGenGA_Cast::SpawnGroundArea(TSubclassOf<AGenGroundArea>, const FVector& Center, const FGenAreaParams&, TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain) -> AGenGroundArea*`
  - `UGenGA_GroundArea : UGenGA_Cast`, with properties `AreaClass`, `Range`, `Radius`, `RadiusAtMaxFeed`, `ImpactDelay`, `MinTelegraph`, `Damage`, `DamagePerFeed`, `StunDuration`, `KnockbackDistance`, `EnergyOnHit`, `EnergyPerFeed` and `bShowAimPreview`.
  - Telegraph material parameters (consumed by Task 10's `M_VFX_Telegraph`): `RelationIndex`, `Fill`, `FillAlpha`, `BorderAlpha`, `EnemyPattern`.

- [ ] **Step 1: Create `Source/Gen/Actors/GenGroundArea.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GenAreaRules.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "GenGroundArea.generated.h"

class AGenCharacterBase;
class AGenPlayerController;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class UStaticMeshComponent;

/** Réglages d'une zone, fixés par le sort avant FinishSpawning (serveur). */
struct FGenAreaParams
{
	/** Rayon (cm) : celui du télégraphe ET de la zone touchée (guidelines : le dessin = la hitbox). */
	float Radius = 200.f;
	/** Délai avant l'impact (s). 0 = impact immédiat, sans télégraphe. */
	float Delay = 0.f;
	/** Étourdissement infligé (s). 0 = aucun. */
	float StunDuration = 0.f;
	/** Repoussement depuis le centre (cm). 0 = aucun. */
	float KnockbackDistance = 0.f;
};

/**
 * Zone au sol (A) : télégraphe visible par tous pendant Delay, puis impact sur les ennemis vivants du
 * lanceur dans le rayon, en ligne de vue depuis le centre (pas à travers les murs). Ne déclenche pas les contres.
 * - Serveur : dégâts, étourdissement, repoussement, gains du lanceur s'il touche au moins un ennemi.
 *   L'équipe du lanceur est retenue à l'apparition : la zone reste juste s'il meurt avant l'impact.
 * - Clients : télégraphe aux couleurs du point de vue (soi, allié, ennemi), puis effet d'impact.
 * - Aperçu : un exemplaire local, non répliqué, suit le curseur du lanceur pendant l'incantation.
 */
UCLASS()
class GEN_API AGenGroundArea : public AActor
{
	GENERATED_BODY()

public:
	AGenGroundArea();

	/** Serveur, avant FinishSpawning. */
	void InitializeArea(const FGenAreaParams& Params, uint8 InSourceTeam);

	/** Machine du lanceur, avant FinishSpawning : aperçu local qui suit le curseur (rayon selon le nourrissage). */
	void StartPreview(AGenPlayerController* InController, float InRange, float InBaseRadius, float InRadiusAtMaxFeed, int32 InMaxFeed);

	/** Rempli par le sort avant FinishSpawning (serveur uniquement, non répliqué). */
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

	/** Gains du lanceur, appliqués si la zone touche au moins un ennemi (serveur). */
	FGameplayEffectSpecHandle InstigatorOnHitSpecHandle;

	UFUNCTION(BlueprintPure, Category = "Gen|Area")
	float GetRadius() const { return Radius; }

	UFUNCTION(BlueprintPure, Category = "Gen|Area")
	bool HasDetonated() const { return bDetonated; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	/** Serveur : impact. */
	void Detonate();

	bool IsValidTarget(const AGenCharacterBase* Character) const;
	void ApplyHit(AGenCharacterBase* Target);

	UFUNCTION()
	void OnRep_Detonated();

	/** Cache le télégraphe, joue l'effet et le son d'impact (toutes les machines sauf serveur dédié). */
	void PlayImpactEffects();

	void SetTelegraphRadius(float InRadius);
	void UpdateTelegraphFill();
	void UpdatePreview();
	EGenViewerRelation GetLocalViewerRelation() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Gen|Area", meta = (DisplayName = "On Impact"))
	void K2_OnImpact();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> AreaRoot;

	/** Plan (100 x 100 cm) mis à l'échelle du rayon, matériau M_VFX_Telegraph. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TelegraphMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph")
	TObjectPtr<UMaterialInterface> TelegraphMaterial;

	/** Remplissage, soi et alliés (UI §4.12 : 0.15–0.25). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FillAlpha = 0.2f;

	/** Remplissage vu par un ennemi (avec les hachures fixes du motif ennemi). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyFillAlpha = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BorderAlpha = 0.9f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	/** Rayon pour lequel ImpactFX est dessiné à l'échelle 1 (il suit le rayon réel). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact", meta = (ClampMin = "1.0", Units = "cm"))
	float ImpactFXReferenceRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact")
	TObjectPtr<USoundBase> ImpactSound;

	/** Durée de vie après l'impact (le temps que bDetonated soit répliqué et que l'effet se joue). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact", meta = (ClampMin = "0.1", Units = "s"))
	float LingerAfterImpact = 1.f;

	UPROPERTY(Replicated)
	float Radius = 200.f;

	UPROPERTY(Replicated)
	float Delay = 0.f;

	/** Apparition, en temps serveur (GameState) : sert au remplissage du télégraphe chez les clients. */
	UPROPERTY(Replicated)
	float StartServerTime = 0.f;

	/** Équipe du lanceur à l'apparition. */
	UPROPERTY(Replicated)
	uint8 SourceTeam = 255;

	UPROPERTY(ReplicatedUsing = OnRep_Detonated)
	bool bDetonated = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TelegraphMID;

	/** Serveur uniquement. */
	float StunDuration = 0.f;
	float KnockbackDistance = 0.f;

private:
	bool bIsPreview = false;
	TWeakObjectPtr<AGenPlayerController> PreviewController;
	float PreviewRange = 0.f;
	float PreviewBaseRadius = 0.f;
	float PreviewRadiusAtMaxFeed = 0.f;
	int32 PreviewMaxFeed = 0;
	bool bImpactEffectsPlayed = false;
};
```

- [ ] **Step 2: Create `Source/Gen/Actors/GenGroundArea.cpp`**

```cpp
#include "Actors/GenGroundArea.h"

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/GenCharacterBase.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GenGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Player/GenPlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenGroundArea, Log, All);

namespace
{
	/** Forme de la zone : capsule verticale (rayon = Radius) qui couvre du sol à ~2.5 m. */
	constexpr float ShapeCenterHeight = 100.f;
	constexpr float ShapeExtraHalfHeight = 150.f;
	/** Les lignes de vue partent un peu au-dessus du sol. */
	constexpr float LineOfSightHeight = 50.f;
	/** Le télégraphe flotte juste au-dessus du sol (pas de scintillement). */
	constexpr float TelegraphHeight = 2.f;
	/** Le plan /Engine/BasicShapes/Plane mesure 100 cm : échelle = rayon / 50. */
	constexpr float PlaneHalfSize = 50.f;
}

AGenGroundArea::AGenGroundArea()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	bReplicates = true;

	AreaRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AreaRoot"));
	SetRootComponent(AreaRoot);

	TelegraphMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TelegraphMesh"));
	TelegraphMesh->SetupAttachment(AreaRoot);
	TelegraphMesh->SetRelativeLocation(FVector(0.f, 0.f, TelegraphHeight));
	TelegraphMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TelegraphMesh->SetCastShadow(false);
	TelegraphMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	TelegraphMesh->SetStaticMesh(PlaneMesh.Object);
}

void AGenGroundArea::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AGenGroundArea, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenGroundArea, Delay, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenGroundArea, StartServerTime, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenGroundArea, SourceTeam, COND_InitialOnly);
	DOREPLIFETIME(AGenGroundArea, bDetonated);
}

void AGenGroundArea::InitializeArea(const FGenAreaParams& Params, uint8 InSourceTeam)
{
	Radius = FMath::Max(Params.Radius, 1.f);
	Delay = FMath::Max(Params.Delay, 0.f);
	StunDuration = FMath::Max(Params.StunDuration, 0.f);
	KnockbackDistance = FMath::Max(Params.KnockbackDistance, 0.f);
	SourceTeam = InSourceTeam;

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	StartServerTime = GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
}

void AGenGroundArea::StartPreview(AGenPlayerController* InController, float InRange, float InBaseRadius, float InRadiusAtMaxFeed, int32 InMaxFeed)
{
	// Exemplaire local : jamais répliqué, même sur un hôte (listen server)
	bIsPreview = true;
	SetReplicates(false);
	PreviewController = InController;
	PreviewRange = InRange;
	PreviewBaseRadius = InBaseRadius;
	PreviewRadiusAtMaxFeed = InRadiusAtMaxFeed;
	PreviewMaxFeed = InMaxFeed;
	Radius = InBaseRadius;
}

void AGenGroundArea::BeginPlay()
{
	Super::BeginPlay();

	SetTelegraphRadius(Radius);

	const bool bShowsTelegraph = GetNetMode() != NM_DedicatedServer && !bDetonated && (bIsPreview || Delay > 0.f);
	if (bShowsTelegraph && TelegraphMaterial)
	{
		TelegraphMID = TelegraphMesh->CreateDynamicMaterialInstance(0, TelegraphMaterial);

		const EGenViewerRelation Relation = GetLocalViewerRelation();
		const bool bEnemy = Relation == EGenViewerRelation::Enemy;
		TelegraphMID->SetScalarParameterValue(TEXT("RelationIndex"), static_cast<float>(Relation));
		TelegraphMID->SetScalarParameterValue(TEXT("FillAlpha"), bEnemy ? EnemyFillAlpha : FillAlpha);
		TelegraphMID->SetScalarParameterValue(TEXT("BorderAlpha"), BorderAlpha);
		TelegraphMID->SetScalarParameterValue(TEXT("EnemyPattern"), bEnemy ? 1.f : 0.f);
		TelegraphMID->SetScalarParameterValue(TEXT("Fill"), 0.f);

		TelegraphMesh->SetVisibility(true);
		SetActorTickEnabled(true);
	}

	if (HasAuthority() && !bIsPreview)
	{
		if (Delay > 0.f)
		{
			FTimerHandle ImpactTimer;
			GetWorldTimerManager().SetTimer(ImpactTimer, this, &ThisClass::Detonate, Delay, false);
		}
		else
		{
			Detonate();
		}
	}
}

void AGenGroundArea::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsPreview)
	{
		UpdatePreview();
	}
	else
	{
		UpdateTelegraphFill();
	}
}

EGenViewerRelation AGenGroundArea::GetLocalViewerRelation() const
{
	if (bIsPreview)
	{
		return EGenViewerRelation::Self;
	}

	const APlayerController* LocalController = GetWorld()->GetFirstPlayerController();
	const AGenCharacterBase* Viewer = LocalController ? Cast<AGenCharacterBase>(LocalController->GetPawn()) : nullptr;
	const bool bViewerIsSource = Viewer && Viewer == GetInstigator();
	const uint8 ViewerTeam = Viewer ? Viewer->GetTeamId() : GenNoTeam;
	return GenAreaRules::GetViewerRelation(bViewerIsSource, ViewerTeam, SourceTeam, GenNoTeam);
}

void AGenGroundArea::SetTelegraphRadius(float InRadius)
{
	const float Scale = FMath::Max(InRadius, 1.f) / PlaneHalfSize;
	TelegraphMesh->SetRelativeScale3D(FVector(Scale, Scale, 1.f));
}

void AGenGroundArea::UpdateTelegraphFill()
{
	if (!TelegraphMID)
	{
		return;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float Now = GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
	TelegraphMID->SetScalarParameterValue(TEXT("Fill"), GenAreaRules::GetTelegraphFill(Now - StartServerTime, Delay));
}

void AGenGroundArea::UpdatePreview()
{
	AGenPlayerController* Controller = PreviewController.Get();
	const AGenCharacterBase* Caster = Controller ? Cast<AGenCharacterBase>(Controller->GetPawn()) : nullptr;
	if (!Caster)
	{
		TelegraphMesh->SetVisibility(false);
		return;
	}

	FVector Cursor;
	if (Controller->GetCursorLocationOnPlane(Caster->GetActorLocation().Z, Cursor))
	{
		const FVector Center = GenAreaRules::ClampToRange(Caster->GetActorLocation(), Cursor, PreviewRange);
		SetActorLocation(GenWorldQueries::FindFloor(GetWorld(), Center, { this, Caster }));
	}

	// Le télégraphe grandit avec le nourrissage (affichage prédit du lanceur)
	SetTelegraphRadius(GenFeeding::ScaleByFeed(PreviewBaseRadius, PreviewRadiusAtMaxFeed, Caster->GetFedResource(), PreviewMaxFeed));
}

bool AGenGroundArea::IsValidTarget(const AGenCharacterBase* Character) const
{
	// L'équipe retenue à l'apparition décide : la zone reste juste si le lanceur est mort entre-temps
	return Character && !Character->IsDead() && Character != GetInstigator() && AGenCharacterBase::AreTeamsEnemies(SourceTeam, Character->GetTeamId());
}

void AGenGroundArea::Detonate()
{
	if (bDetonated)
	{
		return;
	}

	const FVector Floor = GetActorLocation();
	const FVector LineOrigin = Floor + FVector(0.f, 0.f, LineOfSightHeight);

	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GenGroundArea), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Floor + FVector(0.f, 0.f, ShapeCenterHeight), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeCapsule(Radius, Radius + ShapeExtraHalfHeight), QueryParams);

	TArray<AGenCharacterBase*> Targets;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AGenCharacterBase* Character = Cast<AGenCharacterBase>(Overlap.GetActor());
		if (!Character || Targets.Contains(Character) || !IsValidTarget(Character))
		{
			continue;
		}

		if (!GenWorldQueries::HasLineOfSight(GetWorld(), LineOrigin, Character, { this, Character }))
		{
			continue; // un mur protège la cible
		}

		// Zone au sol : traverse les contres (la cible décide quand même, ex. intouchable)
		if (Character->ResolveIncomingHit(GetInstigator(), EGenHitKind::Area, this) != EGenHitResponse::Hit)
		{
			continue;
		}

		Targets.Add(Character);
	}

	for (AGenCharacterBase* Target : Targets)
	{
		ApplyHit(Target);
	}

	UE_LOG(LogGenGroundArea, Verbose, TEXT("%s : impact en %s, rayon %.0f, %d cible(s) touchée(s)"), *GetName(), *Floor.ToCompactString(), Radius, Targets.Num());

	if (Targets.Num() > 0 && InstigatorOnHitSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* InstigatorASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetInstigator()))
		{
			InstigatorASC->ApplyGameplayEffectSpecToSelf(*InstigatorOnHitSpecHandle.Data.Get());
		}
	}

	bDetonated = true;
	OnRep_Detonated(); // Les RepNotify ne s'exécutent pas sur le serveur : appel manuel (listen server)
	SetLifeSpan(LingerAfterImpact);
}

void AGenGroundArea::ApplyHit(AGenCharacterBase* Target)
{
	if (DamageEffectSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent())
		{
			const FHitResult HitResult(Target, nullptr, GetActorLocation(), FVector::UpVector);
			DamageEffectSpecHandle.Data->GetContext().AddHitResult(HitResult, true);
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
		}
	}

	if (Target->IsDead())
	{
		return; // tué par l'impact : ni étourdissement ni repoussement sur un corps
	}

	if (StunDuration > 0.f)
	{
		if (UGenAbilitySystemComponent* TargetASC = Target->GetGenAbilitySystemComponent())
		{
			TargetASC->ApplyHardCC(GenGameplayTags::State_Stunned, StunDuration, GetInstigator());
		}
	}

	if (KnockbackDistance > 0.f)
	{
		Target->ApplyKnockback(Target->GetActorLocation() - GetActorLocation(), KnockbackDistance);
	}
}

void AGenGroundArea::OnRep_Detonated()
{
	if (bDetonated)
	{
		PlayImpactEffects();
	}
}

void AGenGroundArea::PlayImpactEffects()
{
	if (bImpactEffectsPlayed)
	{
		return;
	}
	bImpactEffectsPlayed = true;

	SetActorTickEnabled(false);
	TelegraphMesh->SetVisibility(false);

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (ImpactFX)
	{
		// Image 1 de l'impact = zone pleine taille au rayon exact (Art Bible §7.3)
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, GetActorLocation(), FRotator::ZeroRotator, FVector(Radius / ImpactFXReferenceRadius));
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, GetActorLocation());
	}

	K2_OnImpact();
}
```

- [ ] **Step 3: Add `SpawnGroundArea` to `UGenGA_Cast`.**
  - In `GenGA_Cast.h`:
    - add `class AGenGroundArea;` and `struct FGenAreaParams;` to the forward declarations;
    - after `SpawnProjectileShot`, add:

```cpp
	/** Serveur : zone au sol posée au sol sous Center, porteuse des dégâts et des gains (énergie si elle touche). */
	AGenGroundArea* SpawnGroundArea(TSubclassOf<AGenGroundArea> AreaClass, const FVector& Center, const FGenAreaParams& Params,
		TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain);
```

  - In `GenGA_Cast.cpp`, add `#include "AbilitySystem/GenWorldQueries.h"` and `#include "Actors/GenGroundArea.h"`, then append:

```cpp
AGenGroundArea* UGenGA_Cast::SpawnGroundArea(TSubclassOf<AGenGroundArea> AreaClass, const FVector& Center, const FGenAreaParams& Params,
	TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || !AreaClass)
	{
		return nullptr;
	}

	const FTransform SpawnTransform(FRotator::ZeroRotator, GenWorldQueries::FindFloor(GetWorld(), Center, { Avatar }));
	AGenGroundArea* Area = GetWorld()->SpawnActorDeferred<AGenGroundArea>(
		AreaClass, SpawnTransform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Area)
	{
		GEN_CAST_LOG(Warning, "Échec du spawn de %s", *GetNameSafe(AreaClass));
		return nullptr;
	}

	const AGenCharacterBase* Character = GetGenCharacterFromActorInfo();
	Area->InitializeArea(Params, Character ? Character->GetTeamId() : GenNoTeam);
	Area->DamageEffectSpecHandle = MakeDamageSpec(DamageClass, DamageAmount, Area);
	Area->InstigatorOnHitSpecHandle = MakeGainSpec(EnergyGain, 0.f);

	GEN_CAST_LOG(Verbose, "Zone %s en %s (rayon %.0f, délai %.2fs, dégâts %.0f, étourdit %.2fs, repousse %.0f)",
		*Area->GetName(), *SpawnTransform.GetLocation().ToCompactString(), Params.Radius, Params.Delay, DamageAmount, Params.StunDuration, Params.KnockbackDistance);

	Area->FinishSpawning(SpawnTransform);
	return Area;
}
```

- [ ] **Step 4: Create `Source/Gen/AbilitySystem/Abilities/GenGA_GroundArea.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "ScalableFloat.h"
#include "GenGA_GroundArea.generated.h"

class AGenGroundArea;

/**
 * Zone au sol visée (ex : Pilier de flammes). Nourrissage optionnel (rayon), incantation, puis
 * une zone retardée (télégraphe) au point visé, ramené à Range. Le lanceur voit un aperçu local
 * qui suit le curseur et grandit avec le nourrissage.
 */
UCLASS()
class GEN_API UGenGA_GroundArea : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_GroundArea();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

	/** Blueprint de la zone (télégraphe, effet d'impact). */
	UPROPERTY(EditDefaultsOnly, Category = "Area")
	TSubclassOf<AGenGroundArea> AreaClass;

	/** Distance max du centre de la zone au lanceur. */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "0.0", Units = "cm"))
	float Range = 900.f;

	/** Rayon sans nourrissage. */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "1.0", Units = "cm"))
	float Radius = 200.f;

	/** Rayon à MaxFeed (linéaire entre les deux). */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (EditCondition = "bFeedable", ClampMin = "1.0", Units = "cm"))
	float RadiusAtMaxFeed = 350.f;

	/** Délai entre le départ du sort et l'impact (télégraphe). 0 = impact immédiat. */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "0.0", Units = "s"))
	float ImpactDelay = 0.8f;

	/** Un télégraphe ne descend jamais sous cette durée. */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "0.0", Units = "s"))
	float MinTelegraph = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects")
	FScalableFloat Damage;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects", meta = (EditCondition = "bFeedable"))
	float DamagePerFeed = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects", meta = (ClampMin = "0.0", Units = "s"))
	float StunDuration = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects", meta = (ClampMin = "0.0", Units = "cm"))
	float KnockbackDistance = 0.f;

	/** Énergie gagnée par le lanceur si la zone touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Gains")
	float EnergyOnHit = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Gains", meta = (EditCondition = "bFeedable"))
	float EnergyPerFeed = 0.f;

	/** Aperçu de la zone sous le curseur pendant l'incantation (lanceur uniquement). */
	UPROPERTY(EditDefaultsOnly, Category = "Area")
	bool bShowAimPreview = true;

private:
	void DestroyPreview();

	UPROPERTY(Transient)
	TObjectPtr<AGenGroundArea> Preview;
};
```

- [ ] **Step 5: Create `Source/Gen/AbilitySystem/Abilities/GenGA_GroundArea.cpp`**

```cpp
#include "AbilitySystem/Abilities/GenGA_GroundArea.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenFeeding.h"
#include "Actors/GenGroundArea.h"
#include "Engine/World.h"
#include "Player/GenPlayerController.h"

UGenGA_GroundArea::UGenGA_GroundArea()
{
	Damage = FScalableFloat(0.f);
}

void UGenGA_GroundArea::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	DestroyPreview();

	AGenPlayerController* Controller = GetGenPlayerControllerFromActorInfo();
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (bShowAimPreview && AreaClass && Controller && Avatar && IsLocallyControlled())
	{
		const FTransform PreviewTransform(FRotator::ZeroRotator, Avatar->GetActorLocation());
		Preview = GetWorld()->SpawnActorDeferred<AGenGroundArea>(AreaClass, PreviewTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Preview)
		{
			Preview->StartPreview(Controller, Range, Radius, bFeedable ? RadiusAtMaxFeed : Radius, MaxFeed);
			Preview->FinishSpawning(PreviewTransform);
		}
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGenGA_GroundArea::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	DestroyPreview();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGenGA_GroundArea::DestroyPreview()
{
	if (Preview)
	{
		Preview->Destroy();
		Preview = nullptr;
	}
}

void UGenGA_GroundArea::OnCastLaunched(const FGenCastRelease& Release)
{
	DestroyPreview();

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority())
	{
		FGenAreaParams Params;
		Params.Radius = bFeedable ? GenFeeding::ScaleByFeed(Radius, RadiusAtMaxFeed, Release.Fed, MaxFeed) : Radius;
		Params.Delay = GenAreaRules::GetImpactDelay(ImpactDelay, MinTelegraph);
		Params.StunDuration = StunDuration;
		Params.KnockbackDistance = KnockbackDistance;

		const int32 Level = GetAbilityLevel();
		const FVector Center = GenAreaRules::ClampToRange(Avatar->GetActorLocation(), Release.AimLocation, Range);
		SpawnGroundArea(AreaClass, Center, Params, UGenGE_Damage::StaticClass(),
			Damage.GetValueAtLevel(Level) + DamagePerFeed * Release.Fed, EnergyOnHit + EnergyPerFeed * Release.Fed);
	}

	FinishAbility();
}
```

- [ ] **Step 6: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes.

- [ ] **Step 7: Commit**

```bash
git add Source/Gen/Actors/GenGroundArea.h Source/Gen/Actors/GenGroundArea.cpp Source/Gen/AbilitySystem/Abilities/GenGA_GroundArea.h Source/Gen/AbilitySystem/Abilities/GenGA_GroundArea.cpp Source/Gen/AbilitySystem/Abilities/GenGA_Cast.h Source/Gen/AbilitySystem/Abilities/GenGA_Cast.cpp
git commit -m "Delayed ground area with team-relative telegraph and aim preview; ground-area ability"
```

---

### Task 7: Counter ability (Backfire) and Curffe's tags

**Files:**
- Create: `Source/Gen/AbilitySystem/Abilities/GenGA_Counter.h`, `Source/Gen/AbilitySystem/Abilities/GenGA_Counter.cpp`
- Create: `Source/Gen/Champions/Curffe/CurffeGameplayTags.h`, `Source/Gen/Champions/Curffe/CurffeGameplayTags.cpp`

**Interfaces:**
- Consumes:
  - `UGenGA_Cast::{OnCastLaunched, FinishAbility, MakeGainSpec}` (Task 5);
  - `UGenGE_TimedMoveSpeed::SetMagnitudes` and `State_Countering` (Task 2);
  - `Event_Counter_Blocked` and the payload contract from `ResolveIncomingHit` (Task 3);
  - `GenHitRules::GetCounterReward` (Task 1);
  - `AGenCharacterBase::ApplyKnockback`.
- Produces:
  - `UGenGA_Counter : UGenGA_Cast`, with properties `CounterWindow`, `WindowMoveSpeedMultiplier`, `ResourcePerBlock`, `EnergyOnFirstBlock`, `MeleeKnockbackDistance` and `BlockCueTag`.
  - Behaviour:
    - at launch, `State.Countering` and the window slow come from one timed GE;
    - the server rewards each block;
    - the window ends after `CounterWindow`, at death, on a stun, or when the player activates another ability (a fresh press; auto-repeat is gated by `State.Casting`).
  - `CurffeGameplayTags::{Ability_Backfire, Cooldown_Ability_Backfire, Ability_FlamePillar, Cooldown_Ability_FlamePillar, GameplayCue_Backfire_Block}`.

- [ ] **Step 1: Create `Source/Gen/Champions/Curffe/CurffeGameplayTags.h`**

```cpp
#pragma once

#include "NativeGameplayTags.h"

/** Tags propres à Curffe (sorts, recharges, effets). Les tags génériques sont dans GenGameplayTags. */
namespace CurffeGameplayTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Backfire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_FlamePillar);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_Backfire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_FlamePillar);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Backfire_Block);
}
```

- [ ] **Step 2: Create `Source/Gen/Champions/Curffe/CurffeGameplayTags.cpp`**

```cpp
#include "Champions/Curffe/CurffeGameplayTags.h"

namespace CurffeGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Backfire, "Ability.Backfire", "Sort : retour de flamme (contre de Curffe)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_FlamePillar, "Ability.FlamePillar", "Sort : pilier de flammes (zone retardee, etourdit)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_Backfire, "Cooldown.Ability.Backfire", "Recharge du retour de flamme");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_FlamePillar, "Cooldown.Ability.FlamePillar", "Recharge du pilier de flammes");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Backfire_Block, "GameplayCue.Curffe.Backfire.Block", "Retour de flamme : un coup bloque (burst)");
}
```

- [ ] **Step 3: Create `Source/Gen/AbilitySystem/Abilities/GenGA_Counter.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "GenGA_Counter.generated.h"

/**
 * Posture de contre (guidelines §3.4) : après une courte incantation, le lanceur bloque les projectiles
 * et la mêlée pendant CounterWindow (les zones au sol passent), ralenti pendant la fenêtre.
 * Chaque coup bloqué (Event.Counter.Blocked, serveur) rapporte ResourcePerBlock, l'énergie une fois par
 * incantation, et repousse un attaquant au corps à corps. La fenêtre se termine à la fin du délai, à la
 * mort, sur un étourdissement ou quand le joueur lance un autre sort.
 */
UCLASS()
class GEN_API UGenGA_Counter : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Counter();

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Serveur : un coup vient d'être bloqué. */
	UFUNCTION()
	void OnBlocked(FGameplayEventData Payload);

	UFUNCTION()
	void OnWindowFinished();

	/** Un autre sort du joueur vient d'être activé : la posture se termine. */
	void OnAbilityActivated(UGameplayAbility* ActivatedAbility);

	/** Durée de la posture (après l'incantation). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter", meta = (ClampMin = "0.1", Units = "s"))
	float CounterWindow = 1.2f;

	/** Vitesse pendant la posture (guidelines §3.4 : un contre gâché coûte quelque chose). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindowMoveSpeedMultiplier = 0.5f;

	/** Ressource gagnée à chaque coup bloqué (Curffe : flammes). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter|Rewards")
	float ResourcePerBlock = 0.f;

	/** Énergie gagnée au premier coup bloqué de l'incantation (guidelines §4.1 : +10). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter|Rewards")
	float EnergyOnFirstBlock = 10.f;

	/** Repoussement d'un attaquant au corps à corps bloqué (cm). 0 = aucun. */
	UPROPERTY(EditDefaultsOnly, Category = "Counter|Rewards", meta = (ClampMin = "0.0", Units = "cm"))
	float MeleeKnockbackDistance = 0.f;

	/** Effet joué à chaque blocage (GameplayCue exécutée sur le lanceur, vue par tous). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter", meta = (Categories = "GameplayCue"))
	FGameplayTag BlockCueTag;

private:
	FActiveGameplayEffectHandle WindowEffectHandle;
	FDelegateHandle AbilityActivatedHandle;
	int32 BlockCount = 0;
};
```

- [ ] **Step 4: Create `Source/Gen/AbilitySystem/Abilities/GenGA_Counter.cpp`**

```cpp
#include "AbilitySystem/Abilities/GenGA_Counter.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystemComponent.h"
#include "Character/GenCharacterBase.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenCounter, Log, All);

UGenGA_Counter::UGenGA_Counter()
{
	CastTime = 0.1f;
	bTurnToAim = false;
}

void UGenGA_Counter::OnCastLaunched(const FGenCastRelease& Release)
{
	BlockCount = 0;

	// Un seul effet porte la posture (State.Countering, vu par tous) et le ralenti de la fenêtre
	FGameplayEffectSpecHandle WindowSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedMoveSpeed::StaticClass(), GetAbilityLevel());
	if (WindowSpec.IsValid())
	{
		UGenGE_TimedMoveSpeed::SetMagnitudes(*WindowSpec.Data, CounterWindow, WindowMoveSpeedMultiplier, FGameplayTagContainer(GenGameplayTags::State_Countering));
		WindowEffectHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, WindowSpec);
	}

	// Seul le serveur résout les coups : il reçoit les blocages
	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		UAbilityTask_WaitGameplayEvent* BlockTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, GenGameplayTags::Event_Counter_Blocked);
		BlockTask->EventReceived.AddDynamic(this, &ThisClass::OnBlocked);
		BlockTask->ReadyForActivation();
	}

	UAbilityTask_WaitDelay* WindowTask = UAbilityTask_WaitDelay::WaitDelay(this, CounterWindow);
	WindowTask->OnFinish.AddDynamic(this, &ThisClass::OnWindowFinished);
	WindowTask->ReadyForActivation();

	// Lancer un autre sort termine la posture (le clic maintenu ne relance rien : State.Casting)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		AbilityActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::OnAbilityActivated);
	}

	UE_LOG(LogGenCounter, Verbose, TEXT("%s : posture de contre (%.2fs)"), *GetName(), CounterWindow);
}

void UGenGA_Counter::OnBlocked(FGameplayEventData Payload)
{
	++BlockCount;

	const GenHitRules::FCounterReward Reward = GenHitRules::GetCounterReward(BlockCount, ResourcePerBlock, EnergyOnFirstBlock);
	const FGameplayEffectSpecHandle GainSpec = MakeGainSpec(Reward.Energy, Reward.Resource);
	if (GainSpec.IsValid())
	{
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, GainSpec);
	}

	// Mêlée bloquée : l'attaquant est repoussé
	const EGenHitKind Kind = static_cast<EGenHitKind>(FMath::RoundToInt(Payload.EventMagnitude));
	const AGenCharacterBase* Self = GetGenCharacterFromActorInfo();
	if (Kind == EGenHitKind::Melee && MeleeKnockbackDistance > 0.f && Self)
	{
		if (AGenCharacterBase* Attacker = const_cast<AGenCharacterBase*>(Cast<AGenCharacterBase>(Payload.Instigator.Get())))
		{
			Attacker->ApplyKnockback(Attacker->GetActorLocation() - Self->GetActorLocation(), MeleeKnockbackDistance);
		}
	}

	if (BlockCueTag.IsValid())
	{
		K2_ExecuteGameplayCue(BlockCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo));
	}

	UE_LOG(LogGenCounter, Verbose, TEXT("%s : coup bloqué n°%d (%s), +%.0f ressource, +%.0f énergie"), *GetName(), BlockCount,
		*GetNameSafe(Payload.Instigator.Get()), Reward.Resource, Reward.Energy);
}

void UGenGA_Counter::OnWindowFinished()
{
	FinishAbility();
}

void UGenGA_Counter::OnAbilityActivated(UGameplayAbility* ActivatedAbility)
{
	if (ActivatedAbility != this && IsActive())
	{
		UE_LOG(LogGenCounter, Verbose, TEXT("%s : posture terminée par %s"), *GetName(), *GetNameSafe(ActivatedAbility));
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}

void UGenGA_Counter::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AbilityActivatedCallbacks.Remove(AbilityActivatedHandle);
	}
	AbilityActivatedHandle.Reset();

	// Fin anticipée (autre sort, étourdi, mort) : la posture et le ralenti s'arrêtent avec le sort
	if (WindowEffectHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(WindowEffectHandle);
		WindowEffectHandle.Invalidate();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
```

- [ ] **Step 5: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes.

- [ ] **Step 6: Commit**

```bash
git add Source/Gen/AbilitySystem/Abilities/GenGA_Counter.h Source/Gen/AbilitySystem/Abilities/GenGA_Counter.cpp Source/Gen/Champions/Curffe/CurffeGameplayTags.h Source/Gen/Champions/Curffe/CurffeGameplayTags.cpp
git commit -m "Counter stance ability (Backfire) and Curffe's native tags"
```

---

### Task 8: Feedable leap and Meteor Leap's ring of Fireballs

**Files:**
- Create: `Source/Gen/AbilitySystem/Abilities/GenGA_Leap.h`, `Source/Gen/AbilitySystem/Abilities/GenGA_Leap.cpp`
- Create: `Source/Gen/Champions/Curffe/CurffeGA_MeteorLeap.h`, `Source/Gen/Champions/Curffe/CurffeGA_MeteorLeap.cpp`

**Interfaces:**
- Consumes:
  - `UGenGA_Cast::{OnCastLaunched, IsInterruptedByHardCC, FinishAbility, SetCastLock, SpawnGroundArea, SpawnProjectileShot}` (Tasks 5 and 6);
  - `GenAreaRules::{ClampToRange, GetRingDirections}` (Task 1);
  - `GenWorldQueries::FindWallHit` (Task 4);
  - `FGenProjectileSalvo`;
  - engine `UAbilityTask_ApplyRootMotionJumpForce::ApplyRootMotionJumpForce(UGameplayAbility*, FName, FRotator, float Distance, float Height, float Duration, float MinimumLandedTriggerTime, bool bFinishOnLanded, ERootMotionFinishVelocityMode, FVector SetVelocityOnFinish, float ClampVelocityOnFinish, UCurveVector*, UCurveFloat*)`, which provides `OnLanded` and `OnFinish`.
- Produces:
  - `UGenGA_Leap : UGenGA_Cast`:
    - properties `MaxDistance`, `LeapHeight`, `LeapDuration`, `LandingAreaClass`, `LandingRadius`, `LandingDamage`, `LandingEnergyOnHit`, `LandingKnockback`, `TrailCueTag`, `ImpactCueTag` and `LandMontage`;
    - virtual `OnLeapLanded(const FGenCastRelease&, const FVector& LandingLocation)`;
    - not interrupted by a stun while airborne;
    - `State.CastLocked` in flight.
  - `UCurffeGA_MeteorLeap : UGenGA_Leap`:
    - properties `RingProjectileClass`, `RingDamage`, `RingEnergyOnHit` and `RingSpawnOffset`;
    - on landing (server), `Release.Fed` Fireballs fly out in an even ring, sharing one salvo.

- [ ] **Step 1: Create `Source/Gen/AbilitySystem/Abilities/GenGA_Leap.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "GenGA_Leap.generated.h"

class AGenGroundArea;
class UAnimMontage;

/**
 * Bond visible (guidelines §3.6) vers le curseur, ramené à MaxDistance. Nourrissable : le décollage
 * (CastTime + nourrissage) s'allonge, la sous-classe utilise les unités nourries à l'atterrissage.
 * Pendant le vol : aucun sort (State.CastLocked), un étourdissement ne coupe pas le bond.
 * Atterrissage : zone de dégâts (A) autour du point d'impact.
 */
UCLASS()
class GEN_API UGenGA_Leap : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Leap();

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual bool IsInterruptedByHardCC() const override { return !bAirborne; }

	/** Atterrissage (serveur et client). Par défaut : montage, effet, zone d'atterrissage (serveur). */
	virtual void OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation);

	/** Fin du bond : posé au sol, ou durée écoulée. */
	UFUNCTION()
	void OnLanded();

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistance = 700.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float LeapHeight = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.1", Units = "s"))
	float LeapDuration = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing")
	TSubclassOf<AGenGroundArea> LandingAreaClass;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing", meta = (ClampMin = "1.0", Units = "cm"))
	float LandingRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing")
	float LandingDamage = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing")
	float LandingEnergyOnHit = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing", meta = (ClampMin = "0.0", Units = "cm"))
	float LandingKnockback = 0.f;

	/** Effet en boucle pendant le vol (retiré à la fin du sort). */
	UPROPERTY(EditDefaultsOnly, Category = "Leap|FX", meta = (Categories = "GameplayCue"))
	FGameplayTag TrailCueTag;

	/** Effet à l'atterrissage. */
	UPROPERTY(EditDefaultsOnly, Category = "Leap|FX", meta = (Categories = "GameplayCue"))
	FGameplayTag ImpactCueTag;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|FX")
	TObjectPtr<UAnimMontage> LandMontage;

private:
	FGenCastRelease LeapRelease;
	bool bAirborne = false;
};
```

- [ ] **Step 2: Create `Source/Gen/AbilitySystem/Abilities/GenGA_Leap.cpp`**

```cpp
#include "AbilitySystem/Abilities/GenGA_Leap.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionJumpForce.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAreaRules.h"
#include "Actors/GenGroundArea.h"
#include "GameFramework/RootMotionSource.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenLeap, Log, All);

UGenGA_Leap::UGenGA_Leap()
{
	CastTime = 0.1f;
}

void UGenGA_Leap::OnCastLaunched(const FGenCastRelease& Release)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		FinishAbility();
		return;
	}

	LeapRelease = Release;
	const FVector Start = Avatar->GetActorLocation();
	const FVector Target = GenAreaRules::ClampToRange(Start, Release.AimLocation, MaxDistance);
	const float Distance = FVector::Dist2D(Start, Target);

	bAirborne = true;
	SetCastLock(true);

	if (TrailCueTag.IsValid())
	{
		K2_AddGameplayCue(TrailCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo), /*bRemoveOnAbilityEnd*/ true);
	}

	// Mouvement racine prédit (client et serveur) : arc visible, arrêt net à l'atterrissage
	UAbilityTask_ApplyRootMotionJumpForce* JumpTask = UAbilityTask_ApplyRootMotionJumpForce::ApplyRootMotionJumpForce(
		this, NAME_None, Release.AimDirection.Rotation(), Distance, LeapHeight, LeapDuration,
		/*MinimumLandedTriggerTime*/ LeapDuration * 0.5f, /*bFinishOnLanded*/ true,
		ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.f, nullptr, nullptr);
	JumpTask->OnLanded.AddDynamic(this, &ThisClass::OnLanded);
	JumpTask->OnFinish.AddDynamic(this, &ThisClass::OnLanded);
	JumpTask->ReadyForActivation();

	UE_LOG(LogGenLeap, Verbose, TEXT("%s : bond de %.0f cm (nourri %d)"), *GetName(), Distance, Release.Fed);
}

void UGenGA_Leap::OnLanded()
{
	if (!bAirborne)
	{
		return; // OnLanded puis OnFinish : une seule fois
	}
	bAirborne = false;
	SetCastLock(false);

	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		OnLeapLanded(LeapRelease, Avatar->GetActorLocation());
	}

	FinishAbility();
}

void UGenGA_Leap::OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation)
{
	if (LandMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, LandMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false);
		MontageTask->ReadyForActivation();
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority())
	{
		return;
	}

	if (ImpactCueTag.IsValid())
	{
		K2_ExecuteGameplayCue(ImpactCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo));
	}

	FGenAreaParams Params;
	Params.Radius = LandingRadius;
	Params.KnockbackDistance = LandingKnockback;
	SpawnGroundArea(LandingAreaClass, LandingLocation, Params, UGenGE_Damage::StaticClass(), LandingDamage, LandingEnergyOnHit);

	UE_LOG(LogGenLeap, Verbose, TEXT("%s : atterrissage en %s"), *GetName(), *LandingLocation.ToCompactString());
}

void UGenGA_Leap::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Fin en plein vol (mort) : le mouvement racine s'arrête avec la tâche, le verrou est retiré par la base
	bAirborne = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
```

- [ ] **Step 3: Create `Source/Gen/Champions/Curffe/CurffeGA_MeteorLeap.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Leap.h"
#include "CurffeGA_MeteorLeap.generated.h"

class AGenProjectile;

/**
 * Bond météore (Espace) de Curffe : chaque flamme nourrie jaillit en boule de feu à l'atterrissage,
 * en anneau régulier autour du point d'impact (5 flammes = étoile à 5 branches). Une salve : un ennemi
 * n'est touché que par une boule de feu de l'anneau. Les boules suivent les règles de la boule de feu
 * (portée, murs, contres).
 */
UCLASS()
class GEN_API UCurffeGA_MeteorLeap : public UGenGA_Leap
{
	GENERATED_BODY()

protected:
	virtual void OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation) override;

	/** Projectile de l'anneau (BP_Projectile_Fireball : portée, vitesse, effets). */
	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap")
	TSubclassOf<AGenProjectile> RingProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap")
	float RingDamage = 8.f;

	/** Énergie par boule de l'anneau qui touche (règles de la boule de feu ; pas de flamme : seul le clic gauche en rend). */
	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap")
	float RingEnergyOnHit = 2.f;

	/** Distance du point d'impact où apparaissent les boules. */
	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float RingSpawnOffset = 70.f;
};
```

- [ ] **Step 4: Create `Source/Gen/Champions/Curffe/CurffeGA_MeteorLeap.cpp`**

```cpp
#include "Champions/Curffe/CurffeGA_MeteorLeap.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenSalvo.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "Actors/GenProjectile.h"
#include "Engine/HitResult.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeMeteorLeap, Log, All);

void UCurffeGA_MeteorLeap::OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation)
{
	Super::OnLeapLanded(Release, LandingLocation);

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || Release.Fed <= 0 || !RingProjectileClass)
	{
		return;
	}

	const TSharedPtr<FGenProjectileSalvo> Salvo = MakeShared<FGenProjectileSalvo>();
	for (const FVector& Direction : GenAreaRules::GetRingDirections(Release.Fed, Release.AimDirection))
	{
		// Jamais derrière un mur proche : la boule apparaît contre lui et y explose (règle des murs)
		FVector Origin = LandingLocation + Direction * RingSpawnOffset;
		FHitResult WallHit;
		if (GenWorldQueries::FindWallHit(GetWorld(), LandingLocation, Origin, { Avatar }, WallHit))
		{
			Origin = FVector(WallHit.ImpactPoint) - Direction * 5.f;
		}

		SpawnProjectileShot(RingProjectileClass, Origin, Direction, FGenProjectileShotParams(), UGenGE_Damage::StaticClass(),
			RingDamage, RingEnergyOnHit, 0.f, Salvo);
	}

	UE_LOG(LogCurffeMeteorLeap, Verbose, TEXT("%s : anneau de %d boule(s) de feu"), *GetName(), Release.Fed);
}
```

- [ ] **Step 5: Build, then run the unit tests.** Expected: `Result: Succeeded`, and every test passes.

- [ ] **Step 6: Commit**

```bash
git add Source/Gen/AbilitySystem/Abilities/GenGA_Leap.h Source/Gen/AbilitySystem/Abilities/GenGA_Leap.cpp Source/Gen/Champions/Curffe/CurffeGA_MeteorLeap.h Source/Gen/Champions/Curffe/CurffeGA_MeteorLeap.cpp
git commit -m "Feedable leap with landing area; Meteor Leap bursts fed flames into a Fireball ring"
```

---

### Task 9: Status visuals (one placeholder shape per state tag)

The Art Bible §7.6 gives each state one shape motif. Until a GameplayCue library exists, a tag-driven component draws engine shapes with a translucent unlit material on every client:
- `State.Countering`: a stance shell;
- `State.Stunned`: a ring above the head.

Plan 3 adds more entries.

**Files:**
- Create: `Source/Gen/Character/GenStatusVisualsComponent.h`, `Source/Gen/Character/GenStatusVisualsComponent.cpp`
- Modify: `Source/Gen/Character/GenCharacterBase.h/.cpp`
- Test: `Source/Gen/Tests/GenCombatWorldTests.cpp`

**Interfaces:**
- Consumes: `UGenAbilitySystemComponent::ApplyHardCC` (Task 2, test only).
- Produces:
  - `USTRUCT FGenStatusVisual { FGameplayTag Tag; UStaticMesh* Mesh; UMaterialInterface* Material; FLinearColor Colour; float Opacity; FVector Offset; FVector Scale; bool bHideOwnerMesh; }`
  - `UGenStatusVisualsComponent::Bind(UAbilitySystemComponent*, const TArray<FGenStatusVisual>&)`, `Unbind()` and `IsStatusShown(FGameplayTag) const` (UFUNCTION)
  - `AGenCharacterBase::StatusVisuals` (component) and `AGenCharacterBase::StatusVisualConfig` (`TArray<FGenStatusVisual>`, EditDefaultsOnly; Python name `status_visual_config`)
  - The material parameters it sets: `Colour` (vector) and `Opacity` (scalar).

- [ ] **Step 1: Write the failing test.** Append this to `GenCombatWorldTests.cpp` before `#endif`, and add `#include "Character/GenStatusVisualsComponent.h"`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenStatusVisualTest, "Gen.Status.VisualFollowsTag",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenStatusVisualTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Dummy ? Dummy->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	UGenStatusVisualsComponent* Visuals = NewObject<UGenStatusVisualsComponent>(Dummy);
	Visuals->SetupAttachment(Dummy->GetRootComponent());
	Visuals->RegisterComponent();

	FGenStatusVisual Stunned;
	Stunned.Tag = GenGameplayTags::State_Stunned;
	Visuals->Bind(ASC, { Stunned });

	TestFalse(TEXT("caché au départ"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr);
	TestTrue(TEXT("affiché pendant l'étourdissement"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));
	TestWorld.Advance(1.1f);
	TestFalse(TEXT("caché à la fin"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));

	Visuals->Unbind();
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `Cannot open include file: 'Character/GenStatusVisualsComponent.h'`.

- [ ] **Step 3: Create `Source/Gen/Character/GenStatusVisualsComponent.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "GenStatusVisualsComponent.generated.h"

class UAbilitySystemComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** Forme provisoire affichée tant qu'un état (tag) est actif. Art Bible §7.6 : une forme par état, la même pour tous. */
USTRUCT(BlueprintType)
struct FGenStatusVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status", meta = (Categories = "State"))
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	TObjectPtr<UStaticMesh> Mesh;

	/** Ex : M_VFX_StatusShape (lit Colour et Opacity), ou M_ST_FireOrb tel quel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FLinearColor Colour = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.5f;

	/** Position par rapport au centre du personnage (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FVector Scale = FVector::OneVector;

	/** Cache le corps du personnage tant que l'état est actif (ex : forme de feu de Flamme vivante). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	bool bHideOwnerMesh = false;
};

/**
 * Formes d'état du personnage (contre, étourdi, intouchable...) : suit les tags de l'ASC, répliqués
 * à tous les clients. Purement cosmétique : rien sur un serveur dédié. À remplacer par la bibliothèque
 * de GameplayCues d'états quand elle existera.
 */
UCLASS(ClassGroup = (Gen), meta = (BlueprintSpawnableComponent))
class GEN_API UGenStatusVisualsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UGenStatusVisualsComponent();

	/** (Re)branche sur un ASC ; rappelable (respawn, OnRep_PlayerState multiples). */
	void Bind(UAbilitySystemComponent* InASC, const TArray<FGenStatusVisual>& InVisuals);
	void Unbind();

	/** Forme de l'état Tag affichée (lu par les tests). */
	UFUNCTION(BlueprintPure, Category = "Gen|Status")
	bool IsStatusShown(FGameplayTag Tag) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnTagChanged(const FGameplayTag Tag, int32 NewCount);
	void SetShown(int32 Index, bool bShown);
	void RefreshOwnerMesh();

	TArray<FGenStatusVisual> Visuals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Shapes;

	TArray<bool> Shown;
	TArray<FDelegateHandle> TagHandles;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
};
```

- [ ] **Step 4: Create `Source/Gen/Character/GenStatusVisualsComponent.cpp`**

```cpp
#include "Character/GenStatusVisualsComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

UGenStatusVisualsComponent::UGenStatusVisualsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGenStatusVisualsComponent::Bind(UAbilitySystemComponent* InASC, const TArray<FGenStatusVisual>& InVisuals)
{
	Unbind();

	if (!InASC || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	BoundASC = InASC;
	Visuals = InVisuals;

	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		const FGenStatusVisual& Visual = Visuals[Index];

		UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(GetOwner());
		Shape->SetStaticMesh(Visual.Mesh);
		Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shape->SetCastShadow(false);
		Shape->SetupAttachment(this);
		Shape->SetRelativeLocation(Visual.Offset);
		Shape->SetRelativeScale3D(Visual.Scale);
		Shape->SetVisibility(false);
		Shape->RegisterComponent();

		if (Visual.Material)
		{
			UMaterialInstanceDynamic* MID = Shape->CreateDynamicMaterialInstance(0, Visual.Material);
			MID->SetVectorParameterValue(TEXT("Colour"), Visual.Colour);
			MID->SetScalarParameterValue(TEXT("Opacity"), Visual.Opacity);
		}

		Shapes.Add(Shape);
		Shown.Add(false);
		TagHandles.Add(Visual.Tag.IsValid()
			? InASC->RegisterGameplayTagEvent(Visual.Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::OnTagChanged)
			: FDelegateHandle());

		SetShown(Index, Visual.Tag.IsValid() && InASC->GetTagCount(Visual.Tag) > 0);
	}

	RefreshOwnerMesh();
}

void UGenStatusVisualsComponent::Unbind()
{
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		for (int32 Index = 0; Index < TagHandles.Num(); ++Index)
		{
			if (TagHandles[Index].IsValid())
			{
				ASC->RegisterGameplayTagEvent(Visuals[Index].Tag, EGameplayTagEventType::NewOrRemoved).Remove(TagHandles[Index]);
			}
		}
	}

	for (UStaticMeshComponent* Shape : Shapes)
	{
		if (Shape)
		{
			Shape->DestroyComponent();
		}
	}

	Shapes.Reset();
	Shown.Reset();
	TagHandles.Reset();
	Visuals.Reset();
	BoundASC.Reset();
	RefreshOwnerMesh();
}

void UGenStatusVisualsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unbind();
	Super::EndPlay(EndPlayReason);
}

bool UGenStatusVisualsComponent::IsStatusShown(FGameplayTag Tag) const
{
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		if (Visuals[Index].Tag == Tag && Shown[Index])
		{
			return true;
		}
	}
	return false;
}

void UGenStatusVisualsComponent::OnTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		if (Visuals[Index].Tag == Tag)
		{
			SetShown(Index, NewCount > 0);
		}
	}
	RefreshOwnerMesh();
}

void UGenStatusVisualsComponent::SetShown(int32 Index, bool bShown)
{
	Shown[Index] = bShown;
	if (Shapes[Index])
	{
		Shapes[Index]->SetVisibility(bShown);
	}
}

void UGenStatusVisualsComponent::RefreshOwnerMesh()
{
	bool bHide = false;
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		bHide |= Shown[Index] && Visuals[Index].bHideOwnerMesh;
	}

	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetMesh()->SetVisibility(!bHide);
	}
}
```

- [ ] **Step 5: Wire it into `AGenCharacterBase`.**
  - In `GenCharacterBase.h`, add `#include "Character/GenStatusVisualsComponent.h"` after the other includes.
  - In the `protected:` section, after `StartupEffects`, add:

```cpp
	/** Formes d'état (contre, étourdi...) affichées sur ce personnage. Python : status_visual_config. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Status")
	TArray<FGenStatusVisual> StatusVisualConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|Status")
	TObjectPtr<UGenStatusVisualsComponent> StatusVisuals;
```

  - In `GenCharacterBase.cpp`, at the end of the constructor:

```cpp
	StatusVisuals = CreateDefaultSubobject<UGenStatusVisualsComponent>(TEXT("StatusVisuals"));
	StatusVisuals->SetupAttachment(GetCapsuleComponent());
```

  - At the end of `OnAbilitySystemInitialized` (all machines; `Bind` ignores the dedicated server):

```cpp
	StatusVisuals->Bind(AbilitySystemComponent, StatusVisualConfig);
```

  - In `UninitializeAbilitySystem`, right after the `IsValid(AbilitySystemComponent)` early return:

```cpp
	StatusVisuals->Unbind();
```

- [ ] **Step 6: Build, then run the unit tests.** Expected: `Gen.Status.VisualFollowsTag` passes, and all earlier tests still pass.

- [ ] **Step 7: Commit**

```bash
git add Source/Gen/Character Source/Gen/Tests/GenCombatWorldTests.cpp
git commit -m "Tag-driven status visuals component on every character"
```

---

### Task 10: Editor assets: telegraph material, areas, Backfire, Flame Pillar, Meteor Leap, BP_Curffe

The editor must be open with the new binaries. Load the VibeUE skills first, through `call_tool` on `ToolsetRegistry.AgentSkillToolset` (`ListSkills`, then `GetSkills`): `VibeUE_materials`, `VibeUE_blueprints`, `VibeUE_blueprint_graphs`, `VibeUE_gas`, `VibeUE_gameplay_tags`, `VibeUE_enhanced_input`. Before writing the materials, read the Art Bible §3.0, §4.3, §7.1, §7.5 and §7.6 and the UI Guidelines §4.12 and §8.6.

**Files (assets):**
- Create:
  - `Content/Gen/Rendering/MPC_TeamColours`
  - `Content/Gen/Rendering/Masters/M_VFX_Telegraph`
  - `Content/Gen/Rendering/Masters/M_VFX_StatusShape`
- Create:
  - `Content/Gen/Champions/Curffe/Areas/BP_Area_FlamePillar`
  - `Content/Gen/Champions/Curffe/Areas/BP_Area_FireBurst`
  - `Content/Gen/Champions/Curffe/Abilities/GA_Backfire`
  - `Content/Gen/Champions/Curffe/Abilities/GA_FlamePillar`
  - `Content/Gen/Champions/Curffe/Abilities/Backfire/GCN_Backfire_Block`
- Modify:
  - `Content/Gen/Champions/Curffe/Abilities/GA_FlameLeap`, reparented to `CurffeGA_MeteorLeap`, with its graph cleared;
  - `Content/Gen/Champions/Curffe/BP_Curffe`;
  - `Content/Gen/Characters/BP_Champion` (status visuals).
- Check only, and fix if needed: `Content/Gen/Input/DA_InputConfig`, `Content/Gen/Input/IMC_Arena`.

The Art Bible owns `MPC_TeamColours` and `M_VFX_Telegraph` (hot shared files, §3.9). If another branch has already created them by the time you run this task, reuse them and skip Step 2 or Step 3.

- [ ] **Step 1: Lock the existing assets you modify.**

```bash
git fetch origin
git diff --stat HEAD...origin/main -- Content
git lfs locks
git lfs lock Content/Gen/Champions/Curffe/Abilities/GA_FlameLeap.uasset
git lfs lock Content/Gen/Champions/Curffe/BP_Curffe.uasset
git lfs lock Content/Gen/Characters/BP_Champion.uasset
```

  - If `git diff` lists `GA_FlameLeap`, `BP_Curffe` or `BP_Champion` changed on `main` (by `ui-ability-bar`, for example), stop and ask the user about the merge order.
  - `BP_Champion` may already be locked by you from Plan 1 (lock 55459113). That's fine.

- [ ] **Step 2: Create the team-colour MPC** (`execute_python_code`, `auto_save: false`).

```python
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

def vparam(name, rgb):
    p = unreal.CollectionVectorParameter()
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return p

MPC = "/Game/Gen/Rendering/MPC_TeamColours"
if EAL.does_asset_exist(MPC):
    mpc = unreal.load_asset(MPC)
    print("MPC existant :", [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")])
else:
    mpc = tools.create_asset("MPC_TeamColours", "/Game/Gen/Rendering", unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    # Relation (Art Bible §4.3 ; valeurs linéaires de UI_Guidelines) + liseré sombre (line.outline)
    mpc.set_editor_property("vector_parameters", [
        vparam("Self", (0.888, 0.888, 0.888)), vparam("Ally", (0.093, 0.456, 0.815)),
        vparam("Enemy", (0.665, 0.112, 0.0)), vparam("Neutral", (0.485, 0.381, 0.209)),
        vparam("Keyline", (0.002, 0.002, 0.003))])
    print("MPC", EAL.save_asset(MPC, only_if_is_dirty=False))
```

  Expected: `MPC True`, or the list of an existing MPC. If an existing MPC lacks one of the five names, add it and keep the existing values.

- [ ] **Step 3: Create `M_VFX_Telegraph`.**
  - It is unlit and translucent. The border is the hitbox radius, 2 px wide (through `fwidth`), with a 1 px keyline.
  - The fill is `FillAlpha` inside the growing timer disc and 75 % of it outside. An enemy viewer sees static stripes.
  - Two Custom nodes share the same HLSL: one returns the colour, the other the opacity.

```python
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
MNS = unreal.MaterialNodeService
MAT = "/Game/Gen/Rendering/Masters/M_VFX_Telegraph"
MPC = "/Game/Gen/Rendering/MPC_TeamColours"

mat = tools.create_asset("M_VFX_Telegraph", "/Game/Gen/Rendering/Masters", unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

BODY = r"""
float2 P = UV * 2.0 - 1.0;
float R = length(P);
float Px = max(fwidth(R), 1e-5);
float BorderW = BorderWidthPx * Px;
float KeyW = KeylineWidthPx * Px;
float3 Rel = RelationIndex < 1.5 ? SelfColour.rgb : (RelationIndex < 2.5 ? AllyColour.rgb : (RelationIndex < 3.5 ? EnemyColour.rgb : NeutralColour.rgb));
float4 Result = float4(0, 0, 0, 0);
if (R <= 1.0)
{
    if (R > 1.0 - BorderW)
    {
        Result = float4(Rel, BorderAlpha);
    }
    else if (R > 1.0 - BorderW - KeyW)
    {
        Result = float4(KeylineColour.rgb, BorderAlpha);
    }
    else
    {
        float Timer = R <= Fill ? 1.0 : 0.75;
        float Stripes = EnemyPattern > 0.5 ? (frac((P.x + P.y) * 6.0) < 0.5 ? 1.0 : 0.6) : 1.0;
        Result = float4(Rel, FillAlpha * Timer * Stripes);
    }
}
"""
INPUTS = ["UV", "RelationIndex", "Fill", "FillAlpha", "BorderAlpha", "EnemyPattern", "BorderWidthPx", "KeylineWidthPx",
          "SelfColour", "AllyColour", "EnemyColour", "NeutralColour", "KeylineColour"]
col = MNS.create_custom_expression(MAT, BODY + "return Result.rgb;", "CMOT_Float3", "TelegraphColour", ",".join(INPUTS), -300, -150)
alp = MNS.create_custom_expression(MAT, BODY + "return Result.a;", "CMOT_Float1", "TelegraphAlpha", ",".join(INPUTS), -300, 150)

uv = MNS.batch_create_expressions(MAT, ["TextureCoordinate"], [-800], [-500])[0]
sources = {"UV": uv}
for i, (name, default) in enumerate([("RelationIndex", "2"), ("Fill", "0"), ("FillAlpha", "0.2"), ("BorderAlpha", "0.9"),
                                     ("EnemyPattern", "0"), ("BorderWidthPx", "2"), ("KeylineWidthPx", "1")]):
    sources[name] = MNS.create_parameter(MAT, "Scalar", name, "Telegraph", default, -800, -400 + i * 80)
for i, (inp, mpc_name) in enumerate([("SelfColour", "Self"), ("AllyColour", "Ally"), ("EnemyColour", "Enemy"),
                                     ("NeutralColour", "Neutral"), ("KeylineColour", "Keyline")]):
    sources[inp] = MNS.create_collection_parameter(MAT, MPC, mpc_name, -800, 200 + i * 80)

src, outs, tgt, ins = [], [], [], []
for node in (col, alp):
    for name in INPUTS:
        src.append(sources[name].id); outs.append(""); tgt.append(node.id); ins.append(name)
print("connexions :", MNS.batch_connect_expressions(MAT, src, outs, tgt, ins), "/", len(src))
print("emissive", MNS.connect_expression_to_output(MAT, col.id, "", "EmissiveColor"))
print("opacity", MNS.connect_expression_to_output(MAT, alp.id, "", "Opacity"))
print("compile", unreal.MaterialService.compile_material(MAT))
print("diag", MNS.get_material_diagnostics(MAT))
print("save", unreal.EditorAssetLibrary.save_asset(MAT, only_if_is_dirty=False))
```

  - Expected: `connexions : 26 / 26`, `emissive True`, `opacity True`, `compile True`, no error in `diag`, `save True`.
  - If a class name is refused (`TextureCoordinate`), list the valid names with `MNS.discover_types(...)` and retry.

- [ ] **Step 4: Create `M_VFX_StatusShape`** (unlit, translucent, two-sided; `Colour` drives emissive and `Opacity` drives opacity).

```python
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
MNS = unreal.MaterialNodeService
SM = "/Game/Gen/Rendering/Masters/M_VFX_StatusShape"
m = tools.create_asset("M_VFX_StatusShape", "/Game/Gen/Rendering/Masters", unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("two_sided", True)
c = MNS.create_parameter(SM, "Vector", "Colour", "Status", "1,1,1,1", -400, 0)
o = MNS.create_parameter(SM, "Scalar", "Opacity", "Status", "0.5", -400, 200)
print(MNS.connect_expression_to_output(SM, c.id, "", "EmissiveColor"), MNS.connect_expression_to_output(SM, o.id, "", "Opacity"))
print(unreal.MaterialService.compile_material(SM), unreal.EditorAssetLibrary.save_asset(SM, only_if_is_dirty=False))
```

  Expected: `True True` twice.

- [ ] **Step 5: Create the area Blueprints.**

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
def make_bp(path, parent):
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else BEL.create_blueprint_asset_with_parent(path, parent)
def cdo(bp):
    return unreal.get_default_object(BEL.generated_class(bp))

tele = unreal.load_asset("/Game/Gen/Rendering/Masters/M_VFX_Telegraph")
AREAS = "/Game/Gen/Champions/Curffe/Areas/"
for name, fx in [("BP_Area_FlamePillar", "/Game/Gen/VFX/Stylized/NS_ST_GreatFireballImpact"),
                 ("BP_Area_FireBurst", "/Game/Gen/VFX/Stylized/NS_ST_FlameLeap_Impact")]:
    bp = make_bp(AREAS + name, unreal.GenGroundArea.static_class())
    d = cdo(bp)
    d.set_editor_property("telegraph_material", tele)
    d.set_editor_property("impact_fx", unreal.load_asset(fx))
    d.set_editor_property("impact_fx_reference_radius", 150.0)
    BEL.compile_blueprint(bp)
    print(name, EAL.save_asset(AREAS + name, only_if_is_dirty=False))
```

  Expected: two `True` lines. The calibration of `impact_fx_reference_radius` happens in Task 11 V7: the impact flash must match the radius (Art Bible §7.1 rule 1).

- [ ] **Step 6: Create the block cue.** Duplicate the leap impact burst and retag it.

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
SRC = "/Game/Gen/Champions/Curffe/Abilities/FlameLeap/GCN_FlameLeap_Impact"
DST = "/Game/Gen/Champions/Curffe/Abilities/Backfire/GCN_Backfire_Block"
bp = EAL.duplicate_asset(SRC, DST)
d = unreal.get_default_object(BEL.generated_class(bp))
d.set_editor_property("gameplay_cue_tag", unreal.GameplayTagService.request_tag("GameplayCue.Curffe.Backfire.Block"))
burst = d.get_editor_property("burst_effects")
particles = burst.get_editor_property("burst_particles")
if len(particles) > 0:
    p0 = particles[0]
    p0.set_editor_property("niagara_system", unreal.load_asset("/Game/Gen/VFX/Stylized/NS_ST_FireballImpact"))
    particles[0] = p0
    burst.set_editor_property("burst_particles", particles)
    d.set_editor_property("burst_effects", burst)
BEL.compile_blueprint(bp)
print(EAL.save_asset(DST, only_if_is_dirty=False), d.get_editor_property("gameplay_cue_tag"))
```

  Expected: `True GameplayCue.Curffe.Backfire.Block`. This is a placeholder: a short fire burst on the countering mage. The art pass replaces it.

- [ ] **Step 7: Create and configure `GA_Backfire` and `GA_FlamePillar`.** Python names drop the `b` prefix (`bFeedable` becomes `feedable`). If a name fails, run `discover_python_class` on the class.

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
GTS = unreal.GameplayTagService
AB = "/Game/Gen/Champions/Curffe/Abilities/"
def make_bp(path, parent):
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else BEL.create_blueprint_asset_with_parent(path, parent)
def cdo(bp):
    return unreal.get_default_object(BEL.generated_class(bp))
def setall(d, values):
    for k, v in values.items():
        d.set_editor_property(k, v)

bf = make_bp(AB + "GA_Backfire", unreal.GenGA_Counter.static_class())
setall(cdo(bf), {
    "input_tag": GTS.request_tag("InputTag.Ability.1"), "display_name": unreal.Text("Retour de flamme"),
    "activation_policy": unreal.GenAbilityActivationPolicy.ON_INPUT_TRIGGERED,
    "cast_time": 0.1, "cast_move_speed_multiplier": 0.5,
    "cast_fx": unreal.load_asset("/Game/Gen/VFX/Stylized/NS_ST_Fireball_Cast"),
    "counter_window": 1.2, "window_move_speed_multiplier": 0.5,
    "resource_per_block": 2.0, "energy_on_first_block": 10.0, "melee_knockback_distance": 300.0,
    "block_cue_tag": GTS.request_tag("GameplayCue.Curffe.Backfire.Block"),
    "cooldown_duration": unreal.ScalableFloat(value=10.0),
    "cooldown_tags": GTS.request_tag_container(["Cooldown.Ability.Backfire"]),
    "ability_tags": GTS.request_tag_container(["Ability.Backfire"]),
})

fp = make_bp(AB + "GA_FlamePillar", unreal.GenGA_GroundArea.static_class())
setall(cdo(fp), {
    "input_tag": GTS.request_tag("InputTag.Ability.2"), "display_name": unreal.Text("Pilier de flammes"),
    "activation_policy": unreal.GenAbilityActivationPolicy.ON_INPUT_TRIGGERED,
    "feedable": True, "feed_interval": 0.2, "max_feed": 5,
    "cast_time": 0.4, "cast_move_speed_multiplier": 0.5,
    "cast_fx": unreal.load_asset("/Game/Gen/VFX/Stylized/NS_ST_GreatFireball_Cast"),
    "area_class": BEL.generated_class(unreal.load_asset("/Game/Gen/Champions/Curffe/Areas/BP_Area_FlamePillar")),
    "range": 900.0, "radius": 200.0, "radius_at_max_feed": 350.0,
    "impact_delay": 0.8, "min_telegraph": 0.5,
    "damage": unreal.ScalableFloat(value=12.0), "stun_duration": 1.0, "energy_on_hit": 8.0,
    "cooldown_duration": unreal.ScalableFloat(value=12.0),
    "cooldown_tags": GTS.request_tag_container(["Cooldown.Ability.FlamePillar"]),
    "ability_tags": GTS.request_tag_container(["Ability.FlamePillar"]),
})
for path, bp in [(AB + "GA_Backfire", bf), (AB + "GA_FlamePillar", fp)]:
    BEL.compile_blueprint(bp)
    print(path, EAL.save_asset(path, only_if_is_dirty=False))
d = cdo(fp)
print("relu :", d.get_editor_property("radius_at_max_feed"), d.get_editor_property("stun_duration"), d.get_editor_property("input_tag"))
```

  Expected: two `True` lines, then `relu : 350.0 1.0 InputTag.Ability.2`.

- [ ] **Step 8: Turn `GA_FlameLeap` into Meteor Leap.**
  - The asset stays where it is, so `BP_Curffe`'s reference holds. Its old Blueprint graph is deleted, because the behaviour is now in C++.
  - Its member variables (`LeapDuration`, `LeapHeight`...) are removed **before** reparenting, because they would collide with the C++ properties.
  - Print the old values first so you can note them in the commit message.

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
BS = unreal.BlueprintService
EAL = unreal.EditorAssetLibrary
P = "/Game/Gen/Champions/Curffe/Abilities/GA_FlameLeap"
bp = unreal.load_asset(P)
old = unreal.get_default_object(BEL.generated_class(bp))
print("parent :", BS.get_parent_class(P))
print("fonctions :", [f.function_name for f in BS.list_functions(P)])
variables = [v.variable_name for v in BS.list_variables(P)]
print("variables :", variables)
for v in variables:
    try:
        print(" ", v, "=", old.get_editor_property(v))
    except Exception as e:
        print(" ", v, "?", e)
```

  Then clear the graph and reparent it:

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
BS = unreal.BlueprintService
EAL = unreal.EditorAssetLibrary
P = "/Game/Gen/Champions/Curffe/Abilities/GA_FlameLeap"
bp = unreal.load_asset(P)
for f in BS.list_functions(P):
    BEL.remove_function_graph(bp, f.function_name)
for graph in ["EventGraph"]:
    for node in BS.get_nodes_in_graph(P, graph):
        BS.delete_node(P, graph, node.node_id)
for v in BS.list_variables(P):
    BS.remove_member_variable(P, v.variable_name, True)
print("compile vide :", BEL.compile_blueprint(bp))
BEL.reparent_blueprint(bp, unreal.CurffeGA_MeteorLeap.static_class())
print("compile reparenté :", BEL.compile_blueprint(bp), "parent :", BS.get_parent_class(P))
```

  Expected: the parent is `CurffeGA_MeteorLeap`, with no functions, no variables and an empty `EventGraph`.
  - If `list_functions` lists engine overrides that cannot be removed, leave them. They are empty once the graph is cleared.
  - Then configure it:

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
GTS = unreal.GameplayTagService
P = "/Game/Gen/Champions/Curffe/Abilities/GA_FlameLeap"
bp = unreal.load_asset(P)
d = unreal.get_default_object(BEL.generated_class(bp))
for k, v in {
    "input_tag": GTS.request_tag("InputTag.Ability.Mobility"), "display_name": unreal.Text("Bond météore"),
    "activation_policy": unreal.GenAbilityActivationPolicy.ON_INPUT_TRIGGERED,
    "feedable": True, "feed_interval": 0.2, "max_feed": 5, "cast_time": 0.1, "cast_move_speed_multiplier": 0.5,
    "cast_montage": unreal.load_asset("/Game/Gen/Champions/Curffe/Animations/AM_FlameLeap"),
    "land_montage": unreal.load_asset("/Game/Gen/Champions/Curffe/Animations/AM_FlameLeap_Land"),
    "max_distance": 700.0, "leap_height": 200.0, "leap_duration": 0.45,
    "landing_area_class": BEL.generated_class(unreal.load_asset("/Game/Gen/Champions/Curffe/Areas/BP_Area_FireBurst")),
    "landing_radius": 150.0, "landing_damage": 8.0, "landing_energy_on_hit": 2.0,
    "trail_cue_tag": GTS.request_tag("GameplayCue.FlameLeap.Trail"), "impact_cue_tag": GTS.request_tag("GameplayCue.FlameLeap.Impact"),
    "ring_projectile_class": BEL.generated_class(unreal.load_asset("/Game/Gen/Champions/Curffe/Projectiles/BP_Projectile_Fireball")),
    "ring_damage": 8.0, "ring_energy_on_hit": 2.0, "ring_spawn_offset": 70.0,
    "cooldown_duration": unreal.ScalableFloat(value=10.0),
    "cooldown_tags": GTS.request_tag_container(["Cooldown.Ability.FlameLeap"]),
    "ability_tags": GTS.request_tag_container(["Ability.FlameLeap"]),
    # Le Blueprint avait remplacé ces tags : on remet State.Casting (porté par toutes les incantations)
    "activation_owned_tags": GTS.request_tag_container(["State.Casting", "State.Leaping"]),
}.items():
    d.set_editor_property(k, v)
print(BEL.compile_blueprint(bp), EAL.save_asset(P, only_if_is_dirty=False))
print("relu :", d.get_editor_property("max_feed"), d.get_editor_property("ring_damage"), d.get_editor_property("activation_owned_tags"))
```

  Expected: `True True`, then `relu : 5 8.0` followed by both tags.

- [ ] **Step 9: Status visuals on `BP_Champion`** (generic: every champion shows them).

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
GTS = unreal.GameplayTagService
P = "/Game/Gen/Characters/BP_Champion"
bp = unreal.load_asset(P)
d = unreal.get_default_object(BEL.generated_class(bp))
shape = unreal.load_asset("/Game/Gen/Rendering/Masters/M_VFX_StatusShape")
def visual(tag, mesh, colour, opacity, offset, scale):
    v = unreal.GenStatusVisual()
    v.set_editor_property("tag", GTS.request_tag(tag))
    v.set_editor_property("mesh", unreal.load_asset(mesh))
    v.set_editor_property("material", shape)
    v.set_editor_property("colour", unreal.LinearColor(*colour, 1.0))
    v.set_editor_property("opacity", opacity)
    v.set_editor_property("offset", unreal.Vector(*offset))
    v.set_editor_property("scale", unreal.Vector(*scale))
    return v
d.set_editor_property("status_visual_config", [
    # Posture de contre : coque ambre (corps du feu #F5B82E) autour du personnage
    visual("State.Countering", "/Engine/BasicShapes/Sphere", (0.913, 0.480, 0.027), 0.35, (0, 0, 0), (1.6, 1.6, 2.2)),
    # Étourdi : anneau au-dessus de la tête (#FFE07A, Art Bible §7.6)
    visual("State.Stunned", "/Engine/BasicShapes/Cylinder", (1.0, 0.745, 0.194), 0.8, (0, 0, 120), (0.6, 0.6, 0.05)),
])
print(BEL.compile_blueprint(bp), EAL.save_asset(P, only_if_is_dirty=False), len(d.get_editor_property("status_visual_config")))
```

  - Expected: `True True 2`.
  - Read the value back on `BP_Curffe`'s CDO. It inherits the list unless `BP_Curffe` overrides it. If `BP_Curffe` shows an empty list, set the same list on it.

- [ ] **Step 10: Give BP_Curffe its new spells.**

```python
import unreal
BEL = unreal.BlueprintEditorLibrary
EAL = unreal.EditorAssetLibrary
AB = "/Game/Gen/Champions/Curffe/Abilities/"
P = "/Game/Gen/Champions/Curffe/BP_Curffe"
bp = unreal.load_asset(P)
d = unreal.get_default_object(BEL.generated_class(bp))
gc = lambda n: BEL.generated_class(unreal.load_asset(AB + n))
d.set_editor_property("startup_abilities", [gc("GA_Fireball"), gc("GA_GreatFireball"), gc("GA_FlameLeap"), gc("GA_Backfire"), gc("GA_FlamePillar")])
print(BEL.compile_blueprint(bp), EAL.save_asset(P, only_if_is_dirty=False))
print([c.get_name() for c in d.get_editor_property("startup_abilities")])
```

  Expected: `True True` and the five `GA_*_C` names.

- [ ] **Step 11: Check the input slots.**

```python
import unreal
cfg = unreal.load_asset("/Game/Gen/Input/DA_InputConfig")
print([(a.get_editor_property("input_action").get_name(), str(a.get_editor_property("input_tag"))) for a in cfg.get_editor_property("ability_input_actions")])
print([(m.action_name, m.key_name) for m in unreal.InputService.get_mappings("/Game/Gen/Input/IMC_Arena")])
```

  - Expected: `IA_Ability_1 → InputTag.Ability.1` mapped to key `A`, and `IA_Ability_2 → InputTag.Ability.2` mapped to `E`.
  - If a field name differs, read the struct with `discover_python_class`.
  - If a mapping is missing: lock the asset (`git lfs lock`), add it (`InputService.add_key_mapping`, or append to `ability_input_actions`), save, and include it in the commit.

- [ ] **Step 12: Icons.**
  - If `UGenGameplayAbility` has an `icon` property (that is, `ui-ability-bar` has been merged) and `Content/Python/gen_ui_icons.py` exists: add entries for `Curffe_1` (Backfire) and `Curffe_2` (Flame Pillar) following that script's pattern, generate them, and set `icon` on `GA_Backfire` and `GA_FlamePillar`.
  - `GA_FlameLeap` keeps its icon.
  - Otherwise, skip this step and list it as pending in the report.

- [ ] **Step 13: Smoke test** (Standalone, 1 player, `log LogGenCast Verbose`).
  - **A key:** an amber shell for 1.2 s, the speed drops to 275, and `Cooldown.Ability.Backfire` is applied.
  - **Hold E:** the aim circle follows the cursor and grows; on release the telegraph lasts 0.8 s, then the impact. Capture it with `capture_image source=game`.
  - **Space:** the leap goes about 7 m. Fed 5 times, it lands with a 5-pointed star of Fireballs.

- [ ] **Step 14: Commit** (no push, no unlock; the locks stay held until the branch merges, as the Plan 1 ledger rules).

```bash
git add Content/Gen/Rendering Content/Gen/Champions/Curffe Content/Gen/Characters/BP_Champion.uasset
git commit -m "Telegraph and status materials, area Blueprints, Backfire, Flame Pillar, Meteor Leap assets"
```

---

### Task 11: PIE verification matrix (dedicated server + 3 clients)

**Files:**
- Modify: `Content/Python/gen_pie_tools.py`

- [ ] **Step 1: Add the helpers** at the end of `gen_pie_tools.py`:

```python
def server_asc(client_index):
    return unreal.AbilitySystemLibrary.get_ability_system_component(server_pawn_for(client_index))


def has_tag(actor, tag_name):
    """Tag présent sur l'ASC d'un acteur (serveur ou copie client)."""
    asc = unreal.AbilitySystemLibrary.get_ability_system_component(actor)
    return bool(asc) and asc.has_matching_gameplay_tag(unreal.GameplayTagService.request_tag(tag_name))


def clear_cooldowns(client_index):
    """Serveur : retire toutes les recharges du joueur (tests rapprochés)."""
    return server_asc(client_index).remove_active_effects_with_granted_tags(
        unreal.GameplayTagService.request_tag_container(["Cooldown"]))


def areas(world_index=0):
    """Zones au sol d'un monde : 0 = serveur, 1..3 = clients. [(nom, x, y, rayon, déclenchée)]"""
    server, clients = worlds()
    world = server if world_index == 0 else clients[world_index - 1]
    result = []
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GenGroundArea):
        loc = a.get_actor_location()
        result.append((a.get_name(), round(loc.x), round(loc.y), round(a.get_radius()), a.has_detonated()))
    return result


def status_shown(viewer_index, subject_index, tag_name):
    """Forme d'état de subject affichée chez le client viewer."""
    p = client_pawn(viewer_index, subject_index)
    comp = p.get_editor_property("status_visuals") if p else None
    return bool(comp) and comp.is_status_shown(unreal.GameplayTagService.request_tag(tag_name))
```

- [ ] **Step 2: Set up the session.**
  - `EngineSettingsService.set_pie_settings("Client", 3, True)` and `PerformanceService.set_background_throttling(False)`.
  - `StartPIE` with a 3 s warm-up.
  - Turn on verbose logs: `log LogGenCast Verbose`, `log LogGenProjectile Verbose`, `log LogGenGroundArea Verbose`, `log LogGenCounter Verbose`, `log LogGenLeap Verbose`, `log LogCurffeMeteorLeap Verbose`.
  - Confirm the teams: `state(1)["team"] == state(3)["team"] != state(2)["team"]`.
  - Use the Plan 1 frame of reference, which keeps clear of the level's dummies: c1 at (-700,-1250) and **+X** forward. Use `watch_start()` and `time_dilation()` the same way.

- [ ] **Step 3: Run the matrix.**
  - Make one `execute_python_code` call per action and read the results in a later call.
  - A row passes only if the expected result is observed.
  - Use `clear_cooldowns(i)` between attempts.
  - Kill with a projectile, never with `damage()` from Python, when a spell is active (Plan 1 caveat about RPCs).

| # | Scenario | Setup (relative to c1) | Expected |
|---|---|---|---|
| V1 | **Regression after the refactor** | Plan 1 V3, V4 and V5, unchanged | Same results as Plan 1: fed 3 gives −32 and +9 energy; a cancel restores the flames with no cooldown; held LMB plus RMB is not cancelled. `[CLIENT]` and `[SERVEUR]` logs now come from `LogGenCast` |
| V2 | **Backfire blocks a Fireball** | c2 (enemy) at +600, flames set to 1 (`gain(2, flames=-4)`). Client 2 taps `IA_Ability_1`; 0.2 s later c1 fires a Fireball at c2 | c2's hp is unchanged and the log says `coup direct bloqué par un contre`. c2 flames 1→3 and energy +10. c1 gains **nothing**. `status_shown(1, 2, "State.Countering")` and `status_shown(3, 2, …)` are true during the window. c2's speed is 275, then 550 after 1.2 s. `Cooldown.Ability.Backfire` lasts 10 s |
| V3 | **Countered Great Fireball with splash** | c2 countering at +600. Enemy dummy (team 1) at (+600,+120). c1 holds RMB 1.2 s (5 flames) at c2 | c2 takes **no damage and no knockback**. The dummy takes −44 and is knocked back. c1 energy +11. Repeat without the dummy: c1's energy is unchanged |
| V4 | **Two blocks, energy once** | c2 flames 0, countering. c1 and c3 each fire a Fireball inside the window | c2 flames 0→4, energy **+10 only once** |
| V5 | **Areas go through the counter** | c1 casts Flame Pillar (tap E) at c2 at +600. Client 2 taps Backfire when the telegraph appears | At impact: c2 −12 and stunned 1 s. `State.Countering` is removed as the stun lands (log: Backfire `Fin (annulé=1)`). c2 gains no flames. c1 energy +8 |
| V6 | **Backfire window and other spells** | (a) c2 counters, then taps LMB 0.3 s later. (b) c2 holds LMB (`hold(IA_Ability_Primary, 3)`), then taps Backfire | (a) `State.Countering` disappears at the LMB press (`posture terminée par GA_Fireball`) and the cooldown stays. (b) The window lasts the full 1.2 s (auto-repeat does not end it); LMB fire resumes afterwards |
| V7 | **Flame Pillar basics** | c1 taps E aimed at c2 (+600) | During the 0.8 s telegraph, `areas(2)` shows 1 area of radius 200, not yet triggered. Capture: the border matches the radius (collision debug `show Collision`), the enemy hatching is visible, and the impact flash matches the radius (calibrate `impact_fx_reference_radius` if not). After impact: c2 −12, `State.Stunned` 1 s, speed 0. Client 2 taps LMB during the stun and **no `Activé` log** follows. c1 energy +8 and `Cooldown.Ability.FlamePillar` 12 s |
| V8 | **Feeding, allies, edge of the radius** | c1 holds E 1.2 s (5 flames) aimed at (+600,0). Ally c3 at (+600,+200). Enemy dummies at (+600,+330) and (+600,+420) | Radius 350 (`areas(0)`). **Ally c3 untouched.** The dummy at +330 is hit (−12, stunned) and the dummy at +420 is not (350 + 42 < 420). While feeding, client 1's preview radius grows (capture). Flames 5→0 |
| V9 | **The caster dies before impact** | c1 at 5 hp casts a pillar on an enemy dummy, with c3 (ally) also in the radius. c2 kills c1 with a Fireball during the telegraph | The pillar still lands: the dummy is hit and **c3 is not**. No error in the log |
| V10 | **Walls and partial cover** | Pillar at (+600,0), radius 200. Wall `spawn_wall(+600,+100, (2,0.2,2))` between the centre and dummy A at (+600,+170). Dummy B at (+700,+150), partly past the end of the wall | A is untouched (the wall protects it). B is hit (part of its capsule is visible). Repeat with a pillar fed 3 at the same spot |
| V11 | **Range clamp** | c1 aims at +1500 and taps E | The log `Zone … en` shows a centre at about +900 from c1. The preview stopped at 9 m |
| V12 | **A pillar cast interrupted by a stun** | c1 holds E (feeding). c2's pillar lands on c1 during the feed | c1's pillar: `Fin (annulé=1)`, flames back to 5 (not spent), no `Cooldown.Ability.FlamePillar`, the preview is gone (`areas(1)` empty) |
| V13 | **Meteor Leap, unfed and fed** | (a) Tap Space aimed at +1500, with a dummy at the expected landing point (+700). (b) Hold Space 1.2 s aimed at +600, with dummies 300 cm from the landing point along the 5 star directions and one dummy 60 cm from it | (a) c1 moves 650–720 cm along an arc. The dummy takes −8 and c1 gets +2 energy. Flight takes about 0.45 s. The trail and impact cues are visible. `Cooldown.Ability.FlameLeap` lasts 10 s. (b) The log says `anneau de 5 boule(s)`. Each star dummy takes −8. The dummy at 60 cm takes 8 (landing) + **at most 8** (one ring Fireball). Flames 5→0 |
| V14 | **Stun at take-off or in flight** | (a) c2's pillar lands on c1 while c1 is feeding the leap. (b) It lands while c1 is airborne | (a) The leap is cancelled with no cooldown and no flames spent. (b) The log says `phase non interruptible : ignoré`; c1 lands normally, then stays stunned for the rest of the second |
| V15 | **Two feedable spells in a row** | c1 holds RMB 0.45 s (2 fed), then holds Space without releasing RMB | The Great Fireball ends with `Fin (annulé=1)` and its 2 flames are not spent. The leap feeds from 0. `client_view` on client 2 shows c1's `fed` following the leap (never stuck at 2) |
| V16 | **Ring versus counter** | c2 counters 250 cm from the landing point, fed 3 | The ring Fireball that reaches c2 is blocked: c2 +2 flames and +10 energy |
| V17 | **Ring at a wall** | Wall 40 cm in front of the landing point (along the leap direction); dummy behind it; fed 1 | The single Fireball explodes on the wall at spawn and the dummy behind it is untouched |
| V18 | **Death during the window** | c2 counters at 10 hp; c1 kills c2 with a pillar | After respawn: no `State.Countering` or `State.Stunned` (`inspect_tags`), speed 550, no shell (`status_shown`) |

- [ ] **Step 4: Clean up.**
  - `watch_stop()`, `PIEActorService.destroy_all()`, then `StopPIE`.
  - Restore the PIE settings to Standalone with 1 client and turn background throttling back on.
  - Set every log category back to `Log`.

- [ ] **Step 5: Fix any failure.** Each failing row is a bug. Use the systematic-debugging skill, fix it in the owning task's files, rebuild, and rerun that row and every row after it.

- [ ] **Step 6: Commit the helpers and any fixes.**

```bash
git add Content/Python/gen_pie_tools.py
git commit -m "PIE helpers for counter, area and leap verification"
```

  In the report, list the VibeUE skills and services you used (CLAUDE.md rule 4).

---

## Open points for later plans

- **Plan 3** (power spells) builds on `UGenGA_Cast`, `AGenGroundArea` and `ApplyHardCC`: Untouchable, Living Flame, Combustion, Resilience, energy costs and the cancel key.
- Audio is not covered here: the Backfire stance's sound cue (guidelines §3.4) and the pillar's wind-up sound (Art Bible §7.3). No sound assets exist yet.
- The status shapes, the block burst and the impact effects are placeholders until the VFX pass (Art Bible §7.4, §7.6).
