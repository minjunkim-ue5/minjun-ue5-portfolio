#pragma once

#include "CoreMinimal.h"
#include "Core/HeistPhase.h"        // EHeistPhaseReason — 델리게이트 시그니처에 값으로 들어가 전방 선언 불가
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"   // FGameplayTag — TMap 키로 값 보유
#include "EquipmentSpawnZone.generated.h"

class USceneComponent;

/**
 * 작업 레벨의 장비 스폰 구역. **은신처에서 산 목록의 숫자를 실제 물건으로 바꾼다.**
 * 레벨당 하나이고, 전부 서버에서만 돈다.
 *
 * [이것이 없으면 상점이 아무 일도 하지 않는다]
 *   은신처에서 미끼를 3개 사면 URunProgressSubsystem 에 { Equipment.Decoy: 3 } 이라는
 *   숫자만 남는다. 저택에 도착해서 그 숫자를 훑어 액터를 만드는 코드가 여기다.
 *
 * [준비 시간에 돈다]
 *   Phase.Prep 이 "구매 장비 회수" 페이즈다(기획서 2장 / Phase.ini). BeginPlay 가 아니라
 *   페이즈 진입에 맞추는 이유는 그 시점에 플레이어 폰이 전부 만들어져 있기 때문이다 —
 *   HeistGameMode 는 접속 대기가 끝난 뒤에 Phase.Prep 으로 들어간다.
 *
 * [태그 -> 클래스 매핑이 여기 있는 이유]
 *   AShopDisplay 는 자기가 파는 것이 어떤 액터인지 모른다. 태그만 알고 목록에 넣는다.
 *   진열대에 클래스를 두면 은신처의 진열대가 저택의 물건을 알게 되고, 진열대 BP 마다
 *   클래스를 중복해서 꽂아야 한다. 매핑은 한 곳에만 있어야 하고 그 자리가 여기다.
 *
 * [선착순이다]
 *   4명이 미끼 3개를 놓고 나눠 갖는 것 자체가 협동의 압력이다. 사람별로 나눠 주지 않고
 *   바닥에 놓는다 (Equipment.ini 주석 — "선착순이다").
 *
 * [개인 장비도 여기서 다시 붙인다]
 *   고무창 신발은 폰에 붙는 컴포넌트라 ServerTravel 로 폰이 새로 만들어지면 사라진다.
 *   URunProgressSubsystem 이 "누가 무엇을 갖고 있는가" 를 캠페인 단위로 들고 있으므로,
 *   스테이지 시작에 그것을 훑어 컴포넌트를 다시 붙인다.
 *
 *   물건을 만드는 것과 같은 일이다 — 산 것을 실체화하는 것이고, 실체화 방식만
 *   액터냐 컴포넌트냐로 갈린다. 그래서 두 경로가 같은 자리에 있다.
 */
UCLASS()
class HEAVYHANDED_API AEquipmentSpawnZone : public AActor
{
	GENERATED_BODY()

public:
	AEquipmentSpawnZone();

	/** 이 레벨의 스폰 구역. 레벨당 하나라는 계약 위에서만 성립한다 (AVanZone::Get 과 같은 방식) */
	UFUNCTION(BlueprintPure, Category = "Equipment|Spawn", meta = (WorldContext = "WorldContext"))
	static AEquipmentSpawnZone* Get(const UObject* WorldContext);

	/**
	 * 구매 목록을 훑어 아이템을 만들고 목록을 비운다. (서버 전용)
	 * 보통은 Phase.Prep 진입이 부른다. 두 번 불려도 두 번 스폰하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment|Spawn")
	void SpawnPurchasedEquipment();

#if !UE_BUILD_SHIPPING
	/**
	 * [테스트용] 이미 스폰했더라도 다시 돌린다. 콘솔 명령 hh.Equip.SpawnNow 가 부른다.
	 *
	 * 정식 경로에 bForce 인자를 두지 않는 이유는 그것이 "두 번 스폰해도 된다" 는 뜻으로
	 * 읽히기 때문이다. 두 번 나오면 안 되는 것이 이 구역의 핵심 규칙이다.
	 */
	void DebugForceSpawn();
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 이 구역이 담당하는 진입점(Entry.*). 플레이어가 그곳으로 들어왔을 때만 스폰한다.
	 *
	 * [왜 구역을 진입점마다 두는가]
	 *   저택의 세 진입점은 성격이 다르다 — 정문은 노출이 심하고, 뒷골목은 통로가 좁고,
	 *   지하 주차장은 동선이 길다. 장비를 쌓아 둘 자리도 그에 맞게 달라야 해서,
	 *   구역 하나가 확정된 진입점을 찾아가는 방식보다 **각 자리에 하나씩 놓는 편**이 낫다.
	 *   진입점 바로 앞이 벽이나 계단인 경우를 코드가 알 방법이 없다.
	 *
	 * **비워 두면 진입점과 무관하게 항상 스폰한다.** 진입점이 없는 레벨과,
	 * 구역을 하나만 두던 기존 배치가 그대로 동작하라고 그렇게 두었다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn")
	FGameplayTag EntryTag;

	/**
	 * 이 구역이 이번 판의 진입점을 담당하는가. (서버 전용)
	 *
	 * 게임모드의 폴백과 판단을 맞춘다 — 고른 진입점이 이 레벨에 없으면 게임모드는
	 * 기본/첫 번째 진입점으로 떨어뜨리는데, 구역이 '고른 값' 만 보면 플레이어는 1번에서
	 * 시작했는데 장비는 아무 데도 안 나오는 상태가 된다.
	 */
	bool IsEntryMatched() const;

	/** 액터의 기준점. 앵커를 하나도 지정하지 않았을 때 이 위치에 원형으로 놓는다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn")
	TObjectPtr<USceneComponent> ZoneRoot;

	/**
	 * 장비 태그 -> 만들 액터 클래스.
	 *
	 * TSubclassOf<AEquipmentBase> 가 아니라 AActor 인 것은 대차(Equipment.HandCart)가
	 * AHandCart 이고 AEquipmentBase 를 상속하지 않기 때문이다 — 저쪽은 던지는 물건이고
	 * 카트는 끌고 다니는 물건이라 상태 기계가 다르다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn")
	TMap<FGameplayTag, TSubclassOf<AActor>> EquipmentClasses;

	/**
	 * 장비 태그 -> 플레이어 폰에 붙일 컴포넌트. 예: Equipment.RubberShoes -> URubberShoesComponent.
	 *
	 * 위 EquipmentClasses 와 나뉘어 있는 것은 실체화 방식이 다르기 때문이다 —
	 * 저쪽은 바닥에 놓이는 액터고 이쪽은 사람에게 붙는 컴포넌트다.
	 * 여기 없는 태그는 그냥 지나간다. 그래서 1인 1회 소모품처럼 컴포넌트가 없는 개인 장비도
	 * 보유 기록만 남고 아무 일도 일어나지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn")
	TMap<FGameplayTag, TSubclassOf<UActorComponent>> PersonalEquipmentClasses;

	/**
	 * 배치 지점. 레벨의 TargetPoint 들을 끌어다 놓는다 (AGuardSpawner::PatrolPoints 와 같은 방식).
	 * 물건이 앵커보다 많으면 앵커를 다시 돌면서 조금씩 비켜 놓는다.
	 * 비워 두면 이 액터 위치에 원형으로 놓는다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Equipment|Spawn")
	TArray<TObjectPtr<AActor>> SpawnAnchors;

	/**
	 * 앵커보다 높은 곳에서 떨어뜨린다. 장비는 물리를 켜고 스폰되므로 바닥에 정확히
	 * 붙여 놓으면 첫 프레임에 침투 상태로 시작해 튄다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn",
		meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnHeightOffset = 30.f;

	/** 같은 앵커를 다시 쓸 때 비켜 놓는 거리. 겹쳐 스폰하면 물리가 서로 밀어낸다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn",
		meta = (ClampMin = "0.0", Units = "cm"))
	float RepeatOffset = 40.f;

	/** 앵커가 없을 때 쓰는 원 반지름 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn",
		meta = (ClampMin = "0.0", Units = "cm"))
	float FallbackRingRadius = 100.f;

	/** 무엇을 몇 개 어디에 놓았는지 화면과 로그에 남긴다. (테스트용) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment|Spawn|Debug")
	bool bShowSpawnDebug = false;

	/** AHeistGameState::OnPhaseChanged 구독. Phase.Prep 에 들어오면 스폰한다 */
	UFUNCTION()
	void HandlePhaseChanged(FGameplayTag NewPhase, FGameplayTag OldPhase, EHeistPhaseReason Reason);

private:
	/** 아이템 하나를 만든다. 실패하면 nullptr 이고 사유를 로그로 남긴다 */
	AActor* SpawnOne(const FGameplayTag& EquipmentTag, UClass* ActorClass, int32 PlacementIndex);

	/**
	 * 접속한 사람들에게 각자의 개인 장비를 다시 붙인다. (서버 전용)
	 * 구매 목록과 달리 **소비하지 않는다** — 캠페인 내내 유지되는 소유물이라
	 * 스테이지마다 다시 붙여야 한다.
	 */
	void ApplyPersonalEquipment();

	/** 폰 하나에 컴포넌트를 붙인다. 이미 갖고 있으면 아무 일도 하지 않는다 */
	void GrantComponentTo(APawn* Pawn, const FGameplayTag& EquipmentTag, UClass* ComponentClass);

	/** PlacementIndex 번째 물건을 놓을 자리. 앵커를 순환하고 한 바퀴마다 비켜 놓는다 */
	FTransform GetPlacementTransform(int32 PlacementIndex) const;

	/** 레벨에 이 액터가 둘 이상이면 경고한다 (AVanZone::WarnIfDuplicateZone 과 같은 이유) */
	void WarnIfDuplicateZone() const;

	void ShowSpawnDebug(const FString& Message, const FVector& Location, const FColor& Color) const;

	/**
	 * 이미 스폰했는가. 페이즈가 두 번 들어오거나 BeginPlay 와 델리게이트가 겹쳐도
	 * 두 번 만들지 않게 한다. 구매 목록은 ConsumePurchasedEquipment 가 비우지만
	 * 그것만 믿으면 목록이 빈 상태로 다시 불릴 때 경고가 시끄러워진다.
	 */
	bool bHasSpawned = false;
};
