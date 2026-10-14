#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"           // FindItem 이 FindRow 템플릿을 부른다
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"       // FGameplayTag — 값으로 받는다
#include "Shop/ShopTypes.h"             // FShopItemRow — FindItem 의 반환 타입
#include "ShopSettings.generated.h"

/**
 * 상점 가격표를 가리키는 프로젝트 세팅. Project Settings → Game → Shop.
 *
 * [왜 진열대마다 지정하지 않는가]
 *   가격표는 진열대마다 다른 것이 아니라 프로젝트에 하나 있다. 진열대 BP 마다 표를
 *   고르게 하면 장비를 추가할 때마다 같은 표를 다시 고르고, 빠뜨리면 조용히 BP 가격으로 돈다.
 *   ULootSettings 가 특성 표를 여기에 둔 것과 같은 이유다.
 *
 * [왜 ULootSettings 에 넣지 않았나]
 *   상점은 노획물이 아니다. 노획물 표는 '물건의 물리·가치' 고 이쪽은 '장비의 판매가' 라
 *   고칠 사람도 고칠 이유도 다르다. UNoiseSettings · UAlertSettings 처럼 파트별로 나눠 둔
 *   기존 구성을 그대로 따른다.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Shop"))
class HEAVYHANDED_API UShopSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** CDO 라 절대 null 이 아니다 — 호출부에서 null 검사를 하지 말 것 (ULootSettings::Get 과 같다) */
	static const UShopSettings* Get() { return GetDefault<UShopSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/**
	 * 구매 장비의 가격표. RowName == 장비 태그 이름(`Equipment.Drone`).
	 *
	 * 비워 두면 진열대가 BP 에 손으로 넣은 가격으로 돈다. 그 편이 안 팔리는 것보다 낫지만
	 * 의도한 상태는 아니라서 진열대가 경고를 찍는다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Data",
		meta = (AllowedClasses = "/Script/Engine.DataTable",
				RequiredAssetDataTags = "RowStructure=/Script/HeavyHanded.ShopItemRow"))
	TSoftObjectPtr<UDataTable> ShopCatalog;

	/**
	 * 장비 태그로 가격표 행을 찾는다. 표가 없거나 행이 없으면 nullptr.
	 *
	 * 어느 쪽이 없는지는 호출부가 구별해야 경고 문구를 제대로 쓸 수 있다 —
	 * 표 연결을 잊은 것과 행을 빠뜨린 것은 고치는 곳이 다르다. 그래서 여기서는
	 * 경고하지 않고 nullptr 만 돌려준다. (ULootSettings::FindTraitRow 와 같은 방침)
	 *
	 * LoadSynchronous 는 표가 작고 한 번 로드되면 캐시되므로 히치가 나지 않는다.
	 */
	const FShopItemRow* FindItem(const FGameplayTag& ItemTag) const
	{
		if (!ItemTag.IsValid())
		{
			return nullptr;
		}

		const UDataTable* Table = ShopCatalog.LoadSynchronous();
		if (!Table)
		{
			return nullptr;
		}

		// 마지막 인자를 false 로 줘서 '행 없음' 에 언리얼 기본 경고가 찍히지 않게 한다.
		// 문맥을 담은 경고는 진열대가 자기 이름과 함께 따로 찍는다.
		return Table->FindRow<FShopItemRow>(ItemTag.GetTagName(), TEXT("FindShopItem"),
			/*bWarnIfRowMissing=*/false);
	}

	/** 표가 지정돼 있는가. 경고 문구를 '표가 없다' 와 '행이 없다' 로 나누기 위해 필요하다 */
	bool HasCatalog() const { return !ShopCatalog.IsNull(); }
};
