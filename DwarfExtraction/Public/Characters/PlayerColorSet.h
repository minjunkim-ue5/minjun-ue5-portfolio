#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlayerColorSet.generated.h"

class UMaterialInterface;

// 색 하나에 대한 부위별 머티리얼 묶음
USTRUCT(BlueprintType)
struct FPlayerColorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInterface> SuitMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInterface> HelmetMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInterface> BackpackMaterial;
};

UCLASS()
class TEAMPROJDWEX54_API UPlayerColorSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	TArray<FPlayerColorEntry> Colors;
};