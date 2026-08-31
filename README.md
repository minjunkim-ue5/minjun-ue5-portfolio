# 김민준 — Game Programmer Portfolio (Source Code)

Unreal Engine 5 / C++ 팀 프로젝트에서 본인이 직접 작성한 소스코드를 발췌한 저장소입니다.

> 팀 프로젝트 특성상 에셋(.uasset, .umap), 상용 마켓플레이스 에셋, 팀원이 작성한 코드는 저작권 및 라이선스 문제로 제외했습니다. 따라서 이 저장소는 빌드용이 아닌 **코드 열람용**입니다.

## Project_Dwarf_Extraction

4인 협동 1인칭 호러 게임 (Lethal Company 스타일)

| 항목 | 내용 |
|---|---|
| 엔진 | Unreal Engine 5.4.4 |
| 기간 | 2026.07.02 ~ 2026.08.04 (5주) |
| 팀 구성 | 프로그래머 4인 |
| 담당 | Player 파트 단독 |
| 기여 | 커밋 52회 / C++ 약 4,700줄 작성 |
| 기술 | GAS, Enhanced Input, Replication(Server RPC / Multicast / OnRep) |

### 구현

- **다운 / 부활 시스템** — 체력 0 시 다운 진입 → 팀원이 3초 홀드로 부활 → 미부활 시 사망
- **상호작용 시스템** — `IInteractableInterface` 기반. 아이템 · 문 · 다운된 플레이어가 동일한 입력 하나로 동작
- **GAS 기반 능력** — `GA_Sprint`(스태미나 소모), `GA_Interact`(서버 권위 트레이스 + 홀드 판정)
- **캐릭터 색상 커스터마이징** — `PlayerColorSet` DataAsset + Server RPC + `OnRep` 복제
- **관전 시스템** — 사망 시 `PlayerSpectatorPawn` 전환, 생존 팀원 순환 관전

### 설계 메모

- ASC는 `PlayerCharacter`에 배치했습니다. 본 게임은 다운 → 부활로 상태만 전이하고 액터가 파괴·리스폰되지 않으므로, PlayerState 배치의 주 이점인 리스폰 시 어트리뷰트 유지가 적용되지 않는다고 판단했습니다.
- 로직·상태는 C++/GAS, 데이터·연출은 DataAsset/Blueprint로 경계를 나눴습니다. `State.Downed` 태그 하나로 Sprint · Jump · Interact 어빌리티가 일괄 차단됩니다.

### 주요 파일

| 파일 | 설명 |
|---|---|
| `Private/Characters/PlayerCharacter.cpp` | 이동 · 다운/부활 · 복제 · 피격 리액션의 중심 |
| `Private/Characters/Abilities/GA_Interact.cpp` | 상호작용 어빌리티. 서버 권위 트레이스 + 홀드 취소 처리 |
| `Public/Characters/PlayerAttributeSet.h` | GAS 어트리뷰트 (체력 · 스태미나) |
| `Public/Characters/PlayerColorSet.h` | 색상 데이터 애셋 |
| `Public/Characters/InteractableInterface.h` | 파트 간 계약. 아이템 / 문 / 플레이어가 모두 구현 |

`Systems/Interaction`, `Systems/Extraction`은 공동 작업 파일입니다. 본인은 상호작용 트레이스 로직과 문 개별 제어 부분을 담당했습니다.

## 대표 트러블슈팅 — 서버 권위 구조의 4단 연쇄 버그

"호스트는 되는데 클라이언트만 안 된다"는 동일한 증상이 4회 반복되었고, 실패 지점은 매번 달랐습니다.

| # | 증상 | 원인 | 해결 |
|---|---|---|---|
| 1 | 클라이언트 상호작용 무반응 | 카메라 컴포넌트 회전은 렌더링하는 머신에서만 갱신되어 서버 트레이스가 허공으로 나감 | `Controller->GetPlayerViewPoint()` 로 변경 |
| 2 | 다운된 팀원만 트레이스 통과 | 캡슐 기본 프리셋이 Visibility 채널을 Ignore | 트레이스 채널을 `ECC_Pawn` 으로 변경 |
| 3 | 클라이언트 → 서버 부활 실패 | 어빌리티가 클라이언트 로컬에서만 활성화 | `NetExecutionPolicy = ServerOnly` |
| 4 | 클라이언트가 키를 떼도 부활됨 | 취소 요청이 로컬 ASC로만 전달 (인스턴스는 서버에 존재) | 취소 전용 Server RPC 추가 |

2번은 `ECC_Visibility`와 `ECC_Pawn` 두 채널을 동시에 트레이스해 로그를 비교하는 임시 진단 코드로 원인을 확정했습니다. 채널 변경 후에는 팀원 파트인 아이템 획득의 회귀 테스트를 거쳐 반영했습니다.

이후 색상 커스터마이징과 피격 리액션 구현 시에는 같은 유형의 버그가 발생하지 않았습니다.

## HEAVY HANDED (진행 중)

협동 잠입 하이스트 게임. 물리 충돌이 소음을 발생시켜 경보 게이지를 채우는 구조.

| 항목 | 내용 |
|---|---|
| 팀 구성 | 6인 |
| 담당 | 노획물 / 물리 파트 |
| 설계 | 아이템은 물리적 사실만 브로드캐스트(`FLootImpactEvent`)하고, 소음 해석은 별도 시스템이 담당 |

## Contact

- Email: kimminjun020220@gmail.com
