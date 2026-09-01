#include "Loot/LootHeavyComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Core/HeavyHandedGameplayTags.h"
#include "Engine/StaticMesh.h"           // GetStaticMesh()->GetName() — 에셋 이름을 찍는다
#include "Loot/LootBase.h"
#include "Loot/LootLog.h"
#include "Loot/LootSettings.h"

ULootHeavyComponent::ULootHeavyComponent()
{
	// 아직 매 프레임 볼 것이 없다. 거리 제약이 들어오는 C 단계에서 다시 판단한다.
	PrimaryComponentTick.bCanEverTick = false;
}

void ULootHeavyComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerLoot = Cast<ALootBase>(GetOwner());
	if (!IsValid(OwnerLoot))
	{
		// 행 이름·태그·메시를 전부 ALootBase 에서 읽으므로 다른 액터에는 붙을 수 없다.
		UE_LOG(LogLoot, Warning,
			TEXT("[%s] ULootHeavyComponent 는 ALootBase 에만 붙일 수 있다. 중량형 판정이 비활성화된다."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// 이 컴포넌트가 붙어 있다는 것이 곧 "중량형" 이라는 선언이다.
	// 예전에는 ALootBase 가 WeightClass 를 보고 달았는데, 그 예외를 여기서 없앤다.
	OwnerLoot->AddLootTypeTag(HHTags::Loot_Type_Heavy);

	ResolveData();

	// 수치를 채운 뒤에 본다. 표에서 온 이름을 검사해야 하기 때문이다.
	WarnOnMissingGripSockets();
}

void ULootHeavyComponent::ResolveData()
{
	if (bDataResolved)
	{
		return;
	}
	bDataResolved = true;

	if (!IsValid(OwnerLoot))
	{
		OwnerLoot = Cast<ALootBase>(GetOwner());
		if (!IsValid(OwnerLoot))
		{
			return;
		}
	}

	const FName RowName = OwnerLoot->GetLootRowName();

	// 표를 안 쓰는 노획물이다. BP 에 적힌 Data 를 그대로 쓴다 — 실험물용 폴백이다.
	if (RowName.IsNone())
	{
		return;
	}

	const FLootHeavyData* Row = ULootSettings::FindTraitRow<FLootHeavyData>(
		ULootSettings::Get()->HeavyTable, RowName, GetName());

	if (!Row)
	{
		// 컴포넌트가 붙었다는 것은 중량형으로 만들겠다는 선언인데 표에 행이 없다.
		// 기본값으로 돌아서 겉보기에는 멀쩡하지만, 그립 소켓 이름이 이 메시와 맞지 않으면
		// 두 사람이 같은 지점을 잡게 되고 2인 캐리의 의미가 사라진다.
		// 반대 방향(행은 있는데 컴포넌트가 없다)은 ALootBase 가 잡는다.
		UE_LOG(LogLoot, Warning,
			TEXT("[Loot:%s] ULootHeavyComponent 가 붙어 있는데 DT_LootHeavy 에 '%s' 행이 없다 "
				 "— 기본값으로 돈다. 표에 행을 추가하거나 Project Settings > Game > Loot 에서 "
				 "Heavy Table 이 지정돼 있는지 확인할 것"),
			*OwnerLoot->GetName(), *RowName.ToString());
		return;
	}

	Data = *Row;
}

void ULootHeavyComponent::WarnOnMissingGripSockets() const
{
	// 서버에서만 본다. 콘텐츠 오류라 모든 머신에서 답이 같은데, 2인 PIE 는 창이 셋이라
	// (호스트 + 클라 + 호스트가 도는 서버 월드) 같은 줄이 여러 번 찍혀 로그가 묻힌다.
	if (!IsValid(OwnerLoot) || !OwnerLoot->HasAuthority())
	{
		return;
	}

	const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(OwnerLoot->GetPhysicsRoot());
	if (!IsValid(Mesh))
	{
		return;
	}

	// 어느 메시를 열어야 하는지 알려주려고 컴포넌트 이름이 아니라 에셋 이름을 찍는다.
	const FString MeshName = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetName() : TEXT("(메시 없음)");
	const FString LootName = OwnerLoot->GetName();
	const FName SocketA = Data.GripSocketA;
	const FName SocketB = Data.GripSocketB;

	// 앞이 틀리면 뒤는 볼 필요가 없다. 이름이 비었는데 "메시에 없다" 까지 같이 찍으면
	// 진짜 원인이 어느 쪽인지 흐려진다.
	if (SocketA.IsNone() || SocketB.IsNone())
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[Loot:%s] 그립 소켓 이름이 비어 있다 (A='%s', B='%s') "
				 "— 두 사람이 같은 지점을 잡게 된다. DT_LootHeavy 의 '%s' 행에 이름을 적을 것"),
			*LootName, *SocketA.ToString(), *SocketB.ToString(), *OwnerLoot->GetLootRowName().ToString());
		return;
	}

	if (SocketA == SocketB)
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[Loot:%s] 그립 소켓 A 와 B 가 같은 이름이다 ('%s') "
				 "— 표에서 칸을 복사하고 한쪽을 안 고친 경우다. DT_LootHeavy 의 '%s' 행을 확인할 것"),
			*LootName, *SocketA.ToString(), *OwnerLoot->GetLootRowName().ToString());
		return;
	}

	// 여기가 제일 자주 걸린다. 표의 이름과 메시의 소켓 이름은 아무도 맞춰주지 않는다.
	bool bMissing = false;
	for (const FName& Socket : { SocketA, SocketB })
	{
		if (!Mesh->DoesSocketExist(Socket))
		{
			UE_LOG(LogLoot, Warning,
				TEXT("[Loot:%s] 메시 '%s' 에 소켓 '%s' 가 없다 "
					 "— GetGripSeparation 이 0 을 돌려주고 플레이어 파트가 2인 캐리 거리를 잡지 못한다. "
					 "메시를 열어 소켓을 추가하거나 DT_LootHeavy 의 이름을 메시에 맞출 것"),
				*LootName, *MeshName, *Socket.ToString());
			bMissing = true;
		}
	}

	if (bMissing)
	{
		return;
	}

	// 소켓을 만들기는 했는데 원점에 그대로 둔 경우다. 이름 검사는 통과하므로 이것까지 봐야
	// "설정을 다 했는데 왜 안 되지" 가 안 나온다.
	//
	// 50cm 인 이유: 캐릭터 캡슐 반지름이 34 안팎이라 두 사람이 그보다 가까우면 서로 겹친다.
	// 물건 길이 자체가 그보다 짧으면 애초에 둘이 들 물건이 아니다.
	constexpr float MinUsefulSeparation = 50.f;
	const float Separation = OwnerLoot->GetGripSeparation();
	if (Separation < MinUsefulSeparation)
	{
		UE_LOG(LogLoot, Warning,
			TEXT("[Loot:%s] 그립 소켓 '%s' 와 '%s' 가 %.1fcm 밖에 안 떨어져 있다 "
				 "— 두 사람이 겹쳐 선다. 메시 '%s' 에서 두 소켓을 물건 양 끝으로 벌릴 것"),
			*LootName, *SocketA.ToString(), *SocketB.ToString(), Separation, *MeshName);
	}
}
