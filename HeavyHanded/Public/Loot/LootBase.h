#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/HeavyHandedTypes.h"
#include "GameplayTagAssetInterface.h"   // 부모 인터페이스 — 전방 선언 불가
#include "GameplayTagContainer.h"        // FGameplayTagContainer 를 값으로 보유
#include "Engine/DataTable.h"            // FDataTableRowHandle 을 값으로 보유
#include "Loot/LootTypes.h"              // FLootDefinitionRow 를 조회한다
#include "Interfaces/Carryable.h"
#include "Shared/ThrowMath.h"           // FThrowParams 를 값으로 반환 — 전방 선언 불가
#include "LootBase.generated.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class UPhysicsHandleComponent;
class UNoiseEmitterComponent;
class AHandCart;
class APawn;
struct FPredictProjectilePathResult;

/**
 * 모든 노획물의 베이스.
 *
 * [설계 원칙 1] 값은 데이터, 행동은 컴포넌트.
 *   중량형·파손형·불안정형을 서브클래스로 나누지 않는다. 셋 다 컴포넌트를 붙여 만들고,
 *   수치는 각자 자기 표(DT_LootHeavy / DT_LootDurability / DT_LootStability)에서
 *   카탈로그와 같은 행 이름으로 가져간다.
 *   대형 금고처럼 '중량형 + 경보 연동형' 조합이 반드시 생기므로 상속하면 클래스가 폭발한다.
 *   (미션 가이드도 "인터페이스와 Actor Component를 활용한 설계"를 요구한다)
 *
 * [설계 원칙 2] 충돌 게이팅은 여기 한 곳에서만 한다.
 *   물리 낙하 1회에 OnHit 은 5~15회 발생한다. 튕김·구름·미세 접촉이 전부 개별 콜백으로 온다.
 *   ALootBase 가 [임계값 + 디바운스]로 걸러 '확정 충격 1개'를 만들고,
 *   그 하나를 OnLootImpact 로 방송하고 OnImpact BP 훅으로 연출을 부른다.
 *   파손 컴포넌트가 raw OnHit 을 따로 세면 파손형이 한 번 낙하로 즉사한다.
 *
 * 서버 권위 + 클라이언트 보간. 클라이언트 예측은 쓰지 않는다.
 */
UCLASS(Blueprintable)
class HEAVYHANDED_API ALootBase : public AActor, public ICarryable, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	ALootBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//~ IGameplayTagAssetInterface — "이것이 무엇인가" 를 다른 파트에 알린다
	/**
	 * 특성 태그(Loot.Type.*)와 상태 태그(Loot.State.*)를 함께 돌려준다.
	 *
	 * 플레이어 파트의 GAB_Interact 가 Loot.Type 부모 태그 하나로 집기 대상을 판정하고,
	 * 환경 파트의 압력판은 Loot.State.Dropped 를 본다.
	 * 인터페이스로 여는 이유는 그쪽이 ALootBase 를 include 하지 않아도 되게 하기 위해서다.
	 */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	//~ IGameplayTagAssetInterface 끝

	/**
	 * 특성 태그를 추가한다. 컴포넌트가 BeginPlay 에서 자기 태그를 등록한다. (서버 전용)
	 *
	 * 파손형·불안정형은 컴포넌트를 붙이는 것이 곧 선언이므로, 태그를 BP 에서 따로
	 * 지정하게 하면 컴포넌트는 있는데 태그는 없는 상태가 만들어진다.
	 * 중량형도 마찬가지다 — ULootHeavyComponent 가 스스로 단다. 예외는 없다.
	 */
	void AddLootTypeTag(const FGameplayTag& TypeTag);

	/**
	 * 상태 태그를 갈아 끼운다. (서버 전용)
	 *
	 * 상태는 배타적이라 컨테이너가 아니라 하나만 들고 있다 — 들려 있으면서 동시에
	 * 바닥에 놓여 있을 수는 없다. 파괴·유출처럼 되돌아가지 않는 상태도 여기로 들어온다.
	 */
	void SetLootStateTag(const FGameplayTag& NewState);

	//~ ICarryable 시작 — 플레이어는 요청하고, 아이템이 허용/거부한다
	virtual int32 GetRequiredCarriers() const override;
	virtual float GetCarrySpeedMultiplier() const override;
	virtual bool IsJumpAllowedWhileCarried() const override;
	virtual bool CanBeCarriedBy(const APawn* Requester) const override;
	virtual void OnGrabbed(APawn* Carrier) override;
	virtual void OnReleased(APawn* Carrier) override;
	virtual bool CanBeSecondCarrierBy(const APawn* Requester) const override;
	virtual void OnSecondGrabbed(APawn* Carrier) override;
	virtual void OnSecondReleased(APawn* Carrier) override;
	virtual APawn* GetSecondaryCarrier() const override { return SecondaryCarrier; }
	virtual int32 GetCarrierCount() const override;
	virtual FName GetGripSocketFor(const APawn* Carrier) const override;
	virtual float GetGripSeparation() const override;
	virtual bool CanBeThrown() const override;
	virtual void OnThrown(APawn* Carrier, const FVector& AimDirection) override;
	// ComputeThrowAimDirection 도 ICarryable 이지만, BlueprintPure 로 열어 두어
	// 아래 Loot|Throw 묶음에 함께 둔다.
	virtual APawn* GetPrimaryCarrier() const override;
	virtual UPrimitiveComponent* GetPhysicsRoot() const override;
	//~ ICarryable 끝

	/**
	 * 중량형 노획물이 잡혀 있는 동안 취해야 할 강체 트랜스폼을 계산한다. (순수 함수)
	 *
	 * [설계 배경] 물리 시뮬레이션(Physics Handle)로 손을 따라가게 하면 캐리어 본인과의
	 * 충돌, 회전 오버슈트, 서버 전용이라 클라이언트 예측과 어긋나는 문제가 계속 났다.
	 * 이 함수는 그 대신 "그립 A/B 로컬 위치가 목표 월드 위치에 오도록" 강체 변환을
	 * 대수적으로 풀기만 한다 — 물리도, 액터/컴포넌트 상태 참조도 하지 않는다.
	 * 그래서 서버·클라이언트 어디서 불러도 같은 입력이면 항상 같은 답이 나온다
	 * (LootHeavyComponent.h 의 2026-08-20 결정: 위치 계산은 CMC 예측 경로를 쥔
	 * 플레이어 파트가 각자 로컬로 돌린다 — 이 함수가 그 계산의 핵심부다).
	 *
	 * [Primary 가 항상 기준점] GripA 는 SecondaryHandWorld 유무와 무관하게 항상
	 * PrimaryHandWorld 에 정확히 맞춘다. 2인 캐리라도 GripB 가 SecondaryHandWorld 에
	 * 정확히 맞는다는 보장은 없다 — 그립 간격(GetGripSeparation)이 고정된 강체라서,
	 * 실제 두 손 사이 거리가 그 값과 다르면 방향만 맞고 위치는 살짝 어긋난다.
	 * (거리 자체는 GA_HeavyCarryAssist::MaxAssistDistance 가 이미 제한한다)
	 *
	 * [Up 벡터로 롤 고정] 그립 축 하나만으로는 롤(비틀림) 자유도가 안 잡힌다.
	 * 월드 Up 에 최대한 맞춰 세우는 쪽으로 롤을 고정한다. 축이 Up 과 거의 평행한
	 * 특이 케이스(물건을 거의 수직으로 든 경우)는 기준 벡터를 Forward 로 바꿔 피한다.
	 *
	 * @param LocalGripA            액터 로컬 공간에서의 Grip A 위치 (주 운반자가 잡는 지점)
	 * @param LocalGripB            액터 로컬 공간에서의 Grip B 위치 (보조 운반자가 잡는 지점)
	 * @param PrimaryHandWorld      주 운반자 손 소켓의 월드 위치. GripA 가 여기 정확히 맞는다.
	 * @param SecondaryHandWorld    보조 운반자 손 소켓의 월드 위치. 없으면(솔로) nullptr.
	 * @param PrimaryCarrierForward 주 운반자의 수평 정면 벡터. 솔로일 때만 쓰인다
	 *                              (그립 축을 이 방향 기준으로 아래로 늘어뜨린다).
	 * @param SoloDragPitchDegrees  솔로 캐리 시 GripB 쪽을 아래로 늘어뜨리는 각도(도).
	 */
	static FTransform ComputeHeavyCarryTransform(
		const FVector& LocalGripA,
		const FVector& LocalGripB,
		const FVector& PrimaryHandWorld,
		const FVector* SecondaryHandWorld,
		const FVector& PrimaryCarrierForward,
		float SoloDragPitchDegrees);

	/** 솔로 캐리 시 처짐 각도(도). ComputeHeavyCarryTransform 호출자(플레이어 파트)가 읽어 쓴다 */
	UFUNCTION(BlueprintPure, Category = "Loot|Carry")
	float GetSoloDragPitchDegrees() const { return SoloDragPitchDegrees; }

	/**
	 * 지금 이 노획물을 밴에 실었을 때 받는 금액($).
	 *
	 * 파손·유출로 깎이므로 설계값(BaseValue)과 다를 수 있다. 정산·UI 는 항상 이쪽을 본다.
	 * 오라클의 '상시 스캔'(기획서 4-1-3)처럼 클라이언트에서 읽는 곳이 있어 복제한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Loot|Value")
	int32 GetCurrentValue() const { return CurrentValue; }

	/** 손상되지 않았을 때의 설계 가치($) */
	UFUNCTION(BlueprintPure, Category = "Loot|Value")
	int32 GetBaseValue() const { return BaseValue; }

	/**
	 * 화면에 뜨는 이름. 상호작용 프롬프트("E — 도자기 들기")와 정산 목록이 쓴다.
	 *
	 * DT_LootCatalog 행에서 온다. 행을 지정하지 않았으면 비어 있고, 그때는
	 * 부르는 쪽이 알아서 대체 표기를 정한다 — 여기서 GetName() 을 돌려주면
	 * "BP_Loot_Fragile_C_0" 같은 내부 이름이 그대로 UI 에 뜬다.
	 */
	UFUNCTION(BlueprintPure, Category = "Loot|Value")
	const FText& GetDisplayName() const { return LootDisplayName; }

	/** 가치가 깎였는가 (파손·유출) */
	UFUNCTION(BlueprintPure, Category = "Loot|Value")
	bool IsValueLost() const { return CurrentValue < BaseValue; }

	/**
	 * 가치를 비율만큼 깎는다. (서버 전용)
	 *
	 * 불안정형 유출은 일부만, 파손형 파괴는 전부 깎는다. 어느 쪽이든 되돌리지 않는다 —
	 * 쏟은 동전을 주워 담는 규칙은 기획에 없다.
	 *
	 * @param LossRatio  0~1. 1 이면 가치 0
	 */
	void ApplyValueLoss(float LossRatio);

	/**
	 * 던졌을 때의 발사 속도. 조준 방향 + 포물선 성분 + 운반자 속도까지 합친 최종 값이다.
	 *
	 * 서버의 임펄스와 클라이언트의 궤적 표시가 이 함수 하나를 공유한다.
	 * 계산을 따로 두면 미리 보이는 궤적과 실제로 날아가는 경로가 어긋난다.
	 */
	FVector ComputeThrowVelocity(const FVector& AimDirection) const;

	/**
	 * 표에 흩어져 있는 던지기 수치를 공용 계산이 받는 형태로 모은다.
	 *
	 * 필드를 FThrowParams 로 옮기지 않고 여기서 옮겨 담는 이유는, 옮기면
	 * DT_LootCatalog 의 기존 행이 중첩 구조로 바뀌면서 값을 잃기 때문이다.
	 */
	FThrowParams MakeThrowParams() const;

	/**
	 * 지금 이 노획물을 던진다면 어느 방향으로 나가야 하는가.
	 *
	 * 운반자의 시선을 그대로 쓰지 않는다. 발사점(손)이 화면 중앙에서 벗어나 있으면
	 * 시선 방향으로 던졌을 때 조준점 옆으로 날아가고, 멀수록 오차가 커진다.
	 * 카메라에서 트레이스해 조준점을 먼저 찾고, 발사점에서 그 지점을 향하게 한다.
	 *
	 * [경계] 보정에 필요한 발사점은 물건만 안다. 그래서 이 계산이 아이템 쪽에 있다.
	 *   플레이어 파트는 "언제 던지는가" 만 정하고 이 값을 그대로 OnThrown 에 넘기면 된다.
	 *   궤적 표시와 실제 발사가 같은 값을 쓰게 하려면 반드시 한 곳에서만 구해야 한다.
	 *
	 * 들려 있지 않으면 영벡터를 돌려준다.
	 */
	UFUNCTION(BlueprintPure, Category = "Loot|Throw")
	virtual FVector ComputeThrowAimDirection() const override;

	/**
	 * 던지기 궤적을 예측한다. 조준 중인 클라이언트가 로컬로 그리는 표시용이다.
	 *
	 * 클라이언트 예측이 아니다 — 결과를 서버에 보내지 않고, 실제 판정은 서버가 다시 한다.
	 * 표시가 실제와 다르면 그건 표시가 틀린 것이지 게임 상태가 갈린 것이 아니다.
	 */
	bool PredictThrowPath(const FVector& AimDirection, FPredictProjectilePathResult& OutResult);

	/**
	 * 예측 궤적을 그린다. 기본값은 한 프레임만 그리므로 조준 중 매 프레임 호출하면 된다.
	 *
	 * [경계] 언제 그릴지(조준 버튼을 누르고 있는 동안)는 플레이어 파트가 정한다.
	 *   아이템은 '자기가 어떻게 날아갈지'만 그린다.
	 *   실제로 던질 때와 같은 ComputeThrowVelocity 를 쓰므로 표시와 결과가 어긋나지 않는다.
	 *
	 * 지금은 디버그 선으로 그린다. 최종 연출(스플라인 메시·나이아가라 리본)은 나중에 교체한다.
	 */
	void ShowThrowTrajectory(const FVector& AimDirection, float Duration = -1.f);

	/**
	 * 게이팅을 통과한 '확정 충격'만 방송된다. 서버에서만 발생한다.
	 *
	 * 지금 구독자는 ULootDurabilityComponent 하나다 — DamageImpulseThreshold 이상만
	 * 골라 파손으로 누적한다. 연출은 델리게이트가 아니라 OnImpact BP 훅으로 나간다.
	 *
	 * [소음 파트는 여기를 구독하지 않는다]
	 *   노획물의 소음은 UNoiseEmitterComponent 가 자기 경로로 발행한다.
	 *   그래서 충돌 게이팅이 두 벌 돌지만 목적이 달라 합치지 않는다 —
	 *   이쪽은 '파손으로 칠 충격' 을 고르고, 소음 쪽은 '얼마나 시끄러운가' 를
	 *   연속값으로 뽑은 뒤 스팸을 AlertScale 로만 억제한다.
	 */
	FOnLootImpactSignature OnLootImpact;

	/** 모든 노획물이 쓰는 물리·운반 수치. 특성별 수치는 각 컴포넌트가 자기 표에서 읽는다 */
	const FLootPhysicsData& GetPhysicsData() const { return PhysicsData; }

	/**
	 * 이 노획물이 DT_LootCatalog 에서 쓰는 행 이름. 지정하지 않았으면 NAME_None.
	 *
	 * 특성 컴포넌트(불안정형·파손형)가 자기 표를 조회할 때 조인 키로 쓴다.
	 * 컴포넌트에 별도 FDataTableRowHandle 을 두지 않는 이유는, 같은 이름을 두 번 적게 되고
	 * 빠뜨리면 경고 없이 기본값으로 돌기 때문이다. 여기서 한 번만 정한다.
	 */
	FName GetLootRowName() const { return LootDefinition.RowName; }

	/**
	 * 소음 발행 창구. 특성 컴포넌트(파손형·불안정형)가 자기 사건을 알릴 때 쓴다.
	 *
	 * 컴포넌트가 FindComponentByClass 로 매번 찾지 않도록 여기서 열어 준다.
	 * 충돌 소음은 이 컴포넌트가 스스로 낸다 — 여기로 부르는 것은 '깨졌다' '쏟았다' 처럼
	 * 물리 충돌이 아니어서 저쪽이 알 수 없는 사건뿐이다.
	 */
	UNoiseEmitterComponent* GetNoiseEmitter() const { return NoiseEmitter; }

	// ── 카트 적재 ─────────────────────────────────────────────────────────

	/** 지금 실려 있는 카트. 없으면 nullptr */
	UFUNCTION(BlueprintPure, Category = "Loot|Cart")
	AHandCart* GetContainingCart() const { return ContainingCart; }

	UFUNCTION(BlueprintPure, Category = "Loot|Cart")
	bool IsContainedInCart() const { return ContainingCart != nullptr; }

	/**
	 * 카트에 실리거나 빠져나올 때 AHandCart 가 부른다. (서버 전용)
	 *
	 * 여기서 두 가지를 끄고 켠다.
	 *   1. 소음  — NoiseEmitter 에 배율 0 감쇄를 등록한다. 안 막으면 소음을 줄이려고
	 *              산 장비가 소음 발생기가 된다
	 *   2. 파손  — 충격 보고 자체를 건너뛴다. 안 막으면 파손형이 타고 가는 것만으로 깨진다
	 *
	 * [유출은 일부러 안 막는다] 불안정형은 기울기로 판정하지 충격으로 판정하지 않아서
	 *   여기서 아무것도 안 해도 살아 있다. 파손형은 카트에서 안전하고 불안정형은 그렇지 않은
	 *   것이 의도한 차이다 — 물건마다 카트와의 관계가 달라야 "이건 실어도 되나" 를 판단하게 된다.
	 *   (2026-08-20 결정)
	 *
	 * 카트 밖에서 직접 부르지 말 것. 목록을 들고 있는 것은 카트라서 한쪽만 바뀌면 어긋난다.
	 */
	void SetContainingCart(AHandCart* Cart);

	/**
	 * 손에서 놓인 직후, 지금 겹쳐 있는 카트가 있으면 그 카트에 싣는다. (서버 전용)
	 *
	 * [왜 필요한가 — 오버랩만으로는 못 잡는 구멍이다]
	 *   물건을 들고 카트 안까지 밀어 넣으면 그 순간 BeginOverlap 이 오지만, 손에 들려 있어서
	 *   AHandCart::ContainLoot 이 거부한다 (들고 서 있기만 해도 조용해지면 카트가 필요 없어지므로
	 *   그 거부 자체는 맞다). 문제는 그다음이다 — 손을 놓아도 물건은 이미 볼륨 안에 있으므로
	 *   BeginOverlap 이 다시 오지 않는다. 그대로 두면 그 물건은 영원히 적재되지 않는다.
	 *
	 *   즉 '멀리서 던져 넣기' 만 되고 '들고 가서 놓기' 는 안 되는 상태였다.
	 *   놓는 쪽이 훨씬 자연스러운 조작이라 이쪽이 오히려 주된 경로다. (2026-08-21)
	 */
	void TryContainInOverlappingCart();

	/**
	 * 마지막으로 이 노획물을 든 사람. 놓거나 던진 뒤에도 남는다. 아무도 든 적 없으면 nullptr.
	 *
	 * [왜 PrimaryCarrier 로 부족한가] 밴에 '던져 넣기' 가 허용되므로(기획 5장 · 코어 루프 Day 2),
	 *   적재가 확정되는 시점에는 이미 손에서 떠나 PrimaryCarrier 가 비어 있다.
	 *   그때 "누가 실었는가" 를 알 수 있는 것은 이 값뿐이다 — 적재자 ASC 로 Event.Loot.Loaded 를
	 *   보내고 결과 화면 기여도를 집계하는 근거가 된다.
	 *
	 * 서버에서만 갱신되고 복제하지 않는다. 집계와 이벤트 발행이 전부 서버 판정이라 필요가 없다.
	 */
	APawn* GetLastCarrier() const { return LastCarrier.Get(); }

	/**
	 * 다음 확정 충격의 원인을 예약한다. (서버 전용)
	 * 놓기·던지기 직후 첫 충돌을 Drop / Throw 로 표시하기 위한 것으로,
	 * 한 번 소비되면 다시 Collision 으로 돌아간다.
	 * 던지기 단계에서 플레이어를 InInstigator 로 넘겨 '최다 소음 유발자' 집계에 쓴다.
	 */
	void SetPendingImpactCause(ELootImpactCause InCause, APawn* InInstigator);

	/**
	 * 물리 충돌이 아닌 사유(파괴 등)로 확정 충격을 즉시 방송한다. (서버 전용)
	 *
	 * 게이팅을 거치지 않는다. 게이팅은 '물리 낙하 1회가 OnHit 5~15회로 쪼개지는 것'을
	 * 되묶기 위한 장치인데, 파괴는 애초에 한 번만 일어나는 사건이라 되묶을 것이 없다.
	 */
	void ReportImpact(ELootImpactCause InCause, float ImpulseMagnitude,
		const FVector& ImpactPoint, APawn* InInstigator);

	/** 파손 컴포넌트 등이 같은 스위치로 디버그를 켜고 끄기 위해 연다 */
	bool IsImpactDebugEnabled() const { return bShowImpactDebug; }

	/**
	 * [임시] 잡기/놓기 토글. 0번 로컬 플레이어 기준.
	 *
	 * 집기 입력·트레이스는 플레이어 파트 담당이라 아직 호출해 줄 것이 없다.
	 * DebugGrabRange 안에 있을 때만 반응하므로, 여러 개를 깔아 둬도
	 * 가까이 간 하나만 잡힌다. 플레이어 파트가 연결되면 지운다.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Loot|Debug")
	void Debug_ToggleGrabByLocalPlayer();

	/**
	 * [임시] 조준 시작. T 를 누르고 있는 동안 궤적이 매 프레임 갱신된다.
	 * 실제 게임에서는 플레이어 파트가 조준 입력을 받아 ShowThrowTrajectory 를 호출한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Loot|Debug")
	void Debug_BeginThrowAim();

	/**
	 * [임시] 조준을 끝내고 던진다. T 를 뗀 순간 호출된다.
	 * 보고 있던 궤적을 6초간 남겨서 실제 경로와 겹치는지 비교할 수 있게 한다.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Loot|Debug")
	void Debug_ThrowForward();

protected:
	/**
	 * DT_LootCatalog 행을 반영한다. BeginPlay 보다 먼저 돌아야 한다 —
	 * BeginPlay 가 PhysicsData.MassKg 로 질량을 덮고 BaseValue 로 CurrentValue 를 채운다.
	 */
	virtual void PostInitializeComponents() override;

	virtual void BeginPlay() override;

#if WITH_EDITOR
	/**
	 * LootDefinition 행이 연결돼 있으면 표가 덮어쓰는 칸들을 디테일 패널에서 잠근다.
	 *
	 * 잠그지 않으면 BP 에서 고칠 수 있는데 PostInitializeComponents 가 그 값을
	 * 표의 값으로 덮어써서 아무 일도 일어나지 않는다. 저장까지 되기 때문에
	 * "고쳤는데 왜 안 먹지" 로 시간을 버리게 된다. 어제 넣은 경고와 같은 종류의
	 * 조용한 실패이고 방향만 반대다.
	 */
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** 평소에는 꺼져 있고 조준 중에만 켜진다 (궤적 갱신용) */
	virtual void Tick(float DeltaSeconds) override;

	/** 물리 바디이자 루트. 플레이어 파트가 Attach 대상으로 쓴다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> LootMesh;

	/**
	 * 중량형 운반 시 물리를 켠 채로 Grip_A 지점을 붙잡아 주 운반자 손 쪽으로 당기는 핸들.
	 * 일반 노획물은 쓰지 않는다 — 그쪽은 AttachToCarrier(물리 끄고 Attach)를 그대로 쓴다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot|Carry|Heavy")
	TObjectPtr<UPhysicsHandleComponent> HeavyCarryHandle;

	/**
	 * 충돌을 소음으로 바꾼다. 소음 파트의 컴포넌트이고 여기서는 붙이기만 한다.
	 *
	 * [내가 부를 일이 없다] BeginPlay 에서 스스로 루트의 OnComponentHit 을 물고,
	 *   임펄스와 표면 재질로 크기를 뽑아 발행한다. 서버가 아니면 델리게이트를 아예 안 건다.
	 *   ImpactTag 를 안 채우면 Noise.Loot.Impact 로 폴백하는데 그게 바로 우리가 쓸 태그다.
	 *
	 * [BP 가 아니라 C++ 에서 붙이는 이유]
	 *   노획물 종류가 늘 때 BP 마다 추가하는 방식이면 언젠가 빼먹는다. 그러면 그 물건만
	 *   소리를 안 내는데, 경고도 안 뜨고 눈에도 안 보여서 발견이 아주 늦다.
	 *   모든 노획물이 소리를 내야 하므로 생성자에서 붙인다. 기존 BP 는 재저장 없이 물려받는다.
	 *
	 * [소지 중에는 저절로 조용하다] 물리를 끄고 CarriedLoot(QueryOnly) 로 바꾸므로
	 *   히트 자체가 오지 않는다. 들고 다닐 때 소리를 막는 코드를 따로 둘 필요가 없다.
	 *
	 * [충돌 게이팅이 두 벌 도는 것은 의도다] 내 HandleMeshHit 과 이 컴포넌트가 같은
	 *   델리게이트에 둘 다 붙는다. 내 쪽은 '파손으로 칠 충격' 하나를 고르고, 소음 쪽은
	 *   '얼마나 시끄러운가' 를 연속값으로 뽑아 자기 스팸 필터로 억제한다. 목적이 달라 합치지 않는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot|Noise")
	TObjectPtr<UNoiseEmitterComponent> NoiseEmitter;

	/**
	 * DT_LootCatalog 에서 이 노획물의 설계값을 가져올 행.
	 *
	 * 지정하면 아래 PhysicsData 와 BaseValue, 그리고 표시 이름을 이 행의 값으로 덮어쓴다.
	 * 비워 두면 아래 인라인 값을 그대로 쓴다 — 표에 아직 안 올린 노획물이나
	 * 일회성 실험물을 위해 열어 둔 길이다.
	 *
	 * 이걸 쓰는 이유는 수치를 BP(uasset) 밖으로 빼기 위해서다. BP 안에 있으면
	 * 기획자가 값 하나 고치려고 BP 를 열어야 하고, 그 파일은 병합이 안 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Data")
	FDataTableRowHandle LootDefinition;

	/**
	 * 무게·질량·임계값. LootDefinition 을 지정했으면 그 행의 값으로 덮어써지고,
	 * 그때는 CanEditChange 가 이 칸을 회색으로 잠근다. 수치는 표에서 본다.
	 *
	 * EditAnywhere 를 유지하는 것은 행을 비웠을 때 다시 열리게 하기 위해서다.
	 * VisibleAnywhere 로 바꾸면 표에 안 올린 실험물을 BP 에서 만들 길이 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FLootPhysicsData PhysicsData;

	/**
	 * 화면 표시 이름. LootDefinition 행에서 온다.
	 *
	 * 복제하지 않는다 — 표 조회는 모든 머신에서 같은 답이 나오는 순수 계산이고,
	 * PostInitializeComponents 는 서버·클라이언트 양쪽에서 돈다. 굳이 대역폭을 쓸 이유가 없다.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Loot|Data")
	FText LootDisplayName;

	/**
	 * 소지 중 어태치할 운반자 스켈레탈 메시의 소켓 이름.
	 *
	 * 캐릭터 리깅이 바뀌거나 파트별로 손 소켓 이름이 달라질 수 있으므로 코드에 박지 않는다.
	 * BP 자식 클래스에서 노획물마다 다른 소켓(예: 양손 물건은 별도 소켓)을 지정할 수도 있다.
	 *
	 * [주의] 기본값은 실제 캐릭터 리그의 소켓 이름과 같아야 한다.
	 *   ABaseCharacter 도 같은 이름을 자기 CarrySocketName 으로 들고 있는데, 어태치를
	 *   ALootBase 가 맡게 되면서 실제로 쓰이는 것은 이쪽 값 하나다. 둘이 어긋나면
	 *   소켓을 못 찾아 메시 원점(= 발밑)에 붙고, 에러도 경고도 나지 않는다.
	 *   ALootBase::AttachToCarrier 가 DoesSocketExist 로 확인은 하지만 조용히 넘어간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry")
	FName CarrySocketName = TEXT("Hand_R_Socket");

	/**
	 * 손 소켓을 찾지 못했을 때 운반자 기준 어디에 들 것인가(cm, X=정면).
	 *
	 * 소켓이 있으면 소켓 위치가 답이므로 쓰지 않는다. 이 값은 소켓이 없는 폰
	 * (지금의 테스트용 DefaultPawn)에서만 적용된다. 그대로 두면 노획물이 폰 원점,
	 * 즉 카메라 자리에 붙어서 화면을 가리거나 아예 안 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry")
	FVector NoSocketCarryOffset = FVector(80.f, 0.f, -20.f);

	/**
	 * 소지 중 메시의 아랫면 중심을 '손이 잡은 지점'으로 본다.
	 *
	 * 액터 원점(대개 메시 중심)을 기준으로 기울이면 물건이 제자리에서 팽이처럼 돈다.
	 * 사람이 상자를 들 때는 아랫부분을 받치고 있으므로, 그 지점을 고정하고
	 * 윗부분만 넘어가야 관성처럼 보인다.
	 *
	 * 끄면 원점 기준으로 돌아간다 — 중심을 잡는 물건(구슬·가방 손잡이)용.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry")
	bool bCarryGripAtBottom = true;

	/**
	 * 손상되지 않았을 때의 가치($). LootDefinition 행에서 오며 그때는 회색으로 잠긴다.
	 *
	 * 기획서의 목표 금액은 저택 $50,000 / 박물관 $120,000 / 은행 $250,000 이고,
	 * 이 값들은 전부 임시라 플레이테스트로 조정된다. 8단계에서 DataAsset 으로 뺀다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Value",
		meta = (ClampMin = "0"))
	int32 BaseValue = 1000;

	/**
	 * 파괴 연출이 끝난 뒤 BP 가 가치 변화를 반영할 훅. (모든 머신)
	 * 숫자 위젯·머티리얼 변화 같은 표시만 한다. 판정은 이미 C++ 에서 끝났다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Value")
	void OnValueChanged(int32 NewValue, int32 InBaseValue);

	/**
	 * 확정 충격 하나가 났을 때 호출된다. 충돌 연출 전용 훅이다. (서버에서만)
	 *
	 * [무엇을 하라는 훅인가] 소리·먼지 파티클·자국 데칼·카메라 셰이크.
	 *   게이팅을 통과한 것만 오므로, 낙하 한 번에 5~15번 터지지 않는다.
	 *   BP 는 Cause 를 보고 연출을 고른다 — HeavyDrop 이면 낮고 긴 '쿵' 에
	 *   먼지와 셰이크를 얹고, Drop 이면 가벼운 '탁' 으로 끝낸다.
	 *
	 * [왜 임펄스가 아니라 Cause 로 고르나] 세기만으로는 갈리지 않는다.
	 *   청동상을 살살 내려놓는 것과 왕관을 높은 데서 떨어뜨리는 것이 비슷한 숫자로 나오는데
	 *   플레이어에게는 전혀 다른 사건이다. 세기는 연출의 '정도' 를 정하는 데 쓰고,
	 *   무엇을 재생할지는 종류로 고른다.
	 *
	 * [왜 서버에서만인가] 확정 충격을 만드는 게이팅이 서버에만 있다. 물리 시뮬레이션
	 *   결과가 머신마다 미세하게 달라서, 클라이언트가 각자 판정하면 사람마다 다른 순간에
	 *   다른 횟수로 소리가 난다.
	 *
	 *   그래서 지금은 리슨 서버 창에서만 연출이 보인다. 클라이언트까지 보내는 것은
	 *   Unreliable Multicast 한 번이면 되지만, 연출 자산이 붙고 무엇을 보낼지
	 *   정해진 뒤에 하는 게 맞다 — 지금 만들면 빈 이벤트를 복제하게 된다.
	 *
	 * [경계] 여기서 게임 상태를 바꾸지 않는다. 가치·파손·태그는 이미 C++ 이 정했다.
	 *   소음도 여기서 내지 않는다 — 그건 UNoiseEmitterComponent 가 별도 경로로 한다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Impact")
	void OnImpact(const FLootImpactEvent& Event);

	/**
	 * 놓을 때 운반자 몸에서 앞으로 띄우는 여유(cm).
	 *
	 * 두 형상의 반경을 더한 값에 이만큼 더 벌린다. 딱 붙여 놓으면 놓자마자
	 * 한 발짝만 움직여도 자기가 놓은 물건에 닿는다.
	 * 파손은 이 거리로 막는 것이 아니다 — 그건 bIgnorePawnImpacts 가 담당한다.
	 * 여기서는 놓은 물건이 발에 차이지 않을 정도만 확보한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry",
		meta = (ClampMin = "0.0"))
	float ReleaseForwardClearance = 30.f;

	/**
	 * 놓거나 던진 뒤 이 시간(초) 동안 사람과의 물리 충돌을 무시한다. 0 이면 끈다.
	 *
	 * [위치를 빼내는 것만으로는 부족하다]
	 *   ResolveReleaseOverlap 이 물리를 켜기 전에 물건을 몸 밖으로 옮기지만, 좁은 구석에서는
	 *   빠져나갈 자리가 없어 겹친 채로 남는다. 던지기는 아예 위치로 못 막는다 — 손에서
	 *   출발해 몸을 스치며 날아가는 것이 정상 궤도라서, 출발점을 몸 밖으로 옮기면
	 *   조준 궤적 예측과 실제 궤도가 어긋난다.
	 *
	 * [그래서 접촉 자체를 잠깐 끈다]
	 *   ReleasedLoot 프로파일로 바꿨다가 이 시간이 지나면 Loot 으로 돌아온다.
	 *   IgnoreActorWhenMoving 으로는 안 된다 — 그건 컴포넌트 이동 스윕 전용이고,
	 *   시뮬레이션 중인 바디의 접촉은 물리 엔진이 따로 처리한다 (ResolveReleaseOverlap 주석 참고).
	 *
	 * [던진 사람만 골라 무시할 수는 없다]
	 *   프로파일은 채널 단위라 그 구간에는 모든 사람을 통과한다. 0.3 초면 물건이 몸을
	 *   벗어나기에 충분하고, 남이 그 사이에 끼어들어 통과당하는 일은 눈에 띄지 않는다.
	 *   경비(Guard 채널)는 계속 Block 이라 던져 맞히는 것은 그대로 된다.
	 *
	 * [팔 뻗기 애니메이션이 오면 다시 볼 것]
	 *   물건을 몸 밖에서 놓게 되면 원인 자체가 없어진다. 그때 이 값을 0 으로 내려
	 *   껐을 때 문제가 없는지 확인하고, 없으면 이 경로를 지운다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry",
		meta = (ClampMin = "0.0", ClampMax = "2.0", Units = "s"))
	float ReleaseIgnorePawnSeconds = 0.3f;

	/**
	 * 솔로로 중량형을 들 때, 안 잡힌 쪽(Grip B)을 주 운반자 정면 기준 아래로
	 * 늘어뜨리는 각도(도). 보조 운반자가 없어 두 번째 좌표를 못 구하는 상황을
	 * 물리 없이 자연스러워 보이게 표현하는 값이다. ComputeHeavyCarryTransform 참고.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Carry",
		meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float SoloDragPitchDegrees = 25.f;


	/**
	 * 같은 대상에 대한 재충돌을 이 시간 동안 무시한다.
	 * 임계값만으로는 부족하다 — 세게 떨어지면 강한 충격이 연달아 여러 번 잡힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Impact",
		meta = (ClampMin = "0.0"))
	float ImpactDebounceSeconds = 0.3f;

	/**
	 * 충돌 게이팅이 실제로 도는지 화면에 표시한다. (테스트용, 기본 꺼짐)
	 *
	 * 이것만 켜면 '확정' 된 충격만 보인다. 기각 사유까지 보려면
	 * bShowRejectedImpacts 를 같이 켠다.
	 *
	 * 판정은 서버에서만 돌기 때문에 표시도 서버(호스트 창)에만 나온다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Debug")
	bool bShowImpactDebug = false;

	/**
	 * 기각된 충격도 사유와 함께 표시한다. (bShowImpactDebug 가 켜져 있을 때만 의미가 있다)
	 *
	 * 낙하 1회에 OnHit 이 몇 번 오고 그중 몇 개가 통과하는지는 이걸 켜야 보인다.
	 * 임계값과 디바운스가 적당한지 판단하려면 그 비율이 있어야 하므로, 값을 만질 때는 켠다.
	 *
	 * 그런데도 기본을 끈 것은 양 때문이다. 중량형은 질량이 커서 플레이어가 몸을 스치기만 해도
	 * 임펄스 2000 대가 매 프레임 올라오는데, 임계값(궤짝은 4000)에 미치지 못해 전부
	 * '기각(약함)' 으로 찍힌다. 정작 봐야 할 '확정' 한 줄이 그 사이에 묻힌다.
	 *
	 * DebugMinLogImpulse 를 올려서 거를 수도 있지만, 임계값이 BP 마다 다르니 두 값을 늘
	 * 같이 맞춰야 한다. 스위치를 따로 두는 편이 관리할 것이 적다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Debug",
		meta = (EditCondition = "bShowImpactDebug"))
	bool bShowRejectedImpacts = false;

	/**
	 * [임시] G = 잡기/놓기, T = 던지기 키를 이 액터에 연결한다.
	 *
	 * 잡기 입력이 아직 없어서(플레이어 파트 담당) 손으로 눌러 볼 수단이 필요하다.
	 * 판정이 서버 전용이라 호스트 창에서만 연결된다. 클라이언트 창에서는 눌러도 반응이 없다.
	 * 플레이어 파트가 연결되면 이 스위치와 Debug_ 함수들을 통째로 지운다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Debug")
	bool bDebugEnableTestKeys = false;

	/**
	 * 이 임펄스 미만의 충돌은 디버그 출력에서 뺀다.
	 *
	 * 낙하 1회에 OnHit 이 5~15회 오고 대부분이 임펄스 한 자릿수의 미세 재접촉이다.
	 * 전부 찍으면 정작 봐야 할 충돌이 로그에 묻힌다.
	 * 게이팅 자체와는 무관하다 — 여기서 거르는 것은 '보여 줄 것'뿐이고,
	 * 실제 판정 기준은 ImpactReportThreshold 와 DamageImpulseThreshold 다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Debug",
		meta = (ClampMin = "0.0"))
	float DebugMinLogImpulse = 100.f;

	/** [임시] 이 거리 안에 있을 때만 G 키에 반응한다 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Debug",
		meta = (ClampMin = "0.0"))
	float DebugGrabRange = 400.f;

	/**
	 * 먼저 잡은 사람. 노획물이 이 사람에게 어태치된다.
	 *
	 * [리더가 아니다] 2인 캐리에서 이 사람이 끌고 다른 사람이 따라오는 구조가 아니다.
	 *   두 사람의 이동을 합쳐 물체 위치가 정해진다. 여기가 '첫 번째' 인 이유는
	 *   액터가 부모를 하나만 가질 수 있어서 어태치 대상을 하나 골라야 하기 때문이고,
	 *   그 이상의 의미는 없다. 그립도 이 사람이 A, 두 번째가 B 로 갈릴 뿐이다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_PrimaryCarrier, VisibleInstanceOnly, Category = "Loot|Carry")
	TObjectPtr<APawn> PrimaryCarrier;

	/**
	 * 반대쪽 그립을 잡은 사람. 중량형에서만 채워진다.
	 *
	 * [왜 ULootHeavyComponent 가 아니라 여기 있나]
	 *   중량형인지 판단하는 것은 컴포넌트지만, 이 값은 운반 상태의 일부다.
	 *   어태치·콜리전·이동 무시가 전부 ALootBase 에 있고 ApplyCarryState 하나가 같이 반영한다.
	 *   컴포넌트로 빼면 운반 상태가 두 객체로 갈라지고, 둘은 복제 채널이 달라서
	 *   도착 순서가 어긋난다 — ApplyCarryState 의 멱등 처리로 막아 둔 바로 그 문제다.
	 *
	 * 첫 번째 운반자가 손을 떼면 이 사람이 승격된다. 중량형도 1인 운반이 가능하기
	 * 때문이다 (속도 30%). 물건만 바닥에 떨어뜨리면 그 규칙과 어긋난다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_SecondaryCarrier, VisibleInstanceOnly, Category = "Loot|Carry")
	TObjectPtr<APawn> SecondaryCarrier;

	/**
	 * 지금 실려 있는 카트. 없으면 nullptr. 채우는 것은 AHandCart 뿐이다.
	 *
	 * [운반자와 다른 상태다]
	 *   사람이 들면 물리를 끄고 붙이지만, 카트에 실리면 물리를 그대로 둔다.
	 *   물건이 카트 안에서 흔들리고 험하게 몰면 쏟아지는 것이 카트의 유일한 위험 요소라서,
	 *   물리를 끄면 그게 사라진다. 그래서 ApplyCarryState 를 타지 않는다.
	 *
	 * [복제하는 이유] 억제 판정 자체는 서버에서만 하지만, 이 값이 클라이언트에도 있으면
	 *   "왜 아직 시끄럽지" 를 디버깅할 때 어느 쪽이 어긋났는지 바로 보인다.
	 *   상태가 하나뿐이고 반영할 부수효과가 없어서 도착 순서 문제도 없다.
	 */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Loot|Cart")
	TObjectPtr<AHandCart> ContainingCart;

	/**
	 * 특성 태그(Loot.Type.*). BeginPlay 에서 채워지고 그 뒤로 바뀌지 않는다.
	 *
	 * 클라이언트도 상호작용 프롬프트("E — 들기")를 띄우려면 종류를 알아야 하므로 복제한다.
	 * 판정 자체는 서버에서만 한다.
	 */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Loot|Tags")
	FGameplayTagContainer LootTypeTags;

	/** 상태 태그(Loot.State.*). 하나만 유효하다 */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Loot|Tags")
	FGameplayTag LootStateTag;

	UFUNCTION()
	void OnRep_PrimaryCarrier();

	UFUNCTION()
	void OnRep_SecondaryCarrier();

	/**
	 * 현재 가치($). 파손·유출로 깎인다.
	 * BeginPlay 에서 BaseValue 로 초기화된다 — 생성자에서 넣으면 BP 가 BaseValue 를
	 * 바꿔도 따라오지 않는다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentValue, VisibleInstanceOnly, Category = "Loot|Value")
	int32 CurrentValue = 0;

	UFUNCTION()
	void OnRep_CurrentValue();

	/** 물리 ON/OFF, 콜리전 프로파일, 어태치, 운반자 상호 무시를 소지 상태에 맞춘다. 모든 머신에서 실행된다 */
	virtual void ApplyCarryState();

	/** 운반자의 손 소켓에 어태치한다. 물리를 끈 뒤에 부른다 */
	void AttachToCarrier(APawn* Carrier);

	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
	/** 디바운스 통과 여부를 판정하고, 통과하면 발생 시각을 기록한다 */
	bool TryConsumeImpactCooldown(const AActor* OtherActor, float Now);

	/** 운반자 캡슐과 노획물이 서로의 이동 스윕을 무시하도록 설정/해제한다 */
	void SetCarrierMoveIgnore(APawn* Carrier, bool bIgnore);

	/**
	 * 놓기·던지기 직후 사람을 통과시키는 구간을 시작한다. 모든 머신에서 돈다.
	 * 물리를 켠 뒤에 부른다 — 프로파일을 먼저 바꾸고 시뮬레이션을 켜야 필터가 반영된다.
	 */
	void BeginReleaseIgnorePawn();

	/**
	 * 통과 구간을 끝내고 평소 프로파일로 되돌린다.
	 *
	 * 구간이 끝나기 전에 누가 다시 집어 갔을 수 있다. 그때 되돌리면 소지 중 프로파일을
	 * 덮어써서 운반자가 자기 물건에 막힌다. 그래서 아직 놓여 있을 때만 되돌린다.
	 */
	void EndReleaseIgnorePawn();

	/**
	 * 물리를 켜기 전에 운반자 몸 밖으로 빼낸다. (서버 전용)
	 * 겹친 채로 켜면 물리 엔진이 침투를 밀어내며 만든 임펄스가 그대로 충돌로 잡히고,
	 * 버린 물건이 엉뚱한 방향으로 튄다.
	 */
	void ResolveReleaseOverlap(const APawn* Carrier);

	/**
	 * 버릴 때 보는 방향으로 약하게 밀어 준다. (서버 전용)
	 * 던지기와 같은 경로를 쓰지 않는다 — 버리기는 조준도 궤적 예측도 없고 값도 훨씬 작다.
	 */
	void ApplyDropImpulse(const APawn* Carrier);

	/**
	 * 로그 + 화면 메시지 + 충돌 지점 구. bShowImpactDebug 가 꺼져 있으면 아무것도 하지 않는다.
	 * @param FilterImpulse  이 충격의 임펄스. DebugMinLogImpulse 미만이면 출력하지 않는다.
	 *                       음수를 넘기면 임펄스와 무관한 메시지로 보고 항상 출력한다.
	 */
	void ShowImpactDebug(const FString& Message, const FColor& Color, const FVector& Location,
		float FilterImpulse = -1.f) const;

	/**
	 * 기각 사유 전용 표시. bShowRejectedImpacts 가 꺼져 있으면 아무것도 하지 않는다.
	 *
	 * 기각은 확정보다 훨씬 자주 나오므로 스위치를 따로 뒀다.
	 * 그것 말고는 ShowImpactDebug 와 같다 — 앞에 스위치 하나를 더 둔 것뿐이다.
	 */
	void ShowRejectDebug(const FString& Message, const FColor& Color, const FVector& Location,
		float FilterImpulse = -1.f) const;

	/** [임시] G/T 키를 이 액터에 연결한다. BeginPlay 에서 부른다 */
	void Debug_SetupTestKeys();

	/** [임시] T 를 누르고 있는 동안 참. 이때만 틱이 돈다 */
	bool bDebugAiming = false;

	/**
	 * 손 소켓 없이 들려 있는가. 소켓이 없으면 매 프레임 시선 앞에 다시 놓아야 한다.
	 * 진짜 캐릭터에 hand_r 이 생기면 항상 false 가 되고 이 경로는 죽는다.
	 */
	bool bCarriedWithoutSocket = false;

	/** 소켓이 없을 때 시선 앞에 노획물을 붙여 둔다 */
	void UpdateNoSocketCarryTransform(const APawn* Carrier);

	/**
	 * 소지 중 '손이 잡은 지점'의 오프셋. 액터 원점 기준의 로컬 방향이고 스케일이 반영돼 있다.
	 * bCarryGripAtBottom 이 꺼져 있으면 0 벡터(= 원점을 잡는다).
	 */
	FVector GetCarryGripOffset() const;

	/**
	 * LootDefinition 이 가리키는 행을 찾아 PhysicsData / BaseValue / LootDisplayName 에 반영한다.
	 * 행을 지정하지 않았으면 아무것도 하지 않는다 (인라인 값 유지).
	 */
	void ApplyLootDefinition();

	/**
	 * 특성 표에 행이 있는데 그 특성의 컴포넌트가 없는 경우를 경고한다. (서버 전용)
	 *
	 * 컴포넌트를 붙이는 것이 곧 특성 선언이라, 표에만 적으면 아무 일도 일어나지 않는다.
	 * 에러도 경고도 없어서 "왜 안 새지" 하며 수치를 계속 만지게 되는데 원인은 수치가 아니라 BP 다.
	 *
	 * 반대 방향(컴포넌트는 있는데 행이 없다)은 각 컴포넌트가 자기 BeginPlay 에서 잡는다.
	 * 양쪽이 서로를 확인하는 구조라 한쪽만 있어도 반드시 로그가 남는다.
	 *
	 * 표를 나누기 전에는 '수치가 기본값과 다른가' 로 추측했다. 지금은 행의 유무만 보면 되므로
	 * 이진 판정이고, 그래서 파손형까지 검사할 수 있게 됐다 (예전에는 거짓양성 때문에 포기했다).
	 *
	 * 컴포넌트가 자기 태그를 등록한 뒤에 불러야 한다 (Super::BeginPlay 이후).
	 */
	void WarnOnUnusedTypeData() const;

	/**
	 * 지금 인원이 필요 인원을 채웠는가.
	 *
	 * 속도와 점프가 같은 조건을 봐야 해서 한 곳에 둔다. 따로 쓰면 나중에 한쪽만 고쳐서
	 * "속도는 돌아왔는데 점프는 안 되는" 상태가 생긴다.
	 */
	bool HasEnoughCarriers() const;

	/**
	 * ApplyCarryState 가 한 번이라도 돌았는가.
	 *
	 * AppliedCarrier 만으로는 '아직 아무것도 반영 안 됨'과 '놓인 상태를 반영함'을
	 * 구별할 수 없다 — 둘 다 nullptr 이다. 그러면 BeginPlay 의 첫 호출이
	 * 조기 반환에 걸려 초기 상태 설정 자체가 통째로 날아간다.
	 */
	bool bCarryStateApplied = false;

	/**
	 * ApplyCarryState 가 마지막으로 반영한 운반자. 놓인 상태면 nullptr.
	 *
	 * PrimaryCarrier(지금 어떤 상태여야 하는가)와 짝을 이루는 '지금 어떤 상태인가'다.
	 * 둘이 같으면 할 일이 없다.
	 */
	TWeakObjectPtr<APawn> AppliedCarrier;

	/**
	 * 위와 같은 짝을 두 번째 운반자에 대해서도 둔다.
	 *
	 * 없으면 두 번째 사람이 붙거나 떨어질 때 ApplyCarryState 가 조기 반환에 걸린다.
	 * 첫 번째 운반자는 그대로이므로 "이미 같은 상태" 로 보이기 때문이다.
	 * 그러면 그 사람에게 이동 무시가 안 걸려서 자기가 잡은 물건에 막혀 못 움직인다.
	 */
	TWeakObjectPtr<APawn> AppliedSecondaryCarrier;

	/**
	 * [임시] 이번 G 입력이 다룰 노획물 하나를 고른다.
	 * 들고 있는 것이 있으면 그것(놓기), 없으면 범위 안에서 가장 가까운 것(집기).
	 */
	ALootBase* Debug_FindGrabTarget(const APawn* LocalPawn) const;

	/** [임시] 지금 이 플레이어가 들고 있는 노획물. 없으면 nullptr. T 키가 쓴다 */
	ALootBase* Debug_FindCarriedLoot(const APawn* LocalPawn) const;

	/**
	 * 카트에 실려 있는 동안 NoiseEmitter 에 걸어 둔 감쇄 규칙의 핸들.
	 *
	 * UNoiseEmitterComponent::AddModifier 가 "반환 핸들을 보관했다가 반드시 Remove 할 것"
	 * 이라고 요구한다. 안 지우면 카트에서 내린 물건이 남은 판 내내 조용하다.
	 */
	FGuid CartNoiseMuteHandle;

	/** OnHit 콜백이 온 총 횟수. 확정 횟수와 비교해 게이팅 효과를 본다 */
	int32 DebugRawHitCount = 0;

	/** 게이팅을 통과해 방송된 횟수 */
	int32 DebugConfirmedCount = 0;

	/** 대상별 마지막 확정 충격 시각. 키가 죽으면 정리된다 */
	TMap<TWeakObjectPtr<const AActor>, float> RecentImpactTimes;

	/** 상호 무시를 걸어 둔 운반자. 운반자가 바뀔 때 해제 대상을 놓치지 않기 위해 따로 들고 있는다 */
	TWeakObjectPtr<APawn> MoveIgnoredCarrier;

	/** 두 번째 운반자 몫. 이유는 위와 같다 */
	TWeakObjectPtr<APawn> MoveIgnoredSecondary;

	/**
	 * 놓기·던지기 직후 사람 통과 구간의 타이머.
	 *
	 * 액터가 먼저 파괴되면 타이머 매니저가 스스로 정리한다(대상 객체를 약참조로 든다).
	 * 그래서 EndPlay 에 해제 코드를 두지 않았다.
	 */
	FTimerHandle ReleaseIgnorePawnTimer;

	/** 마지막으로 이 노획물을 든 사람. PrimaryCarrier 와 달리 놓은 뒤에도 지워지지 않는다 */
	TWeakObjectPtr<APawn> LastCarrier;

	/** SetPendingImpactCause 로 예약된 원인. 확정 충격 1회에 소비된다 */
	ELootImpactCause PendingImpactCause = ELootImpactCause::Collision;

	/** 예약된 원인 제공자 */
	TWeakObjectPtr<APawn> PendingInstigatorPawn;
};
