#include "Shop/ShopDisplay.h"

#include "Components/StaticMeshComponent.h"
#include "Core/RunProgressSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"   // ClientMessage — 구매자 본인에게 가는 유일한 통로
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Shop/ShopSettings.h"                 // 가격표 조회 — ShopTypes.h 를 같이 끌어온다
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogShop, Log, All);

AShopDisplay::AShopDisplay()
{
	PrimaryActorTick.bCanEverTick = false;

	// Multicast 로 구매음을 보내므로 복제되어야 한다.
	// 상태는 복제하지 않는다 — 클라이언트가 화면에 그려야 하는 잔액 · 품절 표시는
	// GameState 를 거쳐야 하고(URunProgressSubsystem 은 복제되지 않는다) 그건 UI 파트 일이다.
	bReplicates = true;

	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	SetRootComponent(DisplayMesh);

	// 시선 스윕(UGAB_Interact 는 ECC_Visibility 로 스윕한다)에만 잡히고 몸은 통과시킨다.
	// Pawn 을 막으면 좁은 은신처에서 진열대에 계속 끼이고, 물리를 켜면 지나가다 밀어서
	// 진열이 흐트러진다. 파는 물건이지 놓인 물건이 아니다.
	DisplayMesh->SetSimulatePhysics(false);
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DisplayMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	DisplayMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DisplayMesh->SetGenerateOverlapEvents(false);
}

void AShopDisplay::BeginPlay()
{
	Super::BeginPlay();

	// 경고보다 먼저 부른다 — 표에서 가격을 채우기 전에 검사하면 BP 가 0 인 진열대가
	// 매번 "공짜로 팔린다" 로 걸린다. 표에 값이 있으면 그것이 정상 경로다.
	ResolvePriceFromCatalog();

	if (HasAuthority())
	{
		WarnIfMisconfigured();
	}
}

void AShopDisplay::ResolvePriceFromCatalog()
{
	if (!ItemTag.IsValid())
	{
		// 태그가 없으면 찾을 행도 없다. 이 사실 자체는 WarnIfMisconfigured 가 따로 찍는다.
		return;
	}

	const UShopSettings* Settings = UShopSettings::Get();

	if (!Settings->HasCatalog())
	{
		UE_LOG(LogShop, Warning,
			TEXT("%s: 가격표가 지정되지 않았다 — 팔지 않는다. "
				 "(Project Settings → Game → Shop → Shop Catalog)"),
			*GetName());
		return;
	}

	const FShopItemRow* Row = Settings->FindItem(ItemTag);
	if (!Row)
	{
		// 표는 있는데 행이 없다 — 대개 태그를 새로 만들고 표에 넣는 것을 잊은 경우다.
		UE_LOG(LogShop, Warning,
			TEXT("%s: 가격표에 '%s' 행이 없다 — 팔지 않는다. "
				 "(Data/ShopCatalog.csv 에 행을 넣고 DT_ShopCatalog 를 Reimport 할 것)"),
			*GetName(), *ItemTag.ToString());
		return;
	}

	bPriceResolved = true;

	// BP 에 남아 있던 값과 다르면 어느 쪽이 이겼는지 남긴다. 같은 값이면 굳이 찍지 않는다 —
	// 진열대가 늘어나면 정상 동작이 로그를 덮는다.
	if (Price != Row->Price)
	{
		UE_LOG(LogShop, Log, TEXT("%s: 가격표의 $%d 로 판다. (BP 값 $%d 는 쓰이지 않는다)"),
			*GetName(), Row->Price, Price);
	}

	Price = Row->Price;
}

void AShopDisplay::OnInteract_Implementation(APawn* Interactor)
{
	// UGAB_Interact 는 서버 권한에서만 부른다(Interactable.h). 그래도 확인한다 —
	// BP 나 다른 경로에서 부를 수 있고, 클라이언트에서 통과하면 그 창에서만 잔액이 줄어든다.
	if (!HasAuthority() || !IsValid(Interactor))
	{
		return;
	}

	// 가격표에서 값을 못 받았으면 팔지 않는다. 폴백으로 파는 쪽이 더 위험하다 —
	// 옛 가격이나 공짜로 팔리고, 그건 플레이 중에 드러나지 않는다 (bPriceResolved 주석 참고).
	if (!bPriceResolved)
	{
		ReportResult(EShopPurchaseResult::Unavailable, Interactor,
			FString::Printf(TEXT("가격표에서 '%s' 가격을 못 받았다"),
				ItemTag.IsValid() ? *ItemTag.ToString() : TEXT("(태그 없음)")));
		return;
	}

	URunProgressSubsystem* Run = URunProgressSubsystem::Get(this);
	if (!Run)
	{
		ReportResult(EShopPurchaseResult::Unavailable, Interactor, TEXT("URunProgressSubsystem 없음"));
		return;
	}

	// 0. 설정 검사를 지출보다 먼저 한다.
	//    무효 태그를 구매 목록에 넣으면 스폰 구역이 조용히 아무것도 만들지 않고 $ 만 사라진다.
	//    1인 1회 물건도 마찬가지다 — 보유 기록이 태그를 키로 쓰므로, 태그가 없으면
	//    제한이 조용히 풀려서 무한히 팔린다.
	if (!ItemTag.IsValid() && (PurchaseKind == EShopPurchaseKind::StageSpawn || bOncePerPlayer))
	{
		ReportResult(EShopPurchaseResult::Unavailable, Interactor, TEXT("ItemTag 가 비어 있다"));
		return;
	}

	// 1. 신원. 1인 1회 제한은 "누가 샀는지" 없이는 성립하지 않는다.
	//    제한이 없는 물건은 신원을 요구하지 않는다 — 요구하면 온라인 서브시스템 사정으로
	//    미끼조차 못 사는 상황이 생긴다.
	FUniqueNetIdRepl BuyerId;
	if (const APlayerState* BuyerState = Interactor->GetPlayerState())
	{
		BuyerId = BuyerState->GetUniqueId();
	}

	if (bOncePerPlayer)
	{
		if (!BuyerId.IsValid())
		{
			// 통과시키면 제한이 조용히 무력화된다. 거절이 낫다.
			ReportResult(EShopPurchaseResult::Unavailable, Interactor,
				TEXT("구매자 신원을 확인할 수 없다 (1인 1회 물건)"));
			return;
		}

		// 이 액터가 아니라 URunProgressSubsystem 이 기억한다. 여기 두면 은신처 레벨과
		// 함께 사라져서, 스테이지를 한 번 다녀오면 신발을 또 살 수 있게 된다.
		if (Run->HasPersonalEquipment(BuyerId, ItemTag))
		{
			ReportResult(EShopPurchaseResult::AlreadyOwned, Interactor, TEXT("이미 보유"));
			return;
		}
	}

	// 2. 지출. 판정과 차감을 한 번에 통과시킨다 — TrySpendTeamGold 주석대로 둘을 나누면
	//    두 명이 같은 프레임에 살 때 잔액이 음수로 내려간다. 실패해도 아무것도 차감되지 않는다.
	if (!Run->TrySpendTeamGold(Price))
	{
		ReportResult(EShopPurchaseResult::NotEnoughGold, Interactor,
			FString::Printf(TEXT("잔액 부족 (가격 %d / 잔액 %d)"), Price, Run->GetTeamGold()));
		return;
	}

	// 3. 지급
	bool bGranted = false;
	switch (PurchaseKind)
	{
	case EShopPurchaseKind::StageSpawn:
		// 목록만 넘어간다. 실제 액터는 작업 레벨의 스폰 구역이 만든다.
		Run->AddPurchasedEquipment(ItemTag, 1);
		bGranted = true;
		break;

	case EShopPurchaseKind::PersonalPassive:
		bGranted = GrantPersonalEquipment(Interactor);
		break;
	}

	if (!bGranted)
	{
		// 지급에 실패했으면 되돌린다. 여기서 넘어가면 $ 만 사라지고 아무 일도 일어나지 않는다.
		Run->AddTeamGold(Price);
		ReportResult(EShopPurchaseResult::Unavailable, Interactor, TEXT("지급 실패 — 되돌렸다"));
		return;
	}

	// 4. 기록. 지급이 확정된 뒤에만 남긴다.
	//    캠페인 단위로 남으므로 스테이지를 다녀와도 유효하고, 작업 레벨의 스폰 구역이
	//    이 기록을 보고 효과를 다시 붙인다.
	if (bOncePerPlayer)
	{
		Run->AddPersonalEquipment(BuyerId, ItemTag);
	}

	ReportResult(EShopPurchaseResult::Purchased, Interactor,
		FString::Printf(TEXT("%s 를 $%d 에 팔았다"), *ItemTag.ToString(), Price));
}

bool AShopDisplay::GrantPersonalEquipment_Implementation(APawn* Buyer)
{
	if (!IsValid(Buyer))
	{
		return false;
	}

	if (!EquipmentComponentClass)
	{
		UE_LOG(LogShop, Warning, TEXT("%s: PersonalPassive 인데 EquipmentComponentClass 가 비어 있다."),
			*GetName());
		return false;
	}

	// 이미 갖고 있으면 두 번 붙이지 않는다. bOncePerPlayer 가 보통 막지만, 같은 효과의
	// 진열대가 둘 있거나 신원을 못 잡았을 때 소음 배율이 두 번 곱해진다(0.5 -> 0.25).
	if (Buyer->FindComponentByClass(EquipmentComponentClass))
	{
		UE_LOG(LogShop, Log, TEXT("%s 는 이미 %s 를 갖고 있다."),
			*GetNameSafe(Buyer), *EquipmentComponentClass->GetName());
		return false;
	}

	UActorComponent* Granted = NewObject<UActorComponent>(Buyer, EquipmentComponentClass);
	if (!Granted)
	{
		return false;
	}

	// 등록해야 BeginPlay 가 돌고, 등록되면서 폰의 OwnedComponents 에 들어가 GC 로부터도 안전해진다.
	Granted->RegisterComponent();

	UE_LOG(LogShop, Log, TEXT("%s 에 %s 를 붙였다."), *GetNameSafe(Buyer), *Granted->GetName());
	return true;
}

void AShopDisplay::ReportResult(EShopPurchaseResult Result, APawn* Interactor, const FString& ServerDetail)
{
	const bool bSuccess = (Result == EShopPurchaseResult::Purchased);

	const URunProgressSubsystem* Run = URunProgressSubsystem::Get(this);
	const int32 GoldAfter = Run ? Run->GetTeamGold() : 0;

	// 1. 서버 로그 — 자세한 사유가 남는 유일한 곳이다. 설정 오류 문구는 개발자용이라
	//    클라이언트로 보내지 않는다.
	UE_LOG(LogShop, Log, TEXT("%s: %s (%s) — 잔액 $%d"),
		*GetName(),
		bSuccess ? TEXT("구매 성공") : TEXT("구매 거절"),
		*ServerDetail,
		GoldAfter);

	// 2. 구매자 본인에게만 — Reliable. 남의 잔액 부족을 내가 볼 이유는 없다.
	//    ClientMessage 는 APlayerController 가 이미 갖고 있는 Client RPC 라서
	//    플레이어 파트 파일을 고칠 필요가 없다.
	if (IsValid(Interactor))
	{
		if (APlayerController* BuyerPC = Cast<APlayerController>(Interactor->GetController()))
		{
			BuyerPC->ClientMessage(MakePlayerMessage(Result, GoldAfter));
		}
	}

	// 3. 전원에게 — 소리와 진열대 위 표시. 클라이언트 창에서 테스트할 때
	//    아무 흔적도 남지 않던 문제가 이것으로 해결된다.
	Multicast_ReportResult(Result, GoldAfter);
}

FString AShopDisplay::MakePlayerMessage(EShopPurchaseResult Result, int32 GoldAfter) const
{
	switch (Result)
	{
	case EShopPurchaseResult::Purchased:
		return FString::Printf(TEXT("구매 완료 — 잔액 $%d"), GoldAfter);

	case EShopPurchaseResult::AlreadyOwned:
		return TEXT("이미 보유하고 있다");

	case EShopPurchaseResult::NotEnoughGold:
		return FString::Printf(TEXT("잔액 부족 — 가격 $%d / 잔액 $%d"), Price, GoldAfter);

	default:
		// 플레이어가 할 수 있는 것이 없는 경우다. 이유를 설명하지 않는다 —
		// "ItemTag 가 비어 있다" 는 플레이어에게 아무 의미가 없다.
		return TEXT("지금은 살 수 없다");
	}
}

void AShopDisplay::Multicast_ReportResult_Implementation(EShopPurchaseResult Result, int32 GoldAfter)
{
	const bool bSuccess = (Result == EShopPurchaseResult::Purchased);

	if (USoundBase* Sound = bSuccess ? PurchaseSound : DeniedSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation());
	}

	// 모든 머신에서 돈다. 그래서 클라이언트 창에서도 진열대 위에 결과가 뜬다.
	ShowShopDebug(MakePlayerMessage(Result, GoldAfter), bSuccess ? FColor::Green : FColor::Red);
}

void AShopDisplay::ShowShopDebug(const FString& Message, const FColor& Color) const
{
#if ENABLE_DRAW_DEBUG
	if (!bShowShopDebug)
	{
		return;
	}

	// 진열대 위에 띄운다. Multicast_ReportResult 가 모든 머신에서 이걸 부르므로
	// 호스트 창과 클라이언트 창 양쪽에 뜬다.
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 80.f),
		Message, nullptr, Color, 3.f, /*bDrawShadow=*/true);
#endif
}

void AShopDisplay::WarnIfMisconfigured() const
{
	// 표에서 값을 받았는데 0 이하인 경우다. 표를 못 읽은 것(bPriceResolved=false)은
	// ResolvePriceFromCatalog 가 이미 따로 찍었다.
	if (bPriceResolved && Price <= 0)
	{
		UE_LOG(LogShop, Warning,
			TEXT("%s: 가격표의 값이 %d 다. 공짜로 팔린다 — Data/ShopCatalog.csv 를 볼 것"),
			*GetName(), Price);
	}

	if (!ItemTag.IsValid())
	{
		// PersonalPassive 는 태그 없이도 동작하지만, 나중에 개인 장비 보유 기록을 붙일 때
		// 태그가 키가 된다. 지금부터 채워 두는 편이 낫다.
		UE_LOG(LogShop, Warning, TEXT("%s: ItemTag 가 비어 있다."), *GetName());
	}

	if (PurchaseKind == EShopPurchaseKind::PersonalPassive && !EquipmentComponentClass)
	{
		UE_LOG(LogShop, Warning, TEXT("%s: PersonalPassive 인데 붙일 컴포넌트가 없다. 아무것도 팔리지 않는다."),
			*GetName());
	}

	if (!DisplayMesh || !DisplayMesh->GetStaticMesh())
	{
		UE_LOG(LogShop, Warning, TEXT("%s: 진열 메시가 비어 있다. 겨눌 수 있는 것이 없다."), *GetName());
	}
}

#if !UE_BUILD_SHIPPING

// ── 임시 콘솔 명령 ──
//
// 팀 골드는 스테이지를 클리어해 밴에 실은 금액으로만 들어온다(HeistGameMode 가 정산한다).
// 은신처만 열어 상점을 시험할 때는 잔액이 0 이라 아무것도 못 산다.
// 상점 UI 와 정상 정산 흐름이 붙으면 이 블록을 지운다. (AEquipmentBase 의 hh.Equip.* 과 같은 자리)

namespace ShopDebugCommands
{
	static void AddGold(const TArray<FString>& Args, UWorld* World)
	{
		URunProgressSubsystem* Run = URunProgressSubsystem::Get(World);
		if (!Run)
		{
			UE_LOG(LogShop, Warning, TEXT("hh.Shop.AddGold: URunProgressSubsystem 을 찾을 수 없다."));
			return;
		}

		const int32 Amount = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 50000;
		Run->AddTeamGold(Amount);

		UE_LOG(LogShop, Log, TEXT("hh.Shop.AddGold: %+d — 잔액 $%d"), Amount, Run->GetTeamGold());
	}

	static void Dump(const TArray<FString>& Args, UWorld* World)
	{
		URunProgressSubsystem* Run = URunProgressSubsystem::Get(World);
		if (!Run)
		{
			UE_LOG(LogShop, Warning, TEXT("hh.Shop.Dump: URunProgressSubsystem 을 찾을 수 없다."));
			return;
		}

		UE_LOG(LogShop, Log, TEXT("=== 상점 상태 ==="));
		UE_LOG(LogShop, Log, TEXT("팀 골드: $%d"), Run->GetTeamGold());

		const TMap<FGameplayTag, int32>& Purchased = Run->GetPurchasedEquipment();
		if (Purchased.IsEmpty())
		{
			UE_LOG(LogShop, Log, TEXT("구매 목록: 없음"));
			return;
		}

		for (const TPair<FGameplayTag, int32>& Entry : Purchased)
		{
			UE_LOG(LogShop, Log, TEXT("구매 목록: %s x%d"), *Entry.Key.ToString(), Entry.Value);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GShopAddGoldCmd(
	TEXT("hh.Shop.AddGold"),
	TEXT("[임시] 팀 공용 골드를 더한다. 인자를 생략하면 50000. 예: hh.Shop.AddGold 100000"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ShopDebugCommands::AddGold));

static FAutoConsoleCommandWithWorldAndArgs GShopDumpCmd(
	TEXT("hh.Shop.Dump"),
	TEXT("[임시] 팀 골드와 구매 목록을 로그로 찍는다."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ShopDebugCommands::Dump));

#endif   // !UE_BUILD_SHIPPING
