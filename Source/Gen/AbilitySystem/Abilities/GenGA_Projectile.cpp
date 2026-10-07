#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h"
#include "AbilitySystem/Tasks/GenAbilityTask_TargetDataUnderCursor.h"
#include "Actors/GenProjectile.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenProjectileAbility, Log, All);

#define GEN_ABILITY_LOG(Verbosity, Format, ...) UE_LOG(LogGenProjectileAbility, Verbosity, TEXT("[%s] %s: " Format), (CurrentActorInfo && CurrentActorInfo->IsNetAuthority()) ? TEXT("SERVEUR") : TEXT("CLIENT"), *GetName(), ##__VA_ARGS__)

UGenGA_Projectile::UGenGA_Projectile()
{
	ProjectileClass = AGenProjectile::StaticClass();
	DamageEffectClass = UGenGE_Damage::StaticClass();
	Damage = FScalableFloat(20.f);

	ActivationOwnedTags.AddTag(GenGameplayTags::State_Casting);
}

void UGenGA_Projectile::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Pas de CommitAbility ici : le cooldown et le coût ne sont appliqués qu'au lancer
	// (OnTargetDataReady). CanActivateAbility les a déjà vérifiés avant l'activation.
	GEN_ABILITY_LOG(Verbose, "Activé (clé %s), incantation %.2fs", *ActivationInfo.GetActivationPredictionKey().ToString(), CastTime);

	if (CastTime > 0.f)
	{
		StartCasting();
	}
	else
	{
		OnCastFinished();
	}
}

void UGenGA_Projectile::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (IsActive())
	{
		GEN_ABILITY_LOG(Verbose, "Fin (annulé=%d, répliqué=%d)", bWasCancelled, bReplicateEndAbility);
	}

	// Annulation en pleine incantation (étourdi, mort, respawn...) : on nettoie
	StopCasting();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGenGA_Projectile::StartCasting()
{
	bIsCasting = true;

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StartCast(GetClass(), CastTime, CastFX, CastFXSocket);
	}

	// Ralenti pendant l'incantation (appliqué dans la fenêtre de prédiction de l'activation)
	if (CastMoveSpeedMultiplier < 1.f)
	{
		FGameplayEffectSpecHandle SlowSpec = MakeOutgoingGameplayEffectSpec(UGenGE_MoveSpeedMultiplier::StaticClass(), GetAbilityLevel());
		if (SlowSpec.IsValid())
		{
			SlowSpec.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_MoveSpeedMultiplier, CastMoveSpeedMultiplier);
			CastSlowHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, SlowSpec);
		}
	}

	// Le geste continue après la fin normale du sort (le lancer tombe à la fin de l'incantation).
	// Une annulation (étourdi, mort) le coupe quand même : la tâche écoute OnGameplayAbilityCancelled.
	if (ChargeMontage)
	{
		UAbilityTask_PlayMontageAndWait* ChargeTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, ChargeMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false);
		ChargeTask->ReadyForActivation();
	}

	// Un étourdissement interrompt l'incantation (la mort annule déjà tous les sorts)
	UAbilityTask_WaitGameplayTagAdded* StunTask = UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(this, GenGameplayTags::State_Stunned, nullptr, true);
	StunTask->Added.AddDynamic(this, &ThisClass::OnCastInterrupted);
	StunTask->ReadyForActivation();

	UAbilityTask_WaitDelay* CastTask = UAbilityTask_WaitDelay::WaitDelay(this, CastTime);
	CastTask->OnFinish.AddDynamic(this, &ThisClass::OnCastFinished);
	CastTask->ReadyForActivation();
}

void UGenGA_Projectile::StopCasting()
{
	if (!bIsCasting)
	{
		return;
	}
	bIsCasting = false;

	if (CastSlowHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(CastSlowHandle);
		CastSlowHandle.Invalidate();
	}

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StopCast(GetClass());
	}
}

void UGenGA_Projectile::OnCastInterrupted()
{
	GEN_ABILITY_LOG(Verbose, "Incantation interrompue (étourdi)");
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void UGenGA_Projectile::OnCastFinished()
{
	GEN_ABILITY_LOG(Verbose, "Incantation terminée, attente de la visée");
	StopCasting();

	// Visée lue maintenant : le joueur peut ajuster pendant toute l'incantation
	UGenAbilityTask_TargetDataUnderCursor* TargetTask = UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(this);
	TargetTask->ValidData.AddDynamic(this, &ThisClass::OnTargetDataReady);
	TargetTask->ReadyForActivation();
}

void UGenGA_Projectile::OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const FGameplayAbilityTargetData* Data = DataHandle.Get(0);
	const FHitResult* Hit = Data ? Data->GetHitResult() : nullptr;

	if (!Avatar || !Hit)
	{
		GEN_ABILITY_LOG(Warning, "Visée invalide (avatar=%d, hit=%d)", Avatar != nullptr, Hit != nullptr);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// Le sort part : on applique cooldown et coût maintenant, pour qu'une incantation
	// interrompue (annulée, étourdi, mort) ne coûte rien. Le client est dans la fenêtre de
	// prédiction ouverte par la tâche de visée, le serveur dans celle de la clé reçue.
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		GEN_ABILITY_LOG(Verbose, "CommitAbility a échoué au lancer");
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	GEN_ABILITY_LOG(Verbose, "Visée reçue : %s", *Hit->Location.ToCompactString());

	// Se tourner vers la cible (client et serveur, pour que la prédiction concorde)
	FVector Direction = (Hit->Location - Avatar->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = Avatar->GetActorForwardVector();
	}
	Avatar->SetActorRotation(Direction.Rotation());

	if (CastMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, CastMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false);
		MontageTask->ReadyForActivation();
	}

	SpawnProjectile(Avatar->GetActorLocation() + Direction * 1000.f);

	// Le client ne réplique PAS la fin du sort : son incantation finit avant celle du serveur
	// (qui a démarré plus tard), et un EndAbility répliqué tuerait le sort côté serveur avant
	// qu'il ait fait apparaître le projectile. C'est le serveur qui termine et prévient le client.
	const bool bReplicateEnd = CurrentActorInfo && CurrentActorInfo->IsNetAuthority();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicateEnd, false);
}

void UGenGA_Projectile::SpawnProjectile(const FVector& TargetLocation)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || !ProjectileClass)
	{
		return;
	}

	const FVector Origin = Avatar->GetActorLocation();
	FVector Direction = (TargetLocation - Origin).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = Avatar->GetActorForwardVector();
	}

	const FTransform SpawnTransform(Direction.Rotation(), Origin + Direction * SpawnForwardOffset);

	AGenProjectile* Projectile = GetWorld()->SpawnActorDeferred<AGenProjectile>(
		ProjectileClass, SpawnTransform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Projectile)
	{
		GEN_ABILITY_LOG(Warning, "Échec du spawn de %s", *GetNameSafe(ProjectileClass));
		return;
	}

	GEN_ABILITY_LOG(Verbose, "Projectile %s créé en %s", *Projectile->GetName(), *SpawnTransform.GetLocation().ToCompactString());

	if (DamageEffectClass)
	{
		const int32 Level = GetAbilityLevel();
		FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, Level);
		if (SpecHandle.IsValid())
		{
			SpecHandle.Data->GetContext().AddSourceObject(Projectile);
			SpecHandle.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Damage, Damage.GetValueAtLevel(Level));
			Projectile->DamageEffectSpecHandle = SpecHandle;
		}
	}

	Projectile->FinishSpawning(SpawnTransform);
}
