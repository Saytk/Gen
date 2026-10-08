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
	 * Carte compacte (détails, Alt maintenu ; UI_Guidelines §4.1) : incantation, recharge et coût seulement (ni portée ni
	 * nourrissage), sur une ligne.
	 */
	TArray<FText> CompactStats;

	/** Carte compacte : une ligne, la série par flamme d'un sort nourri ou l'effet clé (GetCompactTooltipLine du sort). */
	FText CompactLine;

	/** En-tête sur une ligne (« Incantation 0,4 s · Recharge 8 s »). */
	FText GetStatsText() const;

	/** Lignes d'effet, une par ligne. */
	FText GetLinesText() const;

	/** En-tête de la carte compacte sur une ligne (« Incantation 0,5 s · Recharge 6 s »). */
	FText GetCompactStatsText() const;

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

	/** Série de valeurs, une par seuil (« 14/24/34/44 ») ; une seule valeur si elles sont toutes égales. */
	GEN_API FText Series(TConstArrayView<float> Values);

	/** Série de durées (« 0,1/0,4/0,7/1 s »). */
	GEN_API FText SecondsSeries(TConstArrayView<float> InSeconds);

	/** Série de distances, centimètres affichés en mètres (« 2/2,5/3/3,5 m »). */
	GEN_API FText MetersSeries(TConstArrayView<float> Centimetres);

	/**
	 * Seuil d'apparition d'un effet sur la carte compacte : présent dès 0 flamme, Always (sa valeur, « 1,5 m ») ; à partir
	 * d'un seuil intermédiaire, « dès 2 » ; au dernier seuil seulement, « à 3 ».
	 */
	GEN_API FText FromFeed(int32 FirstFed, int32 MaxFeed, const FText& Always);

	/** Ligne compacte d'un sort nourri : « 0/1/2/3 flammes : » puis Parts jointes par « · ». */
	GEN_API FText FeedSummary(int32 MaxFeed, const TArray<FText>& Parts);

	/** Construit l'infobulle du sort Ability (son CDO suffit). */
	GEN_API void Build(const UGenGameplayAbility& Ability, FGenAbilityTooltipData& Out);
}
