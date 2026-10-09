#include "UI/GenDevPanel.h"

#include "Blueprint/WidgetTree.h"
#include "CommonTextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Player/GenPlayerController.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUISettings.h"
#include "UI/GenUISubsystem.h"

#define LOCTEXT_NAMESPACE "GenDevPanel"

namespace
{
	constexpr float CooldownStep = 0.1f;
	constexpr float CastTimeStep = 0.1f;
	constexpr float GameSpeedStep = 0.05f;

	const UGenUIMetrics* PanelMetrics(const UUserWidget* Widget)
	{
		const UGenUISubsystem* UI = UGenUISubsystem::Get(Widget);
		return UI && UI->GetMetrics() ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	}

	const UGenUIPalette* PanelPalette(const UUserWidget* Widget)
	{
		const UGenUISubsystem* UI = UGenUISubsystem::Get(Widget);
		return UI && UI->GetPalette() ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	}

	float Snap(float Value, float Step, float Min, float Max)
	{
		return FMath::Clamp(FMath::GridSnap(Value, Step), Min, Max);
	}

	FText Multiplier(float Value)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 2;
		Options.MaximumFractionalDigits = 2;
		return FText::Format(LOCTEXT("Multiplier", "x{0}"), FText::AsNumber(Value, &Options));
	}
}

TOptional<FUIInputConfig> UGenDevPanel::GetDesiredInputConfig() const
{
	// Jeu et interface à la fois : on continue de jouer panneau ouvert ; la souris n'est capturée que pendant un clic
	return FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::CaptureDuringMouseDown, EMouseLockMode::DoNotLock, false);
}

TSharedRef<SWidget> UGenDevPanel::RebuildWidget()
{
	// Pas de WBP : l'arbre est construit ici, une fois
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UGenDevPanel::BuildTree()
{
	const UGenUIMetrics* Metrics = PanelMetrics(this);
	const UGenUIPalette* Palette = PanelPalette(this);

	// Racine transparente aux clics : seul le panneau prend la souris, le reste de l'écran reste au jeu
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	// Même panneau que les infobulles : bg.panelRaised, coins radius.panel, bordure line.bronze (§2.1, §2.8, §2.9)
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Palette->Bg_PanelRaised);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Metrics->RadiusPanel, Metrics->RadiusPanel, Metrics->RadiusPanel, Metrics->RadiusPanel),
		FSlateColor(Palette->Line_Bronze), Metrics->PanelOutlineWidth);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Panel->SetBrush(Brush);
	Panel->SetBrushColor(FLinearColor::White);
	Panel->SetPadding(FMargin(Metrics->HudPanelPadding));
	UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Panel);
	PanelSlot->SetHorizontalAlignment(HAlign_Right);
	PanelSlot->SetVerticalAlignment(VAlign_Center);
	PanelSlot->SetPadding(FMargin(Metrics->ScreenMargin));

	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size"));
	Size->SetWidthOverride(Metrics->TooltipCompactWidth);
	Panel->SetContent(Size);
	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Rows"));
	Size->SetContent(Box);

	Box->AddChildToVerticalBox(MakeText(LOCTEXT("Title", "Développeur (F10)"), /*bAccent*/ true));
	NoCooldownsBox = AddToggle(Box, LOCTEXT("NoCooldowns", "Aucune recharge"));
	CooldownSlider = AddSlider(Box, GenDevTuning::MinCooldownScale, GenDevTuning::MaxCooldownScale, CooldownStep, CooldownLabel);
	CastTimeSlider = AddSlider(Box, GenDevTuning::MinCastTimeScale, GenDevTuning::MaxCastTimeScale, CastTimeStep, CastTimeLabel);
	InfiniteEnergyBox = AddToggle(Box, LOCTEXT("InfiniteEnergy", "Énergie infinie"));
	InfiniteResourceBox = AddToggle(Box, LOCTEXT("InfiniteResource", "Flammes infinies"));
	InvulnerableBox = AddToggle(Box, LOCTEXT("Invulnerable", "Joueurs invulnérables"));
	GameSpeedSlider = AddSlider(Box, GenDevTuning::MinGameSpeed, GenDevTuning::MaxGameSpeed, GameSpeedStep, GameSpeedLabel);
	AddButton(Box, LOCTEXT("Refill", "Tout recharger (vie, énergie, flammes, recharges)"))->OnClicked.AddDynamic(this, &ThisClass::HandleRefillClicked);
	AddButton(Box, LOCTEXT("Reset", "Réglages par défaut"))->OnClicked.AddDynamic(this, &ThisClass::HandleResetClicked);

	RefreshFromTuning();
}

UCommonTextBlock* UGenDevPanel::MakeText(const FText& Text, bool bAccent)
{
	UCommonTextBlock* Block = WidgetTree->ConstructWidget<UCommonTextBlock>(UCommonTextBlock::StaticClass());
	if (const TSubclassOf<UCommonTextStyle> Style = GetDefault<UGenUISettings>()->DevPanelTextStyle.LoadSynchronous())
	{
		Block->SetStyle(Style);
	}
	const UGenUIPalette* Palette = PanelPalette(this);
	Block->SetColorAndOpacity(FSlateColor(bAccent ? Palette->Accent_Brass : Palette->Text_Primary));
	Block->SetAutoWrapText(true);
	Block->SetText(Text);
	return Block;
}

UCheckBox* UGenDevPanel::AddToggle(UVerticalBox* Box, const FText& Label)
{
	const float Gap = PanelMetrics(this)->HudPanelPadding;
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
	Check->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleToggleChanged);
	Row->AddChildToHorizontalBox(Check)->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(MakeText(Label));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(Gap, 0.f, 0.f, 0.f));
	Box->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, Gap * 0.5f));
	return Check;
}

USlider* UGenDevPanel::AddSlider(UVerticalBox* Box, float Min, float Max, float Step, TObjectPtr<UCommonTextBlock>& OutLabel)
{
	const float Gap = PanelMetrics(this)->HudPanelPadding;
	OutLabel = MakeText(FText::GetEmpty());
	Box->AddChildToVerticalBox(OutLabel)->SetPadding(FMargin(0.f, Gap * 0.5f, 0.f, 0.f));
	USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetStepSize(Step);
	Slider->OnValueChanged.AddDynamic(this, &ThisClass::HandleSliderChanged);
	Slider->OnMouseCaptureEnd.AddDynamic(this, &ThisClass::HandleSliderReleased);
	Slider->OnControllerCaptureEnd.AddDynamic(this, &ThisClass::HandleSliderReleased);
	Box->AddChildToVerticalBox(Slider)->SetPadding(FMargin(0.f, 0.f, 0.f, Gap * 0.5f));
	return Slider;
}

UButton* UGenDevPanel::AddButton(UVerticalBox* Box, const FText& Label)
{
	const UGenUIMetrics* Metrics = PanelMetrics(this);
	const UGenUIPalette* Palette = PanelPalette(this);
	// Boutons aux couleurs des panneaux (le style gris du moteur rend le texte clair illisible)
	const auto MakeBrush = [Metrics, Palette](const FLinearColor& Fill)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Metrics->RadiusPanel, Metrics->RadiusPanel, Metrics->RadiusPanel, Metrics->RadiusPanel),
			FSlateColor(Palette->Line_Bronze), Metrics->PanelOutlineWidth);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return Brush;
	};
	FLinearColor Hovered = Palette->Line_Bronze;
	Hovered.A *= 0.35f;
	FLinearColor Pressed = Palette->Accent_Brass;
	Pressed.A *= 0.5f;
	FButtonStyle Style = UButton::StaticClass()->GetDefaultObject<UButton>()->GetStyle();
	Style.SetNormal(MakeBrush(Palette->Bg_Panel));
	Style.SetHovered(MakeBrush(Hovered));
	Style.SetPressed(MakeBrush(Pressed));
	Style.SetNormalPadding(FMargin(Metrics->HudPanelPadding * 0.5f));
	Style.SetPressedPadding(FMargin(Metrics->HudPanelPadding * 0.5f));

	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Button->SetStyle(Style);
	Button->SetContent(MakeText(Label));
	Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, PanelMetrics(this)->HudPanelPadding * 0.5f));
	return Button;
}

FGenDevTuning UGenDevPanel::ReadWidgets() const
{
	FGenDevTuning Tuning;
	Tuning.bNoCooldowns = NoCooldownsBox && NoCooldownsBox->IsChecked();
	Tuning.bInfiniteEnergy = InfiniteEnergyBox && InfiniteEnergyBox->IsChecked();
	Tuning.bInfiniteResource = InfiniteResourceBox && InfiniteResourceBox->IsChecked();
	Tuning.bInvulnerable = InvulnerableBox && InvulnerableBox->IsChecked();
	if (CooldownSlider)
	{
		Tuning.CooldownScale = Snap(CooldownSlider->GetValue(), CooldownStep, GenDevTuning::MinCooldownScale, GenDevTuning::MaxCooldownScale);
	}
	if (CastTimeSlider)
	{
		Tuning.CastTimeScale = Snap(CastTimeSlider->GetValue(), CastTimeStep, GenDevTuning::MinCastTimeScale, GenDevTuning::MaxCastTimeScale);
	}
	if (GameSpeedSlider)
	{
		Tuning.GameSpeed = Snap(GameSpeedSlider->GetValue(), GameSpeedStep, GenDevTuning::MinGameSpeed, GenDevTuning::MaxGameSpeed);
	}
	return Tuning;
}

void UGenDevPanel::RefreshFromTuning()
{
	const FGenDevTuning& Tuning = GenDevTuning::Get(this);
	TGuardValue<bool> Guard(bApplyingTuning, true);
	if (NoCooldownsBox) { NoCooldownsBox->SetIsChecked(Tuning.bNoCooldowns); }
	if (InfiniteEnergyBox) { InfiniteEnergyBox->SetIsChecked(Tuning.bInfiniteEnergy); }
	if (InfiniteResourceBox) { InfiniteResourceBox->SetIsChecked(Tuning.bInfiniteResource); }
	if (InvulnerableBox) { InvulnerableBox->SetIsChecked(Tuning.bInvulnerable); }
	if (CooldownSlider) { CooldownSlider->SetValue(Tuning.CooldownScale); }
	if (CastTimeSlider) { CastTimeSlider->SetValue(Tuning.CastTimeScale); }
	if (GameSpeedSlider) { GameSpeedSlider->SetValue(Tuning.GameSpeed); }
	RefreshSliderLabels();
}

void UGenDevPanel::RefreshSliderLabels()
{
	const FGenDevTuning Tuning = ReadWidgets();
	if (CooldownLabel)
	{
		CooldownLabel->SetText(FText::Format(LOCTEXT("CooldownScale", "Durée des recharges {0}"), Multiplier(Tuning.CooldownScale)));
	}
	if (CastTimeLabel)
	{
		CastTimeLabel->SetText(FText::Format(LOCTEXT("CastTimeScale", "Temps d'incantation et de nourrissage {0}"), Multiplier(Tuning.CastTimeScale)));
	}
	if (GameSpeedLabel)
	{
		GameSpeedLabel->SetText(FText::Format(LOCTEXT("GameSpeed", "Vitesse du jeu {0}"), Multiplier(Tuning.GameSpeed)));
	}
}

void UGenDevPanel::SendTuning()
{
	if (bApplyingTuning)
	{
		return;
	}
	if (AGenPlayerController* PC = GetOwningPlayer<AGenPlayerController>())
	{
		PC->ServerSetDevTuning(ReadWidgets());
	}
}

void UGenDevPanel::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (UGenDevTuningSubsystem* DevTuning = UGenDevTuningSubsystem::Get(this))
	{
		TuningChangedHandle = DevTuning->OnChanged.AddUObject(this, &ThisClass::RefreshFromTuning);
	}
	RefreshFromTuning();
}

void UGenDevPanel::NativeOnDeactivated()
{
	if (UGenDevTuningSubsystem* DevTuning = UGenDevTuningSubsystem::Get(this))
	{
		DevTuning->OnChanged.Remove(TuningChangedHandle);
	}
	TuningChangedHandle.Reset();
	Super::NativeOnDeactivated();
}

void UGenDevPanel::HandleToggleChanged(bool /*bChecked*/)
{
	SendTuning();
}

void UGenDevPanel::HandleSliderChanged(float /*Value*/)
{
	// Libellés en direct ; l'envoi au serveur attend le relâché (pas un RPC par image de glissement)
	RefreshSliderLabels();
}

void UGenDevPanel::HandleSliderReleased()
{
	SendTuning();
}

void UGenDevPanel::HandleRefillClicked()
{
	if (AGenPlayerController* PC = GetOwningPlayer<AGenPlayerController>())
	{
		PC->ServerDevRefillAll();
	}
}

void UGenDevPanel::HandleResetClicked()
{
	if (AGenPlayerController* PC = GetOwningPlayer<AGenPlayerController>())
	{
		PC->ServerSetDevTuning(FGenDevTuning());
	}
}

#undef LOCTEXT_NAMESPACE
