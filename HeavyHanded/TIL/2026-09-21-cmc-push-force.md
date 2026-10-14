# [UE5] 캐릭터가 스치기만 해도 물체가 날아가는 이유 — CMC 밀기 힘의 두 단계와 단위 버그

> **한 줄 요약**
> UE의 `CharacterMovementComponent`는 물체를 밀 때 **정지 상태면 충격량 500(kg·cm/s), 움직이는 중이면 힘 750,000(kg·cm/s²)** 을 준다.
> 첫 접촉이 물체를 움직이게 만드는 순간 **단위가 다른 힘 분기로 넘어가고**(한 프레임만 닿아도 충격량 환산 25배, 1초면 1500배),
> 그걸 줄여줄 감쇠 코드는 **단위가 맞지 않아 한 번도 실행되지 않는다.**

- 날짜: 2026-09-21
- 상태: **원인 분석만 완료. 아직 안 고쳤다** (수정 방향에 기획 판단이 필요해서)
- 프로젝트: HEAVY HANDED (UE 5.4, Listen Server) — 물리로 물건을 나르는 협동 게임
- 엔진 소스 (UE 5.4):
  - `Engine/Private/Components/CharacterMovementComponent.cpp`
  - `Engine/Private/PrimitiveComponentPhysics.cpp` · `Engine/Private/PhysicsEngine/BodyInstance.cpp`
  - `PhysicsCore/Private/ChaosEngineInterface.cpp`
  - `Experimental/Chaos/Public/Chaos/ParticleHandle.h` · `PerParticleAddImpulses.h` · `PerParticleEulerStepVelocity.h`

---

## 1. 문제 상황

노획물(물리 시뮬레이션되는 물건) 옆을 **걸어서 스치기만 해도** 물건이 몇 미터씩 날아가며 팽이처럼 회전한다.
운반이 핵심인 게임이라 그냥 넘길 수 없는 문제다.

같은 증상이 카트에도 있었고 카트는 따로 대응해 해결했는데(뒤에서 설명), **노획물은 그대로 남아 있었다.**

---

## 2. 어디서 오는 힘인가

캐릭터가 이동하다 막히면 `HandleImpact` 가 불린다.

```cpp
// CharacterMovementComponent.cpp:7416
void UCharacterMovementComponent::HandleImpact(const FHitResult& Impact, float TimeSlice, const FVector& MoveDelta)
{
    ...
    if (bEnablePhysicsInteraction)          // 기본값 true
    {
        const FVector FallingAcceleration = -GetGravityDirection() * GetGravityZ();
        const FVector ForceAccel = Acceleration + (IsFalling() ? FallingAcceleration : FVector::ZeroVector);
        ApplyImpactPhysicsForces(Impact, ForceAccel, Velocity);
    }
}
```

**걷다가 물건에 닿는 모든 순간 이 경로를 탄다.** 특별한 공격이나 상호작용이 아니라 평범한 이동이다.

### UE 5.4 기본값 (생성자 685~698행)

```cpp
bEnablePhysicsInteraction   = true;
InitialPushForceFactor      = 500.0f;
PushForceFactor             = 750000.0f;     // ← 1500배
bPushForceScaledToMass      = false;
bScalePushForceToVelocity   = true;
RepulsionForce              = 2.5f;
```

우리 프로젝트는 **이 값을 하나도 덮어쓰지 않았다.** `BP_BasePlayer` 바이너리를 훑어도 관련 프로퍼티가 저장돼 있지 않고 C++에도 없다.
즉 **"잘못 설정한" 게 아니라 "엔진 기본값을 그대로 쓰고 있는" 상태**다.

---

## 3. 원인 ① — 밀기가 두 가지 방식으로 갈린다

이 절은 **캐릭터가 물건에 닿은 순간부터 물건이 날아가기까지를 시간 순서대로** 따라간 것이다.
등장하는 함수 이름은 각 단계에서 "무슨 일을 하는 칸인지"만 알면 되고, 정확한 파일·행은 3-6에 모아 두었다.

### 3-0. 미리 알아 둘 것 — 충격량과 힘은 다른 것이다

| | 비유 | 물리 | 지속 |
|---|---|---|---|
| **충격량** | 망치로 한 번 때리기 | 속도를 바로 바꾼다 | **한 번으로 끝** |
| **힘** | 손으로 계속 밀기 | 가속도를 만든다 | **닿아 있는 내내 계속** |

엔진은 이 둘을 서로 다른 함수로 받는다. **앞으로 나오는 `500` 과 `750,000` 은 이 중 서로 다른 쪽에 들어가는 값이다.**
그래서 두 숫자를 그냥 나란히 비교할 수 없다 — 이게 이 절의 결론이다.

---

### 3-1. 0단계 — 밀 "방향" 을 만든다

캐릭터가 걸어가다 무언가에 막히면 엔진이 충돌 처리 함수를 부른다. 거기서 제일 먼저 하는 일:

```cpp
FVector Force = Impact.ImpactNormal * -1.0f;
```

`ImpactNormal` 은 **부딪힌 면이 바라보는 방향**이고, 여기에 -1 을 곱해 뒤집으면 **물체를 밀어낼 방향**이 된다.
길이가 1인 방향 벡터다.

> **여기서 이미 문제의 씨앗이 있다.** 이 벡터에는 **방향만 있고 세기가 없다.**
> 살짝 스쳤는지 전력으로 달려와 박았는지가 전혀 반영되지 않는다. 세기는 다음 단계에서 **정해진 상수**를 곱해 만든다.

### 3-2. 1단계 — 갈림길: "물체가 지금 멈춰 있나?"

```cpp
if (물체의 현재 속도 < 1 cm/s)      // 거의 멈춰 있다
{
    Force *= 500;                   // InitialPushForceFactor
    AddImpulseAtLocation(Force);     // ← 충격량으로 준다
}
else                                // 이미 조금이라도 움직이고 있다
{
    Force *= 750000;                // PushForceFactor
    AddForceAtLocation(Force);       // ← 힘으로 준다
}
```

- `InitialPushForceFactor` (= 500) : **첫 접촉용** 세기
- `PushForceFactor` (= 750,000) : **이미 움직이는 물체용** 세기
- `AddImpulseAtLocation` / `AddForceAtLocation` : 물체의 **특정 지점에** 충격량 / 힘을 주는 함수

방향 벡터의 길이가 1이었으므로, **곱셈 결과의 크기는 정확히 500 또는 750,000 이다.** 중간에 깎이는 것이 없다
(깎아 줄 코드가 있긴 한데 작동하지 않는다 — 4절).

### 3-3. 2단계 A — 500 이 어떻게 "0.5 m/s" 가 되는가

노획물 기본 질량 **10kg** 으로 계산한다.

```
① 질량으로 나눈다          500 ÷ 10kg = 50            → 단위: cm/s (= 속도)
② 물체 속도에 그대로 더한다  0 + 50 cm/s = 50 cm/s      = 0.5 m/s
③ 더한 값은 0 으로 지운다    다음 프레임에는 남지 않는다
```

**여기에는 "시간" 이 곱해지지 않는다.** 충격량은 정의상 *힘 × 시간* 이라 이미 시간이 포함된 양이기 때문이다.
그래서 **한 번 속도를 0.5 m/s 올리고 끝난다.** 사람이 가볍게 물건을 툭 미는 정도 — 의도대로 동작하는 쪽이다.

### 3-4. 2단계 B — 750,000 이 어떻게 "한 프레임에 12.5 m/s" 가 되는가

```
① 질량으로 나눈다          750,000 ÷ 10kg = 75,000    → 단위: cm/s² (= 가속도)
                                                       = 750 m/s²  (지구 중력의 76배)
② "가속도 칸" 에 더해 둔다   기존 값에 += 로 쌓인다
③ 한 프레임 시간을 곱해 속도로 바꾼다
      60fps 이면 한 프레임 = 1/60초
      750 m/s² × 1/60초 = 12.5 m/s     ← 이만큼 속도가 오른다
④ 아직 닿아 있으면 다음 프레임에 ① 부터 다시 한다
```

**②에서 "쌓인다" 는 것도 중요하다.** 캐릭터 이동은 한 프레임 안에서 여러 조각으로 나뉘어 처리될 수 있고,
그때마다 충돌 처리가 다시 불리면 가속도가 그만큼 누적된 뒤 한꺼번에 적분된다.

---

### 3-5. 두 결과가 다른 이유 — 마지막 한 줄 차이다

A 와 B 는 **질량으로 나누는 지점까지 완전히 같은 길**을 간다. 갈리는 것은 마지막 한 줄뿐이다.

| | A (충격량 500) | B (힘 750,000) |
|---|---|---|
| 질량으로 나눈다 | O | O |
| 결과의 의미 | 속도 (cm/s) | 가속도 (cm/s²) |
| **한 프레임 시간을 곱한다** | **X** | **O** |
| 쓰고 나서 | 지운다 → 1회만 | 남는다 → 접촉하는 내내 반복 |
| 실제 효과 | 0.5 m/s **한 번** | 12.5 m/s **매 프레임** |

```
A:  속도 += 충격량 ÷ 질량
B:  속도 += (힘 ÷ 질량) × 한 프레임 시간      ← 매 프레임
```

#### 같은 단위로 맞춰 보면

힘은 "× 시간" 을 해야 충격량과 비교할 수 있다.

| 접촉이 유지된 시간 | B 를 충격량으로 환산 | A(500) 대비 |
|---|---|---|
| 1 프레임 (1/60초) | 750,000 ÷ 60 = 12,500 | **25배** |
| 3 프레임 (0.05초) | 37,500 | 75배 |
| 1초 | 750,000 | **1500배** |

**"1500배" 는 1초간 붙어 있었을 때의 숫자이고, 한 프레임만 닿아도 이미 25배다.**

### 3-6. 그래서 실제로 벌어지는 일

```
프레임 1   물체 정지 → A 분기 → 속도 0.5 m/s          ... 여기까지는 의도대로다
             ↓ 그 결과 물체가 "움직이는 중" 이 되었다
프레임 2   0.5 m/s 는 1 cm/s 보다 크다 → B 분기 → 속도 += 12.5 m/s  (13 m/s)
프레임 3   여전히 움직인다 → B 분기 → 속도 += 12.5 m/s             (25 m/s)
             ...
```

**첫 접촉이 물체를 움직이게 만든 것 자체가, 다음 프레임부터 훨씬 센 분기로 넘어가는 조건이 된다.**
스치기만 해도 2~3프레임은 닿아 있으므로 수십 m/s 로 날아간다.

그리고 두 함수 모두 이름이 `...AtLocation` — **무게중심이 아니라 부딪힌 지점에** 준다.
중심에서 벗어난 만큼 회전력(토크)이 함께 생긴다. **물건이 날아가면서 팽이처럼 도는 이유가 이것이다.**

### 3-7. 코드 위치 (더 파고 싶을 때)

| 단계 | 어디 | 하는 일 |
|---|---|---|
| 방향 만들고 분기 | `CharacterMovementComponent.cpp:7446` (`ApplyImpactPhysicsForces`) | 법선 뒤집기 → 500/750,000 곱하기 → 두 함수 중 하나 호출 |
| 충격량 입구 | `PrimitiveComponentPhysics.cpp:206` → `BodyInstance.cpp:3539` | 컴포넌트 → 물리 바디로 전달 |
| 충격량 ÷ 질량 | `SingleParticlePhysicsProxy.h:669` | `Impulse * InvM` — 속도 단위로 바꿔 저장 |
| 충격량 → 속도 | `PerParticleAddImpulses.h:47` | `V += 저장값` 후 **0 으로 리셋** (시간 안 곱함) |
| 힘 입구 | `PrimitiveComponentPhysics.cpp:248` → `BodyInstance.cpp:3426` | 컴포넌트 → 물리 바디로 전달 |
| 힘 ÷ 질량 | `ParticleHandle.h:3416` (`AddForce`) | `Force * InvM + 기존값` — 가속도로 **누적** |
| 힘 → 속도 | `PerParticleEulerStepVelocity.h:47` | `V += 가속도 * Dt` — **여기서만 시간을 곱한다** |
| 토크 계산 | `ChaosEngineInterface.cpp:808 / 884` | `(접촉점 − 무게중심) × 벡터` → 회전 |

`InvM` = 질량의 역수(1/질량), `Dt` = 한 물리 스텝의 시간(60fps면 1/60초).

---

## 4. 원인 ② — 감쇠 코드가 실행되지 않는다 (단위 버그)

`bScalePushForceToVelocity = true` 라 속도에 따라 힘을 줄이게 되어 있다.

```cpp
const FVector ComponentVelocity = ImpactComponent->GetPhysicsLinearVelocity();   // cm/s
const FVector VirtualVelocity = ImpactAcceleration.IsZero()
    ? ImpactVelocity                                     // cm/s
    : ImpactAcceleration.GetSafeNormal() * GetMaxSpeed(); // cm/s

float Dot = 0.0f;
if (bScalePushForceToVelocity && !ComponentVelocity.IsNearlyZero())
{
    Dot = ComponentVelocity | VirtualVelocity;           // (cm/s)·(cm/s) = cm²/s²

    if (Dot > 0.0f && Dot < 1.0f)                        // ← 여기
    {
        PushForceModificator *= Dot;
    }
}
```

**`Dot` 은 정규화되지 않은 내적이라 단위가 cm²/s² 다.**

```
물체가 50 cm/s 로 밀리고, 캐릭터 최대 속도가 600 cm/s 라면
    Dot ≈ 50 × 600 = 30,000
```

`0 < Dot < 1` 창에 들어갈 수가 없다. **감쇠는 사실상 한 번도 실행되지 않고 `PushForceModificator` 는 항상 1로 남는다.**

정규화된 내적(-1 ~ 1)을 의도한 코드로 보이는데 정규화가 빠져 있다. 즉 **엔진이 준비해 둔 안전장치가 죽어 있는 상태**이고, 그래서 이 문제가 이렇게까지 커진다.

---

## 5. 원인 ③ — 질량으로는 해결되지 않는 구조

`bPushForceScaledToMass = false` 라 힘에 질량을 곱하지 않는다. 따라서

```
가속도 = PushForceFactor / 질량
```

| 질량 | 가속도 | 중력 대비 |
|---|---|---|
| 2 kg | 3,750 m/s² | 382배 |
| 10 kg (기본) | 750 m/s² | 76배 |
| 60 kg | 125 m/s² | 13배 |

무거울수록 덜하지만 **60kg짜리도 중력의 13배**다. 질량만으로는 못 막는다.

그리고 이 플래그를 **켜면 더 나빠진다.**

```cpp
if (bPushForceScaledToMass)
{
    PushForceModificator *= BodyMass;    // Force ∝ mass
}
// → a = F/m 에서 질량이 약분되어 a = PushForceFactor = 7,500 m/s² 고정
```

질량과 무관하게 7,500 m/s² 가 된다. **이 플래그는 해결책이 아니다.**

---

## 6. 프로젝트 쪽 기여 요인

### 노획물 (`ALootBase`)

| 항목 | 상태 | 영향 |
|---|---|---|
| 질량 | `MassKg` 기본 **10kg**, `BeginPlay` 가 `SetMassOverrideInKg` (LootBase.cpp:304) | 가벼울수록 가속도가 크다 |
| **감쇠** | **설정하지 않음** → 엔진 기본 `LinearDamping 0.01` | 한 번 얻은 속도를 빼내는 것이 없다 |
| 콜리전 | `Loot` 프로파일, Pawn = Block | 부딪혀야 `HandleImpact` 가 돈다 — 정상 동작 |
| 시뮬레이션 | 생성자에서 무조건 `SetSimulatePhysics(true)` | 클라이언트 사본도 돈다 |

**감쇠가 없는 것이 두 번째 요인이다.** 엔진이 큰 가속을 주더라도 저항이 있으면 속도가 곧 떨어지는데, 0.01은 없는 것과 같다.

### 왜 카트만 멀쩡한가 — 이미 대응했기 때문

`AHandCart` 는 아무도 잡고 있지 않을 때 두 가지를 건다.

```
IdleMassKg   = 400   →  가속도 750,000 / 400 = 18.75 m/s²      (10kg 대비 1/40)
IdlePushDrag = 8     →  종단 속도 ≈ 18.75 / 8 = 2.3 m/s 로 수렴
```

**질량은 가속도를 낮추고, 저항은 속도의 상한을 만든다. 둘 다 있어야 한다.**
- 질량만 올리면 → 느리게라도 계속 가속해서 결국 빨라진다
- 저항만 걸면 → 초기 가속이 너무 커서 첫 프레임에 이미 튄다

노획물에는 이 두 가지가 모두 없다. 카트에서 이미 답을 찾아 놓고 노획물에 옮기지 않은 상태다.

### 네트워크 측면 (부수적)

`HandleImpact` 는 **캐릭터를 움직이는 모든 머신에서 돈다.** 클라이언트 예측 경로에도 있으므로
클라이언트도 자기 노획물 사본에 힘을 준다. 서버 트랜스폼이 복제되어 덮어쓰지만
그 사이 한두 프레임은 두 값이 싸운다 — "클라이언트에서 더 심하게 튄다"는 인상이 여기서 온다.

---

## 7. 손댈 수 있는 지점 (아직 결정 안 함)

| 위치 | 효과 | 대가 |
|---|---|---|
| `BaseCharacter` CMC — `PushForceFactor` 축소 | 한 줄로 전부 해결 | 플레이어 파트 파일. 노획물·장비·카트 전부에 영향 |
| `BaseCharacter` CMC — `bEnablePhysicsInteraction = false` | 밀기를 아예 끈다 | 몸으로 물건을 밀 수 없게 되어 게임 느낌이 바뀐다 |
| `ALootBase` — 카트 방식(질량 + 저항) 이식 | 남의 파일을 안 건드린다 | 노획물마다 튜닝 필요, 던지기 느낌에도 영향 |
| `ALootBase` — 감쇠만 추가 | 가장 작은 변경 | 초기 튐은 남고 멀리 날아가는 것만 줄어든다 |

**어느 쪽이든 "몸으로 밀어서 물건을 옮기는 것을 허용할지"라는 기획 판단이 먼저다.**
카트는 "천천히 밀린다"로 정했으니, 노획물도 같은 규칙으로 갈지 정하면 방향이 나온다.

---

## 8. 배운 것

### ① 충격량과 힘은 다른 물리량이다

- **충격량** = 속도를 한 번 바꾼다. `AddImpulse`
- **힘** = 가속도를 만들고, 작용하는 동안 계속 쌓인다. `AddForce`

같은 숫자라도 의미가 완전히 다르다. 엔진 API가 둘을 나눠 두었다면 **숫자를 비교하기 전에 어느 쪽인지부터 확인해야 한다.**
`500` 과 `750,000` 은 1500배 차이가 아니라 **단위부터 다른 값**이고, 실제 체감 차이는 접촉 시간에 비례해 더 벌어진다.
차이가 생기는 곳은 **속도를 갱신하는 마지막 한 줄**이다 — 충격량은 시간을 안 곱하고, 힘은 한 프레임 시간을 곱한다.

### ② "값이 이상하게 크다"면 단위를 의심한다

`if (Dot > 0.0f && Dot < 1.0f)` 는 **정규화된 값(-1~1)을 기대하는 조건문**이다.
그런데 들어오는 값은 cm/s 두 개의 내적이라 수만 단위다.
조건이 틀린 게 아니라 **들어오는 값의 단위가 가정과 다른 것**이고, 이런 건 컴파일도 되고 경고도 없다.

> 같은 성격의 함정을 열흘 전에도 겪었다 — RPC로 받은 `FRotator`를 `Clamp(-80, 80)` 한 건.
> **"이 값의 단위와 범위가 내 가정과 같은가"** 를 확인하는 습관이 필요하다.

### ③ 엔진 기본값은 "합리적인 값"이 아니다

`PushForceFactor = 750000` 은 UE 기본값이다. 아무도 건드리지 않았고, 그래서 문제가 된다.
**엔진 기본값은 "그 기능을 쓰는 어떤 게임에서 한 번 맞춰 본 값"이지 내 게임에 맞는 값이 아니다.**
물리 상호작용처럼 게임 느낌을 좌우하는 설정은 기본값을 그대로 두는 것 자체가 선택이다.

### ④ 한 번 푼 문제는 같은 종류 전체에 적용했는지 확인한다

카트에서 이미 "질량 + 저항" 조합으로 답을 찾았는데 노획물에는 옮기지 않았다.
**증상이 같으면 원인도 같은지 먼저 의심하고, 이미 있는 해법을 찾아보는 편이 빠르다.**

---

## 9. 체크리스트 — 캐릭터가 물리 물체를 밀 때

- [ ] `bEnablePhysicsInteraction` 을 쓸 것인지 결정했는가 (기본 켜짐)
- [ ] `PushForceFactor` / `InitialPushForceFactor` 를 게임에 맞게 조정했는가
- [ ] 밀리는 물체에 **감쇠(LinearDamping)** 가 있는가 — 없으면 속도가 안 빠진다
- [ ] 질량만으로 막으려 하고 있지 않은가 (`a = F/m` 이라 한계가 있다)
- [ ] 물체가 회전하며 날아간다면 `AddForceAtLocation` 의 토크를 고려했는가
- [ ] 클라이언트에서만 더 심하지 않은가 (예측 경로에서 같은 힘이 한 번 더 들어간다)
