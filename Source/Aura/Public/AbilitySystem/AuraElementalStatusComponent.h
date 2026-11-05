// Copyright yosep
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AuraElementalStatusComponent.generated.h"

class AAuraCharacter;
class AAuraEnemy;
class UStaticMeshComponent;
class UStaticMesh;
class UNiagaraComponent;
class UNiagaraSystem;

USTRUCT()
struct FAuraElementalStatus
{
	GENERATED_BODY()
	UPROPERTY() float SlowFraction = 0.f;
	UPROPERTY() float SlowUntil = 0.f;
	UPROPERTY() float IgniteUntil = 0.f;
	UPROPERTY() float ParalysisUntil = 0.f;
	UPROPERTY() float CrystalHealth = 0.f;
	UPROPERTY() float CrystalMaxHealth = 0.f;
	UPROPERTY() float CrystalUntil = 0.f;
	UPROPERTY() float ControlProtectedUntil = 0.f;
	UPROPERTY() bool bHasEmber = false;
};

/** Enemy-owned, shared control resistance and server-authoritative elemental state. */
UCLASS(ClassGroup=(Aura))
class AURA_API UAuraElementalStatusComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAuraElementalStatusComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	const FAuraElementalStatus& GetStatus() const { return State; }
	float GetSlowFraction() const;
	bool HasCrystal() const { return State.CrystalHealth > 0.f; }
	bool CanAcceptHardControl() const;
	void NotifyHardControl(float Duration);
	void ApplySlow();
	bool ApplyCrystal(AAuraCharacter* Source, float BaseDamage);
	void OnFireHit(AAuraCharacter* Source, int32 CastId, float Damage);
	void OnLightningHit(AAuraCharacter* Source, int32 CastId, float Damage, bool bApplyDirectDamage = false);
	void OnDirectDamage(float Damage);
	void ClearAll();
	static bool DealDamage(AAuraCharacter* Source, AAuraEnemy* Target, float Damage, bool bSecondary = false);
	static TArray<AAuraEnemy*> GatherTargets(UWorld* World, const FVector& Center, float Radius);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void OnRep_State();
	AAuraEnemy* Enemy() const;
	float Now() const;
	void ReleaseCrystal(bool bBroken);
	void Publish();
	UPROPERTY(ReplicatedUsing=OnRep_State) FAuraElementalStatus State;
	UPROPERTY() TObjectPtr<UStaticMesh> CrystalMeshAsset;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CrystalVisual;
	UPROPERTY() TObjectPtr<UNiagaraSystem> IgniteEffect;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> IgniteVisual;
	struct FSourceState { float EmberUntil = 0.f; TSet<int32> FireCasts; TSet<int32> ShockCasts; };
	TMap<TWeakObjectPtr<AAuraCharacter>, FSourceState> Sources;
	TWeakObjectPtr<AAuraCharacter> CrystalSource;
	TWeakObjectPtr<AAuraCharacter> IgniteSource;
	TWeakObjectPtr<AAuraCharacter> ParalysisSource;
	float CrystalBurstDamage = 0.f;
	float IgniteDamage = 0.f;
	float NextIgniteAt = 0.f;
	float NextParalysisAt = 0.f;
};
