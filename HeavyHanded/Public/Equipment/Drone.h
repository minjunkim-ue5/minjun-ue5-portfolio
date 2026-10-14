#pragma once

#include "CoreMinimal.h"
#include "Core/HeistPhase.h"          // EHeistPhaseReason — 델리게이트 시그니처에 값으로 들어가 전방 선언 불가
#include "Equipment/EquipmentBase.h"
#include "Drone.generated.h"

class APlayerController;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * 정찰 드론. 던져 놓으면 떠올라 조종할 수 있고, 그 시점으로 잠긴 스테이지 안을 미리 본다.
 * (기획서 7장 — $4,000 / 준비 시간 45초 동안 사전 정찰)
 *
 * [던지는 것이 곧 사용이다]
 *   ActivationMode 는 OnImpact 다. 집어서 던지면 바닥에 닿는 순간 Deployed -> Active 로
 *   넘어가고, 그 자리에서 떠올라 조종이 시작된다.
 *
 *   ManualActivate 를 쓰지 않은 이유가 여기 있다. 별도의 '사용' 키를 만들면 입력 자산과
 *   IMC 를 플레이어 파트에 요청해야 하는데, 던지기가 이미 플레이어가 타이밍을 고르는
 *   입력이다. 조작도 다른 장비와 똑같아서 새로 배울 것이 없다.
 *
 * [폰을 Possess 하지 않는다]
 *   시점만 옮긴다 — UHeistSpectatorComponent 가 관전에서 쓰는 것과 같은 방식이다.
 *     PC->SetViewTarget(this, Blend)            시점
 *     Subsystem->AddMappingContext(IMC, 100)    입력
 *   Possess 로 폰을 갈아치우면 조종사의 GAS·다운 상태·소지품이 전부 걸린다.
 *   시점만 옮기면 **플레이어 파트를 한 줄도 고치지 않는다.**
 *
 *   조종 중 본인 몸은 던진 자리에 그대로 서 있다. 무방비로 남는 것이 이 아이템의 대가다.
 *
 * [카메라를 액터가 아니라 컴포넌트로 돌린다]
 *   AEquipmentBase 는 SetReplicateMovement(true) 다. 액터 회전을 조종사가 로컬로 돌리면
 *   서버에서 복제된 회전이 매 프레임 그것을 덮어써서 시야가 떤다.
 *   그래서 **카메라 컴포넌트의 상대 회전만 로컬에서 즉시 돌리고**, 같은 값을 서버로 보내
 *   서버가 이동 방향의 기준으로 쓴다. 위치만 복제되고 시야는 지연이 없다.
 *
 * [이 액터는 Owner 를 갖는다]
 *   조종 입력이 Server RPC 로 올라가야 하고, Server RPC 는 액터의 Owner 커넥션에서만
 *   보낼 수 있다. 그래서 발동 시 서버가 SetOwner(조종사 PC) 를 한다.
 *   AShopDisplay 가 Client RPC 를 못 쓴 것과 같은 규칙이고, 이쪽은 조종사가 정확히
 *   한 명이라 소유권을 넘겨도 엉키지 않는다.
 *
 * [끝나는 길이 셋이다 — 하나라도 놓치면 화면이 드론에 갇힌다]
 *   ① EffectDuration 만료 (평시)
 *   ② Phase.Prep 종료 — 남은 시간과 무관하게 끊는다. 본 작업이 드론 시점으로
 *      시작되는 사고를 막는 안전장치다
 *   ③ EndPlay — 액터가 어떤 이유로든 사라질 때의 최후 방어선
 *   StopFlight 는 여러 번 불려도 안전하게 만들어 두었다.
 */
UCLASS()
class HEAVYHANDED_API ADrone : public AEquipmentBase
{
	GENERATED_BODY()

public:
	ADrone();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 던진 사람을 조종사로 기록한다. 베이스가 PrimaryCarrier 를 지우므로 **먼저** 잡아 둔다 */
	virtual void OnThrown(APawn* Carrier, const FVector& AimDirection) override;

	/** 바닥에 닿아 발동했다. 떠올라 조종을 시작한다 */
	virtual void OnActivated() override;

	/** 시간이 다 됐다. 시점과 입력을 반납한다 */
	virtual void OnSpent() override;

	// ---- 비행 ----

	/**
	 * 수평 최고 속도. 뛰는 플레이어보다 확실히 빨라야 한다 —
	 * 45초짜리 준비 시간 안에 건물을 훑어야 하는 물건이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Flight",
		meta = (ClampMin = "0.0", Units = "CentimetersPerSecond"))
	float MaxFlySpeed = 1100.f;

	/** 상승·하강 최고 속도. 수평보다 느린 편이 조작하기 쉽다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Flight",
		meta = (ClampMin = "0.0", Units = "CentimetersPerSecond"))
	float MaxAscendSpeed = 700.f;

	/**
	 * 가속도. 입력을 넣었을 때 최고 속도까지 붙는 빠르기다.
	 * 즉시 최고 속도가 되게 하면 드론이 아니라 순간이동처럼 느껴진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Flight",
		meta = (ClampMin = "1.0"))
	float FlyAcceleration = 3200.f;

	/** 입력을 놓았을 때 멈추는 빠르기. 가속보다 크게 두면 제어하기 쉽다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Flight",
		meta = (ClampMin = "1.0"))
	float FlyBraking = 4000.f;

	/** 마우스 감도 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Flight",
		meta = (ClampMin = "0.01"))
	float LookSensitivity = 1.f;

	/**
	 * 마우스 상하를 뒤집는다.
	 *
	 * 엔진의 Mouse XY 축 부호는 프로젝트 설정과 모디파이어에 따라 갈려서, 어느 쪽이 맞는지
	 * 붙여 보기 전에는 확정할 수 없다. IMC 에 Negate 모디파이어를 넣어 고치는 것보다
	 * 여기서 끄고 켜는 편이 낫다 — 자산을 안 건드리니 다른 사람 작업과 겹치지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Flight")
	bool bInvertLookY = false;

	/** 위아래로 꺾을 수 있는 한계(도). 넘게 두면 화면이 뒤집힌다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Flight",
		meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxPitchDegrees = 80.f;

	// ---- 시점 전환 연출 ----
	//
	// 화면을 한 번 완전히 덮었다가 걷는다. **전환 자체를 어둠이 가린다** —
	// 블렌드로 이어 붙이면 몸에서 드론으로 카메라가 날아가는 것이 그대로 보여서
	// 두 위치가 이어져 있는 것처럼 읽힌다. 드론 화면은 '다른 곳의 영상'이라
	// 끊어 주는 편이 맞다.

	/** 어두워지는 시간. 이 시간이 끝난 뒤에 시점이 바뀐다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|View",
		meta = (ClampMin = "0.0", Units = "s"))
	float ViewFadeOutSeconds = 0.2f;

	/** 다시 밝아지는 시간. 나가는 것보다 조금 길게 두면 덜 급해 보인다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|View",
		meta = (ClampMin = "0.0", Units = "s"))
	float ViewFadeInSeconds = 0.35f;

	/** 덮는 색. 검정이 기본이고, 드론 신호 느낌을 내려면 어두운 청록 같은 것도 쓸 수 있다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|View")
	FLinearColor ViewFadeColor = FLinearColor::Black;

	// ---- 입력 (BP 에서 지정) ----

	/**
	 * 조종 중에만 붙는 입력 컨텍스트. **여기에 W/A/S/D · 마우스 · Space/Ctrl 을 매핑한다.**
	 *
	 * 우선순위를 플레이어 것보다 높게 두면 같은 키가 드론 쪽으로 소비되어 본인 폰은
	 * 움직이지 않는다. 그래서 "조종 중에는 몸이 멈춘다" 를 따로 구현할 필요가 없다.
	 *
	 * ⚠ 플레이어의 IA_Move 를 그대로 재사용하면 폰과 드론이 **동시에** 움직인다.
	 * 드론 전용 IA 를 따로 만들어 물릴 것.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Input")
	TObjectPtr<UInputMappingContext> DroneMappingContext;

	/** 수평 이동. Axis2D (X = 좌우, Y = 전후) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Input")
	TObjectPtr<UInputAction> MoveAction;

	/** 시선. Axis2D (X = 요, Y = 피치) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Input")
	TObjectPtr<UInputAction> LookAction;

	/** 상승·하강. Axis1D (+1 = 상승) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Input")
	TObjectPtr<UInputAction> AscendAction;

	/**
	 * 입력 컨텍스트 우선순위. 플레이어 이동보다 높아야 키를 가져온다.
	 * 이름에 Drone 을 붙인 것은 AActor 에 InputPriority 가 이미 있어서다 (UHT 가 섀도잉을 막는다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Input")
	int32 DroneInputPriority = 100;

	/** 조종 인수·반납과 비행 속도를 로그와 화면에 남긴다. (테스트용) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone|Debug")
	bool bShowDroneDebug = false;

	/**
	 * 조종사가 보는 카메라. 시점 전환의 대상이다.
	 * 액터가 아니라 이 컴포넌트를 돌리는 이유는 클래스 주석을 볼 것.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UCameraComponent> DroneCamera;

private:
	/** 떠올라 비행을 시작한다. (서버 전용) bFlightActive 복제가 조종사 화면을 깨운다 */
	void StartFlight();

	/** 비행을 끝낸다. (서버 전용) **여러 번 불려도 안전하다** — 끝나는 길이 셋이라 그래야 한다 */
	void StopFlight();

	/**
	 * 비행 여부가 복제돼 왔다. **조종사가 클라이언트일 때 시점을 가져가는 유일한 경로다.**
	 *
	 * AEquipmentBase 의 OnActivated / OnSpent 는 서버에서만 불린다(그쪽 주석 참고).
	 * 그래서 상태를 따로 복제하지 않으면 리슨 서버 호스트만 드론을 조종할 수 있다.
	 * 서버에서는 OnRep 이 오지 않으므로 StartFlight / StopFlight 가 직접 부른다.
	 */
	UFUNCTION()
	void OnRep_FlightActive();

	/** 조종사 머신에서 시점과 입력을 가져온다 */
	void TakeControl(APlayerController* PilotController);

	/** 암전이 끝났다. 이제 시점을 바꾸고 입력을 붙인 뒤 다시 밝힌다 */
	void FinishTakeControl();

	/** 가져간 시점과 입력을 되돌린다 */
	void ReleaseControl();

	/** 화면을 덮기 시작한다. 끝나도 덮은 채로 둔다 — 그 사이에 시점을 바꾼다 */
	void StartFadeOut(APlayerController* PilotController) const;

	/** 덮은 화면을 걷는다 */
	static void StartFadeIn(APlayerController* PilotController, float Duration, const FLinearColor& Color);

	/** 조종을 마쳤을 때 돌아갈 대상. 원래 보던 것 → 본인 폰 → 컨트롤러 순으로 떨어진다 */
	AActor* ResolveRestoreTarget(APlayerController* PilotController) const;

	/** 조종사의 PlayerController. 없으면 nullptr */
	APlayerController* GetPilotController() const;

	/** 이 머신이 조종사의 화면인가 */
	bool IsLocalPilot() const;

	// 입력 핸들러 — 조종사 머신에서만 돈다
	void HandleMoveInput(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);
	void HandleAscendInput(const FInputActionValue& Value);

	/** 조종 의도를 서버로 보낸다. 매 틱 나가므로 Unreliable 이다 */
	UFUNCTION(Server, Unreliable)
	void Server_SetFlightIntent(FVector2D InMoveInput, float InAscendInput, FRotator InCameraRotation);

	/** 서버에서 한 틱 비행시킨다. 벽에 닿으면 그 면을 따라 미끄러진다 */
	void TickFlight(float DeltaSeconds);

	/**
	 * 조종 회전을 적용한다. 로컬과 서버가 같은 함수를 쓴다.
	 *
	 * **요는 액터에, 피치는 카메라에 준다.**
	 *   요를 액터에 주어야 드론 몸통이 시야와 함께 돌아간다 — 카메라만 돌리면 조종사가
	 *   옆을 봐도 드론은 던져진 방향을 보고 있어서 몸통이 화면을 가로막는다.
	 *   피치까지 액터에 주면 이동·충돌 판정의 기준이 함께 기울어 벽에 박을 때 위아래로 튄다.
	 */
	void ApplyControlRotation(float Yaw, float Pitch);

	/** Phase.Prep 이 끝나면 남은 시간과 무관하게 끊는다 */
	UFUNCTION()
	void HandlePhaseChanged(FGameplayTag NewPhase, FGameplayTag OldPhase, EHeistPhaseReason Reason);

	void ShowDroneDebug(const FString& Message) const;

	/**
	 * 조종사. 복제되는 이유는 클라이언트도 "내가 조종사인가" 를 알아야 시점을 가져오기
	 * 때문이다. 던진 사람이고, 베이스가 PrimaryCarrier 를 지우기 전에 잡아 둔다.
	 */
	UPROPERTY(Replicated)
	TObjectPtr<APawn> Pilot;

	/** 서버가 들고 있는 현재 속도. 복제하지 않는다 — 위치만 복제하면 충분하다 */
	FVector FlightVelocity = FVector::ZeroVector;

	/** 조종사가 보낸 최신 의도. 서버에서만 읽는다 */
	FVector2D MoveInput = FVector2D::ZeroVector;
	float AscendInput = 0.f;

	/**
	 * 지금 향하고 있는 방향. **이동 방향의 기준은 이 값이지 컴포넌트 트랜스폼이 아니다.**
	 *
	 * 카메라의 월드 회전을 읽어 쓰다가, 조종사가 클라이언트일 때 W 가 위로만 가는 버그가 났다.
	 * 서버는 그 경우 bHasControl 이 false 라 매 틱 회전을 다시 씌우지 않는데,
	 * StartFlight 의 수평 세우기가 물리 트랜스폼 동기화에 덮여 액터가 기울어진 채 남고,
	 * 그 기울기가 그대로 카메라 월드 회전 → 이동 방향이 되었다.
	 * 호스트는 매 프레임 다시 씌우고 있어서 증상이 가려져 있었다. (2026-09-11)
	 */
	FRotator ControlRotation = FRotator::ZeroRotator;

	/** 시점·입력을 가져간 상태인가. ReleaseControl 이 두 번 돌지 않게 한다 */
	bool bHasControl = false;

	/** 조종 시작 전 조종사가 보던 대상. 되돌릴 때 쓴다 */
	TWeakObjectPtr<AActor> PreviousViewTarget;

	/**
	 * 조종사가 보고 있는 방향. **조종사 머신의 로컬 값이다.**
	 *
	 * 매 틱 다시 씌운다 — 액터 회전은 복제되므로, 서버에서 한 발 늦게 도착한 값이
	 * 조종사의 화면을 흔드는 것을 막아야 한다. 조종사가 호스트면 복제 자체가 없어 무해하다.
	 */
	float DesiredYaw = 0.f;
	float DesiredPitch = 0.f;

	/** 조종을 인수할 때의 암전 타이머. 반납은 이 액터가 곧 사라질 수 있어 여기 걸지 않는다 */
	FTimerHandle ViewFadeTimer;

	/**
	 * 비행 중인가. 서버가 정하고 **복제된다** — 조종사가 클라이언트일 때 그쪽 화면이
	 * 시점을 가져갈 계기가 이것뿐이다. 서버에서는 틱을 돌릴지도 이 값으로 판단한다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_FlightActive)
	bool bFlightActive = false;
};
