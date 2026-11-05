// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "AuraAIController.generated.h"

class AAuraCharacter;
class UAIPerceptionComponent;
class UAISenseConfig_Sight;

UENUM(BlueprintType)
enum class EAuraAIState : uint8
{
	Idle,
	Chasing,
	Attacking,
	Retreating,
	Returning,
	Stunned,
	Dead
};

UCLASS()
class AURA_API AAuraAIController : public AAIController
{
	GENERATED_BODY()

public:
	AAuraAIController();

	virtual void Tick(float DeltaSeconds) override;

	AAuraCharacter* GetCombatTarget() const { return CombatTarget.Get(); }
	EAuraAIState GetAIState() const { return AIState; }
	const FVector& GetHomeLocation() const { return HomeLocation; }
	bool HasClearRangedShot(const class AAuraEnemy* Enemy,
		const AAuraCharacter* Target) const;
	/** Deterministic smoke-test hook; runtime acquisition still uses perception. */
	void SetCombatTargetForTest(AAuraCharacter* NewTarget) { SetCombatTarget(NewTarget); }

protected:
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

private:
	void ApplyCombatBalance();
	void AcquirePlayerTarget();
	void SetCombatTarget(AAuraCharacter* NewTarget);
	void SetAIState(EAuraAIState NewState);
	void ReturnHome();
	void HandleRangedCombat(class AAuraEnemy* Enemy, AAuraCharacter* Target,
		float DistanceToTarget, float DeltaSeconds);
	void HandleBossCombat(class AAuraEnemy* Enemy, AAuraCharacter* Target,
		float DistanceToTarget, float DeltaSeconds);
	void RequestRangedRetreat(class AAuraEnemy* Enemy, const AAuraCharacter* Target);

	UPROPERTY(VisibleAnywhere, Category = "Aura|AI")
	TObjectPtr<UAIPerceptionComponent> AuraPerception;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	TWeakObjectPtr<AAuraCharacter> CombatTarget;
	FVector HomeLocation = FVector::ZeroVector;
	EAuraAIState AIState = EAuraAIState::Idle;
	float NextMoveRequestTime = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|AI")
	float DetectionRadius = 1200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|AI")
	float LoseTargetRadius = 1550.f;

	/** Legacy name kept for existing balance assets; limits pursuit by player distance. */
	UPROPERTY(EditDefaultsOnly, Category = "Aura|AI")
	float LeashRadius = 1850.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|AI")
	float AttackRange = 190.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|AI")
	float HomeAcceptanceRadius = 45.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|AI")
	float MoveRequestInterval = 0.22f;
};
