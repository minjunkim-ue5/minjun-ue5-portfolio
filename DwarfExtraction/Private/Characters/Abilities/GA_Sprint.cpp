#include "Characters/Abilities/GA_Sprint.h"
#include "AbilitySystemComponent.h"
#include "Characters/PlayerAttributeSet.h"

UGA_Sprint::UGA_Sprint()
{
	// 이 어빌리티는 캐릭터마다 하나씩 인스턴스를 가짐 (일반적인 설정)
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UGA_Sprint::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const UPlayerAttributeSet* AttrSet = Cast<UPlayerAttributeSet>(
		ActorInfo->AbilitySystemComponent->GetAttributeSet(UPlayerAttributeSet::StaticClass()));

	// 스태미나가 너무 적으면 애초에 스프린트 시작을 막음
	if (AttrSet && AttrSet->GetStamina() <= MinStaminaToSprint)
	{
		return false;
	}

	return true;
}

void UGA_Sprint::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UE_LOG(LogTemp, Warning, TEXT("Sprint Activate Called"));
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!ASC || !SprintCostEffect || !SprintSpeedEffect)
	{
		UE_LOG(LogTemp, Warning, TEXT("Effect missing"));

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	// 스태미나 소모 이펙트 적용
	FGameplayEffectSpecHandle CostSpec = ASC->MakeOutgoingSpec(SprintCostEffect, 1.f, Context);
	if (CostSpec.IsValid())
	{
		CostEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*CostSpec.Data.Get());
	}

	// 이동속도 버프 이펙트 적용
	FGameplayEffectSpecHandle SpeedSpec = ASC->MakeOutgoingSpec(SprintSpeedEffect, 1.f, Context);
	if (SpeedSpec.IsValid())
	{
		SpeedEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpeedSpec.Data.Get());
	}

	// 스태미나가 0이 되는 순간을 감시해서 자동으로 스프린트 종료
	if (const UPlayerAttributeSet* AttrSet = Cast<UPlayerAttributeSet>(ASC->GetAttributeSet(UPlayerAttributeSet::StaticClass())))
	{
		StaminaChangedDelegateHandle = ASC->GetGameplayAttributeValueChangeDelegate(AttrSet->GetStaminaAttribute())
			.AddUObject(this, &UGA_Sprint::OnStaminaChanged);
	}
}

void UGA_Sprint::OnStaminaChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue <= 0.f)
	{
		CancelAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true);
	}
}

void UGA_Sprint::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UE_LOG(LogTemp, Warning, TEXT("EndAbility Called"));


	if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		// 적용해뒀던 이펙트 2개 제거 (제거하면 스태미나 소모/속도버프 둘 다 즉시 멈춤)
		if (CostEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(CostEffectHandle);
		}
		if (SpeedEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(SpeedEffectHandle);
		}

		if (const UPlayerAttributeSet* AttrSet = Cast<UPlayerAttributeSet>(ASC->GetAttributeSet(UPlayerAttributeSet::StaticClass())))
		{
			if (StaminaChangedDelegateHandle.IsValid())
			{
				ASC->GetGameplayAttributeValueChangeDelegate(AttrSet->GetStaminaAttribute()).Remove(StaminaChangedDelegateHandle);
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}