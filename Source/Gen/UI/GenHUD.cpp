#include "UI/GenHUD.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Character/GenCharacterBase.h"
#include "CommonActivatableWidget.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialParameterCollection.h"
#include "Player/GenPlayerState.h"
#include "UI/GenDevPanel.h"
#include "UI/GenHUDLayout.h"
#include "UI/GenPrimaryGameLayout.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUISubsystem.h"
#include "UI/GenUILog.h"
#include "UI/GenUIRules.h"
#include "UI/GenUISettings.h"
#include "UI/GenUITags.h"

namespace GenNameplateDraw
{
	/**
	 * Forme dessinée ligne par ligne (1 px de haut) : dégradé vertical Top -> Bottom, et pour chaque ligne un retrait à
	 * gauche et à droite (Inset) dont la partie fractionnaire est rendue en alpha : bords lisses sans texture ni matériau.
	 * ClipRight coupe la forme à droite (remplissage partiel), ClipLeft à gauche (tronçon d'une barre).
	 */
	template <typename FInsetFn>
	void Rows(AHUD* HUD, float X, float Y, float Width, float Height, const FLinearColor& Top, const FLinearColor& Bottom, float ClipRight, FInsetFn Inset, float ClipLeft = -1e9f)
	{
		const int32 RowCount = FMath::CeilToInt32(Height);
		for (int32 Row = 0; Row < RowCount; ++Row)
		{
			const float RowHeight = FMath::Min(1.f, Height - Row);
			const float T = RowCount > 1 ? static_cast<float>(Row) / (RowCount - 1) : 0.f;
			// Dégradé à deux pentes : le haut s'éclaircit à peine, le bas s'assombrit (relief sans brillance)
			const FLinearColor Colour = T < 0.4f ? FMath::Lerp(Top, FMath::Lerp(Top, Bottom, 0.25f), T / 0.4f)
				: FMath::Lerp(FMath::Lerp(Top, Bottom, 0.25f), Bottom, (T - 0.4f) / 0.6f);
			const float In = FMath::Max(Inset(Row + 0.5f), 0.f);
			const float Left = FMath::Max(X + In, ClipLeft);
			const float Right = FMath::Min(X + Width - In, ClipRight);
			if (Right - Left <= 0.f)
			{
				continue;
			}
			const float SolidLeft = FMath::CeilToFloat(Left);
			const float SolidRight = FMath::FloorToFloat(Right);
			const float RowY = Y + Row;
			if (SolidRight > SolidLeft)
			{
				HUD->DrawRect(Colour, SolidLeft, RowY, SolidRight - SolidLeft, RowHeight);
			}
			// Pixels de bord partiellement couverts : alpha = couverture
			FLinearColor Edge = Colour;
			Edge.A = Colour.A * (SolidLeft - Left);
			if (Edge.A > 0.01f)
			{
				HUD->DrawRect(Edge, SolidLeft - 1.f, RowY, 1.f, RowHeight);
			}
			Edge.A = Colour.A * (Right - SolidRight);
			if (Edge.A > 0.01f)
			{
				HUD->DrawRect(Edge, SolidRight, RowY, 1.f, RowHeight);
			}
		}
	}

	/** Rectangle aux coins arrondis de rayon Radius. */
	void Rounded(AHUD* HUD, float X, float Y, float Width, float Height, float Radius, const FLinearColor& Top, const FLinearColor& Bottom)
	{
		const float R = FMath::Min(Radius, FMath::Min(Width, Height) * 0.5f);
		Rows(HUD, X, Y, Width, Height, Top, Bottom, X + Width, [R, Height](float RowCentre)
		{
			const float Dy = RowCentre < R ? R - RowCentre : (RowCentre > Height - R ? RowCentre - (Height - R) : 0.f);
			return R - FMath::Sqrt(FMath::Max(R * R - Dy * Dy, 0.f));
		});
	}

	/** Barre aux bouts en chevron (profondeur = demi-hauteur). */
	void Hexagon(AHUD* HUD, float X, float Y, float Width, float Height, const FLinearColor& Top, const FLinearColor& Bottom, float ClipRight)
	{
		const float Half = Height * 0.5f;
		Rows(HUD, X, Y, Width, Height, Top, Bottom, ClipRight, [Half](float RowCentre)
		{
			return FMath::Abs(RowCentre - Half);
		});
	}
}

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

void AGenHUD::ToggleDevPanel()
{
#if !UE_BUILD_SHIPPING
	if (!PrimaryLayout)
	{
		return;
	}
	if (UGenDevPanel* Panel = DevPanel.Get(); Panel && Panel->IsActivated())
	{
		// Une pile CommonUI retire d'elle-même le widget désactivé
		Panel->DeactivateWidget();
		DevPanel.Reset();
		UE_LOG(LogGenUI, Log, TEXT("Panneau développeur fermé"));
		return;
	}
	DevPanel = Cast<UGenDevPanel>(PrimaryLayout->PushWidgetToLayer(GenUITags::UI_Layer_GameMenu, UGenDevPanel::StaticClass()));
	UE_LOG(LogGenUI, Log, TEXT("Panneau développeur ouvert (%s)"), DevPanel.IsValid() ? TEXT("ok") : TEXT("échec"));
#endif
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
	const UGenUISubsystem* UI = UGenUISubsystem::Get(GetOwningPlayerController());
	const UGenUIPalette* Palette = UI && UI->GetPalette() ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	const float Radius = FMath::Min(3.f, Height * 0.5f);
	GenNameplateDraw::Rounded(this, X - 1.f, Y - 1.f, Width + 2.f, Height + 2.f, Radius + 1.f, Palette->Line_Outline, Palette->Line_Outline);
	GenNameplateDraw::Rounded(this, X, Y, Width, Height, Radius, Palette->Bg_Panel, Palette->Bg_Panel * 0.7f);

	// Sort nourri [TASTE #13] : le remplissage change de couleur à chaque seuil franchi (vert, puis jaune, puis rouge) ;
	// sinon la couleur de cast. Chaque tronçon garde le dégradé doux des autres barres
	const float Fill = FMath::Clamp(Layout.Fill, 0.f, 1.f);
	if (Fill > 0.f)
	{
		if (Layout.bFed && !Layout.bDrain)
		{
			const FLinearColor Tiers[] = {
				GenUIRules::HexToLinear(GenUITokens::CastTierLowHex),
				GenUIRules::HexToLinear(GenUITokens::CastTierMidHex),
				GenUIRules::HexToLinear(GenUITokens::CastTierHighHex) };
			float SectionStart = 0.f;
			for (int32 Section = 0; Section <= Layout.Ticks.Num() && SectionStart < Fill; ++Section)
			{
				const float SectionEnd = Section < Layout.Ticks.Num() ? FMath::Min(Layout.Ticks[Section], 1.f) : 1.f;
				const float End = FMath::Min(SectionEnd, Fill);
				if (End > SectionStart)
				{
					const FLinearColor& Colour = Tiers[FMath::Min(Section, 2)];
					GenNameplateDraw::Rows(this, X, Y, Width, Height, FMath::Lerp(Colour, FLinearColor::White, 0.1f), Colour * 0.62f, X + Width * End,
						[Radius, Height](float RowCentre)
						{
							const float Dy = RowCentre < Radius ? Radius - RowCentre : (RowCentre > Height - Radius ? RowCentre - (Height - Radius) : 0.f);
							return Radius - FMath::Sqrt(FMath::Max(Radius * Radius - Dy * Dy, 0.f));
						}, X + Width * SectionStart);
				}
				SectionStart = SectionEnd;
			}
		}
		else
		{
			GenNameplateDraw::Rows(this, X, Y, Width, Height, FMath::Lerp(CastColor, FLinearColor::White, 0.1f), CastColor * 0.62f, X + Width * Fill,
				[Radius, Height](float RowCentre)
				{
					const float Dy = RowCentre < Radius ? Radius - RowCentre : (RowCentre > Height - Radius ? RowCentre - (Height - Radius) : 0.f);
					return Radius - FMath::Sqrt(FMath::Max(Radius * Radius - Dy * Dy, 0.f));
				});
		}
	}

	// Front à l'avancée du remplissage (concept example_hpbar_concept_v0), discret
	if (Fill > 0.f && Fill < 1.f)
	{
		const float FrontX = FMath::RoundToFloat(X + Width * Fill);
		DrawRect(FLinearColor(1.f, 0.95f, 0.8f, 0.15f), FrontX - 2.f, Y - 1.f, 4.f, Height + 2.f);
		DrawRect(FLinearColor(1.f, 0.95f, 0.8f, 0.85f), FrontX - 1.f, Y, 2.f, Height);
	}

	if (!Layout.bFed)
	{
		return;
	}

	// Un cran par seuil de flamme (celui qui tombe sur le bout de la barre est déjà marqué par le bord)
	for (const float Tick : Layout.Ticks)
	{
		if (Tick < 1.f)
		{
			DrawRect(Palette->Line_Outline, FMath::RoundToFloat(X + Width * Tick), Y, 1.f, Height);
		}
	}

	// Compteur de flammes à droite de la barre, centré sur sa hauteur
	DrawNameplateText(FString::FromInt(Layout.Counter), X + Width + 4.f, Y + Height * 0.5f, 9, TEXT("Bold"), GetCastTickColor(), /*bCentreX*/ false);
}

void AGenHUD::DrawTriangle(const FLinearColor& Color, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	// Texture blanche du Canvas (pas de dépendance à RenderCore pour GWhiteTexture)
	const FTexture* White = Canvas->DefaultTexture ? Canvas->DefaultTexture->GetResource() : nullptr;
	if (!White)
	{
		return;
	}
	FCanvasTriangleItem Triangle(A, B, C, White);
	Triangle.SetColor(Color);
	Triangle.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Triangle);
}

void AGenHUD::DrawNameplateText(const FString& Text, float X, float Y, int32 Size, FName Typeface, const FLinearColor& Color, bool bCentreX)
{
	UFont* Font = NameplateFont.LoadSynchronous();
	FSlateFontInfo FontInfo = Font ? FSlateFontInfo(Font, Size, Typeface) : FSlateFontInfo();
	FontInfo.OutlineSettings.OutlineSize = 1;
	FontInfo.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), FontInfo, Color);
	Item.bCentreX = bCentreX;
	Item.bCentreY = true;
	Canvas->DrawItem(Item);
}

FLinearColor AGenHUD::GetTeamColour(FName Parameter, const FLinearColor& Fallback)
{
	const UMaterialParameterCollection* Collection = TeamColours.LoadSynchronous();
	const FCollectionVectorParameter* Vector = Collection ? Collection->GetVectorParameterByName(Parameter) : nullptr;
	return Vector ? Vector->DefaultValue : Fallback;
}

void AGenHUD::DrawHealthSegments(float X, float Y, float Width, float Height, float Health, float MaxHealth, const FLinearColor& Color, float Scale)
{
	const UGenUISubsystem* UI = UGenUISubsystem::Get(GetOwningPlayerController());
	const UGenUIPalette* Palette = UI && UI->GetPalette() ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	const float Frame = FMath::Max(1.f, FMath::RoundToFloat(2.f * Scale));
	const float Radius = 4.f * Scale;
	GenNameplateDraw::Rounded(this, X - Frame, Y - Frame, Width + 2.f * Frame, Height + 2.f * Frame, Radius + Frame, Palette->Line_Outline, Palette->Line_Outline);
	GenNameplateDraw::Rounded(this, X, Y, Width, Height, Radius, Palette->Bg_Panel, Palette->Bg_Panel * 0.7f);

	// Un rectangle arrondi par HealthPerSegment PV, dégradé vertical doux (pas de bande blanche) ; le dernier se vide
	const int32 Segments = FMath::Max(1, FMath::CeilToInt32(MaxHealth / HealthPerSegment));
	const float Gap = FMath::Max(1.f, FMath::RoundToFloat(2.f * Scale));
	const float Inner = FMath::Max(1.f, FMath::RoundToFloat(1.f * Scale));
	const float SegmentWidth = (Width - 2.f * Inner - Gap * (Segments - 1)) / Segments;
	const FLinearColor Top = FMath::Lerp(Color, FLinearColor::White, 0.12f);
	const FLinearColor Bottom = Color * 0.62f;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float SegmentHealth = FMath::Min(HealthPerSegment, MaxHealth - Segment * HealthPerSegment);
		const float Fill = FMath::Clamp((Health - Segment * HealthPerSegment) / SegmentHealth, 0.f, 1.f);
		const float FillWidth = SegmentWidth * Fill;
		if (FillWidth >= 1.f)
		{
			GenNameplateDraw::Rounded(this, X + Inner + Segment * (SegmentWidth + Gap), Y + Inner, FillWidth, Height - 2.f * Inner, 2.5f * Scale, Top, Bottom);
		}
	}
}

void AGenHUD::DrawEnergyBar(float X, float Y, float Width, float Height, float Energy, float MaxEnergy, float Scale)
{
	const UGenUISubsystem* UI = UGenUISubsystem::Get(GetOwningPlayerController());
	const UGenUIPalette* Palette = UI && UI->GetPalette() ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	const float Frame = FMath::Max(1.f, FMath::RoundToFloat(1.5f * Scale));
	const float MidY = Y + Height * 0.5f;

	// Forme hexagonale (bouts en chevron) dessinée ligne par ligne : bords lissés, dégradé vertical
	GenNameplateDraw::Hexagon(this, X - Frame, Y - Frame, Width + 2.f * Frame, Height + 2.f * Frame, Palette->Line_Outline, Palette->Line_Outline, X + Width + Frame);
	GenNameplateDraw::Hexagon(this, X, Y, Width, Height, Palette->Bg_Panel, Palette->Bg_Panel * 0.7f, X + Width);

	const float Percent = MaxEnergy > 0.f ? FMath::Clamp(Energy / MaxEnergy, 0.f, 1.f) : 0.f;
	const FLinearColor Fill = Percent >= 1.f ? Palette->Energy_Full : Palette->Energy_Charging;
	const float Inner = FMath::Max(1.f, FMath::RoundToFloat(1.f * Scale));
	if (Percent > 0.f)
	{
		GenNameplateDraw::Hexagon(this, X + Inner, Y + Inner, Width - 2.f * Inner, Height - 2.f * Inner,
			FMath::Lerp(Fill, FLinearColor::White, 0.1f), Fill * 0.6f, X + Inner + (Width - 2.f * Inner) * Percent);
	}

	// Un trait tous les EnergyPerChunk (25 : 4 tronçons pour 100)
	const float Cap = Height * 0.5f;
	const float BodyWidth = Width - 2.f * Cap;
	const int32 Chunks = MaxEnergy > 0.f ? FMath::Max(1, FMath::RoundToInt32(MaxEnergy / EnergyPerChunk)) : 1;
	for (int32 Chunk = 1; Chunk < Chunks; ++Chunk)
	{
		DrawRect(Palette->Line_Outline * FLinearColor(1.f, 1.f, 1.f, 0.8f), FMath::RoundToFloat(X + Cap + BodyWidth * Chunk / Chunks), Y + Inner, FMath::Max(1.f, Scale), Height - 2.f * Inner);
	}

	DrawNameplateText(FString::Printf(TEXT("%d/%d"), FMath::FloorToInt32(Energy), FMath::RoundToInt32(MaxEnergy)), X + Width * 0.5f, MidY,
		FMath::RoundToInt32(7.f * Scale), TEXT("SemiBold"), Palette->Text_Primary, /*bCentreX*/ true);
}

void AGenHUD::DrawNameplate(const AGenCharacterBase* Character, const FLinearColor& HealthColor, float CentreX, float Bottom, float Scale)
{
	const float Width = FMath::RoundToFloat(NameplateWidth * Scale);
	const float HealthHeight = FMath::RoundToFloat(NameplateHealthHeight * Scale);
	const float EnergyHeight = FMath::RoundToFloat(NameplateEnergyHeight * Scale);
	const float CastHeight = FMath::RoundToFloat(NameplateCastHeight * Scale);
	const float Gap = FMath::RoundToFloat(NameplateRowGap * Scale);

	// Lignes toujours réservées (la pile ne bouge pas quand la barre de cast apparaît) ; le bas de la pile est au point projeté
	const float X = FMath::RoundToFloat(CentreX - Width * 0.5f);
	const float Top = FMath::RoundToFloat(Bottom - (HealthHeight + Gap + EnergyHeight + Gap + CastHeight));
	const float EnergyY = Top + HealthHeight + Gap;
	const float CastY = EnergyY + EnergyHeight + Gap;

	DrawHealthSegments(X, Top, Width, HealthHeight, Character->GetHealth(), Character->GetMaxHealth(), HealthColor, Scale);
	DrawEnergyBar(X, EnergyY, Width, EnergyHeight, Character->GetEnergy(), Character->GetMaxEnergy(), Scale);

	GenCastBar::FLayout CastLayout;
	if (Character->GetCastBarLayout(CastLayout))
	{
		DrawCastBar(X, CastY, Width, CastHeight, CastLayout);
	}

	// Ressource du champion à droite : logo + nombre disponible (les unités nourries sont déjà dans le sort, comme le Foyer)
	if (Character->GetMaxResource() > 0.f)
	{
		const UGenUISubsystem* UI = UGenUISubsystem::Get(GetOwningPlayerController());
		const UGenUIPalette* Palette = UI && UI->GetPalette() ? UI->GetPalette() : GetDefault<UGenUIPalette>();
		const float IconSize = FMath::RoundToFloat(NameplateResourceIconSize * Scale);
		const float MidY = Top + (HealthHeight + Gap + EnergyHeight) * 0.5f;
		const float IconX = X + Width + FMath::RoundToFloat(6.f * Scale);
		if (UTexture2D* Icon = ResourceIcon.LoadSynchronous())
		{
			DrawTexture(Icon, IconX, FMath::RoundToFloat(MidY - IconSize * 0.5f), IconSize, IconSize, 0.f, 0.f, 1.f, 1.f);
		}
		const int32 Available = FMath::Max(FMath::FloorToInt32(Character->GetResource()) - static_cast<int32>(Character->GetFedResource()), 0);
		DrawNameplateText(FString::FromInt(Available), IconX + IconSize + FMath::RoundToFloat(3.f * Scale), MidY,
			FMath::RoundToInt32(14.f * Scale), TEXT("Bold"), Palette->Text_Primary, /*bCentreX*/ false);
	}
}

void AGenHUD::DrawOverheadBars(const AGenCharacterBase* LocalCharacter, uint8 LocalTeam)
{
	// Échelle d'UI : tailles écrites pour 1080p (UI_Guidelines §4.4), jamais liées au zoom de la caméra
	const float Scale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.75f, 2.f);
	const FLinearColor SelfHealth = GenUIRules::HexToLinear(GenUITokens::SelfHealthHex);
	const FLinearColor AllyHealth = GetTeamColour(TEXT("Ally"), AllyColor);
	const FLinearColor EnemyHealth = GetTeamColour(TEXT("Enemy"), EnemyColor);

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

		// Soi en sarcelle (concept), alliés en bleu, ennemis en rouge : les couleurs des télégraphes ([TASTE #13])
		const FLinearColor& Color = Character == LocalCharacter ? SelfHealth
			: (!AGenCharacterBase::AreTeamsEnemies(LocalTeam, Character->GetTeamId()) ? AllyHealth : EnemyHealth);
		DrawNameplate(Character, Color, ScreenLocation.X, ScreenLocation.Y, Scale);
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
	// Sort nourri pendant le nourrissage : temps restant si toutes les flammes passent.
	// Canalisation (V4) : Fill est déjà la part restante (la barre se vide).
	const float Remaining = Layout.TotalDuration * (Layout.bDrain ? Layout.Fill : 1.f - Layout.Fill);

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

FLinearColor AGenHUD::GetCastTickColor() const
{
	// Jeton de la palette, jamais un littéral : il suit DA_UIPalette quand la palette change
	const UGenUISubsystem* UI = UGenUISubsystem::Get(GetOwningPlayerController());
	const UGenUIPalette* Palette = UI && UI->GetPalette() ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	return Palette->Text_Primary;
}

float AGenHUD::GetAbilityBarTop() const
{
	// Haut de la barre UMG en pixels Canvas : marge d'écran + hauteur de la barre (DA_UIMetrics), à l'échelle DPI du viewport
	const UGenUISubsystem* UI = UGenUISubsystem::Get(GetOwningPlayerController());
	const UGenUIMetrics* Metrics = UI && UI->GetMetrics() ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
	return Canvas->ClipY - (Metrics->ScreenMargin + Metrics->GetAbilityBarHeight()) * Scale;
}
