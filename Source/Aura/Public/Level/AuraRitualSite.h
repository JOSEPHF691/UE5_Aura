// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraRitualSite.generated.h"

class AAuraCharacter;
class AAuraDungeonGate;
class AAuraEncounterVolume;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EAuraRitualSiteType : uint8
{
	BranchSeal,
	BossAltar
};

/** Shared-quest seal or boss altar. The GameState owns progress, not this actor or a backpack. */
UCLASS()
class AURA_API AAuraRitualSite : public AActor
{
	GENERATED_BODY()

public:
	AAuraRitualSite();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	bool CanInteract(const AAuraCharacter* Character, FString& OutFailureReason) const;
	/** Whether this seal/altar can make progress now, independent of player distance. */
	bool IsReadyForInteraction() const;
	bool TryInteract(AAuraCharacter* Character, FString& OutFailureReason);
	void RefreshBossGate();
	bool IsBossAltar() const { return SiteType == EAuraRitualSiteType::BossAltar; }
	AAuraDungeonGate* GetBossGate() const { return BossGate; }
	FName GetSealId() const { return SealId; }
	float GetInteractionRadius() const { return InteractionRadius; }

private:
	void RefreshVisuals();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayRitualCue();

	UPROPERTY(EditInstanceOnly, Category = "Aura|Ritual")
	EAuraRitualSiteType SiteType = EAuraRitualSiteType::BranchSeal;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Ritual")
	FName SealId = NAME_None;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Ritual")
	TObjectPtr<AAuraEncounterVolume> PrerequisiteEncounter;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Ritual")
	TObjectPtr<AAuraEncounterVolume> BossEncounter;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Ritual")
	TObjectPtr<AAuraDungeonGate> BossGate;
	UPROPERTY(EditAnywhere, Category = "Aura|Ritual", meta = (ClampMin = "100.0"))
	float InteractionRadius = 265.f;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Pedestal;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> RuneCore;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> InteractionLabel;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> RitualLight;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> ActivationEffect;
	bool bCachedActive = false;
	bool bCachedReady = false;
};
