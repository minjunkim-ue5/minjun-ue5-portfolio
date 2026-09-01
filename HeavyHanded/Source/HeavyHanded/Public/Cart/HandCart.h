#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HandCart.generated.h"

class ALootBase;
class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * 핸드카트(대차). 노획물을 담아 옮기는 장비. (기획서 7장 — $20,000)
 *
 * [카트를 사는 이유는 '인원을 푸는 것' 이다]
 *   중량형은 여전히 2인이어야 들린다. 카트가 그 규칙을 깨지 않는다.
 *   두 사람이 함께 들어서 카트에 싣고, 그 뒤로는 한 명이 끌고 간다.
 *   카트가 없으면 두 명이 밴까지 계속 붙잡혀 있어야 하는데, 있으면 한 명이 풀려난다.
 *   기획서의 "중량형을 1인이 밀어서 운반" 은 운반이 1인이라는 뜻이지
 *   적재까지 1인이라는 뜻이 아니다. (2026-08-20 결정)
 *
 * [담긴 물건은 물리를 유지한다 — 어태치하지 않는다]
 *   들고 있을 때(ALootBase::ApplyCarryState)는 물리를 끄고 붙이지만 카트는 반대다.
 *   물건이 카트 안에서 계속 흔들리고, 험하게 몰면 밖으로 쏟아진다.
 *   그 '쏟아짐' 이 카트의 유일한 위험 요소라서, 물리를 끄면 게임이 사라진다.
 *
 *   대신 덜그럭거리는 소리와 파손을 막아야 한다. 안 막으면 소음을 줄이려고 산 장비가
 *   소음 발생기가 되고, 파손형은 타고 가는 것만으로 깨진다.
 *   → ALootBase::SetContainingCart 가 그 두 가지를 끈다.
 *
 * [노획물 3종이 카트와 각각 다른 관계를 갖는다]
 *   파손형     안 깨진다      (충격 보고를 끄므로 누적되지 않는다)
 *   불안정형   샌다          (유출은 기울기로 판정하지 벽 충돌로 판정하지 않는다.
 *                            그래서 아무것도 안 해도 살아 있다 — 의도한 것이다)
 *   중량형     실을 수 있다   (단, 싣는 데 2인)
 *
 * [벽에 막히면 미는 사람도 막힌다]
 *   카트는 물리 바디이고 Pawn 채널을 Block 한다. 운반자에게 이동 무시를 걸어 주는
 *   노획물과 정반대다 — 노획물은 자기가 든 물건에 막히면 안 되지만,
 *   카트는 막혀야 "좁은 통로 불가" 라는 기획서상 유일한 단점이 성립한다.
 *   그래서 IgnoreActorWhenMoving 을 걸지 않는다. 콜리전이 알아서 한다.
 *
 * 서버 권위 + 클라이언트 보간. 노획물과 같은 정책이다.
 */
UCLASS(Blueprintable)
class HEAVYHANDED_API AHandCart : public AActor
{
	GENERATED_BODY()

public:
	AHandCart();

	/** 지금 이 카트에 실려 있는 노획물. 서버에서만 채워진다 */
	const TArray<TObjectPtr<ALootBase>>& GetContainedLoot() const { return ContainedLoot; }

	UFUNCTION(BlueprintPure, Category = "Cart")
	int32 GetContainedCount() const { return ContainedLoot.Num(); }

	UFUNCTION(BlueprintPure, Category = "Cart")
	bool IsContaining(const ALootBase* Loot) const;

	/**
	 * 이 노획물을 적재 목록에서 뺀다. (서버 전용)
	 *
	 * 볼륨을 벗어나면 저절로 빠지지만, 적재면 위에서 그대로 집어 올리는 경우가 있다.
	 * 그때는 볼륨 안에 머문 채 사람 손에 들리므로 EndOverlap 이 오지 않는다.
	 * 그래서 ALootBase::OnGrabbed 가 이 함수를 직접 부른다.
	 */
	void ReleaseLoot(ALootBase* Loot);

	/**
	 * 이 노획물을 적재 목록에 넣는다. (서버 전용)
	 *
	 * 보통은 LoadVolume 오버랩이 알아서 부르지만, 들고 들어와서 놓는 경우에는
	 * 진입 시점에 손에 들려 있어 거부되고 놓는 시점에는 오버랩이 다시 오지 않는다.
	 * 그 구멍을 ALootBase::TryContainInOverlappingCart 가 이 함수를 직접 불러 메운다.
	 * ReleaseLoot 과 짝이다.
	 */
	void ContainLoot(ALootBase* Loot);

	// ── 끌기 ─────────────────────────────────────────────────────────────

	/**
	 * 상호작용 키로 카트를 잡거나 놓는다. (서버 전용 — 플레이어 파트가 부르는 유일한 진입점)
	 *
	 * [왜 이 모양인가]
	 *   UGAB_Interact 는 시선에 맞은 액터를 if-else 사슬로 분기하는데, 그 파일은 플레이어 파트다.
	 *   AVanZone 이 TryToggleBoarding 하나로 처리되는 것과 같은 모양으로 맞춰서,
	 *   저쪽에 들어갈 코드가 두 줄로 끝나게 한다.
	 *
	 *       else if (AHandCart* Cart = Cast<AHandCart>(HitActor))
	 *       {
	 *           Cart->TryTogglePush(Character);
	 *       }
	 *
	 *   잡을 수 있는지, 이미 누가 잡고 있는지, 어떻게 따라가는지는 전부 이 클래스 안에 있다.
	 *   Server RPC 는 요청일 뿐이므로 판정은 여기서 한다 — 클라이언트를 신뢰하지 않는다.
	 */
	void TryTogglePush(APawn* Pawn);

	/**
	 * 끌기를 강제로 푼다. (서버 전용)
	 *
	 * 플레이어가 다운되거나 체포되는 등, 카트가 스스로 알 수 없는 이유로 손을 놓아야 할 때
	 * 플레이어 파트가 부른다. 카트는 거리와 유효성까지만 스스로 본다 —
	 * 폰의 상태 태그를 카트가 들여다보기 시작하면 경계가 무너진다.
	 */
	void StopPush();

	/** 지금 이 카트를 끌고 있는 사람. 없으면 nullptr */
	UFUNCTION(BlueprintPure, Category = "Cart|Push")
	APawn* GetPusher() const { return CurrentPusher; }

	UFUNCTION(BlueprintPure, Category = "Cart|Push")
	bool IsBeingPushed() const { return CurrentPusher != nullptr; }

	/**
	 * 손잡이 그립의 월드 트랜스폼. bLeft 가 참이면 왼손 쪽.
	 *
	 * 손 IK 나 붙이기 연출에 쓰라고 열어 둔다. 소켓이 없으면 카트 원점을 돌려주므로
	 * 반환값만 보고는 설정 실수를 알 수 없다 — 그건 BeginPlay 경고가 잡는다.
	 */
	UFUNCTION(BlueprintPure, Category = "Cart|Push")
	FTransform GetGripTransform(bool bLeft) const;

	/** 카트를 끌 때 사람이 서게 되는 지점. 그립 중점에서 손잡이 바깥으로 물러난 자리다 */
	UFUNCTION(BlueprintPure, Category = "Cart|Push")
	FVector GetStandLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 물리 바디이자 루트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cart")
	TObjectPtr<UStaticMeshComponent> CartMesh;

	/**
	 * 적재면 위 공간. 여기 들어온 노획물을 '실린 것' 으로 센다.
	 *
	 * 메시가 임시라 기본값은 대략치다. 실제 카트 메시가 들어오면 BP 에서 적재면에 맞춘다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cart")
	TObjectPtr<UBoxComponent> LoadVolume;

	/** 이 세기 미만의 충돌은 소음으로 치지 않는다. 밀고 다닐 때의 미세 접촉을 거른다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise", meta = (ClampMin = "0.0"))
	float NoiseImpulseThreshold = 600.f;

	/** 이 세기면 프로파일 기본 크기를 그대로 낸다. 그 아래는 비례해 줄어든다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise", meta = (ClampMin = "1.0"))
	float NoiseFullImpulse = 4000.f;

	/** 같은 대상에 대해 이 시간 안에는 다시 발행하지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise", meta = (ClampMin = "0.0", Units = "s"))
	float NoiseDebounceSeconds = 0.3f;

	/**
	 * 충격 방향이 이만큼 수직에 가까우면(|Z| 성분) 소음으로 치지 않는다.
	 *
	 * [왜 필요한가] 물건을 카트에 실으면 그 하중이 바퀴를 통해 바닥으로 전달되면서
	 *   강한 수직 충격이 잡힌다. 실제로 불안정형을 넣었을 때 물건 쪽 4409 에 이어
	 *   카트-바닥 4080 이 찍혔다. 상대가 노획물이 아니라 Floor 라서 적재 목록 검사에
	 *   걸리지 않고 그대로 소음이 됐다 — 조용히 옮기려고 산 장비가 실을 때마다 소리를 낸 것이다.
	 *
	 *   벽에 박는 충격은 벽 법선 방향이라 수평이다. 그래서 방향으로 갈린다.
	 *
	 * [한계] 카트를 높은 데서 떨어뜨리거나 계단에서 굴리면 그 착지음도 같이 죽는다.
	 *   지금은 계단 대응 자체가 없어서 문제가 안 되지만, 계단이 들어오면 낙하 속도로
	 *   예외를 두는 편이 낫다. 속도로 판정하지 않은 것은 OnHit 시점의 속도가 이미
	 *   충돌로 감속된 뒤라서, 벽에 세게 박을수록 오히려 느리게 보이기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoiseVerticalImpactCutoff = 0.7f;

	// ── 끌기 설정 ────────────────────────────────────────────────────────

	/** 왼손 그립 소켓 이름. CartMesh 의 스태틱 메시에 있어야 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push")
	FName GripSocketLeft = TEXT("Push_Grip_L");

	/** 오른손 그립 소켓 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push")
	FName GripSocketRight = TEXT("Push_Grip_R");

	/** 그립 중점에서 사람이 서는 자리까지의 거리(cm). 팔 길이쯤 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push", meta = (ClampMin = "0.0", Units = "cm"))
	float StandOffset = 75.f;

	/**
	 * 메시의 정면이 로컬 +X 에서 몇 도 돌아가 있는가.
	 *
	 * 카트를 사람 시선 방향으로 놓을 때 이 값만큼 되돌린다. 임포트된 메시의 축이 무엇을
	 * 정면으로 삼았는지는 만든 사람마다 달라서, 코드가 알 방법이 없다.
	 *
	 * 축이 틀어지면 카트가 옆으로 보이는 데서 그치지 않는다. 그립 위치를 역산하는 계산도
	 * 같이 틀어져서 목표 지점이 사람 몸 안쪽으로 잡히고, 카트는 Pawn 을 Block 하므로
	 * 물리 엔진이 겹침을 풀려고 카트를 위로 밀어낸다 — 실제로 그 증상이 나왔다.
	 *
	 * 0 / 90 / -90 / 180 중에서 화면을 보며 맞는 값을 고르면 된다. (2026-08-21)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float MeshForwardYawOffset = 0.f;

	/**
	 * 목표 지점으로 얼마나 세게 당길지. 클수록 사람 움직임에 딱 붙는다.
	 *
	 * 너무 키우면 벽에 낀 상태에서 카트가 부들부들 떨고, 너무 낮추면 사람만 앞서 나가고
	 * 카트가 뒤늦게 따라온다. 12 안팎에서 시작한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push", meta = (ClampMin = "0.1"))
	float FollowStiffness = 12.f;

	/**
	 * 따라가는 속도 상한(cm/s).
	 *
	 * [순간이동을 하지 않는 이유] 위치를 대입하면 카트가 벽을 뚫고 사람 몸에 박힌다.
	 *   속도로 밀면 물리 솔버가 벽에서 막아 주고, 그 막힘이 그대로 미는 사람에게 전달된다 —
	 *   기획서상 카트의 유일한 단점인 "좁은 통로 불가" 가 여기서 나온다.
	 *   상한을 두는 것은 한 프레임에 너무 멀리 뛰어 벽을 통과하는 것을 막기 위해서다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push", meta = (ClampMin = "1.0"))
	float MaxFollowSpeed = 600.f;

	/** 카트가 사람 시선 방향으로 도는 속도 계수. 클수록 빠르게 정렬된다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push", meta = (ClampMin = "0.1"))
	float TurnStiffness = 8.f;

	/**
	 * 이 거리(cm)보다 멀어지면 손을 놓는다.
	 *
	 * 벽 뒤로 돌아가거나 낙사해서 카트와 떨어졌을 때 카트가 벽을 긁으며 따라오는 것을 막는다.
	 * 잡은 채로 뒷걸음질하는 정상 조작까지 끊지 않도록 넉넉히 잡는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxPushDistance = 300.f;

	/** 카트 자체의 질량(kg). 실린 물건 무게는 물리 엔진이 따로 더한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Physics", meta = (ClampMin = "1.0"))
	float MassKg = 60.f;

	/**
	 * 앞뒤·좌우로 넘어지는 것을 막는다.
	 *
	 * 물리 바디라 급회전하면 뒤집힌다. 재밌을 수도 있지만 처음부터 열어 두면
	 * "왜 자꾸 뒤집히지" 로 시간을 쓴다. 잠가 두고 나중에 풀어 보는 편이 낫다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Physics")
	bool bLockTipping = true;

private:
	UFUNCTION()
	void HandleLoadBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleLoadEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/**
	 * 카트 몸체가 무언가에 부딪혔다. 벽에 박은 것만 소음으로 낸다.
	 *
	 * [UNoiseEmitterComponent 를 안 쓰고 직접 거는 이유]
	 *   그 컴포넌트는 자기가 알아서 모든 OnComponentHit 을 잡는다. 편해서 노획물에는 그대로
	 *   붙였지만 카트에는 못 쓴다 — 실려 있는 물건이 카트 바닥에 부딪히는 것까지 소음이 되고,
	 *   그러면 물건 쪽 소음을 아무리 막아도 카트가 대신 시끄럽다.
	 *   무엇에 부딪혔는지를 봐야 하는데 그 판단을 끼워 넣을 자리가 저쪽에는 없다.
	 *
	 *   그래서 여기서 걸러 UNoiseSubsystem::ReportNoise 로 직접 보낸다.
	 *   임계값과 재발행 차단은 ALootBase 가 쓰는 것과 같은 두 겹 구조다.
	 */
	UFUNCTION()
	void HandleCartHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** 이 충돌을 소음으로 칠 것인가. 실려 있는 물건과 사람은 제외한다 */
	bool ShouldReportHitAsNoise(const AActor* OtherActor) const;

	/** 대상별 마지막 발행 시각. 짧은 시간 내 재발행을 막는다 */
	TMap<TWeakObjectPtr<const AActor>, float> RecentNoiseTimes;

	/**
	 * 끌고 있는 사람. 복제한다 — 클라이언트도 "지금 누가 잡고 있나" 를 알아야
	 * 손 붙이기 연출을 각자 돌릴 수 있다. 판정은 서버만 한다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentPusher, VisibleInstanceOnly, Category = "Cart|Push")
	TObjectPtr<APawn> CurrentPusher;

	UFUNCTION()
	void OnRep_CurrentPusher();

	/** 매 프레임 카트를 사람 앞으로 당긴다. 서버에서만 돈다 */
	void UpdateFollow(float DeltaSeconds);

	/** 사람의 위치·시선으로부터 카트가 있어야 할 자리를 구한다. 못 구하면 false */
	bool ComputeFollowTarget(FVector& OutLocation, FQuat& OutRotation) const;

	/**
	 * 그립 소켓이 없으면 경고한다. (서버 전용)
	 *
	 * 이름이 틀리면 그립 위치가 조용히 카트 원점으로 떨어진다. 그러면 카트가 사람 몸에
	 * 겹치려 들면서 서로 밀어내는 엉뚱한 증상으로만 드러나 원인까지 가는 데 한참 걸린다.
	 *
	 * 중량형과 달리 간격은 보지 않는다 — 거기서는 그립 간격이 곧 두 사람 사이의 거리 제약이라
	 * 0 이면 기능이 성립하지 않았지만, 카트는 한 사람이 두 손으로 잡는 것이라
	 * 간격이 아무것도 결정하지 않는다.
	 */
	void WarnOnMissingGripSockets() const;

	/**
	 * 실려 있는 노획물. 서버에서만 유효하다.
	 *
	 * 복제하지 않는 이유: 이 목록으로 하는 일(소음 억제 · 파손 제외)이 전부 서버 판정이다.
	 * 클라이언트가 "이 물건이 카트에 실렸는가" 를 알아야 할 때는 노획물 쪽
	 * ALootBase::ContainingCart 가 복제되므로 그것을 본다.
	 *
	 * UPROPERTY 가 없으면 GC 가 회수한 뒤 엉뚱한 곳에서 크래시한다.
	 */
	UPROPERTY()
	TArray<TObjectPtr<ALootBase>> ContainedLoot;
};
