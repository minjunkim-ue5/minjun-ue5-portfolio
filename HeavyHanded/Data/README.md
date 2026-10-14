# Data — 데이터 테이블 원본 CSV

여기 있는 CSV 는 `Content/` 의 DataTable 에셋(`DT_*.uasset`)의 **원본**이다.

## 왜 CSV 를 따로 두는가

uasset 은 병합이 안 된다. 두 사람이 같은 표를 고치면 나중에 push 한 쪽이 이기고
다른 쪽 작업은 소리 없이 사라진다.

CSV 는 텍스트라 git 이 줄 단위로 diff 를 보여주고, 서로 다른 행을 고쳤다면
자동으로 병합된다. 그래서 **수치의 진짜 원본은 CSV 쪽이다.**

`Content/` 밖에 두는 이유는 쿠킹 대상에서 빼기 위해서다. 게임에 들어가는 것은
임포트된 uasset 이고, CSV 는 소스일 뿐이다.

## 표가 여러 개인 이유 — 상속이 아니라 조인이다

노획물 수치는 표 하나가 아니라 **공통 표 하나 + 특성별 표 여러 개**로 나뉜다.

```
DT_LootCatalog                    DT_LootStability            DT_LootDurability
─────────────────────────         ──────────────────          ──────────────────
Loot_Candlestick  은촛대 $800     Loot_CoinBag  50 …          Loot_Painting  1200  2
Loot_CoinBag      자루  $1000  ←── 행 이름이 같다
Loot_Crown        왕관  $4000                                 Loot_Vase      2400  3
Loot_GoldBar      금괴  $3000
Loot_LargeSafe    금고  $15000
Loot_Painting     초상화 $2500 ←──────────────────────────────── 행 이름이 같다
Loot_Statue       청동상 $5000
Loot_Vase         백자  $1500  ←──────────────────────────────── 행 이름이 같다

8행 (모든 노획물)                 1행 (불안정형만)             2행 (파손형만)
```

`DT_LootHeavy` 도 같은 방식이다 — 대형 금고와 청동상 2행뿐이다.

**표 사이에 부모/자식 관계는 없다.** 셋은 대등한 별개의 파일이고, 이어주는 것은
행 이름이 같다는 약속 하나뿐이다. 관계형 DB 의 조인과 같다. 그래서

- 특성 표에는 `DisplayName`·`BaseValue` 같은 열이 **없다.** 물려받지 않는다.
- 카탈로그에서 행을 지워도 특성 표의 행은 **따라 지워지지 않는다.** 고아 행이 조용히 남고,
  언리얼이 막아 주지 않으므로 코드가 경고로 잡는다.

**왜 나눴나.** 예전에는 불안정형 9개 값이 카탈로그의 열이었는데, 8행 × 9필드 = 72개 중
의미 있는 값이 1개(동전 자루의 `SpillTiltAngle=50`)였다. 나머지 71개는 기본값이
직렬화된 것일 뿐인데 표에 숫자로 적혀 있으니 "이 물건도 기울면 새겠구나" 로 읽혔다.
파손형 2개 값도 `Physics` 안에 섞여 있어서 같은 문제를 겪었고, 무게·던지기 값 사이에
끼어 있어 눈에 덜 띄었을 뿐이다.

나누고 나면 **표에 행이 있다는 것 자체가 그 특성의 명단**이 된다.
`DT_LootStability` 를 열면 불안정형이 무엇인지 한눈에 보인다.

> ~~중량형은 아직 `Physics.WeightClass` 로 정한다~~ → 2026-08-19 에 `ULootHeavyComponent` 와
> `DT_LootHeavy` 로 옮겨서 예외가 사라졌다. 이제 특성 셋 다 컴포넌트가 선언한다.

## 작업 순서

**값을 고칠 때**

1. 언리얼 에디터에서 DataTable 을 열어 고친다 (중첩 구조체가 펼쳐져서 편집하기 편하다)
2. 표 상단 **Export as CSV** → 여기 있는 파일에 덮어쓴다
3. CSV 와 uasset 을 **같이 커밋한다**

엑셀에서 직접 고쳐도 된다. 그때는 반대로 에디터에서 **Reimport** 한다.
CSV 는 UTF-8(BOM 포함)이라 엑셀에서 한글이 깨지지 않는다.

**노획물을 새로 추가할 때**

먼저 **특성 조합이 기존과 같은지** 본다. 표는 값만 담고, 특성은 BP 의 컴포넌트 조합이
정하기 때문이다.

*조합이 같으면* (메시와 수치만 다르다)

1. `DT_LootCatalog` 에 행을 추가한다 (이름 규칙은 아래)
2. **특성이 있으면 같은 이름으로 특성 표에도 행을 추가한다** (아래 표 참고)
3. 비슷한 `BP_Loot_*` 을 복제하고 메시를 교체한다
4. 디테일의 `Loot|Data > Loot Definition` 에서 새 행을 고른다

*새로운 조합이면* (예: 파손형이면서 중량형)

새 BP 를 만들고 아래대로 컴포넌트를 붙인 뒤 위 4번을 한다.
**태그는 손으로 달지 않는다** — 컴포넌트가 BeginPlay 에서 자기 태그를 등록한다.
그래서 "컴포넌트는 있는데 태그가 없는" 상태가 생기지 않는다.

| 만들 것 | 붙일 컴포넌트 | 행을 추가할 표 | 자동으로 붙는 태그 |
|---|---|---|---|
| 파손형 | `LootDurabilityComponent` | `DT_LootDurability` | `Loot.Type.Fragile` |
| 불안정형 | `LootStabilityComponent` | `DT_LootStability` | `Loot.Type.Unstable` |
| 중량형 | `LootHeavyComponent` | `DT_LootHeavy` | `Loot.Type.Heavy` |
| 평범 | 없음 | — | `Loot.Type` |

**컴포넌트와 행은 둘 다 있어야 한다.** 하나만 있으면 PIE 로그에 경고가 뜬다.
어느 쪽이 빠졌는지도 메시지가 알려준다.

무게 등급(`WeightClass`)은 없앴다. "중량형인가" 는 컴포넌트가 다는 `Loot.Type.Heavy` 태그가,
"얼마나 느려지는가" 는 카탈로그의 `CarrySpeedMultiplier` 가 말한다. 등급과 배율이 같은 것을
두 번 말하고 있었고, Light / Normal 구분은 끝까지 아무도 읽지 않았다.

상속이 아니라 조합이라 특성을 섞어도 클래스가 늘어나지 않는다.
대형 금고처럼 중량형 + 경보 연동형이 되면 컴포넌트 두 개를 붙이고 표 두 곳에 행을 넣는다.

## 행 이름 규칙

`Name` 열이 곧 ID 다. **기본은 `Loot_물건` 하나뿐이다.**

```
Loot_Candlestick        은촛대
Loot_Vase               청화백자
```

특성도 장소도 이름에 넣지 않는다. 둘 다 이미 다른 곳이 알고 있는 정보이고,
이름에 또 적으면 두 벌이 되어 한쪽만 바뀐다.

- **특성**은 BP 의 컴포넌트 조합이 정한다.
  `Loot_Heavy_Statue` 로 적어 두면 BP 에서 컴포넌트를 떼는 순간 이름이 거짓말이 된다.
  조합(중량형 + 경보 연동형인 대형 금고)이 생기면 부를 이름도 없어진다.
- **장소**는 배치의 속성이지 물건의 속성이 아니다. 행 하나는 '한 종류의 물건'이지
  '한 배치'가 아니다. 같은 은촛대를 저택과 박물관에 두면 값이 똑같은 행이 둘 생기고,
  가격을 조정할 때 한쪽만 고치게 된다.

**같은 물건이 장소마다 정말 달라야 할 때만** 뒤에 구분자를 붙인다.

```
Loot_Vase               일반 도자기
Loot_Vase_Museum        박물관 소장품 — 더 비싸고 더 잘 깨진다
```

접두사가 아니라 접미사인 것이 중요하다. 이름순으로 정렬하면 같은 물건의 변형이
나란히 붙어서 무엇이 다른지 바로 비교된다. 앞에 붙이면 표 반대편으로 흩어진다.

**이름은 처음에 정하고 바꾸지 않는다.** `FDataTableRowHandle` 은 이름으로 참조해서,
행 이름을 바꾸면 그 행을 쓰던 BP 가 전부 연결이 끊긴다. 끊겨도 로그 경고만 뜨고
조용히 BP 인라인 값으로 돌기 때문에 발견이 늦어진다.

## 파일

모든 표는 `Content/HeavyHanded/Data/DataTables/` 아래에 있다.

| CSV | DataTable | 행 구조체 | 담는 것 |
|---|---|---|---|
| `LootCatalog.csv` | `DT_LootCatalog` | `FLootDefinitionRow` | 모든 노획물의 이름·가치·물리 |
| `LootStability.csv` | `DT_LootStability` | `FLootStabilityData` | 불안정형만 |
| `LootDurability.csv` | `DT_LootDurability` | `FLootDurabilityData` | 파손형만 |
| `LootHeavy.csv` | `DT_LootHeavy` | `FLootHeavyData` | 중량형만 (2인 캐리) |
| `ShopCatalog.csv` | `DT_ShopCatalog` | `FShopItemRow` | 은신처 상점 구매 장비의 가격 |

전부 김민준 담당이다.

`DT_LootCatalog` 은 BP 마다 `Loot Definition` 에서 행을 고르지만, 특성 표는 고를 것이
없어서 **Project Settings → Game → Loot** 에 프로젝트 전체 기준으로 한 번만 지정한다.
행 이름을 카탈로그와 공유하기 때문이다. 표를 새로 만들면 여기 연결을 잊지 말 것 —
비어 있으면 특성 수치가 조용히 기본값으로 돈다.

## 상점 가격표는 태그가 행 이름이다

`ShopCatalog.csv` 만 행 이름 규칙이 다르다. **장비 태그 이름을 그대로 쓴다.**

```
Equipment.Drone,정찰 드론,4000
```

진열대(`AShopDisplay`)는 이미 `ItemTag` 를 갖고 있어서 BP 에서 행을 고를 것이 없다.
`ItemTag.GetTagName()` 으로 바로 찾는다 — `DT_SiteCatalog` 가 `SiteTag` 로 찾는 것과 같다.
그래서 표에 태그 열이 없다. 넣으면 행 이름과 두 벌이 되어 한쪽만 고치게 된다.

연결은 **Project Settings → Game → Shop → Shop Catalog** 에 한 번만 한다.

**진열대가 없는 장비도 행을 둔다** (EMP · 대차). 이 표가 가격의 전체 목록이어야
장소별 목표 금액과 나란히 놓고 균형을 볼 수 있다.

BP 의 `Price` 칸은 표에 행이 없을 때만 쓰이는 폴백으로 남아 있다. 표에 행이 있으면
`BeginPlay` 에서 덮어쓰고, 어느 값이 쓰였는지 로그에 남는다. 노획물처럼 칸이 회색으로
잠기지는 않는다 — 잠금은 BP 가 고른 행 핸들을 근거로 거는데 여기는 태그로 찾기 때문에
에디터가 미리 알 수 있는 것이 없다.

## BP 에서 수치를 고칠 수 없는 이유

`Loot Definition` 에 행을 지정한 BP 는 디테일 패널에서 아래 두 칸이 **회색으로 잠긴다.**

```
Loot > Physics Data
Loot|Value > Base Value
```

표의 값이 스폰 시 이 칸들을 통째로 덮어쓰기 때문이다. 열어 두면 BP 에서 고칠 수 있고
저장까지 되는데 게임에는 반영되지 않아서, 원인을 찾는 데 시간을 버리게 된다.

**회색으로 보이는 숫자는 마지막으로 저장된 BP 값이지 표의 값이 아니다.**
수치를 확인할 때는 `Loot Definition` 옆 화살표로 `DT_LootCatalog` 를 열어서 본다.

행을 비우면 두 칸이 다시 열린다. 표에 올리지 않은 일회성 실험물은 그렇게 만들면 된다.

`Loot|Carry`, `Loot|Impact`, `Loot|Debug` 계열은 표에 없는 값이라 계속 BP 에서 고친다.

**특성 컴포넌트의 `Data` 칸은 잠기지 않는다.** 표에 행이 있으면 스폰 시 덮어쓰이므로
거기 적힌 값은 무시된다. 잠그지 않은 것은 행이 없을 때 그대로 폴백으로 쓰이기 때문이다.
액터의 두 칸과 같은 규칙인데, 잠금이 아직 액터에만 걸려 있다.

## 경고 메시지

컴포넌트와 표의 행은 **둘 다 있어야** 특성이 동작한다. 하나만 있으면 PIE 로그에 경고가 뜬다.
양쪽이 서로를 확인하기 때문에 어느 쪽이 빠져도 반드시 걸린다.

*행은 있는데 컴포넌트가 없다* — `ALootBase` 가 찍는다. **BP 를 봐야 한다.**

```
[Loot:...] DT_LootStability 에 'Loot_CoinBag' 행이 있는데 ULootStabilityComponent 가 없다
[Loot:...] DT_LootDurability 에 'Loot_Vase' 행이 있는데 ULootDurabilityComponent 가 없다
[Loot:...] DT_LootHeavy 에 'Loot_Statue' 행이 있는데 ULootHeavyComponent 가 없다
```

*컴포넌트는 있는데 행이 없다* — 각 컴포넌트가 찍는다. **표를 봐야 한다.**
Project Settings 의 표 연결이 비어 있어도 같은 메시지가 나온다.

```
[Loot:...] ULootStabilityComponent 가 붙어 있는데 DT_LootStability 에 '...' 행이 없다
[Loot:...] ULootDurabilityComponent 가 붙어 있는데 DT_LootDurability 에 '...' 행이 없다
[Loot:...] ULootHeavyComponent 가 붙어 있는데 DT_LootHeavy 에 '...' 행이 없다
```

`Loot|Tags > Loot Type Tags` 에 `Loot.Type` 하나만 있으면 컴포넌트를 빼먹은 것이다.
파손형이면 `Loot.Type.Fragile`, 불안정형이면 `Loot.Type.Unstable`,
중량형이면 `Loot.Type.Heavy` 가 자동으로 붙어 있어야 한다.

## 주의

`Physics` 열은 구조체가 통째로 들어가 있어서 괄호 안에 필드가 나열된다.
엑셀에서 이 칸을 직접 고치는 것은 권하지 않는다 — 괄호나 쉼표 하나가 어긋나면
그 행 전체가 기본값으로 임포트된다. **물리 수치는 에디터에서 고치는 것이 안전하다.**

특성 표(`LootStability.csv` / `LootDurability.csv` / `LootHeavy.csv`)는 중첩이 없어서 열이 평평하다.
엑셀에서 고쳐도 안전하다.

`DamageImpulseThreshold` 는 질량에 비례한다. 카탈로그에서 `MassKg` 를 바꾸면
`DT_LootDurability` 쪽도 같이 조정해야 한다.
10kg 기준 실측값: 100cm 낙하 5031 / 150cm 6497 / 300cm 9622, 착지 후 튕김은 500~900.
