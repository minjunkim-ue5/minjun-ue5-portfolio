#pragma once

#include "CoreMinimal.h"
#include "ThrowMath.generated.h"

class APawn;
class UPrimitiveComponent;
struct FPredictProjectilePathResult;

/**
 * 던지는 물건의 수치.
 *
 * [저장 위치가 둘로 갈린다]
 *   노획물은 DT_LootCatalog(FLootPhysicsData)에 이미 흩어져 있고, 계산 직전에
 *   MakeThrowParams 로 옮겨 담는다. 필드를 이 구조체로 옮기면 기존 행이 중첩 구조로
 *   바뀌면서 값을 잃기 때문이다.
 *
 *   장비는 표를 쓰지 않으므로 이 구조체를 그대로 BP 속성으로 둔다. 장비는 5종이
 *   각자 BP 하나씩이라, 표를 두면 조회 한 겹과 '잊어버릴 자리' 만 늘어난다.
 */
USTRUCT(BlueprintType)
struct FThrowParams
{
	GENERATED_BODY()

	/** 발사 속도 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw",
		meta = (ClampMin = "0.0", Units = "cm"))
	float Speed = 900.f;

	/** 조준 방향에 섞을 위쪽 성분. 포물선을 만든다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw",
		meta = (ClampMin = "0.0"))
	float UpwardRatio = 0.25f;

	/** 운반자의 이동 속도를 얼마나 더할지. 1 이면 조준이 무의미해진다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CarrierVelocityInfluence = 0.f;

	/** 날아가는 동안의 회전(도/초). 0 이면 미끄러지듯 날아가 던진 느낌이 안 난다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw",
		meta = (ClampMin = "0.0"))
	float SpinSpeed = 180.f;

	/** 발사 전 손에서 밀어내는 거리. 던진 사람 캡슐과의 겹침을 푼다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw",
		meta = (ClampMin = "0.0", Units = "cm"))
	float Clearance = 20.f;
};

/**
 * 던지기 수식. 노획물(ALootBase)과 장비(AEquipmentBase)가 공유한다.
 *
 * [왜 한 곳에 모으는가]
 *   서버의 임펄스와 클라이언트의 궤적 표시가 같은 계산을 써야 한다.
 *   따로 두면 미리 보이는 궤적과 실제로 날아가는 경로가 어긋나고,
 *   던지는 감을 튜닝할 때 두 군데를 고쳐야 한다.
 *
 *   노획물과 장비가 각자 수식을 들면 같은 문제가 파트 사이에서 생긴다 —
 *   같은 팔로 던졌는데 절단기가 꽃병과 다르게 날아간다.
 *
 * [경계] 여기 있는 것은 계산과 발사뿐이다. '언제 던지는가' 는 플레이어 파트가,
 *   '던질 수 있는가' 는 물건 자신(ICarryable::CanBeThrown)이 정한다.
 */
namespace HHThrow
{
	/** 조준점을 찾을 때 시선으로 쏘는 거리(cm) */
	inline constexpr float AimTraceDistance = 20000.f;

	/** 조준점이 이보다 가까우면 시선 방향을 그대로 쓴다(cm) */
	inline constexpr float MinAimDistance = 150.f;

	/**
	 * 지금 던진다면 어느 방향으로 나가야 하는가.
	 *
	 * 운반자의 시선을 그대로 쓰지 않는다. 발사점(손)이 화면 중앙에서 벗어나 있으면
	 * 시선 방향으로 던졌을 때 조준점 옆으로 날아가고, 멀수록 오차가 커진다.
	 * 카메라에서 트레이스해 조준점을 먼저 찾고, 발사점에서 그 지점을 향하게 한다.
	 *
	 * 운반자가 없으면 영벡터를 돌려준다.
	 */
	HEAVYHANDED_API FVector ComputeAimDirection(const AActor* Thrown, const APawn* Carrier);

	/** 조준 방향 + 포물선 성분 + 운반자 속도까지 합친 최종 발사 속도 */
	HEAVYHANDED_API FVector ComputeVelocity(const FVector& AimDirection,
		const FThrowParams& Params, const APawn* Carrier);

	/**
	 * 궤적을 예측한다. 조준 중인 클라이언트가 로컬로 그리는 표시용이다.
	 *
	 * 클라이언트 예측이 아니다 — 결과를 서버에 보내지 않고 판정은 서버가 다시 한다.
	 * 표시가 실제와 다르면 그건 표시가 틀린 것이지 게임 상태가 갈린 것이 아니다.
	 */
	HEAVYHANDED_API bool PredictPath(const AActor* Thrown, const APawn* Carrier,
		const FVector& AimDirection, const FThrowParams& Params,
		float ProjectileRadius, FPredictProjectilePathResult& OutResult);

	/**
	 * 실제로 발사한다 — 간격 확보 · 임펄스 · 회전. **서버에서만 부를 것.**
	 *
	 * 물리를 켜고 디태치하는 것은 부르는 쪽이 먼저 끝내야 한다. 물건마다
	 * 소지 상태를 관리하는 방식이 달라서 여기서 대신 해 줄 수 없다.
	 *
	 * @param Carrier  던진 사람. 이미 소지에서 풀린 뒤라도 속도 합산을 위해 넘긴다
	 */
	HEAVYHANDED_API void Launch(AActor* Thrown, UPrimitiveComponent* Body,
		const FVector& AimDirection, const FThrowParams& Params, const APawn* Carrier);

	/** 예측 궤적을 디버그 선으로 그린다. 최종 연출은 나중에 교체한다 */
	HEAVYHANDED_API void DrawTrajectory(const AActor* Thrown, const APawn* Carrier,
		const FVector& AimDirection, const FThrowParams& Params,
		float ProjectileRadius, float Duration);
}
