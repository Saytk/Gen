#include "UI/GenUISubsystem.h"

#include "AbilitySystemComponent.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUILog.h"
#include "UI/GenUIRules.h"
#include "UI/GenUISettings.h"

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
		Delegate.ExecuteIfBound(AbilitySystem.Get());
	}
	return OnAbilitySystemReady.Add(MoveTemp(Delegate));
}

void UGenUISubsystem::UnregisterOnAbilitySystemReady(FDelegateHandle Handle)
{
	OnAbilitySystemReady.Remove(Handle);
}
