#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"           // FTableRowBase — 부모 구조체, 전방 선언 불가
#include "ShopTypes.generated.h"

/**
 * 은신처 상점에서 파는 물건 하나의 값. DT_ShopCatalog 의 행 구조체다.
 * (원본 CSV 는 Data/ShopCatalog.csv — 수치를 고칠 때는 그쪽이 기준이다)
 *
 * [행 이름이 곧 장비 태그다]
 *   행 이름을 `Equipment.Drone` 처럼 태그 이름 그대로 쓴다. 진열대는 이미 ItemTag 를
 *   갖고 있으므로 BP 에서 행을 따로 고르지 않고, 태그로 바로 찾는다.
 *   그래서 이 구조체에 ItemTag 열이 없다 — 넣으면 같은 값이 두 벌이 되고,
 *   행 이름만 고쳤을 때 조용히 어긋난다.
 *
 *   develop 에 들어온 DT_SiteCatalog 가 같은 방식이다 (HeistSettings 가 SiteTag.GetTagName()
 *   으로 찾는다). 노획물 카탈로그만 BP 에서 행을 고르는데, 그쪽은 '어느 행인가' 를
 *   골라야 하기 때문이다. 진열대는 태그가 곧 행이라 고를 것이 없다.
 *
 * [진열대가 없는 장비도 행을 둔다]
 *   EMP 와 대차처럼 아직 진열대가 없는 것도 넣는다. 이 표가 '가격의 전체 목록' 이어야
 *   장소별 목표 금액과 나란히 놓고 균형을 볼 수 있고, 진열대는 나중에 붙어도 값은 이미 있다.
 */
USTRUCT(BlueprintType)
struct FShopItemRow : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * 표를 사람이 읽기 위한 이름. 태그만 있으면 무슨 물건인지 알 수 없다.
	 *
	 * FString 이 아니라 FText 인 이유는 FLootDefinitionRow::DisplayName 과 같다 —
	 * 나중에 영어를 넣을 때 문자열을 찾아 바꾸지 않아도 되게 한다.
	 * 지금은 아무 코드도 읽지 않는다. 표를 열었을 때를 위한 칸이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FText DisplayName;

	/**
	 * 가격($). 팀 공용 골드에서 나간다.
	 *
	 * 정가는 Config/Tags/Equipment.ini 의 DevComment 에 적혀 있었지만 주석이라 코드가
	 * 읽지 못했고, 그래서 진열대 BP 6개에 손으로 들어가 있었다. 이 칸이 그것을 대신한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "0"))
	int32 Price = 0;
};
