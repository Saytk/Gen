#include "AbilitySystem/Abilities/GenGA_Leap.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionJumpForce.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAreaRules.h"
#include "Actors/GenGroundArea.h"
#include "GameFramework/RootMotionSource.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenLeap, Log, All);

UGenGA_Leap::UGenGA_Leap()
{
	CastTime = 0.1f;
}

void UGenGA_Leap::OnCastLaunched(const FGenCastRelease& Release)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		FinishAbility();
		return;
	}

	LeapRelease = Release;
	const FVector Start = Avatar->GetActorLocation();
	const FVector Target = GenAreaRules::ClampToRange(Start, Release.AimLocation, MaxDistance);
	const float Distance = FVector::Dist2D(Start, Target);

	bAirborne = true;
	// Le serveur ne refuse les sorts du client que pendant le vol le plus court (atterrissage précoce possible dès
	// MinimumLandedTriggerTime, ci-dessous) moins la tolérance : son vol finit ~½ RTT après celui du client.
	// SetCastLock appelle UGenAbilitySystemComponent::NoteCastLock (jamais de tag posé à la main)
	const float MinimumLandedTime = LeapDuration * 0.5f;
	SetCastLock(true, MinimumLandedTime);

	if (TrailCueTag.IsValid())
	{
		K2_AddGameplayCue(TrailCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo), /*bRemoveOnAbilityEnd*/ true);
	}

	// Mouvement racine prédit (client et serveur) : arc visible, arrêt net à l'atterrissage
	UAbilityTask_ApplyRootMotionJumpForce* JumpTask = UAbilityTask_ApplyRootMotionJumpForce::ApplyRootMotionJumpForce(
		this, NAME_None, Release.AimDirection.Rotation(), Distance, LeapHeight, LeapDuration,
		/*MinimumLandedTriggerTime*/ MinimumLandedTime, /*bFinishOnLanded*/ true,
		ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.f, nullptr, nullptr);
	JumpTask->OnLanded.AddDynamic(this, &ThisClass::OnLanded);
	JumpTask->OnFinish.AddDynamic(this, &ThisClass::OnLanded);
	JumpTask->ReadyForActivation();

	// Avec bFinishOnLanded, la tâche n'expire jamais d'elle-même (la force de saut continue jusqu'au sol)
	UAbilityTask_WaitDelay* SafetyTask = UAbilityTask_WaitDelay::WaitDelay(this, FMath::Max(MaxFlightDuration, LeapDuration));
	SafetyTask->OnFinish.AddDynamic(this, &ThisClass::OnLanded);
	SafetyTask->ReadyForActivation();

	UE_LOG(LogGenLeap, Verbose, TEXT("[%s] %s : bond de %.0f cm (nourri %d)"), CurrentActorInfo && CurrentActorInfo->IsNetAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"),
		*GetName(), Distance, Release.Fed);
}

void UGenGA_Leap::OnLanded()
{
	if (!bAirborne)
	{
		return; // OnLanded puis OnFinish (et le filet) : une seule fois
	}
	bAirborne = false;
	SetCastLock(false);

	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		OnLeapLanded(LeapRelease, Avatar->GetActorLocation());
	}

	FinishAbility();
}

void UGenGA_Leap::OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation)
{
	if (LandMontage)
	{
		// Même échelle de mouvement racine que les montages du sort (0 pour un bond : c'est la Root Motion Source qui déplace)
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, LandMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false, CastMontageRootMotionScale);
		MontageTask->ReadyForActivation();
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority())
	{
		return;
	}

	if (ImpactCueTag.IsValid())
	{
		K2_ExecuteGameplayCue(ImpactCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo));
	}

	FGenAreaParams Params;
	Params.Radius = LandingRadius;
	Params.KnockbackDistance = LandingKnockback;
	SpawnGroundArea(LandingAreaClass, LandingLocation, Params, UGenGE_Damage::StaticClass(), LandingDamage, LandingEnergyOnHit);

	UE_LOG(LogGenLeap, Verbose, TEXT("[SERVEUR] %s : atterrissage en %s"), *GetName(), *LandingLocation.ToCompactString());
}

void UGenGA_Leap::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Fin en plein vol (mort) : le mouvement racine s'arrête avec la tâche, le verrou est retiré par la base
	bAirborne = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
