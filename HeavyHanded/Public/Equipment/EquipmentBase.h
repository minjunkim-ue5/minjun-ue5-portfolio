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
	 * 자리를 잡았다. 붙었거나 바닥에 닿았다. **(서버 전용)**
	 * 서브클래스가 여기서 '준비' 를 한다 — 폭탄이라면 퓨즈 소리를 낸다.
	 */
	virtual void OnDeployed(const FHitResult& Hit);

	/**
	 * 효과가 발동했다. **(서버 전용)**
	 *
	 * ⚠ 아래 세 훅은 Deploy / Activate / Finish 에서만 불리고 그 셋은 전부 서버 전용이다.
	 * 클라이언트의 OnRep_State 는 ApplyStateEffects(연출)만 실행한다 —
	 * 2026-09-11 까지 이 주석이 "(모든 머신)" 이라고 잘못 적혀 있었다.
	 *
	 * 그래서 **클라이언트에서 무언가를 해야 하는 장비는 자기 상태를 따로 복제해야 한다.**
	 * ADrone 이 bFlightActive 를 복제해 조종사의 화면에서 시점을 가져가는 것이 그 예다.
	 */
	virtual void OnActivated();

	/** 효과가 끝났다. **(서버 전용 — OnActivated 주석을 볼 것)** */
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

	/**
	 * 놓거나 던진 뒤 이 시간(초) 동안 사람과의 물리 충돌을 무시한다. 0 이면 끈다.
	 *
	 * 소지 중에는 장비가 운반자 몸 안에 있다. 놓는 순간 물리를 켜면 그 침투를 물리 엔진이
	 * 밀어내면서 던진 것이 손에서 튕기고 궤도가 어긋난다. (ALootBase 쪽과 같은 문제다)
	 *
	 * [장비에는 이유가 하나 더 있다]
	 *   bAttachOnImpact 인 장비는 가장 먼저 닿는 것에 붙는다. 이게 없으면 던진 사람
	 *   몸에 붙어서, 자기 발밑에서 퓨즈가 도는 그림이 나온다.
	 *
	 * 경비(Guard 채널)는 계속 Block 이라 던져 맞히거나 붙이는 것은 그대로 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Carry",
		meta = (ClampMin = "0.0", ClampMax = "2.0", Units = "s"))
	float ReleaseIgnorePawnSeconds = 0.3f;

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
	 *   지성인 님께 태그를 요청해 두었고, 정해지면 BP 에서 지정한다.
	 *
	 * [Active 구간은 ActiveNoiseTag 가 맡는다]
	 *   여기는 Deployed, 즉 '자리는 잡았지만 아직 발동 전' 이다.
	 *   발동한 뒤에 나는 소리는 아래 ActiveNoiseTag 로 간다. (2026-09-08 미끼 작업에서 추가)
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

	/**
	 * 발동한 뒤(Active) 주기적으로 발행할 소음 태그. DeployNoiseTag 와 대칭이다.
	 *
	 * 미끼가 이것으로 동작한다 — 20초(EffectDuration) 동안 Noise.Equipment.Decoy 를 계속 내면,
	 * 소음이 UAISense_Hearing 을 거쳐 경비의 InvestigateLocation 이 되어 경비가 그리로 온다.
	 * "이리 와라" 같은 AI 호출 경로는 만들지 않는다 — 소음 시스템을 우회하는 두 번째 길이 생긴다.
	 *
	 * 비워 두면 아무것도 발행하지 않는다. 점착 폭탄처럼 EffectDuration 이 0 인 장비는
	 * Active 구간 자체가 한 프레임도 없으므로 지정해도 의미가 없다.
	 *
	 * [경계도 억제를 여기서 하지 않는다]
	 *   "경비는 부르되 경계도는 덜 올린다" 를 AlertScale 로 넘기고 싶어지지만 그러면 안 된다.
	 *   AlertScale 은 소음 파트의 스팸 필터가 계산해 넘기는 값이고(NoiseSpamFilter.cpp),
	 *   같은 자리에서 반복되는 소음의 기여를 자동으로 깎는 것이 그쪽 일이다.
	 *   미끼의 20초 반복이 정확히 그 상황이라 손대면 오히려 망가진다.
	 *   경계도를 몇 % 올릴지는 DT_NoiseProfiles 의 AlertDelta 가 정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Noise")
	FGameplayTag ActiveNoiseTag;

	/**
	 * ActiveNoiseTag 를 몇 초마다 발행할지. 짧을수록 경비를 강하게 끈다.
	 *
	 * 미끼의 유인력을 조절하는 값이 이것이다. 너무 짧게 잡아도 소음 파트의 쿨다운
	 * (FNoiseProfileRow::CooldownSeconds)에 걸려 그대로 나가지는 않고, 걸러지는 호출만 늘어난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Noise",
		meta = (ClampMin = "0.05", Units = "s"))
	float ActiveNoiseInterval = 1.f;

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

	/**
	 * 효과를 예정보다 일찍 끝낸다. EffectDuration 타이머를 지우고 Spent 로 넘어간다. (서버 전용)
	 *
	 * 드론이 이걸 쓴다 — 준비 시간이 끝나면 남은 시간과 무관하게 조종을 끊어야 한다.
	 * Active 가 아니면 아무 일도 하지 않는다.
	 */
	void FinishEffectEarly();

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

	/** ActiveNoiseTag 를 한 번 발행한다. Active 인 동안 타이머가 반복해서 부른다 (서버 전용) */
	void EmitActiveNoise();

	/**
	 * 퓨즈 경고음을 끈다.
	 *
	 * Fadeout 으로 끈다 — Stop 은 파형을 그 자리에서 잘라서 '뚝' 하는 클릭음이 남고,
	 * 폭발음과 겹치면 그것만 유난히 튄다.
	 */
	void StopDeployLoop();

	/** 소지 상태에 맞춰 물리·콜리전·부착을 정리한다. 모든 머신에서 실행된다 */
	void ApplyCarryState();

	/**
	 * 놓기·던지기 직후의 사람 통과 구간을 끝낸다.
	 *
	 * 구간이 끝나기 전에 다시 집혔거나 이미 어딘가에 붙었으면 아무것도 하지 않는다 —
	 * 그때 되돌리면 그 상태의 콜리전을 덮어쓴다.
	 */
	void EndReleaseIgnorePawn();

	/** 상태에 맞는 연출을 켜고 끈다. 모든 머신에서 실행된다 */
	void ApplyStateEffects(EEquipmentState OldState);

	/**
	 * 끝나는 순간의 연출(SpentEffect · SpentSound)을 전원에게 내려보낸다. 위치는 서버 값이다.
	 *
	 * [왜 복제 상태에 맡기지 않는가] — 2026-10-02 실측으로 바꿨다
	 *   원래는 State 가 Spent 로 복제되면 각 머신이 ApplyStateEffects 에서 띄웠다. 그런데
	 *   이 장비는 Spent 가 되고 SpentDestroyDelay 뒤에 자기를 파괴한다 — 그 사이에 상태가
	 *   못 나가면 클라이언트는 연출을 영영 못 본다. 실제로 복제가 1.8초씩 밀리던 상황에서
	 *   **호스트가 던지면 보이고 클라이언트가 던지면 안 보이는** 증상으로 나타났다.
	 *
	 *   위치를 인자로 싣는 이유도 같은 측정에서 나왔다. 클라이언트의 액터 위치가 서버보다
	 *   4.75m 뒤처져 있어서, 각자 자기 위치에 띄우면 **엉뚱한 자리에서 터진다.**
	 *
	 *   Reliable 인 것은 장비 하나당 한 번만 나가는 이벤트이기 때문이다. 놓치면 플레이어가
	 *   $10,000 짜리 물건이 작동했는지 알 방법이 없다 — 연출이지만 버려도 되는 연출은 아니다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SpentEffect(FVector_NetQuantize Location);

	/**
	 * 방금 스폰한 나이아가라에 값을 넘긴다. 서브클래스가 덮어쓴다.
	 * **모든 머신에서 불린다** — ApplyStateEffects 안이라 서버 전용이 아니다.
	 *
	 * [왜 필요한가]
	 *   이펙트의 크기가 게임 수치와 같아야 하는 장비가 있다. EMP 의 펄스 구가 그렇다 —
	 *   나이아가라에 반경을 숫자로 박아 두면, 나중에 PulseRadius 를 바꿨을 때
	 *   **보이는 구와 실제로 꺼지는 범위가 조용히 어긋난다.** 그때 "분명 닿았는데 안 꺼진다"
	 *   로 나타나고, 원인을 찾기 어렵다.
	 *   ASecurityCamera 가 VisionCone 을 DetectionRange 로 강제 동기화하는 것과 같은 이유다.
	 *
	 *   값은 나이아가라의 User 파라미터로 넘긴다 (Effect->SetFloatParameter("Radius", ...)).
	 *   복제하지 않아도 되는 것은 이 값들이 BP 기본값이라 모든 머신이 같은 값을 갖기 때문이다.
	 */
	virtual void ConfigureEffect(class UNiagaraComponent* Effect, EEquipmentState ForState) {}

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

	/**
	 * 직전에 반영해 둔 운반자. '방금 놓였다' 를 구분하는 데만 쓴다.
	 * PrimaryCarrier 가 비었는지만 보면 맵에 처음부터 놓여 있던 장비까지 걸린다.
	 */
	TWeakObjectPtr<APawn> AppliedCarrier;

	FTimerHandle ActivationTimer;
	FTimerHandle EffectTimer;
	FTimerHandle DestroyTimer;
	FTimerHandle DeployNoiseTimer;
	FTimerHandle ActiveNoiseTimer;

	/** 사람 통과 구간의 타이머. 액터가 먼저 사라지면 타이머 매니저가 스스로 정리한다 */
	FTimerHandle ReleaseIgnorePawnTimer;
};
