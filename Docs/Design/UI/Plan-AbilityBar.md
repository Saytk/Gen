# Ability Bar and UI Foundation Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The in-match HUD shows the UI_Guidelines §4.1 ability bar (7 round slots with live key labels, icons, cooldown sweep and number, locked and ultimate states, ready flash), built on the minimum CommonUI foundation §8 requires, verified in PIE with a dedicated server and 2 clients.

**Architecture:**
- **Root and HUD:** a CommonUI root layout (`UGenPrimaryGameLayout`) is created once per local player by `AGenHUD`. It pushes `UGenHUDLayout`, which hosts `WBP_AbilityBar`.
- **Data:** a LocalPlayer subsystem (`UGenUISubsystem`) loads the token data assets named in a project setting (`UGenUISettings`), resolves key labels from the live Enhanced Input mapping, and broadcasts "ASC ready" for the local pawn.
- **Slots:** each `UGenAbilitySlot` finds the ability granted with its `InputTag`, listens to cooldown and stun tags and to Energy, and drives cached material instances. It never uses `NativeTick`; a short timer runs only while cooling or flashing.
- **Shared logic:** pure rules (cooldown text format, state precedence, hex → linear, key label fallback, ultimate segments) live in a header and are unit-tested.

**Tech Stack:** UE 5.8 C++ (`Gen` module), CommonUI and CommonInput plugins, UMG, Enhanced Input, GAS, Unreal Automation tests, VibeUE MCP for editor and asset work.

**Spec:** `Docs/Design/UI/AbilityBar.md` (scope and structure). Every value comes from `Docs/UI_Guidelines.md`, cited by section. On conflict, `Docs/ArtBible.md` wins.

## Global Constraints

- **Branch:** `ui-ability-bar`. Commit there. Never push and never unlock LFS locks; the controller handles both at merge time.
- **Code comments in French**, matching the existing code. UI text and docs in Canadian English.
- **No hard-coded UI values** (§9 Tokens): every colour, size, duration and key name comes from `DA_UIPalette`, `DA_UIMetrics`, `DA_UIKeyGlyphs` or the `TS_*` styles. Colours are produced from their hex token with `UGenUILibrary::HexToLinear`, never pasted as linear numbers.
- **§4.1 geometry:**
  - slot 64 px, ultimate 72 px, gap 12 px, row 528 px;
  - key label `TS_Label` (20 px) above the slot with a 2 px gap;
  - bottom margin 32 px inside the SafeZone.
- **§4.1 cooldown number:**
  - whole seconds rounded **up** while ≥ 1 s, one decimal under 1 s ("0.6");
  - hidden when the cooldown's total duration is under 2 s;
  - the sweep and the 70% desaturation stay visible even when the number is hidden.
- **§4.1 sweep:** a dark wedge (`cooldown.overlay`) that starts at 12 o'clock and shrinks **clockwise**, linearly in real time.
- **§5.2 timings:** ready flash 200 ms ease-out on the rim, no scale pop. Ultimate-ready pulse 300 ms, once, no idle loop.
- **§8.2 network:**
  - widgets only on the owning client, never on a dedicated server;
  - cooldowns read locally, so the predicted cooldown shows on the same frame;
  - other players' UI is out of scope here.
- **§8.4 data flow:** no UMG property bindings and no `NativeTick` for gameplay values. Bind in the "ASC ready" callback and unbind in `NativeDestruct`.
- **§8.10 folders:**
  - assets in `Content/Gen/UI/{Foundation,Fonts,Materials,HUD,Textures/Icons/Abilities}`;
  - C++ in `Source/Gen/UI/`.
- **Tooling:**
  - Compiling needs the editor closed; implementers may quit and relaunch it themselves (CLAUDE.md).
  - Editor Python always uses `auto_save: false`.
  - Never run `Plugins/VibeUE/BuildAndLaunchGame.ps1`.

## Review Focus

1. **Gameplay input still works once CommonUI owns the viewport** (Game input mode on the HUD layout). LMB, RMB, Space and movement must all still fire abilities in PIE. Pinned by Task 9, V1.
2. **Abilities replicate after "ASC ready" on clients.** A slot must still resolve its ability, and must rebind after respawn. Pinned by Task 6 (polling fallback) and Task 9, V2/V6.
3. **Cooldown under prediction and latency.** The predicted cooldown shows at once, and the countdown stays correct when the server's GE replaces the predicted one (`Net PktLag=150`). A cancelled cast (costs paid on release) shows no cooldown. Pinned by Task 9, V3/V4.
4. **AZERTY or rebound keys.** Labels follow the live mapping (A, E and R on AZERTY), with glyphs for mouse buttons. Pinned by Task 1 tests and Task 9, V5.
5. **No widget on the dedicated server, and no leak on respawn or PIE stop.** Bindings are removed in `NativeDestruct` and timers are cleared. Pinned by Task 9, V6/V8.

---

## File map

| File | Responsibility |
|---|---|
| `Source/Gen/UI/GenUIRules.h` (new) | Pure rules: cooldown text, slot state precedence, hex → linear, key label fallback, ultimate segments |
| `Source/Gen/Tests/GenUIRulesTests.cpp` (new) | Tests for the rules |
| `Source/Gen/UI/GenUITags.h/.cpp` (new) | Native tags `UI.Layer.*` |
| `Source/Gen/UI/GenUISettings.h/.cpp` (new) | `UDeveloperSettings`: soft refs to the token assets and widget classes |
| `Source/Gen/UI/GenUIDataAssets.h/.cpp` (new) | `UGenUIPalette`, `UGenUIMetrics`, `UGenUIKeyGlyphs` |
| `Source/Gen/UI/GenUILibrary.h/.cpp` (new) | `UGenUILibrary::HexToLinear` (Blueprint and Python callable) |
| `Source/Gen/UI/GenUISubsystem.h/.cpp` (new) | Tokens access, key labels, "ASC ready" event |
| `Source/Gen/UI/GenTextBlock.h/.cpp` (new) | `UGenTextBlock : UCommonTextBlock` |
| `Source/Gen/UI/GenPrimaryGameLayout.h/.cpp` (new) | Root layout with 4 tagged stacks |
| `Source/Gen/UI/GenHUDLayout.h/.cpp` (new) | Activatable HUD layout, Game input config |
| `Source/Gen/UI/GenAbilitySlot.h/.cpp` (new) | One slot: binding, states, materials, timers |
| `Source/Gen/UI/GenAbilityBar.h/.cpp` (new) | 7 slots in §4.1 order |
| `Source/Gen/UI/GenHUD.h/.cpp` | Creates the root layout; text spell line removed |
| `Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h` | `Icon` soft texture property |
| `Source/Gen/Character/GenPlayerCharacter.cpp` | Notifies "ASC ready" for the local pawn |
| `Source/Gen/Player/GenPlayerController.h` | `GetInputConfig()` |
| `Source/Gen/Gen.Build.cs`, `Gen.uproject`, `Config/DefaultEngine.ini`, `Config/DefaultGame.ini` | Plugins, modules, viewport client, CommonInput settings, UI settings |
| `Content/Gen/UI/**`, `Content/Python/gen_ui_icons.py` (new) | Fonts, styles, materials, data assets, widgets, icons, icon generator |

**Unit tests** (editor closed, after a successful build):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\Samy D\Documents\Unreal Projects\Gen\Gen.uproject" -ExecCmds="Automation RunTests Gen.;Quit" -unattended -nopause -nosplash -nullrhi -NoSound "-abslog=C:\Users\Samy D\Documents\Unreal Projects\Gen\Saved\Logs\GenTests.log"
Select-String -Path "C:\Users\Samy D\Documents\Unreal Projects\Gen\Saved\Logs\GenTests.log" -Pattern "Test Completed. Result=" | Select-Object -Last 40
```

**Build** (editor closed):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" GenEditor Win64 Development "-Project=C:\Users\Samy D\Documents\Unreal Projects\Gen\Gen.uproject" -WaitMutex | Select-Object -Last 25
```

---

### Task 1: Pure UI rules and tests

**Files:**
- Create: `Source/Gen/UI/GenUIRules.h`
- Test: `Source/Gen/Tests/GenUIRulesTests.cpp`

**Interfaces (produced):**
- `enum class EGenAbilitySlotState : uint8 { Empty, Ready, Cooldown, Locked }` (UENUM, BlueprintType). It lives in `GenUIRules.h`, which includes `GenUIRules.generated.h`.
- `GenUIRules::FormatCooldown(float Remaining, float TotalDuration, float HideBelowTotal) -> FString` (empty string = hidden)
- `GenUIRules::ResolveSlotState(bool bHasAbility, bool bLocked, float CooldownRemaining) -> EGenAbilitySlotState`
- `GenUIRules::HexToLinear(const FString& Hex, float Alpha = 1.f) -> FLinearColor`
- `GenUIRules::FallbackKeyLabel(const FKey& Key, const TMap<FKey, FText>& ShortTexts) -> FText`
- `GenUIRules::FundedSegments(float Energy, float MaxEnergy, int32 Segments) -> int32`

- [ ] **Step 1: Write the failing tests** in `Source/Gen/Tests/GenUIRulesTests.cpp`

```cpp
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "InputCoreTypes.h"
#include "UI/GenUIRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUICooldownFormatTest, "Gen.UI.CooldownFormat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUICooldownFormatTest::RunTest(const FString& Parameters)
{
	// §4.1 : secondes entières arrondies au-dessus dès 1 s, une décimale sous 1 s, caché si durée totale < 2 s
	TestEqual(TEXT("5.2 s -> 6"), GenUIRules::FormatCooldown(5.2f, 6.f, 2.f), FString(TEXT("6")));
	TestEqual(TEXT("1.0 s -> 1"), GenUIRules::FormatCooldown(1.0f, 6.f, 2.f), FString(TEXT("1")));
	TestEqual(TEXT("1.01 s -> 2"), GenUIRules::FormatCooldown(1.01f, 6.f, 2.f), FString(TEXT("2")));
	TestEqual(TEXT("0.6 s -> 0.6"), GenUIRules::FormatCooldown(0.6f, 6.f, 2.f), FString(TEXT("0.6")));
	TestEqual(TEXT("0.04 s -> 0.1"), GenUIRules::FormatCooldown(0.04f, 6.f, 2.f), FString(TEXT("0.1")));
	TestEqual(TEXT("terminé -> vide"), GenUIRules::FormatCooldown(0.f, 6.f, 2.f), FString());
	TestEqual(TEXT("durée totale 1 s -> caché"), GenUIRules::FormatCooldown(0.8f, 1.f, 2.f), FString());
	TestEqual(TEXT("durée totale exactement 2 s -> affiché"), GenUIRules::FormatCooldown(1.5f, 2.f, 2.f), FString(TEXT("2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUISlotStateTest, "Gen.UI.SlotState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUISlotStateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("pas de sort"), GenUIRules::ResolveSlotState(false, true, 3.f), EGenAbilitySlotState::Empty);
	TestEqual(TEXT("étourdi prime sur la recharge"), GenUIRules::ResolveSlotState(true, true, 3.f), EGenAbilitySlotState::Locked);
	TestEqual(TEXT("en recharge"), GenUIRules::ResolveSlotState(true, false, 0.5f), EGenAbilitySlotState::Cooldown);
	TestEqual(TEXT("prêt"), GenUIRules::ResolveSlotState(true, false, 0.f), EGenAbilitySlotState::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUIHexToLinearTest, "Gen.UI.HexToLinear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUIHexToLinearTest::RunTest(const FString& Parameters)
{
	// Valeurs de référence du guide (§2.2 text.primary, §2.4 energy.charging)
	const FLinearColor TextPrimary = GenUIRules::HexToLinear(TEXT("#F3DEC9"));
	TestEqual(TEXT("text.primary R"), TextPrimary.R, 0.896f, 0.002f);
	TestEqual(TEXT("text.primary G"), TextPrimary.G, 0.730f, 0.002f);
	TestEqual(TEXT("text.primary B"), TextPrimary.B, 0.584f, 0.002f);
	TestEqual(TEXT("alpha par défaut"), TextPrimary.A, 1.f, KINDA_SMALL_NUMBER);

	const FLinearColor Energy = GenUIRules::HexToLinear(TEXT("FFC233"), 0.5f);
	TestEqual(TEXT("sans # accepté, R"), Energy.R, 1.000f, 0.002f);
	TestEqual(TEXT("energy G"), Energy.G, 0.539f, 0.002f);
	TestEqual(TEXT("energy B"), Energy.B, 0.033f, 0.002f);
	TestEqual(TEXT("alpha fourni"), Energy.A, 0.5f, KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUIKeyLabelTest, "Gen.UI.KeyLabelFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUIKeyLabelTest::RunTest(const FString& Parameters)
{
	TMap<FKey, FText> Short;
	Short.Add(EKeys::SpaceBar, FText::FromString(TEXT("SPC")));
	TestEqual(TEXT("texte court"), GenUIRules::FallbackKeyLabel(EKeys::SpaceBar, Short).ToString(), FString(TEXT("SPC")));
	TestEqual(TEXT("repli sur le nom de la touche"), GenUIRules::FallbackKeyLabel(EKeys::A, Short).ToString(), EKeys::A.GetDisplayName(false).ToString());
	TestTrue(TEXT("touche invalide -> vide"), GenUIRules::FallbackKeyLabel(FKey(), Short).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUISegmentsTest, "Gen.UI.UltimateSegments",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUISegmentsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("25/100 -> 1"), GenUIRules::FundedSegments(25.f, 100.f, 4), 1);
	TestEqual(TEXT("49/100 -> 1"), GenUIRules::FundedSegments(49.f, 100.f, 4), 1);
	TestEqual(TEXT("100/100 -> 4"), GenUIRules::FundedSegments(100.f, 100.f, 4), 4);
	TestEqual(TEXT("max nul -> 0"), GenUIRules::FundedSegments(50.f, 0.f, 4), 0);
	TestEqual(TEXT("au-delà borné"), GenUIRules::FundedSegments(150.f, 100.f, 4), 4);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Expected: `C1083: Cannot open include file: 'UI/GenUIRules.h'`.

- [ ] **Step 3: Implement `Source/Gen/UI/GenUIRules.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "GenUIRules.generated.h"

/** État visuel d'un emplacement de sort (UI_Guidelines §4.1). */
UENUM(BlueprintType)
enum class EGenAbilitySlotState : uint8
{
	Empty,
	Ready,
	Cooldown,
	Locked
};

/** Règles pures de l'interface, testées hors monde. */
namespace GenUIRules
{
	/**
	 * Texte du chiffre de recharge (§4.1) : secondes entières arrondies au-dessus dès 1 s,
	 * une décimale sous 1 s, rien si la recharge est finie ou si sa durée totale est < HideBelowTotal.
	 */
	inline FString FormatCooldown(float Remaining, float TotalDuration, float HideBelowTotal)
	{
		if (Remaining <= 0.f || TotalDuration < HideBelowTotal)
		{
			return FString();
		}
		if (Remaining >= 1.f)
		{
			return FString::FromInt(FMath::CeilToInt32(Remaining - KINDA_SMALL_NUMBER));
		}
		const float Tenths = FMath::CeilToFloat(Remaining * 10.f - KINDA_SMALL_NUMBER) / 10.f;
		return FString::Printf(TEXT("%.1f"), FMath::Max(Tenths, 0.1f));
	}

	/** Priorité : vide > bloqué (étourdi) > recharge > prêt. */
	inline EGenAbilitySlotState ResolveSlotState(bool bHasAbility, bool bLocked, float CooldownRemaining)
	{
		if (!bHasAbility)
		{
			return EGenAbilitySlotState::Empty;
		}
		if (bLocked)
		{
			return EGenAbilitySlotState::Locked;
		}
		return CooldownRemaining > 0.f ? EGenAbilitySlotState::Cooldown : EGenAbilitySlotState::Ready;
	}

	/** Couleur d'un jeton hexadécimal sRGB ("#RRGGBB" ou "RRGGBB") convertie en linéaire. */
	inline FLinearColor HexToLinear(const FString& Hex, float Alpha = 1.f)
	{
		FLinearColor Linear(FColor::FromHex(Hex));
		Linear.A = Alpha;
		return Linear;
	}

	/** Libellé d'une touche : texte court du jeu de glyphes, sinon le nom court de la touche. */
	inline FText FallbackKeyLabel(const FKey& Key, const TMap<FKey, FText>& ShortTexts)
	{
		if (!Key.IsValid())
		{
			return FText::GetEmpty();
		}
		if (const FText* Short = ShortTexts.Find(Key))
		{
			return *Short;
		}
		return Key.GetDisplayName(false);
	}

	/** Segments d'énergie financés (arc de l'ultime, un segment par tranche de Max/Segments). */
	inline int32 FundedSegments(float Energy, float MaxEnergy, int32 Segments)
	{
		if (MaxEnergy <= 0.f || Segments <= 0)
		{
			return 0;
		}
		return FMath::Clamp(FMath::FloorToInt32(Energy / (MaxEnergy / Segments) + KINDA_SMALL_NUMBER), 0, Segments);
	}
}
```

  Notes:
  - `FLinearColor(const FColor&)` applies the sRGB-to-linear conversion, which matches the guide's tables.
  - If `TestEqual` on a `uint8` enum doesn't compile, compare with `TestTrue(..., A == B)`.

- [ ] **Step 4: Build and run the unit tests.** Expected: the 5 new `Gen.UI.*` tests and the existing `Gen.*` tests all pass.
- [ ] **Step 5: Commit**

```bash
git add Source/Gen/UI/GenUIRules.h Source/Gen/Tests/GenUIRulesTests.cpp
git commit -m "Add pure UI rules (cooldown text, slot state, hex to linear, key labels) with tests"
```

---

### Task 2: CommonUI setup, UI tags, settings, data asset classes, library

**Files:**
- Modify: `Gen.uproject`, `Source/Gen/Gen.Build.cs`, `Config/DefaultEngine.ini`
- Create: `Source/Gen/UI/GenUITags.h/.cpp`, `GenUISettings.h/.cpp`, `GenUIDataAssets.h/.cpp`, `GenUILibrary.h/.cpp`, `GenTextBlock.h/.cpp`

**Interfaces (produced):**
- `GenUITags::UI_Layer_Game`, `UI_Layer_GameMenu`, `UI_Layer_Menu`, `UI_Layer_Modal`
- `UGenUISettings` (config `Game`, `defaultconfig`, shown in Project Settings → Game → Gen UI):
  - `TSoftObjectPtr<UGenUIPalette> Palette`
  - `TSoftObjectPtr<UGenUIMetrics> Metrics`
  - `TSoftObjectPtr<UGenUIKeyGlyphs> KeyGlyphs`
  - `TSoftClassPtr<UGenPrimaryGameLayout> PrimaryLayoutClass`
  - `TSoftClassPtr<UCommonActivatableWidget> HUDLayoutClass`
- `UGenUIPalette` (`FLinearColor` properties, token names from UI_Guidelines §2): `Text_Primary`, `Text_Secondary`, `Line_Bronze`, `Line_Outline`, `Bg_Panel`, `Cooldown_Overlay`, `Cooldown_Locked`, `Energy_Charging`, `Energy_Full`, `Flash_White`
- `UGenUIMetrics`: `SlotSize` 64, `UltimateSlotSize` 72, `SlotGap` 12, `KeyLabelGap` 2, `ScreenMargin` 32, `CooldownHideBelowTotal` 2.0, `CooldownDesaturation` 0.7, `ReadyFlashDuration` 0.2, `UltimatePulseDuration` 0.3, `UltimateSegments` 4, `CooldownRefreshInterval` 0.05
- `FGenKeyGlyph { TObjectPtr<UTexture2D> Glyph; FText ShortText; }`
- `UGenUIKeyGlyphs { TMap<FKey, FGenKeyGlyph> Glyphs; }`
- `UGenUILibrary::HexToLinear(const FString& Hex, float Alpha = 1.f) -> FLinearColor` (BlueprintPure, so Python can call it)
- `UGenTextBlock : UCommonTextBlock`, an empty subclass for now (text scale fixed at 1)

- [ ] **Step 1: Enable the plugins.** In `Gen.uproject`, add to `"Plugins"`:

```json
		{
			"Name": "CommonUI",
			"Enabled": true
		}
```

  In `Gen.Build.cs`, add to `PublicDependencyModuleNames`:

```csharp
			"UMG",
			"Slate",
			"SlateCore",
			"CommonUI",
			"CommonInput",
			"DeveloperSettings"
```

  In `Config/DefaultEngine.ini`, under `[/Script/Engine.Engine]` (create the section if it's missing):

```ini
GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient
```

- [ ] **Step 2: Create `GenUITags.h/.cpp`**

```cpp
// GenUITags.h
#pragma once

#include "NativeGameplayTags.h"

/** Tags de l'interface (UI_Guidelines §8.1). */
namespace GenUITags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Game);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_GameMenu);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Menu);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Modal);
}
```

```cpp
// GenUITags.cpp
#include "UI/GenUITags.h"

namespace GenUITags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(UI_Layer_Game, "UI.Layer.Game", "Couche HUD (seul WBP_HUDLayout y est empile)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(UI_Layer_GameMenu, "UI.Layer.GameMenu", "Panneaux interactifs en match");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(UI_Layer_Menu, "UI.Layer.Menu", "Menu de jeu, options");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(UI_Layer_Modal, "UI.Layer.Modal", "Confirmations, erreurs");
}
```

- [ ] **Step 3: Create `GenUIDataAssets.h/.cpp`**

```cpp
// GenUIDataAssets.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "GenUIDataAssets.generated.h"

class UTexture2D;

/** Jetons de couleur de l'interface (UI_Guidelines §2.1–2.5). Valeurs remplies depuis les hex via HexToLinear. */
UCLASS(BlueprintType, Const)
class GEN_API UGenUIPalette : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Text") FLinearColor Text_Primary = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Text") FLinearColor Text_Secondary = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lines") FLinearColor Line_Bronze = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lines") FLinearColor Line_Outline = FLinearColor::Black;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Surfaces") FLinearColor Bg_Panel = FLinearColor::Black;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_Overlay = FLinearColor::Black;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_Locked = FLinearColor::Black;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Energy") FLinearColor Energy_Charging = FLinearColor::Yellow;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Energy") FLinearColor Energy_Full = FLinearColor::Yellow;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flash") FLinearColor Flash_White = FLinearColor::White;
};

/** Tailles, seuils et durées de l'interface (UI_Guidelines §3.1, §4.1, §5.2). */
UCLASS(BlueprintType, Const)
class GEN_API UGenUIMetrics : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "px")) float SlotSize = 64.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "px")) float UltimateSlotSize = 72.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "px")) float SlotGap = 12.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "px")) float KeyLabelGap = 2.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Layout", meta = (Units = "px")) float ScreenMargin = 32.f;
	/** Chiffre de recharge caché si la durée totale est inférieure (§4.1, tunable). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "s")) float CooldownHideBelowTotal = 2.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float CooldownDesaturation = 0.7f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion", meta = (Units = "s")) float ReadyFlashDuration = 0.2f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion", meta = (Units = "s")) float UltimatePulseDuration = 0.3f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") int32 UltimateSegments = 4;
	/** Rafraîchissement du balayage et du chiffre pendant une recharge (pas de NativeTick). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "s")) float CooldownRefreshInterval = 0.05f;
};

USTRUCT(BlueprintType)
struct FGenKeyGlyph
{
	GENERATED_BODY()

	/** Glyphe (ex : souris). Prioritaire sur le texte. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<UTexture2D> Glyph = nullptr;

	/** Texte court (ex : SpaceBar -> "SPC"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText ShortText;
};

/** Touche -> glyphe ou texte court (UI_Guidelines §8.3 DA_UIKeyGlyphs). */
UCLASS(BlueprintType, Const)
class GEN_API UGenUIKeyGlyphs : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Keys") TMap<FKey, FGenKeyGlyph> Glyphs;
};
```

```cpp
// GenUIDataAssets.cpp
#include "UI/GenUIDataAssets.h"
```

- [ ] **Step 4: Create `GenUISettings.h/.cpp`**

```cpp
// GenUISettings.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GenUISettings.generated.h"

class UCommonActivatableWidget;
class UGenPrimaryGameLayout;
class UGenUIKeyGlyphs;
class UGenUIMetrics;
class UGenUIPalette;

/** Où trouver les jetons et les widgets racines de l'interface (Project Settings > Game > Gen UI). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Gen UI"))
class GEN_API UGenUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Tokens") TSoftObjectPtr<UGenUIPalette> Palette;
	UPROPERTY(Config, EditAnywhere, Category = "Tokens") TSoftObjectPtr<UGenUIMetrics> Metrics;
	UPROPERTY(Config, EditAnywhere, Category = "Tokens") TSoftObjectPtr<UGenUIKeyGlyphs> KeyGlyphs;
	UPROPERTY(Config, EditAnywhere, Category = "Layout") TSoftClassPtr<UGenPrimaryGameLayout> PrimaryLayoutClass;
	UPROPERTY(Config, EditAnywhere, Category = "Layout") TSoftClassPtr<UCommonActivatableWidget> HUDLayoutClass;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
```

```cpp
// GenUISettings.cpp
#include "UI/GenUISettings.h"
```

- [ ] **Step 5: Create `GenUILibrary.h/.cpp`**

```cpp
// GenUILibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GenUILibrary.generated.h"

UCLASS()
class GEN_API UGenUILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Jeton hexadécimal sRGB -> couleur linéaire (à utiliser pour remplir DA_UIPalette, jamais de valeurs collées). */
	UFUNCTION(BlueprintPure, Category = "Gen|UI")
	static FLinearColor HexToLinear(const FString& Hex, float Alpha = 1.f);
};
```

```cpp
// GenUILibrary.cpp
#include "UI/GenUILibrary.h"

#include "UI/GenUIRules.h"

FLinearColor UGenUILibrary::HexToLinear(const FString& Hex, float Alpha)
{
	return GenUIRules::HexToLinear(Hex, Alpha);
}
```

- [ ] **Step 6: Create `GenTextBlock.h/.cpp`**

```cpp
// GenTextBlock.h
#pragma once

#include "CoreMinimal.h"
#include "CommonTextBlock.h"
#include "GenTextBlock.generated.h"

/** Bloc de texte du jeu (UI_Guidelines §8.8). L'échelle de texte viendra avec les options (fixée à 1 pour l'instant). */
UCLASS()
class GEN_API UGenTextBlock : public UCommonTextBlock
{
	GENERATED_BODY()
};
```

```cpp
// GenTextBlock.cpp
#include "UI/GenTextBlock.h"
```

- [ ] **Step 7: Build, run the unit tests, commit.** Expected: the build succeeds and every test passes.

```bash
git add Gen.uproject Source/Gen/Gen.Build.cs Config/DefaultEngine.ini Source/Gen/UI/GenUITags.* Source/Gen/UI/GenUIDataAssets.* Source/Gen/UI/GenUISettings.* Source/Gen/UI/GenUILibrary.* Source/Gen/UI/GenTextBlock.*
git commit -m "Enable CommonUI; add UI tags, settings, token data assets, HexToLinear and text block"
```

---

### Task 3: UI subsystem ("ASC ready", tokens, key labels) and gameplay hooks

**Files:**
- Create: `Source/Gen/UI/GenUISubsystem.h/.cpp`
- Modify: `Source/Gen/Character/GenPlayerCharacter.cpp`, `Source/Gen/Player/GenPlayerController.h`, `Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h`

**Interfaces:**
- Consumes: `UGenUISettings`, data asset classes (Task 2), `GenUIRules::FallbackKeyLabel` (Task 1).
- Produces:
  - `UGenUISubsystem::Get(const UObject* WorldContextOrLocalPlayerOwner) -> UGenUISubsystem*` (static; accepts a widget, a player controller or a local player)
  - `GetPalette() -> const UGenUIPalette*`, `GetMetrics() -> const UGenUIMetrics*` (never null: falls back to the class defaults)
  - `ResolveKeyLabel(const UInputAction* Action, FText& OutText, UTexture2D*& OutGlyph) const -> bool`
  - `NotifyAbilitySystemReady(UAbilitySystemComponent* ASC)`
  - `CallOrRegister_OnAbilitySystemReady(FGenOnAbilitySystemReady::FDelegate&& Delegate) -> FDelegateHandle`
  - `UnregisterOnAbilitySystemReady(FDelegateHandle)`
  - `GetAbilitySystem() const -> UAbilitySystemComponent*`
  - Delegate type: `DECLARE_MULTICAST_DELEGATE_OneParam(FGenOnAbilitySystemReady, UAbilitySystemComponent*)`
  - `AGenPlayerController::GetInputConfig() const -> const UGenInputConfig*`
  - `UGenGameplayAbility::Icon` (`TSoftObjectPtr<UTexture2D>`, EditDefaultsOnly, BlueprintReadOnly, Category "Gen|UI")

- [ ] **Step 1: Create `GenUISubsystem.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GenUISubsystem.generated.h"

class UAbilitySystemComponent;
class UGenUIKeyGlyphs;
class UGenUIMetrics;
class UGenUIPalette;
class UInputAction;
class UTexture2D;

DECLARE_MULTICAST_DELEGATE_OneParam(FGenOnAbilitySystemReady, UAbilitySystemComponent*);

/**
 * Service d'interface par joueur local (UI_Guidelines §8.3) : jetons, libellés de touches,
 * et l'événement "ASC prêt" auquel les widgets s'abonnent (§8.2, §8.4).
 * Le code de gameplay n'inclut jamais d'en-tête UMG : il appelle seulement NotifyAbilitySystemReady.
 */
UCLASS()
class GEN_API UGenUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UGenUISubsystem* Get(const UObject* WorldContextOrLocalPlayerOwner);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const UGenUIPalette* GetPalette() const;
	const UGenUIMetrics* GetMetrics() const;

	/** Libellé actuel de l'action (mapping Enhanced Input actif) : glyphe si disponible, sinon texte court. */
	bool ResolveKeyLabel(const UInputAction* Action, FText& OutText, UTexture2D*& OutGlyph) const;

	/** Appelé par le pion local quand son ASC est initialisé (OnRep_PlayerState / PossessedBy). */
	void NotifyAbilitySystemReady(UAbilitySystemComponent* ASC);

	/** Appelle tout de suite si l'ASC est déjà prêt, puis à chaque (ré)initialisation. */
	FDelegateHandle CallOrRegister_OnAbilitySystemReady(FGenOnAbilitySystemReady::FDelegate&& Delegate);
	void UnregisterOnAbilitySystemReady(FDelegateHandle Handle);

	UAbilitySystemComponent* GetAbilitySystem() const { return AbilitySystem.Get(); }

private:
	UPROPERTY(Transient) TObjectPtr<const UGenUIPalette> Palette;
	UPROPERTY(Transient) TObjectPtr<const UGenUIMetrics> Metrics;
	UPROPERTY(Transient) TObjectPtr<const UGenUIKeyGlyphs> KeyGlyphs;

	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	FGenOnAbilitySystemReady OnAbilitySystemReady;
};
```

- [ ] **Step 2: Create `GenUISubsystem.cpp`**

```cpp
#include "UI/GenUISubsystem.h"

#include "AbilitySystemComponent.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUIRules.h"
#include "UI/GenUISettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenUI, Log, All);

UGenUISubsystem* UGenUISubsystem::Get(const UObject* WorldContextOrLocalPlayerOwner)
{
	const ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(WorldContextOrLocalPlayerOwner);
	if (!LocalPlayer)
	{
		if (const UUserWidget* Widget = Cast<UUserWidget>(WorldContextOrLocalPlayerOwner))
		{
			LocalPlayer = Widget->GetOwningLocalPlayer();
		}
		else if (const APlayerController* PC = Cast<APlayerController>(WorldContextOrLocalPlayerOwner))
		{
			LocalPlayer = PC->GetLocalPlayer();
		}
	}
	return LocalPlayer ? LocalPlayer->GetSubsystem<UGenUISubsystem>() : nullptr;
}

void UGenUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UGenUISettings* Settings = GetDefault<UGenUISettings>();
	Palette = Settings->Palette.LoadSynchronous();
	Metrics = Settings->Metrics.LoadSynchronous();
	KeyGlyphs = Settings->KeyGlyphs.LoadSynchronous();

	UE_CLOG(!Palette || !Metrics || !KeyGlyphs, LogGenUI, Warning, TEXT("Jetons d'interface manquants dans Project Settings > Game > Gen UI : valeurs par défaut des classes utilisées."));
}

const UGenUIPalette* UGenUISubsystem::GetPalette() const
{
	return Palette ? Palette.Get() : GetDefault<UGenUIPalette>();
}

const UGenUIMetrics* UGenUISubsystem::GetMetrics() const
{
	return Metrics ? Metrics.Get() : GetDefault<UGenUIMetrics>();
}

bool UGenUISubsystem::ResolveKeyLabel(const UInputAction* Action, FText& OutText, UTexture2D*& OutGlyph) const
{
	OutText = FText::GetEmpty();
	OutGlyph = nullptr;

	const UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Action || !InputSubsystem)
	{
		return false;
	}

	const TArray<FKey> Keys = InputSubsystem->QueryKeysMappedToAction(Action);
	const FKey* Key = Keys.FindByPredicate([](const FKey& K) { return K.IsValid() && !K.IsGamepadKey(); });
	if (!Key)
	{
		return false;
	}

	TMap<FKey, FText> ShortTexts;
	if (KeyGlyphs)
	{
		if (const FGenKeyGlyph* Entry = KeyGlyphs->Glyphs.Find(*Key))
		{
			OutGlyph = Entry->Glyph;
			if (!Entry->ShortText.IsEmpty())
			{
				ShortTexts.Add(*Key, Entry->ShortText);
			}
		}
	}

	OutText = GenUIRules::FallbackKeyLabel(*Key, ShortTexts);
	return true;
}

void UGenUISubsystem::NotifyAbilitySystemReady(UAbilitySystemComponent* ASC)
{
	AbilitySystem = ASC;
	UE_LOG(LogGenUI, Log, TEXT("ASC prêt pour l'interface : %s"), *GetNameSafe(ASC));
	OnAbilitySystemReady.Broadcast(ASC);
}

FDelegateHandle UGenUISubsystem::CallOrRegister_OnAbilitySystemReady(FGenOnAbilitySystemReady::FDelegate&& Delegate)
{
	if (AbilitySystem.IsValid())
	{
		Delegate.Execute(AbilitySystem.Get());
	}
	return OnAbilitySystemReady.Add(MoveTemp(Delegate));
}

void UGenUISubsystem::UnregisterOnAbilitySystemReady(FDelegateHandle Handle)
{
	OnAbilitySystemReady.Remove(Handle);
}
```

  Add `"EnhancedInput"` to the Build.cs dependencies if it's missing (it's already present).

- [ ] **Step 3: Notify from the player character.** In `GenPlayerCharacter.cpp`, at the end of `InitAbilitySystemFromPlayerState()` (after `OnAbilitySystemInitialized();`):

```cpp
	// Interface locale (aucun widget sur un serveur dédié ni pour les autres joueurs)
	if (IsLocallyControlled() && GetNetMode() != NM_DedicatedServer)
	{
		if (UGenUISubsystem* UISubsystem = UGenUISubsystem::Get(GetController()))
		{
			UISubsystem->NotifyAbilitySystemReady(AbilitySystemComponent);
		}
	}
```

  Add `#include "UI/GenUISubsystem.h"`. The subsystem header includes no UMG header.

- [ ] **Step 4: In `GenPlayerController.h`**, in `public:`:

```cpp
	const UGenInputConfig* GetInputConfig() const { return InputConfig; }
```

- [ ] **Step 5: In `GenGameplayAbility.h`**, after `DisplayName`:

```cpp
	/** Icône de l'emplacement dans la barre de sorts (UI_Guidelines §2.11 : 256 px, affichée à 64 / 72 px). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|UI")
	TSoftObjectPtr<UTexture2D> Icon;
```

  Add `class UTexture2D;` to the forward declarations.

- [ ] **Step 6: Build, run the unit tests, commit.**

```bash
git add Source/Gen/UI/GenUISubsystem.* Source/Gen/Character/GenPlayerCharacter.cpp Source/Gen/Player/GenPlayerController.h Source/Gen/AbilitySystem/Abilities/GenGameplayAbility.h
git commit -m "UI subsystem: tokens, live key labels, ASC-ready event; ability icon property"
```

---

### Task 4: Root layout, HUD layout, and AGenHUD creates them

**Files:**
- Create: `Source/Gen/UI/GenPrimaryGameLayout.h/.cpp`, `Source/Gen/UI/GenHUDLayout.h/.cpp`
- Modify: `Source/Gen/UI/GenHUD.h/.cpp`

**Interfaces:**
- Produces:
  - `UGenPrimaryGameLayout` with BindWidget `UCommonActivatableWidgetStack* GameLayer, GameMenuLayer, MenuLayer, ModalLayer` and `PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass) -> UCommonActivatableWidget*`
  - `UGenHUDLayout : UCommonActivatableWidget` with the Game input config
  - `AGenHUD` creates the root layout on `BeginPlay` (local player only) and pushes the HUD layout; the text spell line is removed

- [ ] **Step 1: Create `GenPrimaryGameLayout.h/.cpp`**

```cpp
// GenPrimaryGameLayout.h
#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "GenPrimaryGameLayout.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;

/**
 * Racine de l'interface d'un joueur local (UI_Guidelines §8.1, sur le modèle de Lyra, sans ses plugins).
 * Quatre piles par tag : UI.Layer.Game (HUD), GameMenu, Menu, Modal. Seule cette racine est ajoutée au viewport.
 */
UCLASS(Abstract)
class GEN_API UGenPrimaryGameLayout : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Empile un widget sur la couche demandée. Retourne l'instance (nullptr si la couche ou la classe manque). */
	UCommonActivatableWidget* PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	UCommonActivatableWidgetStack* GetLayer(FGameplayTag LayerTag) const;

protected:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> GameLayer;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> GameMenuLayer;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> MenuLayer;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> ModalLayer;
};
```

```cpp
// GenPrimaryGameLayout.cpp
#include "UI/GenPrimaryGameLayout.h"

#include "CommonActivatableWidget.h"
#include "UI/GenUITags.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenUILayout, Log, All);

UCommonActivatableWidgetStack* UGenPrimaryGameLayout::GetLayer(FGameplayTag LayerTag) const
{
	if (LayerTag == GenUITags::UI_Layer_Game) { return GameLayer; }
	if (LayerTag == GenUITags::UI_Layer_GameMenu) { return GameMenuLayer; }
	if (LayerTag == GenUITags::UI_Layer_Menu) { return MenuLayer; }
	if (LayerTag == GenUITags::UI_Layer_Modal) { return ModalLayer; }
	return nullptr;
}

UCommonActivatableWidget* UGenPrimaryGameLayout::PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	UCommonActivatableWidgetStack* Layer = GetLayer(LayerTag);
	if (!Layer || !WidgetClass)
	{
		UE_LOG(LogGenUILayout, Warning, TEXT("Impossible d'empiler %s sur %s"), *GetNameSafe(WidgetClass), *LayerTag.ToString());
		return nullptr;
	}

	UCommonActivatableWidget* Widget = Layer->AddWidget<UCommonActivatableWidget>(WidgetClass);
	UE_LOG(LogGenUILayout, Log, TEXT("Empilé %s sur %s"), *GetNameSafe(Widget), *LayerTag.ToString());
	return Widget;
}
```

- [ ] **Step 2: Create `GenHUDLayout.h/.cpp`**

```cpp
// GenHUDLayout.h
#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "GenHUDLayout.generated.h"

/**
 * Seul widget de la couche UI.Layer.Game (UI_Guidelines §8.1). Le Blueprint WBP_HUDLayout porte la
 * disposition : Canvas racine > SafeZone > Overlay, la barre de sorts en bas au centre (marge 32 px).
 */
UCLASS(Abstract)
class GEN_API UGenHUDLayout : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UGenHUDLayout(const FObjectInitializer& ObjectInitializer);

	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
```

```cpp
// GenHUDLayout.cpp
#include "UI/GenHUDLayout.h"

#include "Input/UIActionBindingHandle.h"

UGenHUDLayout::UGenHUDLayout(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
}

TOptional<FUIInputConfig> UGenHUDLayout::GetDesiredInputConfig() const
{
	// Jeu : les entrées vont au gameplay, la souris reste visible et capturée (§8.1)
	return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown, EMouseLockMode::LockAlways, /*bHideCursorDuringViewportCapture*/ false);
}
```

- [ ] **Step 3: Update `AGenHUD`.**
  - Add `virtual void BeginPlay() override;` and `virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;` in `public:`.
  - In `protected:`, add `UPROPERTY(Transient) TObjectPtr<class UGenPrimaryGameLayout> PrimaryLayout;`.

  Implement:

```cpp
void AGenHUD::BeginPlay()
{
	Super::BeginPlay();

	// Interface UMG : uniquement pour le joueur local, jamais sur un serveur dédié (§8.2)
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->IsLocalController() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UGenUISettings* Settings = GetDefault<UGenUISettings>();
	TSubclassOf<UGenPrimaryGameLayout> LayoutClass = Settings->PrimaryLayoutClass.LoadSynchronous();
	if (!LayoutClass)
	{
		return;
	}

	PrimaryLayout = CreateWidget<UGenPrimaryGameLayout>(PC, LayoutClass);
	PrimaryLayout->AddToPlayerScreen(1000); // la racine est le seul widget ajouté à l'écran (§8.1)
	PrimaryLayout->PushWidgetToLayer(GenUITags::UI_Layer_Game, Settings->HUDLayoutClass.LoadSynchronous());
}

void AGenHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PrimaryLayout)
	{
		PrimaryLayout->RemoveFromParent();
		PrimaryLayout = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
```

  Includes: `"UI/GenPrimaryGameLayout.h"`, `"UI/GenUISettings.h"`, `"UI/GenUITags.h"`, `"CommonActivatableWidget.h"`, `"Blueprint/UserWidget.h"`.

  Then in `DrawLocalPlayerPanel`, **delete** the whole block that starts with the comment `// Sorts + cooldowns` and ends with the `for` loop over `GetActivatableAbilities()`. That is the text spell line; `WBP_AbilityBar` replaces it.

- [ ] **Step 4: Build, run the unit tests, commit.**

```bash
git add Source/Gen/UI/GenPrimaryGameLayout.* Source/Gen/UI/GenHUDLayout.* Source/Gen/UI/GenHUD.h Source/Gen/UI/GenHUD.cpp
git commit -m "CommonUI root and HUD layouts; AGenHUD creates them; text spell line removed"
```

---

### Task 5: Ability slot and ability bar widgets (C++)

**Files:**
- Create: `Source/Gen/UI/GenAbilitySlot.h/.cpp`, `Source/Gen/UI/GenAbilityBar.h/.cpp`

**Interfaces:**
- Consumes:
  - `UGenUISubsystem` (Task 3), `GenUIRules` (Task 1), `UGenUIMetrics` / `UGenUIPalette` (Task 2)
  - `UGenGameplayAbility::{InputTag, DisplayName, Icon, CooldownTags, GetCooldownTimeRemainingAndDuration}`
  - `UGenInputConfig::AbilityInputActions`, `GenGameplayTags::State_Stunned`, `UGenAttributeSet::{GetEnergyAttribute, GetMaxEnergyAttribute}`
- Produces:
  - **`UGenAbilitySlot`.**
    - BindWidget: `UImage* IconImage`, `UImage* SweepImage` (material `M_UI_CooldownSweep`), `UGenTextBlock* KeyText`, `UGenTextBlock* CooldownText`, `UImage* LockImage`.
    - BindWidgetOptional: `UImage* KeyGlyphImage`, `UImage* ArcImage` (material `M_UI_SegmentArc`, ultimate only).
    - EditAnywhere: `FGameplayTag InputTag`, `bool bIsUltimate`.
    - API: `Bind(UAbilitySystemComponent*)`, `Unbind()`, `GetState() -> EGenAbilitySlotState`, `GetCooldownText() -> FString` (read by the PIE tests).
  - **`UGenAbilityBar`.**
    - BindWidget: `UGenAbilitySlot* SlotPrimary, SlotSecondary, SlotMobility, Slot1, Slot2, Slot3, SlotUltimate`.
    - Binds all of them on "ASC ready".
  - **Material parameters:**
    - `M_UI_CooldownSweep`: `Progress` (remaining fraction 0..1), `DimAmount` (0..1), `RimFlash` (0..1), `RimColour` (vector), `OverlayColour` (vector), `Locked` (0/1)
    - `M_UI_SegmentArc`: `Segments`, `Funded`, `Colour`, `FullOutline`

- [ ] **Step 1: Create `GenAbilitySlot.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "UI/GenUIRules.h"
#include "GenAbilitySlot.generated.h"

class UAbilitySystemComponent;
class UGenGameplayAbility;
class UGenTextBlock;
class UImage;
class UMaterialInstanceDynamic;
struct FOnAttributeChangeData;

/**
 * Un emplacement de la barre de sorts (UI_Guidelines §4.1). Logique en C++, disposition et style dans WBP_AbilitySlot.
 * Piloté par événements (tags de recharge, étourdissement, énergie) ; un minuteur court ne tourne que
 * pendant une recharge ou un flash, jamais de NativeTick (§8.4).
 */
UCLASS(Abstract)
class GEN_API UGenAbilitySlot : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	void Bind(UAbilitySystemComponent* InASC);
	void Unbind();

	UFUNCTION(BlueprintPure, Category = "Gen|UI") EGenAbilitySlotState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Gen|UI") FString GetCooldownText() const { return CooldownString; }

	/** Tag d'entrée du sort affiché (InputTag.Ability.*). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gen|UI", meta = (Categories = "InputTag")) FGameplayTag InputTag;

	/** Emplacement de l'ultime : arc d'énergie toujours visible, impulsion quand l'énergie est pleine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gen|UI") bool bIsUltimate = false;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> IconImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> SweepImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenTextBlock> KeyText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenTextBlock> CooldownText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> LockImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> KeyGlyphImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> ArcImage;

private:
	void ResolveAbility();
	void RefreshKeyLabel();
	void RefreshCooldown();
	void RefreshVisuals();
	void StartRefreshTimer();
	void TickRefresh();
	void StartFlash(float Duration);
	void OnCooldownTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnStunTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnEnergyChanged(const FOnAttributeChangeData& Data);
	void UpdateUltimateArc();

	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayAbilitySpecHandle SpecHandle;
	TWeakObjectPtr<const UGenGameplayAbility> AbilityCDO;
	FGameplayTagContainer CooldownTags;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SweepMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArcMID;

	TArray<TPair<FGameplayTag, FDelegateHandle>> TagHandles;
	FDelegateHandle EnergyHandle;
	FTimerHandle RefreshTimer;
	FTimerHandle ResolveRetryTimer;

	EGenAbilitySlotState State = EGenAbilitySlotState::Empty;
	FString CooldownString;
	float CooldownEndTime = 0.f;
	float CooldownDuration = 0.f;
	float FlashStartTime = -1.f;
	float FlashDuration = 0.f;
	bool bLocked = false;
	bool bUltimateWasFull = false;
	int32 ResolveAttempts = 0;
};
```

- [ ] **Step 2: Create `GenAbilitySlot.cpp`**

```cpp
#include "UI/GenAbilitySlot.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GenGameplayTags.h"
#include "Input/GenInputConfig.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/GenPlayerController.h"
#include "TimerManager.h"
#include "UI/GenTextBlock.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUISubsystem.h"

namespace
{
	/** Le sort n'est pas encore répliqué au client quand l'ASC est prêt : on réessaie un peu. */
	constexpr float ResolveRetryInterval = 0.25f;
	constexpr int32 ResolveRetryMax = 40;
}

void UGenAbilitySlot::NativeConstruct()
{
	Super::NativeConstruct();

	if (SweepImage)
	{
		SweepMID = SweepImage->GetDynamicMaterial();
	}
	if (ArcImage)
	{
		ArcMID = ArcImage->GetDynamicMaterial();
		ArcImage->SetVisibility(bIsUltimate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	RefreshVisuals();
}

void UGenAbilitySlot::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UGenAbilitySlot::Bind(UAbilitySystemComponent* InASC)
{
	Unbind();
	ASC = InASC;
	ResolveAttempts = 0;

	if (!ASC.IsValid())
	{
		RefreshVisuals();
		return;
	}

	// Étourdi => bloqué (§4.1 Locked)
	FDelegateHandle StunHandle = ASC->RegisterGameplayTagEvent(GenGameplayTags::State_Stunned, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::OnStunTagChanged);
	TagHandles.Emplace(GenGameplayTags::State_Stunned, StunHandle);
	bLocked = ASC->HasMatchingGameplayTag(GenGameplayTags::State_Stunned);

	if (bIsUltimate)
	{
		EnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);
		bUltimateWasFull = GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), 4) == 4;
	}

	ResolveAbility();
	RefreshKeyLabel();
}

void UGenAbilitySlot::Unbind()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
		World->GetTimerManager().ClearTimer(ResolveRetryTimer);
	}

	if (ASC.IsValid())
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Pair : TagHandles)
		{
			ASC->RegisterGameplayTagEvent(Pair.Key, EGameplayTagEventType::NewOrRemoved).Remove(Pair.Value);
		}
		if (EnergyHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).Remove(EnergyHandle);
		}
	}

	TagHandles.Reset();
	EnergyHandle.Reset();
	ASC.Reset();
	SpecHandle = FGameplayAbilitySpecHandle();
	AbilityCDO.Reset();
	CooldownTags.Reset();
	CooldownEndTime = 0.f;
	CooldownDuration = 0.f;
}

void UGenAbilitySlot::ResolveAbility()
{
	if (!ASC.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UGenGameplayAbility* Ability = Cast<UGenGameplayAbility>(Spec.Ability);
		if (Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			SpecHandle = Spec.Handle;
			AbilityCDO = Ability;
			if (const FGameplayTagContainer* Tags = Ability->GetCooldownTags())
			{
				CooldownTags = *Tags;
			}
			break;
		}
	}

	if (!AbilityCDO.IsValid())
	{
		// Les sorts donnés par le serveur arrivent après l'ASC côté client : on réessaie
		if (++ResolveAttempts <= ResolveRetryMax && GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(ResolveRetryTimer, this, &ThisClass::ResolveAbility, ResolveRetryInterval, false);
		}
		RefreshVisuals();
		return;
	}

	for (const FGameplayTag& Tag : CooldownTags)
	{
		FDelegateHandle Handle = ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::OnCooldownTagChanged);
		TagHandles.Emplace(Tag, Handle);
	}

	if (UTexture2D* Icon = AbilityCDO->Icon.LoadSynchronous())
	{
		IconImage->SetBrushFromTexture(Icon);
	}
	IconImage->SetToolTipText(AbilityCDO->DisplayName);

	RefreshCooldown();
}

void UGenAbilitySlot::RefreshKeyLabel()
{
	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	const AGenPlayerController* PC = Cast<AGenPlayerController>(GetOwningPlayer());
	const UGenInputConfig* InputConfig = PC ? PC->GetInputConfig() : nullptr;
	if (!UI || !InputConfig)
	{
		return;
	}

	const UInputAction* Action = nullptr;
	for (const FGenAbilityInputAction& Binding : InputConfig->AbilityInputActions)
	{
		if (Binding.InputTag == InputTag)
		{
			Action = Binding.InputAction;
			break;
		}
	}

	FText Label;
	UTexture2D* Glyph = nullptr;
	UI->ResolveKeyLabel(Action, Label, Glyph);

	if (KeyGlyphImage && Glyph)
	{
		KeyGlyphImage->SetBrushFromTexture(Glyph);
		KeyGlyphImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		KeyText->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		if (KeyGlyphImage)
		{
			KeyGlyphImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		KeyText->SetText(Label);
		KeyText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UGenAbilitySlot::RefreshCooldown()
{
	float Remaining = 0.f;
	float Duration = 0.f;
	if (ASC.IsValid() && AbilityCDO.IsValid() && SpecHandle.IsValid())
	{
		AbilityCDO->GetCooldownTimeRemainingAndDuration(SpecHandle, ASC->AbilityActorInfo.Get(), Remaining, Duration);
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const bool bWasCooling = CooldownEndTime > Now;
	CooldownEndTime = Remaining > 0.f ? Now + Remaining : 0.f;
	CooldownDuration = Duration;

	if (Remaining > 0.f)
	{
		StartRefreshTimer();
	}
	else if (bWasCooling)
	{
		// Fin de recharge : flash du bord (§4.1 Ready flash)
		StartFlash(UGenUISubsystem::Get(this) ? UGenUISubsystem::Get(this)->GetMetrics()->ReadyFlashDuration : 0.2f);
	}

	RefreshVisuals();
}

void UGenAbilitySlot::StartRefreshTimer()
{
	if (UWorld* World = GetWorld())
	{
		const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
		const float Interval = UI ? UI->GetMetrics()->CooldownRefreshInterval : 0.05f;
		if (!World->GetTimerManager().IsTimerActive(RefreshTimer))
		{
			World->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::TickRefresh, Interval, true);
		}
	}
}

void UGenAbilitySlot::TickRefresh()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const bool bCooling = CooldownEndTime > Now;
	const bool bFlashing = FlashStartTime >= 0.f && Now - FlashStartTime < FlashDuration;

	if (!bCooling && CooldownEndTime > 0.f)
	{
		// Recharge terminée localement : on relit l'ASC (autorité) puis on flashe
		RefreshCooldown();
		return;
	}

	if (!bCooling && !bFlashing)
	{
		FlashStartTime = -1.f;
		GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
	}

	RefreshVisuals();
}

void UGenAbilitySlot::StartFlash(float Duration)
{
	FlashStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	FlashDuration = FMath::Max(Duration, 0.01f);
	StartRefreshTimer();
}

void UGenAbilitySlot::RefreshVisuals()
{
	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	const UGenUIMetrics* Metrics = UI ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	const UGenUIPalette* Palette = UI ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Remaining = FMath::Max(CooldownEndTime - Now, 0.f);

	State = GenUIRules::ResolveSlotState(AbilityCDO.IsValid(), bLocked, Remaining);
	CooldownString = State == EGenAbilitySlotState::Cooldown ? GenUIRules::FormatCooldown(Remaining, CooldownDuration, Metrics->CooldownHideBelowTotal) : FString();

	IconImage->SetVisibility(State == EGenAbilitySlotState::Empty ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	CooldownText->SetText(FText::FromString(CooldownString));
	CooldownText->SetVisibility(CooldownString.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	LockImage->SetVisibility(State == EGenAbilitySlotState::Locked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);

	if (SweepMID)
	{
		const float Progress = (State == EGenAbilitySlotState::Cooldown && CooldownDuration > 0.f) ? Remaining / CooldownDuration : 0.f;
		float Flash = 0.f;
		if (FlashStartTime >= 0.f && FlashDuration > 0.f)
		{
			// Ease-out (§5.1) : 1 -> 0 sur la durée du flash
			const float Alpha = FMath::Clamp((Now - FlashStartTime) / FlashDuration, 0.f, 1.f);
			Flash = 1.f - FMath::InterpEaseOut(0.f, 1.f, Alpha, 3.f);
		}
		SweepMID->SetScalarParameterValue(TEXT("Progress"), Progress);
		SweepMID->SetScalarParameterValue(TEXT("DimAmount"), State == EGenAbilitySlotState::Cooldown ? Metrics->CooldownDesaturation : 0.f);
		SweepMID->SetScalarParameterValue(TEXT("RimFlash"), Flash);
		SweepMID->SetScalarParameterValue(TEXT("Locked"), State == EGenAbilitySlotState::Locked ? 1.f : 0.f);
		SweepMID->SetVectorParameterValue(TEXT("OverlayColour"), State == EGenAbilitySlotState::Locked ? Palette->Cooldown_Locked : Palette->Cooldown_Overlay);
		SweepMID->SetVectorParameterValue(TEXT("RimColour"), bIsUltimate ? Palette->Energy_Full : Palette->Line_Bronze);
		SweepMID->SetVectorParameterValue(TEXT("FlashColour"), Palette->Flash_White);
	}

	if (bIsUltimate)
	{
		UpdateUltimateArc();
	}
}

void UGenAbilitySlot::OnCooldownTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RefreshCooldown();
}

void UGenAbilitySlot::OnStunTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	bLocked = NewCount > 0;
	RefreshVisuals();
}

void UGenAbilitySlot::OnEnergyChanged(const FOnAttributeChangeData& Data)
{
	RefreshVisuals();
}

void UGenAbilitySlot::UpdateUltimateArc()
{
	if (!ArcMID || !ASC.IsValid())
	{
		return;
	}

	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	const UGenUIMetrics* Metrics = UI ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	const UGenUIPalette* Palette = UI ? UI->GetPalette() : GetDefault<UGenUIPalette>();

	const int32 Funded = GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()),
		ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), Metrics->UltimateSegments);
	const bool bFull = Funded == Metrics->UltimateSegments;

	ArcMID->SetScalarParameterValue(TEXT("Segments"), Metrics->UltimateSegments);
	ArcMID->SetScalarParameterValue(TEXT("Funded"), Funded);
	ArcMID->SetScalarParameterValue(TEXT("FullOutline"), bFull ? 1.f : 0.f);
	ArcMID->SetVectorParameterValue(TEXT("Colour"), bFull ? Palette->Energy_Full : Palette->Energy_Charging);

	// Ultime prête : une seule impulsion de 300 ms, jamais de boucle (§4.1)
	if (bFull && !bUltimateWasFull)
	{
		StartFlash(Metrics->UltimatePulseDuration);
	}
	bUltimateWasFull = bFull;
}
```

- [ ] **Step 3: Create `GenAbilityBar.h/.cpp`**

```cpp
// GenAbilityBar.h
#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GenAbilityBar.generated.h"

class UAbilitySystemComponent;
class UGenAbilitySlot;

/** Barre de sorts : 7 emplacements dans l'ordre §4.1 (LMB, RMB, Espace, 1, 2, 3, Ultime). */
UCLASS(Abstract)
class GEN_API UGenAbilityBar : public UCommonUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotPrimary;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotSecondary;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotMobility;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> Slot1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> Slot2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> Slot3;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotUltimate;

private:
	void HandleAbilitySystemReady(UAbilitySystemComponent* ASC);
	TArray<UGenAbilitySlot*> GetSlots() const;

	FDelegateHandle ReadyHandle;
};
```

```cpp
// GenAbilityBar.cpp
#include "UI/GenAbilityBar.h"

#include "GenGameplayTags.h"
#include "UI/GenAbilitySlot.h"
#include "UI/GenUISubsystem.h"

void UGenAbilityBar::NativeConstruct()
{
	Super::NativeConstruct();

	// Ordre et tags des emplacements (§4.1) ; l'ultime porte l'arc d'énergie
	const TPair<UGenAbilitySlot*, FGameplayTag> Layout[] = {
		{ SlotPrimary, GenGameplayTags::InputTag_Ability_Primary },
		{ SlotSecondary, GenGameplayTags::InputTag_Ability_Secondary },
		{ SlotMobility, GenGameplayTags::InputTag_Ability_Mobility },
		{ Slot1, GenGameplayTags::InputTag_Ability_1 },
		{ Slot2, GenGameplayTags::InputTag_Ability_2 },
		{ Slot3, GenGameplayTags::InputTag_Ability_3 },
		{ SlotUltimate, GenGameplayTags::InputTag_Ability_Ultimate },
	};
	for (const TPair<UGenAbilitySlot*, FGameplayTag>& Entry : Layout)
	{
		Entry.Key->InputTag = Entry.Value;
		Entry.Key->bIsUltimate = (Entry.Key == SlotUltimate);
	}

	if (UGenUISubsystem* UI = UGenUISubsystem::Get(this))
	{
		ReadyHandle = UI->CallOrRegister_OnAbilitySystemReady(FGenOnAbilitySystemReady::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemReady));
	}
}

void UGenAbilityBar::NativeDestruct()
{
	if (UGenUISubsystem* UI = UGenUISubsystem::Get(this))
	{
		UI->UnregisterOnAbilitySystemReady(ReadyHandle);
	}
	for (UGenAbilitySlot* SlotWidget : GetSlots())
	{
		SlotWidget->Unbind();
	}
	Super::NativeDestruct();
}

void UGenAbilityBar::HandleAbilitySystemReady(UAbilitySystemComponent* ASC)
{
	for (UGenAbilitySlot* SlotWidget : GetSlots())
	{
		SlotWidget->Bind(ASC);
	}
}

TArray<UGenAbilitySlot*> UGenAbilityBar::GetSlots() const
{
	return { SlotPrimary, SlotSecondary, SlotMobility, Slot1, Slot2, Slot3, SlotUltimate };
}
```

  Note: the slots' `NativeConstruct` runs before the bar's, so `bIsUltimate` set here arrives after the slot already built `ArcImage`'s visibility. `Bind` must therefore reapply the arc visibility from `bIsUltimate`. The implementer adds that line at the start of `UGenAbilitySlot::Bind`:

```cpp
	if (ArcImage)
	{
		ArcImage->SetVisibility(bIsUltimate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
```

- [ ] **Step 4: Build, run the unit tests, commit.** If a GAS or UMG API differs on 5.8, adapt minimally and note it in the report: the `RegisterGameplayTagEvent(...).Remove`, `GetDynamicMaterial`, `GetOwningLocalPlayer`, `AddToPlayerScreen` and `GetDynamicSpecSourceTags` calls.

```bash
git add Source/Gen/UI/GenAbilitySlot.* Source/Gen/UI/GenAbilityBar.*
git commit -m "Ability slot and bar widgets: event-driven cooldown, lock, ultimate arc, ready flash"
```

---

### Task 6: Fonts, styles, materials, data assets and project settings (editor)

The editor is open with the new build. Load the VibeUE skills `VibeUE_umg_widgets`, `VibeUE_materials` and `VibeUE_asset_management` first.

**Assets:**
- `Content/Gen/UI/Fonts/`:
  - `FF_Barlow_SemiBold` and `FF_Barlow_Bold` (Font Face, **Runtime** loading)
  - `FNT_Barlow` (composite font, Default typeface entries SemiBold and Bold)
- `Content/Gen/UI/Foundation/`:
  - `TS_Label`, `TS_Cooldown` (`CommonTextStyle` Blueprints, §2.6)
  - `DA_UIPalette`, `DA_UIMetrics`, `DA_UIKeyGlyphs`
  - `DA_CommonUIInputData` (child of `CommonUIInputData` with `EnhancedInputClickAction` / `EnhancedInputBackAction`)
  - `IA_UI_Click`, `IA_UI_Back`
- `Content/Gen/UI/Materials/`: `M_UI_CooldownSweep`, `M_UI_SegmentArc`

**Steps:**

- [ ] **Step 1: Download Barlow** (SIL OFL 1.1) into a new empty scratch folder outside the repo. Use the raw files at `https://raw.githubusercontent.com/google/fonts/main/ofl/barlow/Barlow-SemiBold.ttf` and `.../Barlow-Bold.ttf`, plus `OFL.txt`.
  - Import both files as Font Faces with **Runtime** loading.
  - Create `FNT_Barlow`. Commit `OFL.txt` as `Content/Gen/UI/Fonts/OFL.txt` for the licence.
  - Check that the default figures are equal-width (§2.6): render "0123456789" and "1111111111" at 28 px and compare their widths. If they differ, report it; the cooldown number then needs a fixed-width box, which goes into Task 7 as a ruling.

- [ ] **Step 2: Create the text styles** (§2.6). Slate size = px × 0.75.

  | Style | Font | Weight | Size (Slate) | Colour | Extras |
  |---|---|---|---|---|---|
  | `TS_Label` | `FNT_Barlow` | SemiBold | 15 | `text.primary` | CAPS, letter spacing about +6% (Slate letter spacing 60), no outline |
  | `TS_Cooldown` | `FNT_Barlow` | Bold | 21 | `text.primary` | 1 px outline in `line.outline` |

  Colours come from `UGenUILibrary.hex_to_linear`, never pasted.

- [ ] **Step 3: Fill the data assets.** Use `unreal.GenUILibrary.hex_to_linear` with these tokens:

  | Token | Hex | α |
  |---|---|---|
  | `Text_Primary` | `#F3DEC9` | 1 |
  | `Text_Secondary` | `#C2A893` | 1 |
  | `Line_Bronze` | `#B07A52` | 0.35 |
  | `Line_Outline` | `#070705` | 0.90 |
  | `Bg_Panel` | `#20160F` | 0.80 |
  | `Cooldown_Overlay` | `#110C09` | 0.75 |
  | `Cooldown_Locked` | `#110C09` | 0.70 |
  | `Energy_Charging` | `#FFC233` | 1 |
  | `Energy_Full` | `#FFC233` | 1 |
  | `Flash_White` | `#FFFFFF` | 1 |

  - First check each value against UI_Guidelines §2.1–2.5 and use the guide's value if they differ.
  - `DA_UIMetrics` keeps its class defaults.
  - `DA_UIKeyGlyphs`:
    - `SpaceBar` → ShortText "SPC";
    - `LeftMouseButton` and `RightMouseButton` → glyph textures `T_UI_Glyph_LMB` / `T_UI_Glyph_RMB` from Task 7 (fill them in once Task 7 has made them; leave them empty for now).

- [ ] **Step 4: Build the materials.** Both use the User Interface domain, Translucent, with a Custom HLSL node (VibeUE `MaterialNodeService`).

  **`M_UI_CooldownSweep`**, the overlay drawn on top of the icon. Parameters: `Progress`, `DimAmount`, `RimFlash`, `Locked`, `OverlayColour`, `RimColour`, `FlashColour`.

  ```hlsl
  float2 p = UV - 0.5;                          // centre
  float r = length(p) * 2.0;                    // 0 au centre, 1 au bord
  float inside = step(r, 1.0);
  // Angle depuis 12 h, sens horaire, 0..1
  float a = frac(atan2(p.x, -p.y) / 6.2831853 + 1.0);
  // Le coin sombre couvre [0, Progress] et rétrécit dans le sens horaire quand Progress baisse
  float wedge = step(a, Progress) * step(0.0001, Progress);
  // Hachures 45° pour l'état bloqué
  float hatch = step(0.5, frac((UV.x + UV.y) * 8.0));
  float overlayA = max(wedge, Locked * hatch) * OverlayColour.a;
  // Bord : anneau de 1 px environ (UV relatif, slot 64 px => 1/32)
  float rim = smoothstep(0.94, 0.97, r) * inside;
  float3 rimCol = lerp(RimColour.rgb, FlashColour.rgb, RimFlash);
  float rimA = rim * max(RimColour.a, RimFlash);
  float3 col = lerp(OverlayColour.rgb, rimCol, rim);
  float alpha = saturate(max(overlayA * inside * (1.0 - rim), rimA) + DimAmount * 0.0);
  return float4(col, alpha);
  ```

  The desaturation (`DimAmount`) applies to the **icon**, not this overlay. Give `IconImage` a second material `M_UI_AbilityIcon` (a texture parameter `Icon`; output `lerp(icon, luminance, DimAmount)`, masked by a circle). In that case the slot sets `DimAmount` on the icon's MID as well. Record that in the report; the slot code change is part of Task 7 Step 3.

  **`M_UI_SegmentArc`**: a 120° arc under the slot, 4 px thick (about 0.06 in UV at 72 px), 4 segments with 4° gaps. Funded segments use `Colour`; unfunded ones show a 1 px hollow outline in `text.secondary` (pass it as `HollowColour`). `FullOutline` adds a thin outline around the whole arc.

  Save both, and check them in the material preview with Progress = 0.25 / 0.75 and Funded = 0..4 (capture images).

- [ ] **Step 5: CommonUI input data and settings.**
  - Create `IA_UI_Click` (Boolean) and `IA_UI_Back` (Boolean) in `Content/Gen/UI/Foundation/`, and map them in the existing `IMC_Arena`: `LeftMouseButton` → Click, `Escape` → Back. Lock `Content/Gen/Input/IMC_Arena.uasset` first.
  - Create `DA_CommonUIInputData` (child class of `CommonUIInputData`) and set `EnhancedInputClickAction` and `EnhancedInputBackAction`.
  - In `Config/DefaultGame.ini`:

```ini
[/Script/CommonInput.CommonInputSettings]
bEnableEnhancedInputSupport=True
InputData=/Game/Gen/UI/Foundation/DA_CommonUIInputData.DA_CommonUIInputData_C

[/Script/Gen.GenUISettings]
Palette=/Game/Gen/UI/Foundation/DA_UIPalette.DA_UIPalette
Metrics=/Game/Gen/UI/Foundation/DA_UIMetrics.DA_UIMetrics
KeyGlyphs=/Game/Gen/UI/Foundation/DA_UIKeyGlyphs.DA_UIKeyGlyphs
PrimaryLayoutClass=/Game/Gen/UI/HUD/WBP_PrimaryGameLayout.WBP_PrimaryGameLayout_C
HUDLayoutClass=/Game/Gen/UI/HUD/WBP_HUDLayout.WBP_HUDLayout_C
```

  The two widget classes are created in Task 7; these paths are where Task 7 must create them.

- [ ] **Step 6: Read everything back** (palette values, style font sizes, material parameter names), then save **only** the new or changed assets and commit:

```bash
git add Content/Gen/UI Content/Gen/Input/IMC_Arena.uasset Config/DefaultGame.ini
git commit -m "UI foundation assets: Barlow, TS_Label/TS_Cooldown, palette/metrics/glyph data, sweep and arc materials, CommonUI input data"
```

---

### Task 7: Icons, glyphs and widget Blueprints (editor)

**Assets:**
- `Content/Python/gen_ui_icons.py` (new; it generates PNGs)
- `Content/Gen/UI/Textures/Icons/Abilities/T_UI_Ability_Curffe_{Primary,Secondary,Mobility}`
- `Content/Gen/UI/Textures/T_UI_Glyph_{LMB,RMB,Lock}`
- `Content/Gen/UI/HUD/WBP_PrimaryGameLayout`, `WBP_HUDLayout`, `WBP_AbilityBar`, `WBP_AbilitySlot`

- [ ] **Step 1: Write the icon generator** `Content/Python/gen_ui_icons.py`. Use Pillow if the editor's Python has it (`import PIL`); otherwise install it with `unreal`'s pip or `"<UE>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -m pip install pillow`. It draws 256×256 RGBA PNGs to `Saved/UIIcons/`:
  - **Look:** flat, painterly-style placeholders, one top-left light direction, a limited palette per champion (fire hues `#FFF0C2`, `#F5B82E`, `#7A5230` from Art Bible §7.4), a dark circular background `#20160F`, no text.
  - **Primary** (Fireball): a small round comet with a tapered tail toward the bottom-left.
  - **Secondary** (Great Fireball): a large orb with 3 orbiting flame dots, so it reads as "fed".
  - **Mobility** (Meteor Leap): an arc trajectory with a landing burst.
  - **Glyphs:** a white 2 px-stroke mouse outline with the left or right button filled, 64 px (for the 20 px label); a lock glyph 64 px.
  - **Tests (§2.11):** save greyscale, 2 px-blurred and 32 px versions next to them in `Saved/UIIcons/checks/`, and verify the three icons stay distinguishable. Compare their greyscale mean luminance and silhouettes, and capture the contact sheet image into the report.

- [ ] **Step 2: Import and assign.**
  - Import the PNGs as textures: compression **UserInterface2D**, no mips, texture group UI, sRGB on.
  - Set `Icon` on the CDOs of `GA_Fireball`, `GA_GreatFireball` and `GA_FlameLeap` (in `/Game/Gen/Champions/Curffe/Abilities/`; lock them first). Compile and save.
  - Fill `DA_UIKeyGlyphs` with the LMB and RMB glyphs (lock it first).

- [ ] **Step 3: Build the widget Blueprints** with VibeUE `WidgetService`. Each one is a Blueprint subclass of its C++ class, with the BindWidget names **exactly** as declared.
  - **`WBP_AbilitySlot`** (parent `GenAbilitySlot`): a SizeBox (64×64, overridden to 72 on the ultimate via the bar) containing a VerticalBox. Inside it:
    - `KeyText` (`GenTextBlock`, style `TS_Label`, centred) or `KeyGlyphImage` (20 px), then a 2 px spacer;
    - an Overlay holding `IconImage` (material `M_UI_AbilityIcon`), `SweepImage` (material `M_UI_CooldownSweep`), `CooldownText` (`TS_Cooldown`, centred) and `LockImage` (`T_UI_Glyph_Lock`, 20 px, centred);
    - `ArcImage` (material `M_UI_SegmentArc`, 72×12) under the overlay.
    - No hard-coded colours: the colours come from the styles and the materials' parameters, which the C++ sets.
    - If the icon material needs `DimAmount`, update `UGenAbilitySlot` to cache an `IconMID` and set `DimAmount` on it in `RefreshVisuals`. That's the planned slot change from Task 6 Step 4.
  - **`WBP_AbilityBar`** (parent `GenAbilityBar`): a HorizontalBox with `SlotPrimary`, `SlotSecondary`, `SlotMobility`, `Slot1`, `Slot2`, `Slot3`, `SlotUltimate`, with 12 px Spacers between them.
  - **`WBP_HUDLayout`** (parent `GenHUDLayout`):
    - root **Canvas Panel** (the only one, §8.7) → SafeZone filling the screen → Overlay;
    - `WBP_AbilityBar` in an Overlay slot aligned bottom-centre, with 32 px bottom padding.
  - **`WBP_PrimaryGameLayout`** (parent `GenPrimaryGameLayout`): an Overlay with the 4 `CommonActivatableWidgetStack`s `GameLayer`, `GameMenuLayer`, `MenuLayer` and `ModalLayer`, each filling the screen.
  - Set Project Settings → Widget Designer (Team) → **Property Binding Rule = Prevent and Error** (§8.4).
  - Compile every widget Blueprint with no errors or warnings, and capture `WidgetService.capture_preview` of `WBP_AbilityBar`.

- [ ] **Step 4: Smoke test in PIE** (Standalone, 1 player).
  - The bar shows at the bottom centre with LMB/RMB glyphs, "SPC", A/E/R/F labels (AZERTY: the existing mapping for slot 1 is A), three icons, and four empty slots with only their frame and key.
  - The old text spell line is gone.
  - LMB still fires, RMB still charges, movement still works.
  - Capture an image.

- [ ] **Step 5: Commit**

```bash
git add Content/Python/gen_ui_icons.py Content/Gen/UI Content/Gen/Champions/Curffe/Abilities Source/Gen/UI/GenAbilitySlot.h Source/Gen/UI/GenAbilitySlot.cpp Config
git commit -m "Ability bar widgets, generated placeholder icons and key glyphs"
```

---

### Task 8: Behaviour fixes found by the smoke test (if any)

If Task 7 Step 4 shows a defect (input blocked, slot unresolved, wrong label, visual not updating):
- debug it with the systematic-debugging skill;
- fix it in the owning file;
- rebuild if it's C++;
- rerun Task 7 Step 4.

Commit each fix separately. If there's nothing to fix, say so in the report and make no commit.

---

### Task 9: PIE verification (dedicated server + 2 clients)

Use `Content/Python/gen_pie_tools.py` (already on the branch; it has world, pawn, aim and input helpers). Add a helper to find the local `WBP_AbilityBar` slots in a client world (`unreal.GameplayStatics` or `unreal.WidgetBlueprintLibrary.get_all_widgets_of_class(world, unreal.GenAbilitySlot, False)`) and read `get_state()` / `get_cooldown_text()`.

| # | Scenario | Expected |
|---|---|---|
| V1 | **Input still works** | On clients 1 and 2: movement, LMB (Fireball fires, server log `Projectile … créé`), RMB hold (fed Great Fireball) and Space all work with the HUD layout active |
| V2 | **Binding** | Client 1: Primary, Secondary and Mobility slots are `Ready` with icons; slots 1, 2, 3 and Ultimate are `Empty`. Labels: LMB and RMB glyphs, "SPC", then the live keys for 1/2/3/F |
| V3 | **Cooldown** | Client 1 fires a Great Fireball: the Secondary slot turns `Cooldown` within one refresh of the cast (predicted). Its text counts 6 → 1, then "0.9" … "0.1", then empty with a ready flash (RimFlash > 0 for about 200 ms, then 0). LMB never shows a number (cooldown 0 < 2 s) |
| V4 | **Latency** | `NetEmulation.PktLag 150` on the clients: V3 again. The cooldown appears on the press-release frame, the countdown ends within 0.2 s of the server's cooldown end (compare `GetCooldownTimeRemainingAndDuration` on the server). A Great Fireball cancelled by a fresh LMB press shows **no** cooldown |
| V5 | **Rebinding** | Remap Slot 1's action to Q in the user mapping at runtime (or swap IMC entries): the label updates after `RefreshKeyLabel` (rebind once `Bind` is called again by a respawn; live rebind events are out of scope, so note it) |
| V6 | **Respawn and stun** | Kill client 1 with a projectile; after respawn the bar re-binds (slots `Ready`, icons back). Apply `State.Stunned` (loose tag on the server ASC for 2 s): slots become `Locked` with the lock glyph, then return |
| V7 | **Ultimate arc** | Raise client 1's Energy from 25 to 100 (server-side attribute set): the arc shows 1 → 4 funded segments and one 300 ms pulse at full, with no idle loop |
| V8 | **Server and lifecycle** | No `GenAbilitySlot` exists in the server world. Stopping PIE logs no "object still referenced" errors; no slot timer fires after StopPIE |
| V9 | **Layout** | Captures at 1280×720, 1920×1080 and 2560×1440 (new-window PIE with those sizes): the bar is inside the 32 px margin, doesn't enter the centre clear zone, and has no overlap or clipping |

- [ ] Run all rows and fix failures (systematic debugging; rebuild if needed). Rerun the failed row and every row after it.
- [ ] Fill in the §9 checklist items that are in scope in the report (Placement, Tokens, Linear vs sRGB, Text ≥ 18 physical px, Data flow, Network, Naming).
- [ ] Clean up: NetEmulation 0, PIE settings back to Standalone with 1 client, test actors destroyed, PIE stopped, editor left open.
- [ ] Commit any test helper changes and fixes.
