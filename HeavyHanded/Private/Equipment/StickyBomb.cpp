#include "Equipment/StickyBomb.h"

#include "Core/HeavyHandedGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"        // TActorIterator
#include "Loot/LootLog.h"
#include "Vault/VaultDoor.h"

AStickyBomb::AStickyBomb()
{
	EquipmentTag = HHTags::Equipment_StickyBomb;

	// 던진 곳에 붙는다. 이것이 '점착' 이고, 미끼·EMP 와 갈리는 지점이다.
	bAttachOnImpact = true;

	// 붙은 뒤 퓨즈가 돈다. 붙자마자 터지면 던진 사람이 같이 휘말린다.
	ActivationMode = EEquipmentActivation::AfterDelay;
	ActivationDelay = 3.f;

	// 폭발은 순간적이다. 발동과 동시에 Spent 로 간다.
	EffectDuration = 0.f;

	// 던져서 정확히 붙여야 하는 물건이라 곧게 날아가야 한다.
	// 노획물 기본값(900 / 0.25)보다 빠르고 포물선이 얕다.
	ThrowParams.Speed = 1200.f;
	ThrowParams.UpwardRatio = 0.12f;

	// 붙는 물건이라 회전은 방해만 된다. 굴러가서 엉뚱한 면에 붙는다.
	ThrowParams.SpinSpeed = 0.f;
}

void AStickyBomb::OnActivated()
{
	Super::OnActivated();

	UWorld* World = GetWorld();

#if ENABLE_DRAW_DEBUG
	// 연출과 같아서 모든 머신에서 그린다. 권위 검사 앞에 두는 이유는,
	// 클라이언트 화면에서 폭발 위치가 어긋나 보일 때 그것을 봐야 하기 때문이다
	if (bShowBlastDebug && World)
	{
		DrawDebugSphere(World, GetActorLocation(), BlastRadius, 16, FColor::Orange, false, 3.f, 0, 2.f);
	}
#endif

	// 실제 파괴는 서버가 판정한다. 연출은 베이스가 모든 머신에서 이미 처리했다.
	if (!HasAuthority() || !World)
	{
		return;
	}

	// [왜 오버랩이 아니라 순회인가]
	//   금고 문은 레벨에 한두 개뿐이고, 폭발은 판당 몇 번 일어나지 않는다.
	//   그리고 어차피 거리 판정은 문이 자기 뚜껑 기준으로 다시 한다 — 오버랩으로
	//   미리 걸러 봐야 같은 계산을 두 번 하는 것이고, 대신 콜리전 채널 설정이
	//   맞아야 한다는 조건이 하나 늘어난다. 그쪽이 조용히 깨지기 더 쉽다.
	int32 BreachedCount = 0;

	for (TActorIterator<AVaultDoor> It(World); It; ++It)
	{
		if (It->TryBreach(this, GetActorLocation(), BlastRadius))
		{
			++BreachedCount;
		}
	}

	UE_LOG(LogLoot, Log, TEXT("[StickyBomb:%s] 폭발 — 반경 %.0f, 개방한 금고 문 %d개"),
		*GetName(), BlastRadius, BreachedCount);
}
