#include "Loot/LootStabilityComponent.h"

#include "Core/HeavyHandedGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Loot/LootBase.h"
#include "Loot/LootLog.h"
#include "Loot/LootSettings.h"
#include "Loot/LootTypes.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Noise/NoiseEmitterComponent.h" // 유출 소음 — 충돌이 아니라 내 판정으로 나간다

void ULootStabilityComponent::ResolveData()
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

	const FLootStabilityData* Row = ULootSettings::FindTraitRow<FLootStabilityData>(
		ULootSettings::Get()->StabilityTable, RowName, GetName());

	if (!Row)
	{
		// 컴포넌트가 붙었다는 것은 불안정형으로 만들겠다는 선언인데 표에 행이 없다.
		// 기본값(60도)으로 돌기 때문에 조용히 넘어가면 "왜 다른 물건이랑 똑같지" 가 된다.
		// 반대 방향(행은 있는데 컴포넌트가 없다)은 ALootBase 가 잡는다.
		UE_LOG(LogLoot, Warning,
			TEXT("[Loot:%s] ULootStabilityComponent 가 붙어 있는데 DT_LootStability 에 '%s' 행이 없다 "
				 "— 기본값으로 돈다. 표에 행을 추가하거나 Project Settings > Game > Loot 에서 "
				 "Stability Table 이 지정돼 있는지 확인할 것"),
			*OwnerLoot->GetName(), *RowName.ToString());
		return;
	}

	Data = *Row;
}

ULootStabilityComponent::ULootStabilityComponent()
{
	// 기울어짐은 사건이 아니라 상태라서 매 프레임 봐야 한다.
	// 대신 클라이언트에서는 BeginPlay 에서 꺼 버린다 (판정은 서버 전용).
	PrimaryComponentTick.bCanEverTick = true;

	SetIsReplicatedByDefault(true);
}

void ULootStabilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ULootStabilityComponent, ReplicatedTilt01);
	DOREPLIFETIME(ULootStabilityComponent, SpillCount);
	DOREPLIFETIME(ULootStabilityComponent, bIsSpilling);
}

void ULootStabilityComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerLoot = Cast<ALootBase>(GetOwner());
	if (!IsValid(OwnerLoot))
	{
		// 운반자·가치·물리 루트를 전부 ALootBase 에서 읽으므로 다른 액터에는 붙을 수 없다.
		UE_LOG(LogLoot, Warning,
			TEXT("[%s] ULootStabilityComponent 는 ALootBase 에만 붙일 수 있다. 유출 판정이 비활성화된다."),
			*GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	// 이 컴포넌트가 붙어 있다는 것이 곧 "불안정형" 이라는 선언이다.
	OwnerLoot->AddLootTypeTag(HHTags::Loot_Type_Unstable);

	// 수치는 그다음에 채운다. ALootBase::PostInitializeComponents 가 이미 돌아서
	// 노획물의 행 이름이 확정돼 있다.
	ResolveData();

	// 판정도 기울기 계산도 서버 몫이다. 클라이언트는 복제된 값으로 연출만 맞춘다.
	SetComponentTickEnabled(OwnerLoot->HasAuthority());
}

void ULootStabilityComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(OwnerLoot) || !OwnerLoot->HasAuthority())
	{
		return;
	}

	const APawn* Carrier = OwnerLoot->GetPrimaryCarrier();

	if (IsValid(Carrier))
	{
		// 소지 중 — 기울기를 이동에서 만든다.
		// 그레이스는 쓰지 않는다. 누적값이라 이미 시간이 반영돼 있고,
		// 한계에 닿았다는 것 자체가 '오래 무리했다'는 뜻이다.
		const float TiltDegrees = UpdateCarriedTilt(Carrier, DeltaTime);
		UpdateSpill(TiltDegrees, DeltaTime, /*bUseGrace=*/false);
		return;
	}

	// 놓여 있음 — 실제 물리 회전을 읽는다.
	// 소지 중 누적값은 여기서 버린다. 놓는 순간 기울어진 자세가 그대로 물리 회전이 되므로
	// 위태롭게 들고 오다 급히 내려놓으면 그 자리에서 넘어져 샌다. 두 모드가 이렇게 이어진다.
	if (CarriedTiltDegrees > 0.f)
	{
		CarriedTiltDegrees = 0.f;
		TiltDirection = FVector::ZeroVector;   // 다음에 들 때 그때의 이동 방향으로 다시 잡는다
		PushReplicatedTilt(0.f);
	}

	UpdateSpill(GetPhysicalTiltDegrees(), DeltaTime, /*bUseGrace=*/true);
}

float ULootStabilityComponent::UpdateCarriedTilt(const APawn* Carrier, float DeltaTime)
{
	UpdateTiltDirection(Carrier, DeltaTime);


	// 수직 속도는 빼고 본다. 점프나 계단 낙하로 내용물이 쏟아지는 것은 이 규칙의 의도가 아니다.
	const float Speed = Carrier->GetVelocity().Size2D();

	if (Speed > Data.SafeCarrySpeed)
	{
		// 초과분에 비례해 쌓는다. 빠를수록, 오래갈수록 많이 쌓인다.
		CarriedTiltDegrees += (Speed - Data.SafeCarrySpeed) * Data.TiltGainPerSpeed * DeltaTime;
	}
	else
	{
		// 천천히 가거나 멈추면 다시 선다. 멈춰서 숨 고르는 선택지가 있어야 한다.
		CarriedTiltDegrees -= Data.TiltRecoverRate * DeltaTime;
	}

	CarriedTiltDegrees = FMath::Clamp(CarriedTiltDegrees, 0.f, Data.SpillTiltAngle);

	ApplyCarriedLean(CarriedTiltDegrees);
	PushReplicatedTilt(CarriedTiltDegrees);

	return CarriedTiltDegrees;
}

float ULootStabilityComponent::GetPhysicalTiltDegrees() const
{
	// 노획물의 위쪽이 월드 위쪽에서 얼마나 벌어졌는가.
	const float TiltCos = FVector::DotProduct(OwnerLoot->GetActorUpVector(), FVector::UpVector);

	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(TiltCos, -1.f, 1.f)));
}

void ULootStabilityComponent::UpdateSpill(float TiltDegrees, float DeltaTime, bool bUseGrace)
{

	if (TiltDegrees < Data.SpillTiltAngle)
	{
		TiltedSeconds = 0.f;
		SetSpilling(false);
		return;
	}

	TiltedSeconds += DeltaTime;

	// 던져서 회전하는 중에는 각도가 계속 바뀌므로 여기서 걸린다.
	// 넘어져 있거나 굴러가는 중이면 계속 누적돼 통과한다.
	if (bUseGrace && TiltedSeconds < Data.TiltGraceSeconds)
	{
		SetSpilling(false);
		return;
	}

	// 바닥까지 다 샜으면 더 기울여도 잃을 것이 없다. 이펙트도 멎어야 한다.
	// 계속 흘리면 "아직 손해 보는 중" 이라고 거짓말을 하게 된다.
	if (IsValueDrained())
	{
		SetSpilling(false);
		return;
	}

	// 여기까지 왔으면 새고 있다. 아래 간격 검사는 '가치를 언제 깎을까' 일 뿐이고,
	// 그림은 깎이는 순간이 아니라 새는 내내 나와야 한다.
	SetSpilling(true);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 기울어져 있는 동안 계속 샌다. "빨리 세워라" 는 압박이 여기서 나온다.
	if (World->GetTimeSeconds() - LastSpillTime < Data.SpillIntervalSeconds)
	{
		return;
	}

	LastSpillTime = World->GetTimeSeconds();
	Spill(TiltDegrees);
}

bool ULootStabilityComponent::IsValueDrained() const
{
	if (!IsValid(OwnerLoot))
	{
		return true;
	}

	const int32 FloorValue = FMath::RoundToInt(OwnerLoot->GetBaseValue() * Data.MinValueRatio);
	return OwnerLoot->GetCurrentValue() <= FloorValue;
}

void ULootStabilityComponent::Spill(float TiltDegrees)
{
	// 바닥까지 샜으면 더 깎지 않는다. 가치 0 은 파손형의 몫이다.
	// UpdateSpill 이 이미 걸러 주지만, 이 함수만 따로 불릴 여지를 남기지 않는다.
	if (IsValueDrained())
	{
		return;
	}

	const int32 CurrentValue = OwnerLoot->GetCurrentValue();
	const int32 FloorValue = FMath::RoundToInt(OwnerLoot->GetBaseValue() * Data.MinValueRatio);

	const int32 TargetValue = FMath::Max(
		FMath::RoundToInt(CurrentValue * (1.f - Data.SpillValueLossRatio)), FloorValue);

	// ALootBase 는 비율로 받는다. 바닥에 걸린 경우를 위해 목표 금액을 비율로 되돌린다.
	const float ActualRatio = 1.f - static_cast<float>(TargetValue) / static_cast<float>(CurrentValue);
	OwnerLoot->ApplyValueLoss(ActualRatio);

	++SpillCount;

	// 서버에서 값을 직접 바꾸면 RepNotify 가 불리지 않는다. 서버 몫은 손으로 부른다.
	OnRep_SpillCount();

	// 유출은 물리 충돌이 아니라 기울기 판정으로 나는 사건이라, 소음 컴포넌트가 스스로
	// 알 방법이 없다. 여기서 직접 알려 준다.
	//
	// 이 함수는 기울어져 있는 동안 주기적으로 다시 불린다. 부를 때마다 발행하지만
	// 연속으로 시끄러워지지는 않는다 — DT_NoiseProfiles 의 Noise.Loot.Spill 행에 있는
	// CooldownSeconds 로 소음 파트의 스팸 필터가 묶어 준다. (내 쪽에서 또 막으면 두 벌이 된다)
	if (UNoiseEmitterComponent* Noise = OwnerLoot->GetNoiseEmitter())
	{
		Noise->ReportTaggedNoise(HHTags::Noise_Loot_Spill, 1.f);
	}

	if (OwnerLoot->IsImpactDebugEnabled() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Turquoise,
			FString::Printf(TEXT("[%s] 유출 %d회 — 기울기 %.0f도, 가치 %d"),
				*OwnerLoot->GetName(), SpillCount, TiltDegrees, OwnerLoot->GetCurrentValue()));
	}
}

void ULootStabilityComponent::SetSpilling(bool bNewSpilling)
{
	if (bIsSpilling == bNewSpilling)
	{
		return;
	}

	bIsSpilling = bNewSpilling;

	// 서버에서 값을 직접 바꾸면 RepNotify 가 불리지 않는다. 서버 몫은 손으로 부른다.
	OnRep_IsSpilling();
}

void ULootStabilityComponent::OnRep_IsSpilling()
{
	ApplySpillEffect();
}

void ULootStabilityComponent::ApplySpillEffect()
{
	// 데디케이티드 서버는 화면이 없다. 리슨 서버의 호스트는 클라이언트이기도 하므로
	// 여기 걸리지 않는다 — 호스트 화면에서도 정상적으로 보인다.
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !IsValid(SpillEffect))
	{
		return;
	}

	if (!IsValid(OwnerLoot))
	{
		// 초기 복제는 BeginPlay 보다 먼저 도착할 수 있다. 그때는 여기서 해결한다.
		OwnerLoot = Cast<ALootBase>(GetOwner());
		if (!IsValid(OwnerLoot))
		{
			return;
		}
	}

	if (!bIsSpilling)
	{
		// 파괴하지 않고 끈다. 이미 나와 있는 입자는 자연스럽게 사라지고,
		// 다시 기울면 같은 컴포넌트를 켜서 쓴다 — 굴러가는 동안 껐다 켜기를 반복한다.
		if (IsValid(SpillEffectComponent))
		{
			SpillEffectComponent->Deactivate();
		}
		return;
	}

	if (IsValid(SpillEffectComponent))
	{
		SpillEffectComponent->Activate(/*bReset=*/true);
		return;
	}

	USceneComponent* Mesh = OwnerLoot->GetPhysicsRoot();
	if (!IsValid(Mesh))
	{
		return;
	}

	// 소켓이 없으면 붙일 자리를 원점으로 떨어뜨린다. 조용히 넘어가면
	// "이펙트가 물건 한가운데서 나온다" 는 증상으로만 드러나 원인까지 오래 걸린다.
	FName Socket = SpillSocketName;
	if (!Socket.IsNone() && !Mesh->DoesSocketExist(Socket))
	{
		if (!bWarnedMissingSpillSocket)
		{
			bWarnedMissingSpillSocket = true;
			UE_LOG(LogLoot, Warning,
				TEXT("[Loot:%s] 메시에 '%s' 소켓이 없다. 유출 이펙트가 물건 한가운데서 나온다 ")
				TEXT("(메시에 소켓을 추가하거나 SpillSocketName 을 고칠 것)"),
				*GetNameSafe(OwnerLoot), *Socket.ToString());
		}
		Socket = NAME_None;
	}

	// 붙여야 한다. 월드에 스폰하면 물건이 구르는 동안 이펙트만 제자리에 남는다.
	SpillEffectComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		SpillEffect, Mesh, Socket, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, /*bAutoDestroy=*/false);
}

void ULootStabilityComponent::UpdateTiltDirection(const APawn* Carrier, float DeltaTime)
{
	FVector MoveDirection = Carrier->GetVelocity();
	MoveDirection.Z = 0.f;

	if (MoveDirection.IsNearlyZero())
	{
		// 멈춰 있으면 방향을 갱신하지 않는다. 마지막으로 쏠린 쪽에 그대로 있다가 회복된다.
		// 처음 들었을 때만 기준을 잡아 준다.
		if (TiltDirection.IsNearlyZero())
		{
			TiltDirection = Carrier->GetActorForwardVector().GetSafeNormal2D();
		}
		return;
	}

	MoveDirection.Normalize();

	if (TiltDirection.IsNearlyZero())
	{
		TiltDirection = MoveDirection;
		return;
	}

	// 방향을 즉시 바꾸면 좌우로 왔다 갔다 할 때 물건이 순간이동하듯 꺾인다.
	TiltDirection = FMath::VInterpNormalRotationTo(
		TiltDirection, MoveDirection, DeltaTime, Data.TiltDirectionTurnRate);
}

void ULootStabilityComponent::ApplyCarriedLean(float TiltDegrees)
{
	// 기울어지는 게 눈에 보여야 플레이어가 속도를 조절할 줄 알게 된다.
	// 소지 중에는 물리가 꺼져 있고 손 소켓에 붙어 있으므로 상대 회전으로 넣는다.
	//
	// 복제는 따로 하지 않는다. 어태치된 액터의 상대 트랜스폼은 이동 복제(SetReplicateMovement)가
	// 같이 실어 보내므로 클라이언트도 같은 자세로 보인다.
	USceneComponent* Root = OwnerLoot->GetRootComponent();
	if (!Root)
	{
		return;
	}

	// 관성이므로 물건 윗부분은 이동 방향의 '반대쪽'으로 넘어간다.
	// 앞으로 걸어가면 물건이 내 쪽으로 기운다.
	//
	// 축을 (이동방향 x 위)로 잡으면 오른손 법칙에 따라 윗면이 -이동방향으로 돈다.
	const FVector WorldAxis = FVector::CrossProduct(TiltDirection, FVector::UpVector).GetSafeNormal();
	if (WorldAxis.IsNearlyZero())
	{
		return;
	}

	// 기울기는 월드 기준(중력 방향 기준)인데 넣는 값은 부모(손 소켓) 기준 상대 회전이다.
	// 부모가 어느 쪽을 보고 있든 같은 결과가 나오도록 축을 부모 공간으로 되돌린다.
	const USceneComponent* Parent = Root->GetAttachParent();
	const FQuat ParentQuat = Parent
		? Parent->GetSocketQuaternion(Root->GetAttachSocketName()) : FQuat::Identity;

	const FVector LocalAxis = ParentQuat.UnrotateVector(WorldAxis);

	OwnerLoot->SetActorRelativeRotation(FQuat(LocalAxis, FMath::DegreesToRadians(TiltDegrees)));
}

void ULootStabilityComponent::PushReplicatedTilt(float TiltDegrees)
{
	const float SpillTiltAngle = Data.SpillTiltAngle;

	const float Tilt01 = (SpillTiltAngle > 0.f)
		? FMath::Clamp(TiltDegrees / SpillTiltAngle, 0.f, 1.f) : 0.f;

	const uint8 Quantized = static_cast<uint8>(FMath::RoundToInt(Tilt01 * 255.f));
	if (Quantized == ReplicatedTilt01)
	{
		return;
	}

	ReplicatedTilt01 = Quantized;
	OnRep_ReplicatedTilt01();
}

void ULootStabilityComponent::OnRep_ReplicatedTilt01()
{
	OnCarriedTiltChanged(GetTilt01());
}

void ULootStabilityComponent::OnRep_SpillCount()
{
	if (!IsValid(OwnerLoot))
	{
		// 초기 복제는 BeginPlay 보다 먼저 도착할 수 있다. 그때는 여기서 해결한다.
		OwnerLoot = Cast<ALootBase>(GetOwner());
	}

	OnSpilled(SpillCount, IsValid(OwnerLoot) ? GetPhysicalTiltDegrees() : 0.f);
}
