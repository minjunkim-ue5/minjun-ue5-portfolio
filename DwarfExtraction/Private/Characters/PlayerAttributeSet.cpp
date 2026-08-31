// PlayerAttributeSet.cpp
#include "Characters/PlayerAttributeSet.h"
#include "Net/UnrealNetwork.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffectExtension.h"   // ← 추가: FGameplayEffectModCallbackData의 진짜 정의가 여기 있음


UPlayerAttributeSet::UPlayerAttributeSet()
{
	// 기본값 초기화 (일단 임시 수치. 나중에 GameplayEffect로 초기화하는 방식으로 바꿀 수도 있음)
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
	InitMoveSpeed(300.f);   // 추가
}

void UPlayerAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// DOREPLIFETIME_CONDITION_NOTIFY: "누구에게" 복제할지 + "복제되면 OnRep 함수 호출" 설정
	// COND_None: 모든 클라이언트에게 복제
	DOREPLIFETIME_CONDITION_NOTIFY(UPlayerAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPlayerAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPlayerAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPlayerAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPlayerAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
}

void UPlayerAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	// GAMEPLAYATTRIBUTE_REPNOTIFY: ASC에게 "이 값 방금 복제됐어" 라고 알려주는 필수 매크로
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPlayerAttributeSet, Health, OldHealth);
}

void UPlayerAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPlayerAttributeSet, MaxHealth, OldMaxHealth);
}

void UPlayerAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPlayerAttributeSet, Stamina, OldStamina);
}

void UPlayerAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPlayerAttributeSet, MaxStamina, OldMaxStamina);
}

void UPlayerAttributeSet::OnRep_MoveSpeed(const FGameplayAttributeData& OldMoveSpeed)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPlayerAttributeSet, MoveSpeed, OldMoveSpeed);

	// 클라이언트 쪽에서도 실제 이동속도에 반영
	if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
	{
		if (ACharacter* Character = Cast<ACharacter>(ASC->GetAvatarActor()))
		{
			Character->GetCharacterMovement()->MaxWalkSpeed = GetMoveSpeed();
		}
	}
}

void UPlayerAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// Health/Stamina가 Max를 넘거나 0 밑으로 내려가지 않게 고정
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.f, GetMaxStamina()));
	}
	else if (Data.EvaluatedData.Attribute == GetMoveSpeedAttribute())
	{
		UE_LOG(LogTemp, Warning, TEXT("MoveSpeed Changed"));   // 추가

		// 서버(그리고 리슨서버 호스트)에서 즉시 반영
		if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
		{
			if (ACharacter* Character = Cast<ACharacter>(ASC->GetAvatarActor()))
			{
				Character->GetCharacterMovement()->MaxWalkSpeed = GetMoveSpeed();
			}
		}
	}
}