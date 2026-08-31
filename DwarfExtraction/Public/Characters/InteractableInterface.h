// InteractableInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

// 이 클래스는 언리얼 리플렉션용 (건드릴 필요 없음)
UINTERFACE(MinimalAPI, Blueprintable)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 상호작용 가능한 모든 물체가 구현해야 하는 인터페이스.
 * 아이템, 문, 스위치 등 어떤 액터든 이걸 구현하면 플레이어가 E키로 상호작용 가능.
 */
class TEAMPROJDWEX54_API IInteractableInterface
{
	GENERATED_BODY()

public:
	// 플레이어가 E키를 눌렀을 때 호출됨. Interactor = 상호작용을 시도한 플레이어
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void Interact(AActor* Interactor);

	// (선택) "줍기", "열기" 같은 상호작용 안내 문구. 나중에 UI 팀이 활용 가능
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FText GetInteractionText() const;
};