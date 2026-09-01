#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VaultDoor.generated.h"

class UArrowComponent;
class UNiagaraSystem;
class UStaticMeshComponent;
class USoundBase;

/**
 * 대형 금고의 문. 점착 폭탄으로만 열린다. (기획서 5장 — 대형 금고 / 7장 — 점착 폭탄 $25,000)
 *
 * [금고 전체가 아니라 문만 액터다]
 *   금고는 맵의 한 공간이고 벽·바닥·천장은 레벨 지오메트리다(환경 파트).
 *   플레이 중에 변하는 것은 입구를 막고 있는 이 문 하나뿐이라, 몸통까지 액터로 만들면
 *   아무 일도 안 하는 컴포넌트만 늘어난다.
 *
 *   내용물도 마찬가지다 — 금고 안에 노획물을 미리 배치해 두고 이 문이 막는다.
 *   여는 순간에 스폰하지 않는다. 스폰하면 무엇을 넣을지가 코드로 넘어와서
 *   레벨 디자이너가 장소마다 다른 구성을 짤 수 없고, 중량형·파손형 같은
 *   기존 노획물 기능을 다시 이어 줘야 한다.
 *
 * [사라지는 것은 뚜껑뿐이다]
 *   프레임(볼트 박힌 사각 판)은 벽의 일부라 그대로 남고, 가운데 원형 뚜껑만 날아가
 *   그 자리가 뚫린 입구가 된다. 문짝 전체를 지우면 벽에 사각형 구멍이 뚫려
 *   금고가 아니라 부서진 벽으로 보인다.
 *
 *   ⚠️ 그래서 **프레임 메시의 콜리전에 가운데 구멍이 있어야 한다.** 단순 박스 콜리전이면
 *   뚜껑을 지워도 사람이 못 지나간다 — 화면에는 뚫려 있는데 벽에 막히는 상태가 된다.
 *   BeginPlay 에서 검사할 방법이 없어서(콜리전 모양을 액터가 알 수 없다) 경고가 안 뜬다.
 *
 * [뚜껑에 붙은 것은 같이 사라진다]
 *   핸들·손잡이·경첩을 코드에 박지 않는다. DoorLid 아래에 붙인 컴포넌트는 무엇이든
 *   함께 지워진다. 장식이 늘어도 이 클래스는 그대로다.
 *
 * [판정은 문이 한다]
 *   폭탄은 "여기서 이만큼 터졌다" 만 알리고(TryBreach), 그 폭발이 나를 여는지는
 *   문이 정한다. 노획물의 ICarryable 과 같은 원칙이다 — 요청은 밖에서 오고 판정은 물건이 한다.
 *
 * [열리는 것은 되돌아가지 않는다]
 *   bIsBreached 는 한 방향이다. 다시 닫히는 문이 필요해지면 그건 다른 물건이다.
 *
 * 서버 권위 + 복제. 판정은 서버가 하고 연출은 각 머신이 각자 돌린다.
 */
UCLASS(Blueprintable)
class HEAVYHANDED_API AVaultDoor : public AActor
{
	GENERATED_BODY()

public:
	AVaultDoor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 이 폭발이 문을 여는가. (서버 전용)
	 *
	 * 두 가지 중 하나면 열린다.
	 *   1. 폭발물이 뚜껑(또는 뚜껑에 붙은 핸들)에 직접 붙어 있다 — 정상적인 사용법이다
	 *   2. 뚜껑 표면에서 BreachRadius 안에서 터졌다 — 문틀에 살짝 빗맞은 경우를 구제한다
	 *
	 * 2번이 없으면 "분명히 문에 던졌는데 안 열린다" 가 나온다. 폭탄이 뚜껑 가장자리를
	 * 스치고 프레임에 붙는 일이 실제로 흔하고, 플레이어는 그 차이를 볼 수 없다.
	 *
	 * 거리는 **액터 원점이 아니라 뚜껑 표면** 기준이다. 액터 원점은 프레임 한가운데라
	 * 뚜껑이 한쪽으로 치우쳐 붙어 있으면 판정 범위가 엉뚱한 곳에 잡힌다.
	 *
	 * 프레임 구석에 붙인 폭탄은 열지 못한다 — 뚜껑을 노려야 한다는 것이 이 장비의 규칙이다.
	 *
	 * @param Explosive  터진 폭발물. 붙어 있는지 보려고 받는다. 없어도 된다
	 * @return 이번 호출로 열렸으면 true. 이미 열려 있었거나 범위 밖이면 false
	 */
	bool TryBreach(const AActor* Explosive, const FVector& BlastOrigin, float BlastRadius);

	UFUNCTION(BlueprintPure, Category = "Vault")
	bool IsBreached() const { return bIsBreached; }

protected:
	virtual void BeginPlay() override;

	/**
	 * 볼트 박힌 사각 판. 루트이고 끝까지 남는다.
	 *
	 * 레벨의 SM_Env_VaultDoor_Frame_01 자리다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vault")
	TObjectPtr<UStaticMeshComponent> DoorFrame;

	/**
	 * 가운데 원형 뚜껑. 폭발하면 이것과 그 아래 붙은 것이 전부 사라진다.
	 *
	 * 레벨의 SM_Env_VaultDoor_Lid_01 자리이고, 핸들 2개는 BP 에서 이 아래에 붙인다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vault")
	TObjectPtr<UStaticMeshComponent> DoorLid;

	/**
	 * 파편이 날아갈 방향. 이 화살표의 정면(+X)이 곧 '금고 바깥' 이다.
	 *
	 * [처음에는 폭탄 위치에서 방향을 뽑았고, 그게 틀렸다]
	 *   뚜껑 중심에서 폭탄으로 향하는 벡터를 썼다. 폭탄이 뚜껑 한가운데 붙으면 맞지만,
	 *   금고 문은 넓어서 보통 중심에서 벗어나 붙는다. 그러면 그 벡터가 바깥이 아니라
	 *   옆으로 눕고, 뚜껑이 넓고 얇을수록 심해진다 — 파편이 문을 따라 옆으로 쓸려 나갔다.
	 *
	 *   방향은 폭탄이 아니라 **문의 성질**이다. 어디에 붙여 터뜨리든 금고는 같은 쪽으로 열린다.
	 *
	 * [왜 프로퍼티가 아니라 컴포넌트인가]
	 *   메시가 어느 축을 정면으로 삼았는지는 만든 사람마다 달라서 코드가 알 수 없다.
	 *   숫자로 넣게 하면 (1,0,0) 부터 넣어 보고 틀리면 다시 켜서 확인하게 된다.
	 *   화살표는 뷰포트에 그대로 보이므로, 금고 밖을 향할 때까지 돌리면 그걸로 끝이다.
	 *   (게임에서는 안 보인다 — 에디터 전용 표시다)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vault")
	TObjectPtr<UArrowComponent> BreachArrow;

	/** 뚜껑에 직접 붙지 않은 폭발을 구제하는 거리. 뚜껑 표면 기준이다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault|Breach",
		meta = (ClampMin = "0.0", Units = "cm"))
	float BreachRadius = 150.f;

	// ---- 연출 (BP 는 에셋만 고른다) ----

	/**
	 * 문이 부서지는 연출. 금속 파편이 날고 잔해 연기가 남는다.
	 *
	 * [폭탄의 SpentEffect 와 역할이 갈린다]
	 *   화염·섬광은 폭탄 몫이다. 벽에 붙여 터뜨려도 폭발로 보여야 하므로 폭탄이 어디서
	 *   터지든 나온다. 여기 있는 것은 **문이 부서질 때만** 나오는 것 — 뚜껑에서 떨어져
	 *   나온 금속 조각과 그 뒤에 남는 먼지다.
	 *
	 *   같은 화염을 양쪽에 넣으면 한 자리에서 두 번 터져 보인다. 화염과 파편은 다르다.
	 *
	 * 연기가 뚜껑이 사라지는 순간을 가리는 역할도 겸한다 — DoorHideDelay 참고.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault|Visual")
	TObjectPtr<UNiagaraSystem> BreachEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault|Visual",
		meta = (ClampMin = "0.1"))
	float BreachEffectScale = 1.f;

	/**
	 * 폭발 지점을 뚜껑 중심에서 바깥으로 얼마나 밀어낼지.
	 *
	 * 기준점은 뚜껑의 **바운즈 중심**이다 (피벗이 아니다 — 피벗은 메시마다 엉뚱한 데 있다).
	 * 그 중심은 두꺼운 문의 한가운데라 문 안쪽이고, 0 으로 두면 연기 절반이 문에 묻힌다.
	 *
	 * 뚜껑 두께의 절반보다 조금 크게 잡으면 표면에서 터지는 것으로 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault|Visual",
		meta = (Units = "cm"))
	float BreachEffectForwardOffset = 30.f;

	/** 뚜껑이 떨어져 나가는 소리. 폭발음은 폭탄 쪽(SpentSound)이다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault|Visual")
	TObjectPtr<USoundBase> BreachSound;

	/**
	 * 연기가 피어오르고 이만큼 뒤에 뚜껑을 지운다.
	 *
	 * 0 이면 연기와 같은 프레임에 사라져서 뚜껑이 팍 하고 없어지는 것이 그대로 보인다.
	 * 연기가 화면을 덮을 만큼만 기다렸다가 그 아래에서 지운다. 연기가 걷히면
	 * 뚫린 입구가 드러나 있다 — 뚜껑이 사라지는 장면 자체는 아무도 보지 못한다.
	 *
	 * 연기가 얼마나 오래 남는지는 Niagara 에셋이 정한다. 여기서 만지지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault|Visual",
		meta = (ClampMin = "0.0", Units = "s"))
	float DoorHideDelay = 0.15f;

private:
	UFUNCTION()
	void OnRep_bIsBreached();

	/** 연기·소리를 내고 뚜껑 지우기를 예약한다. 모든 머신에서 실행된다 */
	void ApplyBreach();

	/** 뚜껑과 그 아래 붙은 것을 시야·콜리전·내비게이션에서 뺀다. 모든 머신에서 실행된다 */
	void HideLid();

	/**
	 * 파편이 날아갈 월드 방향. BreachArrow 의 정면이다.
	 *
	 * 복제하지 않는다 — 화살표의 트랜스폼은 모든 머신이 똑같이 갖고 있어서
	 * 각자 계산하면 같은 답이 나온다. 보낼 이유가 없다.
	 *
	 * Niagara 쪽은 **Local Space 를 켜고 콘 축을 (1,0,0)** 으로 두면 된다.
	 * 이 방향이 스폰 회전의 +X 가 되도록 맞춰서 스폰한다.
	 */
	FVector GetBreachDirection() const;

	/**
	 * 열렸는가. 서버가 정하고 복제된다.
	 *
	 * 서버에서 직접 대입하면 RepNotify 가 안 불리므로 OnRep 을 손으로 부른다.
	 * 리슨 서버의 호스트에서만 뚜껑이 안 사라지는 증상이 그것이다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_bIsBreached, VisibleInstanceOnly, Category = "Vault")
	bool bIsBreached = false;

	FTimerHandle HideTimer;
};
