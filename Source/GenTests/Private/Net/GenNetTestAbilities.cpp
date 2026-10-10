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
float UGenNetTestGA_MeteorLeap::TestSegmentDuration = 0.12f;
int32 UGenNetTestGA_Passive::ActivationCount = 0;
FGameplayTag UGenNetTestGA_Triggered::TestInputTag;
int32 UGenNetTestGA_Triggered::ActivationCount = 0;
