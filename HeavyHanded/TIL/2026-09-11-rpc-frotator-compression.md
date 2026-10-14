# [UE5] RPC로 보낸 FRotator가 334.5도로 도착한 이유 — 직렬화 압축과 순환값 Clamp

> **한 줄 요약**
> RPC로 보낸 `FRotator`는 16비트로 압축되며 `[0, 360)` 범위로 도착한다.
> 그 값을 `Clamp(-80, 80)` 같은 대칭 범위로 자르면 `-25.5°`가 `80°`로 뒤집힌다.
> 리슨 서버의 호스트는 RPC가 로컬 호출이라 압축을 거치지 않아서 **호스트에서는 절대 재현되지 않는다.**

- 날짜: 2026-09-11
- 프로젝트: HEAVY HANDED (UE 5.4, Listen Server)
- 대상: 정찰 드론 `ADrone` — 던지면 떠올라 조종하는 장비

---

## 1. 문제 상황

### 증상

| 조종하는 사람 | 결과 |
|---|---|
| 호스트(리슨 서버) | W 전진 / S 후진 — **정상** |
| 클라이언트 | 던진 직후 잠깐은 정상 → 그 뒤로 **W = 상승, S = 하강** |

- A/D(좌우)와 Space/Ctrl(상하)은 클라이언트에서도 정상이었다.
- "시간이 좀 지나면" 이상해진다는 점이 특이했다.

### 구조 (버그와 관련된 부분만)

```
[조종사 머신]                                  [서버]
마우스 입력
  → DesiredPitch / DesiredYaw 누적 (로컬)
  → 카메라를 즉시 돌림 (지연 없는 시야)
  → Server_SetFlightIntent(Move, Ascend, FRotator(Pitch, Yaw, 0))  ──→  ApplyControlRotation(Yaw, Pitch)
                                                                          ControlRotation 저장 (Pitch를 ±80으로 Clamp)
                                                                        TickFlight
                                                                          Forward = ControlRotation.Vector()
                                                                          Velocity = Forward × MoveInput.Y × Speed
```

**이동 방향 = 조종사가 보는 방향**이다. 위를 보고 W를 누르면 위로 올라가는 게 의도된 동작이다.

---

## 2. 디버깅 과정

### 헛짚은 가설 세 개

"호스트는 되고 클라이언트만 안 된다"까지는 바로 좁혔지만, 거기서 **구조**를 의심했다.

| # | 가설 | 한 일 | 결과 |
|---|---|---|---|
| 1 | 서버가 클라이언트 드론의 회전을 매 틱 강제하지 않아서 기울기가 남는다 | 이동 기준을 컴포넌트 트랜스폼이 아니라 `ControlRotation` 값으로 바꾸고 매 틱 재적용 | 여전함 |
| 2 | `SetSimulatePhysics`가 복제되지 않아 클라이언트 사본이 던질 때의 각속도로 계속 돈다 | 물리 끄기를 `OnRep`으로 옮겨 모든 머신에서 실행 | 여전함 (실제 문제이긴 해서 수정은 유지) |
| 3 | RPC 인자 정렬이 어긋나 `MoveInput.Y`가 `AscendInput` 자리로 들어간다 | 생성된 RPC 코드 확인 | 정상이었음 |

> 중간에 **Live Coding(Ctrl+Alt+F11)으로 멤버 변수 추가를 반영하려다** 반영이 안 된 일도 있었다.
> Live Coding은 함수 본문 변경만 안전하다. **클래스 크기가 바뀌는 멤버 추가·삭제는 전체 빌드**가 필요하다.

### 결정적인 로그

클라이언트 입력값과 서버가 받은 값을 양쪽에서 찍었다.

```
[입력]      Move=(0.00, 1.00)                                   ← 클라이언트
[서버 수신] Move=(0.00, 1.00)  Ascend=0.00  Rot=(P 334.5 / Y 28.5) ← 서버
```

- `Move`와 `Ascend`는 정확히 도착했다 → 가설 3 기각
- **`Pitch = 334.5`** → 클라이언트는 아래를 조금 보고 있었으니 `-25.5`를 보냈어야 한다

**값을 찍자마자 끝났다.** 구조 가설을 세우기 전에 경계를 넘는 값부터 찍었어야 했다.

---

## 3. 개념 정리

### 3-1. 직렬화(Serialization)

**메모리에 있는 데이터를 바이트 열로 바꾸는 것.** 네트워크로 보내거나 파일에 저장하려면 필요하다.
받는 쪽에서 바이트 열을 다시 데이터로 되돌리는 것이 **역직렬화(Deserialization)** 다.

```
[보내는 쪽 메모리]  FRotator { Pitch = -25.5, Yaw = 28.5, Roll = 0 }
        │  직렬화
        ▼
[패킷]              uint16 × 3 = { 0xEDDE, 0x1444, 0x0000 }   (Pitch, Yaw, Roll 을 각각 16비트로)
        │  역직렬화
        ▼
[받는 쪽 메모리]    FRotator { Pitch = 334.5, Yaw = 28.5, Roll = 0 }
```

핵심: **직렬화 → 역직렬화가 항상 원래 값을 그대로 돌려주는 것은 아니다.**
방식에 따라 정밀도나 **표현**이 바뀔 수 있다.

### 3-2. 네트워크 압축(양자화, Quantization)

실수를 **더 적은 비트의 정수로 근사**해서 보내는 것. 대역폭을 아끼려고 한다.

UE5의 `FRotator`는 double 3개다.

```
압축 없이   double × 3 = 24 바이트
short 압축  uint16 × 3 =  6 바이트   → 1/4
```

회전은 **움직이는 모든 액터가 거의 매 틱 보내는 값**이라 이 차이가 크다.
그리고 정밀도가 남아돈다.

```
360° / 65536 ≈ 0.0055°   ← 이 정도 오차는 눈에 안 보인다
```

### 3-3. UE가 FRotator를 압축하는 코드

`FRotator`의 `NetSerialize`는 무조건 short 압축을 쓴다.

```cpp
// Engine/Source/Runtime/Core/Private/Math/UnrealMath.cpp:75
bool TRotator<T>::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
    SerializeCompressedShort(Ar);
    bOutSuccess = true;
    return true;
}
```

축 하나를 압축·해제하는 함수:

```cpp
// Engine/Source/Runtime/Core/Public/Math/Rotator.h:721
uint16 CompressAxisToShort(T Angle)
{
    // map [0->360) to [0->65536) and mask off any winding
    return FMath::RoundToInt(Angle * 65536.f / 360.f) & 0xFFFF;
}

// Rotator.h:728
T DecompressAxisFromShort(uint16 Angle)
{
    // map [0->65536) to [0->360)
    return (Angle * 360.f / 65536.f);
}
```

### 3-4. -25.5가 334.5가 되는 계산

```
1. 원래 값                              -25.5°
2. × 65536 / 360 (= 182.0444)           -4642.13
3. RoundToInt                           -4642          ← 아직 음수
4. & 0xFFFF (하위 16비트만)             60894          ← 부호가 사라진다
   (-4642 의 2의 보수 = 0xFFFFEDDE, 하위 16비트 0xEDDE = 60894)
5. 전송 (uint16)                        60894
6. × 360 / 65536                        334.497 ≈ 334.5°
```

- **`& 0xFFFF`** 에서 음수 정보가 사라진다. 부호 없는 16비트에는 음수를 담을 자리가 없다.
- 해제 함수는 `uint16 × 360 / 65536`이라 **결과가 구조적으로 `[0, 360)`을 벗어날 수 없다.**

주석의 **"mask off any winding"** 이 의도다. winding = 몇 바퀴 돌았는가.

```
-25.5°  → 60894
334.5°  → 60894
694.5°  → 60894
```

**셋 다 같은 방향이라 같은 값으로 보낸다.** 표기(음수냐, 몇 바퀴째냐)는 일부러 버리고 방향만 살린다.
엔진은 정보를 잃지 않았다. **틀린 건 받은 쪽 코드**다.

### 3-5. 순환값과 선형값

| | 선형값 | 순환값 |
|---|---|---|
| 예 | 속도, 거리, 체력 | 각도, 시각(시·분), 요일 |
| 크기 비교 | 의미 있음 | 표기에 따라 달라짐 |
| 359와 1의 차이 | 358 | **2** |
| 같은 값의 표기 | 하나 | 여러 개 (`-25.5 = 334.5 = 694.5`) |

**`FMath::Clamp`는 선형값 연산이다.** "작으면 하한, 크면 상한"은 값이 한 줄 위에 순서대로 놓여 있을 때만 성립한다.

```
Clamp(-25.5, -80, 80) = -25.5   ← 의도한 결과
Clamp(334.5, -80, 80) =  80     ← 같은 방향인데 위를 80도 올려다보게 뒤집힘
```

순환값을 선형 연산에 넣으려면 **먼저 표기를 하나로 맞춰야** 한다.

UE가 제공하는 두 함수 (`Rotator.h`):

| 함수 | 결과 범위 | 용도 |
|---|---|---|
| `FRotator::ClampAxis(Angle)` | `[0, 360)` | 0 기준 한 바퀴로 맞출 때 |
| `FRotator::NormalizeAxis(Angle)` | `(-180, 180]` | **0을 중심으로 대칭 범위를 다룰 때** |

피치 제한 `±80`은 0 중심 대칭이라 `NormalizeAxis`가 맞다.

### 3-6. 왜 호스트에서는 안 터졌나 — RPC 호출 경로(Callspace)

같은 코드인데 호스트에서는 멀쩡했던 이유. **호스트의 Server RPC는 네트워크를 타지 않는다.**

RPC를 부르면 엔진이 먼저 "어디서 실행할지"를 정한다.

```cpp
// Engine/Source/Runtime/CoreUObject/Private/UObject/ScriptCore.cpp:2004 — UObject::ProcessEvent
int32 FunctionCallspace = GetFunctionCallspace(Function, NULL);
if (FunctionCallspace & FunctionCallspace::Remote)
{
    CallRemoteFunction(Function, Parms, NULL, NULL);   // 네트워크로 보냄 → 직렬화
}
if ((FunctionCallspace & FunctionCallspace::Local) == 0)
{
    return;
}
// 여기까지 오면 메모리의 Parms 로 _Implementation 을 바로 실행
```

```cpp
// Engine/Source/Runtime/Engine/Private/Actor.cpp — AActor::GetFunctionCallspace
bool bIsServer = NetMode == NM_ListenServer || NetMode == NM_DedicatedServer;
...
// if we are the server, and it's not a send-to-client function,
if (bIsServer && !(Function->FunctionFlags & FUNC_NetClient))
{
    // don't replicate
    return Callspace;   // = Local
}
```

두 경로를 비교하면:

```
호스트가 호출
  GetFunctionCallspace → 서버다 → Local
  CallRemoteFunction   → 안 부름
  NetSerialize         → 안 부름          ← 압축이 없다
  _Implementation(Pitch = -25.5)          원본 그대로
  Clamp(-25.5, -80, 80) = -25.5           정상

클라이언트가 호출
  GetFunctionCallspace → 서버가 아니고 Server 함수 → Remote
  CallRemoteFunction   → 패킷 전송
  NetSerialize         → SerializeCompressedShort   ← 여기서 압축
  서버의 _Implementation(Pitch = 334.5)
  Clamp(334.5, -80, 80) = 80              뒤집힘
```

**`_Implementation` 코드는 한 글자도 다르지 않다. 들어오는 숫자가 달랐다.**

```
버그가 터지는 조건 = 잘못된 Clamp + [0,360) 표기로 바뀐 값
호스트             = 잘못된 Clamp + 원본 값        → 안 터짐
클라이언트         = 잘못된 Clamp + 압축된 값      → 터짐
```

### 3-7. "잠깐은 정상이다가"의 정체

- 마우스를 안 움직이면 `Pitch = 0` → 압축해도 0 → Clamp 통과 → 정상
- **아래를 한 번 보는 순간** 음수 → `334` 근처로 변환 → `80`으로 잘림 → 이후 계속 수직

시간이 지나서가 아니라 **처음 아래를 본 순간**이 경계였다.

---

## 4. 해결

### 수정 코드

```cpp
void ADrone::ApplyControlRotation(float Yaw, float Pitch)
{
    // 자르기 전에 (-180, 180] 로 표기를 맞춘다.
    // RPC 로 온 334.5 → -25.5
    const float NormalizedPitch = FRotator::NormalizeAxis(Pitch);

    ControlRotation = FRotator(
        FMath::Clamp(NormalizedPitch, -MaxPitchDegrees, MaxPitchDegrees), Yaw, 0.f);

    SetActorRotation(FRotator(0.f, Yaw, 0.f));

    if (DroneCamera)
    {
        // 위에서 만든 값을 그대로 쓴다. 여기서 원본 Pitch 를 다시 자르면 같은 함정을 또 밟는다.
        DroneCamera->SetRelativeRotation(FRotator(ControlRotation.Pitch, 0.f, 0.f));
    }
}
```

### 바뀐 점

| 전 | 후 |
|---|---|
| `Clamp(Pitch, -80, 80)` | `Clamp(NormalizeAxis(Pitch), -80, 80)` |
| 카메라 쪽에 같은 `Clamp`가 한 벌 더 있음 | 정규화된 `ControlRotation.Pitch` 하나를 공유 |

**기준값은 한 곳에서만 만든다.** 같은 변환을 두 군데서 따로 하면, 한쪽만 고치고 다른 쪽에서 같은 버그가 남는다.

### 검증

```
[서버 수신] ... Rot=(P -25.5 / ...)   ← 이제 -80 ~ 80 범위 안
```

- 클라이언트: 정면 W/S 전후, 아래 보고 W 하강, 위 보고 W 상승 — 정상
- 호스트: 기존과 동일 — 정상

---

## 5. 배운 것

### ① "호스트는 되는데 클라이언트만 안 된다" → 직렬화 경계를 먼저 의심

리슨 서버의 호스트는 **서버이자 플레이어**라 Server RPC가 전부 로컬 호출이 된다.
그래서 호스트 창에서는 다음이 전부 안 보인다.

- 직렬화 압축 · 정밀도 손실 · 표기 변환
- 패킷 지연 · 순서 뒤바뀜
- Owner가 아니라서 RPC가 버려지는 문제

**구조를 뜯기 전에, 경계를 넘은 값이 보낸 값과 같은지부터 로그로 확인한다.**

### ② 증상이 "점진적"이면 고정 버그가 아니다

처음부터 틀리면 배선 문제다. **어느 시점부터 틀리면** 누적되거나 특정 입력에서 경계를 넘는 값이 있다.
이번엔 "아래를 본 순간 음수가 됨"이 그 경계였다.

### ③ 순환값에 선형 연산을 쓰기 전에 표기를 맞춘다

각도, 시각처럼 한 바퀴 돌면 제자리인 값은 **같은 값이 여러 표기를 가진다.**
`Clamp`, `<`, `>`, 평균, 보간 같은 선형 연산 전에 `NormalizeAxis` 등으로 표기를 하나로 맞춘다.

### ④ Live Coding의 한계

| 변경 | Live Coding (Ctrl+Alt+F11) |
|---|---|
| 함수 본문, 리터럴 값 | 가능 |
| **멤버 변수 추가·삭제** | **불가** — 클래스 크기가 바뀐다. UHT가 막지 않아 조용히 반영 안 됨 |
| `UPROPERTY` / `UFUNCTION` 추가 | 불가 — 에디터 닫고 전체 빌드 |

---

## 6. 체크리스트 — 회전값을 네트워크로 주고받을 때

- [ ] RPC·복제로 받은 `FRotator`를 대칭 범위(`±X`)로 자르기 전에 `NormalizeAxis` 했는가
- [ ] 두 각도의 차이를 구할 때 `FMath::FindDeltaAngleDegrees` 같은 순환 전용 함수를 썼는가
- [ ] 같은 각도 변환을 여러 곳에서 따로 하고 있지 않은가
- [ ] **클라이언트 창에서** 테스트했는가 (Run Under One Process 해제, 2인 이상)
- [ ] 보낸 값과 받은 값을 양쪽에서 로그로 비교해 봤는가

---

## 참고 — 곁가지로 고친 것

**`SetSimulatePhysics`는 복제되지 않는다.**
서버에서만 물리를 끄면 클라이언트의 액터 사본은 계속 시뮬레이션되며, 던질 때 받은 각속도로 회전하면서
복제되어 오는 트랜스폼과 싸운다. 이번 버그의 원인은 아니었지만 실제 문제라,
복제되는 상태 변수의 `OnRep`에서 **모든 머신이** 물리를 끄도록 바꿨다.

```cpp
void ADrone::OnRep_FlightActive()
{
    if (bFlightActive && IsValid(EquipmentMesh))
    {
        EquipmentMesh->SetSimulatePhysics(false);
        EquipmentMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        EquipmentMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    }
    // ...
}
// 서버는 OnRep 을 받지 않으므로 값을 바꾼 직후 직접 호출한다
```
