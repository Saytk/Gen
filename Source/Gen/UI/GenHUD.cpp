#include "UI/GenHUD.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Character/GenCharacterBase.h"
#include "CommonActivatableWidget.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Player/GenPlayerState.h"
#include "UI/GenHUDLayout.h"
#include "UI/GenPrimaryGameLayout.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUISubsystem.h"
#include "UI/GenUILog.h"
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
		UE_LOG(LogGenUI, Log, TEXT("PrimaryLayoutClass non renseignée (Project Settings > Game > Gen UI) : pas d'interface UMG, HUD Canvas seul."));
		return;
	}

	PrimaryLayout = CreateWidget<UGenPrimaryGameLayout>(PC, LayoutClass);
	if (!PrimaryLayout)
	{
		// Classe abstraite (classe native choisie à la main), Blueprint invalide ou PC sans joueur local
		UE_LOG(LogGenUI, Warning, TEXT("Impossible de créer la racine d'interface %s pour %s"), *GetNameSafe(LayoutClass), *GetNameSafe(PC));
		return;
	}
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
	DrawRect(BarBackgroundColor, X - 1.f, Y - 1.f, Width + 2.f, Height + 2.f);
	DrawRect(FillColor, X, Y, Width * FMath::Clamp(Percent, 0.f, 1.f), Height);
}

void AGenHUD::DrawCastBar(float X, float Y, float Width, float Height, const GenCastBar::FLayout& Layout)
{
	DrawBar(X, Y, Width, Height, Layout.Fill, CastColor);

	if (!Layout.bFed)
	{
		return;
	}

	// Un cran par seuil de flamme (celui qui tombe sur le bout de la barre est déjà marqué par le bord).
	// Clair devant le remplissage ; sombre une fois franchi, sinon presque invisible sur la couleur de cast.
	for (const float Tick : Layout.Ticks)
	{
		if (Tick < 1.f)
		{
			const FLinearColor& TickColor = Tick <= Layout.Fill ? BarBackgroundColor : CastTickColor;
			DrawRect(TickColor, FMath::RoundToFloat(X + Width * Tick), Y, 1.f, Height);
		}
	}

	// Compteur de flammes à droite de la barre, centré sur sa hauteur
	UFont* Font = GEngine->GetSmallFont();
	const FString CounterText = FString::FromInt(Layout.Counter);
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(CounterText, TextWidth, TextHeight, Font);
	DrawText(CounterText, CastTickColor, X + Width + 4.f, Y + (Height - TextHeight) * 0.5f, Font);
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

		// Barre de cast sous la barre de vie (permet de voir venir les sorts ennemis, et leur charge)
		GenCastBar::FLayout CastLayout;
		if (Character->GetCastBarLayout(CastLayout))
		{
			DrawCastBar(X, Y + OverheadBarSize.Y + 3.f, OverheadBarSize.X, 5.f, CastLayout);
		}
	}
}

void AGenHUD::DrawLocalCastBar(const AGenCharacterBase* LocalCharacter, float Bottom)
{
	GenCastBar::FLayout Layout;
	if (!LocalCharacter->GetCastBarLayout(Layout))
	{
		return;
	}

	const FGenCastInfo& Info = LocalCharacter->GetCastInfo();
	const UGenGameplayAbility* AbilityCDO = Cast<UGenGameplayAbility>(Info.Ability->GetDefaultObject());
	const FString Name = !AbilityCDO || AbilityCDO->DisplayName.IsEmpty() ? Info.Ability->GetName() : AbilityCDO->DisplayName.ToString();
	// Sort nourri pendant le nourrissage : temps restant si toutes les flammes passent
	const float Remaining = Layout.TotalDuration * (1.f - Layout.Fill);

	const float Width = 260.f;
	const float Height = 14.f;
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = Bottom - Height;

	DrawCastBar(X, Y, Width, Height, Layout);

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

	// Rangées du panneau prototype, en pixels Canvas bruts : vie (barre 18 + 6), énergie (barre 8 + 8), ressource (texte)
	constexpr float HealthRowHeight = 24.f;
	constexpr float EnergyRowHeight = 16.f;
	constexpr float ResourceRowHeight = 22.f;
	const float MaxResource = LocalCharacter->GetMaxResource();
	const float PanelHeight = HealthRowHeight + EnergyRowHeight + (MaxResource > 0.f ? ResourceRowHeight : 0.f);

	// En attendant WBP_Vitals, le panneau se pose au-dessus de la barre de sorts UMG, jamais dessous
	float Y = GetAbilityBarTop() - PanelGapAboveBar - PanelHeight;

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
	Y += HealthRowHeight;

	// Énergie
	const float MaxEnergy = FMath::Max(LocalCharacter->GetMaxEnergy(), 1.f);
	DrawBar(X, Y, PanelWidth, 8.f, LocalCharacter->GetEnergy() / MaxEnergy, EnergyColor);
	Y += EnergyRowHeight;

	// Ressource du champion (Curffe : flammes du Foyer)
	if (MaxResource > 0.f)
	{
		const FString ResourceText = FString::Printf(TEXT("Flammes : %.0f / %.0f"), LocalCharacter->GetResource(), MaxResource);
		DrawText(ResourceText, FLinearColor(1.f, 0.6f, 0.2f), X, Y, Font);
	}
}

float AGenHUD::GetAbilityBarTop() const
{
	// Haut de la barre UMG en pixels Canvas : marge d'écran + hauteur de la barre (DA_UIMetrics), à l'échelle DPI du viewport
	const UGenUISubsystem* UI = UGenUISubsystem::Get(GetOwningPlayerController());
	const UGenUIMetrics* Metrics = UI && UI->GetMetrics() ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
	return Canvas->ClipY - (Metrics->ScreenMargin + Metrics->GetAbilityBarHeight()) * Scale;
}
