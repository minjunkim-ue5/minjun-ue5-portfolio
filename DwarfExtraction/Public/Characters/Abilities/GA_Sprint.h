#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ActiveGameplayEffectHandle.h"
#include "GA_Sprint.generated.h"

class UGameplayEffect;
struct FOnAttributeChangeData;

UCLASS()
class TEAMPROJDWEX54_API UGA_Sprint : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Sprint();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	// 스태미나를 깎는 이펙트 (0.2초마다 -5 같은 식으로 설정할 예정)
	UPROPERTY(EditDefaultsOnly, Category = "Sprint")
	TSubclassOf<UGameplayEffect> SprintCostEffect;

	// 이동속도를 올려주는 이펙트
	UPROPERTY(EditDefaultsOnly, Category = "Sprint")
	TSubclassOf<UGameplayEffect> SprintSpeedEffect;

	// 이 수치 이하로 스태미나가 떨어지면 스프린트 시작 자체를 막음
	UPROPERTY(EditDefaultsOnly, Category = "Sprint")
	float MinStaminaToSprint = 5.f;

private:
	FActiveGameplayEffectHandle CostEffectHandle;
	FActiveGameplayEffectHandle SpeedEffectHandle;
	FDelegateHandle StaminaChangedDelegateHandle;

	void OnStaminaChanged(const FOnAttributeChangeData& Data);
};