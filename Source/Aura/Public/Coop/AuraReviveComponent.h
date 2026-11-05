// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AuraReviveComponent.generated.h"

class AAuraCharacter;
class UGameplayAbility;

/** One server-owned rescue attempt. Replicate the identity and start time together. */
USTRUCT()
struct FAuraRescueChannel
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<AAuraCharacter> Rescuer;
	UPROPERTY() float StartServerTime = 0.f;
	UPROPERTY() int32 RequestId = 0;
};

/** Co-op downed state, authoritative rescue channel, and short get-up protection. */
UCLASS(ClassGroup = (Aura), meta = (BlueprintSpawnableComponent))
class AURA_API UAuraReviveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAuraReviveComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	static constexpr float RescueDuration = 5.f;
	static constexpr float RescueRadius = 200.f;
	static constexpr float ReviveVitalFraction = 0.2f;
	static constexpr float GetUpProtectionDuration = 2.f;

	bool IsWaitingForRescue() const { return bWaitingForRescue; }
	bool IsRescuing() const { return RescueTarget != nullptr; }
	AAuraCharacter* GetRescueTarget() const { return RescueTarget; }
	AAuraCharacter* GetRescuer() const { return Channel.Rescuer; }
	const FTransform& GetRescueTransform() const { return RescueTransform; }
	const FTransform& GetLastSafeTransform() const { return LastSafeTransform; }
	float GetRescueProgress() const;
	bool IsProtected() const;
	bool CanRescue(const AAuraCharacter* Target) const;

	void EnterDownedState();
	void ResetAfterRevive(float ProtectionDuration);
	bool TryBeginRescue(AAuraCharacter* Target, int32 RequestId);
	void CancelRescue(int32 RequestId = MAX_int32);
	void CancelAllRescueLinks();
	void EndProtection();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AAuraCharacter* GetCharacter() const;
	float ServerTime() const;
	void OnAbilityActivated(UGameplayAbility* Ability);
	void ClearActiveRescue();

	UPROPERTY(Replicated) bool bWaitingForRescue = false;
	UPROPERTY(Replicated) FTransform RescueTransform;
	UPROPERTY(Replicated) FAuraRescueChannel Channel;
	UPROPERTY(Replicated) TObjectPtr<AAuraCharacter> RescueTarget;
	UPROPERTY(Replicated) float ProtectionEndServerTime = 0.f;

	FTransform LastSafeTransform;
	FVector RescueStartLocation = FVector::ZeroVector;
	int32 LastRequestId = 0;
	int32 ActiveRequestId = 0;
};
