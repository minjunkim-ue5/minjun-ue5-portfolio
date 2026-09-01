#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Loot/LootTypes.h"              // FLootStabilityData 를 값으로 보유 — 전방 선언 불가
#include "LootStabilityComponent.generated.h"

class ALootBase;
class APawn;
class UNiagaraComponent;
class UNiagaraSystem;

/**
 * 불안정형 노획물. 기울어지면 내용물이 새고 가치가 깎인다. (기획서 5장 — 기울기 60도 초과)
 *
 * [파손형과 다른 점] 파손형은 '사건'(충돌)이지만 불안정형은 '상태'(지금 기울어져 있음)다.
 *   충돌 콜백으로는 잡을 수 없어서 틱으로 본다. 서버에서만, 그리고 계산이 몇 줄뿐이라 싸다.
 *   결과도 다르다 — 파손형은 가치 0 까지 가고 액터가 사라지지만,
 *   불안정형은 MinValueRatio 가 바닥이고 물건은 끝까지 남는다. 손해는 보되 회수는 된다.
 *
 * [기울기를 두 곳에서 얻는다]
 *   놓여 있을 때 : 실제 물리 회전. 넘어지거나 굴러가면 샌다
 *   소지 중      : 운반자의 이동으로 시뮬레이션한다
 *
 *   소지 중에 물리 회전을 읽으면 안 된다. 물리가 꺼진 채 손 소켓에 붙어 있어서
 *   기울기가 '애니메이션이 손을 어디 두느냐'로 정해진다. 플레이어가 통제할 수 없다.
 *   대신 빠르게 오래 움직일수록 쌓이게 해서, 속도를 조절하며 옮기는 압박을 만든다.
 *
 * [경계] 운반자의 속도를 읽기만 하고 플레이어 파트는 건드리지 않는다.
 *   중량형처럼 이동 속도를 깎지 않는다 — 강제가 아니라 압박이다. 얼마나 빨리 갈지는
 *   플레이어가 정하고, 그 대가를 물건이 치른다.
 */
UCLASS(ClassGroup = (Loot), meta = (BlueprintSpawnableComponent))
class HEAVYHANDED_API ULootStabilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULootStabilityComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 지금까지 샌 횟수 */
	UFUNCTION(BlueprintPure, Category = "Loot|Stability")
	int32 GetSpillCount() const { return SpillCount; }

	/** 한 번이라도 샜는가 */
	UFUNCTION(BlueprintPure, Category = "Loot|Stability")
	bool IsSpilled() const { return SpillCount > 0; }

	/** 현재 기울기가 유출 한계에 얼마나 다가갔는가 (0~1). HUD 경고용 */
	UFUNCTION(BlueprintPure, Category = "Loot|Stability")
	float GetTilt01() const { return ReplicatedTilt01 / 255.f; }

	/**
	 * 지금 내용물이 새고 있는가. 모든 머신에서 같은 답이 나온다.
	 *
	 * IsSpilled() 와 다르다 — 저쪽은 "한 번이라도 샜는가"(누적), 이쪽은 "지금 새는 중인가"(상태).
	 */
	UFUNCTION(BlueprintPure, Category = "Loot|Stability")
	bool IsSpilling() const { return bIsSpilling; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 설계 수치. DT_LootStability 에서 노획물의 행 이름으로 찾아 채운다.
	 *
	 * [왜 컴포넌트가 직접 조회하나]
	 *   이 값을 읽는 것은 여기뿐이다. 예전처럼 ALootBase 가 들고 있으면 불안정형이 아닌
	 *   노획물까지 전부 9개 필드를 갖게 되고, 카탈로그 표에도 그 열이 생겨서
	 *   "이 물건도 기울면 새나?" 로 읽힌다.
	 *
	 * [BP 에서 고칠 수 있게 열어 두는 이유]
	 *   표에 행이 없으면 여기 적힌 값이 그대로 쓰인다. ALootBase 가 PhysicsData 에 대해
	 *   하는 것과 같은 폴백이고, 표에 안 올린 실험물을 만들 길이다.
	 *   행이 있으면 ResolveData 가 덮어쓰므로 여기 값은 무시된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability")
	FLootStabilityData Data;

	/**
	 * 새는 동안 계속 나오는 이펙트. 메시의 SpillSocketName 소켓에 붙는다.
	 *
	 * [왜 지속 효과인가]
	 *   유출은 사건이 아니라 상태다. 기울어져 있는 내내 SpillIntervalSeconds 마다
	 *   가치가 깎인다. 깎일 때마다 한 번씩 터뜨리면 플레이어는 "지금 새는 중" 이 아니라
	 *   "가끔 뭔가 나오네" 로 읽는다. 흐르는 그림이어야 세고 있다는 것이 전달된다.
	 *
	 * [BP 는 에셋만 고른다] 언제 켜고 끌지는 C++ 이 정한다. 파손형의 BreakEffect 와 같은 이유다.
	 *
	 * 비워 두면 아무 효과도 나오지 않는다 (경고하지 않는다 — 연출 없는 물건도 있을 수 있다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability|Visual")
	TObjectPtr<UNiagaraSystem> SpillEffect;

	/**
	 * 이펙트를 붙일 소켓. 내용물이 넘치는 자리 — 항아리라면 아가리다.
	 * 소켓이 없으면 메시 원점에 붙고 경고를 낸다. 조용히 원점에 붙으면
	 * "이펙트가 물건 한가운데서 나온다" 는 증상으로만 드러나 원인을 찾기 어렵다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability|Visual")
	FName SpillSocketName = TEXT("Spill");

	/**
	 * 소지 중 기울기가 바뀔 때 호출된다. (모든 머신)
	 * 흔들림·경고 아이콘·찰랑거리는 사운드는 BP 가 붙인다. 판정은 이미 C++ 에서 끝났다.
	 * @param Tilt01  0~1. 1 이면 지금 새는 중이다
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Stability")
	void OnCarriedTiltChanged(float Tilt01);

	/**
	 * 내용물이 샜을 때 호출된다. (모든 머신)
	 * 쏟아지는 파티클·사운드용. 실제 가치 차감은 서버가 이미 했다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Stability")
	void OnSpilled(int32 NewSpillCount, float TiltDegrees);

private:
	/**
	 * DT_LootStability 에서 자기 행을 찾아 Data 를 채운다. 한 번만 실제로 돈다.
	 *
	 * 클라이언트에서도 부른다. 표 조회는 모든 머신에서 같은 답이 나오는 순수 계산이라
	 * 수치를 복제할 이유가 없다 — 복제하는 것은 결과(기울기·유출 횟수)뿐이다.
	 */
	void ResolveData();

	/** 소지 중 기울기를 이동에서 만든다. 반환값은 현재 기울기(도) */
	float UpdateCarriedTilt(const APawn* Carrier, float DeltaTime);

	/** 놓인 상태에서 실제 물리 회전을 읽는다. 반환값은 현재 기울기(도) */
	float GetPhysicalTiltDegrees() const;

	/** 기울기를 받아 유출 여부를 판정한다. 두 모드가 공유한다 */
	void UpdateSpill(float TiltDegrees, float DeltaTime, bool bUseGrace);

	/** 가치를 깎고 연출을 방송한다 (서버 전용) */
	void Spill(float TiltDegrees);

	/**
	 * MinValueRatio 바닥까지 다 샜는가. 더 기울여도 잃을 것이 없는 상태다.
	 *
	 * 판정과 연출이 같은 답을 봐야 한다. Spill 만 바닥을 확인하고 이펙트는 계속 나오면,
	 * 플레이어는 아직 손해를 보는 줄 알고 급히 세우려 하지만 실제로는 아무 일도 없다.
	 */
	bool IsValueDrained() const;

	/** '지금 새는 중' 상태를 바꾼다. 값이 실제로 달라졌을 때만 일한다 (서버 전용) */
	void SetSpilling(bool bNewSpilling);

	/** bIsSpilling 에 맞춰 이펙트를 켜거나 끈다. 모든 머신에서 실행된다 */
	void ApplySpillEffect();

	UFUNCTION()
	void OnRep_IsSpilling();

	/** 소지 중 기울어진 모습을 실제로 보여준다. 안 보이면 플레이어가 배울 수 없다 */
	void ApplyCarriedLean(float TiltDegrees);

	/** 이동 방향을 따라가는 기울기 방향(월드, 수평). 관성 방향의 기준이 된다 */
	void UpdateTiltDirection(const APawn* Carrier, float DeltaTime);

	/** 서버가 계산한 기울기를 0~255 로 담아 보낸다 */
	void PushReplicatedTilt(float TiltDegrees);

	UFUNCTION()
	void OnRep_ReplicatedTilt01();

	UFUNCTION()
	void OnRep_SpillCount();

	/** 소유 노획물. UPROPERTY 가 없으면 GC 가 회수한 뒤 엉뚱한 곳에서 크래시한다 */
	UPROPERTY()
	TObjectPtr<ALootBase> OwnerLoot;

	/** ResolveData 가 이미 돌았는가. 표 조회를 매번 반복하지 않기 위한 것이다 */
	bool bDataResolved = false;

	/**
	 * 소지 중 누적된 기울기(도). 서버에서만 쓴다.
	 * 놓는 순간 실제 물리 회전으로 넘어가므로 그때 0 으로 돌린다.
	 */
	float CarriedTiltDegrees = 0.f;

	/** 지금 기울기가 한계를 넘긴 채 지난 시간(초). 그레이스 판정용 */
	float TiltedSeconds = 0.f;

	/**
	 * 관성이 작용하는 방향(월드, 수평 단위벡터). 대개 이동 방향과 같다.
	 * 물건 윗부분은 이 반대쪽으로 넘어간다.
	 * 멈춰 있는 동안에는 마지막 방향을 유지해야 기울기가 제자리에서 돌지 않는다.
	 */
	FVector TiltDirection = FVector::ZeroVector;

	/** 마지막으로 샌 시각(서버 기준). 초기값은 첫 유출이 즉시 나가도록 크게 잡는다 */
	float LastSpillTime = -BIG_NUMBER;

	/**
	 * 복제되는 기울기 비율(0~255).
	 *
	 * float 를 그대로 복제하면 걷는 내내 매 프레임 dirty 가 된다.
	 * 표시용이라 0.4%p 해상도면 충분하다. (소음 파트가 경계도에 쓰는 방식과 같다)
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedTilt01, VisibleInstanceOnly, Category = "Loot|Stability")
	uint8 ReplicatedTilt01 = 0;

	UPROPERTY(ReplicatedUsing = OnRep_SpillCount, VisibleInstanceOnly, Category = "Loot|Stability")
	int32 SpillCount = 0;

	/**
	 * 지금 새는 중인가. 판정은 서버가 하지만 이펙트는 모든 화면에 보여야 하므로 복제한다.
	 *
	 * 기울기(ReplicatedTilt01)로 클라이언트가 직접 계산하게 두지 않는다.
	 * 그레이스 판정이 서버의 누적 시간(TiltedSeconds)에 달려 있어서
	 * 클라이언트가 같은 답을 낼 수 없다 — 던진 물건이 공중에서 새는 것처럼 보이게 된다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsSpilling, VisibleInstanceOnly, Category = "Loot|Stability")
	bool bIsSpilling = false;

	/**
	 * 붙여 둔 이펙트 인스턴스. 껐다 켰다 하며 재사용한다.
	 * UPROPERTY 가 없으면 GC 가 회수한 뒤 다음 유출에서 크래시한다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SpillEffectComponent;

	/** 소켓이 없다는 경고를 이미 냈는가 */
	bool bWarnedMissingSpillSocket = false;
};
