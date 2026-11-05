// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraArcaneSlowField.generated.h"

class AAuraCharacter;

/** Replicated, short-lived Q Rank 3 zone. Only the server applies the refreshable slow. */
UCLASS()
class AURA_API AAuraArcaneSlowField : public AActor
{
	GENERATED_BODY()

public:
	AAuraArcaneSlowField();
	void Configure(AAuraCharacter* InCaster, float InRadius, float InDuration);
	float GetRadius() const { return Radius; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	void PulseSlow();

	UPROPERTY(Replicated)
	float Radius = 260.f;

	UPROPERTY()
	TObjectPtr<AAuraCharacter> Caster;

	FTimerHandle PulseTimer;
};
