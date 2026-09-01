#pragma once

#include "CoreMinimal.h"

/**
 * 노획물 물리·운반·파손 전용 로그 카테고리.
 *
 * [왜 헤더에 EXTERN 인가] 기본은 .cpp 안의 DEFINE_LOG_CATEGORY_STATIC 이다 (문서 07 테스트).
 *   헤더에 노출하면 아무나 이 카테고리로 찍어서 필터의 의미가 사라지기 때문이다.
 *   다만 노획물은 액터 하나와 컴포넌트 둘이 같은 사건을 나눠 처리해서,
 *   카테고리가 갈리면 "물건 하나에 무슨 일이 있었나" 를 한 필터로 따라갈 수 없다.
 *   AI 파트가 BT 노드 7개 때문에 LogGuardAI 를 EXTERN 으로 둔 것과 같은 이유다.
 *
 * [무엇을 찍나] 이 시스템은 실패해도 예외가 나지 않는다.
 *   소켓이 없거나, 컴포넌트가 엉뚱한 액터에 붙었거나, 권위가 없어서 되돌아가면
 *   "물건이 안 잡힌다" 로만 드러난다. 조용히 return 하는 지점마다 이유를 남긴다.
 *
 * 충돌 디버그(화면 메시지·구)는 별개다 — ALootBase::bShowImpactDebug 로 켠다.
 * 이쪽은 액터별 스위치라 레벨에 수십 개가 깔려도 하나만 볼 수 있다.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogLoot, Log, All);
