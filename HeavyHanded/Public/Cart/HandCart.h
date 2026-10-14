#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/Interactable.h"   // 부모 인터페이스 — 전방 선언 불가
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
 * [카트를 옮기는 것은 잡은 사람뿐이다]
 *   Pawn 을 Block 하는 것은 위 이유로 그대로 두지만, 몸으로 밀어서 옮기지는 못한다.
 *   잡지 않은 동안은 IdleMassKg 로 무거워지고 IdlePushDrag 가 속도를 계속 빼내서,
 *   부딪히면 뭉그적거리며 조금 밀리는 정도로 끝난다. 다가가기만 해도 멀리 날아가던
 *   문제를 고친 것이다 (원인은 IdleMassKg 주석 참고).
 *
 *   잡고 있는 동안은 UpdateFollow 가 매 프레임 속도를 '대입' 하므로 남이 밀어 넣은
 *   속도가 한 프레임 살고 지워진다 — 그쪽은 원래부터 보호돼 있었다. (2026-09-08 팀 결정)
 *
 * 서버 권위 + 클라이언트 보간. 노획물과 같은 정책이다.
 */
UCLASS(Blueprintable)
class HEAVYHANDED_API AHandCart : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AHandCart();

	/**
	 * IInteractable — 상호작용 키(E)로 끌기를 시작하거나 놓는다.
	 *
	 * 카트는 '드는' 물건이 아니라 '미는' 물건이라 ICarryable 이 아니다.
	 * 그래서 버리기(Q)나 던지기도 없다 — 놓는 것도 E 를 다시 누르는 것이다.
	 *
	 * UGAB_Interact 가 IInteractable 구현체를 종류를 모른 채 처리하므로,
	 * 이걸 구현하는 것만으로 플레이어 파트를 고치지 않고 E 가 붙는다.
	 */
	virtual void OnInteract_Implementation(APawn* Interactor) override;

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
	 * 상호작용 키로 카트를 잡거나 놓는다. (서버 전용)
	 *
	 * OnInteract_Implementation 이 부르는 실제 판정부다. 잡을 수 있는지, 이미 누가 잡고
	 * 있는지, 어떻게 따라가는지는 전부 이 클래스 안에 있다.
	 * Server RPC 는 요청일 뿐이므로 판정은 여기서 한다 — 클라이언트를 신뢰하지 않는다.
	 *
	 * [예전에는 플레이어 파트에 분기를 추가해 달라고 할 생각이었다]
	 *   UGAB_Interact 의 if-else 사슬에 Cast<AHandCart> 를 한 줄 넣는 방식이었는데,
	 *   그 사이 IInteractable 이 생기면서 저쪽이 구현체를 종류를 모른 채 처리하게 됐다.
	 *   그래서 요청 없이 이쪽에서 인터페이스만 구현하는 것으로 끝났다.
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

	/**
	 * 부딪힌 면을 향해 이 속도(cm/s) 이상으로 다가가고 있었을 때만 소음으로 친다.
	 *
	 * [무엇을 재는가 — '닫히는 속도']
	 *   ApproachSampleSeconds 창 동안의 실제 이동 속도를, 부딪힌 면의 법선에 투영한 값이다.
	 *   즉 "이 면을 향해 얼마나 빠르게 다가갔나". 판정 기준은 이것 하나뿐이다.
	 *
	 *     평지 주행   이동 (600,0,0)  법선 (0,0,1)   → 0     바닥은 다가간 게 아니다
	 *     벽에 박기   이동 (600,0,0)  법선 (-1,0,0)  → 600
	 *     벽 스치기   이동이 벽과 평행                → ~0    긁는 것은 부딪힘이 아니다
	 *     낙하 착지   이동 (0,0,-400) 법선 (0,0,1)   → 400   이건 나야 맞다
	 *
	 * [순간 속도와 임펄스는 둘 다 못 쓴다 — 실측]
	 *   벽에 대고 미는 동안 순간 속도가 408~902cm/s 로 찍혔다(2026-09-08 로그).
	 *   끌기 최고 속도가 MaxFollowSpeed(600) 인데 902 가 나온다 — 우리가 넣을 수 없는 값이고,
	 *   눌린 카트를 솔버가 침투 해소로 튕겨내는 속도다. 눌린 카트는 느리지 않고 오히려 빠르다.
	 *   임펄스도 같다(직전 61cm/s 프레임에 임펄스 환산 13,706).
	 *
	 *   위치 변화는 그 영향을 받지 않는다. 진동은 거리에서 상쇄되고, 실제로 이동해 온 것만 남는다.
	 *
	 * [법선은 NormalImpulse 가 아니라 Hit.ImpactNormal 이다]
	 *   NormalImpulse 에는 마찰이 섞여 있다. 600cm/s 로 굴러가면 마찰 임펄스가 수평으로 커서
	 *   방향이 수평으로 잡히고, 바닥이 '벽에 박은 것' 처럼 통과한다 — 실제로 그 증상이 나왔다.
	 *   Hit.ImpactNormal 은 순수 기하 법선이라 바닥은 항상 수직이다.
	 *   법선 방향은 항상 카트를 향한다(FRigidBodyContactInfo::SwapOrder 가 받는 쪽 기준으로 뒤집는다).
	 *
	 * [문턱값 근거] 전속(600)으로 박으면 창 동안 72cm 를 지나 600 으로 잡히고,
	 *   눌린 상태는 2~5cm 라 17~40 으로 잡힌다. 300 이면 그 사이에 넉넉히 들어간다.
	 *
	 * [부작용] 멈춰 있는 카트에 노획물을 던져 맞혀도 카트 쪽 소음은 안 난다.
	 *   물건 쪽 UNoiseEmitterComponent 가 자기 충격을 따로 발행하므로 소리 자체는 나고,
	 *   오히려 같은 사건을 두 번 알리지 않게 된다.
	 *
	 * 0 이면 이 검사를 건너뛴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise",
		meta = (ClampMin = "0.0", Units = "CentimetersPerSecond"))
	float NoiseMinApproachSpeed = 300.f;

	/**
	 * 이 속도로 다가가 부딪히면 프로파일 기본 크기를 그대로 낸다. 그 아래는 비례해 줄어든다.
	 *
	 * 끌 때 낼 수 있는 최고 속도(MaxFollowSpeed)에 맞춰 둔다 — 전속으로 박은 것이
	 * 가장 시끄러운 경우다. 문턱(300)에서 0.5, 여기(600)에서 1.00 이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise",
		meta = (ClampMin = "1.0", Units = "CentimetersPerSecond"))
	float NoiseLoudSpeed = 600.f;

	/**
	 * 이동 속도를 재는 창의 길이(초).
	 *
	 * 짧으면 진동이 상쇄되지 않고, 길면 부딪히는 순간의 속도를 놓친다.
	 * 0.12 면 전속(600)에서 72cm 를 지나므로 진동(수 cm)과 확실히 갈린다.
	 * 값이 갱신되는 주기이기도 하므로, 이 시간만큼은 직전 창의 값을 그대로 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise",
		meta = (ClampMin = "0.02", ClampMax = "0.5", Units = "s"))
	float ApproachSampleSeconds = 0.12f;

	/**
	 * 이 카트의 충돌 판정을 화면과 로그에 찍는다. (ALootBase::bShowImpactDebug 와 같은 용법)
	 *
	 * 기본값에서는 소음으로 확정된 것만 찍는다. 기각까지 보려면 bShowRejectedImpacts 를 켠다.
	 * 소음의 출처가 카트가 아닐 수도 있으므로(플레이어 발소리 등) 같이 hh.Noise.Debug 1 을 켠다.
	 *
	 * 인스턴스별 스위치라 레벨에 카트가 여러 대여도 하나만 볼 수 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Debug")
	bool bShowImpactDebug = false;

	/**
	 * 기각된 충돌까지 전부 찍는다. bShowImpactDebug 가 켜져 있어야 의미가 있다.
	 *
	 * 기각은 확정보다 압도적으로 자주 나온다 — 벽에 대고 있으면 바닥·벽·문에서 매 프레임
	 * 서너 줄씩 들어와서, 정작 봐야 할 확정 한 줄이 화면 밖으로 밀려난다.
	 * 문턱값을 조정할 때처럼 '왜 걸러졌는가' 를 봐야 할 때만 켠다. (ALootBase 와 같은 구조)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Debug")
	bool bShowRejectedImpacts = false;

	/** 같은 대상에 대해 이 시간 안에는 다시 발행하지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Noise", meta = (ClampMin = "0.0", Units = "s"))
	float NoiseDebounceSeconds = 0.3f;

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
	 * 아무도 잡고 있지 않은 동안의 질량(kg). 몸으로 밀 때 튀어 나가지 않게 한다.
	 *
	 * [왜 필요한가] 사람이 카트에 닿기만 해도 멀리 날아가는 문제가 있었다. 원인은
	 *   UCharacterMovementComponent 의 밀기 힘이다 — 접촉이 유지되는 매 프레임
	 *   PushForceFactor(750,000)가 들어오는데, 질량으로 나뉘지도 속도로 감쇠되지도 않는다.
	 *   (감쇠 코드가 `if (Dot > 0 && Dot < 1)` 인데 Dot 은 cm/s 두 벡터의 내적이라
	 *    수천~수만이 나와 그 창에 절대 들어가지 못한다. UE 5.4 기준)
	 *   60kg 에서 가속이 125m/s² 라 한 프레임에 2m/s 가 붙는다.
	 *
	 * [IdlePushDrag 와 짝이다]
	 *   질량만 올려서는 밀리는 정도를 정할 수 없다. 마찰 감속(약 686cm/s²)은 질량과 무관한데
	 *   밀기 가속은 질량에 반비례해서, 1,093kg 부근에서 둘이 만난다 — 그보다 가벼우면
	 *   여전히 빠르고 무거우면 아예 안 밀린다. 중간이 없다.
	 *
	 *   그래서 질량은 '관성' 만 맡고, 속도는 IdlePushDrag 가 정한다.
	 *   여기 값은 밀기 가속이 마찰보다 넉넉히 크게 남을 만큼만 올린다 —
	 *   너무 올리면 바닥 재질이 바뀔 때 '안 밀림' 으로 넘어가 버린다.
	 *
	 * [중력은 질량과 무관하다]
	 *   그래서 낙하·정착이 정상으로 남는다. 물리를 끄는(Kinematic) 방법을 쓰지 않은 이유가
	 *   이것이다 — 그쪽은 공중이나 경사에서 놓으면 그 자리에 뜬 채로 멈추고,
	 *   무엇보다 접촉이 힘을 전혀 못 주므로 '약하게 밀림' 자체가 성립하지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Physics", meta = (ClampMin = "1.0"))
	float IdleMassKg = 400.f;

	/**
	 * 아무도 잡고 있지 않은 동안 수평 속도에 걸리는 저항(1/초). 0 이면 걸지 않는다.
	 *
	 * [상한이 아니라 저항인 이유 — 이게 핵심이다]
	 *   처음에는 속도 상한으로 막았는데 "천천히 밀리다가 갑자기 살짝 빠르게" 라는 보고가 나왔다.
	 *   상한은 넘친 만큼을 '잘라내는' 방식이라, 물리가 프레임 안에서 붙인 속도와 우리가
	 *   잘라낸 값 사이를 매 프레임 톱니처럼 오간다. 사람 몸이 붙었다 떨어졌다 하면 그 톱니의
	 *   평균이 프레임마다 달라져서 불규칙하게 느껴진다. 램프를 걸어도 구조가 같아 남았다.
	 *
	 *   저항은 잘라내지 않고 매 프레임 비례해서 빼낸다. 밀리는 방식은 물리 그대로이고
	 *   정도만 약해지므로 톱니가 생기지 않는다. 미는 힘과 저항이 만나는 지점에서
	 *   속도가 저절로 멎고, 손을 떼면 같은 시간 상수로 스스로 잦아든다.
	 *
	 * [값의 의미] 도달 속도 ≈ (밀기 가속 - 마찰 감속) / 이 값.
	 *   400kg 기준 밀기 1,875cm/s² 에서 마찰 686 을 빼면 1,189 이고, 15 로 나누면 약 79cm/s 다.
	 *   시간 상수는 1/15 = 0.067초 — 밀면 곧 일정 속도가 되고 떼면 잠깐 미끄러지다 선다.
	 *   끌 때가 MaxFollowSpeed(600) 이므로 그 1/8 이다. 몸으로도 눈에 보이게 밀리지만
	 *   옮기는 수단으로 쓰기에는 느려서, "카트는 잡고 끄는 것" 이라는 규칙이 흐려지지 않는다.
	 *
	 *   [조정 방향] 너무 잘 밀리면 올리고(20, 30) 안 밀리면 내린다(10, 8).
	 *   질량은 건드리지 않는 편이 낫다 — 1,093kg 부근에서 마찰과 만나 '안 밀림' 으로 넘어간다.
	 *
	 * [Z 는 건드리지 않는다] 수평에만 걸어야 한다. 엔진의 LinearDamping 을 쓰지 않은 이유가
	 *   이것이다 — 그건 낙하까지 같이 눌러서(낙하 종단속도 = 980÷담핑) 카트가 깃털처럼 떨어진다.
	 *
	 * 프레임 시간에 무관하게 같은 결과가 나오도록 지수 감쇠로 적용한다.
	 * (곱셈으로 하면 저사양에서 계수가 1 을 넘어 속도가 반대로 튄다)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Physics", meta = (ClampMin = "0.0"))
	float IdlePushDrag = 8.f;

	/**
	 * 끌고 있는 동안 카트가 위로 솟을 수 있는 속도 상한(cm/s). 0 이면 제한하지 않는다.
	 *
	 * [왜 필요한가] 끌고 다니는 중에 카트가 중간중간 위로 살짝 튀어 오르는 문제가 있다.
	 *   UpdateFollow 는 수평 속도를 매 프레임 '대입' 하는데, 바닥 이음새나 콜리전 모서리에
	 *   걸려도 그 대입을 멈추지 않는다. 계속 밀어붙이니 솔버가 겹침을 위로 풀어내고,
	 *   그렇게 생긴 상승 속도를 우리가 Z 성분이라는 이유로 그대로 보존해 왔다.
	 *   솟는 것만 막으면 눈에 보이는 증상이 사라진다.
	 *
	 * [내려가는 쪽은 건드리지 않는다] 중력과 낙하는 그대로여야 한다. 상한은 +Z 에만 붙는다.
	 *
	 * [경사로가 들어오면 다시 봐야 한다] 램프를 600cm/s 로 오르면 기울기 20도에서
	 *   Z 가 205cm/s 쯤 필요하다. 지금은 경사로가 없어서 문제가 안 되지만, 생기면
	 *   이 값을 수평 속도와 걷기 가능 기울기에서 계산하는 편이 맞다.
	 *
	 * [이건 증상 억제다] 근본 원인이 콜리전 형상이면 그쪽을 고쳐야 한다.
	 *   0 으로 두고 튀는지 보면 원인이 어디인지 갈린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Push",
		meta = (ClampMin = "0.0", Units = "CentimetersPerSecond"))
	float MaxRiseSpeedWhilePushed = 60.f;

	/**
	 * 손을 놓은 뒤 이 시간(초) 동안은 속도 상한을 걸지 않는다.
	 *
	 * 600cm/s 로 밀던 카트를 놓는 순간 40 으로 깎으면 기세가 뚝 끊겨 어색하다.
	 * 이 구간에는 마찰이 알아서 감속시키고, 지나면 상한이 붙는다.
	 * 질량은 놓는 즉시 IdleMassKg 로 바꾼다 — 그래야 이 구간에 몸으로 밀어도 덜 튄다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cart|Physics",
		meta = (ClampMin = "0.0", ClampMax = "3.0", Units = "s"))
	float ReleaseCoastSeconds = 0.4f;

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

	/** bShowImpactDebug 가 켜져 있으면 화면과 로그에 한 줄 남긴다 */
	void ShowImpactDebug(const FString& Message, const FColor& Color, const FVector& Location) const;

	/** 기각 사유. bShowRejectedImpacts 까지 켜져 있을 때만 나온다 */
	void ShowRejectDebug(const FString& Message, const FColor& Color, const FVector& Location) const;

	/** 대상별 마지막 발행 시각. 짧은 시간 내 재발행을 막는다 */
	TMap<TWeakObjectPtr<const AActor>, float> RecentNoiseTimes;

	/**
	 * 손을 놓은 뒤 남은 무상한 구간(초). 0 이하면 상한이 붙는다.
	 *
	 * 복제하지 않는다 — 놓는 시점(CurrentPusher 가 null 이 되는 순간)이 양쪽에 전달되므로
	 * 각자 자기 타이머를 돌리면 된다. 이 값 자체는 물리 감각이지 판정이 아니다.
	 */
	float ReleaseCoastRemaining = 0.f;

	/**
	 * 지금 바디에 들어가 있는 질량(kg). ApplyPushState 가 헛일하지 않게 비교용으로 둔다.
	 *
	 * 0 으로 시작하므로 첫 호출은 반드시 반영된다. 질량 변경은 관성 텐서를 다시 계산하므로
	 * 매 틱 같은 값을 넣지 않는 편이 낫다.
	 */
	float AppliedMassKg = 0.f;

	/**
	 * 창 동안 실제로 이동한 평균 속도 벡터(cm/s). 소음 판정과 막힘 판정이 본다.
	 *
	 * 순간 속도가 아니라 위치 변화로 재는 이유는 NoiseMinApproachSpeed 주석에 적었다.
	 * 방향이 필요해서 벡터로 둔다 — 소음 판정은 이것을 부딪힌 면의 법선에 투영한다.
	 * Z 도 담는다. 높은 데서 떨어진 카트의 착지음이 그것으로 잡힌다.
	 */
	FVector ApproachVelocity = FVector::ZeroVector;

	/** 창의 시작 위치와 경과 시간. 창이 끝나면 ApproachSpeed 를 갱신하고 다시 시작한다 */
	FVector ApproachSampleLocation = FVector::ZeroVector;
	float ApproachSampleAge = 0.f;

	/**
	 * 끌고 있는 사람. 복제한다 — 클라이언트도 "지금 누가 잡고 있나" 를 알아야
	 * 손 붙이기 연출을 각자 돌릴 수 있다. 판정은 서버만 한다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentPusher, VisibleInstanceOnly, Category = "Cart|Push")
	TObjectPtr<APawn> CurrentPusher;

	UFUNCTION()
	void OnRep_CurrentPusher();

	/**
	 * 잡힘 여부에 따른 물리 설정을 반영한다. 서버와 OnRep 이 같은 이 함수 하나를 부른다.
	 *
	 * 질량 변경도 틱 On/Off 도 복제되지 않는 로컬 호출이다. 복제되는 것은
	 * "누가 잡고 있는가"(CurrentPusher) 하나뿐이고, 양쪽이 그 사실을 보고 각자 반영한다.
	 * ALootBase::ApplyCarryState 와 같은 패턴이라 이름도 맞췄다.
	 *
	 * 서버에서만 걸면 클라이언트 쪽 카트는 60kg 로 남아, 그 화면에서만 카트가 날아간다.
	 * CMC 의 밀기 힘은 각 머신에서 자기 캐릭터에 대해 로컬로 돌기 때문이다.
	 */
	void ApplyPushState();

	/**
	 * 창 단위로 실제 이동 속도(ApproachSpeed)를 갱신한다. 모든 머신에서 돈다.
	 *
	 * UpdateFollow 가 속도를 대입하기 전, Tick 맨 앞에서 불러야 한다 —
	 * 재는 것은 '지난 프레임들 동안 실제로 어디까지 갔는가' 이기 때문이다.
	 */
	void UpdateApproachSpeed(float DeltaSeconds);

	/** 매 프레임 카트를 사람 앞으로 당긴다. 서버에서만 돈다 */
	void UpdateFollow(float DeltaSeconds);

	/**
	 * 잡히지 않은 동안 수평 속도에 저항을 걸어 몸으로 밀리는 정도를 약하게 한다.
	 * 모든 머신에서 돈다 — CMC 의 밀기 힘이 각 머신에서 자기 캐릭터에 대해 로컬로 들어온다.
	 *
	 * 잘라내지 않고 비례해서 빼내므로 톱니가 생기지 않는다. 자세한 이유는 IdlePushDrag 주석.
	 * 회전에도 같은 저항을 건다 — 밀기 힘이 접촉점에 걸려 요 토크가 생기기 때문이다.
	 */
	void ApplyIdlePushDrag(float DeltaSeconds);

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
