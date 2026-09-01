#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"          // 부모 구조체 FTableRowBase — 전방 선언 불가
#include "Core/HeavyHandedTypes.h"     // FLootPhysicsData 를 값으로 보유
#include "LootTypes.generated.h"

/**
 * 불안정형 노획물의 설계값. (기획서 5장 — 기울기 60도 초과 시 내용물 유출)
 *
 * DT_LootStability 의 한 행이다. RowName 은 DT_LootCatalog 의 행 이름과 같다.
 *
 * [왜 DT_LootCatalog 의 열이 아닌가]
 *   이 9개 값을 읽는 것은 ULootStabilityComponent 뿐이다. 카탈로그의 열로 두면
 *   불안정형이 아닌 노획물까지 전부 이 값을 들고 다니게 된다. 실제로 그랬는데
 *   8행 × 9필드 = 72개 중 의미 있는 값이 1개(동전 자루의 SpillTiltAngle=50)였다.
 *
 *   나머지 71개는 기본값이 직렬화된 것일 뿐인데, 표에 숫자로 적혀 있으니
 *   "이 물건도 기울면 새겠구나" 하고 읽히고, 실제로는 컴포넌트가 없어서 아무 일도
 *   일어나지 않는다. 표를 나누면 이 표에 행이 있다는 것 자체가 곧 불안정형 명단이 된다.
 *
 * [카탈로그와의 관계는 상속이 아니라 조인이다]
 *   FTableRowBase 상속은 '이 구조체를 표의 행으로 쓸 수 있다' 는 표시일 뿐이고
 *   FLootDefinitionRow 와는 아무 관계도 없다. 두 표를 잇는 것은 행 이름 하나다.
 *   그래서 카탈로그에서 행을 지워도 여기 행은 따라 지워지지 않는다 —
 *   고아 행은 언리얼이 막아 주지 않으므로 ALootBase 가 경고로 잡는다.
 */
USTRUCT(BlueprintType)
struct FLootStabilityData : public FTableRowBase
{
	GENERATED_BODY()

	/** 이 각도를 넘으면 샌다(도). 수직에서 벗어난 각도이므로 90 이면 완전히 누운 상태다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability",
		meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SpillTiltAngle = 60.f;

	/**
	 * 이 속도 이하로 움직이면 기울기가 쌓이지 않는다(cm/s).
	 * 걷기는 안전하고 뛰면 쌓이는 지점에 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability|Carry",
		meta = (ClampMin = "0.0"))
	float SafeCarrySpeed = 200.f;

	/**
	 * 안전 속도 초과분 1cm/s 당 초당 쌓이는 기울기(도).
	 *
	 * 속도와 시간이 둘 다 들어가는 것이 핵심이다. 얼마나 빠른지(초과분)와
	 * 얼마나 오래 그랬는지(누적)가 같이 반영돼야 "조금만 더 뛸까" 하는 판단이 생긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability|Carry",
		meta = (ClampMin = "0.0"))
	float TiltGainPerSpeed = 0.06f;

	/** 안전 속도 이하일 때 되돌아오는 속도(도/초). 멈춰서 숨 고르면 회복된다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability|Carry",
		meta = (ClampMin = "0.0"))
	float TiltRecoverRate = 25.f;

	/**
	 * 기우는 방향이 이동 방향을 따라가는 속도(도/초).
	 *
	 * 방향을 즉시 바꾸면 좌우로 왔다 갔다 할 때 물건이 순간이동하듯 꺾인다.
	 * 내용물이 쏠렸다가 반대로 쏠리는 데도 시간이 걸린다고 보면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability|Carry",
		meta = (ClampMin = "0.0"))
	float TiltDirectionTurnRate = 180.f;

	/**
	 * 놓인 상태에서 이 시간 이상 연속으로 기울어져 있어야 샌다(초).
	 *
	 * 던지면 ThrowSpinSpeed 로 회전하면서 날아가기 때문에, 순간 각도만 보면
	 * 공중에서 새 버린다. 시간으로 한 겹 걸러야 '넘어진 것'과 '회전 중인 것'이 갈린다.
	 * 굴러가는 중에는 계속 기울어져 있으므로 그대로 통과한다 — 의도한 동작이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability",
		meta = (ClampMin = "0.0"))
	float TiltGraceSeconds = 0.5f;

	/** 기울어져 있는 동안 이 주기로 계속 샌다(초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability",
		meta = (ClampMin = "0.1"))
	float SpillIntervalSeconds = 2.f;

	/** 1회 유출당 깎이는 가치 비율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpillValueLossRatio = 0.15f;

	/**
	 * 설계 가치 대비 이 비율 밑으로는 깎이지 않는다.
	 * 가치 0 은 파손형의 몫이다. 불안정형까지 0 이 되면 두 특성이 같아진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Stability",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinValueRatio = 0.2f;
};

/**
 * 파손형 노획물의 설계값. (기획서: 충격 3회 누적 시 파괴, 가치 0)
 *
 * DT_LootDurability 의 한 행이다. RowName 은 DT_LootCatalog 의 행 이름과 같다.
 *
 * [왜 FLootPhysicsData 에서 뺐나]
 *   불안정형과 같은 이유인데 눈에 덜 띄었을 뿐이다. 이 둘을 읽는 것은
 *   ULootDurabilityComponent 뿐인데, 무게·던지기 값들과 같은 구조체에 섞여 있어서
 *   파손형이 아닌 노획물도 전부 들고 다녔다.
 *
 *   덕분에 경고도 못 넣었다. DamageImpulseThreshold 는 질량에 비례해서 전 행에
 *   관례적으로 채워 두는 값이라 '기본값과 다르다' 가 착오의 신호가 되지 못했다.
 *   표를 나누면 그 추측 자체가 필요 없어진다 — 행이 있으면 파손형인 것이다.
 *
 * [임계값이 두 개인 이유] ImpactReportThreshold 는 FLootPhysicsData 에 남는다.
 *   그건 소음 파트에 알릴 최소 충격이라 모든 노획물이 쓰기 때문이다.
 *   여기 있는 것은 '파손으로 칠 최소 충격' 이고 더 높게 잡힌다.
 */
USTRUCT(BlueprintType)
struct FLootDurabilityData : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * 이 값 이상의 충격만 파손 카운트에 반영한다. 미만은 소리만 나고 물건은 멀쩡하다.
	 *
	 * 임펄스는 대략 [질량(kg) x 낙하속도(cm/s)] 이고, 낙하속도는 sqrt(2 * 980 * 높이cm) 다.
	 * 10kg 기준 실측: 100cm 낙하 5031 / 150cm 6497 / 300cm 9622.
	 * 착지 후 튀는 충격은 500~900 대로 나온다.
	 *
	 * 3000 은 그 사이를 가르는 값이다. 처음 잡았던 600 은 '툭 스침' 수준이라
	 * 튕김까지 파손으로 세서 한 번 떨어뜨리면 목숨이 2개씩 날아갔다.
	 *
	 * [주의] 질량에 비례한다. DT_LootCatalog 에서 MassKg 를 바꾸면 같이 조정해야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability",
		meta = (ClampMin = "0.0"))
	float DamageImpulseThreshold = 3000.f;

	/** 이 횟수만큼 충격이 쌓이면 깨진다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Durability",
		meta = (ClampMin = "1"))
	int32 MaxImpactCount = 3;
};

/**
 * 중량형 노획물의 설계값. (기획서 5장 — 1인 시 속도 30%, 2인이면 100%)
 *
 * DT_LootHeavy 의 한 행이다. RowName 은 DT_LootCatalog 의 행 이름과 같다.
 *
 * [여기 있는 것은 '2인 캐리를 어떻게 하는가' 뿐이다]
 *   무게로 인한 페널티(CarrySpeedMultiplier, bAllowJumpWhileCarried)는 카탈로그에 남는다.
 *   중량형만의 것이 아니기 때문이다 — 금괴는 중량형이 아닌데도 0.75 로 느리다.
 *   그것까지 여기로 옮기면 중량형이 아닌 노획물이 중량형 행을 갖는 모순이 생긴다.
 *
 *   반대로 '두 사람이 어디를 잡는가' 는 2인 캐리에만 있는 개념이라 여기 있어야 한다.
 *
 * [두 그립 사이 거리는 여기 없다]
 *   메시의 두 소켓 위치에서 계산한다. 값으로 적어 두면 메시를 바꿨을 때
 *   숫자만 옛 메시에 맞은 채로 남아 물건이 손에서 어긋난다.
 *
 * [제약 튜닝 값도 아직 없다]
 *   거리 제약을 푸는 함수를 쓰면서 어떤 손잡이가 필요한지 정해지면 그때 열을 추가한다.
 *   미리 넣으면 실제로 필요한 것과 다른 값이 표에 남는다.
 */
USTRUCT(BlueprintType)
struct FLootHeavyData : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * 페널티 없이 옮기려면 필요한 인원.
	 *
	 * 이 인원이 안 되면 못 드는 것이 아니라 느려진다 (기획: 1인 시 30%).
	 * 그래서 CanBeCarriedBy 는 이 값으로 막지 않는다 — 혼자 낑낑대며 옮기는 그림이
	 * 기획 의도이고, 못 들게 하면 그 선택지가 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Heavy",
		meta = (ClampMin = "2", ClampMax = "4"))
	int32 RequiredCarriers = 2;

	/**
	 * 두 사람이 각각 잡는 노획물 메시의 소켓 이름.
	 *
	 * 리더가 끌고 팔로워가 따라오는 구조가 아니다. 두 사람이 양 끝을 잡고,
	 * 둘의 이동을 합쳐 물체 트랜스폼이 결정된다. 한 명이 멈춰 있으면 다른 한 명은
	 * 그 사람을 중심으로 돌 수밖에 없고, 이 '서로 방해되는' 감각이 협동의 핵심이다.
	 *
	 * 소켓이 없으면 어태치는 되지만 두 지점이 겹쳐서 제약이 의미를 잃는다.
	 * 없을 때는 ULootHeavyComponent 가 경고한다.
	 *
	 * [B 단계 예고] 이 두 값을 실제로 읽는 것은 그립 어태치를 붙일 때다.
	 *   지금은 메시 작업에서 소켓 이름을 맞출 수 있도록 먼저 열어 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Heavy")
	FName GripSocketA = TEXT("Grip_A");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Heavy")
	FName GripSocketB = TEXT("Grip_B");
};

/**
 * 노획물 한 종류의 설계값. DT_LootCatalog 의 한 행이다.
 *
 * [왜 DataAsset 이 아니라 DataTable 인가]
 *   기획자가 "도자기가 은촛대보다 비싼가" 를 한눈에 보고 한 자리에서 고쳐야 한다.
 *   DataAsset 은 노획물마다 파일이 하나씩 생겨서 비교하려면 창을 여러 개 열어야 한다.
 *   결정적인 이유는 CSV 다 — DataTable 은 표를 CSV 로 내보내고 되받을 수 있고,
 *   CSV 는 텍스트라 git 이 줄 단위로 diff 를 보여준다. uasset 은 병합이 안 되므로
 *   수치 조정이 잦은 데이터를 uasset 안에만 두면 두 사람이 만지는 순간 한쪽이 날아간다.
 *   소음 파트의 DT_NoiseProfiles 도 같은 이유로 DataTable 이다.
 *
 * [값만 담는다 — 어떤 특성인지는 여기서 정하지 않는다]
 *   특성 셋(중량형·파손형·불안정형)은 모두 컴포넌트를 붙이는 것이 곧 선언이다 (BP 조합).
 *   여기에 "파손형인가" 같은 칸을 만들면 컴포넌트는 없는데 표에는 파손형인 상태가
 *   만들어지고, 둘 중 무엇이 진실인지 알 수 없게 된다.
 *
 *   중량형이 Physics.WeightClass 로 결정되던 예외는 없앴다. 규칙은 하나다 —
 *   컴포넌트가 특성을 선언하고, 같은 이름의 행이 수치를 준다.
 *
 * [행 이름] RowName 이 곧 노획물 ID 다. Loot_Vase 처럼 물건 이름만 적는다.
 *   특성도 장소도 넣지 않는다 — 둘 다 다른 곳이 이미 알고 있어서 이름에 또 적으면
 *   두 벌이 되고 한쪽만 바뀐다. 자세한 규칙은 Data/README.md 에 있다.
 *
 *   이 이름은 특성 표 셋의 조인 키이기도 하다.
 *   그래서 한번 정하면 바꾸지 않는다. 바꾸면 표 네 개가 동시에 끊긴다.
 */
USTRUCT(BlueprintType)
struct FLootDefinitionRow : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * 화면에 뜨는 이름. 상호작용 프롬프트와 정산 목록이 쓴다.
	 *
	 * FString 이 아니라 FText 인 이유는 현지화 때문이다. 게임은 한국어로 만들지만
	 * 나중에 영어를 넣을 때 FString 이면 문자열을 전부 찾아 바꿔야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FText DisplayName;

	/**
	 * 손상되지 않았을 때의 가치($).
	 *
	 * 기획서 목표 금액은 저택 $50,000 / 박물관 $120,000 / 은행 $250,000 이다.
	 * 한 장소의 노획물 값을 다 더한 것이 목표 금액보다 충분히 커야 선택의 여지가 생긴다.
	 * 표에서 세로로 훑으며 맞추라고 이 칸이 여기 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 BaseValue = 1000;

	/**
	 * 무게·운반·던지기·파손 임계값.
	 *
	 * 필드를 여기에 옮겨 적지 않고 기존 구조체를 통째로 품는다. 옮겨 적으면 같은 값이
	 * 두 벌이 되고, 한쪽만 고쳤을 때 컴파일도 통과해서 발견이 늦어진다.
	 *
	 * 특성별 수치(불안정형·파손형)는 여기 없다. 모든 노획물이 쓰는 값만 둔다 —
	 * 그것이 이 표가 '카탈로그' 인 이유다. 특성 수치는 DT_LootStability /
	 * DT_LootDurability 에 같은 행 이름으로 따로 적는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FLootPhysicsData Physics;
};
