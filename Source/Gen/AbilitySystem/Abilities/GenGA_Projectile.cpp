#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "Abilities/Tasks/AbilityTask_NetworkSyncPoint.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenTargetData.h"
#include "AbilitySystem/Tasks/GenAbilityTask_TargetDataUnderCursor.h"
#include "AbilitySystemComponent.h"
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
	GEN_ABILITY_LOG(Verbose, "Activé (clé %s), incantation %.2fs%s", *ActivationInfo.GetActivationPredictionKey().ToString(), CastTime, bFeedable ? TEXT(", nourrissable") : TEXT(""));

	FedCount = 0;
	FedVisualCount = 0;
	ServerFeedElapsed = 0.f;
	bInterruptWatchStarted = false;

	if (bFeedable)
	{
		StartFeeding();
	}
	else if (CastTime > 0.f)
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

	// Annulation en plein nourrissage ou incantation (étourdi, mort, autre sort...) : on nettoie.
	// La ressource nourrie n'est dépensée qu'au lancer : rien à rendre.
	StopCasting();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

int32 UGenGA_Projectile::GetAvailableFeed() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;
	return GenFeeding::GetFeedLimit(MaxFeed, Available);
}

void UGenGA_Projectile::StartFeeding()
{
	bIsFeeding = true;
	FeedStartTime = GetWorld()->GetTimeSeconds();

	ApplyCastSlow();
	StartInterruptWatch();

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		// Barre de cast : durée maximale possible, remplacée par la vraie incantation à la fin du nourrissage
		Character->StartCast(GetClass(), GetAvailableFeed() * FeedInterval + CastTime, CastFX, CastFXSocket);
	}

	if (IsLocallyControlled())
	{
		if (GetAvailableFeed() == 0)
		{
			StopFeedingLocal(); // rien à nourrir : on incante directement
			return;
		}

		FeedReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, /*bTestAlreadyReleased*/ true);
		FeedReleaseTask->OnRelease.AddDynamic(this, &ThisClass::OnFeedInputReleased);
		FeedReleaseTask->ReadyForActivation();

		// Un simple appui (déjà relâché) a pu terminer le nourrissage pendant ReadyForActivation
		if (bIsFeeding)
		{
			ScheduleFeedTick();
		}
	}
	else
	{
		// Serveur pour un client distant : c'est le client qui annonce la fin du nourrissage
		UAbilityTask_NetworkSyncPoint* SyncTask = UAbilityTask_NetworkSyncPoint::WaitNetSync(this, EAbilityTaskNetSyncType::OnlyServerWait);
		SyncTask->OnSync.AddDynamic(this, &ThisClass::OnFeedSynced);
		SyncTask->ReadyForActivation();

		// Estimation cosmétique pour les autres joueurs (les flammes quittent l'orbite)
		ScheduleFeedTick();
	}
}

void UGenGA_Projectile::ScheduleFeedTick()
{
	FeedTickTask = UAbilityTask_WaitDelay::WaitDelay(this, FeedInterval);
	FeedTickTask->OnFinish.AddDynamic(this, &ThisClass::OnFeedTick);
	FeedTickTask->ReadyForActivation();
}

void UGenGA_Projectile::OnFeedTick()
{
	if (!bIsFeeding)
	{
		return;
	}

	const int32 Limit = GetAvailableFeed();
	if (FedCount < Limit)
	{
		++FedCount;

		// Serveur : le compte exact annoncé par le client (ServerReportFedResource) a pu arriver
		// avant ce tick de l'estimation : ne pas faire redescendre l'affichage
		const AGenCharacterBase* Character = GetGenCharacterFromActorInfo();
		SetFedVisual(Character ? FMath::Max<int32>(FedCount, Character->GetFedResource()) : FedCount);
	}

	if (FedCount >= Limit)
	{
		if (IsLocallyControlled())
		{
			StopFeedingLocal(); // plus rien à absorber : l'incantation enchaîne même si la touche reste enfoncée
		}
		return; // serveur : attend le signal du client
	}

	ScheduleFeedTick();
}

void UGenGA_Projectile::OnFeedInputReleased(float TimeHeld)
{
	if (bIsFeeding)
	{
		StopFeedingLocal();
	}
}

void UGenGA_Projectile::StopFeedingLocal()
{
	if (!bIsFeeding)
	{
		return;
	}

	EndFeedTasks();

	// Client distant : le serveur n'a qu'une estimation des unités nourries (son minuteur peut avoir un
	// tick de retard, surtout quand le client s'arrête pile au maximum). On lui annonce le compte exact
	// pour que les autres joueurs voient le bon nombre de flammes quitter l'orbite pendant l'incantation.
	if (FedCount > 0 && CurrentActorInfo && !CurrentActorInfo->IsNetAuthority())
	{
		if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
		{
			Character->ServerReportFedResource(GetClass(), static_cast<uint8>(FMath::Clamp(FedCount, 0, 255)));
		}
	}

	// Client : envoie le signal au serveur et continue sans attendre. Hôte : se termine aussitôt.
	UAbilityTask_NetworkSyncPoint* SyncTask = UAbilityTask_NetworkSyncPoint::WaitNetSync(this, EAbilityTaskNetSyncType::OnlyServerWait);
	SyncTask->OnSync.AddDynamic(this, &ThisClass::OnFeedSynced);
	SyncTask->ReadyForActivation();
}

void UGenGA_Projectile::OnFeedSynced()
{
	if (!bIsFeeding)
	{
		return;
	}

	bIsFeeding = false;
	EndFeedTasks();

	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		ServerFeedElapsed = GetWorld()->GetTimeSeconds() - FeedStartTime;
	}

	GEN_ABILITY_LOG(Verbose, "Nourrissage terminé : %d (%.2fs)", FedCount, GetWorld()->GetTimeSeconds() - FeedStartTime);

	if (CastTime > 0.f)
	{
		StartCasting();
	}
	else
	{
		OnCastFinished();
	}
}

void UGenGA_Projectile::EndFeedTasks()
{
	if (FeedTickTask)
	{
		FeedTickTask->EndTask();
		FeedTickTask = nullptr;
	}
	if (FeedReleaseTask)
	{
		FeedReleaseTask->EndTask();
		FeedReleaseTask = nullptr;
	}
}

void UGenGA_Projectile::SetFedVisual(int32 Count)
{
	FedVisualCount = Count;
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->SetFedResource(static_cast<uint8>(FMath::Clamp(Count, 0, 255)));
	}
}

void UGenGA_Projectile::StartCasting()
{
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StartCast(GetClass(), CastTime, CastFX, CastFXSocket);
	}

	ApplyCastSlow(); // sans effet s'il est déjà actif depuis le nourrissage
	StartInterruptWatch();

	// Le geste continue après la fin normale du sort (le lancer tombe à la fin de l'incantation).
	// Une annulation (étourdi, mort) le coupe quand même : la tâche écoute OnGameplayAbilityCancelled.
	if (ChargeMontage)
	{
		UAbilityTask_PlayMontageAndWait* ChargeTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, ChargeMontage, 1.f, NAME_None, /*bStopWhenAbilityEnds*/ false);
		ChargeTask->ReadyForActivation();
	}

	UAbilityTask_WaitDelay* CastTask = UAbilityTask_WaitDelay::WaitDelay(this, CastTime);
	CastTask->OnFinish.AddDynamic(this, &ThisClass::OnCastFinished);
	CastTask->ReadyForActivation();
}

void UGenGA_Projectile::ApplyCastSlow()
{
	// Ralenti appliqué dans la fenêtre de prédiction de l'activation
	if (CastSlowHandle.IsValid() || CastMoveSpeedMultiplier >= 1.f)
	{
		return;
	}

	FGameplayEffectSpecHandle SlowSpec = MakeOutgoingGameplayEffectSpec(UGenGE_MoveSpeedMultiplier::StaticClass(), GetAbilityLevel());
	if (SlowSpec.IsValid())
	{
		SlowSpec.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_MoveSpeedMultiplier, CastMoveSpeedMultiplier);
		CastSlowHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, SlowSpec);
	}
}

void UGenGA_Projectile::StartInterruptWatch()
{
	if (bInterruptWatchStarted)
	{
		return;
	}
	bInterruptWatchStarted = true;

	// Un étourdissement interrompt le nourrissage et l'incantation (la mort annule déjà tous les sorts)
	UAbilityTask_WaitGameplayTagAdded* StunTask = UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(this, GenGameplayTags::State_Stunned, nullptr, true);
	StunTask->Added.AddDynamic(this, &ThisClass::OnCastInterrupted);
	StunTask->ReadyForActivation();
}

void UGenGA_Projectile::EndCastPresentation()
{
	if (CastSlowHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(CastSlowHandle);
		CastSlowHandle.Invalidate();
	}

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StopCast(GetClass()); // sans effet si un autre sort a pris la barre
	}
}

void UGenGA_Projectile::StopCasting()
{
	bIsFeeding = false;
	EndFeedTasks();

	// Le serveur a pu recevoir le compte du client (ServerReportFedResource) sans avoir lui-même rien affiché
	const AGenCharacterBase* Character = GetGenCharacterFromActorInfo();
	if (FedVisualCount > 0 || (bFeedable && Character && Character->GetFedResource() > 0))
	{
		SetFedVisual(0);
	}

	EndCastPresentation();
}

void UGenGA_Projectile::OnCastInterrupted()
{
	GEN_ABILITY_LOG(Verbose, "Incantation interrompue (étourdi)");
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void UGenGA_Projectile::OnCastFinished()
{
	GEN_ABILITY_LOG(Verbose, "Incantation terminée, attente de la visée (nourri : %d)", FedCount);
	EndCastPresentation();

	// Visée lue maintenant : le joueur peut ajuster pendant toute l'incantation
	UGenAbilityTask_TargetDataUnderCursor* TargetTask = UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(this, static_cast<uint8>(FMath::Clamp(FedCount, 0, 255)));
	TargetTask->ValidData.AddDynamic(this, &ThisClass::OnTargetDataReady);
	TargetTask->ReadyForActivation();
}

int32 UGenGA_Projectile::ResolveFedCount(const FGameplayAbilityTargetData* Data) const
{
	if (!bFeedable)
	{
		return 0;
	}

	const FGenTargetData_Aim* AimData = (Data && Data->GetScriptStruct() == FGenTargetData_Aim::StaticStruct())
		? static_cast<const FGenTargetData_Aim*>(Data)
		: nullptr;
	const int32 Reported = AimData ? AimData->FedCount : 0;

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;

	// Serveur pour un client distant : le client ne peut annoncer ni plus que ce qu'il a, ni plus que le temps écoulé
	if (CurrentActorInfo->IsNetAuthority() && !IsLocallyControlled())
	{
		const int32 Validated = GenFeeding::ValidateFedCount(Reported, MaxFeed, Available, ServerFeedElapsed, FeedInterval);
		if (Validated != Reported)
		{
			GEN_ABILITY_LOG(Warning, "Nourrissage corrigé par le serveur : %d -> %d (ressource %.0f, %.2fs)", Reported, Validated, Available, ServerFeedElapsed);
		}
		return Validated;
	}

	return FMath::Min(Reported, GenFeeding::GetFeedLimit(MaxFeed, Available));
}

void UGenGA_Projectile::SpendResource(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UGenGE_Gain::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		UGenGE_Gain::SetMagnitudes(*Spec.Data, 0.f, -static_cast<float>(Amount));
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, Spec);
	}
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

	const int32 Fed = ResolveFedCount(Data);

	// Le sort part : on applique cooldown, coût et dépense des flammes maintenant, pour qu'une
	// incantation interrompue (annulée, étourdi, mort) ne coûte rien. Le client est dans la fenêtre
	// de prédiction ouverte par la tâche de visée, le serveur dans celle de la clé reçue.
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		GEN_ABILITY_LOG(Verbose, "CommitAbility a échoué au lancer");
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	SpendResource(Fed);
	SetFedVisual(0);

	GEN_ABILITY_LOG(Verbose, "Visée reçue : %s, nourri : %d", *Hit->Location.ToCompactString(), Fed);

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

	SpawnProjectile(Avatar->GetActorLocation() + Direction * 1000.f, Fed);

	// Le client ne réplique PAS la fin du sort : son incantation finit avant celle du serveur
	// (qui a démarré plus tard), et un EndAbility répliqué tuerait le sort côté serveur avant
	// qu'il ait fait apparaître le projectile. C'est le serveur qui termine et prévient le client.
	const bool bReplicateEnd = CurrentActorInfo && CurrentActorInfo->IsNetAuthority();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicateEnd, false);
}

void UGenGA_Projectile::SpawnProjectile(const FVector& TargetLocation, int32 Fed)
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

	const float ClassSpeed = ProjectileClass->GetDefaultObject<AGenProjectile>()->GetSpeed();
	FGenProjectileShotParams ShotParams;
	ShotParams.Speed = GenFeeding::ScaleByFeed(ClassSpeed, ClassSpeed * SpeedMultiplierAtMaxFeed, Fed, MaxFeed);
	ShotParams.Scale = GenFeeding::ScaleByFeed(1.f, ScaleAtMaxFeed, Fed, MaxFeed);
	ShotParams.ExplosionRadius = GenFeeding::ReachesThreshold(Fed, ExplosionMinFeed) ? ExplosionRadius : 0.f;
	ShotParams.KnockbackDistance = GenFeeding::ReachesThreshold(Fed, KnockbackMinFeed) ? KnockbackDistance : 0.f;
	Projectile->InitializeShot(ShotParams);

	GEN_ABILITY_LOG(Verbose, "Projectile %s créé en %s (nourri %d, vitesse %.0f, échelle %.2f, zone %.0f, repoussement %.0f)",
		*Projectile->GetName(), *SpawnTransform.GetLocation().ToCompactString(), Fed, ShotParams.Speed, ShotParams.Scale, ShotParams.ExplosionRadius, ShotParams.KnockbackDistance);

	const int32 Level = GetAbilityLevel();

	if (DamageEffectClass)
	{
		FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, Level);
		if (SpecHandle.IsValid())
		{
			SpecHandle.Data->GetContext().AddSourceObject(Projectile);
			SpecHandle.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Damage, Damage.GetValueAtLevel(Level) + DamagePerFeed * Fed);
			Projectile->DamageEffectSpecHandle = SpecHandle;
		}
	}

	const float EnergyGain = EnergyOnHit + EnergyPerFeed * Fed;
	if (EnergyGain > 0.f || ResourceOnHit > 0.f)
	{
		FGameplayEffectSpecHandle GainSpec = MakeOutgoingGameplayEffectSpec(UGenGE_Gain::StaticClass(), Level);
		if (GainSpec.IsValid())
		{
			UGenGE_Gain::SetMagnitudes(*GainSpec.Data, EnergyGain, ResourceOnHit);
			Projectile->InstigatorOnHitSpecHandle = GainSpec;
		}
	}

	Projectile->FinishSpawning(SpawnTransform);
}
