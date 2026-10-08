# Gen.Net.* — CQTest network tests (UE 5.8)

Module: `Source/GenTests` (Type `Editor`, listed in `Gen.uproject` and `GenEditor.Target.cs` only).
Introduced in commit `a5f5d70` on `curffe-plan2`. Engine reference:
`Engine/Source/Developer/CQTest/Public/Components/PIENetworkComponent.h` (+ `Impl/PIENetworkComponent.inl`,
`Private/Components/PIENetworkComponent.cpp`) and Epic's own tests in
`Engine/Plugins/Tests/CQTest/Source/CQTestTests/Private/Components/PIENetworkComponentTests.cpp`.

## What the component does (per TEST_METHOD)

Each `TEST_METHOD` gets a fresh session. Before your steps run, the component queues:
Stop PIE -> `FAutomationEditorCommonUtils::CreateNewMap()` (a blank **Untitled** map) -> start PIE with
`RunUnderOneProcess`, N clients, separate dedicated server, `GameModeOverride` = your game mode ->
wait for worlds -> map each client world to its server connection -> wait until every connection has a
`ViewTarget`. Your `.ThenServer/.UntilClients/...` steps run after that. Teardown ends PIE.

Cost: ~0.5 s per method for setup + whatever your steps wait for. A full `Gen.` headless run is ~20 s
wall-clock including editor startup.

## Template

```cpp
#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST   // WITH_EDITOR && dev automation tests

#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

// Full name: Gen.Net.MyFeature.<MethodName>
NETWORK_TEST_CLASS(MyFeature, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	// Cross-machine data lives in test-class members (client lambdas cannot see ServerState)
	UWorld* ServerWorld = nullptr;
	int32 TargetPlayerId = INDEX_NONE;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())   // BP_GenGameMode -> BP_Curffe pawn, BP_GenPlayerController
			// .WithPacketSimulationSettings(&LagSettings)  // optional: FPacketSimulationSettings member
			.Build(Network);
	}

	TEST_METHOD(Something_WhenX_ThenY)
	{
		Network
			.UntilServer(TEXT("Server: players ready"), [](FBasePIENetworkComponentState& S) { return AreAllServerPlayersReady(S); }, DefaultWait())
			.UntilClients(TEXT("Clients: players ready"), [](FBasePIENetworkComponentState& C) { return IsPlayerReady(GetLocalController(C)); }, DefaultWait())
			.ThenServer(TEXT("Server: do the authoritative thing"), [this](FBasePIENetworkComponentState& S)
			{
				ServerWorld = S.World;
				const AGenPlayerState* PS = GetServerController(S, /*ClientIndex*/ 0)->GetPlayerState<AGenPlayerState>();
				TargetPlayerId = PS->GetPlayerId();
				UAbilitySystemComponent* ASC = GetASC(PS);
				ASSERT_THAT(IsNotNull(ASC));
				// ... apply a GE, call a server function, etc.
			})
			.UntilClients(TEXT("Clients: change replicated"), [this](FBasePIENetworkComponentState& C)
			{
				const UAbilitySystemComponent* ASC = GetASC(FindPlayerStateById(C.World, TargetPlayerId));
				return ASC && /* condition */ true;
			}, DefaultWait())
			.ThenClients(TEXT("Clients: final asserts with messages"), [this](FBasePIENetworkComponentState& C)
			{
				// ASSERT_THAT(...) here: the Until above only waits, this step explains failures
			});
	}
};

#endif
```

Put new files in `Source/GenTests/Private/Net/`, add includes from `Gen` freely (its public include path is the
module root, e.g. `"Character/GenPlayerCharacter.h"`).

## API you will actually use

| Call | Runs | Notes |
|---|---|---|
| `.ThenServer(Desc, fn(State&))` | once, next frame | `State.World` = server PIE world, `State.ClientConnections[i]` = server-side connection of client i |
| `.ThenClients(Desc, fn)` / `.ThenClient(Desc, i, fn)` | once on each / one client | `State.ClientIndex` = i, `State.World` = that client's world |
| `.UntilServer / .UntilClients / .UntilClient(Desc, [i,] fn -> bool, Timeout)` | every frame until true | Timeout -> test error `Timed out waiting for <Desc> after N ms`. Always pass `DefaultWait()` (15 s) or an explicit `FTimespan` |
| `.Then / .Do / .Until / .StartWhen` | plain steps | no state argument |
| `.SpawnAndReplicate<AMyActor, &MyState::Ptr>(...)` | server spawn + wait for each client | needs a derived state struct with an `AMyActor*` member; actor must replicate |
| `.ThenClientJoins()` | late join | appends a client state, `ServerState.ClientCount++` |

Description strings show in the log as `LogCqTest: Running/Starting/Finished <Desc>` — keep them meaningful.

Helpers (`Net/GenNetTestHelpers.h`, namespace `GenNetTest`):
`LoadGameModeClass()`, `LoadCurffeAbilityClass(TEXT("GA_Fireball"))`, `GetServerController(ServerState, i)`,
`GetLocalController(ClientState)`, `FindPlayerStateById(World, PlayerId)`, `GetASC(PS)`, `IsPlayerReady(PC)`,
`AreAllServerPlayersReady(ServerState)`, `GetAttribute(ASC, Attr)`, `DoReplicatedAttributesMatch(Expected, Actual, &Diff)`,
`FindAbilitySpec(ASC, Class)`, `IgnoreUntitledMapNetWarnings(Test)`, `DefaultWait()`.

### Getting the pawn / ASC per machine

- Server, player of client i: `GetServerController(S, i)` -> `GetPawn<AGenPlayerCharacter>()`,
  `GetPlayerState<AGenPlayerState>()` -> `GetASC(PS)` (ASC lives on the PlayerState).
- Client, its own player: `GetLocalController(C)` (= `World->GetFirstPlayerController()`), same accessors.
- Client, *another* player's simulated view: `FindPlayerStateById(C.World, PlayerId)` — `PlayerId` is the same on
  every machine; pawns have no stable cross-world id, go through the PlayerState.
- Server value from a client lambda: remember `ServerWorld` in a `ThenServer` step, then
  `FindPlayerStateById(ServerWorld, LocalPS->GetPlayerId())`.
- Client index i == `ServerState.ClientConnections[i]` (matched by port by the component). Team: client 0 -> team 0,
  client 1 -> team 1 (`AGenGameMode::PickTeamForNewPlayer`), so they are enemies.

### Activating an ability from a client (no input injection)

See `GenNetAbilityTests.cpp`. On client 0: set `AGenPlayerController::bDebugAimOverride = true` and
`DebugAimLocation` (deterministic aim, no mouse in tests), find the spec with `FindAbilitySpec`, then
`ASC->TryActivateAbility(Spec->Handle)`. This goes through the real LocalPredicted path
(ServerTryActivateAbility, NetSync, target data). Not holding input means a feedable spell feeds 0 flames.
To observe server spawns deterministically use `Server.World->AddOnActorSpawnedHandler(...)` and remove it
in the next server step **and** in `AFTER_EACH` (test may fail before).

## Pitfalls hit / to know

1. **Blank map**: no floor, no PlayerStart. Both pawns spawn at the origin, overlapping, and fall (KillZ is far,
   so a test of a few seconds is fine). Don't assert positions; teleport pawns apart on the server
   (`TeleportTo(..., false, true)`) before anything that collides (projectiles hit the enemy at spawn otherwise).
   If a test needs ground (movement, leaps, knockback), spawn a floor actor in each world first or use
   `MOVE_Flying`; no helper exists yet.
2. **Untitled map warnings**: `LogNetPackageMap: FNetGUIDCache::SupportsObject: Level /Temp/UEDPIE_..._Untitled... NOT
   Supported` and `RegisterNetGUID_Client: Guid with pathname ... WorldSettings` appear in every network test and turn
   results into "Success with warnings". Call `IgnoreUntitledMapNetWarnings(*TestRunner)` in `BEFORE_EACH`.
3. **No float `AreEqual`**: CQTest `static_assert`s on floating-point `AreEqual`. Use `IsNear(a, b, 0.01f)`.
   `AreEqual` on enums (e.g. `ENetMode`) needs a string conversion that doesn't exist: use `IsTrue(a == b, msg)`.
4. **`ASSERT_THAT` returns `void`**: usable in `Then*` lambdas and the TEST_METHOD body, not inside `Until*`
   lambdas (they return bool). Pattern: `Until*` only waits on the condition, a following `Then*` asserts with messages.
5. **Exported symbols**: `GenGameplayTags::*` are declared with `UE_DECLARE_GAMEPLAY_TAG_EXTERN` (not `GEN_API`), so
   they don't link from GenTests. Use `FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage"))`. All `UCLASS`es
   used so far are `GEN_API`; header-only namespaces (`CurffeTuning`, `GenFeeding`) are fine.
6. **Lambda captures**: captured `this` is the test object (alive for the whole method); world/actor pointers die at
   teardown — use `TWeakObjectPtr` for anything a delegate may touch later.
7. **Timeouts are failures**: verified — an `Until` that never becomes true fails with
   `Timed out waiting for <Desc>` and the next test still runs cleanly.
8. Iris is off in this project (`iris="0"` in the log, generic replication). The component supports both.
9. Startup noise `LogAutomationTest: Error: Condition failed` (x20, frame 0) comes from engine self-tests at boot and is
   not attributed to any Gen test; ignore it.

## Running

### Headless (worktree, no collision with the main editor)

Build first (worktree only — never use `-NoHotReloadFromIDE` against the main tree while its editor is open):

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GenEditor Win64 Development -Project="<worktree>\Gen.uproject" -NoHotReloadFromIDE -WaitMutex
```

Then (PowerShell; `Tools/RunGenTests.ps1 -Filter "Gen.Net."` wraps this and prints a summary):

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<worktree>\Gen.uproject" -ExecCmds="Automation RunTests Gen.Net.;Quit" -unattended -nullrhi -nosplash -nosound -ReportExportPath="<worktree>\Saved\TestReport" -ModelContextProtocolPort=8011
```

`-nullrhi` works: PIE clients get their windows/HUD (`LogGenUI: Empilé WBP_HUDLayout`), no spurious errors.
Results: `Saved/TestReport/index.json` (`succeeded`, `succeededWithWarnings`, `failed`, per-test `entries`).
Exit code 0 = all passed, 255 = failures. Detailed steps: grep `LogCqTest` in the log.

### In the open editor (MCP)

`AutomationTestToolset.RunTestsByFilter("StartsWith:Gen.Net.")` (or `"StartsWith:Gen."` for everything).
Requirements and side effects:
- The editor must have been built with the GenTests module (after merging this branch: close editor, build, relaunch).
- **The component calls `GEditor->NewMap()` without prompting: the currently open level is unloaded and unsaved level
  edits are lost.** Save everything first. Afterwards the editor stays on an Untitled map — reopen `L_Arena`.
- It stops any running PIE session first. Don't run it while another agent is using PIE on that editor.
