#include "UI/GenAbilityTooltip.h"

#include "AbilitySystem/GenAbilityTooltipData.h"
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
		ContentBox->SetMaxDesiredWidth(Metrics->TooltipMaxWidth);
	}
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(FSlateColor(Palette->Accent_Brass));
	}
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

void UGenAbilityTooltip::SetContent(const FGenAbilityTooltipData& Data, const FText& KeyLabel)
{
	if (!bStyled)
	{
		ApplyStyle();
	}
	NameText->SetText(Data.Name);
	SetOptionalText(KeyText, KeyLabel);
	SetOptionalText(StatsText, Data.GetStatsText());
	DescriptionText->SetText(Data.Description);
	DescriptionText->SetVisibility(Data.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	SetOptionalText(LinesText, Data.GetLinesText());
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
