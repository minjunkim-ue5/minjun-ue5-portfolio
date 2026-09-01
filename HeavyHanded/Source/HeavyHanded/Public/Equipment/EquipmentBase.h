#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"   // 부모 인터페이스 — 전방 선언 불가
#include "GameplayTagContainer.h"        // FGameplayTag 를 값으로 보유
#include "Interfaces/Carryable.h"        // 부모 인터페이스
#include "Shared/ThrowMath.h"            // FThrowParams 를 값으로 보유
#include "EquipmentBase.generated.h"

class UNiagaraSystem;
class UNoiseEmitterComponent;
class UStaticMeshComponent;
class USoundBase;

/**
 * 장비의 생애. 서버가 정하고 복제된다.
 *
 * Deployed 와 Active 를 나누는 이유는 점착 폭탄 때문이다. 던져서 붙은 뒤
 * 퓨즈가 도는 동안이 Deployed 이고, 폭발하는 순간이 Active 다.
 * 붙자마자 터지게 하면 이 둘이 하나로 보이지만, EMP·미끼는 Active 가
 * 15~20초씩 이어지므로 어느 쪽이든 두 상태가 다 필요하다.
 */
UENUM(BlueprintType)
enum class EEquipmentState : uint8
{
	/** 맵에 놓여 있음. 아직 아무도 안 집었다 */
	Idle       UMETA(DisplayName = "Idle"),

	/** 누군가 들고 있음 */
	Carried    UMETA(DisplayName = "Carried"),

	/** 날아가는 중 */
	InFlight   UMETA(DisplayName = "InFlight"),

	/** 어딘가 닿아 자리를 잡음. 발동 대기 */
	Deployed   UMETA(DisplayName = "Deployed"),

	/** 효과 발동 중 */
	Active     UMETA(DisplayName = "Active"),

	/** 다 쓴 상태. 되돌아가지 않는다 */
	Spent      UMETA(DisplayName = "Spent"),
};

/** 자리를 잡은 뒤 언제 발동하는가 */
UENUM(BlueprintType)
enum class EEquipmentActivation : uint8
{
	/** 닿는 즉시 */
	OnImpact   UMETA(DisplayName = "OnImpact"),

	/** ActivationDelay 초 뒤 (점착 폭탄의 퓨즈) */
	AfterDelay UMETA(DisplayName = "AfterDelay"),

	/** 코드가 ManualActivate 를 부를 때까지 기다린다 */
	Manual     UMETA(DisplayName = "Manual"),
};

/**
 * 던져 쓰는 장비의 베이스. (기획서 7장 — 은신처 구매 장비)
 *
 * [ALootBase 를 상속하지 않는다]
 *   상속하면 CurrentValue / BaseValue / DT_LootCatalog / Loot.Type 태그가 전부 따라온다.
 *   그러면 던져 둔 EMP 가 AVanZone 에 노획물로 실려 정산 화면에 뜬다.
 *   던지기 수식만 HHThrow 로 공유하고 액터는 형제로 둔다.
 *
 * [집기·던지기는 ICarryable 하나로 끝난다]
 *   GAB_Throw 와 ABaseCharacter 는 이미 Cast<ICarryable> 로만 동작한다.
 *   그래서 던지기·부착은 플레이어 파트 수정 없이 그대로 된다.
 *
 *   집기만 예외다 — UGAB_Interact::IsCarryableLoot 이 'Loot.Type' 태그를 보고 판정하기
 *   때문이다. 그래서 이 클래스는 IGameplayTagAssetInterface 로 자기 EquipmentTag 를
 *   내놓고, 저쪽에 'Equipment' 루트를 보는 판정을 하나 추가해 달라고 요청한다.
 *
 *   장비에 Loot.Type 태그를 다는 방법도 있지만 그러면 밴에 실린다. 태그를 나누는 것이
 *   곧 "이건 노획물이 아니다" 라는 선언이다.
 *
 * [신발·장갑·카트는 이 클래스를 상속하지 않는다]
 *   던지는 물건이 아니다. 상속하면 위 판정에 걸려 집을 수 있게 되고,
 *   손에 든 신발을 던지는 그림이 나온다.
 *
 * [효과는 서브클래스가 갖는다]
 *   여기 있는 것은 '던져서 자리를 잡고 발동해 소모된다' 는 뼈대뿐이다.
 *   카메라를 멈추는 것, 경비를 유인하는 것, 금고 문을 부수는 것은 각자의 몫이다.
 */
UCLASS(Abstract, Blueprintable)
class HEAVYHANDED_API AEquipmentBase : public AActor, public ICarryable, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	AEquipmentBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** IGameplayTagAssetInterface — 집기 판정이 'Equipment' 루트를 보고 고른다 */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	EEquipmentState GetEquipmentState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	FGameplayTag GetEquipmentTag() const { return EquipmentTag; }

	/** 다 써서 더는 쓸모가 없는가 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsSpent() const { return State == EEquipmentState::Spent; }

	/**
	 * ActivationMode 가 Manual 일 때 발동시킨다. (서버 전용)
	 * 지금 쓰는 장비는 없다 — 나중에 플레이어가 타이밍을 고르는 장비가 생길 때를 위한 것이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void ManualActivate();

	// ---- ICarryable ----
	virtual int32 GetRequiredCarriers() const override { return 1; }
	virtual float GetCarrySpeedMultiplier() const override { return 1.f; }
	virtual bool IsJumpAllowedWhileCarried() const override { return true; }
	virtual bool CanBeCarriedBy(const APawn* Requester) const override;
	virtual void OnGrabbed(APawn* Carrier) override;
	virtual void OnReleased(APawn* Carrier) override;
	virtual bool CanBeThrown() const override;
	virtual FVector ComputeThrowAimDirection() const override;
	virtual void OnThrown(APawn* Carrier, const FVector& AimDirection) override;
	virtual APawn* GetPrimaryCarrier() const override { return PrimaryCarrier; }
	virtual UPrimitiveComponent* GetPhysicsRoot() const override;

protected:
	virtual void BeginPlay() override;

	/**
	 * 자리를 잡았다. 붙었거나 바닥에 닿았다. (모든 머신)
	 * 서브클래스가 여기서 '준비' 를 한다 — 폭탄이라면 퓨즈 소리를 낸다.
	 */
	virtual void OnDeployed(const FHitResult& Hit);

	/**
	 * 효과가 발동했다. (모든 머신)
	 * 실제 효과는 **서버에서만** 적용할 것. 여기는 모든 머신에서 불린다.
	 */
	virtual void OnActivated();

	/** 효과가 끝났다. (모든 머신) */
	virtual void OnSpent();

	/** BP 확장점. C++ 가상 함수로 부족할 때만 쓴다 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Equipment", meta = (DisplayName = "On Deployed"))
	void BP_OnDeployed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Equipment", meta = (DisplayName = "On Activated"))
	void BP_OnActivated();

	UFUNCTION(BlueprintImplementableEvent, Category = "Equipment", meta = (DisplayName = "On Spent"))
	void BP_OnSpent();

	// ---- 정체성 ----

	/**
	 * 어떤 장비인가. Config/Tags/Equipment.ini 의 태그를 그대로 쓴다.
	 *
	 * URunProgressSubsystem 의 구매 목록이 같은 태그를 키로 쓰므로, 여기가 비어 있으면
	 * "산 물건과 스폰된 물건을 잇지 못하는" 상태가 된다. BeginPlay 에서 경고한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FGameplayTag EquipmentTag;

	// ---- 던지기 ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Throw")
	FThrowParams ThrowParams;

	// ---- 소지 ----

	/**
	 * 들었을 때 붙을 캐릭터 리그의 손 소켓.
	 *
	 * ALootBase::CarrySocketName 과 같은 값이어야 한다. 노획물과 장비가 다른 손에
	 * 들리면 애니메이션이 어긋난다. 상수로 박아 두면 리그가 바뀔 때 한쪽만 고치게 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Carry")
	FName CarrySocketName = TEXT("Hand_R_Socket");

	// ---- 자리잡기 · 발동 ----

	/**
	 * 닿은 것에 붙을 것인가. 점착 폭탄만 true 다.
	 *
	 * 붙지 않으면 그 자리에 굴러다닌다 — 미끼는 그래야 경비가 엉뚱한 곳으로 간다.
	 * 붙으면 물리를 끄고 맞은 컴포넌트에 어태치한다. 움직이는 것에 붙어도 따라간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Deploy")
	bool bAttachOnImpact = false;

	/**
	 * 자리를 잡는 데 필요한 최소 충격.
	 *
	 * 이 값이 없으면 손에서 놓자마자 바닥에 살짝 스친 것도 '착지' 로 쳐서
	 * 던지기 시작하자마자 발동한다. 노획물의 ImpactReportThreshold 와 같은 이유다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Deploy",
		meta = (ClampMin = "0.0"))
	float DeployImpulseThreshold = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Deploy")
	EEquipmentActivation ActivationMode = EEquipmentActivation::AfterDelay;

	/** ActivationMode 가 AfterDelay 일 때의 대기 시간. 점착 폭탄의 퓨즈다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Deploy",
		meta = (ClampMin = "0.0", Units = "s"))
	float ActivationDelay = 3.f;

	/**
	 * 효과가 이어지는 시간. 0 이면 발동 즉시 Spent 로 간다 (폭발처럼 순간적인 것).
	 * EMP 는 15, 미끼는 20 이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Deploy",
		meta = (ClampMin = "0.0", Units = "s"))
	float EffectDuration = 0.f;

	/**
	 * Spent 가 되고 이만큼 뒤에 액터를 지운다.
	 *
	 * 0 으로 두면 서버가 즉시 지워서 액터가 복제보다 먼저 사라진다. 그러면 클라이언트에서는
	 * 연출이 실행되지 않고 물건이 소리 없이 증발한다. (파손형의 BreakDestroyDelay 와 같다)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Deploy",
		meta = (ClampMin = "0.0", Units = "s"))
	float SpentDestroyDelay = 0.25f;

	// ---- 소음 ----

	/**
	 * 자리를 잡고 발동할 때까지 주기적으로 발행할 소음 태그.
	 * (Loot.ini — 대형 금고는 "폭파 진행 중 경계도 상승")
	 *
	 * 비워 두면 아무것도 발행하지 않는다. 소리를 낼 이유가 없는 장비가 더 많아서
	 * 그쪽이 기본이다 — EMP·미끼는 조용히 놓여 있어야 한다.
	 *
	 * [소음 파트와의 경계]
	 *   여기서 정하는 것은 "언제 무슨 태그로 소리가 나는가" 까지다. 그것이 몇 미터까지
	 *   들리고 경계도를 몇 % 올리는지는 DT_NoiseProfiles 가 정한다.
	 *
	 *   ⚠️ 폭파 진행용 태그가 아직 없다. Noise.Device.SafeCut 이 수치상 맞지만
	 *   (특대 지속 / +3%/초 / 전 구역) 이름이 '절단' 이라 지금 설계와 어긋난다.
	 *   소음 파트에 태그를 요청해 두었고, 정해지면 BP 에서 지정한다.
	 *
	 * [Active 구간에는 아직 같은 슬롯이 없다]
	 *   미끼는 발동한 뒤 20초 동안 시끄러워야 하므로(Noise.Equipment.Decoy) 이 훅으로는
	 *   안 된다 — 여기는 Deployed 구간, 즉 '자리는 잡았지만 아직 발동 전' 이다.
	 *   미끼를 만들 때 ActiveNoiseTag 를 대칭으로 붙인다. 지금 미리 파지 않는 것은
	 *   그 20초를 EffectDuration 으로 둘지가 아직 안 정해져서, 추측으로 만든 슬롯이
	 *   정작 그때 안 맞을 가능성이 높기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Noise")
	FGameplayTag DeployNoiseTag;

	/**
	 * DeployNoiseTag 를 몇 초마다 발행할지.
	 *
	 * 너무 짧게 잡아도 소음 파트의 쿨다운(FNoiseProfileRow::CooldownSeconds)에 걸려
	 * 그대로 나가지는 않는다. 다만 걸러지는 호출만 늘어나므로 프로파일 값과 맞춰 두는 편이 낫다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Noise",
		meta = (ClampMin = "0.05", Units = "s"))
	float DeployNoiseInterval = 1.f;

	// ---- 연출 (BP 는 에셋만 고른다) ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Visual")
	TObjectPtr<UNiagaraSystem> DeployEffect;

	/** 발동 중 계속 나오는 이펙트. 액터에 붙는다 — 효과가 끝나면 꺼진다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Visual")
	TObjectPtr<UNiagaraSystem> ActiveEffect;

	/** 다 썼을 때 한 번 터지는 이펙트. 폭발이 여기다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Visual")
	TObjectPtr<UNiagaraSystem> SpentEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Visual")
	TObjectPtr<USoundBase> DeploySound;

	/**
	 * 자리를 잡은 뒤 발동할 때까지 계속 나는 소리. 점착 폭탄의 경고음이다.
	 * (Equipment.ini — "부착 후 기폭까지 경고음 지속")
	 *
	 * DeploySound 와 짝이 아니라 별개다. 저쪽은 '철컥' 하고 붙는 한 번의 소리이고,
	 * 이쪽은 그 뒤로 이어지는 삑— 삑— 이다. 하나로 합치면 루프 여부를 에셋이 정하게 되어
	 * BP 에서 잘못 고른 것을 코드가 알 수 없다.
	 *
	 * **루프하는 사운드 에셋을 넣을 것.** 한 번 재생하고 끝나는 에셋을 넣으면
	 * 소리가 조용히 멈추고, 아무 경고도 뜨지 않는다.
	 *
	 * 액터에 붙여 재생하므로 폭탄이 움직이면 따라간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Visual")
	TObjectPtr<USoundBase> DeployLoopSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Visual")
	TObjectPtr<USoundBase> SpentSound;

	/** 물리 바디이자 루트. 플레이어 파트가 Attach 대상으로 쓴다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	TObjectPtr<UStaticMeshComponent> EquipmentMesh;

	/**
	 * 충돌을 소음으로 바꾼다. 소음 파트의 컴포넌트이고 여기서는 붙이기만 한다.
	 * ALootBase 와 같은 이유로 생성자에서 붙인다 — BP 마다 추가하는 방식이면 언젠가
	 * 빼먹고, 그 장비만 조용한데 경고도 없어서 발견이 아주 늦다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	TObjectPtr<UNoiseEmitterComponent> NoiseEmitter;

	/** 지금 상태를 바꾼다. 서버 전용이고, 값이 실제로 달라졌을 때만 일한다 */
	void SetEquipmentState(EEquipmentState NewState);

private:
	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** 붙이거나 멈춰 세우고 Deployed 로 넘어간다 (서버 전용) */
	void Deploy(const FHitResult& Hit);

	/** ActivationDelay 뒤 또는 즉시 (서버 전용) */
	void Activate();

	/** EffectDuration 뒤 또는 즉시 (서버 전용) */
	void Finish();

	void DestroySelf();

	/** DeployNoiseTag 를 한 번 발행한다. Deployed 인 동안 타이머가 반복해서 부른다 (서버 전용) */
	void EmitDeployNoise();

	/**
	 * 퓨즈 경고음을 끈다.
	 *
	 * Fadeout 으로 끈다 — Stop 은 파형을 그 자리에서 잘라서 '뚝' 하는 클릭음이 남고,
	 * 폭발음과 겹치면 그것만 유난히 튄다.
	 */
	void StopDeployLoop();

	/** 소지 상태에 맞춰 물리·콜리전·부착을 정리한다. 모든 머신에서 실행된다 */
	void ApplyCarryState();

	/** 상태에 맞는 연출을 켜고 끈다. 모든 머신에서 실행된다 */
	void ApplyStateEffects(EEquipmentState OldState);

	UFUNCTION()
	void OnRep_State(EEquipmentState OldState);

	UFUNCTION()
	void OnRep_PrimaryCarrier();

	UPROPERTY(ReplicatedUsing = OnRep_State, VisibleInstanceOnly, Category = "Equipment")
	EEquipmentState State = EEquipmentState::Idle;

	UPROPERTY(ReplicatedUsing = OnRep_PrimaryCarrier, VisibleInstanceOnly, Category = "Equipment")
	TObjectPtr<APawn> PrimaryCarrier;

	/** 붙어 있는 지속 이펙트. UPROPERTY 가 없으면 GC 가 회수한 뒤 끄려다 크래시한다 */
	UPROPERTY(Transient)
	TObjectPtr<class UNiagaraComponent> ActiveEffectComponent;

	/** 돌고 있는 퓨즈 경고음. ActiveEffectComponent 와 같은 이유로 UPROPERTY 다 */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> DeployLoopComponent;

	FTimerHandle ActivationTimer;
	FTimerHandle EffectTimer;
	FTimerHandle DestroyTimer;
	FTimerHandle DeployNoiseTimer;
};
