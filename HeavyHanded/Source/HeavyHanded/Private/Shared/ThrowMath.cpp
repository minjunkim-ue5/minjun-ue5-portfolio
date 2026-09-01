#include "Shared/ThrowMath.h"

#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace HHThrow
{

FVector ComputeAimDirection(const AActor* Thrown, const APawn* Carrier)
{
	const UWorld* World = IsValid(Thrown) ? Thrown->GetWorld() : nullptr;
	if (!IsValid(Carrier) || !World)
	{
		return FVector::ZeroVector;
	}

	// 카메라 시점. 컨트롤러가 있으면 실제 카메라를, 없으면 폰의 눈 위치를 쓴다.
	FVector ViewLocation;
	FRotator ViewRotation;
	if (const AController* CarrierController = Carrier->GetController())
	{
		CarrierController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		Carrier->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const FVector ViewDirection = ViewRotation.Vector();

	// 화면 중앙이 가리키는 지점을 찾는다.
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(HHThrowAim), false, Thrown);

	// 들고 있는 물건은 카메라 바로 앞에 있어서 반드시 먼저 걸린다. 던진 사람도 뺀다.
	TraceParams.AddIgnoredActor(Carrier);

	FVector AimPoint = ViewLocation + ViewDirection * AimTraceDistance;

	FHitResult AimHit;
	if (World->LineTraceSingleByChannel(AimHit, ViewLocation, AimPoint, ECC_Visibility, TraceParams))
	{
		AimPoint = AimHit.ImpactPoint;
	}

	// 벽에 바짝 붙으면 조준점이 발사점보다 뒤에 놓여 엉뚱한 방향이 나온다.
	// 그때는 시선 방향을 그대로 쓴다.
	const FVector ToAimPoint = AimPoint - Thrown->GetActorLocation();
	if (ToAimPoint.SizeSquared() < FMath::Square(MinAimDistance))
	{
		return ViewDirection;
	}

	return ToAimPoint.GetSafeNormal();
}

FVector ComputeVelocity(const FVector& AimDirection, const FThrowParams& Params, const APawn* Carrier)
{
	const FVector Aim = AimDirection.GetSafeNormal();
	if (Aim.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	// 조준 방향에 위쪽 성분을 섞어 포물선을 만든다.
	const FVector LaunchDirection = (Aim + FVector::UpVector * Params.UpwardRatio).GetSafeNormal();

	FVector Velocity = LaunchDirection * Params.Speed;

	// 운반자의 이동 속도를 얼마나 섞을지는 데이터가 정한다. 기본은 0 이다.
	// 1:1 로 더하면 이동 속도가 Speed 와 비슷할 때 조준이 무의미해진다.
	if (Params.CarrierVelocityInfluence > 0.f && IsValid(Carrier))
	{
		Velocity += Carrier->GetVelocity() * Params.CarrierVelocityInfluence;
	}

	return Velocity;
}

bool PredictPath(const AActor* Thrown, const APawn* Carrier, const FVector& AimDirection,
	const FThrowParams& Params, float ProjectileRadius, FPredictProjectilePathResult& OutResult)
{
	if (!IsValid(Thrown))
	{
		return false;
	}

	const FVector LaunchDirection = AimDirection.GetSafeNormal();
	if (LaunchDirection.IsNearlyZero())
	{
		return false;
	}

	FPredictProjectilePathParams PathParams;

	// 실제 던지기와 같은 출발점·속도를 써야 미리 보이는 궤적이 맞는다.
	PathParams.StartLocation = Thrown->GetActorLocation() + LaunchDirection * Params.Clearance;
	PathParams.LaunchVelocity = ComputeVelocity(AimDirection, Params, Carrier);
	PathParams.ProjectileRadius = ProjectileRadius;

	PathParams.bTraceWithCollision = true;
	PathParams.bTraceWithChannel = true;
	PathParams.TraceChannel = ECollisionChannel::ECC_WorldStatic;

	// 자기 자신과 던지는 사람은 궤적에서 빼야 조준선이 발밑에서 끊기지 않는다.
	PathParams.ActorsToIgnore.Add(const_cast<AActor*>(Thrown));
	if (IsValid(Carrier))
	{
		PathParams.ActorsToIgnore.Add(const_cast<APawn*>(Carrier));
	}

	PathParams.MaxSimTime = 3.f;
	PathParams.SimFrequency = 15.f;

	return UGameplayStatics::PredictProjectilePath(Thrown, PathParams, OutResult);
}

void Launch(AActor* Thrown, UPrimitiveComponent* Body, const FVector& AimDirection,
	const FThrowParams& Params, const APawn* Carrier)
{
	if (!IsValid(Thrown) || !IsValid(Body))
	{
		return;
	}

	const FVector LaunchVelocity = ComputeVelocity(AimDirection, Params, Carrier);
	const FVector LaunchDirection = AimDirection.GetSafeNormal();

	// 손 소켓은 던진 사람 캡슐과 겹쳐 있다. 겹친 채로 물리를 켜면 물리 엔진이
	// 침투를 해소하느라 자기가 던진 물건에 튕겨 나간다. 먼저 간격을 만든다.
	if (Params.Clearance > 0.f && !LaunchDirection.IsNearlyZero())
	{
		Thrown->SetActorLocation(Thrown->GetActorLocation() + LaunchDirection * Params.Clearance,
			/*bSweep=*/false);
	}

	// 임펄스 = 질량 x 목표 속도.
	// 질량을 곱해야 무게와 무관하게 데이터에 적은 Speed 그대로 나간다.
	// 무거운 물건이 덜 날아가는 것은 Speed 값으로 표현한다. (값은 데이터, 행동은 공통)
	Body->AddImpulse(LaunchVelocity * Body->GetMass());

	// 회전이 없으면 물건이 미끄러지듯 날아가 던진 느낌이 안 난다.
	// 조준 방향 기준 오른쪽 축으로 굴린다. 위/아래로 똑바로 던지면 축이 0 이라 회전은 생략된다.
	if (Params.SpinSpeed > 0.f)
	{
		const FVector SpinAxis =
			FVector::CrossProduct(LaunchDirection, FVector::UpVector).GetSafeNormal();
		if (!SpinAxis.IsNearlyZero())
		{
			Body->SetPhysicsAngularVelocityInDegrees(SpinAxis * Params.SpinSpeed);
		}
	}
}

void DrawTrajectory(const AActor* Thrown, const APawn* Carrier, const FVector& AimDirection,
	const FThrowParams& Params, float ProjectileRadius, float Duration)
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = IsValid(Thrown) ? Thrown->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	FPredictProjectilePathResult Result;

	// 아무데도 맞지 않아도 경로 자체는 그린다. 반환값은 충돌 여부일 뿐이다.
	PredictPath(Thrown, Carrier, AimDirection, Params, ProjectileRadius, Result);

	for (int32 Index = 1; Index < Result.PathData.Num(); ++Index)
	{
		DrawDebugLine(World,
			Result.PathData[Index - 1].Location, Result.PathData[Index].Location,
			FColor::Cyan, false, Duration, 0, 2.f);
	}

	// 예측한 착탄 지점. 실제로 여기 떨어지는지 보면 된다.
	if (Result.HitResult.bBlockingHit)
	{
		DrawDebugSphere(World, Result.HitResult.ImpactPoint, 20.f, 12, FColor::Cyan, false, Duration);
	}
#endif
}

}   // namespace HHThrow
