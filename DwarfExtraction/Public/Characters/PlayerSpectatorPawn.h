#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "PlayerSpectatorPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class APlayerCharacter;

UCLASS()
class TEAMPROJDWEX54_API APlayerSpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	APlayerSpectatorPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 다음 관전 대상으로 전환 (BP의 좌클릭 입력에서 호출)
	UFUNCTION(BlueprintCallable, Category = "Spectate")
	void CycleNextTarget();

	// 현재 관전 중인 플레이어 (UI 표시용)
	UFUNCTION(BlueprintPure, Category = "Spectate")
	APlayerCharacter* GetSpectateTarget() const { return SpectateTarget; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> SpectatorCamera;

	// 관전 대상 (복제 - 서버/클라 모두 같은 대상을 따라감)
	UPROPERTY(ReplicatedUsing = OnRep_SpectateTarget, BlueprintReadOnly, Category = "Spectate")
	TObjectPtr<APlayerCharacter> SpectateTarget;

	UFUNCTION()
	void OnRep_SpectateTarget();

	// 대상 위치 기준 오프셋 (허리~가슴 높이를 보게)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spectate")
	FVector TargetOffset = FVector(0.f, 0.f, 60.f);

	// 위치 추적 보간 속도 (클수록 즉각적)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spectate")
	float FollowInterpSpeed = 15.f;

	UFUNCTION(Server, Reliable)
	void Server_CycleNextTarget();

	// 관전 가능한(살아있는) 플레이어 목록 수집
	void GatherTargets(TArray<APlayerCharacter*>& Out) const;
};