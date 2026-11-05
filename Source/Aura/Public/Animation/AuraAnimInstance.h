// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "AuraAnimInstance.generated.h"

class UBlendSpace;

USTRUCT()
struct AURA_API FAuraAnimInstanceProxy : public FAnimSingleNodeInstanceProxy
{
	GENERATED_BODY()

	FAuraAnimInstanceProxy();
	explicit FAuraAnimInstanceProxy(UAnimInstance* InAnimInstance);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	float SlotNodeWeight = 0.f;
	float SourceWeight = 1.f;
	float TotalNodeWeight = 0.f;
};

/** Cook-safe BlendSpace locomotion with a native DefaultSlot montage overlay. */
UCLASS(Transient, Blueprintable)
class AURA_API UAuraAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	void SetMovementBlendSpace(UBlendSpace* InBlendSpace);
	float GetDefaultSlotWeight() const;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Aura|Animation")
	float GroundSpeed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Aura|Animation")
	bool bIsInAir = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Aura|Animation")
	TObjectPtr<UBlendSpace> MovementBlendSpace;

protected:
	virtual void Montage_Advance(float DeltaTime) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	friend struct FAuraAnimInstanceProxy;
};
