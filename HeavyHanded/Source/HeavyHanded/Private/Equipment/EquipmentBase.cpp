#include "Equipment/EquipmentBase.h"

#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"                 // TActorIterator — 아래 임시 콘솔 명령용
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/LootLog.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Noise/NoiseEmitterComponent.h"
#include "TimerManager.h"

namespace
{
	/** 물리 시뮬레이션 중. 노획물과 같은 프로파일을 쓴다 — 던진 장비도 물건처럼 굴러야 한다 */
	static const FName EquipmentSimulatingProfile(TEXT("Loot"));
}

AEquipmentBase::AEquipmentBase()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	EquipmentMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EquipmentMesh"));
	SetRootComponent(EquipmentMesh);

	EquipmentMesh->SetCollisionProfileName(EquipmentSimulatingProfile);
	EquipmentMesh->SetSimulatePhysics(true);
	EquipmentMesh->SetNotifyRigidBodyCollision(true);   // OnComponentHit 을 받으려면 필요하다

	// ALootBase 와 같은 이유로 생성자에서 붙인다. BP 마다 추가하는 방식이면 언젠가
	// 빼먹고, 그 장비만 조용한데 경고도 없어서 발견이 아주 늦다.
	NoiseEmitter = CreateDefaultSubobject<UNoiseEmitterComponent>(TEXT("NoiseEmitter"));
}

void AEquipmentBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEquipmentBase, State);
	DOREPLIFETIME(AEquipmentBase, PrimaryCarrier);
}

void AEquipmentBase::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// 태그가 없으면 산 물건과 스폰된 물건을 이을 수 없다. 조용히 두면
		// "상점에서 샀는데 맵에 안 나온다" 로만 드러나서 원인까지 오래 걸린다.
		if (!EquipmentTag.IsValid())
		{
			UE_LOG(LogLoot, Warning,
				TEXT("[Equipment:%s] EquipmentTag 가 비어 있다. Config/Tags/Equipment.ini 의 태그를 지정할 것"),
				*GetName());
		}

		EquipmentMesh->OnComponentHit.AddDynamic(this, &AEquipmentBase::HandleMeshHit);
	}
}

void AEquipmentBase::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	// UGAB_Interact 의 집기 판정이 'Equipment' 루트를 보고 고른다.
	// 노획물이 Loot.Type 을 내놓는 것과 같은 자리다.
	if (EquipmentTag.IsValid())
	{
		TagContainer.AddTag(EquipmentTag);
	}
}

UPrimitiveComponent* AEquipmentBase::GetPhysicsRoot() const
{
	return EquipmentMesh;
}

// --------------------------------------------------------------------------
// 소지
// --------------------------------------------------------------------------

bool AEquipmentBase::CanBeCarriedBy(const APawn* Requester) const
{
	// 다 쓴 물건은 집히지 않는다. 손에 든 채로 사라지면 플레이어가 이유를 모른다.
	if (State == EEquipmentState::Spent)
	{
		return false;
	}

	// 이미 발동한 물건도 집히지 않는다. 터지는 폭탄을 주워 드는 그림이 나온다.
	if (State == EEquipmentState::Active)
	{
		return false;
	}

	return IsValid(Requester) && !IsValid(PrimaryCarrier);
}

void AEquipmentBase::OnGrabbed(APawn* Carrier)
{
	if (!HasAuthority() || !IsValid(Carrier))
	{
		return;
	}

	if (!CanBeCarriedBy(Carrier))
	{
		return;
	}

	PrimaryCarrier = Carrier;
	OnRep_PrimaryCarrier();

	SetEquipmentState(EEquipmentState::Carried);
}

void AEquipmentBase::OnReleased(APawn* Carrier)
{
	if (!HasAuthority() || PrimaryCarrier.Get() != Carrier)
	{
		return;
	}

	PrimaryCarrier = nullptr;
	OnRep_PrimaryCarrier();

	// 놓은 것은 던진 것이 아니다. 아직 자리를 잡은 것도 아니라 Idle 로 되돌린다.
	if (State == EEquipmentState::Carried)
	{
		SetEquipmentState(EEquipmentState::Idle);
	}
}

void AEquipmentBase::ApplyCarryState()
{
	const bool bCarried = IsValid(PrimaryCarrier);

	if (bCarried)
	{
		// PhysicsHandle 로 물리를 유지한 채 드는 방식은 멀티에서 깨진다.
		// 물리를 끄고 Attach 한 뒤, 놓기/던지기 순간에만 다시 켠다.
		EquipmentMesh->SetSimulatePhysics(false);

		// [노획물과 다른 점] 소지 중 콜리전을 아예 끈다.
		//   노획물은 CarriedLoot 프로파일로 '다른 캐릭터만 Block' 하고, 그 때문에
		//   운반자 본인을 무시 목록에 넣는 처리가 서버·클라이언트 양쪽에 필요하다.
		//   장비는 작고 가벼워서 남을 막을 이유가 없다. 끄면 그 기계장치가 통째로 사라진다.
		EquipmentMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// 손 소켓은 스켈레탈 메시에 있다. 캐릭터가 아니거나 메시가 없으면 루트에 붙여
		// 최소한 따라다니게 한다.
		USceneComponent* AttachTarget = PrimaryCarrier->GetRootComponent();
		FName SocketToUse = NAME_None;

		if (const ACharacter* CarrierCharacter = Cast<ACharacter>(PrimaryCarrier))
		{
			if (USkeletalMeshComponent* CarrierMesh = CarrierCharacter->GetMesh())
			{
				AttachTarget = CarrierMesh;

				// 소켓이 없는데 이름을 넘기면 메시 원점에 조용히 붙는다.
				// 그러면 물건이 발밑에 붙는데, 겉보기로는 '집히지 않았다' 로 보여서
				// 어태치나 물리를 의심하며 엉뚱한 곳을 뒤지게 된다. 그 자리에서 말해 준다.
				if (CarrierMesh->DoesSocketExist(CarrySocketName))
				{
					SocketToUse = CarrySocketName;
				}
				else
				{
					UE_LOG(LogLoot, Warning,
						TEXT("[Equipment:%s] 운반자 %s 의 메시에 소켓 '%s' 가 없다 — 메시 원점에 붙는다. "
							 "Equipment|Carry > Carry Socket Name 을 실제 리그 소켓 이름으로 맞출 것"),
						*GetName(), *GetNameSafe(PrimaryCarrier), *CarrySocketName.ToString());
				}
			}
		}

		if (AttachTarget)
		{
			AttachToComponent(AttachTarget,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketToUse);
		}
		return;
	}

	// 붙어 있는 상태(Deployed 이후)에서는 물리를 되돌리지 않는다.
	// 되돌리면 금고에 붙인 폭탄이 바닥으로 떨어진다.
	if (State == EEquipmentState::Deployed
		|| State == EEquipmentState::Active
		|| State == EEquipmentState::Spent)
	{
		return;
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	EquipmentMesh->SetCollisionProfileName(EquipmentSimulatingProfile);
	EquipmentMesh->SetSimulatePhysics(true);
}

void AEquipmentBase::OnRep_PrimaryCarrier()
{
	ApplyCarryState();
}

// --------------------------------------------------------------------------
// 던지기
// --------------------------------------------------------------------------

bool AEquipmentBase::CanBeThrown() const
{
	return State == EEquipmentState::Carried;
}

FVector AEquipmentBase::ComputeThrowAimDirection() const
{
	// 노획물과 같은 계산을 쓴다. 같은 팔로 던졌는데 폭탄이 꽃병과 다르게 날아가면 안 된다.
	return HHThrow::ComputeAimDirection(this, PrimaryCarrier.Get());
}

void AEquipmentBase::OnThrown(APawn* Carrier, const FVector& AimDirection)
{
	if (!HasAuthority() || PrimaryCarrier.Get() != Carrier)
	{
		return;
	}

	// 던질 수 없는 상태면 요청을 씹지 않고 제자리에 놓는다.
	// 거부만 하면 플레이어는 입력이 먹지 않는 것으로 느낀다.
	if (!CanBeThrown())
	{
		OnReleased(Carrier);
		return;
	}

	PrimaryCarrier = nullptr;
	OnRep_PrimaryCarrier();

	// 상태를 먼저 바꾼다. ApplyCarryState 가 Deployed 이후를 건너뛰기 때문에
	// 순서가 반대면 물리가 안 켜진다.
	SetEquipmentState(EEquipmentState::InFlight);

	HHThrow::Launch(this, EquipmentMesh, AimDirection, ThrowParams, Carrier);
}

// --------------------------------------------------------------------------
// 자리잡기 · 발동
// --------------------------------------------------------------------------

void AEquipmentBase::HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority() || State != EEquipmentState::InFlight)
	{
		return;
	}

	// 손에서 놓자마자 바닥에 살짝 스친 것을 '착지' 로 치면 던지기 시작하자마자 발동한다.
	// 노획물의 ImpactReportThreshold 와 같은 이유의 게이트다.
	if (NormalImpulse.Size() < DeployImpulseThreshold)
	{
		return;
	}

	Deploy(Hit);
}

void AEquipmentBase::Deploy(const FHitResult& Hit)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bAttachOnImpact)
	{
		// 물리를 끄고 맞은 컴포넌트에 붙는다. 움직이는 것에 붙어도 따라간다.
		EquipmentMesh->SetSimulatePhysics(false);
		EquipmentMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		if (USceneComponent* HitTarget = Hit.GetComponent())
		{
			AttachToComponent(HitTarget,
				FAttachmentTransformRules::KeepWorldTransform, Hit.BoneName);
		}
	}

	SetEquipmentState(EEquipmentState::Deployed);
	OnDeployed(Hit);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 폭파가 진행 중이라는 사실을 소음으로 흘린다. 경고음(DeployLoopSound)은 들리는 소리이고
	// 이쪽은 경비가 반응하는 근거다 — 둘은 다른 경로이며 한쪽만 켤 수도 있다.
	// 발행 자체는 ReportTaggedNoise 가 권위를 다시 확인하므로 여기서는 태그 유무만 본다.
	if (DeployNoiseTag.IsValid())
	{
		EmitDeployNoise();

		World->GetTimerManager().SetTimer(DeployNoiseTimer, this,
			&AEquipmentBase::EmitDeployNoise, DeployNoiseInterval, /*bLoop=*/true);
	}

	switch (ActivationMode)
	{
	case EEquipmentActivation::OnImpact:
		Activate();
		break;

	case EEquipmentActivation::AfterDelay:
		if (ActivationDelay > 0.f)
		{
			World->GetTimerManager().SetTimer(ActivationTimer, this,
				&AEquipmentBase::Activate, ActivationDelay, false);
		}
		else
		{
			Activate();
		}
		break;

	case EEquipmentActivation::Manual:
		// 코드가 ManualActivate 를 부를 때까지 기다린다.
		break;
	}
}

void AEquipmentBase::ManualActivate()
{
	if (!HasAuthority() || State != EEquipmentState::Deployed)
	{
		return;
	}

	Activate();
}

void AEquipmentBase::Activate()
{
	if (!HasAuthority() || State != EEquipmentState::Deployed)
	{
		return;
	}

	// 발동했으면 '대기 중' 이 아니다. 폭탄이 터진 뒤에도 경고음이 계속 울리면 안 된다.
	// 타이머는 서버에만 있고, 소리는 각 머신의 ApplyStateEffects 가 끈다.
	GetWorldTimerManager().ClearTimer(DeployNoiseTimer);

	SetEquipmentState(EEquipmentState::Active);
	OnActivated();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 0 이면 순간적인 효과다 — 폭발처럼 발동과 동시에 끝난다.
	if (EffectDuration > 0.f)
	{
		World->GetTimerManager().SetTimer(EffectTimer, this,
			&AEquipmentBase::Finish, EffectDuration, false);
	}
	else
	{
		Finish();
	}
}

void AEquipmentBase::Finish()
{
	if (!HasAuthority() || State != EEquipmentState::Active)
	{
		return;
	}

	SetEquipmentState(EEquipmentState::Spent);
	OnSpent();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 바로 Destroy 하면 액터가 복제보다 먼저 사라져 클라이언트에서는 연출 없이 증발한다.
	if (SpentDestroyDelay > 0.f)
	{
		World->GetTimerManager().SetTimer(DestroyTimer, this,
			&AEquipmentBase::DestroySelf, SpentDestroyDelay, false);
	}
	else
	{
		DestroySelf();
	}
}

void AEquipmentBase::DestroySelf()
{
	if (HasAuthority())
	{
		Destroy();
	}
}

void AEquipmentBase::EmitDeployNoise()
{
	// 발동한 뒤에도 타이머가 한 번 더 도는 경우를 막는다. ClearTimer 와 이 검사가
	// 겹쳐 보이지만, 타이머가 이미 큐에 들어간 프레임에서는 검사 쪽만 남는다.
	if (State != EEquipmentState::Deployed)
	{
		return;
	}

	if (IsValid(NoiseEmitter) && DeployNoiseTag.IsValid())
	{
		// 권위 검사는 ReportTaggedNoise 안에 있다. 클라이언트에서 불려도 조용히 무시된다.
		NoiseEmitter->ReportTaggedNoise(DeployNoiseTag);
	}
}

void AEquipmentBase::StopDeployLoop()
{
	if (!IsValid(DeployLoopComponent))
	{
		return;
	}

	// FadeOut 이다. Stop 은 파형을 그 자리에서 잘라 '뚝' 하는 클릭음을 남기는데,
	// 폭발음과 겹치면 그 클릭만 유난히 튄다.
	DeployLoopComponent->FadeOut(0.15f, 0.f);
	DeployLoopComponent = nullptr;
}

void AEquipmentBase::OnDeployed(const FHitResult& Hit)
{
	BP_OnDeployed();
}

void AEquipmentBase::OnActivated()
{
	BP_OnActivated();
}

void AEquipmentBase::OnSpent()
{
	BP_OnSpent();
}

// --------------------------------------------------------------------------
// 상태 · 연출
// --------------------------------------------------------------------------

void AEquipmentBase::SetEquipmentState(EEquipmentState NewState)
{
	if (!HasAuthority() || State == NewState)
	{
		return;
	}

	const EEquipmentState OldState = State;
	State = NewState;

	// 서버에서 값을 직접 바꾸면 RepNotify 가 불리지 않는다. 서버 몫은 손으로 부른다.
	OnRep_State(OldState);
}

void AEquipmentBase::OnRep_State(EEquipmentState OldState)
{
	ApplyStateEffects(OldState);
}

void AEquipmentBase::ApplyStateEffects(EEquipmentState OldState)
{
	// 데디케이티드 서버는 화면도 스피커도 없다. 리슨 서버의 호스트는 클라이언트이기도
	// 하므로 여기 걸리지 않는다.
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	switch (State)
	{
	case EEquipmentState::Deployed:
		if (IsValid(DeployEffect))
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(DeployEffect, EquipmentMesh, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget, /*bAutoDestroy=*/true);
		}
		if (IsValid(DeploySound))
		{
			UGameplayStatics::PlaySoundAtLocation(World, DeploySound, GetActorLocation());
		}
		if (IsValid(DeployLoopSound))
		{
			// 붙여서 재생한다. 폭탄이 움직이는 것에 붙었으면 소리도 따라가야 한다.
			// bStopWhenAttachedToDestroyed 를 켜 두면 액터가 사라질 때 소리도 같이 끊긴다.
			DeployLoopComponent = UGameplayStatics::SpawnSoundAttached(
				DeployLoopSound, EquipmentMesh, NAME_None,
				FVector::ZeroVector, EAttachLocation::SnapToTarget,
				/*bStopWhenAttachedToDestroyed=*/true);
		}
		break;

	case EEquipmentState::Active:
		// 발동했으면 대기가 끝났다. Spent 까지 기다리면 폭발 뒤에도 경고음이 남는다.
		StopDeployLoop();
		// 지속 이펙트는 붙인다. 액터가 움직이면 따라와야 한다.
		if (IsValid(ActiveEffect))
		{
			ActiveEffectComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				ActiveEffect, EquipmentMesh, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget, /*bAutoDestroy=*/false);
		}
		break;

	case EEquipmentState::Spent:
		// Active 를 거쳐 왔으면 이미 꺼져 있다. 그 경로를 건너뛰는 장비가 생겨도
		// 경고음만 남는 일이 없도록 여기서도 확인한다.
		StopDeployLoop();

		if (IsValid(ActiveEffectComponent))
		{
			// 파괴하지 않고 끈다. 이미 나와 있는 입자는 자연스럽게 사라진다.
			ActiveEffectComponent->Deactivate();
		}


		// 액터는 곧 사라지므로 붙이지 않고 월드에 스폰한다.
		// 붙였다가는 SpentDestroyDelay 뒤에 폭발 이펙트가 함께 사라진다.
		if (IsValid(SpentEffect))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, SpentEffect,
				GetActorLocation(), GetActorRotation(), FVector(1.f), /*bAutoDestroy=*/true);
		}
		if (IsValid(SpentSound))
		{
			UGameplayStatics::PlaySoundAtLocation(World, SpentSound, GetActorLocation());
		}
		break;

	default:
		break;
	}
}

// --------------------------------------------------------------------------
// [임시] 콘솔 명령
//
// UGAB_Interact 의 집기 판정이 'Loot.Type' 태그만 보기 때문에 장비는 아직 집히지
// 않는다. 플레이어 파트에서 'Equipment' 루트 판정을 넣어 주면 이 블록을 지운다.
// 그때까지 던지기·붙기·퓨즈·폭발을 검증할 유일한 경로다.
// (카트의 hh.Cart.TogglePush 와 같은 자리의 임시 코드다)
// --------------------------------------------------------------------------

#if !UE_BUILD_SHIPPING

namespace
{
	APawn* FindLocalPawn(UWorld* World)
	{
		return World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	}

	/** 폰이 들고 있는 장비. 없으면 nullptr */
	AEquipmentBase* FindHeldEquipment(UWorld* World, const APawn* Pawn)
	{
		for (TActorIterator<AEquipmentBase> It(World); It; ++It)
		{
			if (It->GetPrimaryCarrier() == Pawn)
			{
				return *It;
			}
		}
		return nullptr;
	}

	void GrabNearestEquipment(UWorld* World)
	{
		APawn* Pawn = FindLocalPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogLoot, Warning, TEXT("[임시] 플레이어 폰이 없다."));
			return;
		}

		AEquipmentBase* Nearest = nullptr;
		float NearestDistSq = TNumericLimits<float>::Max();

		// "없다" 만 찍으면 월드에 액터가 없는 것인지, 있는데 거부당한 것인지 알 수 없다.
		int32 TotalFound = 0;

		for (TActorIterator<AEquipmentBase> It(World); It; ++It)
		{
			++TotalFound;

			if (!It->CanBeCarriedBy(Pawn))
			{
				UE_LOG(LogLoot, Warning, TEXT("[임시] %s 거부됨 (상태 %d, 운반자 %s)"),
					*It->GetName(), static_cast<int32>(It->GetEquipmentState()),
					*GetNameSafe(It->GetPrimaryCarrier()));
				continue;
			}

			const float DistSq = FVector::DistSquared(It->GetActorLocation(), Pawn->GetActorLocation());
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = *It;
			}
		}

		if (!Nearest)
		{
			UE_LOG(LogLoot, Warning,
				TEXT("[임시] 집을 수 있는 장비가 없다. (월드의 AEquipmentBase 총 %d개, 폰 %s, 월드 %s)"),
				TotalFound, *Pawn->GetName(), *World->GetName());
			return;
		}

		Nearest->OnGrabbed(Pawn);
		UE_LOG(LogLoot, Log, TEXT("[임시] %s 을(를) 들었다."), *Nearest->GetName());
	}

	void ThrowHeldEquipment(UWorld* World)
	{
		APawn* Pawn = FindLocalPawn(World);
		AEquipmentBase* Held = IsValid(Pawn) ? FindHeldEquipment(World, Pawn) : nullptr;

		if (!Held)
		{
			UE_LOG(LogLoot, Warning, TEXT("[임시] 들고 있는 장비가 없다. hh.Equip.Grab 먼저."));
			return;
		}

		// 실제 경로와 같은 값을 쓴다. 여기서만 다른 방향을 만들면 검증이 무의미하다.
		Held->OnThrown(Pawn, Held->ComputeThrowAimDirection());
		UE_LOG(LogLoot, Log, TEXT("[임시] %s 을(를) 던졌다."), *Held->GetName());
	}
}

static FAutoConsoleCommandWithWorld GEquipGrabCmd(
	TEXT("hh.Equip.Grab"),
	TEXT("[임시] 가장 가까운 장비를 든다."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&GrabNearestEquipment));

static FAutoConsoleCommandWithWorld GEquipThrowCmd(
	TEXT("hh.Equip.Throw"),
	TEXT("[임시] 들고 있는 장비를 시선 방향으로 던진다."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&ThrowHeldEquipment));

#endif   // !UE_BUILD_SHIPPING
