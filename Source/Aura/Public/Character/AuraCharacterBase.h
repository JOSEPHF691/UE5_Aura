// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "AuraCharacterBase.generated.h"

class UAuraAttributeSet;
class UAbilitySystemComponent;

UCLASS()
class AURA_API AAuraCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AAuraCharacterBase();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFUNCTION(BlueprintPure, Category = "Aura|Abilities")
	UAuraAttributeSet* GetAuraAttributeSet() const { return AttributeSet; }

	UFUNCTION(BlueprintPure, Category = "Aura|Combat")
	USkeletalMeshComponent* GetWeaponMesh() const { return Weapon; }

	/** Player characters override this to switch their passive resource economy into combat mode. */
	virtual void NotifyCombatActivity() {}

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere , Category = "Combat")
	TObjectPtr<USkeletalMeshComponent> Weapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Attributes")
	TObjectPtr<UAuraAttributeSet> AttributeSet;

};
