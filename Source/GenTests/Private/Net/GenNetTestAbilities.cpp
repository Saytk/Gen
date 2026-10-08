#include "Net/GenNetTestAbilities.h"
#include "Net/GenNetCurffeTestAbilities.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"

void UGenNetTestGA_Lingering::OnCastLaunched(const FGenCastRelease& Release)
{
	// Reste actif après le départ : un autre sort du joueur ne doit pas l'annuler (CancelOtherPendingCasts)
	UAbilityTask_WaitDelay* LingerTask = UAbilityTask_WaitDelay::WaitDelay(this, LingerTime);
	LingerTask->OnFinish.AddDynamic(this, &ThisClass::OnLingerFinished);
	LingerTask->ReadyForActivation();
}

FGameplayTagContainer UGenNetTestGA_MeteorLeap::TestCooldownTags;
