#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Interact.generated.h"

class APlayerCharacter;

UCLASS()
class TEAMPROJDWEX54_API UGA_Interact : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Interact();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Interact")
	float InteractRange = 300.f;

	// 부활에 필요한 홀드 시간
	UPROPERTY(EditDefaultsOnly, Category = "Interact")
	float ReviveHoldTime = 3.f;

private:
	// 트레이스로 대상 찾기 (성공 시 액터 반환)
	AActor* TraceForTarget(const FGameplayAbilityActorInfo* ActorInfo) const;

	// 홀드 진행 중 매 틱 확인
	void TickReviveHold();

	FTimerHandle ReviveTickHandle;
	float ReviveElapsed = 0.f;
	TWeakObjectPtr<APlayerCharacter> ReviveTarget;
	TWeakObjectPtr<APlayerCharacter> ReviveInstigator;
};