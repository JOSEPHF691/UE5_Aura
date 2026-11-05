// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "AuraAbilityGameplayEffects.generated.h"

/** Shared SetByCaller mana cost used by every player attack. */
UCLASS()
class AURA_API UAuraManaCostEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraManaCostEffect();
};

/** Deprecated compatibility aliases retained for existing references. */
UCLASS()
class AURA_API UAuraFireBoltCostEffect : public UAuraManaCostEffect
{
	GENERATED_BODY()
};

/** Standard GAS cooldown for FireBolt. */
UCLASS()
class AURA_API UAuraFireBoltCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraFireBoltCooldownEffect(const FObjectInitializer& ObjectInitializer);
};

/** Standard GAS mana cost for Arcane Shards. */
UCLASS()
class AURA_API UAuraArcaneShardsCostEffect : public UAuraManaCostEffect
{
	GENERATED_BODY()
};

/** Standard GAS cooldown for Arcane Shards. */
UCLASS()
class AURA_API UAuraArcaneShardsCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraArcaneShardsCooldownEffect(const FObjectInitializer& ObjectInitializer);
};

/** Standard GAS mana cost for Shock. */
UCLASS()
class AURA_API UAuraShockCostEffect : public UAuraManaCostEffect
{
	GENERATED_BODY()
};

/** Standard GAS cooldown for Shock. */
UCLASS()
class AURA_API UAuraShockCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraShockCooldownEffect(const FObjectInitializer& ObjectInitializer);
};

/** Duration effect that owns State.Stunned on the affected enemy. */
UCLASS()
class AURA_API UAuraShockStunEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraShockStunEffect(const FObjectInitializer& ObjectInitializer);
};

/** Short refreshable slow tag applied while an enemy remains in the Q rank-three field. */
UCLASS()
class AURA_API UAuraArcaneSlowEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraArcaneSlowEffect(const FObjectInitializer& ObjectInitializer);
};

/** Shroom spore debuff on a player; a later enemy special may consume the tag. */
UCLASS()
class AURA_API UAuraSporeExposureEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraSporeExposureEffect(const FObjectInitializer& ObjectInitializer);
};

/** Standard GAS cooldown for the elite Shaman's telegraphed projectile. */
UCLASS()
class AURA_API UAuraShamanBoltCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraShamanBoltCooldownEffect(const FObjectInitializer& ObjectInitializer);
};

/** Instant GAS restore used by the reusable health pickup. */
UCLASS()
class AURA_API UAuraHealthPickupEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraHealthPickupEffect();
};

/** Instant GAS restore used by the reusable mana pickup. */
UCLASS()
class AURA_API UAuraManaPickupEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraManaPickupEffect();
};

/** One infinite instance per equipped item; every modifier is supplied by Item.Stat tags. */
UCLASS()
class AURA_API UAuraEquipmentAttributeEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraEquipmentAttributeEffect();
};
