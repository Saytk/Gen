#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AbilitySystem/GenCastBarRules.h"
#include "GenHUD.generated.h"

class AGenCharacterBase;
class UFont;
class UGenDevPanel;
class UGenPrimaryGameLayout;
class UMaterialParameterCollection;
class UTexture2D;

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

	/** Pile au-dessus d'un personnage (concept example_hpbar_concept_v0, UI_Guidelines §4.4) : vie en rectangles, énergie
	 * en tronçons avec « 85/100 », barre de cast à seuils, logo et nombre de la ressource à droite. Scale = échelle d'UI. */
	void DrawNameplate(const AGenCharacterBase* Character, const FLinearColor& HealthColor, float CentreX, float Bottom, float Scale);
	/** Vie : rectangles de HealthPerSegment PV, biseau clair en haut. */
	void DrawHealthSegments(float X, float Y, float Width, float Height, float Health, float MaxHealth, const FLinearColor& Color, float Scale);
	/** Énergie : bouts en chevron, un trait tous les EnergyPerChunk, valeur au centre. */
	void DrawEnergyBar(float X, float Y, float Width, float Height, float Energy, float MaxEnergy, float Scale);
	void DrawTriangle(const FLinearColor& Color, const FVector2D& A, const FVector2D& B, const FVector2D& C);
	/** Texte centré sur (X, Y), police FNT_Barlow, contour sombre. */
	void DrawNameplateText(const FString& Text, float X, float Y, int32 Size, FName Typeface, const FLinearColor& Color, bool bCentreX);
	/** Couleur d'équipe de MPC_TeamColours (même source que les télégraphes), ou Fallback. */
	FLinearColor GetTeamColour(FName Parameter, const FLinearColor& Fallback);

	/** Haut de la barre de sorts UMG, en pixels Canvas (le panneau prototype se dessine au-dessus). */
	float GetAbilityBarTop() const;

	/** Écart entre le bas du panneau prototype et le haut de la barre de sorts (pixels Canvas). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float PanelGapAboveBar = 8.f;

	/** Barre de cast : remplissage, puis pour un sort nourri un cran de 1 px par flamme et le compteur à droite. */
	void DrawCastBar(float X, float Y, float Width, float Height, const GenCastBar::FLayout& Layout);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float OverheadOffsetZ = 130.f;

	/** Pile au-dessus des personnages, en pixels à 1080p (multipliés par l'échelle d'UI). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	float NameplateWidth = 128.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	float NameplateHealthHeight = 14.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	float NameplateEnergyHeight = 11.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	float NameplateCastHeight = 7.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	float NameplateRowGap = 3.f;

	/** Un rectangle de la barre de vie = ce nombre de PV (200 PV = 5 rectangles). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate", meta = (ClampMin = "1"))
	float HealthPerSegment = 40.f;

	/** Un tronçon de la barre d'énergie = ce nombre d'énergie. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate", meta = (ClampMin = "1"))
	float EnergyPerChunk = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	float NameplateResourceIconSize = 26.f;

	/** Logo de la ressource du champion (flammes de Curffe), à droite de la vie. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	TSoftObjectPtr<UTexture2D> ResourceIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Gen/UI/Textures/Icons/T_UI_Resource_Flame.T_UI_Resource_Flame")));

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	TSoftObjectPtr<UFont> NameplateFont = TSoftObjectPtr<UFont>(FSoftObjectPath(TEXT("/Game/Gen/UI/Fonts/FNT_Barlow.FNT_Barlow")));

	/** Couleurs allié / ennemi des barres : celles des télégraphes (une seule source). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Nameplate")
	TSoftObjectPtr<UMaterialParameterCollection> TeamColours = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/Gen/Rendering/MPC_TeamColours.MPC_TeamColours")));

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
