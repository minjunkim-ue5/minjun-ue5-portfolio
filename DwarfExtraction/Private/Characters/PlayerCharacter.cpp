// PlayerCharacter.cpp
#include "Characters/PlayerCharacter.h"
#include "Core/PlayerStates/DwarfExtractionPlayerState.h"
#include "Characters/PlayerAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"       // 추가
#include "EnhancedInputSubsystems.h"      // 추가
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Systems/Interaction/InteractionComponent.h"
#include "Systems/Inventory/BackpackComponent.h"
#include "InputAction.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AISightTargetInterface.h"
#include "Characters/PlayerColorSet.h"
#include "Core/GameModes/DwarfExtractionInGameGameMode.h"
#include "Core/Settings/DwarfGameUserSettings.h"
#include "Systems/Noise/NoiseEmitterComponent.h"


APlayerCharacter::APlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	// 1인칭 카메라를 캡슐 눈높이쯤에 부착
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	FirstPersonCamera->bUsePawnControlRotation = true;
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
	GetCharacterMovement()->MaxWalkSpeedCrouched = 150.f;

	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	Backpack = CreateDefaultSubobject<UBackpackComponent>(TEXT("Backpack"));

	// 소음 컴포넌트 생성 (이 줄이 빠져있었음)
	NoiseEmitter = CreateDefaultSubobject<UNoiseEmitterComponent>(TEXT("NoiseEmitter"));

	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
}

void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	DefaultCapsuleHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(); // 추가

	// BP에서 설정된 실제 카메라 위치를 캐싱 (부활 시 복원용)
	if (FirstPersonCamera)
	{
		DefaultCameraLocation = FirstPersonCamera->GetRelativeLocation();
	}

	ApplyViewSettings();

	// 이 캐릭터를 조종하는 컨트롤러(플레이어)에게 IMC를 등록
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}

		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->ViewPitchMin = -50.f; // 아래로 최대 50도
			PC->PlayerCameraManager->ViewPitchMax = 70.f; // 위로 최대 70도
		}
	}

	// 1인칭: 내 화면에서만 머리를 숨김 (다른 플레이어에게는 정상적으로 보임)
	if (IsLocallyControlled() && !FirstPersonHideBoneName.IsNone())
	{
		GetMesh()->HideBoneByName(FirstPersonHideBoneName, EPhysBodyOp::PBO_None);
	}

	ApplyColor();
}

void APlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ===== 서버 전용: 태그 관리 =====
	if (HasAuthority() && AbilitySystemComponent)
	{
		const bool bIsMoving = GetVelocity().SizeSquared() > FMath::Square(10.f);

		static const FGameplayTag MovingTag = FGameplayTag::RequestGameplayTag(FName("State.Moving"));
		static const FGameplayTag SprintingTag = FGameplayTag::RequestGameplayTag(FName("State.Sprinting"));
		static const FGameplayTag DrainingTag = FGameplayTag::RequestGameplayTag(FName("State.SprintDraining"));

		// 이동 상태 태그 관리 (기존 로직)
		if (bIsMoving && !bIsMovingTagSet)
		{
			AbilitySystemComponent->AddLooseGameplayTag(MovingTag);
			bIsMovingTagSet = true;
		}
		else if (!bIsMoving && bIsMovingTagSet)
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(MovingTag);
			bIsMovingTagSet = false;
		}

		// "실제로 스태미나를 소모 중인지" = 이동 중 AND 스프린트 어빌리티 활성 중
		const bool bIsSprinting = AbilitySystemComponent->HasMatchingGameplayTag(SprintingTag);
		const bool bShouldDrain = bIsMoving && bIsSprinting;

		if (bShouldDrain && !bIsDrainingTagSet)
		{
			AbilitySystemComponent->AddLooseGameplayTag(DrainingTag);
			bIsDrainingTagSet = true;
		}
		else if (!bShouldDrain && bIsDrainingTagSet)
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(DrainingTag);
			bIsDrainingTagSet = false;
		}

		// 웅크림 상태 태그 관리
		if (AbilitySystemComponent)
		{
			const FGameplayTag CrouchTag = FGameplayTag::RequestGameplayTag(FName("State.Crouching"));
			const bool bHasCrouchTag = AbilitySystemComponent->HasMatchingGameplayTag(CrouchTag);

			if (bIsCrouched && !bHasCrouchTag)
			{
				AbilitySystemComponent->AddLooseGameplayTag(CrouchTag);
			}
			else if (!bIsCrouched && bHasCrouchTag)
			{
				AbilitySystemComponent->RemoveLooseGameplayTag(CrouchTag);
			}
		}
	}

	// ===== 피격 화면 효과 갱신 (모든 머신에서 실행되지만 로컬 카메라에만 의미 있음) =====
	if (!bIsDowned && IsLocallyControlled() && FirstPersonCamera && HitFeedbackTimeRemaining > 0.f)
	{
		HitFeedbackTimeRemaining = FMath::Max(0.f, HitFeedbackTimeRemaining - DeltaSeconds);

		// 남은 시간 비율 (1.0 = 방금 맞음, 0.0 = 효과 끝)
		const float Alpha = HitFeedbackTimeRemaining / HitFeedbackDuration;

		FPostProcessSettings& PP = FirstPersonCamera->PostProcessSettings;
		// 비네트: 가장자리를 어둡게/강하게
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = (bIsDead ? DeathVignetteIntensity : HitVignetteIntensity) * Alpha;
		// 화면 색조를 붉은 쪽으로: SceneColorTint 사용
		PP.bOverride_SceneColorTint = true;
		// Alpha가 클수록 붉게, 0에 가까울수록 원래 색(흰색)으로
		const float TintStrength = bIsDead ? 0.25f : 0.5f;
		PP.SceneColorTint = FLinearColor(1.f, 1.f - TintStrength * Alpha, 1.f - TintStrength * Alpha);
		// 효과가 끝나는 순간 오버라이드 해제 (원상복구)
		if (HitFeedbackTimeRemaining <= 0.f)
		{
			PP.bOverride_VignetteIntensity = false;
			PP.bOverride_SceneColorTint = false;
		}
	}

	// ===== 다운 상태: 은은한 붉은 화면 (고정 강도, 시간 감쇠 없음) =====
	if (bIsDowned && !bIsDead && IsLocallyControlled() && FirstPersonCamera)
	{
		FPostProcessSettings& PP = FirstPersonCamera->PostProcessSettings;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = DownedVignetteIntensity;
		PP.bOverride_SceneColorTint = true;
		PP.SceneColorTint = FLinearColor(1.f, 0.85f, 0.85f); // 살짝 붉은 톤
	}

#if !UE_BUILD_SHIPPING
	if (GEngine && IsLocallyControlled())
	{
		const TCHAR* Who = HasAuthority() ? TEXT("HOST (Server)") : TEXT("CLIENT");
		const FColor Col = HasAuthority() ? FColor::Green : FColor::Cyan;
		GEngine->AddOnScreenDebugMessage(9001, 0.f, Col,
		                                 FString::Printf(TEXT("=== %s ==="), Who));
	}
#endif
}


UAbilitySystemComponent* APlayerCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void APlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitAbilityActorInfo(); // 서버 기준으로 초기화
}

void APlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitAbilityActorInfo(); // 클라이언트 기준으로 초기화
}

void APlayerCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	
	// 클라이언트는 BeginPlay 때 컨트롤러가 아직 없어서 FOV 가 적용되지 않는다.
	// 컨트롤러가 붙는 이 시점에 다시 시도한다 (IMC 를 여기서 다시 거는 것과 같은 이유)
	ApplyViewSettings();
	
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void APlayerCharacter::InitAbilityActorInfo()
{
	ADwarfExtractionPlayerState* PS = GetPlayerState<ADwarfExtractionPlayerState>();
	if (PS)
	{
		AbilitySystemComponent = PS->GetAbilitySystemComponent();
		AttributeSet = PS->GetAttributeSet();
		// ASC에게 "이 Character가 너의 실제 몸(아바타)이야" 라고 알려주는 필수 함수
		AbilitySystemComponent->InitAbilityActorInfo(PS, this);


		// MoveSpeed 속성이 바뀔 때마다 OnMoveSpeedChanged가 호출되도록 등록
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AttributeSet->GetMoveSpeedAttribute()).
		                        AddUObject(this, &APlayerCharacter::OnMoveSpeedChanged);

		// 캐릭터의 실제 이동속도를 AttributeSet 기본값(300)으로 강제 동기화
		GetCharacterMovement()->MaxWalkSpeed = AttributeSet->GetMoveSpeed();

		// Health 변화 감지 델리게이트 등록 (MoveSpeed와 같은 패턴)
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AttributeSet->GetHealthAttribute()).AddUObject(
			this, &APlayerCharacter::OnHealthChanged);

		GrantAbilities(); // 추가
	}
}


void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 기본 InputComponent를 EnhancedInputComponent로 캐스팅해야 Bind가 가능함
	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// ETriggerEvent::Triggered = 키를 누르고 있는 동안 계속 호출
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);

		// 점프는 기본 Character 기능(ACharacter::Jump / StopJumping) 그대로 사용
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Sprint는 시작/종료 두 시점 다 필요 (Shift 누르는 순간 / 떼는 순간)
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &APlayerCharacter::StartSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &APlayerCharacter::StopSprint);

		// 크라우치
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Started, this, &APlayerCharacter::StartCrouch);
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Completed, this, &APlayerCharacter::StopCrouch);

		// 상호작용 (E키는 누르는 순간 한 번만)
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &APlayerCharacter::Interact);

		EnhancedInput->BindAction(DropAction, ETriggerEvent::Started, this, &APlayerCharacter::OnDrop);
		EnhancedInput->BindAction(UseAction, ETriggerEvent::Started, this, &APlayerCharacter::OnUse);
		EnhancedInput->BindAction(UseAction, ETriggerEvent::Completed, this, &APlayerCharacter::OnUseReleased);

		if (ReloadAction)
		{
			EnhancedInput->BindAction(ReloadAction, ETriggerEvent::Started, this, &APlayerCharacter::OnReload);
		}
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Completed, this, &APlayerCharacter::InteractReleased);

		if (CursorToggleAction)
		{
			EnhancedInput->BindAction(CursorToggleAction, ETriggerEvent::Started, this,
			                          &APlayerCharacter::ToggleCursorMode);
		}

		for (UInputAction* SlotAction : BackpackSlotActions)
		{
			if (!SlotAction)
				continue;

			EnhancedInput->BindAction(SlotAction, ETriggerEvent::Started, this, &APlayerCharacter::OnSlotStarted);
			EnhancedInput->BindAction(SlotAction, ETriggerEvent::Completed, this, &APlayerCharacter::OnSlotComplete);
		}
	}
}

void APlayerCharacter::InteractReleased(const FInputActionValue& Value)
{
	if (HasAuthority())
	{
		// 서버(호스트)면 바로 취소
		Server_CancelInteract_Implementation();
	}
	else
	{
		// 클라이언트면 서버에 취소 요청
		Server_CancelInteract();
	}
}

void APlayerCharacter::Server_CancelInteract_Implementation()
{
	if (AbilitySystemComponent)
	{
		FGameplayTagContainer CancelTags;
		CancelTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Interact")));
		AbilitySystemComponent->CancelAbilities(&CancelTags);
	}
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();

	if (Controller)
	{
		// 컨트롤러(카메라)가 보는 방향 기준으로 "앞"과 "오른쪽" 벡터를 구함
		const FRotator ControlRotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, ControlRotation.Yaw, 0); // Yaw(좌우 회전)만 사용, 위아래는 무시

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, MoveInput.X); // 전후
		AddMovementInput(RightDirection, MoveInput.Y); // 좌우
	}
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
	// 커서 모드에서는 카메라 회전 잠금
	if (bCursorMode)
		return;
	
	const FVector2D LookInput = Value.Get<FVector2D>();

	// 설정을 못 읽어도 조작은 되어야 하므로 1.0 으로 안전하게 시작한다
	float Sensitivity = 1.f;
	float LookScaleY = 1.f;

	if (const UDwarfGameUserSettings* Settings = UDwarfGameUserSettings::Get())
	{
		Sensitivity = Settings->GetMouseSensitivity();
		LookScaleY = Settings->GetLookScaleY(); // 반전이면 음수가 들어온다
	}

	AddControllerYawInput(LookInput.X * Sensitivity); // 좌우 시선 (마우스 X)
	AddControllerPitchInput(-LookInput.Y * LookScaleY); // 위아래 시선 (마우스 Y)
}

void APlayerCharacter::ApplyViewSettings()
{
	// FOV 는 보는 사람 화면에만 의미가 있다. 남의 캐릭터에 적용하면 안 된다
	if (!IsLocallyControlled() || !FirstPersonCamera)
		return;

	if (const UDwarfGameUserSettings* Settings = UDwarfGameUserSettings::Get())
	{
		FirstPersonCamera->SetFieldOfView(Settings->GetFieldOfView());
	}
}

void APlayerCharacter::ToggleCursorMode(const FInputActionValue& Value)
{
	// 호스트(리슨서버 권한)만 사용 가능
	if (!HasAuthority())
		return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
		return;

	bCursorMode = !bCursorMode;

	if (bCursorMode)
	{
		// 마우스 표시 + UI/게임 병행 입력 (카메라는 Look 가드로 잠김)
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}
	else
	{
		// 원상 복구
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void APlayerCharacter::StartSprint(const FInputActionValue& Value)
{
	const UDwarfGameUserSettings* Settings = UDwarfGameUserSettings::Get();
	const bool bToggleMode = Settings && Settings->GetSprintToggle();

	// 토글 모드에서 달리는 중에 다시 누르면 정지
	if (bToggleMode && AbilitySystemComponent)
	{
		static const FGameplayTag SprintingTag = FGameplayTag::RequestGameplayTag(FName("State.Sprinting"));
		if (AbilitySystemComponent->HasMatchingGameplayTag(SprintingTag))
		{
			CancelSprint();
			return;
		}
	}

	if (AbilitySystemComponent && SprintAbilityClass)
	{
		AbilitySystemComponent->TryActivateAbilityByClass(SprintAbilityClass);
	}
}

void APlayerCharacter::StopSprint(const FInputActionValue& Value)
{
	// 토글 모드에서는 키를 떼도 계속 달린다
	const UDwarfGameUserSettings* Settings = UDwarfGameUserSettings::Get();
	if (Settings && Settings->GetSprintToggle())
		return;

	CancelSprint();
}

void APlayerCharacter::CancelSprint()
{
	if (AbilitySystemComponent)
	{
		// Ability.Sprint 태그가 붙은 어빌리티를 찾아서 취소
		FGameplayTagContainer CancelTags;
		CancelTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Sprint")));
		AbilitySystemComponent->CancelAbilities(&CancelTags);
	}
}

void APlayerCharacter::GrantAbilities()
{
	// 서버에서만 부여 (클라이언트가 직접 어빌리티를 주면 안 됨 - GAS 필수 원칙)
	if (!HasAuthority() || bAbilitiesGranted || !AbilitySystemComponent || !SprintAbilityClass)
	{
		return;
	}

	AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(SprintAbilityClass, 1, INDEX_NONE, this));

	// 스태미나 회복 이펙트를 서버에서 한 번 적용 (Infinite라서 계속 유지됨)
	if (StaminaRegenEffectClass)
	{
		FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
		Context.AddSourceObject(this);
		FGameplayEffectSpecHandle Spec = AbilitySystemComponent->
			MakeOutgoingSpec(StaminaRegenEffectClass, 1.f, Context);
		if (Spec.IsValid())
		{
			AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	bAbilitiesGranted = true;

	if (InteractAbilityClass)
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(InteractAbilityClass, 1, INDEX_NONE, this));
	}
}

void APlayerCharacter::OnMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	//UE_LOG(LogTemp, Warning, TEXT("MoveSpeed Changed To New Value"));
	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
}

bool APlayerCharacter::CanJumpInternal_Implementation() const
{
	// 기본 조건(땅에 서있는지 등)을 먼저 검사
	if (!Super::CanJumpInternal_Implementation())
	{
		return false;
	}

	// 스태미나가 부족하면 점프 불가
	if (AttributeSet && AttributeSet->GetStamina() < MinStaminaToJump)
	{
		return false;
	}

	return true;
}

void APlayerCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	// 스태미나 차감은 서버에서만 (GAS 서버 권위 원칙, 이제 익숙하시죠)
	if (!HasAuthority() || !AbilitySystemComponent || !JumpCostEffectClass)
	{
		return;
	}

	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(JumpCostEffectClass, 1.f, Context);
	if (Spec.IsValid())
	{
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

void APlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (HasAuthority() && NoiseEmitter)
	{
		NoiseEmitter->EmitNoise(TEXT("Jump_Land"));
	}
}

UAISense_Sight::EVisibilityResult APlayerCharacter::CanBeSeenFrom(
	const FCanBeSeenFromContext& Context,
	FVector& OutSeenLocation,
	int32& OutNumberOfLoSChecksPerformed,
	int32& OutNumberOfAsyncLosCheckRequested,
	float& OutSightStrength,
	int32* UserData,
	const FOnPendingVisibilityQueryProcessedDelegate* Delegate)
{
	OutNumberOfLoSChecksPerformed = 0;
	OutNumberOfAsyncLosCheckRequested = 0;
	OutSightStrength = 0.f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return UAISense_Sight::EVisibilityResult::NotVisible;
	}

	const float HalfHeight = GetCapsuleComponent()
		                         ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		                         : 88.f;
	const FVector Center = GetActorLocation();

	// 머리 / 가슴 / 중심 / 무릎. 지형 모서리 하나에 전부 막히는 일을 없앤다.
	const FVector TestPoints[] = {
		Center + FVector(0.f, 0.f, HalfHeight * 0.9f),
		Center + FVector(0.f, 0.f, HalfHeight * 0.4f),
		Center,
		Center + FVector(0.f, 0.f, -HalfHeight * 0.7f),
	};

	// 엔진 기본 쿼리와 동일 조건 (complex trace, 관측자 무시)
	FCollisionQueryParams Params(TEXT("AILineOfSight"), true, Context.IgnoreActor);

	for (const FVector& Point : TestPoints)
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit, Context.ObserverLocation, Point, ECC_Visibility, Params);
		++OutNumberOfLoSChecksPerformed;

		if (!bHit || (Hit.GetActor() && Hit.GetActor()->IsOwnedBy(this)))
		{
			OutSeenLocation = Point;
			OutSightStrength = 1.f;
			return UAISense_Sight::EVisibilityResult::Visible;
		}
	}

	return UAISense_Sight::EVisibilityResult::NotVisible;
}

void APlayerCharacter::StartCrouch(const FInputActionValue& Value)
{
	const UDwarfGameUserSettings* Settings = UDwarfGameUserSettings::Get();

	// 토글 모드에서 앉아있는 중에 다시 누르면 일어선다
	if (Settings && Settings->GetCrouchToggle() && bIsCrouched)
	{
		UnCrouch();
		return;
	}

	// ACharacter 내장 함수. 캡슐 축소, 이동속도 감소, 네트워크 동기화 자동 처리
	Crouch();

	// 달리다가 앉으면 스프린트 중단
	CancelSprint();
}

void APlayerCharacter::StopCrouch(const FInputActionValue& Value)
{
	// 토글 모드에서는 키를 떼도 계속 앉아있는다
	const UDwarfGameUserSettings* Settings = UDwarfGameUserSettings::Get();
	if (Settings && Settings->GetCrouchToggle())
		return;

	UnCrouch();
}

void APlayerCharacter::OnDrop(const FInputActionValue& Value)
{
	if (InteractionComponent)
		InteractionComponent->DropHeld();
}

void APlayerCharacter::OnUse(const FInputActionValue& Value)
{
	if (InteractionComponent)
		InteractionComponent->UseHeld();
}

void APlayerCharacter::OnUseReleased(const FInputActionValue& Value)
{
	if (InteractionComponent)
		InteractionComponent->StopUseHeld();
}

void APlayerCharacter::OnReload(const FInputActionValue& Value)
{
	if (InteractionComponent)
		InteractionComponent->ReloadHeld();
}

void APlayerCharacter::OnSlotStarted(const FInputActionInstance& Instance)
{
	const UInputAction* Source = Instance.GetSourceAction();

	PendingSlot = INDEX_NONE;
	for (int32 i = 0; i < BackpackSlotActions.Num(); ++i)
	{
		if (BackpackSlotActions[i].Get() == Source)
		{
			PendingSlot = i;
			break;
		}
	}

	if (PendingSlot == INDEX_NONE)
		return;

	GetWorldTimerManager().SetTimer(
		SlotHoldTimer, this, &APlayerCharacter::OnSlotHoldReached, SlotHoldThreshold, false);
}

void APlayerCharacter::OnSlotComplete(const FInputActionInstance& Instance)
{
	if (PendingSlot == INDEX_NONE)
		return;

	GetWorldTimerManager().ClearTimer(SlotHoldTimer);

	if (InteractionComponent)
	{
		InteractionComponent->UseBackpackSlot(PendingSlot);
	}

	PendingSlot = INDEX_NONE;
}

void APlayerCharacter::OnSlotHoldReached()
{
	if (PendingSlot == INDEX_NONE || !InteractionComponent)
		return;

	InteractionComponent->DropBackpackSlot(PendingSlot);

	PendingSlot = INDEX_NONE;
}

void APlayerCharacter::Interact(const FInputActionValue& Value)
{
	if (AbilitySystemComponent && InteractAbilityClass)
	{
		AbilitySystemComponent->TryActivateAbilityByClass(InteractAbilityClass);
	}
}

float APlayerCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
                                   AController* EventInstigator, AActor* DamageCauser)
{
	const float Actual = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || Actual <= 0.f || bIsDead)
	{
		return 0.f;
	}

	// 팀킬 차단: 가해자가 다른 플레이어면 무시
	if (!bAllowFriendlyFire && EventInstigator
		&& EventInstigator->IsA<APlayerController>()
		&& EventInstigator != GetController())
	{
		return 0.f;
	}

	ReceivePlayerDamage(Actual, DamageCauser); // 기존 GAS 경로 재사용
	return Actual;
}



void APlayerCharacter::ReceivePlayerDamage(float DamageAmount, AActor* DamageCauser)
{
	// 데미지는 서버에서만 적용 (익숙한 원칙이죠)
	if (!HasAuthority() || bIsDead || !AbilitySystemComponent || !DamageEffectClass)
	{
		return;
	}

	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	Context.AddSourceObject(DamageCauser ? DamageCauser : this);

	FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(DamageEffectClass, 1.f, Context);
	if (Spec.IsValid())
	{
		// SetByCaller: "Data.Damage 이름표에 이 값을 실어 보낸다"
		// Health를 깎아야 하니 음수로 변환해서 전달 (호출자는 양수로 30, 50처럼 넘기면 됨)
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(FName("Data.Damage")), -DamageAmount);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}

	if (HasAuthority() && DamageCauser && !bIsDowned && !bIsDead)
	{
		const uint8 Dir = CalculateHitDirection(DamageCauser->GetActorLocation());
		Multicast_PlayHitReact(Dir);
	}

}

uint8 APlayerCharacter::CalculateHitDirection(const FVector& AttackerLocation) const
{
	// 공격자가 내 몸 기준 어느 방향에 있는지
	const FVector ToAttacker = (AttackerLocation - GetActorLocation()).GetSafeNormal2D();
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();

	const float ForwardDot = FVector::DotProduct(Forward, ToAttacker);
	const float RightDot = FVector::DotProduct(Right, ToAttacker);

	// 앞뒤 성분이 좌우보다 크면 앞/뒤, 아니면 좌/우
	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		return (ForwardDot >= 0.f) ? 0 : 1;   // 0=앞, 1=뒤
	}
	else
	{
		return (RightDot >= 0.f) ? 3 : 2;      // 3=우, 2=좌
	}
}

void APlayerCharacter::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	const float MaxHP = AttributeSet ? AttributeSet->GetMaxHealth() : 0.f;
	const TCHAR* NetRole = HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT");

	//(LogTemp, Warning, TEXT("[%s] %s HP: %.1f -> %.1f / %.1f (delta %.1f)"),
	//		NetRole, *GetName(), Data.OldValue, Data.NewValue, MaxHP, Data.NewValue - Data.OldValue);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow,
		                                 FString::Printf(TEXT("[%s] HP %.0f / %.0f"), NetRole, Data.NewValue, MaxHP));
	}

	// 체력이 감소했을 때만 피격 효과 (회복일 때는 번쩍이면 안 되니까)
	if (Data.NewValue < Data.OldValue && !bIsDead && !bIsDowned)
	{
		PlayHitFeedback();

		if (HasAuthority())
		{
			// 피격 GameplayCue (서버 → 전 클라 복제): 다른 플레이어가 내 피격 혈흔을 봄
			if (AbilitySystemComponent)
			{
				static const FGameplayTag HitCueTag =
					FGameplayTag::RequestGameplayTag(FName("GameplayCue.Character.Hit"));

				FGameplayCueParameters CueParams;
				CueParams.Location = GetActorLocation() + FVector(0.f, 0.f, 50.f); // 몸통 높이
				CueParams.Normal = GetActorForwardVector() * -1.f; // 피 튀는 방향(대략)
				AbilitySystemComponent->ExecuteGameplayCue(HitCueTag, CueParams);
			}

			if (InteractionComponent)
			{
				InteractionComponent->InterruptUse();
			}
		}
	}

	// 체력이 감소했고, 내가 누군가를 부활시키는 중이면 → 부활 중단
	if (Data.NewValue < Data.OldValue && bIsReviving && HasAuthority())
	{
		if (AbilitySystemComponent)
		{
			FGameplayTagContainer CancelTags;
			CancelTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Interact")));
			AbilitySystemComponent->CancelAbilities(&CancelTags);
		}
	}

	// 체력 0 → (기존: HandleDeath) → 다운 상태로 변경
	if (Data.NewValue <= 0.f && !bIsDead && !bIsDowned)
	{
		// 다운 진입은 서버가 결정 (상태 변화는 서버 권위)
		if (HasAuthority())
		{
			EnterDownedState();
		}
	}
}

void APlayerCharacter::PlayHitFeedback()
{
	// 내 화면에서만 재생 (다른 플레이어가 맞았을 때 내 화면이 번쩍이면 안 됨)
	if (!IsLocallyControlled())
	{
		return;
	}

	HitFeedbackTimeRemaining = HitFeedbackDuration;
}

void APlayerCharacter::HandleDeath()
{
	// 서버에서만 진입 (상태 결정)
	bIsDead = true;
	bIsDowned = false;
	// 다운 타이머가 돌고 있었다면 정지
	GetWorldTimerManager().ClearTimer(BleedOutTimerHandle);

	if (HasAuthority())
	{
		if (AbilitySystemComponent)
		{
			AbilitySystemComponent->CancelAllAbilities();
			AbilitySystemComponent->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Downed")));
		}

		if (InteractionComponent)
		{
			InteractionComponent->DropAllOnDeath();
		}
		
		if (ADwarfExtractionInGameGameMode* GM = GetWorld()->GetAuthGameMode<ADwarfExtractionInGameGameMode>())
		{
			GM->NotifyPlayerIncapacitated();
		}
	}

	// 서버 자신의 연출 (리슨서버 호스트가 죽은 경우)
	ApplyDeathEffects();

	// 관전 전환 예약 (서버 전용 - 기존 코드)
	GetWorldTimerManager().SetTimer(DeathTimerHandle, this, &APlayerCharacter::FinishDeathSequence,
	                                DeathSequenceDuration, false);
}

void APlayerCharacter::OnRep_IsDead()
{
	// 클라이언트에서 사망 복제 도착 → 연출 실행
	if (bIsDead)
	{
		ApplyDeathEffects();
	}
}

void APlayerCharacter::ApplyDeathEffects()
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 래그돌 (즉시 - 시체는 바로 쓰러져야 자연스러움)
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetSimulatePhysics(true);

	// ===== 카메라를 머리 본에 부착 (쓰러지는 시점 연출, 내 화면에서만) =====
	if (IsLocallyControlled() && FirstPersonCamera)
	{
		GetMesh()->UnHideBoneByName(FirstPersonHideBoneName);
		FirstPersonCamera->bUsePawnControlRotation = false;
		FirstPersonCamera->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		                                     HeadBoneName);
		FirstPersonCamera->SetRelativeLocation(DeathCameraOffset);
		FirstPersonCamera->SetRelativeRotation(DeathCameraRotation);
	}

	// ===== 사망 연출 (내 화면에서만) =====
	if (IsLocallyControlled())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, DeathSequenceDuration, FLinearColor::Black, false, true);
		}
		HitFeedbackTimeRemaining = DeathSequenceDuration;
	}
}

void APlayerCharacter::FinishDeathSequence()
{
	// (기존 HandleDeath에 있던 관전 전환 블록을 그대로 이곳으로 이동)
	if (SpectatorPawnClass)
	{
		AController* MyController = GetController();
		if (MyController)
		{
			if (APlayerController* PC = Cast<APlayerController>(MyController))
			{
				if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
					ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
				{
					Subsystem->ClearAllMappings();
				}
			}

			const FVector SpawnLocation = GetActorLocation() + FVector(0.f, 0.f, 100.f);
			const FRotator SpawnRotation = MyController->GetControlRotation();

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			APawn* Spectator = GetWorld()->SpawnActor<APawn>(SpectatorPawnClass, SpawnLocation, SpawnRotation,
			                                                 SpawnParams);
			if (Spectator)
			{
				MyController->UnPossess();
				MyController->Possess(Spectator);
			}
		}
	}
}

void APlayerCharacter::Cheat_Damage(float Amount)
{
	if (HasAuthority())
	{
		// 서버(호스트)에서 실행된 경우: 바로 적용
		ReceivePlayerDamage(Amount, this);
	}
	else
	{
		// 클라이언트에서 실행된 경우: 서버에게 요청 전달
		Server_CheatDamage(Amount);
	}
}

// Server RPC의 구현부는 함수명 뒤에 _Implementation을 붙여야 함 (언리얼 규칙)
void APlayerCharacter::Server_CheatDamage_Implementation(float Amount)
{
	// 이 코드는 서버에서 실행됨
	ReceivePlayerDamage(Amount, this);
}

void APlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlayerCharacter, bIsDowned);
	DOREPLIFETIME(APlayerCharacter, bIsDead); // 추가
	DOREPLIFETIME(APlayerCharacter, bIsReviving);
	DOREPLIFETIME(APlayerCharacter, ColorIndex);
}

void APlayerCharacter::EnterDownedState()
{
	bIsDowned = true;

	// 진행 중인 어빌리티 취소 + Downed 태그 부여
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->CancelAllAbilities();
		AbilitySystemComponent->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Downed")));
	}

	// drop the hand-slot item on downed (backpack stays)
	if (InteractionComponent)
	{
		InteractionComponent->DropHeld();
	}

	// 서버 자신도 연출 적용 (리슨서버 호스트가 다운된 경우)
	ApplyDownedEffects();

	// 제한시간 타이머: 시간 내 부활 못 하면 사망
	GetWorldTimerManager().SetTimer(BleedOutTimerHandle, this, &APlayerCharacter::BleedOut, DownedBleedOutTime, false);
	
	if (HasAuthority())
	{
		if (ADwarfExtractionInGameGameMode* GM = GetWorld()->GetAuthGameMode<ADwarfExtractionInGameGameMode>())
		{
			GM->NotifyPlayerIncapacitated();
		}
	}
}

void APlayerCharacter::OnRep_IsDowned()
{
	// 클라이언트에서 bIsDowned 복제가 도착했을 때
	if (bIsDowned)
	{
		ApplyDownedEffects();
	}
	else
	{
		RestoreFromDowned();
	}
}

void APlayerCharacter::ApplyDownedEffects()
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	UnCrouch();

	// 캡슐 축소 (충돌 판정용은 유지 - 누운 높이)
	GetCapsuleComponent()->SetCapsuleHalfHeight(30.f);

	// 카메라를 머리 본에 부착 (누운 애니메이션의 머리 위치를 따라감)
	if (IsLocallyControlled() && FirstPersonCamera)
	{
		// 혹시 본에 붙어있었다면 캡슐로 복귀 (안전장치)
		FirstPersonCamera->AttachToComponent(GetCapsuleComponent(),
		                                     FAttachmentTransformRules::KeepRelativeTransform);
		FirstPersonCamera->SetRelativeLocation(DownedCameraOffset);
		// bUsePawnControlRotation = true 그대로 → 고개 돌려 주변 확인 가능
	}
}

void APlayerCharacter::BleedOut()
{
	// 제한시간 만료 → 진짜 사망 (기존 사망 흐름 재사용)
	if (HasAuthority() && bIsDowned && !bIsDead)
	{
		HandleDeath();
	}
}

void APlayerCharacter::Interact_Implementation(AActor* Interactor)
{
	// 다운 상태가 아니면 상호작용 무시 (평소엔 그냥 지나가는 팀원일 뿐)
	if (!bIsDowned || bIsDead)
	{
		return;
	}

	APlayerCharacter* Reviver = Cast<APlayerCharacter>(Interactor);
	if (!Reviver || Reviver == this || Reviver->IsDowned() || Reviver->IsDead())
	{
		return;
	}

	// 이 시점은 이미 서버 (GA_Interact가 서버에서 트레이스하므로)
	Revive();
}

FText APlayerCharacter::GetInteractionText_Implementation() const
{
	return bIsDowned ? FText::FromString(TEXT("Revive")) : FText::GetEmpty();
}

void APlayerCharacter::Revive()
{
	if (!HasAuthority() || !bIsDowned)
	{
		return;
	}

	bIsDowned = false;
	GetWorldTimerManager().ClearTimer(BleedOutTimerHandle);

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Downed")));

		if (ReviveEffectClass)
		{
			FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
			FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(ReviveEffectClass, 1.f, Context);
			if (Spec.IsValid())
			{
				Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(FName("Data.Heal")),
				                                   ReviveHealthAmount);
				AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
	}

	// 서버 자신의 화면도 복구 (리슨서버 호스트가 부활한 경우)
	RestoreFromDowned();
}

void APlayerCharacter::RestoreFromDowned()
{
	// 이동 복구
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// 캡슐 원래 크기로
	GetCapsuleComponent()->SetCapsuleHalfHeight(DefaultCapsuleHalfHeight);

	if (IsLocallyControlled() && FirstPersonCamera)
	{
		// 카메라를 캡슐로 재부착 + 회전 제어 복원
		FirstPersonCamera->AttachToComponent(GetCapsuleComponent(),
		                                     FAttachmentTransformRules::KeepRelativeTransform);
		FirstPersonCamera->bUsePawnControlRotation = true; // 다운 중 껐다면 복원
		FirstPersonCamera->SetRelativeLocation(DefaultCameraLocation);
		FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);

		// 화면 효과 해제
		FPostProcessSettings& PP = FirstPersonCamera->PostProcessSettings;
		PP.bOverride_VignetteIntensity = false;
		PP.bOverride_SceneColorTint = false;
	}
}

void APlayerCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (IsLocallyControlled() && FirstPersonCamera && !bIsDowned && !bIsDead)
	{
		FirstPersonCamera->SetRelativeLocation(DefaultCameraLocation + CrouchCameraOffset);
	}
}

void APlayerCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (IsLocallyControlled() && FirstPersonCamera && !bIsDowned && !bIsDead)
	{
		FirstPersonCamera->SetRelativeLocation(DefaultCameraLocation);
	}
}


void APlayerCharacter::PauseBleedOut()
{
	if (HasAuthority() && bIsDowned)
	{
		GetWorldTimerManager().PauseTimer(BleedOutTimerHandle);
	}
}

void APlayerCharacter::ResumeBleedOut()
{
	if (HasAuthority() && bIsDowned)
	{
		GetWorldTimerManager().UnPauseTimer(BleedOutTimerHandle);
	}
}

void APlayerCharacter::Cheat_DamageDelayed(float Amount, float Delay)
{
	FTimerHandle TempHandle;
	GetWorldTimerManager().SetTimer(TempHandle, [this, Amount]()
	{
		if (HasAuthority())
		{
			ReceivePlayerDamage(Amount, this);
		}
		else
		{
			Server_CheatDamage(Amount); // 기존 RPC 재활용
		}
	}, Delay, false);
}


void APlayerCharacter::SetColorIndex(int32 NewIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	ColorIndex = NewIndex;
	ApplyColor();   // 서버 자신도 반영
}

void APlayerCharacter::OnRep_ColorIndex()
{
	ApplyColor();   // 클라이언트에서 색 적용
}

void APlayerCharacter::ApplyColor()
{
	if (!ColorSet || !ColorSet->Colors.IsValidIndex(ColorIndex) || !GetMesh())
	{
		return;
	}

	const FPlayerColorEntry& Entry = ColorSet->Colors[ColorIndex];

	if (Entry.SuitMaterial)
		GetMesh()->SetMaterial(SuitMaterialSlot, Entry.SuitMaterial);
	if (Entry.HelmetMaterial)
		GetMesh()->SetMaterial(HelmetMaterialSlot, Entry.HelmetMaterial);
	if (Entry.BackpackMaterial)
		GetMesh()->SetMaterial(BackpackMaterialSlot, Entry.BackpackMaterial);
}


void APlayerCharacter::Cheat_SetColor(int32 Index)
{
	if (HasAuthority())  SetColorIndex(Index);
	else                 Server_SetColor(Index);   // Server RPC 하나 만들어서
}


void APlayerCharacter::Server_SetColor_Implementation(int32 Index)
{
	SetColorIndex(Index);
}

void APlayerCharacter::Multicast_PlayHitReact_Implementation(uint8 DirectionIndex)
{
	if (HitReactMontages.IsValidIndex(DirectionIndex) && HitReactMontages[DirectionIndex])
	{
		PlayAnimMontage(HitReactMontages[DirectionIndex]);
	}
}
