// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "AuraDamageGameplayEffect.generated.h"

/** Instant health reduction driven by the Data.Damage SetByCaller magnitude. */
UCLASS()
class AURA_API UAuraDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraDamageGameplayEffect();
};
