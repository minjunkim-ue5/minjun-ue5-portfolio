#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentBase.h"
#include "MedKit.generated.h"

class ABaseCharacter;

/**
 * 응급 키트. 던져서 다운된 동료를 즉시 일으킨다. (기획서 7장 — $7,000 / 1회)
 *
 * [E키 부활과 무엇이 다른가]
 *   UGAB_Interact 의 부활은 5초 채널링이고 그동안 리바이버가 묶인다. 경비가 오는 중이면
 *   둘 다 잡힌다. 이 물건은 그 5초를 돈으로 사는 것이라 즉발이어야 하고,
 *   그래서 EffectDuration 이 0 이다.
 *
 * [맞은 사람 우선, 빗나가면 착지 반경]
 *   던진 물건이 누운 사람에게 정확히 맞기는 은근히 어렵다. 다운된 동료는 바닥에 있어서
 *   더 그렇고, $7,000 짜리가 살짝 빗나갔다고 날아가면 쓸 수 없는 물건이 된다.
 *   그래서 맞았으면 그 사람에게, 아니면 떨어진 자리 반경 안에서 찾는다.
 *   반경을 작게 잡는 것이 중요하다 — 크면 아무 데나 던져도 되는 물건이 된다.
 *
 * [부활은 태그로 한다]
 *   대상의 ASC 에서 State.Downed 를 부여한 GE 를 지운다. UGAB_Interact 가 쓰는 것과
 *   같은 경로다 — 그쪽 주석대로 "건 쪽이 대상의 GE 클래스를 알 필요가 없도록" 태그로 지운다.
 *   덕분에 플레이어 파트에 요청할 것이 없다.
 */
UCLASS()
class HEAVYHANDED_API AMedKit : public AEquipmentBase
{
	GENERATED_BODY()

public:
	AMedKit();

protected:
	virtual void OnDeployed(const FHitResult& Hit) override;
	virtual void OnActivated() override;

	/**
	 * 맞히지 못했을 때 다운된 동료를 찾는 반경.
	 *
	 * 작게 잡는다. 크면 대충 던져도 되는 물건이 되어 "정확히 맞힌다" 는 조작이 사라진다.
	 * 누운 캐릭터의 몸길이쯤이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|MedKit",
		meta = (ClampMin = "0.0", Units = "cm"))
	float RescueRadius = 200.f;

	/**
	 * 반경을 구로 그리고 누구를 일으켰는지 로그로 남긴다.
	 *
	 * 던진 것이 맞았는지 반경으로 잡힌 것인지가 눈으로 구별되지 않아서, 반경 값을
	 * 조정할 때 이것 없이는 판단할 수 없다. (AStickyBomb::bShowBlastDebug 와 같은 용법)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|MedKit")
	bool bShowRescueDebug = false;

	/**
	 * 일어난 사람에게 붙는 회복 연출.
	 *
	 * 키트가 떨어진 자리가 아니라 **사람에게** 붙인다. 누가 살아났는지가 보여야 하고,
	 * 빗나간 키트가 반경으로 살렸을 때는 둘이 최대 2m 떨어져 있어서 자리에 띄우면
	 * 엉뚱한 바닥이 빛난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|MedKit")
	TObjectPtr<class UNiagaraSystem> RescueEffect;

	/**
	 * 회복 연출을 전원에게 내려보낸다. 일으킨 사람에게 붙는다.
	 *
	 * [여기는 위치가 아니라 대상을 보낸다]
	 *   Multicast_SpentEffect 는 곧 사라지는 장비라 위치를 실어 보냈지만, 이쪽 대상은
	 *   플레이어 캐릭터다 — 항상 복제돼 있고 계속 갱신되므로 붙여도 어긋나지 않는다.
	 *   붙이는 편이 낫다: 일어나는 동안 캐릭터가 움직이는데, 자리에 띄우면 연출만 바닥에 남는다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_RescueEffect(class ABaseCharacter* Rescued);

private:
	/** 이 캐릭터가 다운돼 있으면 일으킨다. 성공하면 true */
	bool TryRescue(ABaseCharacter* Target);

	/**
	 * 맞은 순간의 상대. OnDeployed 에서 받아 OnActivated 에서 쓴다.
	 *
	 * 약참조인 것은 두 호출 사이에 대상이 사라질 수 있어서다 — 접속 종료나 파괴 모두
	 * 같은 프레임에 일어날 수 있고, 원시 포인터로 들고 있으면 그때 크래시한다.
	 */
	TWeakObjectPtr<AActor> DeployedHitActor;
};
