#include "Cart/HandCart.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HeavyHandedGameplayTags.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"                 // TActorIterator (임시 콘솔 명령)
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Loot/LootBase.h"
#include "Loot/LootLog.h"
#include "Net/UnrealNetwork.h"           // DOREPLIFETIME
#include "Noise/NoiseSubsystem.h"        // 걸러낸 충돌만 직접 발행한다

namespace
{
	/**
	 * 카트 몸체의 콜리전 프로파일.
	 *
	 * 노획물의 시뮬레이션 프로파일을 그대로 쓴다. 이 프로파일은 Camera 와 NoiseOcclusion 만
	 * 무시하고 나머지를 전부 Block 하는데, 카트에 필요한 것이 정확히 그것이다 —
	 * 특히 Pawn 을 막아야 벽에 걸린 카트가 미는 사람도 세울 수 있다.
	 *
	 * 오브젝트 타입이 "Loot" 로 잡히는 것이 걸리지만, 지금 노획물을 고르는 코드는
	 * 콜리전 채널이 아니라 Loot.Type 태그로 판정한다(GAB_Interact). 카트에는 그 태그가 없으니
	 * 집기 대상으로 잡히지 않는다. 채널로 거르는 코드가 생기면 그때 전용 프로파일을 만든다.
	 */
	static const FName CartCollisionProfile(TEXT("Loot"));

	/**
	 * 적재 판정 볼륨의 프로파일. 노획물만 Overlap 하고 나머지는 전부 Ignore 한다.
	 *
	 * 엔진 기본 프로파일(OverlapAllDynamic 등)을 쓰면 안 된다. Loot 은 나중에 추가한
	 * 커스텀 채널이라 엔진 프로파일에는 응답이 적혀 있지 않고, 그러면 채널 기본값인
	 * Block 이 적용돼 오버랩 이벤트가 한 번도 오지 않는다. (Config/DefaultEngine.ini)
	 */
	static const FName CartLoadZoneProfile(TEXT("CartLoadZone"));

	/** 적재면 위 공간의 기본 크기(cm). 임시 메시 기준이라 실제 카트가 오면 BP 에서 맞춘다 */
	static const FVector DefaultLoadExtent(60.f, 40.f, 30.f);
	static const FVector DefaultLoadOffset(0.f, 0.f, 60.f);
}

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * [임시] 가장 가까운 카트를 잡거나 놓는다.
	 *
	 * 카트를 잡는 정식 경로는 상호작용 키(UGAB_Interact)인데, 그 파일은 플레이어 파트라
	 * 내가 고치지 않는다. 두 줄이 들어가기 전까지 끌기를 검증할 방법이 없어서 임시로 연다.
	 * 저쪽에 아래 두 줄이 들어가면 이 명령은 지운다.
	 *
	 *     else if (AHandCart* Cart = Cast<AHandCart>(HitActor))
	 *     {
	 *         Cart->TryTogglePush(Character);
	 *     }
	 *
	 * 리슨 서버 호스트 창에서만 동작한다. TryTogglePush 가 서버 전용이기 때문이다 —
	 * 클라이언트 창에서 쓰려면 Server RPC 가 필요한데, 그건 정식 경로가 이미 갖고 있다.
	 */
	void ToggleNearestCart(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		const APlayerController* PC = World->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
		{
			UE_LOG(LogLoot, Warning, TEXT("[Cart] 조종 중인 폰이 없다 — 관전자 상태에서는 카트를 잡을 수 없다"));
			return;
		}

		AHandCart* Nearest = nullptr;
		float NearestDistSq = TNumericLimits<float>::Max();
		for (TActorIterator<AHandCart> It(World); It; ++It)
		{
			const float DistSq = FVector::DistSquared(It->GetActorLocation(), Pawn->GetActorLocation());
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = *It;
			}
		}

		if (!Nearest)
		{
			UE_LOG(LogLoot, Warning, TEXT("[Cart] 레벨에 카트가 없다"));
			return;
		}

		if (!Nearest->HasAuthority())
		{
			UE_LOG(LogLoot, Warning, TEXT("[Cart] 이 명령은 호스트 창에서만 동작한다"));
			return;
		}

		Nearest->TryTogglePush(Pawn);
	}
}

static FAutoConsoleCommandWithWorld GCartTogglePushCmd(
	TEXT("hh.Cart.TogglePush"),
	TEXT("[임시] 가장 가까운 카트를 잡거나 놓는다. 상호작용 키가 붙기 전까지 쓰는 검증용 경로."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&ToggleNearestCart));
#endif

AHandCart::AHandCart()
{
	// 끌고 있는 동안에만 돈다. 맵에 놓인 카트가 아무것도 안 하면서 틱하지 않게 한다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	bReplicates = true;
	SetReplicateMovement(true);

	// 물리 물체는 서버 스냅샷 간격이 곧 클라이언트가 보는 끊김이 된다.
	// 노획물과 같은 값으로 맞춘다.
	NetUpdateFrequency = 60.f;
	MinNetUpdateFrequency = 20.f;

	CartMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CartMesh"));
	SetRootComponent(CartMesh);
	CartMesh->SetCollisionProfileName(CartCollisionProfile);
	CartMesh->SetSimulatePhysics(true);

	// 이게 없으면 OnComponentHit 이 아예 오지 않는다 — 벽에 박아도 소리가 안 난다.
	CartMesh->SetNotifyRigidBodyCollision(true);

	// 앞뒤·좌우 회전을 잠근다. Z(요)만 남겨서 방향 전환은 되고 뒤집히지는 않게 한다.
	// SixDOF 로 놓지 않으면 개별 축 잠금이 무시된다.
	CartMesh->BodyInstance.DOFMode = EDOFMode::SixDOF;
	CartMesh->BodyInstance.bLockXRotation = true;
	CartMesh->BodyInstance.bLockYRotation = true;

	LoadVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("LoadVolume"));
	LoadVolume->SetupAttachment(CartMesh);
	LoadVolume->SetBoxExtent(DefaultLoadExtent);
	LoadVolume->SetRelativeLocation(DefaultLoadOffset);

	// 판정용 볼륨이라 물리에는 관여하지 않는다. 물건을 담아 두는 것은 카트 메시의 턱이 한다.
	LoadVolume->SetCollisionProfileName(CartLoadZoneProfile);
	LoadVolume->SetGenerateOverlapEvents(true);

	// 판정 볼륨이지 적재함 벽이 아니다. 게임 화면에 보이면 안 된다.
	LoadVolume->SetHiddenInGame(true);

}

void AHandCart::BeginPlay()
{
	Super::BeginPlay();

	// 메시가 없으면 물리 바디가 안 만들어진다. 그러면 밀어도 안 움직이고 벽도 못 막는데,
	// 화면에는 BP 에서 따로 붙인 다른 메시가 보여서 "카트는 있는데 왜 안 밀리지" 가 된다.
	// BP 에서 Cube 컴포넌트를 새로 추가하는 대신 CartMesh 에 메시를 지정해야 한다.
	if (!CartMesh->GetStaticMesh())
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[Cart:%s] CartMesh 에 스태틱 메시가 없다 — 물리 바디가 만들어지지 않아 "
				 "밀리지도 않고 벽에 막히지도 않는다. BP 에 메시 컴포넌트를 새로 추가하지 말고 "
				 "CartMesh 의 Static Mesh 칸에 지정할 것"),
			*GetName());
	}
	else if (!CartMesh->IsSimulatingPhysics())
	{
		// 루트가 Static/Stationary 면 물리가 안 돈다. 에디터에서 실수로 바꾸는 일이 잦다.
		UE_LOG(LogLoot, Warning,
			TEXT("[Cart:%s] CartMesh 가 물리 시뮬레이션을 하지 않는다 — Mobility 가 Movable 인지, "
				 "Simulate Physics 가 켜져 있는지 확인할 것"),
			*GetName());
	}

	CartMesh->SetMassOverrideInKg(NAME_None, MassKg, true);

	if (!bLockTipping)
	{
		// BP 에서 껐으면 생성자에서 걸어 둔 잠금을 푼다.
		CartMesh->BodyInstance.bLockXRotation = false;
		CartMesh->BodyInstance.bLockYRotation = false;
		CartMesh->BodyInstance.SetDOFLock(EDOFMode::SixDOF);
	}

	// 클라이언트는 예측하지 않고 서버 스냅샷을 향해 보간만 한다. 노획물과 같은 정책이다.
	SetPhysicsReplicationMode(EPhysicsReplicationMode::PredictiveInterpolation);

	// 적재 판정은 서버가 정한다. 클라이언트에서 각자 세면 사람마다 다른 답이 나온다.
	if (HasAuthority())
	{
		LoadVolume->OnComponentBeginOverlap.AddDynamic(this, &AHandCart::HandleLoadBeginOverlap);
		LoadVolume->OnComponentEndOverlap.AddDynamic(this, &AHandCart::HandleLoadEndOverlap);
		CartMesh->OnComponentHit.AddDynamic(this, &AHandCart::HandleCartHit);

		// 레벨에 물건이 이미 실린 채로 배치돼 있을 수 있다.
		// 그 경우 BeginOverlap 이 오지 않으므로 여기서 한 번 훑는다.
		TArray<AActor*> Overlapping;
		LoadVolume->GetOverlappingActors(Overlapping, ALootBase::StaticClass());
		for (AActor* Actor : Overlapping)
		{
			ContainLoot(Cast<ALootBase>(Actor));
		}

		WarnOnMissingGripSockets();
	}
}

void AHandCart::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 등록을 빠뜨려도 컴파일 에러도 경고도 없고, 호스트에서는 멀쩡히 돌아서 발견이 늦는다.
	DOREPLIFETIME(AHandCart, CurrentPusher);
}

void AHandCart::WarnOnMissingGripSockets() const
{
	const UStaticMesh* Mesh = CartMesh ? CartMesh->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return;   // 메시 자체가 없는 경우는 BeginPlay 가 이미 경고했다
	}

	const FString MeshName = Mesh->GetName();

	// 1) 이름이 비었나
	if (GripSocketLeft.IsNone() || GripSocketRight.IsNone())
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[Cart:%s] 그립 소켓 이름이 비어 있다 (L='%s' R='%s') — 잡아도 카트가 "
				 "사람 몸에 박힌 채 따라온다"),
			*GetName(), *GripSocketLeft.ToString(), *GripSocketRight.ToString());
		return;
	}

	// 2) 둘이 같은 이름인가. 그러면 간격이 0 이 되어 좌우가 사라진다
	if (GripSocketLeft == GripSocketRight)
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[Cart:%s] 좌우 그립 소켓이 같은 이름이다 ('%s')"),
			*GetName(), *GripSocketLeft.ToString());
		return;
	}

	// 3) 메시에 실제로 있나. 대소문자까지 정확해야 한다
	for (const FName& SocketName : { GripSocketLeft, GripSocketRight })
	{
		if (!CartMesh->DoesSocketExist(SocketName))
		{
			UE_LOG(LogLoot, Warning,
				TEXT("[Cart:%s] 메시 '%s' 에 소켓 '%s' 가 없다 — 이름은 대소문자까지 일치해야 한다"),
				*GetName(), *MeshName, *SocketName.ToString());
		}
	}

	// 간격은 검사하지 않는다.
	//
	// 중량형은 그립 간격이 곧 두 사람 사이의 거리 제약이라 0 이면 기능이 성립하지 않았지만,
	// 카트는 한 사람이 두 손으로 잡는 것이라 간격이 아무것도 결정하지 않는다.
	// 추종에 쓰는 것은 두 그립의 중점 하나뿐이다. (2026-08-21)
}

FTransform AHandCart::GetGripTransform(bool bLeft) const
{
	const FName SocketName = bLeft ? GripSocketLeft : GripSocketRight;

	if (IsValid(CartMesh) && !SocketName.IsNone() && CartMesh->DoesSocketExist(SocketName))
	{
		return CartMesh->GetSocketTransform(SocketName);
	}

	// 소켓이 없으면 카트 원점. 설정 실수는 BeginPlay 경고가 알린다.
	return GetActorTransform();
}

FVector AHandCart::GetStandLocation() const
{
	const FVector GripMid = (GetGripTransform(true).GetLocation() + GetGripTransform(false).GetLocation()) * 0.5f;

	// 손잡이는 카트 뒤쪽에 있으므로, 사람은 그립 중점에서 카트 뒤(-Forward)로 더 물러선 자리에 선다.
	// 카트의 '앞' 은 액터 정면이 아니라 메시 축 보정을 거친 방향이다.
	const FRotator CartFacing(0.f, GetActorRotation().Yaw + MeshForwardYawOffset, 0.f);
	return GripMid - CartFacing.Vector() * StandOffset;
}

void AHandCart::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 카트가 사라지는데 노획물이 조용한 채로 남으면, 그 물건은 남은 판 내내 소리를 안 낸다.
	// 소음 억제는 카트가 걸어 준 것이라 카트가 풀어 주고 나가야 한다.
	for (int32 Index = ContainedLoot.Num() - 1; Index >= 0; --Index)
	{
		if (ALootBase* Loot = ContainedLoot[Index].Get())
		{
			Loot->SetContainingCart(nullptr);
		}
	}
	ContainedLoot.Reset();

	Super::EndPlay(EndPlayReason);
}

bool AHandCart::IsContaining(const ALootBase* Loot) const
{
	return Loot && ContainedLoot.Contains(Loot);
}

// ──────────────────────────────────────────────────────────────
// 끌기
// ──────────────────────────────────────────────────────────────

void AHandCart::TryTogglePush(APawn* Pawn)
{
	// Server RPC 는 요청일 뿐이다. 판정은 서버가 한다.
	if (!HasAuthority() || !IsValid(Pawn))
	{
		return;
	}

	// 잡고 있던 사람이 다시 누르면 놓는다.
	if (CurrentPusher.Get() == Pawn)
	{
		StopPush();
		return;
	}

	// 한 번에 한 명만 끈다.
	//
	// 둘이 동시에 잡으면 각자 다른 목표 지점을 요구해서 카트가 두 사람 사이에서 진동한다.
	// 기획서의 "1인이 밀어서 운반" 과도 맞고, 무엇보다 카트의 값어치가 '인원을 푸는 것' 이라
	// 두 명이 붙잡혀 있으면 살 이유가 없어진다.
	if (IsValid(CurrentPusher))
	{
		UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] %s 의 잡기 거부 — 이미 %s 가 끌고 있다"),
			*GetName(), *Pawn->GetName(), *CurrentPusher->GetName());
		return;
	}

	CurrentPusher = Pawn;
	OnRep_CurrentPusher();      // 서버에서 값을 직접 바꾸면 OnRep 이 안 불린다
	SetActorTickEnabled(true);

	UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] %s 가 끌기 시작"), *GetName(), *Pawn->GetName());
}

void AHandCart::StopPush()
{
	if (!HasAuthority() || !CurrentPusher)
	{
		return;
	}

	UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] %s 가 끌기 종료"),
		*GetName(), *GetNameSafe(CurrentPusher));

	CurrentPusher = nullptr;
	OnRep_CurrentPusher();
	SetActorTickEnabled(false);

	// 손을 놓는 순간 남아 있던 추종 속도로 카트가 혼자 미끄러져 나가지 않게 한다.
	// 각속도만 끊는다 — 선속도까지 0 으로 만들면 밀던 기세가 뚝 끊겨 어색하다.
	if (IsValid(CartMesh) && CartMesh->IsSimulatingPhysics())
	{
		CartMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void AHandCart::OnRep_CurrentPusher()
{
	// 지금은 알릴 곳이 없다. 손 붙이기·UI 연출이 붙을 자리다.
	// 클라이언트에서는 틱을 켜지 않는다 — 추종은 서버만 계산하고 결과만 복제된다.
}

void AHandCart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 물리 추종은 서버 권위다. 클라이언트가 각자 계산하면 사람마다 카트가 다른 데 있게 된다.
	if (!HasAuthority())
	{
		return;
	}

	if (!IsValid(CurrentPusher))
	{
		// 끌던 사람이 사라졌다(접속 종료·파괴). 카트가 유령을 쫓지 않게 여기서 끊는다.
		StopPush();
		return;
	}

	UpdateFollow(DeltaSeconds);
}

bool AHandCart::ComputeFollowTarget(FVector& OutLocation, FQuat& OutRotation) const
{
	if (!IsValid(CurrentPusher) || !IsValid(CartMesh))
	{
		return false;
	}

	// 사람이 보는 방향(요만)을 카트 정면으로 삼는다.
	//
	// 컨트롤 회전을 쓰는 것은 화면을 돌리면 카트가 따라 도는 감각(R.E.P.O) 때문이다.
	// 폰 회전을 쓰면 캐릭터가 실제로 도는 것보다 화면이 먼저 돌아서 반응이 굼떠 보인다.
	// 피치는 버린다 — 하늘을 보면 카트가 뜨면 안 된다.
	FRotator ViewRotation = CurrentPusher->GetControlRotation();
	if (ViewRotation.IsNearlyZero())
	{
		// 컨트롤러가 없는 폰(AI·테스트용)이면 폰 자신의 방향을 쓴다
		ViewRotation = CurrentPusher->GetActorRotation();
	}
	// 메시의 정면이 로컬 +X 가 아니면 그만큼 되돌린다.
	// 이 보정이 없으면 카트가 옆으로 서고, 아래의 그립 역산도 같이 틀어져 목표가 사람 몸 안으로 들어간다.
	const FRotator YawOnly(0.f, ViewRotation.Yaw - MeshForwardYawOffset, 0.f);
	OutRotation = YawOnly.Quaternion();

	// 사람이 바라보는 방향은 보정 전 값이다. 카트를 어느 쪽에 둘지는 메시 축과 무관하다.
	const FVector ViewForward = FRotator(0.f, ViewRotation.Yaw, 0.f).Vector();

	// 그립 중점이 사람 앞 StandOffset 지점에 오도록 카트 원점을 역산한다.
	//
	// 그립은 카트 로컬 좌표에 고정돼 있으므로, 목표 회전으로 돌린 뒤 빼면
	// "그 자세일 때 카트 원점이 어디여야 하는가" 가 나온다.
	const FVector GripMidWorld = (GetGripTransform(true).GetLocation() + GetGripTransform(false).GetLocation()) * 0.5f;
	const FVector GripMidLocal = GetActorTransform().InverseTransformPosition(GripMidWorld);

	const FVector StandLocation = CurrentPusher->GetActorLocation();
	const FVector DesiredGripMid = StandLocation + ViewForward * StandOffset;

	OutLocation = DesiredGripMid - OutRotation.RotateVector(GripMidLocal);

	// 높이는 물리에 맡긴다. 목표 Z 를 강제하면 카트가 떠오르거나 바닥을 파고든다.
	OutLocation.Z = GetActorLocation().Z;

	return true;
}

void AHandCart::UpdateFollow(float DeltaSeconds)
{
	if (!IsValid(CartMesh) || !CartMesh->IsSimulatingPhysics() || DeltaSeconds <= 0.f)
	{
		return;
	}

	// 너무 멀어지면 놓는다. 벽 뒤로 돌아가거나 떨어졌을 때 카트가 벽을 긁으며 따라오는 것을 막는다.
	const float Distance = FVector::Dist2D(GetActorLocation(), CurrentPusher->GetActorLocation());
	if (Distance > MaxPushDistance)
	{
		UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] %.0fcm 떨어져 끌기 해제"), *GetName(), Distance);
		StopPush();
		return;
	}

	FVector TargetLocation;
	FQuat   TargetRotation;
	if (!ComputeFollowTarget(TargetLocation, TargetRotation))
	{
		return;
	}

	// [위치] 목표까지의 오차를 속도로 바꿔 넣는다.
	//
	// 위치를 대입하지 않는 이유 — 순간이동시키면 카트가 벽을 뚫고 사람 몸에 박힌다.
	// 속도로 밀면 물리 솔버가 벽에서 막아 주고, 그 막힘이 콜리전을 통해 미는 사람에게 그대로
	// 전달된다. "좁은 통로 불가" 라는 카트의 유일한 단점이 여기서 저절로 나온다.
	const FVector Error = TargetLocation - GetActorLocation();
	FVector DesiredVelocity = Error * FollowStiffness;
	DesiredVelocity = DesiredVelocity.GetClampedToMaxSize(MaxFollowSpeed);

	// Z 는 건드리지 않는다. 중력과 바닥 접촉이 계속 살아 있어야 한다.
	const FVector CurrentVelocity = CartMesh->GetPhysicsLinearVelocity();
	CartMesh->SetPhysicsLinearVelocity(FVector(DesiredVelocity.X, DesiredVelocity.Y, CurrentVelocity.Z));

	// [회전] 요 오차를 각속도로. X·Y 회전은 생성자에서 잠가 두었으므로 여기서도 0 이다.
	const float YawError = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, TargetRotation.Rotator().Yaw);
	CartMesh->SetPhysicsAngularVelocityInDegrees(FVector(0.f, 0.f, YawError * TurnStiffness));
}

void AHandCart::ContainLoot(ALootBase* Loot)
{
	if (!HasAuthority() || !IsValid(Loot) || ContainedLoot.Contains(Loot))
	{
		return;
	}

	// 사람이 든 채로 카트 위를 지나가는 것은 적재가 아니다.
	// 손에서 놓아야 실린 것으로 센다 — 들고 서 있기만 해도 조용해지면 카트가 필요 없어진다.
	if (Loot->GetPrimaryCarrier() != nullptr)
	{
		return;
	}

	// 이미 다른 카트에 실려 있으면 그쪽에서 먼저 뺀다. 두 카트가 같은 물건을 들고 있으면
	// 나중에 어느 쪽이 소음 억제를 풀어야 하는지가 어긋난다.
	if (AHandCart* Previous = Loot->GetContainingCart())
	{
		if (Previous != this)
		{
			Previous->ReleaseLoot(Loot);
		}
	}

	ContainedLoot.Add(Loot);
	Loot->SetContainingCart(this);

	UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] %s 적재 (총 %d개)"),
		*GetName(), *Loot->GetName(), ContainedLoot.Num());
}

void AHandCart::ReleaseLoot(ALootBase* Loot)
{
	if (!HasAuthority() || !Loot)
	{
		return;
	}

	if (ContainedLoot.Remove(Loot) > 0)
	{
		Loot->SetContainingCart(nullptr);

		UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] %s 이탈 (총 %d개)"),
			*GetName(), *Loot->GetName(), ContainedLoot.Num());
	}
}

void AHandCart::HandleLoadBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	ContainLoot(Cast<ALootBase>(OtherActor));
}

void AHandCart::HandleLoadEndOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	ReleaseLoot(Cast<ALootBase>(OtherActor));
}

bool AHandCart::ShouldReportHitAsNoise(const AActor* OtherActor) const
{
	if (!OtherActor)
	{
		return false;
	}

	// 실려 있는 물건이 적재함 안에서 부딪히는 것은 소음이 아니다.
	// 이걸 안 거르면 물건 쪽 소음을 아무리 막아도 카트가 대신 시끄러워서,
	// 소음을 줄이려고 산 장비가 소음 발생기가 된다.
	if (const ALootBase* Loot = Cast<ALootBase>(OtherActor))
	{
		if (Loot->GetContainingCart() == this)
		{
			return false;
		}

		// 아직 적재 목록에 없더라도 볼륨 안이면 조용하다.
		//
		// 오버랩 이벤트와 물리 히트는 같은 프레임에 와도 순서가 보장되지 않는다.
		// 세게 던져 넣으면 바닥에 닿는 첫 충돌이 BeginOverlap 보다 먼저 처리될 수 있고,
		// 그때 목록만 보면 그 한 번을 놓쳐서 "던져 넣을 때마다 한 번씩 시끄럽다" 가 된다.
		// 목록은 상태이고 이쪽은 사실이라, 사실을 한 번 더 본다.
		if (IsValid(LoadVolume) && LoadVolume->IsOverlappingActor(OtherActor))
		{
			return false;
		}
	}

	// 사람이 몸으로 미는 것도 소음이 아니다. 밀고 다니는 내내 접촉이 이어져서,
	// 이걸 소리로 치면 카트를 쓰는 것 자체가 시끄러워진다.
	if (OtherActor->IsA<APawn>())
	{
		return false;
	}

	return true;
}

void AHandCart::HandleCartHit(UPrimitiveComponent* /*HitComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, FVector NormalImpulse, const FHitResult& Hit)
{
	// 클라이언트의 물리 충돌은 신뢰하지 않는다. 시뮬레이션 결과가 머신마다 다르다.
	const UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !ShouldReportHitAsNoise(OtherActor))
	{
		return;
	}

	const float ImpulseMagnitude = NormalImpulse.Size();

	// [1겹] 임계값 미만 무시. 벽을 스치거나 바닥에 얹혀 있는 접촉까지 전부 OnHit 으로 온다.
	if (ImpulseMagnitude < NoiseImpulseThreshold)
	{
		return;
	}

	// [1.5겹] 수직 충격은 카트가 부딪힌 것이 아니라 실린 하중이 바닥으로 빠지는 것이다.
	// 이걸 안 막으면 물건을 실을 때마다 카트가 바닥을 치면서 소리가 난다. (헤더 주석 참고)
	if (FMath::Abs(NormalImpulse.GetSafeNormal().Z) >= NoiseVerticalImpactCutoff)
	{
		UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] 수직 충격 무시 (%s, 세기 %.0f)"),
			*GetName(), *GetNameSafe(OtherActor), ImpulseMagnitude);
		return;
	}

	// [2겹] 같은 대상에 대한 짧은 시간 내 재발행 차단. 임계값만으로는 못 막는다 —
	// 벽에 한 번 박아도 강한 접촉이 연달아 여러 번 잡힌다. (ALootBase 와 같은 구조)
	const float Now = World->GetTimeSeconds();
	const TWeakObjectPtr<const AActor> Key(OtherActor);
	if (const float* Last = RecentNoiseTimes.Find(Key))
	{
		if (Now - *Last < NoiseDebounceSeconds)
		{
			return;
		}
	}
	RecentNoiseTimes.Add(Key, Now);

	// 키가 죽은 항목은 여기서 정리한다. 안 하면 맵이 계속 자란다.
	for (auto It = RecentNoiseTimes.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > NoiseDebounceSeconds * 4.f)
		{
			It.RemoveCurrent();
		}
	}

	UNoiseSubsystem* Noise = UNoiseSubsystem::Get(this);
	if (!Noise)
	{
		return;
	}

	// 세기를 0~1 로 정규화해서 넘긴다. 실제 크기·반경·경계도는 DT_NoiseProfiles 가 정한다 —
	// 여기서는 "얼마나 세게 부딪혔나" 만 알린다.
	//
	// 아직 카트 전용 행이 없어서 Noise.Loot.Impact 를 쓴다. 카트가 벽에 박는 소리를
	// 따로 튜닝하고 싶어지면 소음 파트에 행 추가를 요청할 것.
	// 굴러가는 지속음은 넣지 않기로 했다 (2026-08-20).
	const float LoudnessScale = FMath::Clamp(ImpulseMagnitude / NoiseFullImpulse, 0.f, 1.f);
	Noise->ReportNoise(HHTags::Noise_Loot_Impact, Hit.ImpactPoint, LoudnessScale, this);

	UE_LOG(LogLoot, Verbose, TEXT("[Cart:%s] 충돌 소음 %.2f (%s, 세기 %.0f)"),
		*GetName(), LoudnessScale, *GetNameSafe(OtherActor), ImpulseMagnitude);
}
