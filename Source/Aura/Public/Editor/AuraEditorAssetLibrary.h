// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AuraEditorAssetLibrary.generated.h"

class USkeletalMesh;
class UNiagaraSystem;

/** Small project-owned asset setup helpers used by repeatable editor automation. */
UCLASS()
class AURA_API UAuraEditorAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Aura|Editor Assets")
	static bool EnsureSkeletalMeshSocket(
		USkeletalMesh* SkeletalMesh,
		FName SocketName,
		FName BoneName,
		FVector RelativeLocation,
		FRotator RelativeRotation,
		FVector RelativeScale);

	/** Makes every emitter simulate relative to its attached Niagara component. */
	UFUNCTION(BlueprintCallable, Category = "Aura|Editor Assets")
	static bool SetNiagaraEmittersLocalSpace(
		UNiagaraSystem* NiagaraSystem,
		bool bLocalSpace);
};
