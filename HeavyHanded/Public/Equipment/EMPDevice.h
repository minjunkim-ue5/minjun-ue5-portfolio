#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentBase.h"
#include "EMPDevice.generated.h"

/**
 * EMP 장치. 던지면 수류탄처럼 날아가 잠시 뒤 터지고, 반경 안의 감시 카메라를
 * 일정 시간 멈춘다. (기획서 7장 — $10,000, "감시 카메라 15초 정지")
 *
 * [베이스와 다른 것이 값뿐이다 — AStickyBomb 와 같다]
 *   날아간다 / 착탄한다 / 신관이 돈다 / 터진다 / 사라진다 는 전부 AEquipmentBase 의
 *   상태 기계다. 여기서 하는 것은 생성자에서 값을 정하고, 터질 때 카메라에 알리는 것뿐이다.
 *
 * [붙지 않는다]
 *   bAttachOnImpact 가 false 다. 점착 폭탄은 금고 문에 정확히 붙여야 하지만 이쪽은
 *   "이 근처" 면 되는 물건이라, 던져서 굴러간 자리에서 터지는 편이 쓰기 쉽다.
 *
 * [소음을 발행하지 않는다]
 *   DeployNoiseTag / ActiveNoiseTag 를 둘 다 비워 둔다. 몰래 지나가려고 쓰는 물건인데
 *   터질 때 경비를 부르면 아이템이 자기 목적을 스스로 깬다. 폭발음은 BP 의 SpentEffect
 *   로 내되 **연출 전용**이고, 소음 시스템(UNoiseEmitterComponent)에는 아무것도 보내지 않는다.
 *   그래서 Noise.Equipment.EMP 태그도 만들지 않았다.
 *
 * [이미 발각된 경보는 무마하지 않는다]
 *   ASecurityCamera::Disable() 은 감지를 멈출 뿐 진행 중인 bAlarmed 를 내리지 않는다.
 *   그래서 카메라에 이미 걸린 뒤에 터뜨리면 경비 호출(GuardCallDelay)은 그대로 진행된다.
 *   2026-09-30 에 그대로 두기로 정했다 — EMP 는 "걸리기 전에 쓰는 물건" 이다.
 *
 * [벽을 통과한다]
 *   가림 판정을 넣지 않는다. 전자기 펄스라는 설정이고, 모퉁이 너머로 던져 넣고 지나가는
 *   그림이 이 장비의 쓰임새다. AStickyBomb::BlastRadius 도 같은 이유로 가림을 보지 않는다.
 */
UCLASS()
class HEAVYHANDED_API AEMPDevice : public AEquipmentBase
{
	GENERATED_BODY()

public:
	AEMPDevice();

	/** 펄스가 닿는 거리 */
	UFUNCTION(BlueprintPure, Category = "Equipment|EMP")
	float GetPulseRadius() const { return PulseRadius; }

protected:
	virtual void OnActivated() override;

	/**
	 * 펄스 이펙트에 반경을 넘긴다 (나이아가라 User 파라미터 `Radius`). 모든 머신에서 불린다.
	 *
	 * 나이아가라에 크기를 숫자로 박지 않는 이유는 베이스 주석에 적어 두었다 —
	 * **보이는 구와 실제로 꺼지는 범위가 어긋나면 안 된다.** 이펙트 쪽은 이 값을 받아
	 * 구의 반지름으로 쓰기만 하면 된다.
	 */
	virtual void ConfigureEffect(class UNiagaraComponent* Effect, EEquipmentState ForState) override;

	/**
	 * 펄스가 닿는 거리. 이 안의 감시 카메라가 전부 멈춘다.
	 *
	 * ⚠ 임시값이다. 카메라를 레벨에 배치해 보고 정할 것 — 카메라 감지 반경이 1500 이라
	 * 그보다 작게 두어 "어디서 터뜨릴까" 가 선택이 되게 하려는 의도지만, 실제로 걸어
	 * 보기 전에는 8m 가 좁은지 넓은지 알 수 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|EMP",
		meta = (ClampMin = "0.0", Units = "cm"))
	float PulseRadius = 800.f;

	/**
	 * 카메라를 몇 초 동안 멈출 것인가. 기획서 정가 설명의 "15초" 가 이 값이다.
	 *
	 * 시간은 카메라가 자기 타이머로 센다(ASecurityCamera::Disable). 그래서 이 액터가
	 * 터진 직후 사라져도 카메라는 끝까지 멈춰 있다 — 수명을 서로 묶지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|EMP",
		meta = (ClampMin = "0.0", Units = "s"))
	float DisableSeconds = 15.f;

	/**
	 * 펄스 반경을 구로 그린다.
	 *
	 * 액터별 스위치인 것은 AStickyBomb::bShowBlastDebug 와 같은 이유다 — 레벨에 여러 개가
	 * 깔려도 하나만 본다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|EMP")
	bool bShowPulseDebug = false;
};
