#include "AbilitySystem/GenAttributeSet.h"

#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "GenGameplayTags.h"
#include "Net/UnrealNetwork.h"

UGenAttributeSet::UGenAttributeSet()
{
	// Valeurs par défaut. Pour des stats différentes par champion, appliquez un GE
	// instantané "DefaultAttributes" via StartupEffects sur le personnage.
	InitHealth(200.f);
	InitMaxHealth(200.f);
	InitEnergy(25.f); // Chaque manche commence à 25 (guidelines §4.1)
	InitMaxEnergy(100.f);
	InitResource(0.f);
	InitMaxResource(0.f);
	InitMoveSpeed(550.f);
	InitIncomingDamage(0.f);
	InitIncomingHealing(0.f);
}

void UGenAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, Energy, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, MaxEnergy, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, Resource, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, MaxResource, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGenAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
}

void UGenAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.f);
	}
	else if (Attribute == GetEnergyAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxEnergy());
	}
	else if (Attribute == GetMaxEnergyAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetResourceAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxResource());
	}
	else if (Attribute == GetMaxResourceAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetMoveSpeedAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
}

void UGenAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UGenAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UGenAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();

	if (Attribute == GetMaxHealthAttribute() && ASC && GetHealth() > NewValue)
	{
		// Si la vie max baisse, la vie courante ne doit pas la dépasser
		ASC->ApplyModToAttribute(GetHealthAttribute(), EGameplayModOp::Override, NewValue);
	}
	else if (Attribute == GetMaxEnergyAttribute() && ASC && GetEnergy() > NewValue)
	{
		ASC->ApplyModToAttribute(GetEnergyAttribute(), EGameplayModOp::Override, NewValue);
	}
	else if (Attribute == GetMaxResourceAttribute() && ASC && GetResource() > NewValue)
	{
		ASC->ApplyModToAttribute(GetResourceAttribute(), EGameplayModOp::Override, NewValue);
	}

	// Réinitialise le flag de mort quand la vie remonte (respawn, résurrection...)
	if (Attribute == GetHealthAttribute() && NewValue > 0.f)
	{
		bOutOfHealth = false;
	}
}

void UGenAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	const FGameplayEffectContextHandle& Context = Data.EffectSpec.GetEffectContext();
	AActor* Instigator = Context.GetOriginalInstigator();
	AActor* Causer = Context.GetEffectCauser();

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float LocalDamage = GetIncomingDamage();
		SetIncomingDamage(0.f);

		// Filet de sécurité (Plan 3 Task 4) : les coups ennemis sont déjà ignorés en amont (ResolveIncomingHit),
		// mais toute autre source de dégâts (mêlée, dégâts sur la durée, scripts) l'est aussi tant qu'il est intouchable
		const UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent();
		const bool bUntouchable = OwnerASC && OwnerASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable);
		if (LocalDamage > 0.f && !bOutOfHealth && !bUntouchable)
		{
			// C'est ici qu'on ajoutera boucliers / réductions de dégâts plus tard
			SetHealth(FMath::Clamp(GetHealth() - LocalDamage, 0.f, GetMaxHealth()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetIncomingHealingAttribute())
	{
		const float LocalHealing = GetIncomingHealing();
		SetIncomingHealing(0.f);

		if (LocalHealing > 0.f && !bOutOfHealth)
		{
			SetHealth(FMath::Clamp(GetHealth() + LocalHealing, 0.f, GetMaxHealth()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}

	if (GetHealth() <= 0.f && !bOutOfHealth)
	{
		bOutOfHealth = true;
		OnOutOfHealth.Broadcast(Instigator, Causer);
	}
}

void UGenAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, Health, OldValue);
}

void UGenAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, MaxHealth, OldValue);
}

void UGenAttributeSet::OnRep_Energy(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, Energy, OldValue);
}

void UGenAttributeSet::OnRep_MaxEnergy(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, MaxEnergy, OldValue);
}

void UGenAttributeSet::OnRep_MoveSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, MoveSpeed, OldValue);
}

void UGenAttributeSet::OnRep_Resource(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, Resource, OldValue);
}

void UGenAttributeSet::OnRep_MaxResource(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGenAttributeSet, MaxResource, OldValue);
}
