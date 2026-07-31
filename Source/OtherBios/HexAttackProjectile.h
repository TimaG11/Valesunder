#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexAttackProjectile.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UNiagaraComponent;
class UNiagaraSystem;
class AHexGridActor;
class AHexUnitActor;

UCLASS()
class OTHERBIOS_API AHexAttackProjectile : public AActor
{
	GENERATED_BODY()

public:
	AHexAttackProjectile();

	virtual void Tick(float DeltaSeconds) override;

	void InitializeProjectile(
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
	);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	USceneComponent* SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	UStaticMeshComponent* ProjectileMeshComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	UNiagaraComponent* ProjectileVFXComponent = nullptr;

private:
	UPROPERTY()
	AHexGridActor* OwningGrid = nullptr;

	UPROPERTY()
	AHexUnitActor* Attacker = nullptr;

	UPROPERTY()
	AHexUnitActor* Target = nullptr;

	UPROPERTY()
	UNiagaraSystem* ImpactVFX = nullptr;

	FName TargetSocketName = NAME_None;
	FVector FallbackTargetLocation = FVector::ZeroVector;
	FVector TargetOffset = FVector::ZeroVector;
	FRotator RotationOffset = FRotator::ZeroRotator;

	float Speed = 1200.0f;
	float ArrivalRadius = 25.0f;
	float MaxLifetime = 5.0f;
	float ImpactScale = 1.0f;
	float ElapsedLifetime = 0.0f;
	bool bCompleted = false;

	FVector ResolveTargetLocation() const;
	void CompleteProjectile();
};
