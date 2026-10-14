#include "Equipment/EMPDevice.h"

#include "Core/HeavyHandedGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"            // TActorIterator
#include "Hazards/SecurityCamera.h"
#include "Loot/LootLog.h"
#include "NiagaraComponent.h"      // SetFloatParameter — 펄스 반경을 이펙트에 넘긴다

AEMPDevice::AEMPDevice()
{
	EquipmentTag = HHTags::Equipment_EMP;

	// 붙지 않는다. 던져서 구른 자리에서 터진다 (헤더 주석 참고)
	bAttachOnImpact = false;

	// 착탄하면 신관이 돈다. 던지자마자 터지면 던진 사람이 자기 시야 안에서 터뜨리게 되고,
	// 카메라를 끄고 '지나갈' 시간을 벌 수 없다.
	ActivationMode = EEquipmentActivation::AfterDelay;
	ActivationDelay = 1.5f;

	// 폭발은 순간적이다. 발동과 동시에 Spent 로 간다 — 15초는 카메라 쪽이 센다.
	EffectDuration = 0.f;

	// 수류탄처럼 던진다. 점착 폭탄(1200 / 0.12)보다 느리고 포물선이 높다 —
	// 정확히 맞히는 물건이 아니라 모퉁이 너머로 넘기는 물건이라서다.
	ThrowParams.Speed = 1000.f;
	ThrowParams.UpwardRatio = 0.30f;
	ThrowParams.SpinSpeed = 180.f;

	// DeployNoiseTag / ActiveNoiseTag 는 비워 둔다 — 소음 시스템에 아무것도 발행하지 않는다.
	// 폭발음은 BP 의 SpentEffect 로 내는 연출이고, 경비 청각에는 걸리지 않는다 (헤더 주석 참고).
}

void AEMPDevice::ConfigureEffect(UNiagaraComponent* Effect, EEquipmentState ForState)
{
	// 펄스는 터지는 순간(Spent)에만 나온다. 다른 상태의 이펙트까지 반경을 받을 이유는 없다.
	if (ForState != EEquipmentState::Spent || !IsValid(Effect))
	{
		return;
	}

	// 나이아가라 쪽 이름은 User.Radius 지만, C++ 에서는 접두사를 떼고 넘긴다.
	// 이펙트에 이 파라미터가 없으면 조용히 무시된다 — 그래서 구가 엉뚱한 크기로 나오면
	// 먼저 나이아가라에 User.Radius(float) 가 있는지부터 볼 것.
	Effect->SetFloatParameter(TEXT("Radius"), PulseRadius);
}

void AEMPDevice::OnActivated()
{
	Super::OnActivated();

	UWorld* World = GetWorld();

#if ENABLE_DRAW_DEBUG
	// 연출과 같아서 모든 머신에서 그린다. 권위 검사 앞에 두는 이유는 AStickyBomb 와 같다 —
	// 클라이언트 화면에서 폭발 위치가 어긋나 보일 때 그것을 봐야 한다
	if (bShowPulseDebug && World)
	{
		DrawDebugSphere(World, GetActorLocation(), PulseRadius, 16, FColor::Cyan, false, 3.f, 0, 2.f);
	}
#endif

	// 무력화 판정은 서버가 한다. 연출은 베이스가 모든 머신에서 이미 처리했다.
	if (!HasAuthority() || !World)
	{
		return;
	}

	// [왜 오버랩이 아니라 순회인가]
	//   ASecurityCamera 는 두 메시 모두 콜리전을 꺼 두었다(NoCollision). 구체 오버랩으로
	//   찾으면 **조용히 0개가 나온다** — 콜리전 프로파일을 뒤지며 한참 헤매게 되는 종류의 실패다.
	//   카메라는 레벨에 많아야 십여 대고 이 순회는 폭발 순간 한 번만 도니, 거리만 재면 된다.
	//   AStickyBomb 가 금고 문을 찾는 방식과 같다.
	int32 DisabledCount = 0;
	const float RadiusSq = FMath::Square(PulseRadius);

	for (TActorIterator<ASecurityCamera> It(World); It; ++It)
	{
		ASecurityCamera* Camera = *It;
		if (!IsValid(Camera))
		{
			continue;
		}

		// 카메라 루트(CameraBase)까지의 거리다. 벽·천장에 붙은 본체 위치이고,
		// 가림 판정은 하지 않는다 (헤더 주석 참고)
		if (FVector::DistSquared(GetActorLocation(), Camera->GetActorLocation()) > RadiusSq)
		{
			continue;
		}

		// 서버 권위 검사는 Disable() 안에 이미 있다. 이미 멈춘 카메라를 다시 부르면
		// 타이머가 새로 걸려 시간이 연장된다 — 중첩이 아니라 갱신이라 그대로 둔다.
		Camera->Disable(DisableSeconds);
		++DisabledCount;
	}

	// 0대일 때도 찍는다. 카메라가 아직 레벨에 배치되지 않은 단계라, 아무 일도 안 일어난 것이
	// '고장' 인지 '대상이 없음' 인지 로그로 구별되어야 한다.
	UE_LOG(LogLoot, Log, TEXT("[EMPDevice:%s] 펄스 — 반경 %.0f, 무력화한 카메라 %d대 (%.0f초)"),
		*GetName(), PulseRadius, DisabledCount, DisableSeconds);
}
