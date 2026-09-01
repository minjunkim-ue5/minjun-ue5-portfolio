#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentBase.h"
#include "StickyBomb.generated.h"

/**
 * 점착 폭탄. 던지면 맞은 곳에 붙고 몇 초 뒤 폭발한다. (기획서 7장 — $25,000)
 *
 * 대형 금고의 문(AVaultDoor)을 부수는 것이 본래 용도다.
 *
 * [베이스와 다른 것이 값뿐이다]
 *   붙는다 / 퓨즈가 돈다 / 터진다 / 사라진다 는 전부 AEquipmentBase 의 상태 기계다.
 *   여기서 하는 것은 생성자에서 값을 정하고, 터질 때 금고 문에 알리는 것뿐이다.
 *
 * [폭탄은 금고의 구조를 모른다]
 *   "여기서 이만큼 터졌다" 만 알리고, 그것이 문을 여는지는 AVaultDoor 가 판정한다.
 *   뚜껑이 어디에 붙어 있고 얼마나 큰지를 폭탄이 알기 시작하면, 금고 메시가 바뀔 때마다
 *   폭탄을 고쳐야 한다.
 *
 * [EffectDuration 이 0 이다]
 *   폭발은 순간적이라 Active 가 이어지지 않는다. 그래서 폭발 연출은 ActiveEffect 가
 *   아니라 SpentEffect 에 넣는다 — 저쪽은 액터에 붙지 않고 월드에 스폰되므로
 *   폭탄이 사라져도 파편이 남는다. 붙였다면 SpentDestroyDelay 뒤에 같이 사라진다.
 */
UCLASS()
class HEAVYHANDED_API AStickyBomb : public AEquipmentBase
{
	GENERATED_BODY()

public:
	AStickyBomb();

	/** 폭발이 닿는 거리 */
	UFUNCTION(BlueprintPure, Category = "Equipment|Blast")
	float GetBlastRadius() const { return BlastRadius; }

protected:
	virtual void OnActivated() override;

	/**
	 * 폭발이 닿는 거리. 금고 문을 부수는 판정에 쓴다.
	 *
	 * 벽 너머로도 닿는다 — 가림 판정은 넣지 않는다. 금고 문에 직접 붙여야 열리는
	 * 물건이라 "구석에서 터뜨려 여는" 우회로가 애초에 생기지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Blast",
		meta = (ClampMin = "0.0", Units = "cm"))
	float BlastRadius = 300.f;

	/**
	 * 폭발 반경을 구로 그린다.
	 *
	 * 붙인 자리와 반경이 맞는지 눈으로 볼 때만 켠다. 액터별 스위치인 것은
	 * ALootBase::bShowImpactDebug 와 같은 이유다 — 레벨에 여러 개가 깔려도 하나만 본다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Blast")
	bool bShowBlastDebug = false;
};
