#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "GameplayEffectTypes.h"
#include "Engine/TimerHandle.h"
#include "InteractionComponent.generated.h"

class AItemBase;
class UGameplayEffect;
class UInventoryComponent;
class UBackpackComponent;

UCLASS(ClassGroup=(Dwarf), meta=(BlueprintSpawnableComponent))
class TEAMPROJDWEX54_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UInteractionComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// UFUNCTION(BlueprintCallable, Category = "Interact")
	// void TryInteract();
	
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void DropHeld();
	
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void UseHeld();
	
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void StopUseHeld();

	UFUNCTION(Server, Reliable)
	void Server_StopUseHeld();

	// Reload held weapon (R key -> server)
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void ReloadHeld();

	UFUNCTION(Server, Reliable)
	void Server_ReloadHeld();
	
	void EquipToHand(AItemBase* Item);
	
	UFUNCTION(BlueprintPure, Category = "Interact")
	AItemBase* GetHeldItem() const { return HeldItem; }
	
	UFUNCTION(BlueprintPure, Category = "Interact|Weight")
	float GetHeldWeight() const;
	
	UFUNCTION(BlueprintPure, Category = "Interact|Weight")
	float GetSpeedMultiplier() const;

	UFUNCTION(BlueprintCallable, Category = "Backpack")
	void UseBackpackSlot(int32 SlotIndex);
	
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	void DropBackpackSlot(int32 SlotIndex);
	
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void DropAllOnDeath();
	
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	void InterruptUse();
	
	UFUNCTION(BlueprintPure, Category = "Backpack")
	bool isUsingBackpackSlot() const { return PendingUseSlot != INDEX_NONE; }
	
	AItemBase* ReleaseHeldItem();
	
	UFUNCTION(Client, Reliable)
	void Client_ShowMessage(const FString& Msg);
	
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
protected:
	// UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact", meta = (ClampMin = "0.0"))
	// float InteractRange = 300.f;
	
	UPROPERTY(ReplicatedUsing = OnRep_HeldItem, BlueprintReadOnly, Category = "Interact")
	TObjectPtr<AItemBase> HeldItem;
	
	UFUNCTION()
	void OnRep_HeldItem();
	
	// UFUNCTION(Server, Reliable)
	// void Server_TryInteract();
	UFUNCTION(Server, Reliable)
	void Server_DropHeld();
	UFUNCTION(Server, Reliable)
	void Server_UseHeld();
	
	class APawn* GetOwnerPawn() const;
	// bool GetViewPoint(FVector& OutLoc, FRotator& OutRot) const;
	bool HasAuthority() const;	
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Weight")
	TSubclassOf<UGameplayEffect> WeightPenaltyEffect;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Weight", meta = (ClampMin = "1.0"))
	float RefWeight = 50.f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Weight", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxPenalty = 0.5f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Weight", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MinSpeedMultiplier = 0.4f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Weight", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JumpPenaltyScale = 1.f;
	
	FActiveGameplayEffectHandle WeightEffectHandle;
	float BaseJumpZ = 0.f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Drop", meta = (ClampMin = "0.0"))
	float DropThrowSpeed = 250.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Drop", meta = (ClampMin = "0.0"))
	float DropForwardOffset = 80.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Drop", meta = (ClampMin = "0.0"))
	float DropUpOffset = 30.f;
	
	void RefreshWeightPenalty();
	void ApplyWeightPenaltyGE(float Mult);
	void UpdateJumpPenalty(float Mult);
	
	UFUNCTION()
	void OnHeldInventoryChanged();
	
	UPROPERTY()
	TObjectPtr<UInventoryComponent> BoundInventory;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Backpack")
	TSubclassOf<UGameplayEffect> RootedEffect;
	
	UFUNCTION(Server, Reliable)
	void Server_UseBackpackSlot(int32 SlotIndex);
	
	UFUNCTION(Server, Reliable)
	void Server_DropBackpackSlot(int32 SlotIndex);
	
	void FinishBackpackUse();
	
	void CancelPendingUse();
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Death", meta = (ClampMin = "0.0"))
	float DeathDropRadius = 70.f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact|Death", meta = (ClampMin = "0.0"))
	float DeathDropImpulse = 200.f;
	
	UBackpackComponent* GetBackpack() const;
	
	FActiveGameplayEffectHandle RootedHandle;
	FTimerHandle UseTimer;
	int32 PendingUseSlot = INDEX_NONE;
	
	UFUNCTION(Client, Reliable)
	void Client_NotifyUseInterrupted();
};