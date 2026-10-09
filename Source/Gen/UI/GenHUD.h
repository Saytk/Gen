#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AbilitySystem/GenCastBarRules.h"
#include "GenHUD.generated.h"

class AGenCharacterBase;
class UGenDevPanel;
class UGenPrimaryGameLayout;

/**
 * HUD du joueur local :
 * - crée la racine CommonUI (UGenPrimaryGameLayout) et y empile le HUD UMG (barre de sorts) ;
 * - dessine encore au Canvas, en prototype : barre de vie au-dessus de chaque personnage
 *   (vert = soi, bleu = allié, rouge = ennemi) et panneau local (vie, énergie, ressource, cast).
 */
UCLASS()
class GEN_API AGenHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;

	/** Panneau développeur (F10, hors Shipping) : empilé sur UI.Layer.GameMenu, ou retiré s'il est ouvert. */
	void ToggleDevPanel();

protected:
	TWeakObjectPtr<UGenDevPanel> DevPanel;

	/** Racine CommonUI du joueur local (nulle sur serveur dédié ou si la classe n'est pas configurée). */
	UPROPERTY(Transient) TObjectPtr<UGenPrimaryGameLayout> PrimaryLayout;

	void DrawOverheadBars(const AGenCharacterBase* LocalCharacter, uint8 LocalTeam);
	void DrawLocalPlayerPanel(const AGenCharacterBase* LocalCharacter);

	/** Barre de cast du joueur local, centrée, dont le bas est à Bottom. */
	void DrawLocalCastBar(const AGenCharacterBase* LocalCharacter, float Bottom);
	void DrawBar(float X, float Y, float Width, float Height, float Percent, const FLinearColor& FillColor);

	/** Haut de la barre de sorts UMG, en pixels Canvas (le panneau prototype se dessine au-dessus). */
	float GetAbilityBarTop() const;

	/** Écart entre le bas du panneau prototype et le haut de la barre de sorts (pixels Canvas). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float PanelGapAboveBar = 8.f;

	/** Barre de cast : remplissage, puis pour un sort nourri un cran de 1 px par flamme et le compteur à droite. */
	void DrawCastBar(float X, float Y, float Width, float Height, const GenCastBar::FLayout& Layout);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float OverheadOffsetZ = 130.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FVector2D OverheadBarSize = FVector2D(90.f, 9.f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor SelfColor = FLinearColor(0.25f, 0.9f, 0.3f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor AllyColor = FLinearColor(0.2f, 0.55f, 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor EnemyColor = FLinearColor(0.95f, 0.2f, 0.2f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor EnergyColor = FLinearColor(1.f, 0.75f, 0.1f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor CastColor = FLinearColor(1.f, 0.55f, 0.1f);

	/** Fond et contour des barres ; aussi les crans déjà franchis par le remplissage. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor BarBackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.7f);

	/** Crans pas encore atteints et compteur des sorts nourris : jeton text.primary de DA_UIPalette (UI_Guidelines §2.2 et §4.5). */
	FLinearColor GetCastTickColor() const;
};
