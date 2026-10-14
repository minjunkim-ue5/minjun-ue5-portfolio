#include "Equipment/MedKit.h"

#include "AbilitySystemComponent.h"
#include "Character/BaseCharacter.h"
#include "Core/HeavyHandedGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"        // TActorIterator
#include "Loot/LootLog.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "NiagaraFunctionLibrary.h"   // 회복 연출을 일어난 사람에게 붙인다

AMedKit::AMedKit()
{
	EquipmentTag = HHTags::Equipment_MedKit;

	// 붙지 않는다. 던져서 쓰고 사라지는 물건이다.
	bAttachOnImpact = false;

	// 닿는 순간 쓴다. 퓨즈를 두면 그 사이에 동료가 체포된다.
	ActivationMode = EEquipmentActivation::OnImpact;

	// 즉발이다. E키 부활의 5초를 돈으로 사는 것이 이 물건의 값어치다.
	EffectDuration = 0.f;
}

void AMedKit::OnDeployed(const FHitResult& Hit)
{
	Super::OnDeployed(Hit);

	// 맞은 상대를 기억해 둔다. OnActivated 가 바로 다음에 오지만(OnImpact),
	// 그때는 FHitResult 가 없다.
	DeployedHitActor = Hit.GetActor();
}

void AMedKit::OnActivated()
{
	Super::OnActivated();

	UWorld* World = GetWorld();

#if ENABLE_DRAW_DEBUG
	// 권위 검사 앞에 둔다. 클라이언트 화면에서 착지 위치가 어긋나 보일 때 그것을 봐야 한다.
	if (bShowRescueDebug && World)
	{
		DrawDebugSphere(World, GetActorLocation(), RescueRadius, 16, FColor::Green, false, 3.f, 0, 2.f);
	}
#endif

	// 상태를 바꾸는 판정은 서버만 한다.
	if (!HasAuthority() || !World)
	{
		return;
	}

	// [1] 맞은 사람 우선. 정확히 맞힌 것을 반경 판정이 덮어써서 엉뚱한 사람이 일어나면
	//     "맞혔는데 왜 저 사람이" 가 된다.
	if (TryRescue(Cast<ABaseCharacter>(DeployedHitActor.Get())))
	{
		return;
	}

	// [2] 빗나갔으면 떨어진 자리 반경에서 찾는다.
	//
	// 오버랩이 아니라 순회인 이유는 AStickyBomb 과 같다 — 캐릭터는 최대 4명이고
	// 이 물건은 판당 몇 번 쓰이지 않는다. 오버랩으로 걸러 봐야 거리 계산을 두 번 하는 것이고,
	// 대신 콜리전 채널 설정이 맞아야 한다는 조건이 늘어난다. 그쪽이 조용히 깨지기 더 쉽다.
	//
	// 여럿이 겹쳐 쓰러져 있으면 가장 가까운 한 명만 일으킨다. 1회용이라 그것이 맞다.
	ABaseCharacter* Nearest = nullptr;
	float NearestDistSq = FMath::Square(RescueRadius);

	for (TActorIterator<ABaseCharacter> It(World); It; ++It)
	{
		ABaseCharacter* Candidate = *It;
		if (!IsValid(Candidate) || !Candidate->IsDowned())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
		if (DistSq <= NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Candidate;
		}
	}

	if (!TryRescue(Nearest))
	{
		// 헛되이 쓴 것이다. 물건은 그대로 소비된다 — 되돌려주면 반경 밖에 대고 던져 보며
		// 탐색하는 조작이 생긴다.
		UE_LOG(LogLoot, Log, TEXT("[MedKit:%s] 반경 %.0f 안에 다운된 동료가 없다 — 소비됨"),
			*GetName(), RescueRadius);
	}
}

bool AMedKit::TryRescue(ABaseCharacter* Target)
{
	if (!IsValid(Target) || !Target->IsDowned())
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
	if (!TargetASC)
	{
		return false;
	}

	// UGAB_Interact 의 부활과 같은 경로다. 대상의 GE 클래스를 몰라도 되도록 태그로 지운다.
	TargetASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(HHTags::State_Downed));

	// 성공한 자리에서만 연출을 보낸다. 빗나가 아무도 못 살렸으면 아무것도 안 나오는 것이
	// 맞다 — "살렸는지" 가 화면으로 구별돼야 한다.
	Multicast_RescueEffect(Target);

	UE_LOG(LogLoot, Log, TEXT("[MedKit:%s] %s 를 일으켰다"), *GetName(), *Target->GetName());
	return true;
}

void AMedKit::Multicast_RescueEffect_Implementation(ABaseCharacter* Rescued)
{
	const UWorld* World = GetWorld();

	// 데디케이티드 서버는 화면이 없다 (AEquipmentBase::ApplyStateEffects 와 같은 사유)
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 늦게 접속했거나 그 사이 사라졌으면 대상이 비어 온다. 연출일 뿐이라 조용히 넘어간다.
	if (!IsValid(RescueEffect) || !IsValid(Rescued) || !Rescued->GetRootComponent())
	{
		return;
	}

	// 캡슐 루트에 붙는다 — 원점이 캐릭터의 가운데(발이 아니다)라, 발밑에서 올라오는
	// 연출이면 나이아가라 쪽에서 아래로 내려 잡아야 한다.
	UNiagaraFunctionLibrary::SpawnSystemAttached(RescueEffect, Rescued->GetRootComponent(),
		NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, /*bAutoDestroy=*/true);
}

#if !UE_BUILD_SHIPPING

// ── 임시 콘솔 명령 ──
//
// 다운을 만드는 경로가 BP_BasePlayer 안에만 있어서(체력 0 -> GE_Downed), 응급 키트를
// 시험하려면 매번 경비에게 맞아야 했다. 연출 하나 고칠 때마다 그럴 수는 없다.
// 상점·장비의 hh.* 들과 함께 지울 임시 블록이다.
//
// GE 경로를 코드에 적는 것은 평소 피하는 일이지만, 여기는 쉬핑에서 통째로 빠지는
// 디버그 전용이고 BP 는 건드리지 않는다.

namespace MedKitDebugCommands
{
	static const TCHAR* DownedEffectPath =
		TEXT("/Game/HeavyHanded/Characters/Player/Blueprints/Effect/GE_Downed.GE_Downed_C");

	static void Down(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		// GE 적용은 서버만 할 수 있다. 클라이언트 창에서 쳐도 아무 일이 안 일어난다.
		AGameStateBase* GameState = World->GetGameState();
		if (!World->GetAuthGameMode() || !GameState)
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Debug.Down 은 서버(호스트) 창에서만 동작한다."));
			return;
		}

		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		if (!GameState->PlayerArray.IsValidIndex(Index))
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Debug.Down — %d번 플레이어가 없다 (접속 %d명)"),
				Index, GameState->PlayerArray.Num());
			return;
		}

		APawn* Pawn = GameState->PlayerArray[Index] ? GameState->PlayerArray[Index]->GetPawn() : nullptr;
		ABaseCharacter* Target = Cast<ABaseCharacter>(Pawn);
		UAbilitySystemComponent* ASC = Target ? Target->GetAbilitySystemComponent() : nullptr;
		if (!ASC)
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Debug.Down — %d번 플레이어의 폰/ASC 를 찾지 못했다"), Index);
			return;
		}

		UClass* DownedEffect = LoadClass<UGameplayEffect>(nullptr, DownedEffectPath);
		if (!DownedEffect)
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Debug.Down — GE_Downed 를 못 찾았다 (%s). "
				"에셋이 옮겨졌으면 이 경로를 고칠 것"), DownedEffectPath);
			return;
		}

		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		ASC->ApplyGameplayEffectToSelf(DownedEffect->GetDefaultObject<UGameplayEffect>(), 1.f, Context);

		UE_LOG(LogLoot, Log, TEXT("hh.Debug.Down — %s 를 다운시켰다"), *Target->GetName());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GDebugDownCmd(
	TEXT("hh.Debug.Down"),
	TEXT("[임시] 플레이어를 다운시킨다(응급 키트 시험용). 예: hh.Debug.Down 1 — 서버 창에서만 동작"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MedKitDebugCommands::Down));

#endif   // !UE_BUILD_SHIPPING
