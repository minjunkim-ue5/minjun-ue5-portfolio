#include "Systems/Interaction/InteractionComponent.h"
//#include "Systems/Interaction/Interactable.h"
#include "Items/ItemBase.h"
#include "Items/HoldableItemBase.h"
#include "GameFramework/Pawn.h"
//#include "GameFramework/Controller.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "Systems/Inventory/InventoryComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Systems/Inventory/BackpackComponent.h"   
#include "Items/ItemDefinition.h"                  
#include "Engine/World.h"                          
#include "TimerManager.h"                          
#include "Items/MiningToolItem.h"
#include "Items/WeaponItem.h"


UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	
	if (const ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		if (const UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			BaseJumpZ = CMC->JumpZVelocity;
		}
	}
}


void UInteractionComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInteractionComponent, HeldItem);
}

bool UInteractionComponent::HasAuthority() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

class APawn* UInteractionComponent::GetOwnerPawn() const
{
	return Cast<APawn>(GetOwner());
}

// bool UInteractionComponent::GetViewPoint(FVector& OutLoc, FRotator& OutRot) const
// {
// 	APawn* Pawn = GetOwnerPawn();
// 	if (!Pawn)
// 		return false;
// 	
// 	if (AController* C = Pawn->GetController())
// 	{
// 		C->GetPlayerViewPoint(OutLoc, OutRot);
// 		return true;
// 	}
// 	Pawn->GetActorEyesViewPoint(OutLoc, OutRot);
// 	return true;
// }


// void UInteractionComponent::TryInteract()
// {
// 	Server_TryInteract();
// }

void UInteractionComponent::DropHeld()
{
	Server_DropHeld();
}

void UInteractionComponent::UseHeld()
{
	Server_UseHeld();
}

void UInteractionComponent::StopUseHeld()
{
	Server_StopUseHeld();
}

// void UInteractionComponent::Server_TryInteract_Implementation()
// {
// 	FVector Loc;
// 	FRotator Rot;
// 	if (!GetViewPoint(Loc, Rot))
// 		return;
// 	
// 	const FVector End = Loc + Rot.Vector() + InteractRange;
// 	FHitResult Hit;
// 	FCollisionQueryParams Params(SCENE_QUERY_STAT(Interact), false, GetOwner());
// 	if (GetWorld()->LineTraceSingleByChannel(Hit, Loc, End, ECC_Visibility, Params))
// 	{
// 		AActor* HitActor = Hit.GetActor();
// 		if (HitActor && HitActor->Implements<UInteractable>())
// 		{
// 			IInteractable::Execute_Interact(HitActor, GetOwnerPawn());
// 		}
// 	}
// }

void UInteractionComponent::Server_DropHeld_Implementation()
{
	if (!HeldItem)
		return;
	
	AItemBase* Item = HeldItem;
	APawn* Pawn = GetOwnerPawn();
	
	const float Mult = GetSpeedMultiplier();
	
	if (Pawn)
	{
		const FVector Fwd = Pawn->GetActorForwardVector();
		Item->SetActorLocation(Pawn->GetActorLocation() + Fwd * DropForwardOffset + FVector(0.f, 0.f, DropUpOffset));
	}
	
	HeldItem->OnDropped();
	HeldItem = nullptr;
	OnRep_HeldItem();
	
	if (Pawn)
	{
		Item->ThrowInDirection(Pawn->GetActorForwardVector(), DropThrowSpeed * Mult);
	}
}

void UInteractionComponent::Server_UseHeld_Implementation()
{
	AHoldableItemBase* Holdable = Cast<AHoldableItemBase>(HeldItem);
	if (!Holdable)
		return;

	const TSubclassOf<UGameplayAbility> Ability = Holdable->GetPrimaryAbility();
	if (!Ability)
		return;

	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwnerPawn()))
	{
		ASC->TryActivateAbilityByClass(Ability);
	}
}

void UInteractionComponent::Server_StopUseHeld_Implementation()
{
	if (AMiningToolItem* Tool = Cast<AMiningToolItem>(HeldItem))
	{
		Tool->StopMining();
	}
}

void UInteractionComponent::ReloadHeld()
{
	Server_ReloadHeld();
}

void UInteractionComponent::Server_ReloadHeld_Implementation()
{
	if (AWeaponItem* Weapon = Cast<AWeaponItem>(HeldItem))
	{
		Weapon->TryStartReload(GetOwnerPawn());
	}
}

void UInteractionComponent::EquipToHand(AItemBase* Item)
{
	if (!HasAuthority() || !Item || Item == HeldItem)
		return;
	
	if (HeldItem)
	{
		return; //Server_DropHeld_Implementation();
	}
	
	APawn* Pawn = GetOwnerPawn();
	if (!Pawn)
		return;
	
	Item->OnPickedUp(Pawn);

	// 캐릭터의 스켈레탈 메시에, ItemDef가 지정한 소켓으로 부착
	USceneComponent* AttachTarget = Pawn->GetRootComponent();
	FName SocketName = NAME_None;

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (Char->GetMesh())
		{
			AttachTarget = Char->GetMesh();
			if (Item->GetItemDef())
			{
				SocketName = Item->GetItemDef()->AttachSocket;
			}
		}
	}

	//Item->AttachToComponent(AttachTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);

	HeldItem = Item;
	OnRep_HeldItem();
}

void UInteractionComponent::OnRep_HeldItem()
{
	if (BoundInventory)
	{
		BoundInventory->OnInventoryChanged.RemoveDynamic(this, &UInteractionComponent::OnHeldInventoryChanged);
		BoundInventory = nullptr;
	}
	
	if (HeldItem)
	{
		if (UInventoryComponent* Inv = HeldItem->FindComponentByClass<UInventoryComponent>())
		{
			Inv->OnInventoryChanged.AddDynamic(this, &UInteractionComponent::OnHeldInventoryChanged);
			BoundInventory = Inv;
		}
	}
	
	RefreshWeightPenalty();
	
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage((uint64)GetUniqueID(), 2.f, FColor::Cyan,
					  FString::Printf(TEXT("Hand: %s | W=%.1f | Value=%d | Speed x%.2f"),
							  HeldItem ? *HeldItem->GetName() : TEXT("empty"),
							  GetHeldWeight(),
							  HeldItem ? HeldItem->GetRecoveryValue() : 0,
							  GetSpeedMultiplier()));
	}
#endif
}


float UInteractionComponent::GetHeldWeight() const
{
	return HeldItem ? HeldItem->GetTotalWeight() : 0.f;
}

float UInteractionComponent::GetSpeedMultiplier() const
{
	const float W = GetHeldWeight();
	const float Mult = 1.f - (W / FMath::Max(RefWeight, 1.f)) * MaxPenalty;
	return FMath::Clamp(Mult, MinSpeedMultiplier, 1.f);
}

void UInteractionComponent::UseBackpackSlot(int32 SlotIndex)
{
	Server_UseBackpackSlot(SlotIndex);
}

void UInteractionComponent::DropBackpackSlot(int32 SlotIndex)
{
	Server_DropBackpackSlot(SlotIndex);
}

void UInteractionComponent::DropAllOnDeath()
{
	if (!HasAuthority())
		return;
	
	CancelPendingUse();
	
	if (HeldItem)
	{
		AItemBase* Dropped = HeldItem;
		HeldItem = nullptr;
		OnRep_HeldItem();
		Dropped->OnDropped();
	}
	
	APawn* Pawn = GetOwnerPawn();
	UBackpackComponent* Bp = GetBackpack();
	
	if (Bp && Pawn)
	{
		Bp->StopAutoPickup();
		
		const FVector Origin = Pawn->GetActorLocation();
		const int32 SlotCount = Bp->GetCapacity();
		
		for (int32 i = 0; i < SlotCount; ++i)
		{
			if (!Bp->GetSlotClass(i))
				continue;
			
			const float AngleDeg = (360.f / FMath::Max(SlotCount, 1)) * i;
			const FVector Dir = FRotator(0.f, AngleDeg, 0.f).RotateVector(Pawn->GetActorForwardVector()).GetSafeNormal2D();
			const FTransform Xf(Pawn->GetActorRotation(), Origin + Dir* DeathDropRadius + FVector(0.f, 0.f, 40.f));
			if (AItemBase* Item = Bp->RemoveOne(i, Xf))
			{
				Item->ThrowInDirection(Dir, DeathDropImpulse);
			}
		}
	}
	
	RefreshWeightPenalty();
	
	UE_LOG(LogTemp, Warning, TEXT("[Death] Rooted=%d Weight=%d"),
			  RootedHandle.IsValid(), WeightEffectHandle.IsValid());
}

void UInteractionComponent::InterruptUse()
{
	if (!HasAuthority() || PendingUseSlot == INDEX_NONE)
		return;
	
	CancelPendingUse();
	
	Client_NotifyUseInterrupted();
}

AItemBase* UInteractionComponent::ReleaseHeldItem()
{
	if (!HasAuthority() || !HeldItem)
		return nullptr;
	
	AItemBase* Item = HeldItem;
	HeldItem = nullptr;
	OnRep_HeldItem();
	
	return Item;
}

void UInteractionComponent::Client_ShowMessage_Implementation(const FString& Msg)
{
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, Msg);
	}
#endif
}

void UInteractionComponent::Client_NotifyUseInterrupted_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Red, TEXT("Use Interrupted!"));
	}
#endif
}

void UInteractionComponent::CancelPendingUse()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(UseTimer);
	}
	PendingUseSlot = INDEX_NONE;
	
	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwnerPawn()))
	{
		if (RootedHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(RootedHandle);
			RootedHandle.Invalidate();
		}
	}
	
	UpdateJumpPenalty(GetSpeedMultiplier());
}

void UInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPendingUse();
	
	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwnerPawn()))
	{
		if (WeightEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(WeightEffectHandle);
			WeightEffectHandle.Invalidate();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UInteractionComponent::Server_UseBackpackSlot_Implementation(int32 SlotIndex)
{
	UBackpackComponent* Bp = GetBackpack();
	if (!Bp || PendingUseSlot != INDEX_NONE)
		return;
	
	TSubclassOf<AItemBase> Cls = Bp->GetSlotClass(SlotIndex);
	if (!Cls)
		return;
	
	AItemBase* CDO = Cls.GetDefaultObject();
	if (!CDO || !CDO->CanConsume(GetOwner(), HeldItem))
	{
#if !UE_BUILD_SHIPPING
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Orange, TEXT("Can't use that now"));
		}
#endif
		return;
	}
	
	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwnerPawn()))
	{
		if (RootedEffect)
		{
			FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
			Ctx.AddSourceObject(this);
			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(RootedEffect, 1.f, Ctx);
			if (Spec.IsValid())
			{
				RootedHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
		
		FGameplayTagContainer CancelTags;
		CancelTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Sprint")));
		ASC->CancelAbilities(&CancelTags);
	}
	
	const UItemDefinition* Def = CDO->GetItemDef();
	const float UseTime = Def ? Def->UseTime : 1.f;
	
	if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		Char->GetCharacterMovement()->JumpZVelocity = 0.f;
	}
	
	PendingUseSlot = SlotIndex;
	GetWorld()->GetTimerManager().SetTimer(
		UseTimer, this, &UInteractionComponent::FinishBackpackUse, UseTime, false);
}

void UInteractionComponent::Server_DropBackpackSlot_Implementation(int32 SlotIndex)
{
	UBackpackComponent* Bp = GetBackpack();
	APawn* Pawn = GetOwnerPawn();
	if (!Bp || !Pawn)
		return;
	
	const FTransform Xf(Pawn->GetActorRotation(), Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 80.f + FVector(0.f, 0.f, 30.f));
	if (AItemBase* Thrown = Bp->RemoveOne(SlotIndex, Xf))
	{
		Thrown->ThrowInDirection(Pawn->GetActorForwardVector());
	}
}

void UInteractionComponent::FinishBackpackUse()
{
	UpdateJumpPenalty(GetSpeedMultiplier());
	
	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwnerPawn()))
	{
		if (RootedHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(RootedHandle);
			RootedHandle.Invalidate();
		}
	}
	
	const int32 Slot = PendingUseSlot;
	PendingUseSlot = INDEX_NONE;
	
	UBackpackComponent* Bp = GetBackpack();
	if (!Bp || Slot == INDEX_NONE)
		return;
	
	TSubclassOf<AItemBase> Cls = Bp->GetSlotClass(Slot);
	if (!Cls)
		return;
	
	AItemBase* CDO = Cls.GetDefaultObject();
	if (!CDO)
		return;
	
	if (CDO->CanConsume(GetOwner(), HeldItem))
	{
		CDO->Consume(GetOwner(), HeldItem);
		Bp->ConsumeSlot(Slot);	
	}
}

UBackpackComponent* UInteractionComponent::GetBackpack() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UBackpackComponent>() : nullptr;
}

void UInteractionComponent::RefreshWeightPenalty()
{
	const float Mult = GetSpeedMultiplier();
	
	if (HasAuthority())
	{
		ApplyWeightPenaltyGE(Mult);
	}
	UpdateJumpPenalty(Mult);
}

void UInteractionComponent::ApplyWeightPenaltyGE(float Mult)
{
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwnerPawn());
	if (!ASC || !WeightPenaltyEffect)
		return;
	
	if (WeightEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(WeightEffectHandle);
		WeightEffectHandle.Invalidate();
	}
	
	if (FMath::IsNearlyEqual(Mult, 1.f))
		return;
	
	FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
	Ctx.AddSourceObject(this);
	
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(WeightPenaltyEffect, 1.f, Ctx);
	if (Spec.IsValid())
	{
		static const FGameplayTag WeightTag = FGameplayTag::RequestGameplayTag(FName("Data.WeightMultiplier"));
		Spec.Data->SetSetByCallerMagnitude(WeightTag, Mult);
		WeightEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

void UInteractionComponent::UpdateJumpPenalty(float Mult)
{
	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char || BaseJumpZ <= 0.f)
		return;
	
	const float JumpMult = FMath::Lerp(1.f, Mult, JumpPenaltyScale);
	Char->GetCharacterMovement()->JumpZVelocity = BaseJumpZ * JumpMult;
}

void UInteractionComponent::OnHeldInventoryChanged()
{
	RefreshWeightPenalty();
}

