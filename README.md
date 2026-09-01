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

- **ASC와 AttributeSet은 `PlayerState`에 배치**하고, `PlayerCharacter`는 이를 캐시해 사용합니다. 캐릭터 액터의 생명주기와 무관하게 어트리뷰트가 유지되는 구조입니다.
- **초기화는 서버·클라이언트 양쪽에서 각각 수행**합니다. 서버는 `PossessedBy()`, 클라이언트는 `OnRep_PlayerState()` 에서 `InitAbilityActorInfo()` 를 호출합니다. 클라이언트에서는 PlayerState가 복제되어 도착하는 시점이 늦기 때문에, 한쪽만 처리하면 클라이언트에서 어빌리티가 동작하지 않습니다.
- `InitAbilityActorInfo(PS, this)` — **Owner는 PlayerState, Avatar는 Character** 로 분리해 전달합니다.
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

2~4인 온라인 협동 물리 운반 게임. 물리 충돌이 소음을 발생시켜 경보 게이지를 채우는 구조입니다.

| 항목 | 내용 |
|---|---|
| 엔진 | Unreal Engine 5.4 |
| 네트워크 | Listen Server · 서버 권위 + 클라이언트 보간 |
| 기간 | 2026.08.06 ~ 진행 중 (작업일 12일) |
| 팀 구성 | 6인 · 파트 분담 |
| 담당 | 물리 / 아이템 파트 단독 |
| 기여 | 커밋 45회 / `Source/` 기준 +10,844 / -3,129줄 |
| 수록 | 본인 기여 80% 이상인 25개 파일 · 7,706줄 |

### 구현

- **노획물 특성 3종** — 중량형(2인 협력 캐리) · 파손형(충격 누적 파괴) · 불안정형(기울기 초과 시 내용물 유출)
- **잡기 / 놓기 / 던지기** — 소지 중 물리 OFF + 소켓 Attach, 판정은 전부 서버 권위
- **충돌 · 낙하 감지** — `OnHit` 을 임펄스 임계값과 0.3초 디바운스로 걸러 `FLootImpactEvent` 방송
- **대형 금고 · 카트 · 점착 폭탄** — 폭파 진행률, 적재 판정, 적재 중 소음 감쇄

### 설계 메모

- 특성을 **상속이 아닌 컴포넌트**로 구현했습니다. 각 컴포넌트가 `BeginPlay` 에서 자기 태그를 등록하므로, BP 에 컴포넌트를 붙이는 행위 자체가 특성 부여가 됩니다. "무겁고 잘 깨지는 궤짝" 같은 조합이 다중 상속 없이 가능합니다.
- **아이템은 물리적 사실만 방송**합니다(`FLootImpactEvent`). 그 소리가 몇 미터까지 들리고 경계도를 몇 % 올리는지는 소음 시스템의 몫으로 분리했습니다.
- **이동 속도는 값만 제공**하고 실제 적용은 플레이어 파트가 합니다. 양쪽에서 곱하면 배율이 두 번 적용되고, `MaxWalkSpeed` 가 GAS 어트리뷰트라 다음 어트리뷰트 변화 때 값이 지워지기 때문입니다.

→ **상세 문서 · 트러블슈팅 4건: [HeavyHanded/README.md](HeavyHanded/README.md)**

## Contact

- Email: kimminjun020220@gmail.com
- GitHub: [@minjunkim-ue5](https://github.com/minjunkim-ue5)
