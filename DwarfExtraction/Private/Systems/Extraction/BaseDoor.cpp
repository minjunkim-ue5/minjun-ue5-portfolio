#include "Systems/Extraction/BaseDoor.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"


ABaseDoor::ABaseDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;   // 움직일 때만 Tick 켬
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Root);
}

void ABaseDoor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ClosedRelativeLocation = DoorMesh->GetRelativeLocation();
}

void ABaseDoor::BeginPlay()
{
	Super::BeginPlay();
	ApplyDoorState(true);   // 시작 상태는 즉시 반영
}

void ABaseDoor::SetOpen(bool bNewOpen)
{
	if (!HasAuthority() || bIsOpen == bNewOpen)
	{
		return;
	}

	bIsOpen = bNewOpen;
	ApplyDoorState(false);   // 서버도 함께 애니메이션
}

void ABaseDoor::OnRep_DoorState()
{
	ApplyDoorState(false);
}

void ABaseDoor::ApplyDoorState(bool bInstant)
{
	if (bInstant || OpenDuration <= KINDA_SMALL_NUMBER)
	{
		DoorProgress = bIsOpen ? 1.f : 0.f;
		UpdateDoorTransform();
		SetActorTickEnabled(false);
	}
	else
	{
		SetActorTickEnabled(true);   // Tick에서 목표까지 보간
	}

	if (!bInstant)
	{
		USoundBase* SoundToPlay = bIsOpen ? DoorOpenSound : DoorCloseSound;
		if (SoundToPlay)
		{
			UGameplayStatics::PlaySoundAtLocation(this, SoundToPlay, GetActorLocation());
		}
	}

	if (bInstant || OpenDuration <= KINDA_SMALL_NUMBER)
	{
		DoorProgress = bIsOpen ? 1.f : 0.f;
		UpdateDoorTransform();
		SetActorTickEnabled(false);
	}
	else
	{
		SetActorTickEnabled(true);
	}
	OnDoorStateUpdated(bIsOpen);


}

void ABaseDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Target = bIsOpen ? 1.f : 0.f;
	const float Speed = 1.f / FMath::Max(OpenDuration, KINDA_SMALL_NUMBER);

	DoorProgress = FMath::FInterpConstantTo(DoorProgress, Target, DeltaSeconds, Speed);
	UpdateDoorTransform();

	if (FMath::IsNearlyEqual(DoorProgress, Target, 0.001f))
	{
		DoorProgress = Target;
		UpdateDoorTransform();
		SetActorTickEnabled(false);   // 도착했으면 Tick 중지
	}
}

void ABaseDoor::UpdateDoorTransform()
{
	// 등속 진행도에 가감속 곡선을 얹어 자연스럽게
	const float Alpha = FMath::InterpEaseInOut(0.f, 1.f, DoorProgress, EaseExponent);

	DoorMesh->SetRelativeLocation(ClosedRelativeLocation + OpenOffset * Alpha);

	// 완전히 닫혔을 때만 막고, 움직이는 동안은 통과 가능
	DoorMesh->SetCollisionEnabled(
		DoorProgress <= KINDA_SMALL_NUMBER ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void ABaseDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABaseDoor, bIsOpen);
	DOREPLIFETIME(ABaseDoor, bIsLocked);
}

void ABaseDoor::Unlock()
{
	if (HasAuthority())
	{
		bIsLocked = false;
	}
}