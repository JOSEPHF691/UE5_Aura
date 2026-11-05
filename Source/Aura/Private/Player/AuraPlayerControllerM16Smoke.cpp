// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AuraAnimInstance.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "EngineUtils.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Game/AuraGameState.h"
#include "Game/AuraProfileSaveGame.h"
#include "Game/AuraRunSubsystem.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/AuraDungeonGate.h"
#include "Level/AuraEncounterVolume.h"
#include "Level/AuraRitualSite.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/AuraPlayerState.h"

namespace
{
	FString GetM16TestSlot()
	{
		FString Slot;
		if (!FParse::Value(FCommandLine::Get(), TEXT("AuraM16TestSlot="), Slot)
			|| !Slot.StartsWith(TEXT("AuraM16Smoke_")) || Slot.Len() > 64) return FString();
		for (const TCHAR Character : Slot)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_')) return FString();
		}
		return Slot;
	}
}

void AAuraPlayerController::AuraTestPackagedAnimationM16()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	UAuraAnimInstance* Anim = AuraPawn
		? Cast<UAuraAnimInstance>(AuraPawn->GetMesh()->GetAnimInstance()) : nullptr;
	UAnimSequence* CastSequence = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Assets/Characters/Aura/Animations/Abilities/Cast_FireBolt.Cast_FireBolt"));
	if (!Anim || !CastSequence || !Anim->MovementBlendSpace)
	{
		UE_LOG(LogTemp, Error, TEXT("AURA_M16_ANIMATION Passed=false Setup=false"));
		return;
	}
	UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(
		CastSequence, TEXT("DefaultSlot"), 0.02f, 0.15f, 1.f, 1, -1.f, 0.f);
	FTimerHandle VerifyTimer;
	GetWorldTimerManager().SetTimer(VerifyTimer, [Anim, Montage]()
	{
		const float SlotWeight = Anim->GetDefaultSlotWeight();
		const bool bPassed = Montage && Anim->GetCurrentActiveMontage() == Montage
			&& Anim->GetAnimationAsset() == Anim->MovementBlendSpace
			&& SlotWeight > 0.f;
		UE_LOG(LogTemp, Display,
			TEXT("AURA_M16_ANIMATION Passed=%s Montage=%s SlotWeight=%.2f BlendSpace=%s"),
			bPassed ? TEXT("true") : TEXT("false"), *GetNameSafe(Montage),
			SlotWeight, *GetNameSafe(Anim->GetAnimationAsset()));
	}, 0.15f, false);
}

void AAuraPlayerController::AuraTestSealChoiceM16()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	AAuraDungeonGate* BossGate = nullptr;
	bool bFourthSite = false;
	for (TActorIterator<AAuraRitualSite> It(GetWorld()); It; ++It)
	{
		if (It->IsBossAltar()) BossGate = It->GetBossGate();
		if (It->GetSealId() == FName(TEXT("UpperNorth"))) bFourthSite = true;
	}
	if (!Quest || !BossGate || !bFourthSite)
	{
		UE_LOG(LogTemp, Error, TEXT("AURA_M16_SEAL Passed=false Setup=false"));
		return;
	}
	const bool bFourSources = AAuraGameState::RequiredSealIds().Num() == 4;
	const bool bInitialClosed = !Quest->AreAllSealsActivated() && !BossGate->IsOpen();
	const bool bFirst = Quest->TryActivateSeal(TEXT("LowerNorth"));
	const bool bSecond = Quest->TryActivateSeal(TEXT("LowerSouth"));
	const bool bStillClosed = !Quest->AreAllSealsActivated() && !BossGate->IsOpen();
	const bool bThird = Quest->TryActivateSeal(TEXT("UpperNorth"));
	const bool bSkippedSouth = !Quest->HasActivatedSeal(TEXT("UpperSouth"));
	const bool bGateOpened = Quest->AreAllSealsActivated() && BossGate->IsOpen();
	const bool bSummonOnce = Quest->TryMarkBossSummoned() && !Quest->TryMarkBossSummoned();
	const bool bPassed = bFourSources && bInitialClosed && bFirst && bSecond
		&& bStillClosed && bThird && bSkippedSouth && bGateOpened && bSummonOnce;
	UE_LOG(LogTemp, Display,
		TEXT("AURA_M16_SEAL Passed=%s Sources=%d Count=%d SkippedSouth=%s Gate=%s SummonOnce=%s"),
		bPassed ? TEXT("true") : TEXT("false"), AAuraGameState::RequiredSealIds().Num(),
		Quest->GetActivatedSealCount(), bSkippedSouth ? TEXT("true") : TEXT("false"),
		bGateOpened ? TEXT("true") : TEXT("false"),
		bSummonOnce ? TEXT("true") : TEXT("false"));
}

void AAuraPlayerController::AuraTestAbyssTrialM16()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	static bool bPrepared = false;
	UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	if (!Run) return;
	if (!bPrepared)
	{
		bPrepared = true;
		Run->MarkRunCompleted();
		Run->RequestPostBossCamp();
		Run->ConsumePostBossCampRequest();
		const bool bStarted = Run->StartAbyssTrial();
		UE_LOG(LogTemp, Display,
			TEXT("M16_ABYSS_PREP Unlocked=%s Started=%s"),
			Run->IsAbyssTrialUnlocked() ? TEXT("true") : TEXT("false"),
			bStarted ? TEXT("true") : TEXT("false"));
		if (bStarted)
		{
			UGameplayStatics::OpenLevel(this,
				FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
		}
		return;
	}
	AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(GetPawn());
	AAuraEncounterVolume* Outer = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 1) Outer = *It;
	}
	if (!Quest || !PlayerCharacter || !Outer)
	{
		UE_LOG(LogTemp, Error, TEXT("AURA_M16_ABYSS Passed=false Setup=false"));
		return;
	}
	Outer->StartEncounter(PlayerCharacter, true);
	const TArray<AAuraEnemy*> Enemies = Outer->GetActiveEnemies();
	const bool bComposition = Enemies.Num() == 3
		&& Enemies[0]->GetEnemyArchetype() == EAuraEnemyArchetype::GoblinSpearman
		&& Enemies[1]->GetEnemyArchetype() == EAuraEnemyArchetype::ShamanElite
		&& Enemies[2]->GetEnemyArchetype() == EAuraEnemyArchetype::CaveShroom;
	const bool bHealth = Enemies.Num() == 3 && Enemies[1]->GetStartingHealth()
		>= UAuraCombatBalanceData::Get().Shaman.MaxHealth * 1.29f;
	const FAuraAbyssTrialBalance& Trial = UAuraCombatBalanceData::Get().AbyssTrial;
	const bool bTuning = FMath::IsNearlyEqual(Trial.MaxHealthMultiplier, 1.30f)
		&& FMath::IsNearlyEqual(Trial.DamageMultiplier, 1.15f)
		&& FMath::IsNearlyEqual(Trial.SoulMultiplier, 1.25f)
		&& FMath::IsNearlyEqual(Trial.OreMultiplier, 1.25f);
	const bool bPassed = Run->IsAbyssTrialUnlocked() && Run->IsAbyssTrialActive()
		&& Quest->IsAbyssTrialActive() && bComposition && bHealth && bTuning;
	UE_LOG(LogTemp, Display,
		TEXT("AURA_M16_ABYSS Passed=%s Active=%s Composition=%s Health=%s Enemies=%d"),
		bPassed ? TEXT("true") : TEXT("false"),
		Quest->IsAbyssTrialActive() ? TEXT("true") : TEXT("false"),
		bComposition ? TEXT("true") : TEXT("false"),
		bHealth ? TEXT("true") : TEXT("false"), Enemies.Num());
}

void AAuraPlayerController::AuraTestWriteLegacySaveM16()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const FString Slot = GetM16TestSlot();
	UAuraProfileSaveGame* Save = Cast<UAuraProfileSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAuraProfileSaveGame::StaticClass()));
	if (!State || !Save || Slot.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("AURA_M16_V1_SEED Passed=false Setup=false"));
		return;
	}
	Save->SchemaVersion = 1;
	Save->bHasProfile = true;
	Save->Growth = State->GetGrowthState();
	Save->bRunCompleted = true;
	Save->bAtPostBossCamp = true;
	const bool bSaved = UGameplayStatics::SaveGameToSlot(
		Save, Slot, 0);
	UE_LOG(LogTemp, Display, TEXT("AURA_M16_V1_SEED Passed=%s Version=1"),
		bSaved ? TEXT("true") : TEXT("false"));
}

void AAuraPlayerController::AuraTestVerifyLegacySaveM16()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	const FString Slot = GetM16TestSlot();
	if (Slot.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("AURA_M16_V1_MIGRATION Passed=false Slot=false"));
		return;
	}
	const bool bLoaded = Run && Run->IsAbyssTrialUnlocked()
		&& Run->IsAtPostBossCamp() && Run->IsRunCompleted();
	const bool bWritten = Run && Run->FlushPersistentSave();
	const UAuraProfileSaveGame* Save = Cast<UAuraProfileSaveGame>(
		UGameplayStatics::LoadGameFromSlot(Slot, 0));
	const bool bMigrated = Save && Save->SchemaVersion == UAuraProfileSaveGame::CurrentVersion
		&& Save->bAbyssTrialUnlocked && Save->bRunCompleted && Save->bAtPostBossCamp;
	UE_LOG(LogTemp, Display,
		TEXT("AURA_M16_V1_MIGRATION Passed=%s Loaded=%s Written=%s Migrated=%s Version=%d"),
		bLoaded && bWritten && bMigrated ? TEXT("true") : TEXT("false"),
		bLoaded ? TEXT("true") : TEXT("false"),
		bWritten ? TEXT("true") : TEXT("false"),
		bMigrated ? TEXT("true") : TEXT("false"), Save ? Save->SchemaVersion : -1);
}

void AAuraPlayerController::AuraTestLegendaryM16()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(GetPawn());
	UAuraInventoryComponent* Inventory = State ? State->GetInventoryComponent() : nullptr;
	UAuraEquipmentComponent* Equipment = State ? State->GetEquipmentComponent() : nullptr;
	UAuraItemDefinition* EmberDefinition = UAuraItemCatalog::Get().FindItem(
		TEXT("Equipment.EmberweaveStaff"));
	UAuraItemDefinition* StormDefinition = UAuraItemCatalog::Get().FindItem(
		TEXT("Equipment.StormcallRod"));
	if (!PlayerCharacter || !Inventory || !Equipment || !EmberDefinition || !StormDefinition
		|| !PrepareComboSmokeRanks({EAuraComboMarkType::Ember}))
	{
		UE_LOG(LogTemp, Error, TEXT("AURA_M16_LEGENDARY Passed=false Setup=false"));
		return;
	}
	FString Failure;
	FAuraItemInstance Ember;
	Ember.Definition = EmberDefinition;
	Ember.Rarity = EAuraItemRarity::Legendary;
	Ember.EnsureIdentity();
	const bool bEmberEquipped = Inventory->TryAddItem(Ember, Failure, false) == 1
		&& Equipment->TryEquipItem(Ember.InstanceId, Failure)
		&& PlayerCharacter->HasLegendaryComboEffect(TEXT("Item.Power.EmberEcho"));
	AAuraEnemy* Target = GetWorld()->SpawnActor<AAuraEnemy>(AAuraEnemy::StaticClass(),
		PlayerCharacter->GetActorLocation() + FVector(400.f, 0.f, 0.f),
		FRotator::ZeroRotator);
	bool bLongWindow = false;
	if (Target && bEmberEquipped)
	{
		PlayerCharacter->ApplyComboMark(Target, EAuraComboMarkType::Ember,
			PlayerCharacter->AllocateComboCastId());
		for (const FAuraComboMark& Mark : PlayerCharacter->GetComboMarks())
		{
			if (Mark.Target == Target && Mark.Type == EAuraComboMarkType::Ember)
			{
				bLongWindow = Mark.ExpireServerTime - PlayerCharacter->GetComboServerTime()
					> UAuraCombatBalanceData::Get().Combo.MarkDuration + 1.3f;
			}
		}
		Target->Destroy();
	}
	FAuraItemInstance Storm;
	Storm.Definition = StormDefinition;
	Storm.Rarity = EAuraItemRarity::Legendary;
	Storm.EnsureIdentity();
	const bool bStormEquipped = Inventory->TryAddItem(Storm, Failure, false) == 1
		&& Equipment->TryEquipItem(Storm.InstanceId, Failure)
		&& PlayerCharacter->HasLegendaryComboEffect(TEXT("Item.Power.FractureSurge"))
		&& !PlayerCharacter->HasLegendaryComboEffect(TEXT("Item.Power.EmberEcho"));
	FAuraItemInstance Common;
	Common.Definition = StormDefinition;
	Common.Rarity = EAuraItemRarity::Common;
	Common.EnsureIdentity();
	const bool bRarityGate = Inventory->TryAddItem(Common, Failure, false) == 1
		&& Equipment->TryEquipItem(Common.InstanceId, Failure)
		&& !PlayerCharacter->HasLegendaryComboEffect(TEXT("Item.Power.FractureSurge"));
	UE_LOG(LogTemp, Display,
		TEXT("AURA_M16_LEGENDARY Passed=%s Ember=%s LongWindow=%s Storm=%s RarityGate=%s"),
		bEmberEquipped && bLongWindow && bStormEquipped && bRarityGate
			? TEXT("true") : TEXT("false"),
		bEmberEquipped ? TEXT("true") : TEXT("false"),
		bLongWindow ? TEXT("true") : TEXT("false"),
		bStormEquipped ? TEXT("true") : TEXT("false"),
		bRarityGate ? TEXT("true") : TEXT("false"));
}
