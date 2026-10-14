#include "Equipment/Decoy.h"

#include "Core/HeavyHandedGameplayTags.h"

ADecoy::ADecoy()
{
	EquipmentTag = HHTags::Equipment_Decoy;

	// 붙지 않는다. 개껌은 던지면 바닥에 떨어져 구르는 물건이다.
	// 점착 폭탄과 갈리는 지점이 여기다.
	bAttachOnImpact = false;

	// 닿는 순간부터 시끄러워야 한다. 퓨즈를 두면 그 사이에 경비가 지나가 버린다.
	ActivationMode = EEquipmentActivation::OnImpact;

	// 기획서 7장 — 경비 20초 유인.
	EffectDuration = 20.f;

	// 유인의 실체. 이 태그를 20초 동안 반복 발행하는 것이 미끼의 전부다.
	ActiveNoiseTag = HHTags::Noise_Equipment_Decoy;

	// 1초마다 낸다. 짧게 잡을수록 강하게 끌지만, 소음 파트의 쿨다운에 걸리면
	// 걸러지는 호출만 늘어난다. 프로파일 값을 보고 맞추는 편이 낫다.
	ActiveNoiseInterval = 1.f;

	// ThrowParams 는 건드리지 않는다.
	//
	// 기본값(900 / 0.25 / 180)이 "던지면 포물선을 그리며 날아가 바닥에 떨어져 구른다" 이고,
	// 미끼에 필요한 것이 정확히 그것이다. 점착 폭탄이 1200 / 0.12 / 0 으로 덮은 것은
	// 곧게 날아가 정확히 붙어야 하는 물건이라 기본값이 안 맞았기 때문이다.
	// 같은 값을 여기 한 번 더 적으면 기본값이 바뀔 때 이쪽만 옛 값으로 남는다.
}
