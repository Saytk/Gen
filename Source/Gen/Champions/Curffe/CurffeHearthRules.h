#pragma once

#include "CoreMinimal.h"

/** Règles pures de l'affichage du Foyer (Curffe-Visuals.md §5). */
namespace CurffeHearthRules
{
	/** Lit : flamme dans l'orbite. InSpell : flamme partie dans le sort en cours (emplacement éteint). Empty : vide. */
	enum class ESocket : uint8
	{
		Lit,
		InSpell,
		Empty
	};

	/**
	 * Toujours Sockets emplacements fixes : d'abord les flammes restantes (Flames − Fed), puis celles parties dans le sort,
	 * puis les vides. Un compte nourri supérieur aux flammes (correction réseau) est borné aux flammes.
	 */
	inline void GetSocketStates(int32 Flames, int32 Fed, int32 Sockets, TArray<ESocket, TInlineAllocator<8>>& Out)
	{
		Out.Reset();
		const int32 Total = FMath::Clamp(Flames, 0, FMath::Max(Sockets, 0));
		const int32 InSpell = FMath::Clamp(Fed, 0, Total);
		const int32 Lit = Total - InSpell;
		for (int32 Index = 0; Index < Sockets; ++Index)
		{
			Out.Add(Index < Lit ? ESocket::Lit : (Index < Lit + InSpell ? ESocket::InSpell : ESocket::Empty));
		}
	}
}
