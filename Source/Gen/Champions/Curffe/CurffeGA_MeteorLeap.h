#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Dash.h"
#include "CurffeGA_MeteorLeap.generated.h"

/**
 * Flame Dash (Espace) de Curffe : ruée en zigzag nourrissable (Curffe.md, « Space: Flame Dash »). 0 flamme : une ruée
 * courte de 3 m selon la visée ; chaque flamme nourrie ajoute un segment de 2.5 m, alternativement à gauche puis à
 * droite (±30°). Aucun dégât, ni anneau ni zone d'atterrissage : pure mobilité. Tout le comportement est celui de
 * UGenGA_Dash. Nom de classe gardé (ancien Meteor Leap, retiré le 2026-10-09) pour que le Blueprint GA_FlameLeap continue
 * de fonctionner.
 */
UCLASS()
class GEN_API UCurffeGA_MeteorLeap : public UGenGA_Dash
{
	GENERATED_BODY()
};
