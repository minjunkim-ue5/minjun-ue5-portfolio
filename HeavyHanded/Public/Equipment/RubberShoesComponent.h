#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"   // FGameplayTag — 값으로 보유
#include "RubberShoesComponent.generated.h"

class UNoiseEmitterComponent;

/**
 * 고무창 신발. 붙어 있는 동안 소유자가 내는 이동 소음이 절반이 된다.
 * (기획서 7장 — $8,000 / 이동 소음 -50%. 구매 즉시 자동 장착)
 *
 * [왜 액터가 아닌가]
 *   효과에 위치가 없다. 던지지도 집지도 않고, 스테이지에 물건으로 나오지도 않는다.
 *   그리고 AddModifier 가 돌려주는 핸들을 누군가 들고 있다가 반드시 지워야 하는데,
 *   자기를 파괴하는 액터는 그 핸들을 들고 있을 수 없다. 사람에게 붙어 있는 것이 맞는 모양이다.
 *   상점 진열대는 별개의 액터다(AShopDisplay) — 그쪽이 메시를 갖는다.
 *
 * [소음 파트가 이걸 위해 API 를 미리 만들어 뒀다]
 *   FNoiseModifier 의 Multiplier 주석에 "고무창 신발 0.5, 완충 장갑 0.3" 이 그대로 적혀 있다.
 *   UNoiseEmitterComponent::AddModifier / RemoveModifier 로 등록과 해제만 하면 되고,
 *   소음 코드를 고칠 것이 없다. 이 컴포넌트가 그 API 의 첫 사용자다.
 *
 * [파이프라인에서 어디에 끼어드는가]
 *   State.Sprinting -> ABaseCharacter::EmitSprintNoise -> ReportTaggedNoise(Noise.Player.Run)
 *     -> UNoiseEmitterComponent::EmitThroughFilter   ★ 여기서 모디파이어가 곱해진다
 *     -> 스팸 필터 -> UNoiseSubsystem::ReportNoise -> 경비(AISense_Hearing) / UAlertComponent
 *
 *   맨 앞이라 경비가 듣는 반경과 경계도 게이지에 **둘 다** 걸린다. 다만 비율이 다르다 —
 *   경계도는 Loudness 에 그대로 비례해 정확히 절반이 되지만, 반경은
 *   Radius * Lerp(0.6, 1.0, Loudness) 라서 0.8 배까지만 줄어든다(NoiseSubsystem.cpp).
 *   "반경도 절반" 을 원하면 그 하한 0.6 을 소음 파트에 조정 요청해야 한다.
 *
 * [걷기 소음은 줄일 것이 없다]
 *   기획서 3장에서 걷기 · 앉아서 이동은 소음 0 이고, 실제로 걷기 소음을 발행하는 코드가 없다.
 *   Noise.Player.Land 는 태그와 DT 행이 있는데 아직 아무도 발행하지 않는다(착지 소음 미착수).
 *   그래서 AffectedNoiseRootTag 를 Noise.Player 로 두고 하위 전부를 걸어 둔다 —
 *   누가 착지 소음을 붙이는 순간 신발이 자동으로 그것까지 줄인다.
 */
UCLASS(ClassGroup = (HeavyHanded), meta = (BlueprintSpawnableComponent))
class HEAVYHANDED_API URubberShoesComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URubberShoesComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 소리 크기 배율. 0.5 가 "소음 -50%" 다.
	 * 이 값이 곱해지는 대상은 Loudness(0~1)이고, 그것이 경계도 증가분과 청취 반경을 함께 정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoes",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoiseMultiplier = 0.5f;

	/**
	 * 어느 소음을 줄이는가. **하위 태그까지 걸린다.**
	 * 비워 두면 Noise.Player 로 채운다 (생성자에서 태그를 조회하면 태그가 아직 로드되지
	 * 않았을 수 있어 BeginPlay 에서 채운다).
	 *
	 * ⚠ 여기를 무효 태그로 만들면 쿼리가 비게 되고, EmitThroughFilter 는 빈 쿼리를
	 * "전부 적용" 으로 읽는다 — 노획물 충돌음까지 반이 된다. 그 경우 경고를 남긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoes")
	FGameplayTag AffectedNoiseRootTag;

private:
	/**
	 * AddModifier 가 돌려준 핸들. EndPlay 에서 반드시 지운다 —
	 * 남기면 신발을 잃은 뒤에도 그 사람 소음이 계속 반으로 나간다.
	 */
	FGuid ModifierHandle;

	/**
	 * 핸들을 등록한 대상. 약참조인 것은 폰이 먼저 파괴되는 순서에서도 안전해야 하기 때문이다.
	 * ABaseCharacter::NoiseEmitter 는 protected 라 FindComponentByClass 로 잡는다 —
	 * 덕분에 플레이어 파트를 고칠 필요가 없었다.
	 */
	TWeakObjectPtr<UNoiseEmitterComponent> RegisteredEmitter;
};
