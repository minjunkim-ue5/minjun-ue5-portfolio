#include "Equipment/RubberShoesComponent.h"

#include "Loot/LootLog.h"                  // LogLoot — 장비 계열이 다 이걸 쓴다 (EquipmentBase.cpp)
#include "Noise/NoiseEmitterComponent.h"
#include "Noise/NoiseTypes.h"              // FNoiseModifier

namespace
{
	/**
	 * 줄일 소음의 뿌리. Noise.* 는 소음 파트(지성인) 소유라 네이티브 태그로 올리지 않고
	 * 문자열로 조회한다 — ABaseCharacter::EmitSprintNoise 도 같은 이유로 같은 방식이다.
	 */
	static const FName PlayerNoiseRootTagName(TEXT("Noise.Player"));
}

URubberShoesComponent::URubberShoesComponent()
{
	// 하는 일이 등록과 해제뿐이라 틱이 필요 없다.
	PrimaryComponentTick.bCanEverTick = false;
}

void URubberShoesComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return;
	}

	// 소음 발행이 서버 전용이라 모디파이어도 서버에만 있으면 된다.
	// 런타임에 만든 컴포넌트는 애초에 복제되지 않으므로 클라이언트에는 이것이 존재하지 않는다.
	// HUD 에 신발 아이콘을 띄울 때가 되면 별도의 복제 경로가 필요해진다.
	if (!Owner->HasAuthority())
	{
		return;
	}

	UNoiseEmitterComponent* Emitter = Owner->FindComponentByClass<UNoiseEmitterComponent>();
	if (!Emitter)
	{
		UE_LOG(LogLoot, Warning,
			TEXT("%s 에 UNoiseEmitterComponent 가 없다. 고무창 신발이 아무것도 하지 않는다."),
			*GetNameSafe(Owner));
		return;
	}

	// 생성자에서 조회하지 않는 이유: CDO 가 만들어지는 시점에는 태그가 아직 로드되지
	// 않았을 수 있다. BP 에서 다른 뿌리를 지정했으면 그것을 그대로 쓴다.
	if (!AffectedNoiseRootTag.IsValid())
	{
		AffectedNoiseRootTag = FGameplayTag::RequestGameplayTag(PlayerNoiseRootTagName, /*ErrorIfNotFound=*/false);
	}

	FNoiseModifier Modifier;
	Modifier.Multiplier = NoiseMultiplier;

	if (AffectedNoiseRootTag.IsValid())
	{
		// MatchAnyTags 는 하위 태그까지 맞춘다 — Noise.Player 하나로 Run 과 Land 가 함께 걸린다.
		Modifier.AffectedTags =
			FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(AffectedNoiseRootTag));
	}
	else
	{
		// 쿼리를 비워 두면 EmitThroughFilter 가 "전부 적용" 으로 읽는다. 이 사람이 내는
		// 노획물 충돌음까지 반이 되므로 의도한 상태가 아니다. 조용히 넘어가지 않는다.
		UE_LOG(LogLoot, Warning,
			TEXT("%s: %s 태그를 찾을 수 없어 소유자의 모든 소음이 %.2f 배가 된다."),
			*GetName(), *PlayerNoiseRootTagName.ToString(), NoiseMultiplier);
	}

	ModifierHandle = Emitter->AddModifier(Modifier);
	RegisteredEmitter = Emitter;

	UE_LOG(LogLoot, Log, TEXT("고무창 신발 장착: %s — %s 소음 %.2f 배"),
		*GetNameSafe(Owner),
		AffectedNoiseRootTag.IsValid() ? *AffectedNoiseRootTag.ToString() : TEXT("전체"),
		NoiseMultiplier);
}

void URubberShoesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 반드시 지운다. 남기면 신발을 잃은 뒤에도 그 사람 소음이 계속 반으로 나간다.
	if (ModifierHandle.IsValid())
	{
		if (UNoiseEmitterComponent* Emitter = RegisteredEmitter.Get())
		{
			Emitter->RemoveModifier(ModifierHandle);
		}

		ModifierHandle.Invalidate();
		RegisteredEmitter.Reset();
	}

	Super::EndPlay(EndPlayReason);
}
