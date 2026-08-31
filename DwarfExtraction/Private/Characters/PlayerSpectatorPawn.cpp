#include "Characters/PlayerSpectatorPawn.h"
#include "Characters/PlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

APlayerSpectatorPawn::APlayerSpectatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// DefaultPawn의 자유 비행 입력 제거 (우리는 대상 고정 방식)
	bAddDefaultMovementBindings = false;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 300.f;
	SpringArm->bUsePawnControlRotation = true;   // 마우스로 궤도 회전
	SpringArm->bDoCollisionTest = true;                    // 다시 켜기
	SpringArm->ProbeChannel = ECC_Camera;                  // 카메라 전용 채널로 검사
	SpringArm->ProbeSize = 12.f;                           // 검사 구체 반지름
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;

	SpectatorCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("SpectatorCamera"));
	SpectatorCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	SpectatorCamera->bUsePawnControlRotation = false;   // 회전은 SpringArm이 담당
}

void APlayerSpectatorPawn::BeginPlay()
{
	Super::BeginPlay();

	// 서버에서 첫 관전 대상 지정
	if (HasAuthority() && !SpectateTarget)
	{
		CycleNextTarget();
	}
}

void APlayerSpectatorPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlayerSpectatorPawn, SpectateTarget);
}

void APlayerSpectatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 대상이 사라지거나 죽었으면 다음 대상으로 (서버에서 판정)
	if (HasAuthority() && (!SpectateTarget || SpectateTarget->IsDead()))
	{
		CycleNextTarget();
	}

	if (SpectateTarget)
	{
		const FVector Desired = SpectateTarget->GetActorLocation() + TargetOffset;
		const FVector NewLoc = FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, FollowInterpSpeed);
		SetActorLocation(NewLoc);
	}
}

void APlayerSpectatorPawn::OnRep_SpectateTarget()
{
	// 대상이 바뀌면 즉시 그 위치로 순간이동 (보간으로 화면이 쭉 날아가는 것 방지)
	if (SpectateTarget)
	{
		SetActorLocation(SpectateTarget->GetActorLocation() + TargetOffset);
	}
}

void APlayerSpectatorPawn::GatherTargets(TArray<APlayerCharacter*>& Out) const
{
	Out.Reset();
	for (TActorIterator<APlayerCharacter> It(GetWorld()); It; ++It)
	{
		APlayerCharacter* Player = *It;
		// 죽지 않은 플레이어만 (다운 상태는 관전 가능 - 구조되는 걸 봐야 하니까)
		if (Player && !Player->IsDead())
		{
			Out.Add(Player);
		}
	}

	// 매 프레임 순서가 바뀌지 않도록 고정 정렬
	Out.Sort([](const APlayerCharacter& A, const APlayerCharacter& B)
	{
		return A.GetName() < B.GetName();
	});
}

void APlayerSpectatorPawn::CycleNextTarget()
{
	if (!HasAuthority())
	{
		Server_CycleNextTarget();
		return;
	}

	TArray<APlayerCharacter*> Targets;
	GatherTargets(Targets);

	if (Targets.Num() == 0)
	{
		SpectateTarget = nullptr;
		return;
	}

	const int32 CurrentIndex = Targets.IndexOfByKey(SpectateTarget);
	const int32 NextIndex = (CurrentIndex == INDEX_NONE) ? 0 : (CurrentIndex + 1) % Targets.Num();

	SpectateTarget = Targets[NextIndex];
	OnRep_SpectateTarget();   // 서버 자신도 즉시 반영
}

void APlayerSpectatorPawn::Server_CycleNextTarget_Implementation()
{
	CycleNextTarget();
}