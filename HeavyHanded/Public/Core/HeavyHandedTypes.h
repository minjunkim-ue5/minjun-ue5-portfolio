#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Chaos/ChaosEngineInterface.h"
#include "HeavyHandedTypes.generated.h"

// TWeakObjectPtr 전방 선언 (헤더에서는 #include 하지 않는다 — 컨벤션)
class AActor;
class APawn;

// [삭제됨] EWeightClass — 등급과 배율이 같은 것을 두 번 말해서 어긋날 수 있었다.
// 지금은 셋으로 나뉜다: Loot.Type.Heavy 태그 · CarrySpeedMultiplier · bAllowJumpWhileCarried.
// 이산 등급이 다시 필요하면 값이 아니라 태그를 추가한다

/**
 * 충돌 원인 구분 — FLootImpactEvent 에 실려 소음 파트로 전달된다.
 * "무엇 때문에 부딪혔는가"만 알린다. 그게 얼마나 시끄러운지는 소음 파트가 해석한다.
 */
UENUM(BlueprintType)
enum class ELootImpactCause : uint8
{
	Drop      UMETA(DisplayName = "Drop"),      // 놓기
	HeavyDrop UMETA(DisplayName = "HeavyDrop"), // 중량형 놓기 — 같은 '놓기'라도 사건의 무게가 다르다
	Throw     UMETA(DisplayName = "Throw"),     // 던지기
	Collision UMETA(DisplayName = "Collision"), // 일반 충돌 (낙하·튕김·구름)
	Break     UMETA(DisplayName = "Break")      // 파괴
};

/**
 * 노획물의 물리 데이터.
 * 값은 블루프린트에서 지정하고, 로직은 C++에서만 소비한다. (컨벤션 4-3)
 */
USTRUCT(BlueprintType)
struct FLootPhysicsData
{
	GENERATED_BODY()

	/** 물리 질량(kg). 던지기 충격량과 낙하 소음 계산에 사용 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Physics",
		meta = (ClampMin = "0.1"))
	float MassKg = 10.f;

	// 필요 인원(RequiredCarriers)은 여기 없다. 2인이 필요한 것은 중량형뿐이라
	// FLootHeavyData 로 옮겨 DT_LootHeavy 에 행 이름으로 조인한다.
	// 중량형이 아닌 노획물은 물어볼 것도 없이 1명이다.

	/**
	 * 소지 중 이동 속도 배율 (기획서: 중량형 1인 시 0.3)
	 *
	 * 중량형 표로 옮기지 않은 이유는 이것이 중량형 전용이 아니기 때문이다.
	 * 금괴는 중량형이 아닌데도 0.75 로 느리다 — 무거우면 다 느리다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry",
		meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float CarrySpeedMultiplier = 1.f;

	/** 소지 중 점프 허용 여부 (기획서: 중량형 점프 불가) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry")
	bool bAllowJumpWhileCarried = true;

	/**
	 * 놓을 때 앞으로 살짝 미는 속도(cm/s). 0 이면 제자리에서 떨어진다.
	 * 제자리에 떨어뜨리면 발밑에 박혀 다시 집기 번거롭다. ThrowSpeed 와는 자릿수가 다르다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop",
		meta = (ClampMin = "0.0"))
	float DropSpeed = 200.f;

	/** 버릴 때 섞을 위쪽 성분의 비율. 살짝 떠서 굴러가야 툭 놓은 느낌이 난다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropUpwardRatio = 0.35f;

	/** 버릴 때 부여할 회전 속도(도/초). 던지기보다 약하게 준다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop",
		meta = (ClampMin = "0.0"))
	float DropSpinSpeed = 90.f;

	/** 던질 수 있는가. 중량형처럼 놓기만 되는 물건은 false 로 둔다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Throw")
	bool bAllowThrow = true;

	/**
	 * 던졌을 때의 목표 초기 속도(cm/s).
	 * 임펄스는 질량을 곱해 만들기 때문에, 무게가 달라도 이 속도 그대로 나간다.
	 * 무거운 물건이 덜 날아가는 것은 여기 값을 낮게 잡아 표현한다. (행동이 아니라 데이터로)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Throw",
		meta = (ClampMin = "0.0"))
	float ThrowSpeed = 900.f;

	/**
	 * 조준 방향에 섞을 위쪽 성분의 비율. 포물선을 만든다.
	 * 0 이면 조준한 그대로 직선으로 나가 바닥에 바로 박힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Throw",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThrowUpwardRatio = 0.25f;

	/**
	 * 던질 때 운반자의 이동 속도를 얼마나 더할지. **물리적으로는 1 이 맞지만 기본은 0 이다** —
	 * 1 이면 뒷걸음질만 쳐도 물건이 뒤로 날아가 밴에 던져 넣기가 운에 좌우된다.
	 * 밋밋하면 0.2~0.3 정도로 조금씩 올린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Throw",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CarrierVelocityInfluence = 0.f;

	/** 던질 때 부여할 회전 속도(도/초). 0 이면 회전 없이 날아가 던진 티가 안 난다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Throw",
		meta = (ClampMin = "0.0"))
	float ThrowSpinSpeed = 180.f;

	/**
	 * 던지기 직전 조준 방향으로 밀어내는 거리(cm).
	 * 손 소켓은 던진 사람의 캡슐과 겹쳐 있어서, 그대로 물리를 켜면
	 * 물리 엔진이 겹침을 해소하느라 자기가 던진 물건에 튕긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Throw",
		meta = (ClampMin = "0.0"))
	float ThrowClearance = 20.f;

	/**
	 * 이 값 미만의 충격은 FLootImpactEvent 를 방송하지 않는다.
	 * 미세 진동·재접촉을 걸러 소음 파트의 경계도가 순식간에 치솟는 것을 막는다.
	 * (물리 낙하 1회에 OnHit 은 5~15회 발생한다)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Impact",
		meta = (ClampMin = "0.0"))
	float ImpactReportThreshold = 200.f;

	// 파손 수치(DamageImpulseThreshold / MaxImpactCount)는 여기 없다.
	// ULootDurabilityComponent 만 읽는 값이라 파손형이 아닌 노획물까지 들고 다닐 이유가 없다.
	// FLootDurabilityData 로 옮겨 DT_LootDurability 에 행 이름으로 조인한다.
};

/**
 * 노획물 물리 충돌 이벤트. 물리·아이템 파트 → 소음 파트로 전달되는 데이터 단위.
 *
 * [원칙] 아이템은 '물리적 사실'만 알린다. 해석은 소음 파트가 한다.
 *   무엇이 / 얼마의 충격으로 / 무슨 재질에 / 왜 부딪혔는지만 담는다.
 *   그게 얼마나 시끄러운 소리인지는 여기서 판단하지 않는다.
 */
USTRUCT(BlueprintType)
struct FLootImpactEvent
{
	GENERATED_BODY()

	/** 충돌 지점 (월드 좌표). VFX·데칼 스폰 위치로도 쓴다 */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	FVector ImpactPoint = FVector::ZeroVector;

	/** 충돌 표면의 노멀. 데칼 방향·튕김 판단에 사용 */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	FVector ImpactNormal = FVector::ZeroVector;

	/** 충격량 크기. 소음 등급·카메라 셰이크 강도의 원천 값 */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	float ImpulseMagnitude = 0.f;

	/** 부딪힌 바닥·벽의 물리 재질 (Config 정의 7종) */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	/** 충돌 원인 (놓기/던지기/일반충돌/파괴) */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	ELootImpactCause Cause = ELootImpactCause::Collision;

	/** 부딪힌 노획물 액터 */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	TWeakObjectPtr<AActor> LootActor = nullptr;

	/**
	 * 부딪힌 상대. 재질만으로는 부족하다 — 같은 콘크리트라도 바닥에 떨어진 것과
	 * 사람이 밀친 것은 다른 사건이다. 상대가 없는 사건(파괴)에서는 비어 있다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	TWeakObjectPtr<AActor> HitActor = nullptr;

	/** 원인을 제공한 플레이어. 결과 화면 '최다 소음 유발자' 집계용 */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	TWeakObjectPtr<APawn> InstigatorPawn = nullptr;

	/** 서버 기준 발생 시각 */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Impact")
	float ServerTime = 0.f;
};

/**
 * 노획물 충돌 방송용 델리게이트. C++ 전용 멀티캐스트로 둬서
 * BP 에 소음 판정 로직을 짜는 것을 원천 차단한다. 인스턴스는 ALootBase 가 소유한다.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLootImpactSignature, const FLootImpactEvent&);
