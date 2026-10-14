#include "Equipment/Drone.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"   // StartCameraFade — 시점 전환을 덮는 암전
#include "Components/StaticMeshComponent.h"
#include "Core/GameStates/HeistGameState.h"
#include "Core/HeavyHandedGameplayTags.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "Loot/LootLog.h"                // LogLoot — 장비 계열이 다 이걸 쓴다
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ADrone::ADrone()
{
	EquipmentTag = HHTags::Equipment_Drone;

	// 던지는 것이 사용이다. 바닥에 닿으면 그 자리에서 떠오른다.
	bAttachOnImpact = false;
	ActivationMode = EEquipmentActivation::OnImpact;

	// 준비 시간이 45초다. 그보다 짧게 두어 "언제 띄울까" 가 판단이 되게 한다 —
	// 45초 내내 날 수 있으면 그냥 시작과 동시에 띄우는 것이 항상 최적이 된다.
	EffectDuration = 30.f;

	// 비행 중 매 틱 위치를 계산해야 한다. 베이스는 틱이 꺼져 있고, 조종이 시작될 때 켠다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	DroneCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("DroneCamera"));
	DroneCamera->SetupAttachment(EquipmentMesh);

	// 조종사의 시선이 드론 몸통에 가리지 않게 조금 뒤·위로 뺀다. BP 에서 조정한다.
	DroneCamera->SetRelativeLocation(FVector(-40.f, 0.f, 20.f));

	// 폰 회전을 따르지 않는다. 이 카메라는 조종사가 직접 돌린다.
	DroneCamera->bUsePawnControlRotation = false;
}

void ADrone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADrone, Pilot);
	DOREPLIFETIME(ADrone, bFlightActive);
}

void ADrone::OnThrown(APawn* Carrier, const FVector& AimDirection)
{
	// **Super 보다 먼저 잡는다.** 베이스가 PrimaryCarrier 를 nullptr 로 지우기 때문에
	// 뒤에서 읽으면 조종사를 영영 알 수 없다.
	if (HasAuthority() && IsValid(Carrier))
	{
		Pilot = Carrier;
	}

	Super::OnThrown(Carrier, AimDirection);
}

void ADrone::OnActivated()
{
	Super::OnActivated();

	StartFlight();
}

void ADrone::OnSpent()
{
	StopFlight();

	Super::OnSpent();
}

void ADrone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 최후 방어선이다. 여기까지 놓치면 조종사 화면이 사라진 액터를 계속 보고 있게 된다.
	// 조종 반납은 로컬 상태라 모든 머신에서 해야 하고, 비행 정리는 서버만 한다.
	ReleaseControl();

	if (HasAuthority())
	{
		StopFlight();
	}

	Super::EndPlay(EndPlayReason);
}

// ──────────────────────────────────────────────────────────────
// 조종 시작 · 종료
// ──────────────────────────────────────────────────────────────

void ADrone::StartFlight()
{
	if (!HasAuthority() || bFlightActive)
	{
		return;
	}

	bFlightActive = true;

	// Server RPC 는 액터의 Owner 커넥션에서만 올라온다. 조종사 PC 를 Owner 로 잡아야
	// 조종 입력이 서버에 도달한다 (AShopDisplay 가 Client RPC 를 못 쓴 것과 같은 규칙).
	if (APlayerController* PilotController = GetPilotController())
	{
		SetOwner(PilotController);
	}

	// 던져진 자세 그대로 두면 기울어진 채 조종이 시작된다. 수평으로 세우고 요만 남긴다 —
	// 떨어진 방향을 보고 있는 것은 자연스럽다.
	// 여기서 세우는 것만으로는 부족하다. 물리 트랜스폼 동기화가 이 한 번을 덮을 수 있어서
	// TickFlight 가 매 틱 다시 씌운다.
	ApplyControlRotation(GetActorRotation().Yaw, 0.f);

	FlightVelocity = FVector::ZeroVector;
	MoveInput = FVector2D::ZeroVector;
	AscendInput = 0.f;

	SetActorTickEnabled(true);

	// 준비 시간이 끝나면 남은 시간과 무관하게 끊는다.
	if (const UWorld* World = GetWorld())
	{
		if (AHeistGameState* GS = World->GetGameState<AHeistGameState>())
		{
			GS->OnPhaseChanged.AddDynamic(this, &ADrone::HandlePhaseChanged);
		}
	}

	// 서버는 OnRep 을 받지 않는다. 호스트가 조종사인 경우를 위해 직접 부른다.
	OnRep_FlightActive();
}

void ADrone::StopFlight()
{
	if (!HasAuthority() || !bFlightActive)
	{
		return;
	}

	bFlightActive = false;

	SetActorTickEnabled(false);
	FlightVelocity = FVector::ZeroVector;
	MoveInput = FVector2D::ZeroVector;
	AscendInput = 0.f;

	if (const UWorld* World = GetWorld())
	{
		if (AHeistGameState* GS = World->GetGameState<AHeistGameState>())
		{
			GS->OnPhaseChanged.RemoveDynamic(this, &ADrone::HandlePhaseChanged);
		}
	}

	OnRep_FlightActive();
}

void ADrone::OnRep_FlightActive()
{
	// **물리는 모든 머신에서 꺼야 한다.** SetSimulatePhysics 는 복제되지 않는다 —
	// 서버에서만 끄면 클라이언트의 사본은 던질 때 받은 각속도로 계속 돌고, 복제되어 오는
	// 트랜스폼과 싸운다. 카메라가 그 메시에 붙어 있어서 시야와 이동 기준이 서서히 어긋난다.
	// (조종사가 클라이언트일 때 몇 초 뒤 W 가 위로만 가던 원인)
	if (bFlightActive && IsValid(EquipmentMesh))
	{
		// 물리로 띄우면 중력과 싸워야 하고, 조종 입력이 힘으로 들어가 벽에서 튕겨 날아간다.
		// 코드가 직접 옮긴다.
		EquipmentMesh->SetSimulatePhysics(false);
		EquipmentMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

		// 남아 있는 각속도까지 지운다. 물리를 끄는 것만으로는 이미 실려 있던 회전이
		// 마지막 한 틱 더 반영될 수 있다.
		EquipmentMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		EquipmentMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	}

	if (bFlightActive)
	{
		// 조종사 화면에서만 시점과 입력을 가져간다. 다른 사람 화면에서는 아무 일도 없다.
		if (IsLocalPilot())
		{
			TakeControl(GetPilotController());
		}
	}
	else
	{
		ReleaseControl();
	}
}

void ADrone::HandlePhaseChanged(FGameplayTag NewPhase, FGameplayTag OldPhase, EHeistPhaseReason Reason)
{
	// 준비 시간에서 벗어나는 순간이다. 무엇으로 넘어가는지는 보지 않는다 —
	// 본 작업이든 결과든, 준비 시간이 아니면 드론은 끝이다.
	if (!OldPhase.MatchesTag(HHTags::Phase_Prep) || NewPhase.MatchesTag(HHTags::Phase_Prep))
	{
		return;
	}

	UE_LOG(LogLoot, Log, TEXT("%s: 준비 시간이 끝나 드론 조종을 끊는다."), *GetName());

	// 상태 기계를 건너뛰지 않는다. Spent 로 정상 종료시키면 OnSpent 가 시점을 되돌리고
	// 연출·파괴 지연까지 그대로 간다.
	FinishEffectEarly();
}

// ──────────────────────────────────────────────────────────────
// 시점 · 입력 (조종사 머신)
// ──────────────────────────────────────────────────────────────

void ADrone::TakeControl(APlayerController* PilotController)
{
	if (bHasControl || !IsValid(PilotController))
	{
		return;
	}

	// 되돌릴 대상을 먼저 기억한다. 보통 조종사의 폰이지만, 관전 중이었다면 다른 것일 수 있다.
	PreviousViewTarget = PilotController->GetViewTarget();

	// 지금 자세에서 이어받는다. 서버가 이미 수평으로 세워 둔 상태다.
	DesiredYaw = GetActorRotation().Yaw;
	DesiredPitch = 0.f;

	// 조종사 머신에서도 틱이 돌아야 한다 — 매 프레임 내 회전을 다시 씌워
	// 복제된 회전이 시야를 흔드는 것을 막는다.
	SetActorTickEnabled(true);

	// 여기서 켠다. 암전이 끝나기 전에 조종이 끊기더라도 ReleaseControl 이 돌아
	// 화면을 반드시 다시 밝히게 하기 위해서다.
	bHasControl = true;

	// 화면을 덮는다. 전환은 이 어둠 뒤에서 일어난다.
	StartFadeOut(PilotController);

	GetWorldTimerManager().SetTimer(ViewFadeTimer, this, &ADrone::FinishTakeControl,
		FMath::Max(ViewFadeOutSeconds, 0.01f), false);
}

void ADrone::FinishTakeControl()
{
	APlayerController* PilotController = GetPilotController();

	// 암전 중에 조종이 끊겼다면(페이즈 종료 등) 여기서 시점을 뺏으면 안 된다.
	if (!bHasControl || !IsValid(PilotController))
	{
		return;
	}

	// 블렌드 없이 바꾼다. 화면이 지금 덮여 있어서 전환이 보이지 않는다.
	PilotController->SetViewTarget(this);

	// 입력을 이 액터가 받게 한다. Possess 하지 않으므로 이것이 필요하다.
	EnableInput(PilotController);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADrone::HandleMoveInput);
			// 키를 놓는 순간 입력이 0 으로 와야 멈춘다. Triggered 만 물면 마지막 값이 남아
			// 드론이 계속 같은 방향으로 흘러간다.
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &ADrone::HandleMoveInput);
		}

		if (LookAction)
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADrone::HandleLookInput);
		}

		if (AscendAction)
		{
			EnhancedInput->BindAction(AscendAction, ETriggerEvent::Triggered, this, &ADrone::HandleAscendInput);
			EnhancedInput->BindAction(AscendAction, ETriggerEvent::Completed, this, &ADrone::HandleAscendInput);
		}
	}
	else
	{
		// 조용히 넘어가면 "시점은 옮겨졌는데 조종이 안 된다" 로만 드러난다.
		UE_LOG(LogLoot, Warning,
			TEXT("%s: EnhancedInputComponent 를 얻지 못해 드론 조종 입력을 붙이지 못했다."), *GetName());
	}

	if (const ULocalPlayer* LocalPlayer = PilotController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			if (DroneMappingContext)
			{
				// 플레이어보다 높은 우선순위로 넣어 같은 키를 가져온다.
				// 그래서 "조종 중에는 몸이 멈춘다" 를 따로 구현하지 않았다.
				Subsystem->AddMappingContext(DroneMappingContext, DroneInputPriority);
			}
			else
			{
				UE_LOG(LogLoot, Warning,
					TEXT("%s: DroneMappingContext 가 비어 있어 조종 키가 먹지 않는다. BP 에서 지정할 것."),
					*GetName());
			}
		}
	}

	StartFadeIn(PilotController, ViewFadeInSeconds, ViewFadeColor);

	ShowDroneDebug(TEXT("드론 조종 시작"));
	UE_LOG(LogLoot, Log, TEXT("%s: 드론 조종을 시작한다 (조종사 %s)."), *GetName(), *GetNameSafe(Pilot));
}

void ADrone::ReleaseControl()
{
	if (!bHasControl)
	{
		return;
	}

	bHasControl = false;

	// 인수 중이었다면 취소한다. 이게 남아 있으면 반납한 뒤에 시점을 다시 뺏는다.
	GetWorldTimerManager().ClearTimer(ViewFadeTimer);

	APlayerController* PilotController = GetPilotController();

	if (IsValid(PilotController))
	{
		if (const ULocalPlayer* LocalPlayer = PilotController->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
			{
				if (DroneMappingContext)
				{
					Subsystem->RemoveMappingContext(DroneMappingContext);
				}
			}
		}

		DisableInput(PilotController);

		// 화면을 덮고, 덮인 사이에 시점을 되돌린 뒤 다시 밝힌다.
		StartFadeOut(PilotController);

		// **타이머를 이 액터가 아니라 조종사 컨트롤러에 건다.**
		// 드론은 이 직후 파괴될 수 있고(Spent -> Destroy), 액터에 걸면 타이머가 함께
		// 사라져 화면이 덮인 채로 남는다. 이 기능에서 가장 나쁜 실패다.
		const TWeakObjectPtr<AActor> RestoreTarget(ResolveRestoreTarget(PilotController));
		const float FadeIn = ViewFadeInSeconds;
		const FLinearColor FadeColor = ViewFadeColor;

		FTimerHandle RestoreTimer;
		GetWorldTimerManager().SetTimer(RestoreTimer,
			FTimerDelegate::CreateWeakLambda(PilotController,
				[PilotController, RestoreTarget, FadeIn, FadeColor]()
				{
					AActor* Target = RestoreTarget.IsValid() ? RestoreTarget.Get() : PilotController;
					PilotController->SetViewTarget(Target);
					StartFadeIn(PilotController, FadeIn, FadeColor);
				}),
			FMath::Max(ViewFadeOutSeconds, 0.01f), false);
	}

	MoveInput = FVector2D::ZeroVector;
	AscendInput = 0.f;

	// 서버는 비행 정리에서 따로 끈다. 여기서 끄면 아직 날고 있는데 틱이 멎는다.
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}

	ShowDroneDebug(TEXT("드론 조종 종료"));
	UE_LOG(LogLoot, Log, TEXT("%s: 드론 조종을 끝냈다."), *GetName());
}

AActor* ADrone::ResolveRestoreTarget(APlayerController* PilotController) const
{
	// 원래 보던 것으로 돌린다. 그것이 사라졌다면(조종 중에 죽었다거나) 본인 폰으로,
	// 그것도 없으면 컨트롤러 자신으로 — 무엇이 됐든 **사라진 드론을 계속 보게 두지 않는다.**
	AActor* Target = PreviousViewTarget.Get();

	if (!IsValid(Target) || Target == this)
	{
		Target = PilotController->GetPawn();
	}

	return IsValid(Target) ? Target : PilotController;
}

void ADrone::StartFadeOut(APlayerController* PilotController) const
{
	if (!IsValid(PilotController) || !PilotController->PlayerCameraManager)
	{
		return;
	}

	// bHoldWhenFinished 를 켠다. 덮인 상태를 유지해야 그 뒤에서 시점을 바꿀 수 있다.
	PilotController->PlayerCameraManager->StartCameraFade(
		0.f, 1.f, FMath::Max(ViewFadeOutSeconds, 0.01f), ViewFadeColor,
		/*bShouldFadeAudio=*/false, /*bHoldWhenFinished=*/true);
}

void ADrone::StartFadeIn(APlayerController* PilotController, float Duration, const FLinearColor& Color)
{
	if (!IsValid(PilotController) || !PilotController->PlayerCameraManager)
	{
		return;
	}

	PilotController->PlayerCameraManager->StartCameraFade(
		1.f, 0.f, FMath::Max(Duration, 0.01f), Color,
		/*bShouldFadeAudio=*/false, /*bHoldWhenFinished=*/false);
}

APlayerController* ADrone::GetPilotController() const
{
	const APawn* PilotPawn = Pilot.Get();
	return IsValid(PilotPawn) ? Cast<APlayerController>(PilotPawn->GetController()) : nullptr;
}

bool ADrone::IsLocalPilot() const
{
	const APlayerController* PilotController = GetPilotController();
	return PilotController && PilotController->IsLocalController();
}

// ──────────────────────────────────────────────────────────────
// 입력 → 의도
// ──────────────────────────────────────────────────────────────

void ADrone::HandleMoveInput(const FInputActionValue& Value)
{
	MoveInput = Value.Get<FVector2D>();

	if (bShowDroneDebug)
	{
		UE_LOG(LogLoot, Log, TEXT("%s: [입력] Move=(%.2f, %.2f)"), *GetName(), MoveInput.X, MoveInput.Y);
	}

	Server_SetFlightIntent(MoveInput, AscendInput, FRotator(DesiredPitch, DesiredYaw, 0.f));
}

void ADrone::HandleAscendInput(const FInputActionValue& Value)
{
	AscendInput = Value.Get<float>();

	if (bShowDroneDebug)
	{
		UE_LOG(LogLoot, Log, TEXT("%s: [입력] Ascend=%.2f"), *GetName(), AscendInput);
	}

	Server_SetFlightIntent(MoveInput, AscendInput, FRotator(DesiredPitch, DesiredYaw, 0.f));
}

void ADrone::HandleLookInput(const FInputActionValue& Value)
{
	if (!DroneCamera)
	{
		return;
	}

	const FVector2D LookAxis = Value.Get<FVector2D>();

	// **로컬에서 즉시 돌린다.** 서버를 거치면 마우스가 왕복 지연만큼 늦게 따라와서
	// 조종이 불가능하다. 같은 값을 서버로도 보내 이동 방향의 기준을 맞춘다.
	const float PitchDelta = LookAxis.Y * LookSensitivity * (bInvertLookY ? -1.f : 1.f);

	DesiredYaw += LookAxis.X * LookSensitivity;
	DesiredPitch = FMath::Clamp(DesiredPitch + PitchDelta, -MaxPitchDegrees, MaxPitchDegrees);

	// **로컬에서 즉시 돌린다.** 서버를 거치면 마우스가 왕복 지연만큼 늦게 따라와서
	// 조종이 불가능하다. 같은 값을 서버로도 보내 이동 방향의 기준과 남의 화면을 맞춘다.
	ApplyControlRotation(DesiredYaw, DesiredPitch);

	Server_SetFlightIntent(MoveInput, AscendInput, FRotator(DesiredPitch, DesiredYaw, 0.f));
}

void ADrone::Server_SetFlightIntent_Implementation(FVector2D InMoveInput, float InAscendInput, FRotator InCameraRotation)
{
	// 서버가 실제로 무엇을 받았는지 본다. 조종사가 클라이언트일 때만 직렬화를 거치므로
	// "호스트는 되는데 클라는 안 된다" 부류의 원인을 여기서 갈라낼 수 있다.
	if (bShowDroneDebug)
	{
		UE_LOG(LogLoot, Log, TEXT("%s: [서버 수신] Move=(%.2f, %.2f)  Ascend=%.2f  Rot=(P %.1f / Y %.1f)"),
			*GetName(), InMoveInput.X, InMoveInput.Y, InAscendInput,
			InCameraRotation.Pitch, InCameraRotation.Yaw);
	}

	// 클라이언트를 신뢰하지 않는다. 방향과 세기는 받되 속도 상한은 서버가 정한다.
	MoveInput = InMoveInput.GetSafeNormal() * FMath::Min(InMoveInput.Size(), 1.f);
	AscendInput = FMath::Clamp(InAscendInput, -1.f, 1.f);

	ApplyControlRotation(InCameraRotation.Yaw, InCameraRotation.Pitch);
}

void ADrone::ApplyControlRotation(float Yaw, float Pitch)
{
	// **반드시 정규화하고 자른다.** FRotator 는 RPC 로 나갈 때 압축되면서 [0, 360) 범위로
	// 돌아온다 — 클라이언트가 보낸 -25.5 가 서버에는 334.5 로 도착한다.
	// 그대로 Clamp(-80, 80) 하면 334.5 가 80 이 되어 드론이 수직 위를 보게 되고,
	// 이동 방향이 그쪽이라 W 가 상승이 된다. (2026-09-11 클라이언트 전용 버그의 원인)
	//
	// 호스트는 RPC 가 로컬 호출이라 직렬화를 거치지 않아 증상이 나타나지 않는다.
	const float NormalizedPitch = FRotator::NormalizeAxis(Pitch);

	// 기준값을 먼저 남긴다. 이동 방향은 컴포넌트 트랜스폼이 아니라 이 값에서 나온다 —
	// 물리 동기화나 복제가 액터를 건드려도 조종 방향이 흔들리지 않게 하려는 것이다.
	ControlRotation = FRotator(FMath::Clamp(NormalizedPitch, -MaxPitchDegrees, MaxPitchDegrees), Yaw, 0.f);

	// 요는 액터에 준다. 그래야 드론 몸통이 시야와 함께 돌고, 그 회전이 복제되어
	// 다른 사람 화면에서도 드론이 나아가는 쪽을 향한다.
	SetActorRotation(FRotator(0.f, Yaw, 0.f));

	// 피치는 카메라만 기울인다. 액터를 기울이면 이동 방향과 스윕 판정의 기준이 함께 기운다.
	if (DroneCamera)
	{
		// 위에서 이미 정규화·클램프한 값을 그대로 쓴다. 여기서 원본 Pitch 를 다시 자르면
		// 같은 함정을 한 번 더 밟는다 — 기준은 한 곳에서만 만든다.
		DroneCamera->SetRelativeRotation(FRotator(ControlRotation.Pitch, 0.f, 0.f));
	}
}

// ──────────────────────────────────────────────────────────────
// 비행 (서버)
// ──────────────────────────────────────────────────────────────

void ADrone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 조종사 화면에서는 매 프레임 내 회전을 다시 씌운다. 액터 회전은 복제되므로,
	// 서버에서 한 발 늦게 도착한 값이 시야를 흔드는 것을 여기서 덮어 막는다.
	if (bHasControl)
	{
		ApplyControlRotation(DesiredYaw, DesiredPitch);
	}

	if (HasAuthority() && bFlightActive)
	{
		TickFlight(DeltaSeconds);
	}
}

void ADrone::TickFlight(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f)
	{
		return;
	}

	// **서버도 매 틱 회전을 다시 씌운다.** 조종사가 클라이언트면 서버는 bHasControl 이
	// false 라 여기서 하지 않으면 물리 동기화가 남긴 기울기가 영영 남는다.
	ApplyControlRotation(ControlRotation.Yaw, ControlRotation.Pitch);

	// 이동 방향은 조종사가 보낸 값에서 직접 뽑는다. 컴포넌트의 월드 회전을 읽으면
	// 물리·복제가 건드린 결과까지 섞여 들어온다.
	const FVector Forward = ControlRotation.Vector();
	const FVector Right = FRotationMatrix(FRotator(0.f, ControlRotation.Yaw, 0.f)).GetUnitAxis(EAxis::Y);

	FVector DesiredVelocity = Forward * (MoveInput.Y * MaxFlySpeed) + Right * (MoveInput.X * MaxFlySpeed);
	DesiredVelocity.Z += AscendInput * MaxAscendSpeed;

	// 입력이 없으면 제동, 있으면 가속. 제동을 크게 두면 손을 뗐을 때 바로 서서 다루기 쉽다.
	const bool bHasInput = !MoveInput.IsNearlyZero() || !FMath::IsNearlyZero(AscendInput);
	const float Rate = bHasInput ? FlyAcceleration : FlyBraking;

	FlightVelocity = FMath::VInterpConstantTo(FlightVelocity, DesiredVelocity, DeltaSeconds, Rate);

	if (FlightVelocity.IsNearlyZero())
	{
		return;
	}

	// 스윕으로 옮긴다. 벽을 뚫고 들어가면 조종사가 벽 안에서 아무것도 못 보게 된다.
	FHitResult Hit;
	AddActorWorldOffset(FlightVelocity * DeltaSeconds, /*bSweep=*/true, &Hit);

	if (Hit.bBlockingHit)
	{
		// 정면으로 죽이지 않고 부딪힌 면을 따라 미끄러뜨린다. 좁은 실내에서 벽마다
		// 완전히 멈추면 조종이 답답해진다.
		FlightVelocity = FVector::VectorPlaneProject(FlightVelocity, Hit.ImpactNormal);
	}
}

void ADrone::ShowDroneDebug(const FString& Message) const
{
	if (!bShowDroneDebug)
	{
		return;
	}

	UE_LOG(LogLoot, Log, TEXT("%s: %s (조종사 %s / 로컬 %s)"),
		*GetName(), *Message, *GetNameSafe(Pilot),
		IsLocalPilot() ? TEXT("O") : TEXT("X"));
}
