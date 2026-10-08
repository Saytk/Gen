#pragma once

#include "CoreMinimal.h"

class UGenGameplayAbility;

/**
 * Contenu de l'infobulle d'un sort (UI_Guidelines §4.1, infobulle) : généré depuis les valeurs de jeu du sort (son CDO),
 * par le même code que le jeu, pour que l'infobulle ne puisse pas mentir. Aucun UMG ici (UGenAbilityTooltip l'affiche).
 */
struct GEN_API FGenAbilityTooltipData
{
	/** Nom du sort (DisplayName). */
	FText Name;

	/** En-tête : incantation, recharge, coût d'énergie, portée, nourrissage (une entrée par valeur présente). */
	TArray<FText> Stats;

	/** Description courte (Description du sort, arguments nommés remplis). Vide si le sort n'en a pas. */
	FText Description;

	/** Un seuil de nourrissage par ligne (0 à MaxFeed), puis les lignes d'effet (fenêtres, durées, états). */
	TArray<FText> Lines;

	/**
	 * Revue PIE finale, C-2 : nombre de lignes « par flamme » en tête de Lines (un seuil par ligne, ou l'effet unique d'un
	 * sort non nourri), avant les lignes d'effet. La carte compacte n'affiche qu'elles.
	 */
	int32 CoreLineCount = 0;

	/** En-tête sur une ligne (« Incantation 0,4 s · Recharge 8 s »). */
	FText GetStatsText() const;

	/** Lignes d'effet, une par ligne. */
	FText GetLinesText() const;

	/**
	 * Revue PIE finale, C-2 : lignes de la carte compacte (détails, Alt maintenu) : les lignes par flamme seulement ; un sort
	 * qui n'en a pas garde sa première ligne d'effet (la carte dit toujours ce que fait le sort).
	 */
	FText GetCompactLinesText() const;

	/** Tout le texte (tests, journal). */
	FString ToString() const;
};

/** Formatage des nombres dans la culture courante (FText::AsNumber) et génération de l'infobulle. */
namespace GenAbilityTooltip
{
	/** Nombre dans la culture courante, au plus 2 décimales, sans zéro inutile (« 0,4 », « 44 »). */
	GEN_API FText Number(float Value);

	/** Durée en secondes (« 0,4 s »). */
	GEN_API FText Seconds(float InSeconds);

	/** Distance : centimètres de jeu affichés en mètres (« 3,5 m »). */
	GEN_API FText Meters(float Centimetres);

	/** Rapport en pourcentage dans la culture courante (0.3 -> « 30 % »). */
	GEN_API FText Percent(float Ratio);

	/** Parties jointes par Separator (FText, pas de concaténation de chaînes). */
	GEN_API FText Join(const TArray<FText>& Parts, const FText& Separator);

	/** Construit l'infobulle du sort Ability (son CDO suffit). */
	GEN_API void Build(const UGenGameplayAbility& Ability, FGenAbilityTooltipData& Out);
}
