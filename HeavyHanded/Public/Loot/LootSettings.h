#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"          // FindTraitRow 가 FindRow 템플릿을 부른다
#include "Engine/DeveloperSettings.h"
#include "LootSettings.generated.h"

/**
 * 노획물 특성별 수치 표를 가리키는 프로젝트 세팅. Project Settings → Game → Loot.
 *
 * [왜 여기 두는가]
 *   특성 수치 표는 노획물마다 다른 것이 아니라 프로젝트에 하나씩 있다.
 *   BP 마다 FDataTableRowHandle 로 지정하게 하면 노획물 하나 만들 때마다
 *   같은 표를 다시 골라야 하고, 빠뜨리면 조용히 기본값으로 돈다.
 *
 *   DT_LootCatalog 만 BP 에서 행을 고르는 이유는 그쪽은 '어느 행인가' 를
 *   골라야 하기 때문이다. 특성 표는 행 이름을 카탈로그와 공유하므로 고를 것이 없다.
 *
 *   소음 파트의 UNoiseSettings, 경보 파트의 UAlertSettings 와 같은 패턴이다.
 *
 * [표를 늘릴 때]
 *   경보 연동형·중량형에 전용 컴포넌트가 생기면 여기에 TSoftObjectPtr 하나를 더 넣는다.
 *   조회는 FindTraitRow 가 공통이라 컴포넌트 쪽에 복사할 코드가 없다.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Loot"))
class HEAVYHANDED_API ULootSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * CDO 라 절대 null 이 아니다 — 호출부에서 null 검사를 하지 말 것.
	 * (UNoiseSettings::Get 과 같은 이유. 자세한 설명은 그쪽 주석에 있다)
	 */
	static const ULootSettings* Get() { return GetDefault<ULootSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/**
	 * 불안정형 수치. RowName == DT_LootCatalog 의 행 이름.
	 *
	 * 이 표에 행이 있다는 것이 곧 '이 노획물은 불안정형으로 설계됐다' 는 뜻이다.
	 * 특성을 최종적으로 정하는 것은 여전히 BP 의 컴포넌트지만, 둘이 어긋나면
	 * 양쪽에서 경고가 나가도록 대조할 근거가 생긴다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Data",
		meta = (AllowedClasses = "/Script/Engine.DataTable",
				RequiredAssetDataTags = "RowStructure=/Script/HeavyHanded.LootStabilityData"))
	TSoftObjectPtr<UDataTable> StabilityTable;

	/** 파손형 수치. RowName == DT_LootCatalog 의 행 이름 */
	UPROPERTY(config, EditAnywhere, Category = "Data",
		meta = (AllowedClasses = "/Script/Engine.DataTable",
				RequiredAssetDataTags = "RowStructure=/Script/HeavyHanded.LootDurabilityData"))
	TSoftObjectPtr<UDataTable> DurabilityTable;

	/** 중량형(2인 캐리) 수치. RowName == DT_LootCatalog 의 행 이름 */
	UPROPERTY(config, EditAnywhere, Category = "Data",
		meta = (AllowedClasses = "/Script/Engine.DataTable",
				RequiredAssetDataTags = "RowStructure=/Script/HeavyHanded.LootHeavyData"))
	TSoftObjectPtr<UDataTable> HeavyTable;

	/**
	 * 특성 표에서 행 하나를 찾는다. 없으면 nullptr.
	 *
	 * 표가 지정되지 않았거나 행이 없는 것은 정상 경로다 — 그 노획물이 이 특성을
	 * 안 쓰는 것뿐이다. 그래서 여기서는 경고하지 않는다. '컴포넌트는 붙었는데 행이 없다'
	 * 는 착오는 컴포넌트가 자기 문맥에서 판단해 경고한다.
	 *
	 * LoadSynchronous 를 쓰는 이유는 노획물 스폰 시점에 값이 바로 필요하기 때문이다.
	 * 표는 작고 한 번 로드되면 캐시되므로 히치가 나지 않는다.
	 */
	template <typename TRow>
	static const TRow* FindTraitRow(const TSoftObjectPtr<UDataTable>& TablePtr, FName RowName, const FString& Context)
	{
		if (RowName.IsNone())
		{
			return nullptr;
		}

		const UDataTable* Table = TablePtr.LoadSynchronous();
		if (!Table)
		{
			return nullptr;
		}

		// 마지막 인자를 false 로 줘서 '행 없음' 에 언리얼 기본 경고가 찍히지 않게 한다.
		// 행이 없는 것은 대부분 정상이고, 착오인 경우는 호출부가 문맥을 담아 따로 찍는다.
		return Table->FindRow<TRow>(RowName, Context, /*bWarnIfRowMissing=*/false);
	}
};
