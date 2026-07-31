#include "HexAttackProjectile.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HexGridActor.h"
#include "HexUnitActor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

AHexAttackProjectile::AHexAttackProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	ProjectileMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMeshComponent->SetupAttachment(SceneRoot);
	ProjectileMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProjectileMeshComponent->SetGenerateOverlapEvents(false);
	ProjectileMeshComponent->SetVisibility(false);

	ProjectileVFXComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileVFX"));
	ProjectileVFXComponent->SetupAttachment(SceneRoot);
	ProjectileVFXComponent->SetAutoActivate(false);

	SetActorEnableCollision(false);
}

void AHexAttackProjectile::InitializeProjectile(
	AHexGridActor* InOwningGrid,
	AHexUnitActor* InAttacker,
	AHexUnitActor* InTarget,
	UStaticMesh* InProjectileMesh,
	UNiagaraSystem* InProjectileVFX,
	UNiagaraSystem* InImpactVFX,
	FName InTargetSocketName,
	const FVector& InFallbackTargetLocation,
	const FVector& InTargetOffset,
	float InSpeed,
	float InArrivalRadius,
	float InMaxLifetime,
	float InProjectileScale,
	float InImpactScale,
	const FRotator& InRotationOffset
)
{
	OwningGrid = InOwningGrid;
	Attacker = InAttacker;
	Target = InTarget;
	ImpactVFX = InImpactVFX;
	TargetSocketName = InTargetSocketName;
	FallbackTargetLocation = InFallbackTargetLocation;
	TargetOffset = InTargetOffset;
	RotationOffset = InRotationOffset;

	Speed = FMath::Max(1.0f, InSpeed);
	ArrivalRadius = FMath::Max(1.0f, InArrivalRadius);
	MaxLifetime = FMath::Max(0.1f, InMaxLifetime);
	ImpactScale = FMath::Max(0.01f, InImpactScale);
	ElapsedLifetime = 0.0f;
	bCompleted = false;

	const float SafeProjectileScale = FMath::Max(0.01f, InProjectileScale);

	if (ProjectileMeshComponent)
	{
		ProjectileMeshComponent->SetStaticMesh(InProjectileMesh);
		ProjectileMeshComponent->SetRelativeScale3D(FVector(SafeProjectileScale));
		ProjectileMeshComponent->SetVisibility(InProjectileMesh != nullptr, true);
	}

	if (ProjectileVFXComponent)
	{
		ProjectileVFXComponent->SetAsset(InProjectileVFX);
		ProjectileVFXComponent->SetRelativeScale3D(FVector(SafeProjectileScale));

		if (InProjectileVFX)
		{
			ProjectileVFXComponent->Activate(true);
		}
		else
		{
			ProjectileVFXComponent->Deactivate();
		}
	}

	const FVector InitialDirection = ResolveTargetLocation() - GetActorLocation();
	if (!InitialDirection.IsNearlyZero())
	{
		SetActorRotation(InitialDirection.Rotation() + RotationOffset);
	}
}

void AHexAttackProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCompleted)
	{
		return;
	}

	ElapsedLifetime += FMath::Max(0.0f, DeltaSeconds);

	const FVector CurrentLocation = GetActorLocation();
	const FVector TargetLocation = ResolveTargetLocation();
	const FVector ToTarget = TargetLocation - CurrentLocation;
	const float Distance = ToTarget.Size();
	const float TravelDistance = Speed * FMath::Max(0.0f, DeltaSeconds);

	if (Distance <= ArrivalRadius || TravelDistance >= Distance || ElapsedLifetime >= MaxLifetime)
	{
		SetActorLocation(TargetLocation);
		CompleteProjectile();
		return;
	}

	if (Distance > KINDA_SMALL_NUMBER)
	{
		const FVector Direction = ToTarget / Distance;
		SetActorLocation(CurrentLocation + Direction * TravelDistance);
		SetActorRotation(Direction.Rotation() + RotationOffset);
	}
}

FVector AHexAttackProjectile::ResolveTargetLocation() const
{
	if (IsValid(Target))
	{
		if (Target->UnitMesh
			&& !TargetSocketName.IsNone()
			&& (Target->UnitMesh->DoesSocketExist(TargetSocketName) || Target->UnitMesh->GetBoneIndex(TargetSocketName) != INDEX_NONE))
		{
			return Target->UnitMesh->GetSocketLocation(TargetSocketName) + TargetOffset;
		}

		const FVector MeshCenter = Target->UnitMesh ? Target->UnitMesh->Bounds.Origin : Target->GetActorLocation();
		return MeshCenter + TargetOffset;
	}

	return FallbackTargetLocation;
}

void AHexAttackProjectile::CompleteProjectile()
{
	if (bCompleted)
	{
		return;
	}

	bCompleted = true;
	SetActorTickEnabled(false);

	if (ProjectileVFXComponent)
	{
		ProjectileVFXComponent->Deactivate();
	}

	if (ImpactVFX && GetWorld())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			ImpactVFX,
			GetActorLocation(),
			GetActorRotation(),
			FVector(ImpactScale),
			true,
			true
		);
	}

	if (IsValid(OwningGrid))
	{
		OwningGrid->HandleAttackProjectileImpact(this, Attacker, Target);
	}

	Destroy();
}
