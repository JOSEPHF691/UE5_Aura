// Copyright yosep
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraElementalSpell.generated.h"
class AAuraCharacter;
class AAuraEnemy;
class UNiagaraSystem;
class UNiagaraComponent;
UENUM()
enum class EAuraElementalZone : uint8 { Echo, Storm, Judgment };

/** Bounded server-owned follow-up waves/fields; clients reconstruct persistent previews. */
UCLASS()
class AURA_API AAuraElementalSpell : public AActor
{
	GENERATED_BODY()
public:
	AAuraElementalSpell();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void Configure(AAuraCharacter* Source, EAuraElementalZone InMode, float InDamage, float InRadius, int32 InCastId);
	EAuraElementalZone GetMode() const { return Mode; }
	float GetRadius() const { return Radius; }
	FVector GetPendingStrike() const { return PendingStrike; }
	bool HasPendingStrike() const { return bPendingStrike; }
	static void ApplyQ(AAuraCharacter* Source, const FVector& Center, float Damage, float Radius, bool bCrystal);
	static void ApplyLightning(AAuraCharacter* Source, AAuraEnemy* Target, float Damage, int32 CastId);
protected:
	virtual void BeginPlay() override;
private:
	void Step();
	UFUNCTION() void RefreshVisuals();
	bool GroundPoint(const FVector& Desired, FVector& OutPoint) const;
	UFUNCTION(NetMulticast, Unreliable) void MulticastCue(FVector Center, float CueRadius, bool bLightning);
	UPROPERTY(Replicated) TObjectPtr<AAuraCharacter> Caster;
	UPROPERTY(ReplicatedUsing=RefreshVisuals) EAuraElementalZone Mode = EAuraElementalZone::Echo;
	UPROPERTY(ReplicatedUsing=RefreshVisuals) float Radius = 300.f;
	UPROPERTY(Replicated) FVector PendingStrike = FVector::ZeroVector;
	UPROPERTY(Replicated) bool bPendingStrike = false;
	UPROPERTY() TObjectPtr<UNiagaraSystem> CrystalEffect;
	UPROPERTY() TObjectPtr<UNiagaraSystem> LightningEffect;
	UPROPERTY() TObjectPtr<UNiagaraSystem> BeamEffect;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> FieldVisuals;
	float Damage = 0.f;
	int32 CastId = 0;
	int32 StepIndex = 0;
	FTimerHandle StepTimer;
	TSet<TWeakObjectPtr<AAuraEnemy>> SelectedTargets;
};
