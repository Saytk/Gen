#include "AbilitySystem/Abilities/GenGA_Leap.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionJumpForce.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "AbilitySystem/GenTargetData.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "Actors/GenGroundArea.h"
#include "Character/GenCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenLeap, Log, All);

namespace GenLeapPrivate
{
	/** Lacet (degrés) de la direction horizontale Direction, axe X si elle est nulle. */
	float FlatYaw(const FVector& Direction)
	{
		const FVector Flat = Direction.GetSafeNormal2D();
		return (Flat.IsNearlyZero() ? FVector::ForwardVector : Flat).Rotation().Yaw;
	}
}

UGenGA_Leap::UGenGA_Leap()
{
	CastTime = 0.1f;
}

void UGenGA_Leap::FillAimData(FGenTargetData_Aim& Data) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	// Mêmes calculs que le serveur ferait depuis SA position : le client les envoie, ils deviennent ceux des deux machines
	const FVector Start = Avatar->GetActorLocation();
	const FVector Cursor = Data.HitResult.Location;
	Data.LeapDistance = FVector::Dist2D(Start, GenAreaRules::ClampToRange(Start, Cursor, MaxDistance));
	Data.LeapYaw = GenLeapPrivate::FlatYaw(Cursor - Start);
}

bool UGenGA_Leap::GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const
{
	// Mêmes valeurs que le bond (MaxDistance, ClampToRange) et la zone d'atterrissage (LandingRadius) : le dessin = la hitbox
	GenIndicatorRules::FLeapAimParams Params;
	Params.MaxDistance = MaxDistance;
	Params.LandingRadius = LandingRadius;
	Params.RingProjectileRadius = GetRingProjectileRadius();
	GenIndicatorRules::ComputeLeapAim(Caster.GetActorLocation(), Cursor, Params, Fed, Out);
	return true;
}

void UGenGA_Leap::GetFlightGeometry(const FGenLeapTarget& Target, FGenAimGeometry& Out) const
{
	GenIndicatorRules::FLeapAimParams Params;
	Params.MaxDistance = MaxDistance;
	Params.LandingRadius = Target.Radius; // rayon répliqué avec le point (celui de la zone d'atterrissage)
	Params.RingProjectileRadius = GetRingProjectileRadius();
	GenIndicatorRules::ComputeLeapFlight(Target.Location, Target.Direction, Params, Target.Fed, Out);
}

void UGenGA_Leap::ResolveLeap(const FGenCastRelease& Release, const FVector& Start, float& OutDistance, float& OutYaw) const
{
	const float OwnDistance = FVector::Dist2D(Start, GenAreaRules::ClampToRange(Start, Release.AimLocation, MaxDistance));
	const float OwnYaw = GenLeapPrivate::FlatYaw(Release.AimDirection);
	OutDistance = OwnDistance;
	OutYaw = OwnYaw;

	if (Release.ClientLeapDistance < 0.f)
	{
		return; // pas d'annonce (IA, visée d'un autre type)
	}

	// Client ou hôte : ses propres valeurs, celles qu'il vient d'envoyer
	if (!IsServerForRemoteClient())
	{
		OutDistance = FMath::Min(Release.ClientLeapDistance, MaxDistance);
		OutYaw = Release.ClientLeapYaw;
		return;
	}

	// Serveur : valeurs du client si elles sont plausibles depuis la position du serveur (sinon, les siennes)
	if (GenAreaRules::AcceptClientLeap(Release.ClientLeapDistance, Release.ClientLeapYaw, MaxDistance, OwnYaw))
	{
		OutDistance = FMath::Min(Release.ClientLeapDistance, MaxDistance);
		OutYaw = Release.ClientLeapYaw;
		return;
	}

	UE_LOG(LogGenLeap, Warning, TEXT("[SERVEUR] %s : bond du client refusé (%.0f cm, lacet %.1f°), valeurs du serveur (%.0f cm, %.1f°)"),
		*GetName(), Release.ClientLeapDistance, Release.ClientLeapYaw, OwnDistance, OwnYaw);
}

void UGenGA_Leap::OnCastLaunched(const FGenCastRelease& Release)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		FinishAbility();
		return;
	}

	const FVector Start = Avatar->GetActorLocation();
	float Distance = 0.f;
	float Yaw = 0.f;
	ResolveLeap(Release, Start, Distance, Yaw);

	// La direction retenue sert à tout : force de saut, cercle montré aux autres, directions de l'anneau
	const FRotator LeapRotation(0.f, Yaw, 0.f);
	LeapRelease = Release;
	LeapRelease.AimDirection = LeapRotation.Vector();
	const FVector Target = Start + LeapRelease.AimDirection * Distance;

	bAirborne = true;
	bWaitingForFloor = false;
	// Le serveur ne refuse les sorts du client que pendant le vol le plus court (atterrissage précoce possible dès
	// MinimumLandedTriggerTime, ci-dessous) moins la tolérance : son vol finit ~½ RTT après celui du client.
	// SetCastLock appelle UGenAbilitySystemComponent::NoteCastLock (jamais de tag posé à la main)
	const float MinimumLandedTime = LeapDuration * 0.5f;
	SetCastLock(true, MinimumLandedTime);

	// Point d'atterrissage vu par tous pendant le vol (cercle et amorces de l'anneau chez les autres joueurs)
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		FGenLeapTarget LeapTarget;
		LeapTarget.Ability = GetClass();
		LeapTarget.Location = Target;
		LeapTarget.Direction = LeapRelease.AimDirection;
		LeapTarget.Radius = LandingRadius;
		LeapTarget.Fed = static_cast<uint8>(FMath::Clamp(Release.Fed, 0, 255));
		Character->SetLeapTarget(LeapTarget);
	}

	if (TrailCueTag.IsValid())
	{
		K2_AddGameplayCue(TrailCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo), /*bRemoveOnAbilityEnd*/ true);
	}

	// Mouvement racine prédit (client et serveur), mêmes paramètres des deux côtés (ResolveLeap). Revue Plan 2 Tasks 7-8,
	// I-2 : la force a son délai propre (bFinishOnLanded faux) et finit à LeapDuration en temps de simulation du
	// mouvement ; au-delà, elle continuerait à ~15 m/s à l'horizontale. Vitesse finale nulle : la chute éventuelle
	// part à la verticale du point visé (rebord, marche descendante).
	JumpTask = UAbilityTask_ApplyRootMotionJumpForce::ApplyRootMotionJumpForce(
		this, NAME_None, LeapRotation, Distance, LeapHeight, LeapDuration,
		/*MinimumLandedTriggerTime*/ MinimumLandedTime, /*bFinishOnLanded*/ false,
		ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.f, nullptr, nullptr);
	JumpTask->OnLanded.AddDynamic(this, &ThisClass::OnJumpLanded);
	JumpTask->OnFinish.AddDynamic(this, &ThisClass::OnJumpForceEnded);
	JumpTask->ReadyForActivation();

	UAbilityTask_WaitDelay* SafetyTask = UAbilityTask_WaitDelay::WaitDelay(this, FMath::Max(MaxFlightDuration, LeapDuration));
	SafetyTask->OnFinish.AddDynamic(this, &ThisClass::OnSafetyNet);
	SafetyTask->ReadyForActivation();

	UE_LOG(LogGenLeap, Verbose, TEXT("[%s] %s : bond de %.0f cm, lacet %.1f° (nourri %d)"), CurrentActorInfo && CurrentActorInfo->IsNetAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"),
		*GetName(), Distance, Yaw, Release.Fed);
}

void UGenGA_Leap::OnJumpLanded()
{
	// Posé avant LeapDuration (marche montante, obstacle) : la force s'arrête là
	Land(/*bSafetyNet*/ false);
}

void UGenGA_Leap::OnJumpForceEnded()
{
	if (!bAirborne)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !Movement->IsFalling())
	{
		Land(/*bSafetyNet*/ false);
		return;
	}

	// Encore en l'air à LeapDuration (rebord, pente descendante) : la force est retirée (vitesse nulle), la gravité finit
	// la chute à la verticale du point visé
	JumpTask = nullptr;
	bWaitingForFloor = true;
	Character->LandedDelegate.AddUniqueDynamic(this, &ThisClass::OnCharacterLanded);
}

void UGenGA_Leap::OnCharacterLanded(const FHitResult& Hit)
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (Character && Character->bClientUpdating)
	{
		// Rejeu de mouvements (correction du client) : la position finale du rejeu décide, à l'image suivante
		TWeakObjectPtr<UGenGA_Leap> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
		{
			UGenGA_Leap* Leap = WeakThis.Get();
			const ACharacter* Avatar = Leap ? Cast<ACharacter>(Leap->GetAvatarActorFromActorInfo()) : nullptr;
			if (Leap && Leap->bAirborne && Avatar && !Avatar->GetCharacterMovement()->IsFalling())
			{
				Leap->Land(/*bSafetyNet*/ false);
			}
		});
		return;
	}

	Land(/*bSafetyNet*/ false);
}

void UGenGA_Leap::OnSafetyNet()
{
	Land(/*bSafetyNet*/ true);
}

void UGenGA_Leap::StopJumpForce()
{
	if (JumpTask)
	{
		UAbilityTask_ApplyRootMotionJumpForce* Task = JumpTask;
		JumpTask = nullptr;
		Task->EndTask(); // retire la source de mouvement racine (vitesse finale nulle)
	}

	if (bWaitingForFloor)
	{
		bWaitingForFloor = false;
		if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			Character->LandedDelegate.RemoveDynamic(this, &ThisClass::OnCharacterLanded);
		}
	}
}

void UGenGA_Leap::Land(bool bSafetyNet)
{
	if (!bAirborne)
	{
		return; // force, chute et filet : une seule fois
	}
	bAirborne = false;
	StopJumpForce();
	SetCastLock(false);
	ClearLeapTarget();

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	bool bOnFloor = Avatar != nullptr;
	FVector LandingLocation = Avatar ? Avatar->GetActorLocation() : FVector::ZeroVector;

	if (bSafetyNet && Avatar)
	{
		// Revue Plan 2 Tasks 7-8, M-3 : jamais de zone ni d'anneau en plein air. Posés sur le sol trouvé sous le
		// personnage (centre de capsule au-dessus), rien du tout s'il n'y en a pas
		FVector Floor;
		bOnFloor = GenWorldQueries::TryFindFloor(GetWorld(), LandingLocation, { Avatar }, Floor);
		if (bOnFloor)
		{
			const ACharacter* Character = Cast<ACharacter>(Avatar);
			const float HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
			LandingLocation = Floor + FVector(0.f, 0.f, HalfHeight);
		}
		UE_LOG(LogGenLeap, Warning, TEXT("[%s] %s : filet de sécurité du bond atteint (%.2fs de vol) en %s, %s : géométrie du niveau à vérifier"),
			Avatar->HasAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"), *GetName(), MaxFlightDuration, *Avatar->GetActorLocation().ToCompactString(),
			bOnFloor ? TEXT("posé sur le sol trouvé") : TEXT("aucun sol, ni zone ni anneau"));
	}

	if (bOnFloor)
	{
		OnLeapLanded(LeapRelease, LandingLocation);
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
	// Fin en plein vol (mort) : la force s'arrête, le verrou est retiré par la base
	bAirborne = false;
	StopJumpForce();
	ClearLeapTarget();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGenGA_Leap::ClearLeapTarget()
{
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->ClearLeapTarget(GetClass()); // sans effet si ce n'est plus notre bond
	}
}
