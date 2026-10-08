#include "UI/GenHUD.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "Blueprint/UserWidget.h"
#include "Character/GenCharacterBase.h"
#include "CommonActivatableWidget.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Player/GenPlayerState.h"
#include "UI/GenPrimaryGameLayout.h"
#include "UI/GenUISettings.h"
#include "UI/GenUITags.h"

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

void AGenHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const AGenCharacterBase* LocalCharacter = Cast<AGenCharacterBase>(GetOwningPawn());

	uint8 LocalTeam = GenNoTeam;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (const AGenPlayerState* PS = PC->GetPlayerState<AGenPlayerState>())
		{
			LocalTeam = PS->GetTeamId();
		}
	}

	DrawOverheadBars(LocalCharacter, LocalTeam);
	DrawLocalPlayerPanel(LocalCharacter);
}

void AGenHUD::DrawBar(float X, float Y, float Width, float Height, float Percent, const FLinearColor& FillColor)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), X - 1.f, Y - 1.f, Width + 2.f, Height + 2.f);
	DrawRect(FillColor, X, Y, Width * FMath::Clamp(Percent, 0.f, 1.f), Height);
}

void AGenHUD::DrawOverheadBars(const AGenCharacterBase* LocalCharacter, uint8 LocalTeam)
{
	UFont* Font = GEngine->GetSmallFont();

	for (TActorIterator<AGenCharacterBase> It(GetWorld()); It; ++It)
	{
		const AGenCharacterBase* Character = *It;
		if (Character->IsDead() || Character->GetMaxHealth() <= 0.f)
		{
			continue;
		}

		const FVector ScreenLocation = Project(Character->GetActorLocation() + FVector(0.f, 0.f, OverheadOffsetZ), false);
		if (ScreenLocation.Z <= 0.f) // derrière la caméra
		{
			continue;
		}

		FLinearColor Color = EnemyColor;
		if (Character == LocalCharacter)
		{
			Color = SelfColor;
		}
		else if (!AGenCharacterBase::AreTeamsEnemies(LocalTeam, Character->GetTeamId()))
		{
			Color = AllyColor;
		}

		const float X = ScreenLocation.X - OverheadBarSize.X * 0.5f;
		const float Y = ScreenLocation.Y;
		DrawBar(X, Y, OverheadBarSize.X, OverheadBarSize.Y, Character->GetHealth() / Character->GetMaxHealth(), Color);

		const FString HealthText = FString::Printf(TEXT("%.0f"), Character->GetHealth());
		float TextWidth = 0.f;
		float TextHeight = 0.f;
		GetTextSize(HealthText, TextWidth, TextHeight, Font);
		DrawText(HealthText, FLinearColor::White, ScreenLocation.X - TextWidth * 0.5f, Y - TextHeight - 1.f, Font);

		// Barre de cast sous la barre de vie (permet de voir venir les sorts ennemis)
		const float CastProgress = Character->GetCastProgress();
		if (CastProgress >= 0.f)
		{
			DrawBar(X, Y + OverheadBarSize.Y + 3.f, OverheadBarSize.X, 5.f, CastProgress, CastColor);
		}
	}
}

void AGenHUD::DrawLocalCastBar(const AGenCharacterBase* LocalCharacter, float Bottom)
{
	const float CastProgress = LocalCharacter->GetCastProgress();
	if (CastProgress < 0.f)
	{
		return;
	}

	const FGenCastInfo& Info = LocalCharacter->GetCastInfo();
	const UGenGameplayAbility* AbilityCDO = Cast<UGenGameplayAbility>(Info.Ability->GetDefaultObject());
	const FString Name = !AbilityCDO || AbilityCDO->DisplayName.IsEmpty() ? Info.Ability->GetName() : AbilityCDO->DisplayName.ToString();
	const float Remaining = Info.Duration * (1.f - CastProgress);

	const float Width = 260.f;
	const float Height = 14.f;
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = Bottom - Height;

	DrawBar(X, Y, Width, Height, CastProgress, CastColor);

	UFont* Font = GEngine->GetSmallFont();
	DrawText(Name, FLinearColor::White, X + 4.f, Y, Font);

	const FString TimeText = FString::Printf(TEXT("%.1f"), Remaining);
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(TimeText, TextWidth, TextHeight, Font);
	DrawText(TimeText, FLinearColor::White, X + Width - TextWidth - 4.f, Y, Font);
}

void AGenHUD::DrawLocalPlayerPanel(const AGenCharacterBase* LocalCharacter)
{
	if (!LocalCharacter)
	{
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	const float PanelWidth = 360.f;
	const float X = (Canvas->ClipX - PanelWidth) * 0.5f;
	float Y = Canvas->ClipY - 110.f;

	if (LocalCharacter->IsDead())
	{
		const FString DeadText = TEXT("MORT - réapparition imminente...");
		float TextWidth = 0.f;
		float TextHeight = 0.f;
		GetTextSize(DeadText, TextWidth, TextHeight, Font);
		DrawText(DeadText, EnemyColor, (Canvas->ClipX - TextWidth) * 0.5f, Canvas->ClipY * 0.4f, Font);
		return;
	}

	DrawLocalCastBar(LocalCharacter, Y - 40.f);

	// Vie
	const float MaxHealth = FMath::Max(LocalCharacter->GetMaxHealth(), 1.f);
	DrawBar(X, Y, PanelWidth, 18.f, LocalCharacter->GetHealth() / MaxHealth, SelfColor);
	DrawText(FString::Printf(TEXT("%.0f / %.0f"), LocalCharacter->GetHealth(), MaxHealth), FLinearColor::White, X + 6.f, Y, Font);
	Y += 24.f;

	// Énergie
	const float MaxEnergy = FMath::Max(LocalCharacter->GetMaxEnergy(), 1.f);
	DrawBar(X, Y, PanelWidth, 8.f, LocalCharacter->GetEnergy() / MaxEnergy, EnergyColor);
	Y += 16.f;

	// Ressource du champion (Curffe : flammes du Foyer)
	const float MaxResource = LocalCharacter->GetMaxResource();
	if (MaxResource > 0.f)
	{
		const FString ResourceText = FString::Printf(TEXT("Flammes : %.0f / %.0f"), LocalCharacter->GetResource(), MaxResource);
		DrawText(ResourceText, FLinearColor(1.f, 0.6f, 0.2f), X, Y, Font);
		Y += 22.f;
	}
}
