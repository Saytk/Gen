#include "UI/GenAbilityTooltip.h"

#include "AbilitySystem/GenAbilityTooltipData.h"
#include "CommonTextBlock.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/GenTextBlock.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUISubsystem.h"

namespace
{
	const UGenUIMetrics* TooltipMetrics(const UUserWidget* Widget)
	{
		const UGenUISubsystem* UI = UGenUISubsystem::Get(Widget);
		return UI && UI->GetMetrics() ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	}

	const UGenUIPalette* TooltipPalette(const UUserWidget* Widget)
	{
		const UGenUISubsystem* UI = UGenUISubsystem::Get(Widget);
		return UI && UI->GetPalette() ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	}

	void SetOptionalText(UGenTextBlock* Block, const FText& Text)
	{
		if (Block)
		{
			Block->SetText(Text);
			Block->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
	}

	/** Style posé dans le WBP (UCommonTextBlock::Style est privé, sans accesseur). */
	TSubclassOf<UCommonTextStyle> GetTextStyle(const UCommonTextBlock* Block)
	{
		static const FClassProperty* StyleProperty = FindFProperty<FClassProperty>(UCommonTextBlock::StaticClass(), TEXT("Style"));
		return Block && StyleProperty ? TSubclassOf<UCommonTextStyle>(Cast<UClass>(StyleProperty->GetObjectPropertyValue_InContainer(Block))) : nullptr;
	}
}

void UGenAbilityTooltip::NativeConstruct()
{
	Super::NativeConstruct();

	// Jamais de saisie : l'infobulle ne prend ni survol ni clic (le jeu garde la souris)
	SetVisibility(bWantShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ApplyStyle();
	ApplyOpacity();
}

void UGenAbilityTooltip::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DelayTimer);
		World->GetTimerManager().ClearTimer(FadeTimer);
	}
	Super::NativeDestruct();
}

void UGenAbilityTooltip::ApplyStyle()
{
	const UGenUIMetrics* Metrics = TooltipMetrics(this);
	const UGenUIPalette* Palette = TooltipPalette(this);

	// Panneau bg.panelRaised, coins radius.panel, bordure line.bronze (§2.1, §2.8, §2.9) ; marge d'un panneau du HUD (§2.7)
	if (Panel)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Palette->Bg_PanelRaised);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Metrics->RadiusPanel, Metrics->RadiusPanel, Metrics->RadiusPanel, Metrics->RadiusPanel),
			FSlateColor(Palette->Line_Bronze), Metrics->PanelOutlineWidth);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Panel->SetBrush(Brush);
		Panel->SetBrushColor(FLinearColor::White);
		Panel->SetPadding(FMargin(Metrics->HudPanelPadding));
	}
	if (ContentBox)
	{
		// Revue PIE finale, C-2 : carte compacte à largeur fixe (DetailsColumns cartes par rangée du panneau des détails)
		if (bCompact)
		{
			ContentBox->SetWidthOverride(Metrics->TooltipCompactWidth);
			ContentBox->SetMaxDesiredWidth(Metrics->TooltipCompactWidth);
		}
		else
		{
			ContentBox->ClearWidthOverride();
			ContentBox->SetMaxDesiredWidth(Metrics->TooltipMaxWidth);
		}
	}
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(FSlateColor(Palette->Accent_Brass));
	}
	ApplyTextStyles();
	// Texte sur plusieurs lignes, coupé à la largeur de la boîte (§7.1 : <= 80 caractères par ligne)
	for (UGenTextBlock* Block : { NameText.Get(), DescriptionText.Get(), StatsText.Get(), LinesText.Get() })
	{
		if (Block)
		{
			Block->SetAutoWrapText(true);
		}
	}
	bStyled = true;
}

void UGenAbilityTooltip::ApplyTextStyles()
{
	UGenTextBlock* const Blocks[] = { NameText.Get(), StatsText.Get(), LinesText.Get() };
	if (AuthoredTextStyles.IsEmpty())
	{
		for (UGenTextBlock* Block : Blocks)
		{
			AuthoredTextStyles.Add(GetTextStyle(Block));
		}
	}
	// Carte compacte : TS_BodyCompact (interligne 1) ; carte complète : les styles du WBP (TS_Body)
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Blocks); ++Index)
	{
		const TSubclassOf<UCommonTextStyle> Style = bCompact && CompactTextStyle ? CompactTextStyle : AuthoredTextStyles[Index];
		if (Blocks[Index] && Style && GetTextStyle(Blocks[Index]) != Style)
		{
			Blocks[Index]->SetStyle(Style);
		}
	}
}

void UGenAbilityTooltip::SetContent(const FGenAbilityTooltipData& Data, const FText& KeyLabel)
{
	if (!bStyled)
	{
		ApplyStyle();
	}
	Content = Data;
	ContentKeyLabel = KeyLabel;
	bHasContent = true;
	ApplyContent();
}

void UGenAbilityTooltip::SetCompact(bool bInCompact)
{
	if (bCompact == bInCompact)
	{
		return;
	}
	bCompact = bInCompact;
	if (bStyled)
	{
		ApplyStyle();
	}
	if (bHasContent)
	{
		ApplyContent();
	}
}

void UGenAbilityTooltip::ApplyContent()
{
	NameText->SetText(Content.Name);
	SetOptionalText(KeyText, ContentKeyLabel);
	// Carte compacte (§4.1) : une ligne de statistiques (incantation, recharge, coût) et une ligne d'effet, sans description
	SetOptionalText(StatsText, bCompact ? Content.GetCompactStatsText() : Content.GetStatsText());
	const FText Description = bCompact ? FText::GetEmpty() : Content.Description;
	DescriptionText->SetText(Description);
	DescriptionText->SetVisibility(Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	SetOptionalText(LinesText, bCompact ? Content.CompactLine : Content.GetLinesText());
}

FText UGenAbilityTooltip::GetNameText() const
{
	return NameText ? NameText->GetText() : FText::GetEmpty();
}

FText UGenAbilityTooltip::GetStatsText() const
{
	return StatsText ? StatsText->GetText() : FText::GetEmpty();
}

FText UGenAbilityTooltip::GetDescriptionText() const
{
	return DescriptionText ? DescriptionText->GetText() : FText::GetEmpty();
}

FText UGenAbilityTooltip::GetLinesText() const
{
	return LinesText ? LinesText->GetText() : FText::GetEmpty();
}

void UGenAbilityTooltip::Show(float Delay)
{
	UWorld* World = GetWorld();
	if (bWantShown || !World)
	{
		return;
	}
	bWantShown = true;
	World->GetTimerManager().ClearTimer(DelayTimer);
	if (Delay > 0.f && Opacity <= 0.f)
	{
		World->GetTimerManager().SetTimer(DelayTimer, this, &ThisClass::StartFade, Delay, false);
	}
	else
	{
		StartFade();
	}
}

void UGenAbilityTooltip::Hide()
{
	if (!bWantShown)
	{
		return;
	}
	bWantShown = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DelayTimer);
	}
	StartFade();
}

void UGenAbilityTooltip::StartFade()
{
	if (bWantShown)
	{
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	UWorld* World = GetWorld();
	if (World && !World->GetTimerManager().IsTimerActive(FadeTimer))
	{
		World->GetTimerManager().SetTimer(FadeTimer, this, &ThisClass::StepFade, TooltipMetrics(this)->TooltipFadeStep, true);
	}
	StepFade();
}

void UGenAbilityTooltip::StepFade()
{
	const UGenUIMetrics* Metrics = TooltipMetrics(this);
	const float Step = Metrics->MotionFast > 0.f ? Metrics->TooltipFadeStep / Metrics->MotionFast : 1.f;
	Opacity = FMath::Clamp(Opacity + (bWantShown ? Step : -Step), 0.f, 1.f);
	ApplyOpacity();

	const bool bDone = bWantShown ? Opacity >= 1.f : Opacity <= 0.f;
	if (bDone)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(FadeTimer);
		}
		if (!bWantShown)
		{
			SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UGenAbilityTooltip::ApplyOpacity()
{
	// motion.fast (§5.1) : entrée ease-out, sortie ease-in
	SetRenderOpacity(bWantShown ? FMath::InterpEaseOut(0.f, 1.f, Opacity, 2.f) : FMath::InterpEaseIn(0.f, 1.f, Opacity, 2.f));
}
