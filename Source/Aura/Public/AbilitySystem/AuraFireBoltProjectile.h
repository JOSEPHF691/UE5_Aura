// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "AuraFireBoltProjectile.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USoundBase;
class USphereComponent;
class AAuraEnemy;

UCLASS()
class AURA_API AAuraFireBoltProjectile : public AActor
{
	GENERATED_BODY()

public:
	AAuraFireBoltProjectile();

	void SetDamage(float InDamage) { Damage = InDamage; }
	void SetComboCastId(int32 InCastId) { ComboCastId = InCastId; }
	void SetExtraPierces(int32 InCount) { ExtraPiercesRemaining = FMath::Max(0, InCount); }
	bool IsOrbitHolding() const { return bOrbitHolding; }
	void SetPoiseDamageScale(float InScale) { PoiseDamageScale = FMath::Max(0.f, InScale); }
	void SetHomingTarget(AAuraEnemy* InTarget);
	/** Three visible hovering bolts are spawned together, then released in sequence. */
	void ConfigureOrbitalRelease(int32 InIndex, float InDelay, AAuraEnemy* InTarget,
		const FVector& InFallbackDirection);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION()
	void OnRep_HomingTarget();
	UFUNCTION()
	void OnRep_OrbitHolding();
	void ReleaseOrbitalShot();
	void RefreshHomingState();
	void RefreshOrbitState();

	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnSphereHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	void SpawnImpactEffects();

	UPROPERTY(VisibleAnywhere, Category = "Aura|Projectile")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, Category = "Aura|Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(VisibleAnywhere, Category = "Aura|Projectile")
	TObjectPtr<UNiagaraComponent> ProjectileEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Projectile")
	TObjectPtr<UNiagaraSystem> ImpactEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Projectile")
	TObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(EditAnywhere, Category = "Aura|Projectile")
	float Damage = 15.f;

private:
	bool bImpacted = false;
	int32 ComboCastId = 0;
	int32 ExtraPiercesRemaining = 0;
	float PoiseDamageScale = 1.f;
	UPROPERTY(ReplicatedUsing = OnRep_HomingTarget)
	TObjectPtr<AAuraEnemy> HomingTarget;
	UPROPERTY(ReplicatedUsing = OnRep_OrbitHolding)
	bool bOrbitHolding = false;
	int32 OrbitIndex = 0;
	float OrbitReleaseDelay = 0.f;
	FVector FallbackShotDirection = FVector::ForwardVector;
	FTimerHandle OrbitReleaseTimer;
	TSet<TWeakObjectPtr<AActor>> HitActors;
};
