// Copyright yosep

#include "Editor/AuraEditorAssetLibrary.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"

bool UAuraEditorAssetLibrary::EnsureSkeletalMeshSocket(
	USkeletalMesh* SkeletalMesh,
	FName SocketName,
	FName BoneName,
	FVector RelativeLocation,
	FRotator RelativeRotation,
	FVector RelativeScale)
{
#if WITH_EDITOR
	if (!IsValid(SkeletalMesh) || SocketName.IsNone() || BoneName.IsNone())
	{
		return false;
	}

	SkeletalMesh->Modify();
	USkeletalMeshSocket* Socket = SkeletalMesh->FindSocket(SocketName);
	if (!Socket)
	{
		Socket = NewObject<USkeletalMeshSocket>(SkeletalMesh);
		Socket->SocketName = SocketName;
		SkeletalMesh->AddSocket(Socket, false);
	}

	Socket->Modify();
	Socket->BoneName = BoneName;
	Socket->RelativeLocation = RelativeLocation;
	Socket->RelativeRotation = RelativeRotation;
	Socket->RelativeScale = RelativeScale;
	SkeletalMesh->MarkPackageDirty();
	SkeletalMesh->PostEditChange();
	return true;
#else
	return false;
#endif
}

bool UAuraEditorAssetLibrary::SetNiagaraEmittersLocalSpace(
	UNiagaraSystem* NiagaraSystem,
	const bool bLocalSpace)
{
#if WITH_EDITOR
	if (!IsValid(NiagaraSystem)) return false;

	NiagaraSystem->Modify();
	int32 ValidEmitterCount = 0;
	int32 ChangedEmitterCount = 0;
	for (FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!EmitterData) continue;

		++ValidEmitterCount;
		if (EmitterData->bLocalSpace == bLocalSpace) continue;

		const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
		if (Instance.Emitter) Instance.Emitter->Modify();
		EmitterData->bLocalSpace = bLocalSpace;
		if (Instance.Emitter)
		{
			Instance.Emitter->MarkPackageDirty();
			Instance.Emitter->PostEditChange();
		}
		++ChangedEmitterCount;
	}

	if (ValidEmitterCount <= 0) return false;
	if (ChangedEmitterCount > 0)
	{
		NiagaraSystem->RequestCompile(true);
		NiagaraSystem->MarkPackageDirty();
		NiagaraSystem->PostEditChange();
	}
	UE_LOG(LogTemp, Display,
		TEXT("Aura Niagara local-space setup. System=%s Emitters=%d Changed=%d LocalSpace=%s"),
		*NiagaraSystem->GetPathName(), ValidEmitterCount, ChangedEmitterCount,
		bLocalSpace ? TEXT("true") : TEXT("false"));
	return true;
#else
	return false;
#endif
}
