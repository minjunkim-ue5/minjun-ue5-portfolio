#include "Equipment/EquipmentSpawnZone.h"

#include "Components/SceneComponent.h"
#include "Core/GameStates/HeistGameState.h"
#include "Core/HeavyHandedGameplayTags.h"
#include "Core/HeistEntryPoint.h"        // FindByTag — 고른 진입점이 이 레벨에 있는지 본다
#include "Core/RunProgressSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"                 // TActorIterator — Get / 중복 경고
#include "GameFramework/GameStateBase.h" // PlayerArray — 개인 장비 재적용
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Loot/LootLog.h"                // LogLoot — 장비 계열이 다 이걸 쓴다

AEquipmentSpawnZone::AEquipmentSpawnZone()
{
	PrimaryActorTick.bCanEverTick = false;

	// 복제하지 않는다. 이 액터는 서버에서만 판단하고, 만들어진 장비들이 각자 복제된다.
	bReplicates = false;

	ZoneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ZoneRoot"));
	SetRootComponent(ZoneRoot);
}

AEquipmentSpawnZone* AEquipmentSpawnZone::Get(const UObject* WorldContext)
{
	if (!GEngine)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	// 둘 이상이면 BeginPlay 가 이미 경고했으므로 여기서 다시 따지지 않는다
	for (TActorIterator<AEquipmentSpawnZone> It(World); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

void AEquipmentSpawnZone::BeginPlay()
{
	Super::BeginPlay();

	// 판정과 스폰이 전부 서버다. 클라이언트에서는 아무것도 하지 않는다.
	if (!HasAuthority())
	{
		return;
	}

	WarnIfDuplicateZone();

	AHeistGameState* GS = GetWorld()->GetGameState<AHeistGameState>();
	if (!GS)
	{
		// 서버라면 GameState 는 레벨 액터의 BeginPlay 보다 먼저 만들어진다.
		// 없다는 것은 이 액터가 작업 레벨이 아닌 곳에 놓였다는 뜻이다.
		UE_LOG(LogLoot, Warning,
			TEXT("%s: AHeistGameState 가 없다. 작업 레벨이 아닌 곳에 놓인 것이 아닌지 확인할 것."),
			*GetName());
		return;
	}

	// 준비 시간에 맞춘다. BeginPlay 에 바로 스폰하지 않는 이유는 개인 장비 재적용(예정)이
	// 플레이어 폰을 필요로 하고, HeistGameMode 가 접속 대기를 끝낸 뒤에 Phase.Prep 으로
	// 들어가기 때문이다.
	GS->OnPhaseChanged.AddDynamic(this, &AEquipmentSpawnZone::HandlePhaseChanged);

	// 이 액터가 늦게 만들어졌을 수 있다(스트리밍 · 재스폰). 이미 준비 시간이면 지금 스폰한다.
	if (GS->IsPhase(HHTags::Phase_Prep))
	{
		SpawnPurchasedEquipment();
	}
}

void AEquipmentSpawnZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		if (AHeistGameState* GS = World->GetGameState<AHeistGameState>())
		{
			GS->OnPhaseChanged.RemoveDynamic(this, &AEquipmentSpawnZone::HandlePhaseChanged);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AEquipmentSpawnZone::HandlePhaseChanged(FGameplayTag NewPhase, FGameplayTag OldPhase, EHeistPhaseReason Reason)
{
	if (NewPhase.MatchesTag(HHTags::Phase_Prep))
	{
		SpawnPurchasedEquipment();
	}
}

bool AEquipmentSpawnZone::IsEntryMatched() const
{
	// 담당 진입점을 안 정했으면 항상 맡는다 (진입점이 없는 레벨 · 구역 하나만 두던 배치)
	if (!EntryTag.IsValid())
	{
		return true;
	}

	const URunProgressSubsystem* Run = URunProgressSubsystem::Get(this);
	const FGameplayTag Selected = Run ? Run->GetSelectedEntry() : FGameplayTag();

	// 고른 것과 같으면 내 차례다. 부모 매칭이 아니라 정확히 일치해야 한다 —
	// HasTag 로 느슨하게 보면 Entry 루트 하나로 세 구역이 전부 켜진다.
	if (Selected.IsValid() && Selected.MatchesTagExact(EntryTag))
	{
		return true;
	}

	// 고른 진입점이 **이 레벨에 실제로 있으면** 그쪽 구역이 맡는다. 내 차례가 아니다.
	if (Selected.IsValid() && AHeistEntryPoint::FindByTag(this, Selected))
	{
		return false;
	}

	// 여기부터는 폴백이다 — 고른 것이 없거나(은신처를 안 거침) 이 레벨에 없는 경우.
	// 게임모드도 같은 상황에서 기본/첫 번째 진입점으로 떨어뜨린다. 구역도 같은 기준을 쓴다.
	TArray<AHeistEntryPoint*> Entries;
	AHeistEntryPoint::CollectEntryPoints(this, Entries);

	const int32 DefaultIndex = AHeistEntryPoint::FindDefaultIndex(Entries);
	const int32 FallbackIndex = Entries.IsValidIndex(DefaultIndex) ? DefaultIndex : 0;

	const bool bIsFallbackEntry = Entries.IsValidIndex(FallbackIndex)
		&& Entries[FallbackIndex]->GetEntryTag().MatchesTagExact(EntryTag);

	if (bIsFallbackEntry)
	{
		UE_LOG(LogLoot, Warning,
			TEXT("%s: 고른 진입점이 %s — 폴백으로 이 구역(%s)이 맡는다."),
			*GetName(),
			Selected.IsValid() ? TEXT("이 레벨에 없다") : TEXT("없다"),
			*EntryTag.ToString());
	}

	return bIsFallbackEntry;
}

void AEquipmentSpawnZone::SpawnPurchasedEquipment()
{
	if (!HasAuthority() || bHasSpawned)
	{
		return;
	}

	// 이번 판의 진입점이 아니면 가만히 있는다. 다른 구역이 맡는다.
	// bHasSpawned 를 세우지 않는 것은 hh.Equip.SpawnNow 로 진입점을 바꿔 가며
	// 시험할 때 이 구역이 다시 판정받을 수 있어야 하기 때문이다.
	if (!IsEntryMatched())
	{
		return;
	}

	URunProgressSubsystem* Run = URunProgressSubsystem::Get(this);
	if (!Run)
	{
		UE_LOG(LogLoot, Warning, TEXT("%s: URunProgressSubsystem 이 없어 구매 장비를 만들 수 없다."), *GetName());
		return;
	}

	// **목록을 복사해서 돈다.** GetPurchasedEquipment 는 살아 있는 맵의 const 참조이고,
	// 아래에서 ConsumePurchasedEquipment 가 그것을 비운다. 순회 중에 비우면 안 되고,
	// 복사해 두면 소비 시점을 순회와 떼어 놓을 수 있다.
	const TMap<FGameplayTag, int32> Purchased = Run->GetPurchasedEquipment();

	bHasSpawned = true;

	// 개인 장비는 구매 목록과 무관하다. 목록이 비어 있어도(이미 소비됐어도) 신발은
	// 매 스테이지 다시 붙여야 하므로 아래 이른 반환보다 위에 있어야 한다.
	ApplyPersonalEquipment();

	if (Purchased.IsEmpty())
	{
		UE_LOG(LogLoot, Log, TEXT("%s: 구매한 장비가 없다."), *GetName());
		return;
	}

	int32 PlacementIndex = 0;
	int32 SpawnedTotal = 0;

	for (const TPair<FGameplayTag, int32>& Entry : Purchased)
	{
		const FGameplayTag& EquipmentTag = Entry.Key;
		const int32 Count = Entry.Value;

		const TSubclassOf<AActor>* FoundClass = EquipmentClasses.Find(EquipmentTag);
		if (!FoundClass || !*FoundClass)
		{
			// 돈은 이미 은신처에서 나갔다. 여기서 조용히 넘기면 산 물건이 영영 나오지 않으므로
			// 크게 남긴다 — 매핑을 빠뜨리는 것이 이 구역에서 가장 하기 쉬운 실수다.
			UE_LOG(LogLoot, Error,
				TEXT("%s: %s 를 %d 개 샀는데 EquipmentClasses 에 매핑이 없다. 그만큼 사라진다."),
				*GetName(), *EquipmentTag.ToString(), Count);
			continue;
		}

		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (SpawnOne(EquipmentTag, *FoundClass, PlacementIndex))
			{
				++SpawnedTotal;
			}
			++PlacementIndex;
		}
	}

	// **스폰 직후 바로 비운다.** 갈라 두면 레벨이 두 번 로드될 때(재접속 · 리스타트)
	// 같은 장비가 두 번 나온다 (URunProgressSubsystem 주석).
	Run->ConsumePurchasedEquipment();

	UE_LOG(LogLoot, Log, TEXT("%s: 구매 장비 %d 개를 만들고 목록을 비웠다."), *GetName(), SpawnedTotal);
}

AActor* AEquipmentSpawnZone::SpawnOne(const FGameplayTag& EquipmentTag, UClass* ActorClass, int32 PlacementIndex)
{
	UWorld* World = GetWorld();
	if (!World || !ActorClass)
	{
		return nullptr;
	}

	const FTransform SpawnTransform = GetPlacementTransform(PlacementIndex);

	FActorSpawnParameters Params;
	// 겹쳐도 반드시 만든다. 장비는 물리를 켜고 나오므로 겹친 것은 스스로 밀려나 풀린다.
	// 여기서 스폰이 실패하면 돈만 쓰고 물건이 없다.
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AActor* Spawned = World->SpawnActor<AActor>(ActorClass, SpawnTransform, Params);
	if (!Spawned)
	{
		UE_LOG(LogLoot, Error, TEXT("%s: %s (%s) 스폰에 실패했다."),
			*GetName(), *EquipmentTag.ToString(), *ActorClass->GetName());
		return nullptr;
	}

	ShowSpawnDebug(FString::Printf(TEXT("%s"), *EquipmentTag.ToString()),
		SpawnTransform.GetLocation(), FColor::Green);

	return Spawned;
}

void AEquipmentSpawnZone::ApplyPersonalEquipment()
{
	if (PersonalEquipmentClasses.IsEmpty())
	{
		return;
	}

	const URunProgressSubsystem* Run = URunProgressSubsystem::Get(this);
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!Run || !GS)
	{
		return;
	}

	// PlayerArray 로 돈다. 폰에서 시작하면 아직 폰이 없는 사람(관전 · 로딩)을 못 세고,
	// 신원(FUniqueNetIdRepl)은 어차피 PlayerState 에만 있다.
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (!IsValid(PS))
		{
			continue;
		}

		APawn* Pawn = PS->GetPawn();
		if (!IsValid(Pawn))
		{
			// 관전자이거나 아직 폰이 없다. 경고할 일은 아니다
			continue;
		}

		const FGameplayTagContainer& Owned = Run->GetPersonalEquipment(PS->GetUniqueId());
		for (const FGameplayTag& EquipmentTag : Owned)
		{
			// 여기 없는 태그는 그냥 지나간다 — 컴포넌트로 표현되지 않는 개인 장비도 있다
			if (const TSubclassOf<UActorComponent>* FoundClass = PersonalEquipmentClasses.Find(EquipmentTag))
			{
				GrantComponentTo(Pawn, EquipmentTag, *FoundClass);
			}
		}
	}
}

void AEquipmentSpawnZone::GrantComponentTo(APawn* Pawn, const FGameplayTag& EquipmentTag, UClass* ComponentClass)
{
	if (!IsValid(Pawn) || !ComponentClass)
	{
		return;
	}

	// 두 번 붙으면 효과가 두 번 곱해진다(신발이라면 0.5 -> 0.25).
	// 이 함수가 페이즈마다 다시 불릴 수 있으므로 반드시 확인한다.
	if (Pawn->FindComponentByClass(ComponentClass))
	{
		return;
	}

	UActorComponent* Granted = NewObject<UActorComponent>(Pawn, ComponentClass);
	if (!Granted)
	{
		UE_LOG(LogLoot, Error, TEXT("%s: %s 컴포넌트를 %s 에 만들지 못했다."),
			*GetName(), *ComponentClass->GetName(), *GetNameSafe(Pawn));
		return;
	}

	// 등록해야 BeginPlay 가 돌고, 등록되면서 폰의 OwnedComponents 에 들어가 GC 로부터도 안전해진다
	Granted->RegisterComponent();

	UE_LOG(LogLoot, Log, TEXT("%s: %s 를 %s 에 다시 붙였다 (%s)."),
		*GetName(), *ComponentClass->GetName(), *GetNameSafe(Pawn), *EquipmentTag.ToString());
}

FTransform AEquipmentSpawnZone::GetPlacementTransform(int32 PlacementIndex) const
{
	// 앵커를 순환한다. 한 바퀴 돌 때마다 조금씩 비켜 놓는다 —
	// 같은 자리에 겹쳐 스폰하면 물리가 서로를 밀어내며 튄다.
	FVector BaseLocation = GetActorLocation();
	FRotator BaseRotation = GetActorRotation();
	int32 Lap = 0;

	// 유효한 앵커만 센다. 레벨에서 앵커를 지우면 배열에 null 이 남는다.
	TArray<const AActor*> ValidAnchors;
	for (const TObjectPtr<AActor>& Anchor : SpawnAnchors)
	{
		if (IsValid(Anchor))
		{
			ValidAnchors.Add(Anchor.Get());
		}
	}

	if (ValidAnchors.Num() > 0)
	{
		const int32 AnchorIndex = PlacementIndex % ValidAnchors.Num();
		Lap = PlacementIndex / ValidAnchors.Num();

		BaseLocation = ValidAnchors[AnchorIndex]->GetActorLocation();
		BaseRotation = ValidAnchors[AnchorIndex]->GetActorRotation();
	}
	else
	{
		// 앵커를 하나도 지정하지 않았을 때. 이 액터 주위에 원형으로 놓는다 —
		// 한 점에 다 쏟으면 물리가 폭발한다.
		static constexpr int32 FallbackRingCount = 6;
		const int32 RingIndex = PlacementIndex % FallbackRingCount;
		Lap = PlacementIndex / FallbackRingCount;

		const float Angle = (2.f * PI * RingIndex) / FallbackRingCount;
		BaseLocation += FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * FallbackRingRadius;
	}

	// 같은 자리를 다시 쓸 때는 옆으로 비켜 놓고, 물리로 떨어뜨리기 위해 위로 띄운다.
	BaseLocation.X += RepeatOffset * Lap;
	BaseLocation.Z += SpawnHeightOffset;

	return FTransform(BaseRotation, BaseLocation);
}

void AEquipmentSpawnZone::WarnIfDuplicateZone() const
{
	int32 Count = 0;
	for (TActorIterator<AEquipmentSpawnZone> It(GetWorld()); It; ++It)
	{
		++Count;
	}

	if (Count > 1)
	{
		// Get() 이 먼저 찾은 하나만 돌려주므로, 둘이면 어느 쪽이 쓰이는지 알 수 없다.
		UE_LOG(LogLoot, Warning,
			TEXT("%s: 레벨에 장비 스폰 구역이 %d 개 있다. 하나만 두어야 한다."), *GetName(), Count);
	}
}

#if !UE_BUILD_SHIPPING

void AEquipmentSpawnZone::DebugForceSpawn()
{
	bHasSpawned = false;
	SpawnPurchasedEquipment();
}

// ── 임시 콘솔 명령 ──
//
// 정식 경로는 은신처에서 사고 -> ServerTravel -> Phase.Prep 이다. 그 한 바퀴를 돌지 않고
// 작업 레벨 하나만 열어 이 구역을 시험하기 위한 것이다. 상점 UI 와 정식 흐름이 붙으면 지운다.
// (AEquipmentBase 의 hh.Equip.Grab / hh.Equip.Throw 와 같은 자리)

namespace SpawnZoneDebugCommands
{
	static void Grant(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Equip.Grant <Equipment.태그> [개수]"));
			return;
		}

		URunProgressSubsystem* Run = URunProgressSubsystem::Get(World);
		if (!Run)
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Equip.Grant: URunProgressSubsystem 을 찾을 수 없다."));
			return;
		}

		// 없는 태그를 넣으면 스폰 구역이 매핑을 못 찾아 Error 를 뱉는다. 여기서 먼저 막는다.
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false);
		if (!Tag.IsValid())
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Equip.Grant: 알 수 없는 태그 %s"), *Args[0]);
			return;
		}

		const int32 Count = Args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*Args[1])) : 1;
		Run->AddPurchasedEquipment(Tag, Count);

		UE_LOG(LogLoot, Log, TEXT("hh.Equip.Grant: %s x%d — 목록에 넣었다(골드는 차감하지 않는다)."),
			*Tag.ToString(), Count);
	}

	static void SpawnNow(UWorld* World)
	{
		AEquipmentSpawnZone* Zone = AEquipmentSpawnZone::Get(World);
		if (!Zone)
		{
			UE_LOG(LogLoot, Warning, TEXT("hh.Equip.SpawnNow: 레벨에 AEquipmentSpawnZone 이 없다."));
			return;
		}

		Zone->DebugForceSpawn();
	}
}

static FAutoConsoleCommandWithWorldAndArgs GEquipGrantCmd(
	TEXT("hh.Equip.Grant"),
	TEXT("[임시] 구매 목록에 장비를 넣는다(골드 차감 없음). 예: hh.Equip.Grant Equipment.Decoy 3"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnZoneDebugCommands::Grant));

static FAutoConsoleCommandWithWorld GEquipSpawnNowCmd(
	TEXT("hh.Equip.SpawnNow"),
	TEXT("[임시] 장비 스폰 구역을 지금 돌린다. 이미 스폰했어도 다시 돈다."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&SpawnZoneDebugCommands::SpawnNow));

#endif   // !UE_BUILD_SHIPPING

void AEquipmentSpawnZone::ShowSpawnDebug(const FString& Message, const FVector& Location, const FColor& Color) const
{
	UE_LOG(LogLoot, Log, TEXT("%s: %s 를 %s 에 놓았다."), *GetName(), *Message, *Location.ToCompactString());

#if ENABLE_DRAW_DEBUG
	if (!bShowSpawnDebug)
	{
		return;
	}

	// 서버 창에만 보인다. 스폰 판정이 서버에서만 돌기 때문이고, 이것은 레벨 배치를
	// 맞추기 위한 표시라 클라이언트에 내려보낼 이유가 없다.
	DrawDebugSphere(GetWorld(), Location, 25.f, 12, Color, false, 20.f);
	DrawDebugString(GetWorld(), Location + FVector(0.f, 0.f, 40.f), Message, nullptr, Color, 20.f, true);
#endif
}
