#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/HeavyHandedTypes.h"
#include "Carryable.generated.h"

class APawn;
class UPrimitiveComponent;

UINTERFACE(MinimalAPI)
class UCarryable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 플레이어가 들어 옮길 수 있는 대상.
 *
 * [파트 경계]
 *   플레이어 파트   : 입력, 트레이스, 소지 상태 관리, 이동 페널티 '적용'
 *   물리·아이템 파트 : 아래 판정과 데이터 '제공'
 *
 * 플레이어는 요청하고, 아이템이 허용/거부한다.
 * 판정 함수는 서버에서만 신뢰한다. (컨벤션 3-1)
 */
class HEAVYHANDED_API ICarryable
{
	GENERATED_BODY()

public:
	// [삭제됨] GetWeightClass()
	//   무게 등급(EWeightClass)을 없애면서 같이 뺐다. 읽는 곳이 한 군데도 없었다.
	//   "중량형인가" 는 Loot.Type.Heavy 태그로, "얼마나 느려지는가" 는 아래
	//   GetCarrySpeedMultiplier 로 알린다. 태그는 IGameplayTagAssetInterface 로 나간다.

	/** 필요한 최소 인원 (중량형 = 2) */
	virtual int32 GetRequiredCarriers() const = 0;

	/** 소지 중 이동 속도 배율 — 플레이어 파트가 이 값을 곱해서 적용한다 */
	virtual float GetCarrySpeedMultiplier() const = 0;

	/** 소지 중 점프 허용 여부 */
	virtual bool IsJumpAllowedWhileCarried() const = 0;

	/**
	 * 이 폰이 지금 들 수 있는가.
	 * 이미 다른 사람이 들고 있는지, 인원이 충족되는지 등을 검사한다.
	 */
	virtual bool CanBeCarriedBy(const APawn* Requester) const = 0;

	/** 잡힘 처리 (서버 전용). 물리 OFF + Attach 는 구현체 책임 */
	virtual void OnGrabbed(APawn* Carrier) = 0;

	/** 놓임 처리 (서버 전용). 물리 ON + 소음 발행 */
	virtual void OnReleased(APawn* Carrier) = 0;

	// ── 2인 캐리 ──────────────────────────────────────────────────────────
	//
	// 아래는 기본 구현을 준다. 대부분의 노획물은 1인 운반이고, 그때 답이 전부 자명하다.
	// 순수 가상으로 두면 중량형과 무관한 구현체까지 6개를 채워야 한다.
	//
	// [쓰는 법] 이미 누가 들고 있는 중량형에 다른 사람이 상호작용하면,
	//   CanBeSecondCarrierBy 로 물어보고 참이면 OnSecondGrabbed 를 부른다.
	//   놓기는 OnSecondReleased 다. 어느 쪽을 부를지는 GetPrimaryCarrier 와 비교해 고른다.

	/**
	 * 이 폰이 두 번째 운반자로 붙을 수 있는가.
	 *
	 * 중량형이 아니거나, 아직 아무도 안 들고 있거나, 이미 두 번째 자리가 찼거나,
	 * 요청자가 이미 첫 번째 운반자면 거짓이다.
	 */
	virtual bool CanBeSecondCarrierBy(const APawn* Requester) const { return false; }

	/** 두 번째 운반자로 붙는다 (서버 전용) */
	virtual void OnSecondGrabbed(APawn* Carrier) {}

	/** 두 번째 운반자가 손을 뗀다 (서버 전용) */
	virtual void OnSecondReleased(APawn* Carrier) {}

	/** 두 번째 운반자. 없으면 nullptr */
	virtual APawn* GetSecondaryCarrier() const { return nullptr; }

	/**
	 * 지금 몇 명이 들고 있는가 (0~2).
	 *
	 * 속도·점프 판정이 이 값을 쓴다. 세는 일을 물건이 하는 이유는, 두 사람이 각자 세면
	 * 서로 다른 답이 나올 수 있기 때문이다. 물건 하나가 정답을 정해서 양쪽에 같은 값을 준다.
	 */
	virtual int32 GetCarrierCount() const { return GetPrimaryCarrier() ? 1 : 0; }

	/**
	 * 이 사람이 잡아야 할 노획물 메시의 그립 소켓 이름. 해당 없으면 NAME_None.
	 *
	 * 두 사람이 양 끝을 잡게 하려면 각자 어느 쪽인지 알아야 한다.
	 * 첫 번째 운반자가 A, 두 번째가 B 로 고정이라 모든 머신에서 답이 같다.
	 */
	virtual FName GetGripSocketFor(const APawn* Carrier) const { return NAME_None; }

	/**
	 * 두 그립 사이의 거리(cm). 2인 캐리가 아니면 0.
	 *
	 * 메시의 두 소켓 위치에서 계산한다 — 거리 제약이 유지해야 할 목표 길이다.
	 * 값으로 적어 두지 않는 이유는 메시를 바꿨을 때 숫자만 옛 메시에 맞은 채 남기 때문이다.
	 */
	virtual float GetGripSeparation() const { return 0.f; }
	// ── 2인 캐리 끝 ───────────────────────────────────────────────────────

	/** 던질 수 있는가. 중량형처럼 놓기만 되는 물건이 있다 */
	virtual bool CanBeThrown() const = 0;

	/**
	 * 지금 던진다면 어느 방향으로 나가야 하는가. 들려 있지 않으면 영벡터.
	 *
	 * 운반자의 시선을 그대로 쓰면 안 된다. 발사점(손)이 화면 중앙에서 벗어나 있어서
	 * 시선 방향으로 던지면 조준점 옆으로 날아가고, 목표가 멀수록 오차가 커진다.
	 * 보정에 필요한 발사점은 물건만 알기 때문에 이 계산이 아이템 쪽에 있다.
	 *
	 * 플레이어 파트는 '언제 던지는가' 만 정하고 이 값을 그대로 OnThrown 에 넘긴다.
	 * 인터페이스에 두는 이유는 플레이어 파트가 ALootBase 를 include 하지 않게 하기 위해서다.
	 */
	virtual FVector ComputeThrowAimDirection() const = 0;

	/**
	 * 던짐 처리 (서버 전용). 놓기와 달리 초기 속도를 준다.
	 *
	 * AimDirection 은 플레이어 파트가 넘기는 조준 방향(정규화 전이어도 된다).
	 * 세기·포물선·회전은 아이템이 자기 데이터로 결정한다 — 플레이어는 방향만 준다.
	 * 던질 수 없는 물건이면 제자리에 놓는 것으로 처리한다.
	 */
	virtual void OnThrown(APawn* Carrier, const FVector& AimDirection) = 0;

	/** 현재 이 노획물을 들고 있는 대표(리더). 없으면 nullptr */
	virtual APawn* GetPrimaryCarrier() const = 0;

	/** 물리 바디. 플레이어 파트가 Attach 대상으로 사용한다 */
	virtual UPrimitiveComponent* GetPhysicsRoot() const = 0;
};
