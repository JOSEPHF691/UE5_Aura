// Copyright yosep

#include "Level/AuraEncounterVolume.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "EngineUtils.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Engine/World.h"
#include "Game/AuraGameState.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Level/AuraDungeonGate.h"
#include "Level/AuraLootChest.h"
#include "Level/AuraTreasureChest.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/AuraLootSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraEncounter, Log, All);

namespace
{
	struct FM22RouteRewardCard
	{
		const TCHAR* Id;
		int32 Experience;
		int32 Souls;
	};

	// First-clear targets include the room's normal kill rewards. Missing spine
	// rooms stay in the ledger but are never paid by an unrelated encounter.
	constexpr FM22RouteRewardCard M22RouteRewardCards[] = {
		{TEXT("M22.OuterGuard"), 90, 65},
		{TEXT("M22.OuterPractice"), 150, 110},
		{TEXT("M22.UpperShaman"), 180, 140},
		{TEXT("M22.UpperCrossroads"), 120, 100},
		{TEXT("M22.MiddleInterlock"), 120, 115},
		{TEXT("M22.DeepHallPincer"), 210, 165},
		{TEXT("M22.CorePrep"), 170, 140},
		{TEXT("M22.CorePractice"), 180, 145},
		{TEXT("LowerNorthBoonGuardians"), 170, 110},
		{TEXT("UpperSouthBoonGuardian"), 180, 125},
		{TEXT("LowerSouthLootAmbush"), 190, 135},
		{TEXT("UpperNorthRiskGuardians"), 240, 175},
	};

	const FM22RouteRewardCard* FindM22RouteRewardCard(const FName Id)
	{
		for (const FM22RouteRewardCard& Card : M22RouteRewardCards)
		{
			if (Id == FName(Card.Id)) return &Card;
		}
		return nullptr;
	}

	bool TraceWalkableFloor(const UWorld* World, const AActor* IgnoreActor,
		const FVector& HorizontalPosition, const float ReferenceZ, float& OutFloorZ)
	{
		if (!World) return false;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AuraEncounterFloor), false, IgnoreActor);
		const FVector Start(HorizontalPosition.X, HorizontalPosition.Y, ReferenceZ + 60.f);
		const FVector End(HorizontalPosition.X, HorizontalPosition.Y, ReferenceZ - 1800.f);
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params)
			|| Hit.ImpactNormal.Z < 0.65f) return false;
		OutFloorZ = Hit.ImpactPoint.Z;
		return true;
	}

	const TCHAR* GetEncounterStateName(const EAuraEncounterState State)
	{
		switch (State)
		{
		case EAuraEncounterState::Dormant: return TEXT("Dormant");
		case EAuraEncounterState::Arming: return TEXT("Arming");
		case EAuraEncounterState::LockedCombat: return TEXT("LockedCombat");
		case EAuraEncounterState::Cleared: return TEXT("Cleared");
		case EAuraEncounterState::RewardAvailable: return TEXT("RewardAvailable");
		case EAuraEncounterState::Completed: return TEXT("Completed");
		default: return TEXT("Unknown");
		}
	}
}

FName AAuraEncounterVolume::GetM22BudgetId() const
{
	if (!GetWorld() || !UWorld::RemovePIEPrefix(GetWorld()->GetMapName()).Equals(
		TEXT("DungeonDemo"), ESearchCase::IgnoreCase)) return NAME_None;
	const FName AuthoredId = GetEncounterId();
	if (FindM22RouteRewardCard(AuthoredId)) return AuthoredId;
	// The two original spine encounters have no authored EncounterId. Do not
	// persist their generated UObject names, which may change between loads.
	if (EncounterId.IsNone() && EncounterOrder == 1) return TEXT("M22.OuterGuard");
	if (EncounterId.IsNone() && EncounterOrder == 2) return TEXT("M22.UpperShaman");
	return NAME_None;
}

void AAuraEncounterVolume::RecordM22KillReward(AAuraPlayerState* Player,
	const int32 Experience, const int32 Souls)
{
	if (!HasAuthority() || !Player || GetM22BudgetId().IsNone()
		|| (Experience <= 0 && Souls <= 0)) return;
	FIntPoint& Credited = M22KillRewardsByPlayer.FindOrAdd(
		TWeakObjectPtr<AAuraPlayerState>(Player));
	Credited.X += FMath::Max(0, Experience);
	Credited.Y += FMath::Max(0, Souls);
}

AAuraEncounterVolume::AAuraEncounterVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(SceneRoot);
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AAuraEncounterVolume::OnTriggerOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &AAuraEncounterVolume::OnTriggerEndOverlap);

	EncounterLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("EncounterLabel"));
	AuraWorldTextFont::Apply(EncounterLabel);
	EncounterLabel->SetupAttachment(SceneRoot);
	EncounterLabel->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	EncounterLabel->SetHorizontalAlignment(EHTA_Center);
	EncounterLabel->SetWorldSize(23.f);
	EncounterLabel->SetTextRenderColor(FColor(255, 180, 70));
	EncounterLabel->SetHiddenInGame(true);

	SpawnOffsets = {
		FVector(650.f, -330.f, 96.f),
		FVector(850.f, 330.f, 96.f),
		FVector(1080.f, 0.f, 96.f)
	};

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SpawnTelegraphAsset(
		TEXT("/Game/Assets/Effects/Combat/NS_GroundSummon.NS_GroundSummon"));
	if (SpawnTelegraphAsset.Succeeded())
	{
		EnemySpawnTelegraphEffect = SpawnTelegraphAsset.Object;
	}
}

void AAuraEncounterVolume::BeginPlay()
{
	Super::BeginPlay();
	if (GetEncounterId() == FName(TEXT("LowerNorthBoonGuardians")))
	{
		EncounterTitle = TEXT("击败宝箱守卫");
	}
	else if (GetEncounterId() == FName(TEXT("UpperSouthBoonGuardian")))
	{
		EncounterTitle = TEXT("击败精英宝箱守卫");
	}
	CalibrateRuntimeGround();
	if (HasAuthority())
	{
		if (RewardChest)
		{
			RewardChest->OnTreasureClaimed.AddDynamic(
				this, &AAuraEncounterVolume::HandleTreasureClaimed);
		}
		if (LootChest)
		{
			LootChest->OnLootChestClaimed.AddDynamic(
				this, &AAuraEncounterVolume::HandleLootChestClaimed);
		}

		const FName M22BudgetId = GetM22BudgetId();
		if (bRewardEncounter || !M22BudgetId.IsNone())
		{
			const FName PersistentClearId = M22BudgetId.IsNone()
				? GetEncounterId() : M22BudgetId;
			const AAuraGameState* AuraGameState = GetWorld()->GetGameState<AAuraGameState>();
			bool bAlreadyCleared = AuraGameState
				&& AuraGameState->IsRewardEncounterCleared(PersistentClearId);
			bool bRewardClaimed = (RewardChest && RewardChest->IsOpened())
				|| (LootChest && LootChest->IsOpened());
			if (GetNetMode() == NM_Standalone)
			{
				const UAuraRunSubsystem* RunSubsystem =
					GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
				bAlreadyCleared = bAlreadyCleared || (RunSubsystem
					&& (RunSubsystem->IsRewardEncounterCleared(PersistentClearId)
						|| (!IsChestAmbushRoom()
							&& ((RewardChest && RunSubsystem->IsTreasureClaimed(
								RewardChest->GetTreasureId()))
								|| (LootChest && RunSubsystem->IsTreasureClaimed(
									LootChest->GetLootChestId()))))));
				bRewardClaimed = bRewardClaimed || (RunSubsystem
					&& ((RewardChest && RunSubsystem->IsTreasureClaimed(
						RewardChest->GetTreasureId()))
						|| (LootChest && RunSubsystem->IsTreasureClaimed(
							LootChest->GetLootChestId()))));
			}

			if (bAlreadyCleared)
			{
				EncounterState = !bRewardEncounter || bRewardClaimed
					? EAuraEncounterState::Completed
					: EAuraEncounterState::RewardAvailable;
				TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				for (AAuraDungeonGate* Gate : ControlledGates)
				{
					if (Gate) Gate->SetGateOpen(true, true);
				}
				SetRewardGuardLocked(false, 0);
			}
			else if (IsChestAmbushRoom() && bRewardClaimed)
			{
				// A claimed chest with an uncleared room means the post-chest
				// ambush was interrupted. Re-entering the room replays that fight.
				bResumeChestAmbush = true;
				SetRewardGuardLocked(false, 0);
			}
			else if (bRewardEncounter)
			{
				SetRewardGuardLocked(true, GetConfiguredSpawnCount());
			}
		}
		if (EncounterState == EAuraEncounterState::Dormant && !bResumeChestAmbush)
		{
			for (const int32 EliteIndex : EliteSpawnIndices)
			{
				if (EliteIndex < 0 || EliteIndex >= GetConfiguredSpawnCount()
					|| !SpawnOffsets.IsValidIndex(EliteIndex)
					|| bFinalEncounter || EncounterType == EAuraEncounterType::Boss)
				{
					UE_LOG(LogAuraEncounter, Error,
						TEXT("Invalid elite spawn slot. Room=%s Index=%d"),
						*GetEncounterId().ToString(), EliteIndex);
				}
			}
			SpawnPreplacedGuards();
		}
		if (EncounterState == EAuraEncounterState::Dormant
			&& !RequiredClearedEncounterIds.IsEmpty())
		{
			// A player may already overlap the next room when the previous
			// encounter clears. In that case no new begin-overlap event is sent.
			GetWorldTimerManager().SetTimer(PrerequisiteProbeTimer, this,
				&AAuraEncounterVolume::TryStartAfterPrerequisiteClear,
				0.25f, true, 0.25f);
		}
	}

	RefreshEditorVisuals();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter ready. Order=%d Title=%s Type=%d Final=%s Reward=%s Id=%s SpawnPoints=%d ConfiguredSpawns=%d Profile=%d Gates=%d BoonChest=%s LootChest=%s State=%s"),
		EncounterOrder, *EncounterTitle, static_cast<int32>(EncounterType),
		bFinalEncounter ? TEXT("true") : TEXT("false"),
		bRewardEncounter ? TEXT("true") : TEXT("false"), *GetEncounterId().ToString(),
		SpawnOffsets.Num(), GetConfiguredSpawnCount(), static_cast<int32>(BalanceProfile),
		ControlledGates.Num(), *GetNameSafe(RewardChest), *GetNameSafe(LootChest),
		GetEncounterStateName(EncounterState));
}

void AAuraEncounterVolume::CalibrateRuntimeGround()
{
	RuntimeFloorZ = GetActorLocation().Z;
	float DetectedFloorZ = RuntimeFloorZ;
	if (!TraceWalkableFloor(GetWorld(), this, GetActorLocation(),
		GetActorLocation().Z, DetectedFloorZ))
	{
		UE_LOG(LogAuraEncounter, Warning,
			TEXT("Encounter floor trace failed; using actor height. Id=%s Z=%.1f"),
			*GetEncounterId().ToString(), RuntimeFloorZ);
		return;
	}
	RuntimeFloorZ = DetectedFloorZ;
	// The actor and map remain untouched. Only the server's collision volume
	// follows the edited walking surface so entry is detected at capsule height.
	TriggerBox->SetRelativeLocation(FVector(0.f, 0.f,
		RuntimeFloorZ + 96.f - GetActorLocation().Z));
	TriggerBox->UpdateOverlaps();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter ground calibrated. Id=%s ActorZ=%.1f FloorZ=%.1f TriggerZ=%.1f"),
		*GetEncounterId().ToString(), GetActorLocation().Z, RuntimeFloorZ,
		TriggerBox->GetComponentLocation().Z);
}

FVector AAuraEncounterVolume::GetGroundedSpawnLocation(const int32 SpawnIndex) const
{
	FVector Location = GetActorLocation()
		+ GetActorRotation().RotateVector(SpawnOffsets[SpawnIndex]);
	float FloorZ = RuntimeFloorZ;
	TraceWalkableFloor(GetWorld(), this, Location, GetActorLocation().Z, FloorZ);
	Location.Z = FloorZ + SpawnOffsets[SpawnIndex].Z;
	return Location;
}

int32 AAuraEncounterVolume::GetPreplacedGuardCount() const
{
	if (StandingGuardCount >= 0)
	{
		return FMath::Min(StandingGuardCount,
			FMath::Min(GetConfiguredSpawnCount(), SpawnOffsets.Num()));
	}
	const FName Id = GetEncounterId();
	if (Id == FName(TEXT("LowerNorthBoonGuardians"))
		|| Id == FName(TEXT("UpperNorthRiskGuardians"))
		|| Id == FName(TEXT("LowerSouthLootAmbush")))
	{
		return FMath::Min(2, FMath::Min(GetConfiguredSpawnCount(), SpawnOffsets.Num()));
	}
	return 0;
}

bool AAuraEncounterVolume::IsChestAmbushRoom() const
{
	return GetEncounterId() == FName(TEXT("LowerSouthLootAmbush")) && LootChest != nullptr;
}

bool AAuraEncounterVolume::ResetAfterTeamWipe()
{
	if (!HasAuthority() || EncounterState == EAuraEncounterState::Completed
		|| EncounterState == EAuraEncounterState::Cleared
		|| EncounterState == EAuraEncounterState::RewardAvailable) return false;
	bool bNeedsReset = IsCombatActive();
	for (AAuraEnemy* Guard : PreplacedGuards)
	{
		const UAbilitySystemComponent* ASC = IsValid(Guard) ? Guard->GetAbilitySystemComponent() : nullptr;
		bNeedsReset |= !IsValid(Guard) || Guard->IsDead() || (ASC
			&& ASC->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute())
				< ASC->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()) - 0.1f);
	}
	if (!bNeedsReset) return false;
	GetWorldTimerManager().ClearAllTimersForObject(this);
	// Destroy, rather than kill: reset must never grant XP, loot, or completion.
	TArray<AAuraEnemy*> EnemiesToRemove;
	for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
	{
		// Possession changes a pawn's Owner to its AIController. The authored
		// encounter link survives possession and is the actual reset identity.
		if (It->GetRewardEncounter() == this || (It->GetBossSummoner()
			&& It->GetBossSummoner()->GetRewardEncounter() == this)) EnemiesToRemove.Add(*It);
	}
	for (AAuraEnemy* Enemy : EnemiesToRemove)
	{
		if (!IsValid(Enemy)) continue;
		Enemy->OnEnemyDefeated.RemoveDynamic(this, &ThisClass::HandleEnemyDefeated);
		Enemy->Destroy();
	}
	ActiveEnemies.Reset();
	PreplacedGuards.Reset();
	EnemyHomeTransforms.Reset();
	ActivePlayer.Reset();
	RewardParticipants.Reset();
	M22KillRewardsByPlayer.Reset();
	NextSpawnIndex = 0;
	PlannedSpawnCount = 0;
	RemainingEnemyCount = 0;
	ArmingEndServerTime = 0.f;
	bRoomClearHandled = false;
	bRitualActivated = false;
	bArmingRequiresOccupancy = false;
	bChestAmbushActive = false;
	bResumeChestAmbush = IsChestAmbushRoom() && LootChest && LootChest->IsOpened();
	EncounterState = EAuraEncounterState::Dormant;
	for (AAuraDungeonGate* Gate : ControlledGates)
		if (Gate) Gate->SetGateOpen(true);
	SetRewardGuardLocked(bRewardEncounter && !bResumeChestAmbush, GetConfiguredSpawnCount());
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	if (!bResumeChestAmbush) SpawnPreplacedGuards();
	if (!RequiredClearedEncounterIds.IsEmpty())
		GetWorldTimerManager().SetTimer(PrerequisiteProbeTimer, this,
			&ThisClass::TryStartAfterPrerequisiteClear, 0.25f, true);
	ForceNetUpdate();
	UE_LOG(LogAuraEncounter, Display, TEXT("Encounter reset after team wipe. Id=%s Guards=%d ChestAmbush=%s"),
		*GetEncounterId().ToString(), PreplacedGuards.Num(), bResumeChestAmbush ? TEXT("true") : TEXT("false"));
	return true;
}

void AAuraEncounterVolume::SpawnPreplacedGuards()
{
	for (int32 Index = 0; Index < GetPreplacedGuardCount(); ++Index)
	{
		if (AAuraEnemy* Guard = SpawnEnemyAtIndex(Index, true))
		{
			PreplacedGuards.Add(Guard);
		}
		else break; // Keep preplaced indices contiguous; missing slots spawn on entry.
	}
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter standing guards ready. Id=%s Count=%d"),
		*GetEncounterId().ToString(), PreplacedGuards.Num());
}

void AAuraEncounterVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshEditorVisuals();
}

void AAuraEncounterVolume::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraEncounterVolume, EncounterState);
	DOREPLIFETIME(AAuraEncounterVolume, RemainingEnemyCount);
	DOREPLIFETIME(AAuraEncounterVolume, ArmingEndServerTime);
}

void AAuraEncounterVolume::OnRep_EncounterState()
{
	RefreshEditorVisuals();
}

bool AAuraEncounterVolume::AreControlledGatesOpen() const
{
	for (const AAuraDungeonGate* Gate : ControlledGates)
	{
		if (Gate && !Gate->IsOpen()) return false;
	}
	return !ControlledGates.IsEmpty();
}

int32 AAuraEncounterVolume::GetConfiguredSpawnCount() const
{
	if (SpawnCountOverride >= 0) return SpawnCountOverride;
	return UAuraCombatBalanceData::Get().GetEncounterSpawnCount(
		BalanceProfile, SpawnOffsets.Num());
}

float AAuraEncounterVolume::GetArmingEntryPlaneLocalX() const
{
	float EntryPlane = -TriggerExtent.X + FMath::Max(0.f, ArmingRoomEntryDepth);
	for (const AAuraDungeonGate* Gate : ControlledGates)
	{
		if (!Gate) continue;
		const float GateLocalX = GetActorTransform().InverseTransformPosition(
			Gate->GetActorLocation()).X;
		// Only a gate physically at this room's near doorway is relevant. The
		// outer/inner progression gates may be thousands of units down the hall.
		if (GateLocalX >= -TriggerExtent.X - 120.f
			&& GateLocalX <= TriggerExtent.X)
		{
			EntryPlane = FMath::Max(EntryPlane,
				GateLocalX + FMath::Max(40.f, ArmingRoomEntryDepth));
		}
	}
	return EntryPlane;
}

float AAuraEncounterVolume::GetArmingTimeRemaining() const
{
	if (EncounterState != EAuraEncounterState::Arming || !GetWorld()) return 0.f;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return FMath::Max(0.f, ArmingEndServerTime - ServerTime);
}

FTransform AAuraEncounterVolume::GetRoomReentryTransform(
	const int32 PlayerIndex, const int32 PlayerCount) const
{
	const int32 SafeCount = FMath::Max(1, PlayerCount);
	const float LateralOffset =
		(PlayerIndex - (SafeCount - 1) * 0.5f) * ReentryPlayerSpacing;
	const FVector LocalOffset = RoomReentryOffset + FVector(0.f, LateralOffset, 0.f);
	FVector Location = GetActorTransform().TransformPosition(LocalOffset);
	Location.Z = RuntimeFloorZ + RoomReentryOffset.Z;
	return FTransform(GetActorRotation(), Location, FVector::OneVector);
}

TArray<AAuraEnemy*> AAuraEncounterVolume::GetActiveEnemies() const
{
	TArray<AAuraEnemy*> Result;
	Result.Reserve(ActiveEnemies.Num());
	for (AAuraEnemy* Enemy : ActiveEnemies)
	{
		if (IsValid(Enemy) && !Enemy->IsDead()) Result.Add(Enemy);
	}
	return Result;
}

void AAuraEncounterVolume::TryStartAfterPrerequisiteClear()
{
	if (!HasAuthority() || EncounterState != EAuraEncounterState::Dormant)
	{
		GetWorldTimerManager().ClearTimer(PrerequisiteProbeTimer);
		return;
	}
	if (AAuraCharacter* Candidate = FindArmingCandidate(false))
	{
		StartEncounter(Candidate, false);
	}
}

void AAuraEncounterVolume::StartEncounter(
	AAuraCharacter* TriggeringCharacter, const bool bSkipArming)
{
	if (const AAuraGameState* State = GetWorld()->GetGameState<AAuraGameState>();
		State && State->IsTeamWipePending()) return;
	if (!HasAuthority() || EncounterState != EAuraEncounterState::Dormant
		|| !TriggeringCharacter || TriggeringCharacter->IsDead()
		|| (bRequiresRitual && !bRitualActivated))
	{
		return;
	}
	if (!RequiredClearedEncounterIds.IsEmpty())
	{
		const AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
		for (const FName RequiredId : RequiredClearedEncounterIds)
		{
			if (!Quest || !Quest->IsRewardEncounterCleared(RequiredId)) return;
		}
	}

	EncounterState = EAuraEncounterState::Arming;
	GetWorldTimerManager().ClearTimer(PrerequisiteProbeTimer);
	ActivePlayer = TriggeringCharacter;
	GetWorldTimerManager().ClearTimer(ArmingTimer);
	GetWorldTimerManager().ClearTimer(ArmingPulseTimer);
	ArmingEndServerTime = 0.f;
	bArmingRequiresOccupancy = !bSkipArming;
	ForceNetUpdate();
	if (!bSkipArming)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get()))
			{
				Controller->ClearCoopGuidance();
			}
		}
	}

	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter entry detected. Order=%d Title=%s Trigger=%s SkipDwell=%s Dwell=%.1f EntryDepth=%.1f"),
		EncounterOrder, *EncounterTitle, *GetNameSafe(TriggeringCharacter),
		bSkipArming ? TEXT("true") : TEXT("false"), ArmingDuration, ArmingRoomEntryDepth);
	if (bSkipArming)
	{
		// Ritual entry locks immediately, but still plays the spawn telegraph.
		BeginLockedCombat(!bRequiresRitual);
		return;
	}

	GetWorldTimerManager().SetTimer(
		ArmingTimer, this, &AAuraEncounterVolume::UpdateArmingDwell,
		0.1f, true, 0.1f);
	UpdateArmingDwell();
}

bool AAuraEncounterVolume::ActivateFromRitual(AAuraCharacter* TriggeringCharacter,
	const bool bImmediateSpawnForSmokeTest)
{
	if (!HasAuthority() || !bRequiresRitual || !bFinalEncounter
		|| EncounterState != EAuraEncounterState::Dormant
		|| !TriggeringCharacter || TriggeringCharacter->IsDead()) return false;
	const AAuraGameState* State = GetWorld()->GetGameState<AAuraGameState>();
	if (!State || !State->AreAllSealsActivated() || State->IsBossSummoned()
		|| State->IsRunCompleted()) return false;
	// A ritual is an explicit team transition; living players outside the boss
	// chamber are moved to the authored re-entry point before the gate seals.
	GatherLivingPlayers();
	bRitualActivated = true;
	StartEncounter(TriggeringCharacter, true);
	if (bImmediateSpawnForSmokeTest
		&& EncounterState == EAuraEncounterState::LockedCombat)
	{
		SpawnNextWave();
	}
	if (!IsCombatActive()) bRitualActivated = false;
	return IsCombatActive();
}

bool AAuraEncounterVolume::IsEligibleArmingCharacter(
	const AAuraCharacter* Character) const
{
	return IsValid(Character) && !Character->IsDead();
}

bool AAuraEncounterVolume::IsInsideRoomSide(
	const AAuraCharacter* Character) const
{
	if (!IsEligibleArmingCharacter(Character)) return false;
	const FVector LocalLocation = GetActorTransform()
		.InverseTransformPosition(Character->GetActorLocation());
	// Encounter actors are authored near room centres, not consistently on the
	// door itself. Measure entry depth from the trigger's approach-side edge so
	// every room starts its dwell a small, consistent distance past the doorway.
	return LocalLocation.X >= GetArmingEntryPlaneLocalX();
}

AAuraCharacter* AAuraEncounterVolume::FindArmingCandidate(
	const bool bRequireRoomSide) const
{
	if (!TriggerBox) return nullptr;
	TArray<AActor*> OverlappingActors;
	TriggerBox->GetOverlappingActors(OverlappingActors, AAuraCharacter::StaticClass());
	for (AActor* Actor : OverlappingActors)
	{
		AAuraCharacter* Character = Cast<AAuraCharacter>(Actor);
		if (IsEligibleArmingCharacter(Character)
			&& (!bRequireRoomSide || IsInsideRoomSide(Character)))
		{
			return Character;
		}
	}
	return nullptr;
}

void AAuraEncounterVolume::CancelArming()
{
	if (!HasAuthority() || EncounterState != EAuraEncounterState::Arming) return;
	GetWorldTimerManager().ClearTimer(ArmingTimer);
	GetWorldTimerManager().ClearTimer(ArmingPulseTimer);
	EncounterState = EAuraEncounterState::Dormant;
	ArmingEndServerTime = 0.f;
	bArmingRequiresOccupancy = false;
	ActivePlayer.Reset();
	ForceNetUpdate();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter arming cancelled because no living player remains inside. Order=%d Title=%s"),
		EncounterOrder, *EncounterTitle);
}

void AAuraEncounterVolume::UpdateArmingDwell()
{
	if (!HasAuthority() || EncounterState != EAuraEncounterState::Arming
		|| !bArmingRequiresOccupancy)
	{
		GetWorldTimerManager().ClearTimer(ArmingTimer);
		return;
	}

	AAuraCharacter* Candidate = ActivePlayer.Get();
	if (!IsEligibleArmingCharacter(Candidate)) Candidate = FindArmingCandidate(false);
	if (!Candidate)
	{
		CancelArming();
		return;
	}

	if (!IsInsideRoomSide(Candidate))
	{
		if (AAuraCharacter* CommittedCandidate = FindArmingCandidate(true))
		{
			Candidate = CommittedCandidate;
		}
		else
		{
			// Before the doorway is crossed, leaving the narrow entry trigger
			// means the player backed out. Past the doorway, overlap is no longer
			// required and the timer continues anywhere deeper inside the room.
			if (!TriggerBox || !TriggerBox->IsOverlappingActor(Candidate))
			{
				CancelArming();
				return;
			}
			ActivePlayer = Candidate;
			if (ArmingEndServerTime > 0.f)
			{
				ArmingEndServerTime = 0.f;
				ForceNetUpdate();
				UE_LOG(LogAuraEncounter, Display,
					TEXT("Encounter dwell reset; player returned to doorway/outside side. Order=%d Player=%s"),
					EncounterOrder, *GetNameSafe(Candidate));
			}
			return;
		}
	}

	ActivePlayer = Candidate;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	if (ArmingEndServerTime <= 0.f)
	{
		ArmingEndServerTime = ServerTime + FMath::Max(0.f, ArmingDuration);
		ForceNetUpdate();
		UE_LOG(LogAuraEncounter, Display,
			TEXT("Encounter dwell started. Order=%d Player=%s Duration=%.1f"),
			EncounterOrder, *GetNameSafe(Candidate), ArmingDuration);
	}
	if (ServerTime + KINDA_SMALL_NUMBER >= ArmingEndServerTime
		&& IsInsideRoomSide(Candidate))
	{
		BeginLockedCombat();
	}
}

void AAuraEncounterVolume::BroadcastArmingStatus()
{
	// Entry/countdown guidance is intentionally silent. Door motion and the
	// spawn telegraph communicate the transition without a persistent text row.
	GetWorldTimerManager().ClearTimer(ArmingPulseTimer);
}

void AAuraEncounterVolume::BeginLockedCombat(const bool bSkipSpawnTelegraph)
{
	if (!HasAuthority() || EncounterState != EAuraEncounterState::Arming) return;
	GetWorldTimerManager().ClearTimer(ArmingTimer);
	GetWorldTimerManager().ClearTimer(ArmingPulseTimer);

	EncounterState = EAuraEncounterState::LockedCombat;
	RewardParticipants.Reset();
	if (const AGameStateBase* State = GetWorld()->GetGameState())
	{
		for (APlayerState* Player : State->PlayerArray)
		{
			AAuraPlayerState* AuraState = Cast<AAuraPlayerState>(Player);
			AAuraCharacter* Character = AuraState
				? Cast<AAuraCharacter>(AuraState->GetPawn()) : nullptr;
			if (Character && !Character->IsDead()
				&& (Character == ActivePlayer.Get()
					|| (FVector::DistSquared(Character->GetActorLocation(), GetActorLocation())
						<= FMath::Square(3500.f) && IsInsideRoomSide(Character))))
			{
				RewardParticipants.Add(AuraState);
			}
		}
	}
	ArmingEndServerTime = 0.f;
	bArmingRequiresOccupancy = false;
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	for (AAuraDungeonGate* Gate : ControlledGates)
	{
		if (Gate) Gate->SetGateOpen(false);
	}

	ActiveEnemies.Reset();
	EnemyHomeTransforms.Reset();
	GetWorldTimerManager().ClearTimer(WaveTimer);
	GetWorldTimerManager().ClearTimer(SpawnCueTimer);
	bChestAmbushActive = IsChestAmbushRoom()
		&& (bResumeChestAmbush || (LootChest && LootChest->IsOpened()));
	bResumeChestAmbush = false;
	NextSpawnIndex = bChestAmbushActive ? 0 : PreplacedGuards.Num();
	bRoomClearHandled = false;
	PlannedSpawnCount = bChestAmbushActive
		? FMath::Min(2, SpawnOffsets.Num())
		: FMath::Min(GetConfiguredSpawnCount(), SpawnOffsets.Num());
	int32 LivingPreplaced = 0;
	if (!bChestAmbushActive)
	{
		for (AAuraEnemy* Guard : PreplacedGuards)
		{
			if (!IsValid(Guard) || Guard->IsDead()) continue;
			Guard->SetEncounterDormant(false);
			ActiveEnemies.Add(Guard);
			EnemyHomeTransforms.Add(Guard, Guard->GetActorTransform());
			++LivingPreplaced;
		}
	}
	RemainingEnemyCount = PlannedSpawnCount
		- (bChestAmbushActive ? 0 : NextSpawnIndex - LivingPreplaced);
	if (GetConfiguredSpawnCount() > SpawnOffsets.Num())
	{
		UE_LOG(LogAuraEncounter, Error,
			TEXT("Encounter requests %d enemies but only %d spawn points exist. Order=%d Profile=%d"),
			GetConfiguredSpawnCount(), SpawnOffsets.Num(), EncounterOrder,
			static_cast<int32>(BalanceProfile));
	}
	SetRewardGuardLocked(HasRoomReward() && !bChestAmbushActive, RemainingEnemyCount);
	ForceNetUpdate();

	if (EncounterType == EAuraEncounterType::Recovery || PlannedSpawnCount <= 0)
	{
		HandleRoomCleared();
		return;
	}
	if (NextSpawnIndex < PlannedSpawnCount
		&& (ActiveEnemies.IsEmpty() || bCueReinforcementsOnLock))
	{
		if (bSkipSpawnTelegraph) SpawnNextWave();
		else CueNextWave();
	}
	GetWorldTimerManager().SetTimer(
		EnemyValidationTimer, this, &AAuraEncounterVolume::ValidateActiveEnemies,
		1.f, true, 1.f);
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter combat locked. Order=%d Title=%s Type=%d Planned=%d Gates=%d"),
		EncounterOrder, *EncounterTitle, static_cast<int32>(EncounterType),
		PlannedSpawnCount, ControlledGates.Num());
}

TArray<AAuraPlayerState*> AAuraEncounterVolume::GetEligibleRewardPlayers() const
{
	TArray<AAuraPlayerState*> Result;
	for (const TWeakObjectPtr<AAuraPlayerState>& Participant : RewardParticipants)
	{
		if (AAuraPlayerState* State = Participant.Get()) Result.Add(State);
	}
	return Result;
}

void AAuraEncounterVolume::GatherLivingPlayers()
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState) return;
	const int32 PlayerCount = FMath::Max(1, GameState->PlayerArray.Num());
	int32 Gathered = 0;
	for (int32 Index = 0; Index < GameState->PlayerArray.Num(); ++Index)
	{
		APlayerState* PlayerState = GameState->PlayerArray[Index];
		AAuraCharacter* Character = PlayerState
			? Cast<AAuraCharacter>(PlayerState->GetPawn()) : nullptr;
		if (!Character || Character->IsDead() || TriggerBox->IsOverlappingActor(Character)) continue;
		const FTransform ReentryTransform = GetRoomReentryTransform(Index, PlayerCount);
		Character->TeleportTo(
			ReentryTransform.GetLocation(), ReentryTransform.Rotator(), false, true);
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		++Gathered;
	}
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter team gathered. Order=%d Players=%d Teleported=%d"),
		EncounterOrder, GameState->PlayerArray.Num(), Gathered);
}

AAuraEnemy* AAuraEncounterVolume::SpawnEnemyAtIndex(
	const int32 SpawnIndex, const bool bPreplaced)
{
	if (!SpawnOffsets.IsValidIndex(SpawnIndex)) return nullptr;
	const AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	const bool bAbyss = Quest && Quest->IsAbyssTrialActive();
	const TSubclassOf<AAuraEnemy> SpawnClass =
		bAbyss && AbyssSpawnEnemyClasses.IsValidIndex(SpawnIndex)
			&& AbyssSpawnEnemyClasses[SpawnIndex]
			? AbyssSpawnEnemyClasses[SpawnIndex]
			: SpawnEnemyClasses.IsValidIndex(SpawnIndex) && SpawnEnemyClasses[SpawnIndex]
			? SpawnEnemyClasses[SpawnIndex]
			: EnemyClass;
	if (!SpawnClass) return nullptr;
	const bool bEliteSlot = EliteSpawnIndices.Contains(SpawnIndex)
		&& !bFinalEncounter && EncounterType != EAuraEncounterType::Boss
		&& !SpawnClass->GetDefaultObject<AAuraEnemy>()->IsBossEnemy();
	const float EliteScale = bEliteSlot
		? UAuraCombatBalanceData::Get().EliteEnemy.VisualScale : 1.f;
	FVector SpawnLocation = GetGroundedSpawnLocation(SpawnIndex);
	if (bEliteSlot)
	{
		// Enlarging the capsule around its center must not push its feet into the floor.
		const float CapsuleHalfHeight = SpawnClass->GetDefaultObject<AAuraEnemy>()
			->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		SpawnLocation.Z += CapsuleHalfHeight * SpawnedEnemyScale.Z * (EliteScale - 1.f);
	}
	const FRotator SpawnRotation = (-GetActorForwardVector()).Rotation();
	const FVector WorldScale = SpawnedEnemyScale * EliteScale;
	const FTransform SpawnTransform(SpawnRotation, SpawnLocation, WorldScale);
	AAuraEnemy* SpawnedEnemy = GetWorld()->SpawnActorDeferred<AAuraEnemy>(
		SpawnClass, SpawnTransform, this, ActivePlayer.Get(),
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!SpawnedEnemy) return nullptr;
	if (bPreplaced) SpawnedEnemy->ConfigurePreplacedGuard();
	if (bEliteSlot) SpawnedEnemy->ConfigureEliteVariant(WorldScale);
	SpawnedEnemy->SetRewardEncounter(this);
	if (bFinalEncounter || EncounterType == EAuraEncounterType::Boss)
	{
		SpawnedEnemy->SetLootProfile(EAuraLootProfileType::Boss);
	}
	else if (EncounterType == EAuraEncounterType::GuardedTreasure)
	{
		SpawnedEnemy->SetLootProfile(EAuraLootProfileType::SealGuardian);
	}
	else if (bEliteSlot || EncounterType == EAuraEncounterType::Elite
		|| BalanceProfile == EAuraEncounterBalanceProfile::EliteShaman)
	{
		SpawnedEnemy->SetLootProfile(EAuraLootProfileType::EliteEnemy);
	}
	UGameplayStatics::FinishSpawningActor(SpawnedEnemy, SpawnTransform);
	if (bAbyss) SpawnedEnemy->ApplyAbyssHealthBonus(
		UAuraCombatBalanceData::Get().AbyssTrial.MaxHealthMultiplier);
	if (bEliteSlot)
	{
		UE_LOG(LogAuraEncounter, Display,
			TEXT("Encounter elite spawned. Room=%s Slot=%d Enemy=%s Health=%.1f Scale=%.2f"),
			*GetEncounterId().ToString(), SpawnIndex, *SpawnedEnemy->GetName(),
			SpawnedEnemy->GetStartingHealth(), EliteScale);
	}
	SpawnedEnemy->OnEnemyDefeated.AddDynamic(
		this, &AAuraEncounterVolume::HandleEnemyDefeated);
	ActiveEnemies.Add(SpawnedEnemy);
	EnemyHomeTransforms.Add(SpawnedEnemy, SpawnedEnemy->GetActorTransform());
	return SpawnedEnemy;
}

void AAuraEncounterVolume::CueNextWave()
{
	if (!HasAuthority() || EncounterState != EAuraEncounterState::LockedCombat) return;
	GetWorldTimerManager().ClearTimer(WaveTimer);
	GetWorldTimerManager().ClearTimer(SpawnCueTimer);
	const int32 WaveSize = bChestAmbushActive ? PlannedSpawnCount
		: EncounterType == EAuraEncounterType::Ambush
		? FMath::Max(1, EnemiesPerWave) : PlannedSpawnCount;
	const int32 WaveEnd = FMath::Min(PlannedSpawnCount, NextSpawnIndex + WaveSize);
	if (NextSpawnIndex >= WaveEnd)
	{
		ResolveEmptyWave();
		return;
	}

	for (int32 Index = NextSpawnIndex; Index < WaveEnd; ++Index)
	{
		if (!SpawnOffsets.IsValidIndex(Index)) continue;
		FVector EffectLocation = GetGroundedSpawnLocation(Index);
		// Spawn offsets place capsule centres about 96uu above the authored floor.
		EffectLocation.Z -= SpawnOffsets[Index].Z - 4.f;
		const float EffectScaleValue = FMath::Clamp(SpawnedEnemyScale.GetMax(), 0.85f, 2.f);
		MulticastPlayEnemySpawnTelegraph(
			EffectLocation, FVector(EffectScaleValue));
	}
	GetWorldTimerManager().SetTimer(
		SpawnCueTimer, this, &AAuraEncounterVolume::SpawnNextWave,
		FMath::Max(0.1f, EnemySpawnTelegraphLeadTime), false);
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter spawn telegraph started. Order=%d Range=%d-%d Lead=%.2f"),
		EncounterOrder, NextSpawnIndex, WaveEnd - 1, EnemySpawnTelegraphLeadTime);
}

void AAuraEncounterVolume::MulticastPlayEnemySpawnTelegraph_Implementation(
	const FVector SpawnLocation, const FVector EffectScale)
{
	if (GetNetMode() == NM_DedicatedServer || !EnemySpawnTelegraphEffect) return;
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, EnemySpawnTelegraphEffect, SpawnLocation,
		FRotator::ZeroRotator, EffectScale, true, true,
		ENCPoolMethod::AutoRelease, true);
}

void AAuraEncounterVolume::SpawnNextWave()
{
	if (!HasAuthority() || EncounterState != EAuraEncounterState::LockedCombat) return;
	GetWorldTimerManager().ClearTimer(WaveTimer);
	GetWorldTimerManager().ClearTimer(SpawnCueTimer);
	const int32 WaveSize = bChestAmbushActive ? PlannedSpawnCount
		: EncounterType == EAuraEncounterType::Ambush
		? FMath::Max(1, EnemiesPerWave) : PlannedSpawnCount;
	const int32 WaveEnd = FMath::Min(PlannedSpawnCount, NextSpawnIndex + WaveSize);
	const int32 WaveStart = NextSpawnIndex;
	for (; NextSpawnIndex < WaveEnd; ++NextSpawnIndex)
	{
		if (!SpawnEnemyAtIndex(NextSpawnIndex))
		{
			RemainingEnemyCount = FMath::Max(0, RemainingEnemyCount - 1);
			UE_LOG(LogAuraEncounter, Error,
				TEXT("Encounter enemy spawn failed. Order=%d SpawnIndex=%d"),
				EncounterOrder, NextSpawnIndex);
		}
	}
	SetRewardGuardLocked(HasRoomReward() && !bChestAmbushActive, RemainingEnemyCount);
	UpdateObjective();
	ForceNetUpdate();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter wave spawned. Order=%d Range=%d-%d Active=%d Remaining=%d"),
		EncounterOrder, WaveStart, WaveEnd - 1, ActiveEnemies.Num(), RemainingEnemyCount);
	ResolveEmptyWave();
}

void AAuraEncounterVolume::HandleEnemyDefeated(AAuraEnemy* DefeatedEnemy)
{
	if (EncounterState != EAuraEncounterState::LockedCombat || !DefeatedEnemy) return;
	DefeatedEnemy->OnEnemyDefeated.RemoveDynamic(
		this, &AAuraEncounterVolume::HandleEnemyDefeated);
	ActiveEnemies.Remove(DefeatedEnemy);
	EnemyHomeTransforms.Remove(DefeatedEnemy);
	RemainingEnemyCount = FMath::Max(0, RemainingEnemyCount - 1);
	SetRewardGuardLocked(HasRoomReward() && !bChestAmbushActive, RemainingEnemyCount);
	UpdateObjective();
	ForceNetUpdate();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter enemy defeated. Order=%d Active=%d Remaining=%d Enemy=%s"),
		EncounterOrder, ActiveEnemies.Num(), RemainingEnemyCount, *GetNameSafe(DefeatedEnemy));
	ResolveEmptyWave();
}

void AAuraEncounterVolume::ResolveEmptyWave()
{
	if (EncounterState != EAuraEncounterState::LockedCombat || !ActiveEnemies.IsEmpty()) return;
	if (NextSpawnIndex < PlannedSpawnCount)
	{
		// The recurring enemy validation pass can observe the empty gap between waves.
		// Do not restart the countdown every time it does, or the next wave can be
		// postponed forever.
		if (GetWorldTimerManager().IsTimerActive(WaveTimer)
			|| GetWorldTimerManager().IsTimerActive(SpawnCueTimer)) return;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get()))
			{
				Controller->ShowCoopMessage(TEXT("下一波敌人即将出现"),
					FLinearColor(1.f, 0.24f, 0.06f), InterWaveDelay,
					FString::Printf(TEXT("%s  |  剩余 %d 名敌人"),
						*EncounterTitle, RemainingEnemyCount));
			}
		}
		GetWorldTimerManager().SetTimer(
			WaveTimer, this, &AAuraEncounterVolume::CueNextWave,
			FMath::Max(0.1f, InterWaveDelay), false);
		return;
	}
	if (RemainingEnemyCount <= 0) HandleRoomCleared();
}

void AAuraEncounterVolume::ValidateActiveEnemies()
{
	if (EncounterState != EAuraEncounterState::LockedCombat)
	{
		GetWorldTimerManager().ClearTimer(EnemyValidationTimer);
		return;
	}
	for (int32 Index = ActiveEnemies.Num() - 1; Index >= 0; --Index)
	{
		AAuraEnemy* Enemy = ActiveEnemies[Index];
		if (!IsValid(Enemy))
		{
			ActiveEnemies.RemoveAtSwap(Index);
			RemainingEnemyCount = FMath::Max(0, RemainingEnemyCount - 1);
			continue;
		}
		if (Enemy->IsDead()) continue;
		if (FVector::DistSquared(Enemy->GetActorLocation(), GetActorLocation())
			> FMath::Square(FMath::Max(500.f, EnemyLeashRadius)))
		{
			if (const FTransform* Home = EnemyHomeTransforms.Find(Enemy))
			{
				Enemy->TeleportTo(Home->GetLocation(), Home->Rotator(), false, true);
				if (AAIController* AIController = Cast<AAIController>(Enemy->GetController()))
				{
					AIController->StopMovement();
				}
				UE_LOG(LogAuraEncounter, Warning,
					TEXT("Escaped encounter enemy returned. Order=%d Enemy=%s"),
					EncounterOrder, *GetNameSafe(Enemy));
			}
		}
	}
	SetRewardGuardLocked(HasRoomReward() && !bChestAmbushActive, RemainingEnemyCount);
	ResolveEmptyWave();
}

void AAuraEncounterVolume::HandleRoomCleared(AAuraEnemy* DefeatedEnemy)
{
	if (!HasAuthority() || bRoomClearHandled
		|| EncounterState != EAuraEncounterState::LockedCombat)
	{
		return;
	}
	if (IsChestAmbushRoom() && !bChestAmbushActive && LootChest
		&& !LootChest->IsOpened())
	{
		// First phase only exposes the chest. The encounter is not persisted as
		// cleared until the chest-triggered ambush is defeated; checkpoint reloads
		// therefore cannot silently skip the second phase.
		bRoomClearHandled = true;
		EncounterState = EAuraEncounterState::RewardAvailable;
		RemainingEnemyCount = 0;
		GetWorldTimerManager().ClearTimer(WaveTimer);
		GetWorldTimerManager().ClearTimer(SpawnCueTimer);
		GetWorldTimerManager().ClearTimer(EnemyValidationTimer);
		for (AAuraDungeonGate* Gate : ControlledGates)
		{
			if (Gate) Gate->SetGateOpen(true);
		}
		SetRewardGuardLocked(false, 0);
		ForceNetUpdate();
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get()))
			{
				Controller->ShowCoopMessage(TEXT("宝箱已解锁"),
					FLinearColor(0.24f, 1.f, 0.48f), 2.f, TEXT("打开宝箱领取战利品"));
			}
		}
		UE_LOG(LogAuraEncounter, Display,
			TEXT("Encounter first phase cleared; waiting for chest ambush. Id=%s"),
			*GetEncounterId().ToString());
		return;
	}
	bRoomClearHandled = true;
	const FName M22BudgetId = GetM22BudgetId();
	const FAuraGrowthEconomyBalance& Economy = UAuraCombatBalanceData::Get().GrowthEconomy;
	const int32 BonusXP = BalanceProfile == EAuraEncounterBalanceProfile::EliteShaman
		? Economy.EliteRoomCatchUpExperience : 0;
	const AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	const int32 BonusSouls = !bRewardEncounter ? 0
		: (Quest && Quest->IsAbyssTrialActive()
			? FMath::RoundToInt(Economy.SealRoomClearSouls
				* UAuraCombatBalanceData::Get().AbyssTrial.SoulMultiplier)
			: Economy.SealRoomClearSouls);
	const TArray<AAuraPlayerState*> RewardPlayers = GetEligibleRewardPlayers();
	if (!M22BudgetId.IsNone())
	{
		// The last lethal GameplayEffect can broadcast room completion before
		// PostGameplayEffectExecute awards its kill XP. Settle on the next tick.
		GetWorldTimerManager().SetTimerForNextTick(
			this, &AAuraEncounterVolume::GrantM22FirstClearReward);
	}
	else if (BonusXP > 0 || BonusSouls > 0)
	{
		for (AAuraPlayerState* State : RewardPlayers)
		{
			State->AwardExperienceAndSouls(BonusXP, BonusSouls);
		}
	}
	if (bRewardEncounter)
	{
		UAuraItemDefinition* OreDefinition = UAuraItemCatalog::Get().FindItem(
			TEXT("Material.ArcaneOreShard"));
		UAuraLootSubsystem* Loot = GetWorld()->GetSubsystem<UAuraLootSubsystem>();
		if (OreDefinition && Loot)
		{
			for (AAuraPlayerState* State : RewardPlayers)
			{
				FAuraItemInstance Ore;
				Ore.Definition = OreDefinition;
				const float BonusOreChance = Quest && Quest->IsAbyssTrialActive()
					? FMath::Clamp(UAuraCombatBalanceData::Get().AbyssTrial.OreMultiplier
						- 1.f, 0.f, 1.f) : 0.f;
				Ore.Quantity = 1 + (FMath::FRand() < BonusOreChance ? 1 : 0);
				Ore.SourceType = EAuraItemSourceType::Treasure;
				Ore.SourceId = GetEncounterId();
				Ore.EnsureIdentity();
				Loot->SpawnSpecificPersonalItem(*State, Ore,
					RewardChest ? RewardChest->GetActorLocation() + FVector(0.f, 0.f, 40.f)
						: GetActorLocation(), 180.f);
			}
		}
	}
	EncounterState = EAuraEncounterState::Cleared;
	RemainingEnemyCount = 0;
	GetWorldTimerManager().ClearTimer(WaveTimer);
	GetWorldTimerManager().ClearTimer(SpawnCueTimer);
	GetWorldTimerManager().ClearTimer(EnemyValidationTimer);
	for (AAuraDungeonGate* Gate : ControlledGates)
	{
		if (Gate) Gate->SetGateOpen(true);
	}

	if (bRewardEncounter || !M22BudgetId.IsNone())
	{
		const FName PersistentClearId = M22BudgetId.IsNone()
			? GetEncounterId() : M22BudgetId;
		if (AAuraGameState* AuraGameState = GetWorld()->GetGameState<AAuraGameState>())
		{
			AuraGameState->MarkRewardEncounterCleared(PersistentClearId);
		}
		if (GetNetMode() == NM_Standalone)
		{
			if (UAuraRunSubsystem* RunSubsystem =
				GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
			{
				RunSubsystem->MarkRewardEncounterCleared(PersistentClearId);
			}
		}
	}
	SetRewardGuardLocked(false, 0);

	if (bFinalEncounter)
	{
		if (AAuraGameState* AuraGameState = GetWorld()->GetGameState<AAuraGameState>())
		{
			AuraGameState->MarkRunCompleted();
		}
		if (GetNetMode() == NM_Standalone)
		{
			if (UAuraRunSubsystem* RunSubsystem =
				GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
			{
				RunSubsystem->MarkRunCompleted();
			}
		}
	}

	if (HasRoomReward() && !bFinalEncounter
		&& !(bChestAmbushActive && LootChest && LootChest->IsOpened()))
	{
		EncounterState = EAuraEncounterState::RewardAvailable;
	}
	else
	{
		FinalizeEncounter(DefeatedEnemy);
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get());
		if (!Controller) continue;
		if (bFinalEncounter)
		{
			Controller->HandleDemoVictory(DefeatedEnemy);
		}
		else
		{
			Controller->ShowCoopMessage(
				HasRoomReward() ? TEXT("奖励已解锁") : TEXT("区域已清理"),
				HasRoomReward() ? FLinearColor(0.24f, 1.f, 0.48f)
					: FLinearColor(1.f, 0.78f, 0.18f), 1.8f,
				HasRoomReward() ? TEXT("领取奖励") : TEXT("继续深入"));
		}
	}

	OnEncounterCompleted.Broadcast(this);
	ForceNetUpdate();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter cleared. Order=%d Title=%s Final=%s Reward=%s State=%s"),
		EncounterOrder, *EncounterTitle, bFinalEncounter ? TEXT("true") : TEXT("false"),
		HasRoomReward() ? TEXT("true") : TEXT("false"),
		GetEncounterStateName(EncounterState));
}

void AAuraEncounterVolume::FinalizeEncounter(AAuraEnemy* DefeatedEnemy)
{
	EncounterState = EAuraEncounterState::Completed;
	ForceNetUpdate();
}

void AAuraEncounterVolume::GrantM22FirstClearReward()
{
	if (!HasAuthority() || !bRoomClearHandled) return;
	const FName BudgetId = GetM22BudgetId();
	const FM22RouteRewardCard* Card = FindM22RouteRewardCard(BudgetId);
	if (!Card) return;
	const AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	const int32 TargetSouls = Quest && Quest->IsAbyssTrialActive()
		? FMath::RoundToInt(Card->Souls
			* UAuraCombatBalanceData::Get().AbyssTrial.SoulMultiplier)
		: Card->Souls;
	for (AAuraPlayerState* State : GetEligibleRewardPlayers())
	{
		if (!State) continue;
		const FIntPoint Credited = M22KillRewardsByPlayer.FindRef(
			TWeakObjectPtr<AAuraPlayerState>(State));
		const int32 CatchUpXP = FMath::Max(0, Card->Experience - Credited.X);
		const int32 CatchUpSouls = FMath::Max(0, TargetSouls - Credited.Y);
		if (CatchUpXP > 0 || CatchUpSouls > 0)
		{
			State->AwardExperienceAndSouls(CatchUpXP, CatchUpSouls);
		}
		UE_LOG(LogAuraEncounter, Display,
			TEXT("AURA_M22_REWARD Id=%s Player=%s Kills=%d/%d CatchUp=%d/%d Target=%d/%d"),
			*BudgetId.ToString(), *State->GetName(), Credited.X, Credited.Y,
			CatchUpXP, CatchUpSouls, Card->Experience, TargetSouls);
	}
	M22KillRewardsByPlayer.Reset();
}

void AAuraEncounterVolume::HandleTreasureClaimed(
	AAuraTreasureChest* Chest, APlayerState* ClaimingPlayer)
{
	if (!HasAuthority() || Chest != RewardChest
		|| EncounterState != EAuraEncounterState::RewardAvailable) return;
	FinalizeEncounter();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter reward claimed. Order=%d Chest=%s Player=%s State=Completed"),
		EncounterOrder, *GetNameSafe(Chest), *GetNameSafe(ClaimingPlayer));
}

void AAuraEncounterVolume::HandleLootChestClaimed(
	AAuraLootChest* Chest, APlayerState* ClaimingPlayer)
{
	if (!HasAuthority() || Chest != LootChest
		|| EncounterState != EAuraEncounterState::RewardAvailable) return;
	if (IsChestAmbushRoom())
	{
		ActivePlayer = ClaimingPlayer
			? Cast<AAuraCharacter>(ClaimingPlayer->GetPawn()) : nullptr;
		EncounterState = EAuraEncounterState::Arming;
		ForceNetUpdate();
		BeginLockedCombat();
		UE_LOG(LogAuraEncounter, Display,
			TEXT("Chest ambush triggered. Id=%s Chest=%s Player=%s"),
			*GetEncounterId().ToString(), *GetNameSafe(Chest),
			*GetNameSafe(ClaimingPlayer));
		return;
	}
	FinalizeEncounter();
	UE_LOG(LogAuraEncounter, Display,
		TEXT("Encounter item reward claimed. Order=%d Chest=%s Player=%s State=Completed"),
		EncounterOrder, *GetNameSafe(Chest), *GetNameSafe(ClaimingPlayer));
}

void AAuraEncounterVolume::SetRewardGuardLocked(
	const bool bLocked, const int32 RemainingGuards)
{
	if (RewardChest) RewardChest->SetGuardLocked(bLocked, RemainingGuards);
	if (LootChest) LootChest->SetGuardLocked(bLocked, RemainingGuards);
}

void AAuraEncounterVolume::UpdateObjective() const
{
	FString Objective;
	FString StatusMessage;
	FLinearColor StatusColor(1.f, 0.62f, 0.12f);
	switch (EncounterState)
	{
	case EAuraEncounterState::Arming:
		return;
	case EAuraEncounterState::LockedCombat:
		Objective = FString::Printf(TEXT("%s  |  剩余 %d 名敌人"),
			*EncounterTitle, RemainingEnemyCount);
		if (RemainingEnemyCount == PlannedSpawnCount)
		{
			StatusMessage = bFinalEncounter ? TEXT("首领已觉醒")
				: (EncounterType == EAuraEncounterType::Ambush
					? TEXT("敌人伏击！") : TEXT("房门已关闭"));
		}
		break;
	case EAuraEncounterState::RewardAvailable:
		Objective = TEXT("领取奖励");
		break;
	case EAuraEncounterState::Completed:
		Objective = bFinalEncounter ? TEXT("地牢通关") : TEXT("继续深入");
		break;
	default:
		Objective = EncounterTitle;
		break;
	}
	if (bFinalEncounter) StatusColor = FLinearColor(1.f, 0.16f, 0.08f);
	else if (HasRoomReward()) StatusColor = FLinearColor(1.f, 0.26f, 0.08f);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get()))
		{
			FString PersonalStatus = StatusMessage;
			float MessageDuration = 1.5f;
			if (EncounterState == EAuraEncounterState::LockedCombat
				&& RemainingEnemyCount == PlannedSpawnCount)
			{
				const AAuraCharacter* Player = Cast<AAuraCharacter>(Controller->GetPawn());
				const bool bQ = Player && Player->GetSkillRank(EAuraPlayerSkill::ArcaneShards) > 0;
				const bool bE = Player && Player->GetSkillRank(EAuraPlayerSkill::Shock) > 0;
				if (bFinalEncounter && bQ && bE)
				{
					PersonalStatus = TEXT("首领战提示：火球术 → Q，再试 Q → E");
					MessageDuration = 4.f;
				}
				else if (GetEncounterId() == FName(TEXT("UpperSouthBoonGuardian")) && bQ)
				{
					PersonalStatus = bE ? TEXT("连招练习：Q → E")
						: TEXT("连招练习：火球术 → Q");
					MessageDuration = 4.f;
				}
				else if (EncounterType == EAuraEncounterType::Elite && bQ)
				{
					PersonalStatus = bE ? TEXT("连招练习：Q → E")
						: TEXT("连招练习：火球术 → Q");
					MessageDuration = 4.f;
				}
			}
			Controller->ShowCoopMessage(PersonalStatus, StatusColor,
				MessageDuration, Objective);
		}
	}
}

void AAuraEncounterVolume::SyncStateToController(AAuraPlayerController* Controller) const
{
	if (!Controller || EncounterState == EAuraEncounterState::Dormant) return;
	FString Message;
	FString Objective;
	FLinearColor Color(1.f, 0.68f, 0.15f);
	if (EncounterState == EAuraEncounterState::Arming)
	{
		return;
	}
	else if (EncounterState == EAuraEncounterState::LockedCombat)
	{
		Message = TEXT("已返回战斗区域");
		Objective = FString::Printf(TEXT("%s  |  剩余 %d 名敌人"),
			*EncounterTitle, RemainingEnemyCount);
		Color = FLinearColor(1.f, 0.25f, 0.07f);
	}
	else if (EncounterState == EAuraEncounterState::RewardAvailable)
	{
		Message = TEXT("奖励可领取");
		Objective = TEXT("领取奖励");
		Color = FLinearColor(0.24f, 1.f, 0.48f);
	}
	else return;
	Controller->ShowCoopMessage(Message, Color, 2.f, Objective);
}

void AAuraEncounterVolume::OnTriggerOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	StartEncounter(Cast<AAuraCharacter>(OtherActor), false);
}

void AAuraEncounterVolume::OnTriggerEndOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex)
{
	if (!HasAuthority() || EncounterState != EAuraEncounterState::Arming
		|| OtherActor != ActivePlayer.Get())
	{
		return;
	}
	UpdateArmingDwell();
}

void AAuraEncounterVolume::RefreshEditorVisuals()
{
	if (TriggerBox) TriggerBox->SetBoxExtent(TriggerExtent);
	if (EncounterLabel)
	{
		const TCHAR* TypeLabel = EncounterType == EAuraEncounterType::Ambush ? TEXT("伏击")
			: (EncounterType == EAuraEncounterType::Elite ? TEXT("精英")
				: (EncounterType == EAuraEncounterType::GuardedTreasure ? TEXT("宝藏")
					: (EncounterType == EAuraEncounterType::Recovery ? TEXT("恢复")
						: (EncounterType == EAuraEncounterType::Boss ? TEXT("首领") : TEXT("房间")))));
		EncounterLabel->SetText(FText::FromString(
			FString::Printf(TEXT("%s  %02d  %s"), TypeLabel, EncounterOrder, *EncounterTitle)));
		EncounterLabel->SetTextRenderColor(
			bFinalEncounter ? FColor(255, 55, 30)
				: (HasRoomReward() ? FColor(65, 190, 255) : FColor(255, 180, 70)));
	}
}
