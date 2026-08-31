#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "BaseDoor.generated.h"

class UStaticMeshComponent;
class USoundBase;


UCLASS()
class TEAMPROJDWEX54_API ABaseDoor : public AActor
{
	GENERATED_BODY()

public:
	ABaseDoor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Door")
	void SetOpen(bool bNewOpen);

	UFUNCTION(BlueprintCallable, Category = "Door")
	void Unlock();

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsOpen() const { return bIsOpen; }

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsLocked() const { return bIsLocked; }

protected:
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;

	// 열렸을 때 이동할 상대 오프셋 (닫힌 위치 기준). 위로 올리는 문이면 Z만, 옆으로 밀리는 문이면 X/Y 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Motion")
	FVector OpenOffset = FVector(0.f, 0.f, 250.f);

	// 완전히 열리거나 닫히는 데 걸리는 시간(초)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Motion", meta = (ClampMin = "0.0"))
	float OpenDuration = 1.5f;

	// 가감속 강도. 1이면 등속, 클수록 시작·끝이 부드러움
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Motion", meta = (ClampMin = "1.0"))
	float EaseExponent = 2.f;

	UPROPERTY(ReplicatedUsing = OnRep_DoorState, EditAnywhere, BlueprintReadOnly, Category = "Door")
	bool bIsOpen = false;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Door")
	bool bIsLocked = false;

	UFUNCTION()
	void OnRep_DoorState();

	// bInstant = true면 보간 없이 즉시 반영 (시작 상태용)
	void ApplyDoorState(bool bInstant);

	void UpdateDoorTransform();

	UFUNCTION(BlueprintImplementableEvent, Category = "Door")
	void OnDoorStateUpdated(bool bOpen);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Door|Sound")
	TObjectPtr<USoundBase> DoorOpenSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Door|Sound")
	TObjectPtr<USoundBase> DoorCloseSound;

	FVector ClosedRelativeLocation = FVector::ZeroVector;

	// 0 = 완전히 닫힘, 1 = 완전히 열림 (복제하지 않고 각 머신이 자체 계산)
	float DoorProgress = 0.f;
};