#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "InputActionValue.h"          // 추가
#include "InteractableInterface.h"
#include "Perception/AISightTargetInterface.h"
#include "PlayerCharacter.generated.h"


class UAbilitySystemComponent;
class UInteractionComponent;
class UBackpackComponent;
class UPlayerAttributeSet;
class UCameraComponent;
class UInputMappingContext; // 추가
class UInputAction; // 추가
class UGameplayAbility; // 클래스 전방 선언 목록에 추가
class UGameplayEffect; // ← 이 줄 추가
struct FInputActionInstance;
class UPlayerColorSet;
class UNoiseEmitterComponent;   // 추가


UCLASS()
class TEAMPROJDWEX54_API APlayerCharacter : public ACharacter, public IAbilitySystemInterface,
                                            public IInteractableInterface, public IAISightTargetInterface
{
	GENERATED_BODY()

public:
	APlayerCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// 서버에서 Pawn이 PlayerState에 빙의될 때 호출됨 (싱글/서버)
	virtual void PossessedBy(AController* NewController) override;

	// 클라이언트에서 PlayerState 복제가 완료됐을 때 호출됨 (멀티플레이 클라이언트용, 이것도 반드시 필요)
	virtual void OnRep_PlayerState() override;

	// Controller is replicated to clients after BeginPlay - bind input here instead.
	virtual void NotifyControllerChanged() override;

	// Enhanced Input을 실제로 바인딩하는 함수 (Character 기본 함수를 오버라이드)
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void Tick(float DeltaSeconds) override;

	// 점프 가능 여부 판단 (스태미나 검사 추가)
	virtual bool CanJumpInternal_Implementation() const override;

	// 점프가 실제로 발생한 순간 호출됨 (스태미나 차감)
	virtual void OnJumped_Implementation() override;

	// 착지 시 호출됨 (착지 소음/사운드 발생용)
	virtual void Landed(const FHitResult& Hit) override;

	/**
   * AI 시야 판정을 직접 구현한다.
   * 엔진 기본 동작은 캡슐 "정중앙" 한 점으로만 라인트레이스를 쏘기 때문에(AISense_Sight.cpp:571),
   * 몬스터보다 살짝 위/아래에 서면 바닥 모서리에 그 한 줄기가 걸려 상체가 다 보여도 미탐지가 된다.
   * 머리/가슴/중심/무릎 4점을 검사해 하나라도 보이면 보인 것으로 처리한다.
   */
	virtual UAISense_Sight::EVisibilityResult CanBeSeenFrom(
		const FCanBeSeenFromContext& Context,
		FVector& OutSeenLocation,
		int32& OutNumberOfLoSChecksPerformed,
		int32& OutNumberOfAsyncLosCheckRequested,
		float& OutSightStrength,
		int32* UserData = nullptr,
		const FOnPendingVisibilityQueryProcessedDelegate* Delegate = nullptr) override;

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	// ===== 피격 창구 (몬스터/함정 파트가 호출할 함수) =====
	// BlueprintCallable이라 다른 팀원이 BP에서도 호출 가능
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ReceivePlayerDamage(float DamageAmount, AActor* DamageCauser);

	// 몬스터·함정 등 외부 가해자의 표준 데미지 진입점 → 기존 GAS 경로로 연결
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	// 사망 여부 확인용 (몬스터 AI가 "이미 죽은 플레이어는 공격 안 함" 판단할 때 사용)
	UFUNCTION(BlueprintCallable, Category = "Health")
	bool IsDead() const { return bIsDead; }

	// 테스트용 치트: 콘솔에서 Cheat_Damage 30 이런 식으로 호출 가능
	UFUNCTION(Exec)
	void Cheat_Damage(float Amount);

	// 클라이언트 → 서버로 치트 요청을 전달하는 RPC
	UFUNCTION(Server, Reliable)
	void Server_CheatDamage(float Amount);

	// 다운 상태인지 확인 (몬스터 AI가 "다운된 애는 공격 안 함" 판단용으로도 사용 가능)
	UFUNCTION(BlueprintCallable, Category = "Health")
	bool IsDowned() const { return bIsDowned; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// IInteractableInterface 구현 - 다운된 상태에서 팀원이 E를 누르면 호출됨
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	// 상호작용 홀드 취소를 서버에 전달
	UFUNCTION(Server, Reliable)
	void Server_CancelInteract();

	void InteractReleased(const FInputActionValue& Value);

	void SetReviving(bool bValue) { bIsReviving = bValue; }

	// 부활 시전 중 출혈 타이머 일시정지 / 재개
	void PauseBleedOut();
	void ResumeBleedOut();

	//셀프 지연 데미지 치트 
	UFUNCTION(Exec)
	void Cheat_DamageDelayed(float Amount, float Delay);

	// 서버에서 색을 설정 (인게임 진입 시 PlayerState/GameInstance에서 읽어 호출)
	UFUNCTION(BlueprintCallable, Category = "Customization")
	void SetColorIndex(int32 NewIndex);
	
	/** 저장된 FOV 를 카메라에 반영. 설정 팝업에서 FOV 를 바꿀 때도 호출한다 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyViewSettings();
	
protected:
	virtual void BeginPlay() override;

	// 1인칭 카메라
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	// 소음 발생 컴포넌트 (착지, 이동 등 DT 기반 소음)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Noise")
	TObjectPtr<UNoiseEmitterComponent> NoiseEmitter;

	// PlayerState에서 가져온 ASC를 여기 캐싱해둠 (매번 PlayerState 거쳐서 찾으면 느리니까)
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backpack")
	TObjectPtr<UBackpackComponent> Backpack;

	UPROPERTY()
	TObjectPtr<UPlayerAttributeSet> AttributeSet;

	// ASC와 AttributeSet을 캐싱하고 초기 설정을 하는 공용 함수
	void InitAbilityActorInfo();

	// ===== Enhanced Input 관련 =====

	// 이 캐릭터가 사용할 매핑 테이블. BP에서 값을 넣어줄 거라 EditAnywhere
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	// 커서 토글 (호스트 전용) - BP에서 IA_CursorToggle 지정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CursorToggleAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> DropAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> UseAction;

	// Reload (R key): assign IA_Reload in BP
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TArray<TObjectPtr<UInputAction>> BackpackSlotActions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (ClampMin = "0.1"))
	float SlotHoldThreshold = 1.f;

	bool bCursorMode = false;

	// 항상 켜져있는 스태미나 회복 이펙트 클래스
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TSubclassOf<UGameplayEffect> StaminaRegenEffectClass;

	// 사망 시 전환할 관전 폰 클래스. BP에서 BP_PlayerSpectator 연결 예정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death")
	TSubclassOf<APawn> SpectatorPawnClass;

	// ===== GAS 어빌리티 =====

	// 이 캐릭터가 처음부터 갖고 시작할 어빌리티 클래스. BP에서 BP_GA_Sprint 연결 예정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TSubclassOf<UGameplayAbility> SprintAbilityClass;

	// 서버에서 딱 한 번만 어빌리티를 부여하기 위한 플래그
	bool bAbilitiesGranted = false;

	// 어빌리티 부여 함수
	void GrantAbilities();

	// 커서 모드 토글: 마우스 표시 + 카메라 회전 잠금 on/off
	void ToggleCursorMode(const FInputActionValue& Value);

	// 실제 입력 처리 함수들
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint(const FInputActionValue& Value);
	void StopSprint(const FInputActionValue& Value);
	
	/** 스프린트 실제 취소 로직. 홀드/토글 양쪽과 앉기에서 공용으로 쓴다 */
	void CancelSprint();
	
	// MoveSpeed 속성이 변경될 때마다 호출될 콜백 함수
	void OnMoveSpeedChanged(const struct FOnAttributeChangeData& Data);

	void StartCrouch(const FInputActionValue& Value);
	void StopCrouch(const FInputActionValue& Value);

	void OnDrop(const FInputActionValue& Value);
	void OnUse(const FInputActionValue& Value);
	void OnUseReleased(const FInputActionValue& Value);
	void OnReload(const FInputActionValue& Value);

	void OnSlotStarted(const FInputActionInstance& Instance);
	void OnSlotComplete(const FInputActionInstance& Instance);
	void OnSlotHoldReached();

	FTimerHandle SlotHoldTimer;

	int32 PendingSlot = INDEX_NONE;

	// 현재 State.Moving 태그가 붙어있는지 추적 (중복 추가/제거 방지용)
	bool bIsMovingTagSet = false;
	bool bIsDrainingTagSet = false;

	// 점프 시 소모할 스태미나 이펙트. BP에서 GE_JumpCost 연결 예정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TSubclassOf<UGameplayEffect> JumpCostEffectClass;

	// 이 수치보다 스태미나가 적으면 점프 불가
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	float MinStaminaToJump = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TSubclassOf<UGameplayAbility> InteractAbilityClass;

	void Interact(const FInputActionValue& Value);


	// ===== 체력/사망 =====

	// 데미지 이펙트 클래스. BP에서 GE_Damage 연결 예정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	// 팀킬 허용 여부. 곡괭이 날 트레이스가 옆 사람을 자주 긁어서 기본 false
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	bool bAllowFriendlyFire = false;

	UPROPERTY(ReplicatedUsing = OnRep_IsDead)
	bool bIsDead = false;

	UFUNCTION()
	void OnRep_IsDead();

	// Health 속성이 변할 때마다 호출될 콜백
	void OnHealthChanged(const struct FOnAttributeChangeData& Data);

	// 사망 처리 (조작 불능 + 래그돌)
	void HandleDeath();

	void ApplyDeathEffects();

	// ===== 피격 화면 효과 =====

	// 피격 효과가 지속되는 시간 (초)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeedback")
	float HitFeedbackDuration = 0.5f;

	// 피격 순간 비네트(가장자리 어두워짐+붉어짐) 최대 강도
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeedback")
	float HitVignetteIntensity = 1.2f;

	// 남은 효과 시간 (0이면 효과 없음)
	float HitFeedbackTimeRemaining = 0.f;

	// 피격 효과 시작 (클라이언트 화면 연출)
	void PlayHitFeedback();

	// ===== 사망 연출 =====

	// 사망 연출 시간 (관전 전환까지의 딜레이)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death")
	float DeathSequenceDuration = 1.5f;

	// 사망 시 비네트 강도 (피격보다 약하게 - 시야는 확보되어야 쓰러지는 게 보임)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death")
	float DeathVignetteIntensity = 0.4f;

	// 연출이 끝난 뒤 실제 관전 전환을 담당
	void FinishDeathSequence();

	FTimerHandle DeathTimerHandle;

	// 사망 시 카메라를 부착할 머리 본 이름 (에셋의 실제 본 이름으로 BP에서 설정)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death")
	FName HeadBoneName = FName("head");

	// 사망 카메라: 머리 본 기준 위치 오프셋 (에디터에서 실시간 조정용)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death")
	FVector DeathCameraOffset = FVector(10.f, 0.f, 8.f);

	// 사망 카메라: 머리 본 기준 회전 오프셋
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death")
	FRotator DeathCameraRotation = FRotator::ZeroRotator;

	// 1인칭에서 숨길 머리 본 이름 (이 본과 그 자식들이 내 화면에서 안 보임)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FName FirstPersonHideBoneName = FName("head");


	// ===== 다운/부활 시스템 =====

	// 다운 상태에서 사망까지의 제한시간 (초)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	float DownedBleedOutTime = 60.f;

	// 다운 상태 여부 (복제됨 - 다른 클라이언트도 알아야 부활 UI 등 처리 가능)
	UPROPERTY(ReplicatedUsing = OnRep_IsDowned)
	bool bIsDowned = false;

	// 다운 상태 카메라 위치 (캡슐 기준). 다운 애니메이션의 머리 위치에 맞춰 조정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	FVector DownedCameraOffset = FVector(0.f, 0.f, 0.f);

	// 다운 상태 동안 화면에 깔리는 붉은 효과 강도 (0.2~0.4 권장)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	float DownedVignetteIntensity = 0.7f;

	// 부활 시 회복시킬 체력량
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	float ReviveHealthAmount = 50.f;

	// 부활 이펙트 클래스
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	TSubclassOf<UGameplayEffect> ReviveEffectClass;

	// 부활시키는 중 (살리는 사람이 몸 숙이는 애니메이션용)
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Downed")
	bool bIsReviving = false;

	//원래 캡슐/카메라 값 (다운 해제 시 복원용)
	float DefaultCapsuleHalfHeight = 88.f;
	FVector DefaultCameraLocation;

	//서버: 실제 부활 철;
	void Revive();

	//다운 해제 시 원상 복구(모든 머신에서 실행)
	void RestoreFromDowned();

	UFUNCTION()
	void OnRep_IsDowned();

	// 다운 상태 진입 (서버)
	void EnterDownedState();

	// 다운 연출 (모든 머신에서 실행)
	void ApplyDownedEffects();

	// 제한시간 만료 → 진짜 사망
	void BleedOut();

	FTimerHandle BleedOutTimerHandle;

	// 웅크릴 때 카메라 오프셋 (앞으로/위로 빼서 자기 몸 안 가리게)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector CrouchCameraOffset = FVector(30.f, 0.f, 0.f);

	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;


	// 색 세트 데이터 (BP에서 DA_PlayerColors 지정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customization")
	TObjectPtr<UPlayerColorSet> ColorSet;

	// 선택된 색 인덱스 (복제 - 모든 클라이언트가 같은 색을 봐야 함)
	UPROPERTY(ReplicatedUsing = OnRep_ColorIndex, BlueprintReadOnly, Category = "Customization")
	int32 ColorIndex = 0;

	// 각 부위 머티리얼이 메시의 몇 번 슬롯인지 (메시마다 다르니 BP에서 조정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customization")
	int32 SuitMaterialSlot = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customization")
	int32 HelmetMaterialSlot = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customization")
	int32 BackpackMaterialSlot = 2;

	UFUNCTION()
	void OnRep_ColorIndex();

	void ApplyColor();

	UFUNCTION(Exec)
	void Cheat_SetColor(int32 Index);

	UFUNCTION(Server, Reliable)
	void Server_SetColor(int32 Index);


	//피격 방향별 몽타주 (0=앞, 1=뒤, 2=좌, 3=우)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact")
	TArray<TObjectPtr<UAnimMontage>> HitReactMontages;

	UFUNCTION (NetMulticast, Unreliable)
	void Multicast_PlayHitReact(uint8 DirectionIndex);

	// 피격 방향 계산 (0=앞, 1=뒤, 2=좌, 3=우)
	uint8 CalculateHitDirection(const FVector& AttackerLocation) const;
	
};
