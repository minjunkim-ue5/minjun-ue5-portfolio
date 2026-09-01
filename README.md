# 김민준 — Game Programmer Portfolio (Source Code)

Unreal Engine 5 / C++ 팀 프로젝트에서 본인이 직접 작성한 소스코드를 발췌한 저장소입니다.

> 에셋(.uasset, .umap), 상용 마켓플레이스 에셋, 팀원이 작성한 코드는 저작권 및 라이선스 문제로 제외했습니다. 빌드용이 아닌 **코드 열람용** 저장소입니다.

| 프로젝트 | 기간 | 팀 | 담당 | 수록 |
|---|---|---|---|---|
| [**HEAVY HANDED**](./HeavyHanded) — 2~4인 협동 물리 운반 잠입 | 2026.08.06 ~ 진행 중 | 6인 | 물리 / 아이템 단독 | 25파일 · 7,706줄 |
| [**Project_Dwarf_Extraction**](./DwarfExtraction) — 4인 협동 1인칭 호러 | 2026.07.02 ~ 08.04 | 4인 | Player 단독 | 18파일 · 약 4,700줄 |

두 프로젝트를 관통하는 주제는 **"서버 권위 구조에서 클라이언트만 실패하는 버그"** 입니다.
7월 프로젝트에서는 제 코드에서 4단 연쇄 버그로 겪었고, 8월 프로젝트에서는 같은 유형을
다른 파트의 코드에서 재현 경로와 함께 찾아냈습니다. 상세 내용은 각 폴더의 README에 있습니다.

## 기술 스택

- **언어 / 엔진** — C++, Unreal Engine 5.4
- **네트워크** — Replication (Server / Multicast / Client RPC, RepNotify), Listen Server, 서버 권위 판정
- **물리** — SimulatePhysics, OnComponentHit, Sweep, MTD(ComputePenetration), PhysicalMaterial
- **게임플레이** — GAS(ASC / AttributeSet / GameplayAbility), GameplayTag, DataTable, UActorComponent 조합, UInterface
- **협업** — Git / Git LFS, 브랜치 전략, `.gitattributes` 바이너리 잠금

## Contact

- Email: kimminjun020220@gmail.com
- GitHub: [@minjunkim-ue5](https://github.com/minjunkim-ue5)
