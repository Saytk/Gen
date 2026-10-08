#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UI/GenCastBarRules.h"
#include "GenHUD.generated.h"

class AGenCharacterBase;

/**
 * HUD de prototypage dessiné au Canvas (aucun asset requis) :
 * - barre de vie au-dessus de chaque personnage (vert = soi, bleu = allié, rouge = ennemi)
 * - panneau du joueur local : vie, énergie, sorts et cooldowns.
 * À remplacer par de l'UMG quand l'UI sera designée.
 */
UCLASS()
class GEN_API AGenHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	void DrawOverheadBars(const AGenCharacterBase* LocalCharacter, uint8 LocalTeam);
	void DrawLocalPlayerPanel(const AGenCharacterBase* LocalCharacter);

	/** Barre de cast du joueur local, centrée, dont le bas est à Bottom. */
	void DrawLocalCastBar(const AGenCharacterBase* LocalCharacter, float Bottom);
	void DrawBar(float X, float Y, float Width, float Height, float Percent, const FLinearColor& FillColor);

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

	/** Crans et compteur des sorts nourris (token text.primary, UI_Guidelines §2.2 et §4.5). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor CastTickColor = FLinearColor(0.791f, 0.807f, 0.831f);
};
