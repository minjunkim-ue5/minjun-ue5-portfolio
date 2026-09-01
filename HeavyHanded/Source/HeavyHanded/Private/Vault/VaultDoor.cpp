#include "Vault/VaultDoor.h"

#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/LootLog.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AVaultDoor::AVaultDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;

	// 문은 움직이지 않는다. 위치를 계속 보내 봐야 대역폭만 쓴다
	SetReplicateMovement(false);

	// [왜 항상 관련 액터인가]
	//   기본 관련성은 거리 기준(약 150m)이라, 멀리서 폭파된 뒤 다가온 플레이어에게는
	//   bIsBreached 의 최초 복제가 그때 도착한다. RepNotify 는 그것도 '변경' 으로 보므로
	//   이미 끝난 폭발의 연기가 눈앞에서 다시 피어오른다.
	//   레벨에 한두 개뿐인 액터라 항상 켜 두는 비용이 그 증상보다 싸다.
	//
	//   판이 진행되는 중에 새로 접속하는 경우는 여전히 남지만, 이 게임은 전원이
	//   로비에서 함께 출발하므로 그런 접속이 없다.
	bAlwaysRelevant = true;

	// 볼트 박힌 사각 판. 끝까지 남으므로 루트다
	DoorFrame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorFrame"));
	SetRootComponent(DoorFrame);
	DoorFrame->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	// 가운데 원형 뚜껑. 이것과 그 아래 붙은 것이 폭발로 사라진다
	DoorLid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorLid"));
	DoorLid->SetupAttachment(DoorFrame);
	DoorLid->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	// [뚜껑만 Movable 이다]
	//   Static 이면 라이트맵에 그림자가 구워진다. 뚜껑을 지워도 그 그림자는 바닥에 남아서
	//   "문은 사라졌는데 문 그림자가 그대로" 인 상태가 된다. 프레임은 안 사라지므로
	//   BP 에서 Static 으로 두고 라이팅을 구워도 된다 — Movable 부모 아래 Static 자식은
	//   불가능하지만 그 반대는 허용된다.
	DoorLid->SetMobility(EComponentMobility::Movable);

	// 파편이 날아갈 방향. 뷰포트에서 이 화살표를 금고 바깥으로 돌려 놓으면 그걸로 끝이다.
	// 루트에 붙인다 — 뚜껑에 붙이면 뚜껑을 숨길 때 같이 숨겨져 다시 맞출 때 안 보인다.
	BreachArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("BreachArrow"));
	BreachArrow->SetupAttachment(DoorFrame);
	BreachArrow->SetArrowColor(FLinearColor(1.f, 0.45f, 0.1f));
	BreachArrow->ArrowSize = 3.f;
	BreachArrow->ArrowLength = 120.f;
	// 크기가 화면에 고정되면 큰 금고 앞에서 점처럼 보인다. 월드 크기로 그린다.
	BreachArrow->bIsScreenSizeScaled = false;
}

FVector AVaultDoor::GetBreachDirection() const
{
	if (IsValid(BreachArrow))
	{
		return BreachArrow->GetForwardVector();
	}

	return GetActorForwardVector();
}

void AVaultDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AVaultDoor, bIsBreached);
}

void AVaultDoor::BeginPlay()
{
	Super::BeginPlay();

	// 설정 실수는 전부 "폭탄을 붙였는데 아무 일도 안 일어난다" 하나로만 드러난다.
	// 원인까지 가는 데 오래 걸리므로 켤 때 미리 말해 둔다.
	if (!IsValid(DoorLid) || !DoorLid->GetStaticMesh())
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[VaultDoor:%s] DoorLid 에 메시가 없다 — 폭파해도 사라질 것이 없다"), *GetName());
	}

	if (!IsValid(BreachEffect))
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[VaultDoor:%s] BreachEffect 가 비어 있다 — 뚜껑이 소리 없이 증발한다"), *GetName());
	}
}

bool AVaultDoor::TryBreach(const AActor* Explosive, const FVector& BlastOrigin, float BlastRadius)
{
	// Server RPC 는 요청일 뿐이고 판정은 서버가 한다. 여기가 그 판정 자리다
	if (!HasAuthority() || bIsBreached)
	{
		return false;
	}

	if (!IsValid(DoorLid))
	{
		return false;
	}

	// ── 1. 뚜껑(또는 그 아래 핸들)에 직접 붙었나 ──
	//
	// 정상적인 사용법이다. 붙어 있으면 거리를 잴 필요가 없다 — 이미 닿아 있다.
	bool bOnLid = false;

	if (IsValid(Explosive))
	{
		const USceneComponent* Root = Explosive->GetRootComponent();

		for (const USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
			Parent != nullptr;
			Parent = Parent->GetAttachParent())
		{
			if (Parent == DoorLid)
			{
				bOnLid = true;
				break;
			}
		}
	}

	// ── 2. 뚜껑 표면에서 충분히 가까운가 ──
	//
	// 폭탄이 뚜껑 가장자리를 스치고 프레임에 붙는 일이 흔하다. 플레이어 눈에는
	// 문에 제대로 붙인 것으로 보이므로 그 정도는 구제한다.
	if (!bOnLid)
	{
		FVector ClosestPoint = FVector::ZeroVector;
		float Distance = DoorLid->GetClosestPointOnCollision(BlastOrigin, ClosestPoint);

		if (Distance < 0.f)
		{
			// 단순 콜리전이 없으면 표면을 잴 수 없다. 바운드 구로 대신한다 —
			// 실제 표면보다 후하게 잡히지만, 못 재서 영영 안 열리는 것보다 낫다
			Distance = FMath::Max(0.f,
				FVector::Dist(BlastOrigin, DoorLid->Bounds.Origin) - DoorLid->Bounds.SphereRadius);
		}

		// 폭발이 뚜껑까지 닿아야 하고(BlastRadius), 문이 인정하는 거리여야 한다(BreachRadius).
		// 둘 중 엄한 쪽이 이긴다
		bOnLid = Distance <= FMath::Min(BreachRadius, BlastRadius);
	}

	if (!bOnLid)
	{
		UE_LOG(LogLoot, Log,
			TEXT("[VaultDoor:%s] 폭발이 뚜껑에서 벗어났다 — 열리지 않는다"), *GetName());
		return false;
	}

	bIsBreached = true;

	// 서버에서 직접 대입하면 RepNotify 가 안 불린다. 손으로 불러야 호스트 화면에서도 열린다
	OnRep_bIsBreached();

	UE_LOG(LogLoot, Log, TEXT("[VaultDoor:%s] 개방"), *GetName());
	return true;
}

void AVaultDoor::OnRep_bIsBreached()
{
	if (bIsBreached)
	{
		ApplyBreach();
	}
}

void AVaultDoor::ApplyBreach()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// [피벗이 아니라 바운즈 중심이다]
	//   GetComponentLocation 은 메시의 피벗이고, 눈에 보이는 중심과 다를 수 있다.
	//   실제로 금고 뚜껑은 피벗이 한쪽으로 치우쳐 있어서 폭발이 문 왼쪽 옆에서 터졌다.
	//   Bounds.Origin 은 그려지는 형상의 한가운데라 피벗이 어디에 있든 맞는다.
	const FVector LidCenter = IsValid(DoorLid) ? DoorLid->Bounds.Origin : GetActorLocation();

	// 뚜껑은 두꺼워서 중심이 문 **안쪽**이다. 그대로 두면 연기 절반이 문에 묻힌다.
	// 바깥으로 조금 밀어낸 자리에서 터뜨린다.
	const FVector LidLocation = LidCenter + GetBreachDirection() * BreachEffectForwardOffset;

	// 이펙트의 로컬 +X 가 문 바깥을 향하도록 세운다. Niagara 쪽은 Local Space 를 켜고
	// 콘 축을 (1,0,0) 으로 두면 문이 어느 방향을 보든 파편이 항상 바깥으로 날아간다.
	// 뚜껑의 회전을 그대로 쓰지 않는 이유는 메시가 어느 축을 정면으로 삼았는지 모르기 때문이다.
	const FRotator BlastRotation = FRotationMatrix::MakeFromX(GetBreachDirection()).Rotator();

	// 데디케이티드 서버는 화면도 스피커도 없다. 리슨 서버의 호스트는 클라이언트이기도 하므로
	// 여기 걸리지 않는다 — 호스트 화면에서도 연기가 정상적으로 보인다
	if (World->GetNetMode() != NM_DedicatedServer)
	{
		if (IsValid(BreachEffect))
		{
			// 뚜껑에 붙이지 않는다. 붙였다가는 DoorHideDelay 뒤에 부모를 숨길 때
			// 연기도 같이 사라져서, 정작 가려야 할 순간에 화면이 맑아진다
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, BreachEffect,
				LidLocation, BlastRotation, FVector(BreachEffectScale));
		}

		if (IsValid(BreachSound))
		{
			UGameplayStatics::PlaySoundAtLocation(World, BreachSound, LidLocation);
		}
	}

	// 뚜껑 지우기는 데디케이티드 서버에서도 해야 한다 — 연출이 아니라 콜리전이다.
	// 안 지우면 서버에서만 입구가 막혀 있어서 클라이언트가 들어가려다 되밀린다
	if (DoorHideDelay > 0.f)
	{
		World->GetTimerManager().SetTimer(HideTimer, this, &AVaultDoor::HideLid, DoorHideDelay, false);
	}
	else
	{
		HideLid();
	}
}

void AVaultDoor::HideLid()
{
	if (!IsValid(DoorLid))
	{
		return;
	}

	// 핸들·장식이 뚜껑 아래에 붙어 있다. 전파를 켜야 같이 사라진다 —
	// 안 그러면 뚜껑만 없어지고 손잡이가 허공에 뜬다
	DoorLid->SetVisibility(false, /*bPropagateToChildren=*/true);

	// 콜리전·내비게이션에는 전파 인자가 없어서 직접 훑는다
	TArray<USceneComponent*> Descendants;
	DoorLid->GetChildrenComponents(/*bIncludeAllDescendants=*/true, Descendants);
	Descendants.Add(DoorLid);

	for (USceneComponent* Component : Descendants)
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!IsValid(Primitive))
		{
			continue;
		}

		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// 경비가 뚫린 입구로 지나갈 수 있어야 한다. 내비메시가 동적일 때만 실제로 갱신되고,
		// 구워 둔 것이라면 아무 일도 일어나지 않는다 — 그때는 AI 파트에 갱신 방식을 물어야 한다
		Primitive->SetCanEverAffectNavigation(false);
	}
}
