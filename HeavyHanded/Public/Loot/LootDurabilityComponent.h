#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Loot/LootTypes.h"              // FLootDurabilityData 를 값으로 보유 — 전방 선언 불가
#include "LootDurabilityComponent.generated.h"

class ALootBase;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class USoundBase;
struct FLootImpactEvent;

/**
 * 파손형 노획물의 내구도. 충격이 누적되면 깨지면서 사라진다. (기획서: 충격 3회)
 *
 * [경계] 이 컴포넌트는 raw OnHit 을 직접 듣지 않는다.
 *   ALootBase 가 게이팅해 만든 '확정 충격'만 구독한다.
 *   물리 낙하 1회에 OnHit 은 5~15회 발생하므로, 직접 세면 한 번 떨어뜨렸을 때
 *   즉사한다. 기획서의 '3회'는 의미 있는 충격 3번이지 콜백 3개가 아니다.
 *
 * [임계값이 2개인 이유]
 *   ImpactReportThreshold(200)   = 소음 파트에 알릴 최소 충격 — ALootBase 의 FLootPhysicsData
 *   DamageImpulseThreshold(3000) = 파손으로 칠 최소 충격 — 여기 Data
 *   살짝 부딪히는 소리는 나야 하지만 그게 파손까지 되면 안 된다.
 *
 *   실측(10kg): 150cm 낙하는 착지 6497 + 튕김 581 로 잡힌다.
 *   둘 다 방송되어 '쿵' 다음 '탁' 소리가 나지만, 파손은 착지 하나만 센다.
 *
 *   앞의 것이 액터에 있는 이유는 모든 노획물이 소리를 내기 때문이고,
 *   뒤의 것이 여기 있는 이유는 파손형만 깨지기 때문이다.
 */
UCLASS(ClassGroup = (Loot), meta = (BlueprintSpawnableComponent))
class HEAVYHANDED_API ULootDurabilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULootDurabilityComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 파괴되었는가. 파괴 직후 액터가 사라지므로 참조하는 쪽은 IsValid 를 먼저 봐야 한다 */
	UFUNCTION(BlueprintPure, Category = "Loot|Durability")
	bool IsBroken() const { return bIsBroken; }

	/** 지금까지 누적된 충격 횟수 */
	UFUNCTION(BlueprintPure, Category = "Loot|Durability")
	int32 GetImpactCount() const { return ImpactCount; }

	/**
	 * 파손 진행도 (0~1). **1 은 '깨진 상태'가 아니라 '다음 충격에 깨진다'** 는 뜻이다.
	 * 균열 머티리얼과 HUD 가 같은 값을 봐야 하므로 여기 하나로 둔다.
	 *
	 * 3회짜리 물건이면 1회 = 0.5, 2회 = 1.0 이다. 마지막 충격에서는 같은 프레임에
	 * 메시가 숨겨지므로 그 값은 화면에 나오지 않는다 — 이유는 .cpp 에 적어 두었다.
	 */
	UFUNCTION(BlueprintPure, Category = "Loot|Durability")
	float GetDamageRatio01() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 설계 수치. DT_LootDurability 에서 노획물의 행 이름으로 찾아 채운다.
	 *
	 * [왜 컴포넌트가 직접 조회하나]
	 *   이 값을 읽는 것은 여기뿐이다. 예전에는 FLootPhysicsData 에 섞여 있어서
	 *   파손형이 아닌 노획물까지 전부 들고 다녔다. 무게·던지기 값 사이에 끼어 있어
	 *   눈에 덜 띄었을 뿐 불안정형과 같은 문제였다.
	 *
	 * [BP 에서 고칠 수 있게 열어 두는 이유]
	 *   표에 행이 없으면 여기 적힌 값이 그대로 쓰인다. 표에 안 올린 실험물용 폴백이다.
	 *   행이 있으면 ResolveData 가 덮어쓰므로 여기 값은 무시된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability")
	FLootDurabilityData Data;

	/**
	 * 파괴 연출이 클라이언트에 도착할 때까지 액터를 남겨 두는 시간(초).
	 *
	 * 0 으로 두면 서버가 즉시 Destroy 해서 액터가 복제보다 먼저 사라진다.
	 * 그러면 클라이언트에서는 RepNotify 도 BP 연출도 실행되지 않고
	 * 물건이 소리 없이 증발한다. 지연 시간 동안에는 이미 숨겨져 있으므로
	 * 플레이어 눈에는 파괴 즉시 사라진 것으로 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability",
		meta = (ClampMin = "0.0"))
	float BreakDestroyDelay = 0.25f;

	/**
	 * 사람 몸(폰)에 닿은 충격은 파손으로 세지 않는다.
	 *
	 * [원칙] 물건이 상하는 건 바닥·벽·다른 물건에 부딪혔을 때다.
	 *   사람이 밀치면 물건이 넘어지고, 넘어져서 바닥에 부딪히며 상한다.
	 *   사람 몸에 스친 것 자체로 상하지는 않는다.
	 *
	 * 기술적으로도 이래야 한다. 캐릭터 캡슐은 키네마틱이라 물리 엔진이 질량을
	 * 무한대처럼 다룬다. 걸어가다 살짝 닿기만 해도 임펄스가 바닥 낙하의 3~4배로 튄다.
	 * (실측: 놓은 상자를 걸어서 밀었을 때 19010 — 3m 낙하가 9622인데도 그렇다)
	 * 이 값을 거리로 막을 수는 없다. 플레이어는 언제든 물건 쪽으로 걸어갈 수 있다.
	 *
	 * 걸러내는 것은 파손 판정뿐이다. 충격 이벤트 자체는 그대로 방송되므로
	 * 소음 파트는 사람이 물건을 밀친 사실을 여전히 받는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability")
	bool bIgnorePawnImpacts = true;

	/**
	 * 균열 머티리얼의 스칼라 파라미터 이름. 충격이 쌓일 때마다 0~1 로 설정된다.
	 *
	 * [왜 BP 그래프가 아니라 C++ 인가]
	 *   파손형 메시가 6종(SM_maartifact1~5, SM_mavaseempty)이라 BP 에 짜면 같은 그래프가
	 *   6벌 생긴다. BP 는 diff 도 병합도 안 되므로 나중에 한 벌만 고쳐놓고 잊으면
	 *   그 물건만 균열이 안 도는데 찾을 방법이 없다.
	 *
	 *   BP 가 지정하는 것은 머티리얼과 이 이름뿐이고, 언제 얼마로 갈지는 C++ 이 정한다.
	 *
	 * None 으로 두면 균열 연출을 하지 않는다 (동적 인스턴스도 만들지 않는다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability|Visual")
	FName CrackParameterName = TEXT("CrackAmount");

	/**
	 * 충격이 하나 쌓였을 때 호출된다. 금 가는 단계 연출용.
	 * BP 는 여기서 사운드·파티클만 붙인다 — 머티리얼 균열은 C++ 이 이미 적용했다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Durability")
	void OnDamageAccumulated(int32 NewImpactCount, int32 MaxCount);

	/**
	 * 깨질 때 뿌릴 파편. 노획물 위치에 스폰된다.
	 *
	 * [BP 는 에셋만 고른다] 언제·어디에 스폰할지는 C++ 이 정한다.
	 *   BP 그래프에 스폰 로직을 두면 노획물마다 타이밍과 위치가 달라질 수 있고,
	 *   uasset 이라 리뷰도 병합도 안 된다. 나중에 이 값을 DT_LootDurability 의
	 *   열로 옮기고 싶어질 때도, 속성이면 옮길 수 있지만 BP 그래프는 못 옮긴다.
	 *
	 * 비워 두면 파편이 나오지 않는다 (경고도 내지 않는다 — 파편 없는 물건도 있을 수 있다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability|Visual")
	TObjectPtr<UNiagaraSystem> BreakEffect;

	/** 파편의 크기 배율. 큰 물건은 파편도 크게 튀어야 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability|Visual",
		meta = (ClampMin = "0.1"))
	float BreakEffectScale = 1.f;

	/** 깨지는 소리. 소음 시스템과 무관한 순수 연출이다 — 경계도는 ReportImpact 가 이미 알렸다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability|Visual")
	TObjectPtr<USoundBase> BreakSound;

	/**
	 * 파괴됐을 때 호출된다. 위 파편·사운드로 부족할 때만 쓴다.
	 * 이 시점에 노획물 메시는 이미 숨겨져 있고, BreakEffect / BreakSound 는 이미 나갔다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Durability")
	void OnBroken();

private:
	/**
	 * DT_LootDurability 에서 자기 행을 찾아 Data 를 채운다. 한 번만 실제로 돈다.
	 *
	 * BeginPlay 뿐 아니라 OnRep_ImpactCount 앞에서도 부른다. 초기 복제는 BeginPlay 보다
	 * 먼저 도착할 수 있는데, 그때 MaxImpactCount 가 기본값이면 클라이언트의 금 간 연출이
	 * "2/3" 처럼 어긋난 단계로 뜬다.
	 */
	void ResolveData();

	/** ALootBase 의 확정 충격 구독 핸들러 (서버에서만 불린다) */
	void HandleLootImpact(const FLootImpactEvent& Event);

	/** 파괴 처리 (서버 전용) */
	void Break(const FLootImpactEvent& CausingEvent);

	/** 숨김·충돌 차단·BP 연출. 모든 머신에서 실행된다 */
	void ApplyBrokenState();

	/**
	 * 균열 정도를 머티리얼에 반영한다. 모든 머신에서 실행된다.
	 *
	 * 동적 인스턴스는 첫 충격 때 한 번만 만든다. 생성자나 BeginPlay 에서 만들면
	 * 맵에 깔린 모든 파손형이 한 번도 안 부딪혀도 슬롯 수만큼 MID 를 들고 있게 된다.
	 */
	void ApplyCrackVisual();

	/** 파편·소리를 낸다. 모든 머신에서 실행되고, 데디케이티드 서버에서만 건너뛴다 */
	void PlayBreakEffects();

	/** BreakDestroyDelay 뒤에 불린다 (서버 전용) */
	void DestroyOwnerLoot();

	UFUNCTION()
	void OnRep_ImpactCount();

	UFUNCTION()
	void OnRep_IsBroken();

	/** 누적 충격 횟수. 클라이언트는 이 값으로 금 간 연출 단계를 맞춘다 */
	UPROPERTY(ReplicatedUsing = OnRep_ImpactCount, VisibleInstanceOnly, Category = "Loot|Durability")
	int32 ImpactCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_IsBroken, VisibleInstanceOnly, Category = "Loot|Durability")
	bool bIsBroken = false;

	/** 소유 노획물. UPROPERTY 가 없으면 GC 가 회수한 뒤 엉뚱한 곳에서 크래시한다 */
	UPROPERTY()
	TObjectPtr<ALootBase> OwnerLoot;

	/** ResolveData 가 이미 돌았는가. 표 조회를 매번 반복하지 않기 위한 것이다 */
	bool bDataResolved = false;

	/**
	 * 슬롯별 동적 머티리얼 인스턴스. 첫 충격 때 만들어 재사용한다.
	 * UPROPERTY 가 없으면 GC 가 회수한 뒤 다음 충격에서 크래시한다.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CrackMaterials;

	/** 균열 파라미터를 가진 슬롯이 하나도 없다는 경고를 이미 냈는가 */
	bool bWarnedMissingCrackParameter = false;

	/** EndPlay 에서 구독을 해제하기 위한 핸들 */
	FDelegateHandle ImpactDelegateHandle;

	FTimerHandle DestroyTimerHandle;
};
