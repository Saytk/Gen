#include "Champions/Curffe/CurffeGA_LivingFlame.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "Actors/GenGroundArea.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
#include "Character/GenCharacterBase.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeLivingFlame, Log, All);

UCurffeGA_LivingFlame::UCurffeGA_LivingFlame()
{
	// Valeurs de la spec (Curffe.md §3 R) : l'asset GA_LivingFlame peut les reprendre telles quelles
	InputTag = GenGameplayTags::InputTag_Ability_3;
	EnergyCost = 25.f;
	CooldownDuration = FScalableFloat(16.f);
	CooldownTags.AddTag(CurffeGameplayTags::Cooldown_Ability_LivingFlame);
	SetAssetTags(FGameplayTagContainer(CurffeGameplayTags::Ability_LivingFlame));

	CastTime = 0.1f;
	bTurnToAim = false;
	BurstAreaClass = AGenGroundArea::StaticClass();
	RefillEffect = UCurffeGE_HearthFill::StaticClass();
}

void UCurffeGA_LivingFlame::OnCastLaunched(const FGenCastRelease& Release)
{
	// Forme de feu prédite chez le client : intouchable, vue par tous (État + tag propre à Curffe pour son visuel).
	// Revue P3 T8-10, M1 : le GE prédit est remplacé par celui du serveur (commencé ~½ RTT plus tard, retrait reçu ~½ RTT
	// après sa fin) : le propriétaire garde les tags de la forme ~1 RTT après SA fin de forme, alors qu'il peut déjà agir
	// (verrou local levé) et être touché (tout est décidé par le serveur). Affichage seulement ; son Foyer suit son verrou
	FGameplayEffectSpecHandle FormSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedState::StaticClass(), GetAbilityLevel());
	if (FormSpec.IsValid())
	{
		FGameplayTagContainer FormTags;
		FormTags.AddTag(GenGameplayTags::State_Untouchable);
		FormTags.AddTag(CurffeGameplayTags::State_LivingFlame);
		UGenGE_TimedState::SetDuration(*FormSpec.Data, FormDuration, FormTags);
		FormEffectHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, FormSpec);
	}

	// Revue P3 T8-10, I2 : Foyer plein dès le départ, prédit chez le client (fenêtre de la visée) et annulé si le serveur
	// refuse le lancer. Rien ne peut le dépenser pendant la forme (verrou de lancement) : même résultat qu'à la fin de la
	// forme, sans le trou d'un RTT où « flamme vivante -> grande boule de feu à 3 flammes » partait sans flamme. Le Foyer
	// n'affiche les flammes qu'à la fin de la forme (UCurffeHearthComponent).
	if (RefillEffect)
	{
		ApplyGameplayEffectToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, RefillEffect->GetDefaultObject<UGameplayEffect>(), GetAbilityLevel());
	}

	// Sans sort pendant la forme : verrou de lancement, seul point d'entrée du tag (State.CastLocked local + fenêtre
	// du serveur : la forme du serveur commence ~½ RTT après celle du client, il ne refuse ses sorts qu'au début)
	SetCastLock(true, FormDuration);

	// Plan Visuals V4 : la forme se lit comme une canalisation (barre qui se vide, télégraphe de l'anneau)
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StartChannel(GetClass(), FormDuration);
	}

	UAbilityTask_WaitDelay* FormTask = UAbilityTask_WaitDelay::WaitDelay(this, FormDuration);
	FormTask->OnFinish.AddDynamic(this, &ThisClass::OnFormEnded);
	FormTask->ReadyForActivation();

	UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("[%s] %s : forme de feu (%.2fs)"), CurrentActorInfo && CurrentActorInfo->IsNetAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"),
		*GetName(), FormDuration);
}

void UCurffeGA_LivingFlame::OnFormEnded()
{
	// Il peut de nouveau lancer (pendant la hâte) ; la forme elle-même expire avec sa durée
	SetCastLock(false);
	FormEffectHandle.Invalidate();
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StopCast(GetClass());
	}

	// Plan Visuals V3 : geste de l'anneau, à vitesse 1, jusqu'au bout même après la fin du sort
	PlayPhaseMontage(FinishMontage, 1.f, /*bStopWhenAbilityEnds*/ false);

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority())
	{
		// Anneau (zone : traverse les contres)
		FGenAreaParams Params;
		Params.Radius = BurstRadius;
		Params.KnockbackDistance = BurstKnockback;
		SpawnGroundArea(BurstAreaClass, Avatar->GetActorLocation(), Params, UGenGE_Damage::StaticClass(), BurstDamage, 0.f);

		// Hâte : appliquée par le serveur (hors fenêtre de prédiction), répliquée au propriétaire. Revue P3 T8-10, M3 :
		// ~1 RTT de décalage chez le client (petites corrections du mouvement au début et à la fin). Point ouvert : la
		// version par machine passerait par AGenCharacterBase::SetLocalMoveSpeedMultiplier (grâce de mouvement du serveur)
		FGameplayEffectSpecHandle HasteSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedMoveSpeed::StaticClass(), GetAbilityLevel());
		if (HasteSpec.IsValid())
		{
			UGenGE_TimedMoveSpeed::SetMagnitudes(*HasteSpec.Data, HasteDuration, HasteMultiplier, FGameplayTagContainer());
			ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, HasteSpec);
		}

		UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("[SERVEUR] %s : anneau (%.0f cm), hâte x%.2f %.1fs"), *GetName(), BurstRadius, HasteMultiplier, HasteDuration);
	}

	FinishAbility();
}

void UCurffeGA_LivingFlame::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Fin anticipée (mort) : la forme s'arrête avec le sort. Le verrou et la barre sont retirés par la base.
	if (FormEffectHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(FormEffectHandle);
		FormEffectHandle.Invalidate();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
