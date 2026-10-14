#include "Loot/LootDurabilityComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Core/HeavyHandedGameplayTags.h"
#include "Core/HeavyHandedTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Loot/LootBase.h"
#include "Loot/LootLog.h"
#include "Loot/LootSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Noise/NoiseEmitterComponent.h" // 파괴 소음 — 충돌이 아니라 내 판정으로 나간다
#include "TimerManager.h"

ULootDurabilityComponent::ULootDurabilityComponent()
{
	// 충격은 이벤트로 온다. 매 프레임 확인할 것이 없다.
	PrimaryComponentTick.bCanEverTick = false;

	// ImpactCount / bIsBroken 이 복제되어야 클라이언트가 금 간 연출과 파괴를 맞출 수 있다.
	SetIsReplicatedByDefault(true);
}

void ULootDurabilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ULootDurabilityComponent, ImpactCount);
	DOREPLIFETIME(ULootDurabilityComponent, bIsBroken);
}

void ULootDurabilityComponent::ResolveData()
{
	if (bDataResolved)
	{
		return;
	}
	bDataResolved = true;

	if (!IsValid(OwnerLoot))
	{
		OwnerLoot = Cast<ALootBase>(GetOwner());
		if (!IsValid(OwnerLoot))
		{
			return;
		}
	}

	const FName RowName = OwnerLoot->GetLootRowName();

	// 표를 안 쓰는 노획물이다. BP 에 적힌 Data 를 그대로 쓴다 — 실험물용 폴백이다.
	if (RowName.IsNone())
	{
		return;
	}

	const FLootDurabilityData* Row = ULootSettings::FindTraitRow<FLootDurabilityData>(
		ULootSettings::Get()->DurabilityTable, RowName, GetName());

	if (!Row)
	{
		// 컴포넌트가 붙었다는 것은 파손형으로 만들겠다는 선언인데 표에 행이 없다.
		// 기본값(3000/3회)으로 도는데, 임계값은 질량에 비례해야 해서 무거운 물건이면
		// 살짝만 부딪혀도 깨지고 가벼운 물건이면 아무리 던져도 안 깨진다.
		// 반대 방향(행은 있는데 컴포넌트가 없다)은 ALootBase 가 잡는다.
		UE_LOG(LogLoot, Warning,
			TEXT("[Loot:%s] ULootDurabilityComponent 가 붙어 있는데 DT_LootDurability 에 '%s' 행이 없다 "
				 "— 기본값(%.0f / %d회)으로 돈다. 임계값은 질량에 비례해야 하므로 표에 행을 추가할 것"),
			*OwnerLoot->GetName(), *RowName.ToString(), Data.DamageImpulseThreshold, Data.MaxImpactCount);
		return;
	}

	Data = *Row;
}

void ULootDurabilityComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerLoot = Cast<ALootBase>(GetOwner());
	if (!IsValid(OwnerLoot))
	{
		// 충격 이벤트·가치·행 이름을 전부 ALootBase 에서 읽으므로 다른 액터에는 붙을 수 없다.
		UE_LOG(LogLoot, Warning,
			TEXT("[%s] ULootDurabilityComponent 는 ALootBase 에만 붙일 수 있다. 파손 판정이 비활성화된다."),
			*GetNameSafe(GetOwner()));
		return;
	}

	ResolveData();

	// 확정 충격은 서버에서만 발생한다. 클라이언트는 복제된 값으로 연출만 맞춘다.
	if (OwnerLoot->HasAuthority())
	{
		// 이 컴포넌트가 붙어 있다는 것이 곧 "파손형" 이라는 선언이다.
		// BP 에서 태그를 따로 지정하게 하면 컴포넌트는 있는데 태그는 없는 상태가 생긴다.
		OwnerLoot->AddLootTypeTag(HHTags::Loot_Type_Fragile);

		ImpactDelegateHandle = OwnerLoot->OnLootImpact.AddUObject(
			this, &ULootDurabilityComponent::HandleLootImpact);
	}
}

void ULootDurabilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 구독을 남긴 채 파괴되면 죽은 객체를 호출한다.
	if (IsValid(OwnerLoot) && ImpactDelegateHandle.IsValid())
	{
		OwnerLoot->OnLootImpact.Remove(ImpactDelegateHandle);
		ImpactDelegateHandle.Reset();
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DestroyTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ULootDurabilityComponent::HandleLootImpact(const FLootImpactEvent& Event)
{
	if (bIsBroken)
	{
		return;
	}

	// 파괴 방송을 다시 세면 자기 자신을 물고 들어간다.
	if (Event.Cause == ELootImpactCause::Break)
	{
		return;
	}

	// 사람 몸에 닿아서는 상하지 않는다. 넘어져서 바닥에 부딪히면 그때 상한다.
	// 키네마틱 캡슐은 살짝 닿아도 임펄스가 낙하의 몇 배로 튀어서, 임계값으로는 못 가른다.
	if (bIgnorePawnImpacts && Cast<APawn>(Event.HitActor.Get()))
	{
		if (OwnerLoot->IsImpactDebugEnabled() && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Emerald,
				FString::Printf(TEXT("[%s] 파손 제외(사람 접촉) 임펄스 %.0f — %s"),
					*OwnerLoot->GetName(), Event.ImpulseMagnitude, *GetNameSafe(Event.HitActor.Get())));
		}
		return;
	}

	// 여기까지 온 충격은 이미 '소음으로 알릴 만한' 크기다.
	// 그렇다고 다 파손은 아니다. 파손 임계값은 따로 더 높게 잡혀 있다.
	if (Event.ImpulseMagnitude < Data.DamageImpulseThreshold)
	{
		return;
	}

	++ImpactCount;

	// 서버에서 값을 직접 바꾸면 RepNotify 가 불리지 않는다. 서버 몫은 손으로 부른다.
	OnRep_ImpactCount();

	// 게이팅이 통과시킨 충격 중 실제로 파손까지 간 것이 몇 개인지 같이 봐야
	// DamageImpulseThreshold 가 적당한지 판단할 수 있다. 스위치는 ALootBase 와 공유한다.
	if (OwnerLoot->IsImpactDebugEnabled() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red,
			FString::Printf(TEXT("[%s] 파손 %d / %d  (임펄스 %.0f)"),
				*OwnerLoot->GetName(), ImpactCount, Data.MaxImpactCount, Event.ImpulseMagnitude));
	}

	const bool bWillBreak = (ImpactCount >= Data.MaxImpactCount);

	// 금이 갈 때마다 값이 깎인다. 깨지고 나서야 손해를 아는 것이 아니라, 들고 가는 동안
	// "지금 밴에 넣을까 하나 더 주우러 갈까" 를 고민하게 만드는 것이 목적이다.
	//
	// 파괴되는 충격에서는 건너뛴다. 어차피 바로 아래 Break 가 100% 손실로 0 을 만드는데
	// 여기서 한 번 더 깎으면 같은 프레임에 OnValueChanged 가 두 번 나가 HUD 가 깜빡인다.
	if (!bWillBreak && Data.ValueLossPerImpact > 0.f)
	{
		OwnerLoot->ApplyValueLoss(Data.ValueLossPerImpact);
	}

	if (bWillBreak)
	{
		Break(Event);
	}
}

void ULootDurabilityComponent::Break(const FLootImpactEvent& CausingEvent)
{
	bIsBroken = true;

	// 기획서 5장 파손 처리 흐름의 첫 단계 — "파괴 판정 → 가치 0 처리".
	// 파괴된 노획물은 곧 사라지지만, 사라지기 전에 값을 0 으로 만들어 둬야
	// 이 액터를 참조하고 있던 쪽(적재 목록·오라클 표시)이 잘못된 금액을 읽지 않는다.
	OwnerLoot->ApplyValueLoss(1.f);

	// 되돌아가지 않는 상태다. 놓기로 덮어쓰이지 않게 ApplyCarryState 가 이 태그를 피해 간다.
	OwnerLoot->SetLootStateTag(HHTags::Loot_State_Broken);

	// 파괴는 부딪힌 소리와 별개의 사건이다. 상자가 바닥에 부딪히는 소리와
	// 깨지는 소리는 다르므로, 같은 충격에서 두 이벤트가 나가는 것이 맞다.
	// 여기서도 '깨졌다'는 물리적 사실만 알린다. 얼마나 시끄러운지는 소음 파트가 정한다.
	//
	// 액터를 지우기 전에 먼저 방송해야 한다. 구독자가 LootActor 를 유효한 상태로 받는다.
	OwnerLoot->ReportImpact(ELootImpactCause::Break, CausingEvent.ImpulseMagnitude,
		CausingEvent.ImpactPoint, CausingEvent.InstigatorPawn.Get());

	// 소음도 같은 이유로 지금 낸다. ReportTaggedNoise 는 소유 액터의 위치를 읽으므로
	// BreakDestroyDelay 뒤 소멸 예약이 걸리기 전, 액터가 살아 있는 이 시점이어야 한다.
	//
	// 배율을 1.0 으로 고정하는 것은 파괴가 '얼마나 세게 맞았나' 와 무관한 사건이라서다.
	// 살짝 금이 가서 깨지든 세게 맞아 깨지든, 부서지는 소리는 똑같이 크다.
	// 몇 % 의 경계도가 되는지는 DT_NoiseProfiles 의 Noise.Loot.Break 행이 정한다.
	if (UNoiseEmitterComponent* Noise = OwnerLoot->GetNoiseEmitter())
	{
		Noise->ReportTaggedNoise(HHTags::Noise_Loot_Break, 1.f);
	}

	if (OwnerLoot->IsImpactDebugEnabled() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Magenta,
			FString::Printf(TEXT("[%s] 파괴 — %.2f초 뒤 소멸"), *OwnerLoot->GetName(), BreakDestroyDelay));
	}

	ApplyBrokenState();

	// 바로 Destroy 하면 액터가 복제보다 먼저 사라져 클라이언트에서는 연출 없이 증발한다.
	// 이미 숨겨진 상태로 잠깐 남겨 두었다가 지운다.
	if (BreakDestroyDelay > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(DestroyTimerHandle, this,
				&ULootDurabilityComponent::DestroyOwnerLoot, BreakDestroyDelay, false);
			return;
		}
	}

	DestroyOwnerLoot();
}

void ULootDurabilityComponent::ApplyBrokenState()
{
	if (!IsValid(OwnerLoot))
	{
		// 초기 복제는 BeginPlay 보다 먼저 도착할 수 있다. 그때는 여기서 해결한다.
		OwnerLoot = Cast<ALootBase>(GetOwner());
	}

	if (!IsValid(OwnerLoot))
	{
		return;
	}

	// bActorEnableCollision 은 복제되지 않는다. 각 머신에서 따로 꺼야 한다.
	// (bHidden 은 복제되지만, 클라이언트도 즉시 반영하도록 여기서 같이 처리한다)
	OwnerLoot->SetActorEnableCollision(false);

	if (UPrimitiveComponent* PhysicsRoot = OwnerLoot->GetPhysicsRoot())
	{
		PhysicsRoot->SetSimulatePhysics(false);
	}

	OwnerLoot->SetActorHiddenInGame(true);

	// 메시를 숨긴 뒤에 뿌린다. 순서가 반대면 한 프레임 동안 멀쩡한 물건과 파편이 겹쳐 보인다.
	PlayBreakEffects();

	// 위 둘로 부족할 때를 위한 확장점. 판정은 이미 끝났다.
	OnBroken();
}

void ULootDurabilityComponent::PlayBreakEffects()
{
	// 데디케이티드 서버는 화면도 스피커도 없다. 리슨 서버의 호스트는 클라이언트이기도 하므로
	// 여기 걸리지 않는다 — 호스트 화면에서도 파편이 정상적으로 보인다.
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 액터는 곧 사라지므로 붙이지 않고 월드에 스폰한다.
	// 붙였다가는 BreakDestroyDelay 뒤에 파편이 부모와 함께 사라진다.
	const FTransform Where = OwnerLoot->GetActorTransform();

	if (IsValid(BreakEffect))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, BreakEffect, Where.GetLocation(), Where.Rotator(),
			FVector(BreakEffectScale), /*bAutoDestroy*/ true);
	}

	if (IsValid(BreakSound))
	{
		UGameplayStatics::PlaySoundAtLocation(World, BreakSound, Where.GetLocation());
	}
}

float ULootDurabilityComponent::GetDamageRatio01() const
{
	// 표를 못 읽었거나 값이 이상하면 0 을 준다. 나눗셈을 막는 것보다
	// "멀쩡해 보인다" 가 "다 갈라져 보인다" 보다 오해가 적다.
	if (Data.MaxImpactCount <= 0)
	{
		return 0.f;
	}

	// 한 방에 깨지는 물건은 '깨지기 직전' 이라는 상태 자체가 없다. 0 을 준다.
	// (아래 식의 분모가 0 이 되는 경우이기도 하다)
	if (Data.MaxImpactCount == 1)
	{
		return 0.f;
	}

	/**
	 * 분모가 MaxImpactCount 가 아니라 그보다 하나 적다.
	 *
	 * 마지막 충격에서는 ++ImpactCount 직후 Break() 가 같은 프레임에 메시를 숨긴다.
	 * 그래서 ImpactCount / MaxImpactCount 로 두면 1.0 이 화면에 한 번도 안 나오고,
	 * 3회짜리 물건의 경우 시각 범위의 위쪽 3분의 1이 통째로 죽는다.
	 *
	 * 하나 적게 나누면 '파괴 직전' 이 정확히 1.0 이 되어, 잡아 둔 연출이 전부 쓰이고
	 * "한 번만 더 부딪히면 깨진다" 가 가장 강한 모습으로 나온다.
	 * 물건을 안고 뛰는 게임이라 이 신호는 셀수록 좋다.
	 */
	const float Ratio = static_cast<float>(ImpactCount) / static_cast<float>(Data.MaxImpactCount - 1);
	return FMath::Clamp(Ratio, 0.f, 1.f);
}

void ULootDurabilityComponent::ApplyCrackVisual()
{
	if (CrackParameterName.IsNone() || !IsValid(OwnerLoot))
	{
		return;
	}

	UPrimitiveComponent* Mesh = OwnerLoot->GetPhysicsRoot();
	if (!IsValid(Mesh))
	{
		return;
	}

	const int32 SlotCount = Mesh->GetNumMaterials();

	// 첫 충격에서만 만든다. 안 부딪힌 노획물은 MID 를 하나도 들지 않는다.
	if (CrackMaterials.Num() != SlotCount)
	{
		CrackMaterials.Reset(SlotCount);

		bool bAnySlotHasParameter = false;
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(Slot);
			CrackMaterials.Add(MID);

			float Unused = 0.f;
			if (IsValid(MID) && MID->GetScalarParameterValue(FMaterialParameterInfo(CrackParameterName), Unused))
			{
				bAnySlotHasParameter = true;
			}
		}

		// 파라미터가 없으면 SetScalarParameterValue 는 조용히 아무 일도 안 한다.
		// 경고가 없으면 "머티리얼을 만들었는데 왜 안 갈라지지" 로 한참 헤맨다.
		if (!bAnySlotHasParameter && !bWarnedMissingCrackParameter)
		{
			bWarnedMissingCrackParameter = true;
			UE_LOG(LogLoot, Warning,
				TEXT("[Loot:%s] 머티리얼에 스칼라 파라미터 '%s' 가 없다. 균열 연출이 나오지 않는다 ")
				TEXT("(머티리얼에 파라미터를 추가하거나 CrackParameterName 을 None 으로 둘 것)"),
				*GetNameSafe(OwnerLoot), *CrackParameterName.ToString());
		}
	}

	const float Ratio = GetDamageRatio01();
	for (UMaterialInstanceDynamic* MID : CrackMaterials)
	{
		if (IsValid(MID))
		{
			MID->SetScalarParameterValue(CrackParameterName, Ratio);
		}
	}
}

void ULootDurabilityComponent::DestroyOwnerLoot()
{
	// 파괴는 서버 권위다. 클라이언트는 액터가 사라지는 것을 복제로 받는다.
	if (IsValid(OwnerLoot) && OwnerLoot->HasAuthority())
	{
		OwnerLoot->Destroy();
	}
}

void ULootDurabilityComponent::OnRep_ImpactCount()
{
	// 초기 복제는 BeginPlay 보다 먼저 도착할 수 있다. 그때는 여기서 해결한다.
	// ResolveData 가 OwnerLoot 확보까지 같이 하고, 두 번째부터는 즉시 반환한다.
	ResolveData();

	// 머티리얼 균열이 먼저다. BP 가 OnDamageAccumulated 에서 파티클을 붙일 때
	// 이미 갈라진 상태를 보고 있어야 연출이 어긋나지 않는다.
	ApplyCrackVisual();

	OnDamageAccumulated(ImpactCount, Data.MaxImpactCount);
}

void ULootDurabilityComponent::OnRep_IsBroken()
{
	if (bIsBroken)
	{
		ApplyBrokenState();
	}
}
