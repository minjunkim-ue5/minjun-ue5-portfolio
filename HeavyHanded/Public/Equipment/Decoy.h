#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentBase.h"
#include "Decoy.generated.h"

/**
 * 미끼(개껌·소음탄). 던져 놓으면 그 자리에서 소리를 내 경비를 끌어온다.
 * (기획서 7장 — $6,000 / 경비 20초 유인)
 *
 * [베이스와 다른 것이 값뿐이다]
 *   던진다 / 떨어진다 / 발동한다 / 20초 뒤 끝난다 는 전부 AEquipmentBase 의 상태 기계다.
 *   여기서 하는 것은 생성자에서 값을 정하는 것뿐이고, 그래서 함수가 하나도 없다.
 *   점착 폭탄과 같은 구조인데 저쪽은 폭발 때 금고에 알릴 것이 있어서 OnActivated 가 있다.
 *
 * [경비를 부르는 코드가 없다]
 *   소리만 내면 경비가 온다. 이미 그 경로가 있기 때문이다 —
 *     UNoiseSubsystem::ReportNoise
 *       -> UAISense_Hearing::ReportNoiseEvent        (NoiseSubsystem.cpp)
 *       -> AGuardAIController::OnTargetPerceptionUpdated 의 Hearing 분기
 *       -> 인지 게이지 -> Investigate -> BTTask_MoveToInvestigate 가 그 지점으로 이동
 *
 *   경비 AI 에 "이 지점으로 와라" 같은 함수를 만들어 달라고 하지 않은 이유가 이것이다.
 *   그런 경로를 열면 소음 시스템을 우회하는 두 번째 길이 생기고, 그쪽만 벽 너머로도
 *   들리는 식으로 규칙이 갈라진다.
 *
 * [얼마나 멀리까지 끌어오는가는 여기서 정하지 않는다]
 *   반경·크기·경계도 기여는 DT_NoiseProfiles 의 Noise.Equipment.Decoy 행이 정한다.
 *   내가 정하는 것은 "언제 무슨 태그로 소리가 나는가" 까지다. (규약 문서 04 데이터)
 *   유인력을 조절하려면 ActiveNoiseInterval(발행 주기)을 만지고,
 *   경계도를 조절하려면 그 행의 AlertDelta 를 만진다.
 */
UCLASS()
class HEAVYHANDED_API ADecoy : public AEquipmentBase
{
	GENERATED_BODY()

public:
	ADecoy();
};
