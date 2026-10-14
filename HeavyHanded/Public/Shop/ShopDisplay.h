#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/OnlineReplStructs.h"   // FUniqueNetIdRepl — TSet 원소로 값 보유
#include "GameplayTagContainer.h"              // FGameplayTag — 값으로 보유
#include "Interfaces/Interactable.h"           // 부모 인터페이스 — 전방 선언 불가
#include "ShopDisplay.generated.h"

class APawn;
class USoundBase;
class UStaticMeshComponent;

/**
 * 산 뒤에 그 물건이 어떻게 되는가.
 * 이 두 가지가 상점의 전부다 — 7종 중 6종이 StageSpawn 이고, 고무창 신발만 PersonalPassive 다.
 */
UENUM(BlueprintType)
enum class EShopPurchaseKind : uint8
{
	/** 구매 목록에만 들어간다. 다음 스테이지의 스폰 구역이 아이템 액터로 만든다 */
	StageSpawn,

	/** 산 사람에게 즉시 컴포넌트를 붙인다. 물건으로 나오지 않고 그 사람에게 붙어 있다 */
	PersonalPassive
};

/**
 * 구매 시도의 결과. 플레이어에게 보여줄 수준으로만 나눈다.
 *
 * 문자열이 아니라 열거형인 것은 이것이 RPC 로 나가기 때문이다 — 문자열을 실어 보내면
 * 매 시도마다 그만큼이 회선을 타고, 나중에 UI 가 붙을 때 문구를 UI 쪽에서 정할 수 없다.
 * 서버에만 남기는 자세한 사유(설정 오류 등)는 이 열거형에 넣지 않고 로그로만 남긴다.
 */
UENUM(BlueprintType)
enum class EShopPurchaseResult : uint8
{
	Purchased,
	AlreadyOwned,
	NotEnoughGold,

	/** 설정 오류 · 신원 확인 실패 · 지급 실패. 플레이어가 할 수 있는 것이 없는 경우 */
	Unavailable
};

/**
 * 은신처 상점의 진열대 하나. 물건 하나를 놓아 두고, 보고 E 를 누르면 판다.
 * (기획서 7장 — 은신처 구매 장비, 팀 공용 골드)
 *
 * [전 종류를 한 클래스로 처리한다]
 *   이 클래스에는 어떤 장비의 이름도 나오지 않는다. 진열대가 아는 것은 태그 · 가격 ·
 *   산 뒤에 무엇을 하는가(PurchaseKind) 뿐이고, 신발이냐 미끼냐는 BP 인스턴스가 정한다.
 *   그래서 장비를 추가할 때 C++ 은 늘어나지 않는다 — 이 클래스를 상속한 BP 에
 *   태그 · 가격 · 메시만 꽂으면 팔린다.
 *
 * [왜 AEquipmentBase 가 아닌가]
 *   진열대는 집히지도 던져지지도 소비되지도 않는다. 사도 그 자리에 그대로 남아 있고,
 *   실제 물건은 다음 스테이지의 스폰 구역에서 생긴다. AEquipmentBase 의 상태 기계
 *   (Idle -> Carried -> InFlight -> Deployed -> Active -> Spent)를 하나도 쓰지 않는다.
 *
 * [상호작용 경로를 새로 만들지 않았다]
 *   IInteractable 만 구현하면 UGAB_Interact 가 대상 종류를 모른 채 OnInteract 를 부른다
 *   (GAB_Interact.cpp 의 "문·상점·상자·밴" 분기 — 인터페이스 주석에 '상점 열기' 가
 *   처음부터 적혀 있다). 플레이어 파트에 요청할 것이 없었다.
 *
 * [판정은 전부 서버다]
 *   골드는 URunProgressSubsystem 이 들고 있고 복제되지 않는다. 클라이언트에서 깎으면
 *   그 창에서만 잔액이 줄어들고 서버는 모른다.
 */
UCLASS()
class HEAVYHANDED_API AShopDisplay : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AShopDisplay();

	/**
	 * IInteractable — 진열 메시에 시선이 닿은 채 E 를 누르면 UGAB_Interact 가 부른다. (서버 전용)
	 * 구매 판정이 들어오는 유일한 입구다.
	 */
	virtual void OnInteract_Implementation(APawn* Interactor) override;

protected:
	virtual void BeginPlay() override;

	/**
	 * 진열 메시. 크기 · 위치 · 회전은 뷰포트에서 정한다.
	 *
	 * 콜리전은 **Visibility 만 Block** 이다. Pawn 을 막으면 좁은 은신처에서 진열대에
	 * 몸이 계속 걸리고, 물리를 켜면 지나가다 밀어서 진열이 흐트러진다.
	 * AVanZone::BoardAimTarget 이 같은 이유로 같은 설정을 쓴다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UStaticMeshComponent> DisplayMesh;

	/**
	 * 무엇을 파는가. Config/Tags/Equipment.ini 의 Equipment.* 중 하나.
	 * PurchaseKind 가 StageSpawn 이면 이 태그가 그대로 구매 목록에 들어가고,
	 * 작업 레벨의 스폰 구역이 이 태그로 어떤 액터를 만들지 결정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FGameplayTag ItemTag;

	/**
	 * 가격($). 팀 공용 골드에서 나간다.
	 *
	 * **표에 행이 있으면 BeginPlay 에서 덮어쓴다.** 진짜 값은 DT_ShopCatalog 의
	 * `Equipment.*` 행이고(원본은 Data/ShopCatalog.csv), 이 칸은 표에 행이 없는
	 * 일회성 진열대를 위한 폴백으로만 남아 있다.
	 *
	 * 노획물처럼 칸을 회색으로 잠그지는 못한다 — 잠금은 BP 가 고른 행 핸들을 근거로 거는데
	 * 진열대는 태그로 행을 찾으므로 에디터가 미리 알 수 있는 것이 없다. 그래서 대신
	 * 어느 값이 쓰였는지 PIE 로그에 남긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "0"))
	int32 Price = 0;

	/** 산 뒤에 어떻게 되는가 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	EShopPurchaseKind PurchaseKind = EShopPurchaseKind::StageSpawn;

	/**
	 * 1인당 1회만 팔 것인가. 고무창 신발처럼 두 번 사도 소용이 없는 물건에 켠다.
	 * 미끼나 응급 키트는 여러 개 살 수 있어야 하므로 꺼 둔다.
	 *
	 * 누가 샀는지는 URunProgressSubsystem 이 캠페인 단위로 들고 있다 — 이 액터가 아니라.
	 * 레벨을 넘어야 하는 사실이기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	bool bOncePerPlayer = false;

	/**
	 * PersonalPassive 일 때 산 사람의 폰에 붙일 컴포넌트. 예: URubberShoesComponent.
	 *
	 * 진열대가 신발을 모르게 하는 장치다. 여기에 클래스를 꽂는 대신 코드에 신발 분기를
	 * 넣으면 다음 패시브 때 또 넣게 되고, 그때부터 장비마다 C++ 이 늘어난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Personal")
	TSubclassOf<UActorComponent> EquipmentComponentClass;

	/** 구매 성공음. 전원에게 이 진열대 위치에서 재생된다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Feedback")
	TObjectPtr<USoundBase> PurchaseSound;

	/** 거절음(잔액 부족 · 이미 보유). 성공음과 반드시 구별되는 소리로 둘 것 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Feedback")
	TObjectPtr<USoundBase> DeniedSound;

	/**
	 * 구매의 성공과 거절을 사유와 함께 진열대 위에 띄운다. (테스트용)
	 * 상점 UI 가 붙기 전까지는 "왜 안 사졌는가" 를 알 방법이 이것뿐이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Debug")
	bool bShowShopDebug = false;

	/**
	 * 개인 장비를 지급한다. 성공하면 true. (서버 전용)
	 * **false 를 돌려주면 호출부가 골드를 되돌린다** — 실패를 삼키면 $ 만 사라진다.
	 *
	 * 기본 구현은 EquipmentComponentClass 를 폰에 붙이는 것이다. 컴포넌트로 표현되지 않는
	 * 패시브가 나오면 BP 에서 이 함수만 덮어쓰면 된다.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Shop")
	bool GrantPersonalEquipment(APawn* Buyer);
	virtual bool GrantPersonalEquipment_Implementation(APawn* Buyer);

	/**
	 * 구매 결과를 소리와 진열대 위 표시로 알린다. **모든 머신에서 돈다.**
	 *
	 * 소리가 전원에게 들리는 것은 의도다 — 은신처는 좁고, 누가 무엇을 샀는지 서로 아는 것이
	 * 팀 공용 지갑에서는 오히려 맞다. 그리고 판정이 서버에서만 도는 탓에 클라이언트 창에는
	 * 아무 흔적도 남지 않던 문제(2026-09-09)를 이것이 함께 해결한다.
	 *
	 * Unreliable 인 것은 연출이기 때문이다. **놓쳐도 되는 것만 여기 실어야 한다** —
	 * 정식 UI 피드백은 이것에 기대지 말 것.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ReportResult(EShopPurchaseResult Result, int32 GoldAfter);

private:
	/**
	 * 결과를 세 갈래로 알린다. (서버 전용 — 판정이 끝난 자리에서 한 번만 부른다)
	 *   자세한 사유(ServerDetail) → 서버 로그. 개발자용이라 클라이언트로 보내지 않는다
	 *   결과                       → 구매자 본인의 PlayerController (Reliable)
	 *   소리 · 화면 표시           → 전원 (Unreliable)
	 *
	 * 진열대에서 Client RPC 를 직접 부를 수 없다는 것이 이 구조의 이유다 — Client RPC 는
	 * 액터의 Owner 커넥션으로 가는데 레벨에 배치된 진열대는 Owner 가 없어서 아무 데도 가지
	 * 않는다. 그래서 엔진이 이미 갖고 있는 APlayerController::ClientMessage 를 통로로 쓴다.
	 */
	void ReportResult(EShopPurchaseResult Result, APawn* Interactor, const FString& ServerDetail);

	/** 플레이어에게 보여줄 한 줄. 결과 열거형에서만 만든다 */
	FString MakePlayerMessage(EShopPurchaseResult Result, int32 GoldAfter) const;

	void ShowShopDebug(const FString& Message, const FColor& Color) const;

	/**
	 * 가격표에서 이 진열대의 가격을 가져와 Price 에 넣는다. **모든 머신에서 돈다.**
	 *
	 * 서버만 하면 안 되는 이유는 Multicast_ReportResult 가 클라이언트에서
	 * MakePlayerMessage 를 부르면서 Price 를 읽기 때문이다 — 서버만 갱신하면
	 * "잔액 부족" 문구의 가격이 클라이언트 창에서만 BP 값으로 뜬다.
	 *
	 * 표는 모든 머신에 같은 에셋으로 있으니 복제할 필요가 없다. 각자 자기 것을 읽으면 된다.
	 */
	void ResolvePriceFromCatalog();

	/** 설정이 비어 있는 진열대를 켤 때 경고한다. 돈만 받고 아무것도 안 주는 사고를 막는다 */
	void WarnIfMisconfigured() const;

	/**
	 * 가격표에서 가격을 받아왔는가. **false 면 팔지 않는다.**
	 *
	 * 예전에는 표가 없으면 BP 의 Price 로 폴백했는데, 그쪽이 더 위험했다 —
	 * 표 연결이 끊기면 옛 가격으로 조용히 팔리고, BP 값을 0 으로 비워 두면 **공짜로** 팔린다.
	 * 둘 다 플레이 중에는 드러나지 않고 밸런스만 어긋난다.
	 *
	 * 안 파는 쪽이 낫다. "지금은 살 수 없다" 는 바로 눈에 띄고, 서버 로그가 어느 행이
	 * 없는지 알려준다. 가격은 Data/ShopCatalog.csv 하나가 정한다는 규칙도 분명해진다.
	 */
	bool bPriceResolved = false;
};
