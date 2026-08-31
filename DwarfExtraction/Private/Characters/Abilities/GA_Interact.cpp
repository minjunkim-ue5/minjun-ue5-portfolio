#include "Characters/Abilities/GA_Interact.h"
#include "Characters/PlayerCharacter.h"
#include "Characters/InteractableInterface.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Controller.h"

UGA_Interact::UGA_Interact()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

AActor* UGA_Interact::TraceForTarget(const FGameplayAbilityActorInfo* ActorInfo) const
{
	APlayerCharacter* Character = Cast<APlayerCharacter>(ActorInfo->AvatarActor.Get());
	if (!Character) return nullptr;

	AController* Controller = Character->GetController();
	if (!Controller) return nullptr;

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector Start = ViewLocation;
	const FVector End = Start + ViewRotation.Vector() * InteractRange;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);

	if (Character->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params))
	{
		return Hit.GetActor();
	}
	return nullptr;
}

void UGA_Interact::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	APlayerCharacter* Character = Cast<APlayerCharacter>(ActorInfo->AvatarActor.Get());
	if (!Character || !Character->HasAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	AActor* Target = TraceForTarget(ActorInfo);
	if (!Target || !Target->Implements<UInteractableInterface>())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 대상이 다운된 플레이어면 홀드 모드
	APlayerCharacter* TargetPlayer = Cast<APlayerCharacter>(Target);
	if (TargetPlayer && TargetPlayer->IsDowned() && !TargetPlayer->IsDead())
	{
		ReviveTarget = TargetPlayer;
		ReviveInstigator = Character;
		ReviveElapsed = 0.f;

		Character->SetReviving(true);   // 시전자만 표시 — 피격 시 부활 중단 판정에 사용
		TargetPlayer->PauseBleedOut();   // 홀드 중에는 대상의 출혈 타이머를 멈춤 (성공 시 재개하지 않음)

		// 0.1초마다 홀드 유효성 검사
		Character->GetWorldTimerManager().SetTimer(ReviveTickHandle, [this]()
		{
			TickReviveHold();
		}, 0.1f, true);

		return;   // 어빌리티는 계속 활성 상태로 유지 (E를 떼면 CancelAbilities로 종료됨)
	}

	// 그 외(아이템 등) → 기존처럼 즉시 상호작용
	IInteractableInterface::Execute_Interact(Target, Character);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_Interact::TickReviveHold()
{
	APlayerCharacter* Target = ReviveTarget.Get();
	APlayerCharacter* Instigator = ReviveInstigator.Get();

	// 유효성 검사: 대상/시전자 상태, 사거리
	const bool bValid =
		Target && Instigator &&
		Target->IsDowned() && !Target->IsDead() &&
		!Instigator->IsDowned() && !Instigator->IsDead() &&
		FVector::Dist(Target->GetActorLocation(), Instigator->GetActorLocation()) <= InteractRange;

	if (!bValid)
	{
		CancelAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true);
		return;
	}

	ReviveElapsed += 0.1f;

	// 진행도 디버그 표시 (부활시키는 사람 화면에)
	if (GEngine)
	{
		const float Percent = FMath::Clamp(ReviveElapsed / ReviveHoldTime, 0.f, 1.f) * 100.f;
		GEngine->AddOnScreenDebugMessage(
			1,                          // 같은 Key(1)를 쓰면 매 틱 갱신되어 한 줄만 유지됨
			0.2f,                       // 표시 시간 (틱 간격보다 살짝 길게)
			FColor::Green,
			FString::Printf(TEXT("Reviving... %.0f%%"), Percent));
	}

	if (ReviveElapsed >= ReviveHoldTime)
	{
		// 부활 실행
		IInteractableInterface::Execute_Interact(Target, Instigator);
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

void UGA_Interact::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (APlayerCharacter* I = ReviveInstigator.Get())
	{
		I->SetReviving(false);
	}

	// 부활이 취소된 경우(성공 아님) 대상이 아직 다운이면 출혈 재개
	if (APlayerCharacter* T = ReviveTarget.Get())
	{
		if (T->IsDowned())   // 부활 성공했으면 IsDowned가 false라 재개 안 됨
		{
			T->ResumeBleedOut();
		}
	}

	// 타이머 정리
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		ActorInfo->AvatarActor->GetWorldTimerManager().ClearTimer(ReviveTickHandle);
	}
	ReviveTarget = nullptr;
	ReviveInstigator = nullptr;
	ReviveElapsed = 0.f;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}