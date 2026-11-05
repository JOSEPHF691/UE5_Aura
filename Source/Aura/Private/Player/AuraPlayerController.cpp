// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AI/AuraAIController.h"
#include "AbilitySystem/Abilities/AuraArcaneShardsAbility.h"
#include "AbilitySystem/Abilities/AuraBossAbility.h"
#include "AbilitySystem/Abilities/AuraFireBoltAbility.h"
#include "AbilitySystem/Abilities/AuraMeleeAbility.h"
#include "AbilitySystem/Abilities/AuraShamanBoltAbility.h"
#include "AbilitySystem/Abilities/AuraShockAbility.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraArcaneSlowField.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagContainer.h"
#include "Game/AuraRunSubsystem.h"
#include "Game/AuraLanSessionSubsystem.h"
#include "Game/AuraGameMode.h"
#include "Game/AuraGameState.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Components/CapsuleComponent.h"
#include "Coop/AuraReviveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Data/AuraLootProfileData.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Items/AuraWorldItem.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Pickup/AuraPickup.h"
#include "Player/AuraPlayerState.h"
#include "Sound/SoundBase.h"
#include "Level/AuraDungeonCheckpoint.h"
#include "Level/AuraArchmage.h"
#include "Level/AuraCampVendor.h"
#include "Level/AuraDungeonGate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Level/AuraEncounterVolume.h"
#include "Level/AuraLootChest.h"
#include "Level/AuraLoreRecord.h"
#include "Level/AuraRitualSite.h"
#include "Level/AuraTreasureChest.h"
#include "Loot/AuraLootSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"
#include "UI/AuraHUD.h"
#include "UI/AuraServiceMenuWidget.h"
#include "UnrealClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraInput, Log, All);

namespace
{
	const TCHAR* GetCameraModeName(const EAuraCameraMode)
	{
		return TEXT("GodView");
	}

	bool ObserveLiveShockNiagara(
		UWorld* World, const FVector& SelectedDirection, bool& bObserved, bool& bDirectionCorrect)
	{
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			const UNiagaraComponent* Component = *It;
			if (!Component || Component->IsTemplate() || Component->GetWorld() != World
				|| GetNameSafe(Component->GetAsset()) != TEXT("NS_ElectricBeam"))
			{
				continue;
			}

			bool bStartValid = false;
			bool bEndValid = false;
			const FVector LocalStart = Component->GetVariableVec3(TEXT("User.Beam Start"), bStartValid);
			const FVector WorldEnd = Component->GetVariableVec3(TEXT("User.Beam End"), bEndValid);
			const FVector WorldStart = Component->GetComponentTransform().TransformPosition(LocalStart);
			const FVector ResolvedDirection = (WorldEnd - WorldStart).GetSafeNormal2D();
			const float DirectionDot = FVector::DotProduct(SelectedDirection, ResolvedDirection);
			bObserved = bStartValid && bEndValid;
			bDirectionCorrect = bObserved && DirectionDot > 0.9999f;
			UE_LOG(LogAuraInput, Display,
				TEXT("Shock live Niagara observation. StartValid=%s EndValid=%s WorldStart=%s WorldEnd=%s DirectionDot=%.4f"),
				bStartValid ? TEXT("true") : TEXT("false"), bEndValid ? TEXT("true") : TEXT("false"),
				*WorldStart.ToCompactString(), *WorldEnd.ToCompactString(), DirectionDot);
			return bObserved;
		}
		return false;
	}

	bool ApplyEnemyDamageForSmokeTest(
		AAuraCharacter* PlayerCharacter, AAuraEnemy* Enemy, const float Damage)
	{
		UAbilitySystemComponent* PlayerAbilitySystem = PlayerCharacter
			? PlayerCharacter->GetAbilitySystemComponent() : nullptr;
		UAbilitySystemComponent* EnemyAbilitySystem = Enemy
			? Enemy->GetAbilitySystemComponent() : nullptr;
		if (!PlayerAbilitySystem || !EnemyAbilitySystem || !Enemy || Enemy->IsDead()) return false;

		FGameplayEffectContextHandle DamageContext = PlayerAbilitySystem->MakeEffectContext();
		DamageContext.AddSourceObject(PlayerCharacter);
		FGameplayEffectSpecHandle DamageSpec = PlayerAbilitySystem->MakeOutgoingSpec(
			UAuraDamageGameplayEffect::StaticClass(), 1.f, DamageContext);
		if (!DamageSpec.IsValid()) return false;
		DamageSpec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -Damage);
		EnemyAbilitySystem->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
		return true;
	}

	bool ApplyEnemyStunForSmokeTest(AAuraCharacter* PlayerCharacter, AAuraEnemy* Enemy)
	{
		UAbilitySystemComponent* PlayerAbilitySystem = PlayerCharacter
			? PlayerCharacter->GetAbilitySystemComponent() : nullptr;
		UAbilitySystemComponent* EnemyAbilitySystem = Enemy
			? Enemy->GetAbilitySystemComponent() : nullptr;
		if (!PlayerAbilitySystem || !EnemyAbilitySystem || !Enemy || Enemy->IsDead()) return false;

		FGameplayEffectContextHandle StunContext = PlayerAbilitySystem->MakeEffectContext();
		StunContext.AddSourceObject(PlayerCharacter);
		FGameplayEffectSpecHandle StunSpec = PlayerAbilitySystem->MakeOutgoingSpec(
			UAuraShockStunEffect::StaticClass(), 1.f, StunContext);
		if (!StunSpec.IsValid()) return false;
		StunSpec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")),
			UAuraCombatBalanceData::Get().Shock.StunDuration);
		EnemyAbilitySystem->ApplyGameplayEffectSpecToSelf(*StunSpec.Data.Get());
		return true;
	}

	int32 DefeatEncounterEnemiesForSmokeTest(AAuraEncounterVolume* Encounter)
	{
		if (!Encounter) return 0;
		AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(
			UGameplayStatics::GetPlayerCharacter(Encounter, 0));
		if (!PlayerCharacter || !PlayerCharacter->GetAbilitySystemComponent()) return 0;
		int32 DefeatedCount = 0;
		for (AAuraEnemy* Enemy : Encounter->GetActiveEnemies())
		{
			if (ApplyEnemyDamageForSmokeTest(PlayerCharacter, Enemy, 10000.f)
				&& Enemy->IsDead())
			{
				++DefeatedCount;
			}
		}
		UE_LOG(LogAuraInput, Display,
			TEXT("Dungeon smoke damage injected. Encounter=%d Defeated=%d Remaining=%d"),
			Encounter->GetEncounterOrder(), DefeatedCount, Encounter->GetActiveEnemyCount());
		return DefeatedCount;
	}

	bool StartBossForLegacySmokeTest(AAuraEncounterVolume* Boss,
		AAuraCharacter* Player)
	{
		if (!Boss || !Player || !Boss->IsFinalEncounter()) return false;
		AAuraGameState* State = Boss->GetWorld()->GetGameState<AAuraGameState>();
		if (!State) return false;
		for (const FName SealId : AAuraGameState::RequiredSealIds())
		{
			if (!State->HasActivatedSeal(SealId)) State->TryActivateSeal(SealId);
		}
		const bool bStarted = Boss->ActivateFromRitual(Player, true);
		if (bStarted) State->TryMarkBossSummoned();
		return bStarted;
	}
}

AAuraPlayerController::AAuraPlayerController()
{
	bReplicates = true;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallFadeAsset(
		TEXT("/Game/Aura/Materials/M_DungeonWallCameraFade.M_DungeonWallCameraFade"));
	if (WallFadeAsset.Succeeded()) CameraWallFadeMaterial = WallFadeAsset.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> InteractableGlowAsset(
		TEXT("/Game/Aura/Materials/M_InteractableGoldOutlineV2.M_InteractableGoldOutlineV2"));
	if (InteractableGlowAsset.Succeeded()) InteractableGlowMaterial = InteractableGlowAsset.Object;
}

void AAuraPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("AuraRescueCoopSmoke")))
	{
		FTimerHandle RescueSmokeStartTimer;
		GetWorldTimerManager().SetTimer(RescueSmokeStartTimer, this,
			&ThisClass::AuraTestCoopRescue, 1.f, false);
	}
	if (HasAuthority()
		&& GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& UGameplayStatics::GetCurrentLevelName(this, true).Equals(
			TEXT("DungeonDemo"), ESearchCase::IgnoreCase))
	{
		if (UNavigationSystemV1* NavigationSystem =
			FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			NavigationSystem->Build();
			UE_LOG(LogAuraInput, Display,
				TEXT("Dungeon dynamic navigation rebuild requested at level start."));
		}
	}
	RefreshInputContexts();
	ApplyCursorMode(GetCameraMode());
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraTargetingPolishSmoke")))
	{
		FTimerHandle TargetingPolishTimer;
		GetWorldTimerManager().SetTimer(TargetingPolishTimer, this,
			&AAuraPlayerController::AuraTestTargetingPolish, 1.5f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraFireBoltClickSmoke")))
	{
		FTimerHandle FireBoltClickTimer;
		GetWorldTimerManager().SetTimer(FireBoltClickTimer,
			[this]() { AuraTestFacingFireBolt(180.f, true); }, 1.5f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraFireBoltTurnSmoke")))
	{
		FTimerHandle FireBoltTurnTimer;
		GetWorldTimerManager().SetTimer(FireBoltTurnTimer, this,
			&AAuraPlayerController::AuraTestFireBoltTurn, 1.5f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraPursuitM25Smoke")))
	{
		FTimerHandle PursuitTimer;
		GetWorldTimerManager().SetTimer(PursuitTimer, this,
			&AAuraPlayerController::AuraTestPursuitM25, 1.5f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraInventoryMoveM25Smoke")))
	{
		FTimerHandle InventoryMoveTimer;
		GetWorldTimerManager().SetTimer(InventoryMoveTimer, this,
			&AAuraPlayerController::AuraTestInventoryMoveM25, 1.5f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraInventoryMoveCoopM25Smoke")))
	{
		FTimerHandle InventoryCoopTimer;
		GetWorldTimerManager().SetTimer(InventoryCoopTimer, this,
			&AAuraPlayerController::AuraTestInventoryMoveCoopServerM25, 4.f, false);
	}
	if (IsLocalController() && GetNetMode() == NM_Client
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraInventoryMoveCoopM25Observe")))
	{
		FTimerHandle InventoryCoopTimer;
		GetWorldTimerManager().SetTimer(InventoryCoopTimer, this,
			&AAuraPlayerController::AuraTestInventoryMoveCoopClientM25, 7.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraNetAbilitySmoke")))
	{
		FTimerHandle NetworkAbilitySmokeTimer;
		GetWorldTimerManager().SetTimer(
			NetworkAbilitySmokeTimer, this,
			&AAuraPlayerController::AuraTestNetworkAbilities, 1.5f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraComboSmoke")))
	{
		float SmokeDelay = 2.f;
		FParse::Value(FCommandLine::Get(), TEXT("AuraComboSmokeDelay="), SmokeDelay);
		FTimerHandle ComboSmokeTimer;
		GetWorldTimerManager().SetTimer(ComboSmokeTimer, this,
			&AAuraPlayerController::AuraTestCombos, FMath::Max(0.5f, SmokeDelay), false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraComboM18Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestComboM18, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEnemySynergyM19Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestEnemySynergyM19, 2.f, false);
	}
	if (IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("AuraComboObserve")))
	{
		float ObserveDelay = 3.5f;
		FParse::Value(FCommandLine::Get(), TEXT("AuraComboObserveDelay="), ObserveDelay);
		FTimerHandle ComboObserveTimer;
		GetWorldTimerManager().SetTimer(ComboObserveTimer, this,
			&AAuraPlayerController::AuraLogComboMarks, 0.25f, true,
			FMath::Max(0.5f, ObserveDelay - 3.f));
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSkillRankSmoke")))
	{
		FTimerHandle SkillRankSmokeTimer;
		GetWorldTimerManager().SetTimer(SkillRankSmokeTimer, this,
			&AAuraPlayerController::AuraTestSkillRanks, 2.f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraGrowthM12Smoke")))
	{
		FTimerHandle GrowthSmokeTimer;
		GetWorldTimerManager().SetTimer(GrowthSmokeTimer, this,
			&AAuraPlayerController::AuraTestGrowthM12, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraGrowthM21Smoke")))
	{
		FTimerHandle GrowthSmokeTimer;
		GetWorldTimerManager().SetTimer(GrowthSmokeTimer, this,
			&AAuraPlayerController::AuraTestGrowthM21, 2.f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraGrowthCoopM21Smoke")))
	{
		FTimerHandle GrowthCoopTimer;
		GetWorldTimerManager().SetTimer(GrowthCoopTimer, this,
			&AAuraPlayerController::AuraTestGrowthCoopM21, 10.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraGrowthCoopM21Observe")))
	{
		FTimerHandle GrowthObserveTimer;
		GetWorldTimerManager().SetTimer(GrowthObserveTimer, this,
			&AAuraPlayerController::AuraObserveGrowthM21, 12.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSaveReadOnlyM21Smoke")))
	{
		FTimerHandle SaveAuditTimer;
		GetWorldTimerManager().SetTimer(SaveAuditTimer, this,
			&AAuraPlayerController::AuraTestSaveReadOnlyM21, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraLoadedSaveM21Smoke")))
	{
		FTimerHandle SaveAuditTimer;
		GetWorldTimerManager().SetTimer(SaveAuditTimer, this,
			&AAuraPlayerController::AuraTestLoadedSaveM21, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSeedV2SaveM21Smoke")))
	{
		FTimerHandle SaveAuditTimer;
		GetWorldTimerManager().SetTimer(SaveAuditTimer, this,
			&AAuraPlayerController::AuraTestSeedV2SaveM21, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraFractureRankM21Smoke")))
	{
		FTimerHandle FractureTimer;
		GetWorldTimerManager().SetTimer(FractureTimer, this,
			&AAuraPlayerController::AuraTestFractureRankM21, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraRouteRewardsM22Smoke")))
	{
		FTimerHandle RewardTimer;
		GetWorldTimerManager().SetTimer(RewardTimer, this,
			&AAuraPlayerController::AuraTestRouteRewardsM22, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraFullSpineM22Smoke")))
	{
		FTimerHandle SpineTimer;
		GetWorldTimerManager().SetTimer(SpineTimer, this,
			&AAuraPlayerController::AuraTestFullSpineM22, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraRouteRewardsM22ReloadSmoke")))
	{
		FTimerHandle RewardTimer;
		GetWorldTimerManager().SetTimer(RewardTimer, this,
			&AAuraPlayerController::AuraTestRouteRewardsReloadM22, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraServiceUIM17Smoke")))
	{
		FTimerHandle ServiceUITimer;
		GetWorldTimerManager().SetTimer(ServiceUITimer, this,
			&AAuraPlayerController::AuraTestServiceUIM17, 2.f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraGrowthCoopM12Smoke")))
	{
		FTimerHandle GrowthCoopTimer;
		GetWorldTimerManager().SetTimer(GrowthCoopTimer, this,
			&AAuraPlayerController::AuraTestGrowthCoopM12, 5.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraGrowthCoopM12Observe")))
	{
		FTimerHandle GrowthObserveTimer;
		GetWorldTimerManager().SetTimer(GrowthObserveTimer, this,
			&AAuraPlayerController::AuraObserveGrowthM12, 8.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraPostBossCampM12Smoke")))
	{
		FTimerHandle PostBossTimer;
		GetWorldTimerManager().SetTimer(PostBossTimer, this,
			&AAuraPlayerController::AuraTestPostBossCampM12, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraVictoryReturnM12Smoke")))
	{
		FTimerHandle VictoryReturnTimer;
		GetWorldTimerManager().SetTimer(VictoryReturnTimer, this,
			&AAuraPlayerController::AuraTestVictoryReturnM12, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraCampEconomyM13Smoke")))
	{
		FTimerHandle CampEconomyTimer;
		GetWorldTimerManager().SetTimer(CampEconomyTimer, this,
			&AAuraPlayerController::AuraTestCampEconomyM13, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEconomyM23Smoke")))
	{
		FTimerHandle M23Timer;
		GetWorldTimerManager().SetTimer(M23Timer, this,
			&AAuraPlayerController::AuraTestEconomyM23, 2.f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEconomyCoopM23Smoke")))
	{
		FTimerHandle M23CoopTimer;
		GetWorldTimerManager().SetTimer(M23CoopTimer, this,
			&AAuraPlayerController::AuraTestEconomyCoopM23, 9.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEconomyCoopM23Observe")))
	{
		FTimerHandle M23ObserveTimer;
		GetWorldTimerManager().SetTimer(M23ObserveTimer, this,
			&AAuraPlayerController::AuraObserveEconomyM23, 16.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSeedV3SaveM23Smoke")))
	{
		FTimerHandle M23V3Timer;
		GetWorldTimerManager().SetTimer(M23V3Timer, this,
			&AAuraPlayerController::AuraTestSeedV3SaveM23, 2.f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraCampCoopM13Smoke")))
	{
		FTimerHandle CampCoopTimer;
		GetWorldTimerManager().SetTimer(CampCoopTimer, this,
			&AAuraPlayerController::AuraTestCampCoopM13, 5.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraCampCoopM13Observe")))
	{
		FTimerHandle CampObserveTimer;
		GetWorldTimerManager().SetTimer(CampObserveTimer, this,
			&AAuraPlayerController::AuraObserveCampM13, 8.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSealRitualM14Smoke")))
	{
		FTimerHandle SealTimer;
		GetWorldTimerManager().SetTimer(SealTimer, this,
			&AAuraPlayerController::AuraTestSealRitualM14, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraNavigationM20Smoke")))
	{
		FTimerHandle NavigationTimer;
		GetWorldTimerManager().SetTimer(NavigationTimer, this,
			&AAuraPlayerController::AuraTestDungeonNavigation, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraLoreM20Smoke")))
	{
		FTimerHandle LoreTimer;
		GetWorldTimerManager().SetTimer(LoreTimer, this,
			&AAuraPlayerController::AuraTestLoreM20, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraDungeonM21Smoke")))
	{
		FTimerHandle DungeonTimer;
		GetWorldTimerManager().SetTimer(DungeonTimer, this,
			&AAuraPlayerController::AuraTestDungeonM21, 8.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEliteVariantSmoke")))
	{
		FTimerHandle EliteTimer;
		GetWorldTimerManager().SetTimer(EliteTimer, this,
			&AAuraPlayerController::AuraTestEliteVariants, 4.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEliteSkillSmoke")))
	{
		FTimerHandle EliteSkillTimer;
		GetWorldTimerManager().SetTimer(EliteSkillTimer, this,
			&AAuraPlayerController::AuraTestEliteSkillKits, 1.2f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEnemySkillVFXSmoke")))
	{
		FTimerHandle EnemySkillVFXTimer;
		GetWorldTimerManager().SetTimer(EnemySkillVFXTimer, this,
			&AAuraPlayerController::AuraTestEnemySkillVFX, 1.5f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEnemyPoiseSmoke")))
	{
		FTimerHandle PoiseTimer;
		GetWorldTimerManager().SetTimer(PoiseTimer, this,
			&AAuraPlayerController::AuraTestEnemyPoise, 1.5f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraCameraWallSmoke")))
	{
		FTimerHandle CameraWallTimer;
		GetWorldTimerManager().SetTimer(CameraWallTimer, this,
			&AAuraPlayerController::AuraTestCameraWallFade, 1.5f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraInteractableGlowSmoke")))
	{
		FTimerHandle InteractableGlowTimer;
		GetWorldTimerManager().SetTimer(InteractableGlowTimer, this,
			&AAuraPlayerController::AuraTestInteractableGlow, 1.5f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraGateFadeSmoke")))
	{
		FTimerHandle GateFadeTimer;
		GetWorldTimerManager().SetTimer(GateFadeTimer, this,
			&AAuraPlayerController::AuraTestGateFade, 1.5f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AuraElementalSmoke")) && HasAuthority()
		&& GetWorld()->GetFirstPlayerController() == this)
	{
		FTimerHandle Test;
		GetWorldTimerManager().SetTimer(Test, this, &ThisClass::AuraTestElementalSkills,
			GetNetMode() == NM_Standalone ? 2.f : 7.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AuraElementalObserve")) && IsLocalController())
	{
		FTimerHandle Test;
		GetWorldTimerManager().SetTimer(Test, this, &ThisClass::AuraObserveElementalSkills, 1.5f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AuraElementalSave")) && HasAuthority() && IsLocalController())
	{
		FTimerHandle Test;
		GetWorldTimerManager().SetTimer(Test, this, &ThisClass::AuraTestElementalSave, 2.f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEliteReplicationServer")))
	{
		FTimerHandle EliteServerTimer;
		GetWorldTimerManager().SetTimer(EliteServerTimer, this,
			&AAuraPlayerController::AuraTestEliteReplicationServer, 5.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEliteReplicationObserve")))
	{
		FTimerHandle EliteObserveTimer;
		GetWorldTimerManager().SetTimer(EliteObserveTimer, this,
			&AAuraPlayerController::AuraObserveEliteVariants, 8.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEliteSkillReplicationObserve")))
	{
		FTimerHandle EliteSkillObserveTimer;
		GetWorldTimerManager().SetTimer(EliteSkillObserveTimer, this,
			&AAuraPlayerController::AuraObserveEliteSkillReplication, 1.5f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSealCoopM14Smoke")))
	{
		FTimerHandle SealCoopTimer;
		GetWorldTimerManager().SetTimer(SealCoopTimer, this,
			&AAuraPlayerController::AuraTestSealCoopM14, 5.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSealCoopM14Observe")))
	{
		FTimerHandle SealObserveTimer;
		GetWorldTimerManager().SetTimer(SealObserveTimer, this,
			&AAuraPlayerController::AuraObserveSealM14, 8.f, false);
	}
	if (HasAuthority() && IsLocalController() && GetNetMode() == NM_Standalone
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSeedSaveM15Smoke")))
	{
		FTimerHandle SaveSeedTimer;
		GetWorldTimerManager().SetTimer(SaveSeedTimer, this,
			&AAuraPlayerController::AuraTestSeedSaveM15, 2.f, false);
	}
	if (HasAuthority() && IsLocalController() && GetNetMode() == NM_Standalone
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraVerifySaveM15Smoke")))
	{
		FTimerHandle SaveVerifyTimer;
		GetWorldTimerManager().SetTimer(SaveVerifyTimer, this,
			&AAuraPlayerController::AuraTestVerifySaveM15, 2.f, false);
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetFirstPlayerController() == this
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraNetM15Smoke")))
	{
		FTimerHandle NetSetupTimer;
		GetWorldTimerManager().SetTimer(NetSetupTimer, this,
			&AAuraPlayerController::AuraTestNetSetupM15, 8.f, false);
	}
	if (IsLocalController() && !HasAuthority()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraNetM15Observe")))
	{
		FTimerHandle NetPurchaseTimer;
		GetWorldTimerManager().SetTimer(NetPurchaseTimer, this,
			&AAuraPlayerController::AuraTestNetPurchaseM15, 12.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraLanHostM15Smoke")))
	{
		FTimerHandle LanHostTimer;
		GetWorldTimerManager().SetTimer(LanHostTimer, this,
			&AAuraPlayerController::AuraTestLanHostM15, 2.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraLanJoinM15Smoke")))
	{
		FTimerHandle LanJoinTimer;
		GetWorldTimerManager().SetTimer(LanJoinTimer, this,
			&AAuraPlayerController::AuraTestLanJoinM15, 3.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraComboAbilitySmoke")))
	{
		float SmokeDelay = 2.f;
		FParse::Value(FCommandLine::Get(), TEXT("AuraComboAbilitySmokeDelay="), SmokeDelay);
		FTimerHandle ComboAbilitySmokeTimer;
		GetWorldTimerManager().SetTimer(ComboAbilitySmokeTimer, this,
			&AAuraPlayerController::AuraTestComboAbilities,
			FMath::Max(0.5f, SmokeDelay), false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraSealChoiceM16Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestSealChoiceM16, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraAbyssM16Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestAbyssTrialM16, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraWriteV1M16Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestWriteLegacySaveM16, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraVerifyV1M16Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestVerifyLegacySaveM16, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraLegendaryM16Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestLegendaryM16, 2.f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraAnimationM16Smoke")))
	{
		FTimerHandle SmokeTimer;
		GetWorldTimerManager().SetTimer(SmokeTimer, this,
			&AAuraPlayerController::AuraTestPackagedAnimationM16, 2.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraNetRespawnSmoke")))
	{
		FTimerHandle NetworkRespawnSmokeTimer;
		GetWorldTimerManager().SetTimer(
			NetworkRespawnSmokeTimer, this,
			&AAuraPlayerController::AuraTestNetworkRespawn, 1.5f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraInventorySmoke")))
	{
		FTimerHandle InventorySmokeTimer;
		GetWorldTimerManager().SetTimer(
			InventorySmokeTimer, this,
			&AAuraPlayerController::AuraTestInventory, 1.5f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraInventoryObserve")))
	{
		FTimerHandle InventoryObserveTimer;
		GetWorldTimerManager().SetTimer(
			InventoryObserveTimer, this,
			&AAuraPlayerController::AuraLogInventory, 4.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraManualPickupNetworkSmoke")))
	{
		FTimerHandle ManualPickupSmokeTimer;
		GetWorldTimerManager().SetTimer(
			ManualPickupSmokeTimer, this,
			&AAuraPlayerController::AuraTestManualPickupNetwork, 1.2f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEquipmentSmoke")))
	{
		FTimerHandle EquipmentSmokeTimer;
		GetWorldTimerManager().SetTimer(
			EquipmentSmokeTimer, this,
			&AAuraPlayerController::AuraTestEquipment, 1.8f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEquipmentObserve")))
	{
		FTimerHandle EquipmentObserveTimer;
		GetWorldTimerManager().SetTimer(
			EquipmentObserveTimer, this,
			&AAuraPlayerController::AuraLogEquipment, 5.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraLootSmoke")))
	{
		float LootSmokeDelay = 1.8f;
		FParse::Value(FCommandLine::Get(), TEXT("AuraLootSmokeDelay="), LootSmokeDelay);
		FTimerHandle LootSmokeTimer;
		GetWorldTimerManager().SetTimer(
			LootSmokeTimer, this,
			&AAuraPlayerController::AuraTestLoot, FMath::Max(0.1f, LootSmokeDelay), false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraLootObserve")))
	{
		FTimerHandle LootObserveTimer;
		GetWorldTimerManager().SetTimer(
			LootObserveTimer, this,
			&AAuraPlayerController::AuraLogLoot, 6.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraRoomSmoke")))
	{
		FTimerHandle RoomSmokeTimer;
		GetWorldTimerManager().SetTimer(
			RoomSmokeTimer, this,
			&AAuraPlayerController::AuraTestRoomStateMachine, 1.8f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraRoomObserve")))
	{
		FTimerHandle RoomObserveEarlyTimer;
		GetWorldTimerManager().SetTimer(
			RoomObserveEarlyTimer, this,
			&AAuraPlayerController::AuraLogRoomState, 3.f, false);
		FTimerHandle RoomObserveLateTimer;
		GetWorldTimerManager().SetTimer(
			RoomObserveLateTimer, this,
			&AAuraPlayerController::AuraLogRoomState, 12.f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraEnemyVarietySmoke")))
	{
		FTimerHandle EnemyVarietySmokeTimer;
		GetWorldTimerManager().SetTimer(
			EnemyVarietySmokeTimer, this,
			&AAuraPlayerController::AuraTestEnemyAttackVariety, 1.2f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraShamanGroundBurstSmoke")))
	{
		FTimerHandle ShamanGroundBurstSmokeTimer;
		GetWorldTimerManager().SetTimer(
			ShamanGroundBurstSmokeTimer, this,
			&AAuraPlayerController::AuraTestShamanRanged, 1.2f, false);
	}
	if (IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraBossArsenalSmoke")))
	{
		FTimerHandle BossArsenalSmokeTimer;
		GetWorldTimerManager().SetTimer(
			BossArsenalSmokeTimer, this,
			&AAuraPlayerController::AuraTestBossArsenal, 1.2f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& (FParse::Param(FCommandLine::Get(), TEXT("AuraBossBarrageSmoke"))
			|| FParse::Param(FCommandLine::Get(), TEXT("AuraBossBarrageCancelSmoke"))))
	{
		FTimerHandle BossBarrageSmokeTimer;
		GetWorldTimerManager().SetTimer(BossBarrageSmokeTimer, this,
			&AAuraPlayerController::AuraTestBossBarrage, 1.2f, false);
	}
	if (HasAuthority() && IsLocalController()
		&& FParse::Param(FCommandLine::Get(), TEXT("AuraBossWardSmoke")))
	{
		FTimerHandle BossWardSmokeTimer;
		GetWorldTimerManager().SetTimer(BossWardSmokeTimer, this,
			&AAuraPlayerController::AuraTestBossWard, 1.2f, false);
	}
	UE_LOG(LogAuraInput, Display, TEXT("Aura controller ready. Controller=%s Pawn=%s LocalPlayer=%s"),
		*GetClass()->GetName(), *GetNameSafe(GetPawn()), *GetNameSafe(GetLocalPlayer()));
}

void AAuraPlayerController::HandleDemoVictory(AAuraEnemy* DefeatedBoss)
{
	const float BossScale = DefeatedBoss
		? FMath::Clamp(DefeatedBoss->GetActorScale3D().GetMax(), 1.f, 3.f) : 1.f;
	const FVector BossFocusLocation = DefeatedBoss && DefeatedBoss->GetMesh()
		? DefeatedBoss->GetMesh()->Bounds.Origin
		: (DefeatedBoss ? DefeatedBoss->GetActorLocation() + FVector(0.f, 0.f, 80.f * BossScale)
			: (GetPawn() ? GetPawn()->GetActorLocation() + GetPawn()->GetActorForwardVector() * 450.f
				+ FVector(0.f, 0.f, 100.f) : FVector::ZeroVector));
	FVector CameraApproachDirection = DefeatedBoss && GetPawn()
		? GetPawn()->GetActorLocation() - DefeatedBoss->GetActorLocation()
		: (DefeatedBoss ? -DefeatedBoss->GetActorForwardVector() : FVector::BackwardVector);
	CameraApproachDirection.Z = 0.f;
	CameraApproachDirection = CameraApproachDirection.GetSafeNormal();
	if (CameraApproachDirection.IsNearlyZero()) CameraApproachDirection = FVector::BackwardVector;

	ApplyDemoVictory(DefeatedBoss, BossFocusLocation, CameraApproachDirection, BossScale);
	if (HasAuthority() && !IsLocalController())
	{
		ClientHandleDemoVictory(DefeatedBoss, BossFocusLocation, CameraApproachDirection, BossScale);
	}
}

void AAuraPlayerController::ShowCoopMessage(
	const FString& Message, const FLinearColor& Color,
	const float Duration, const FString& Objective)
{
	if (IsLocalController())
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			if (!Message.IsEmpty()) AuraHUD->ShowStatusMessage(Message, Color, Duration);
			if (!Objective.IsEmpty()) AuraHUD->SetObjective(Objective);
		}
		return;
	}
	if (HasAuthority()) ClientShowCoopMessage(Message, Color, Duration, Objective);
}

void AAuraPlayerController::ClientShowCoopMessage_Implementation(
	const FString& Message, const FLinearColor Color,
	const float Duration, const FString& Objective)
{
	ShowCoopMessage(Message, Color, Duration, Objective);
}

void AAuraPlayerController::ClearCoopGuidance()
{
	if (IsLocalController())
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			AuraHUD->SetObjective(TEXT(""));
			AuraHUD->ShowStatusMessage(TEXT(""), FLinearColor::Transparent, 0.1f);
		}
		return;
	}
	if (HasAuthority()) ClientClearCoopGuidance();
}

void AAuraPlayerController::ClientClearCoopGuidance_Implementation()
{
	ClearCoopGuidance();
}

void AAuraPlayerController::ServerRequestRespawn_Implementation()
{
	if (AAuraGameMode* AuraGameMode = GetWorld()->GetAuthGameMode<AAuraGameMode>())
	{
		AuraGameMode->RequestPlayerRespawn(this);
	}
}

void AAuraPlayerController::ServerRunNetworkRespawnSmoke_Implementation()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !AbilitySystem || AuraCharacter->IsDead()) return;

	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 0.f);
	UE_LOG(LogAuraInput, Display,
		TEXT("Network respawn smoke applied lethal authoritative health change. Character=%s"),
		*GetNameSafe(AuraCharacter));
}

void AAuraPlayerController::ClientHandleDemoVictory_Implementation(
	AAuraEnemy* DefeatedBoss, const FVector BossFocusLocation,
	const FVector CameraApproachDirection, const float BossScale)
{
	ApplyDemoVictory(DefeatedBoss, BossFocusLocation, CameraApproachDirection, BossScale);
}

void AAuraPlayerController::ApplyDemoVictory(
	AAuraEnemy* DefeatedBoss, const FVector& BossFocusLocation,
	const FVector& CameraApproachDirection, const float BossScale)
{
	if (bDemoCompleted) return;
	bDemoCompleted = true;
	bVictoryOverlayVisible = false;
	const float ScaleAlpha = FMath::Clamp((BossScale - 1.f) / 2.f, 0.f, 1.f);
	VictoryCameraScale = FMath::Lerp(1.f, 1.6f, ScaleAlpha);
	CancelArcaneShardsTargeting();
	CancelShockTargeting();
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
	{
		AuraHUD->SetObjective(TEXT("地牢通关"));
	}

	StartVictoryCinematic(DefeatedBoss, BossFocusLocation, CameraApproachDirection);
	if (GetWorld() && IsLocalController())
	{
		GetWorldTimerManager().SetTimer(
			VictoryStingTimer, this, &AAuraPlayerController::PlayVictorySting,
			VictoryStingDelay, false);
		GetWorldTimerManager().SetTimer(
			VictoryOverlayTimer, this, &AAuraPlayerController::RevealVictoryOverlay,
			VictoryOverlayDelay, false);
	}
	UE_LOG(LogAuraInput, Display,
		TEXT("Dungeon victory applied. Controller=%s Focus=%s OverlayDelay=%.2f"),
		*GetName(), *BossFocusLocation.ToCompactString(), VictoryOverlayDelay);
}

void AAuraPlayerController::StartVictoryCinematic(
	AAuraEnemy* DefeatedBoss, const FVector& BossFocusLocation,
	const FVector& CameraApproachDirection)
{
	VictoryBossActor = DefeatedBoss;
	VictoryFocusLocation = BossFocusLocation;
	FVector CurrentViewLocation;
	FRotator CurrentViewRotation;
	GetPlayerViewPoint(CurrentViewLocation, CurrentViewRotation);
	VictoryCameraApproachDirection = (CurrentViewLocation - BossFocusLocation).GetSafeNormal2D();
	if (VictoryCameraApproachDirection.IsNearlyZero())
	{
		VictoryCameraApproachDirection = CameraApproachDirection.GetSafeNormal2D();
	}
	if (VictoryCameraApproachDirection.IsNearlyZero())
	{
		VictoryCameraApproachDirection = FVector::BackwardVector;
	}
	VictoryCinematicElapsed = 0.f;
	bVictoryCinematicActive = IsLocalController() && GetWorld();
	if (!bVictoryCinematicActive) return;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	VictoryCameraActor = GetWorld()->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!VictoryCameraActor)
	{
		bVictoryCinematicActive = false;
		UE_LOG(LogAuraInput, Warning, TEXT("Victory cinematic could not spawn its camera."));
		return;
	}

	VictoryCameraActor->SetLifeSpan(15.f);
	if (UCameraComponent* CameraComponent = VictoryCameraActor->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(VictoryCameraFOV);
	}

	// Pick the closest clear angle to the current gameplay camera. The boss arena is
	// walled, so blindly backing away along the player vector can put the shot outside.
	const float DesiredDistance = VictoryCameraDistance * VictoryCameraScale;
	const float DesiredHeight = VictoryCameraHeight * VictoryCameraScale;
	const TArray<float> CandidateYawOffsets = {0.f, 35.f, -35.f, 70.f, -70.f, 110.f, -110.f, 180.f};
	float BestScore = -BIG_NUMBER;
	FVector BestDirection = VictoryCameraApproachDirection;
	FCollisionQueryParams CameraQuery(SCENE_QUERY_STAT(AuraVictoryCameraPlacement), false);
	CameraQuery.AddIgnoredActor(GetPawn());
	CameraQuery.AddIgnoredActor(VictoryCameraActor);
	if (DefeatedBoss) CameraQuery.AddIgnoredActor(DefeatedBoss);
	for (const float YawOffset : CandidateYawOffsets)
	{
		const FVector CandidateDirection = FRotator(0.f, YawOffset, 0.f)
			.RotateVector(VictoryCameraApproachDirection);
		const FVector CandidateLocation = BossFocusLocation
			+ CandidateDirection * DesiredDistance + FVector::UpVector * DesiredHeight;
		FHitResult PlacementHit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			PlacementHit, BossFocusLocation, CandidateLocation, ECC_Visibility, CameraQuery);
		const float AvailableDistance = FVector::Distance(
			BossFocusLocation, bBlocked ? PlacementHit.Location : CandidateLocation);
		const float Score = AvailableDistance - FMath::Abs(YawOffset) * 1.25f;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestDirection = CandidateDirection;
		}
	}
	VictoryCameraApproachDirection = BestDirection.GetSafeNormal2D();
	UpdateVictoryCinematic(0.f);
	SetViewTargetWithBlend(
		VictoryCameraActor, VictoryCameraBlendTime, VTBlend_Cubic, 0.f, true);
	SetCinematicMode(true, false, false, true, true);
	bShowMouseCursor = false;
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	UE_LOG(LogAuraInput, Display,
		TEXT("Victory cinematic started. Camera=%s Distance=%.1f Height=%.1f Orbit=%.1f Duration=%.2f"),
		*GetNameSafe(VictoryCameraActor), VictoryCameraDistance * VictoryCameraScale,
		VictoryCameraHeight * VictoryCameraScale, VictoryCameraOrbitDegrees,
		VictoryCameraMotionDuration);
}

void AAuraPlayerController::UpdateVictoryCinematic(const float DeltaTime)
{
	if (!bVictoryCinematicActive || !VictoryCameraActor) return;

	VictoryCinematicElapsed += FMath::Max(0.f, DeltaTime);
	if (VictoryBossActor && VictoryBossActor->GetMesh())
	{
		VictoryFocusLocation = FMath::VInterpTo(
			VictoryFocusLocation, VictoryBossActor->GetMesh()->Bounds.Origin,
			DeltaTime, 3.5f);
	}
	const float Progress = GetVictoryPresentationProgress();
	const float SmoothedProgress = FMath::SmoothStep(0.f, 1.f, Progress);
	const FRotator OrbitRotation(0.f, VictoryCameraOrbitDegrees * SmoothedProgress, 0.f);
	const FVector RadialDirection = OrbitRotation.RotateVector(VictoryCameraApproachDirection);
	const float CameraDistance = FMath::Lerp(
		VictoryCameraDistance, VictoryCameraDistance * 0.78f, SmoothedProgress)
		* VictoryCameraScale;
	const float CameraHeight = FMath::Lerp(
		VictoryCameraHeight, VictoryCameraHeight * 0.82f, SmoothedProgress)
		* VictoryCameraScale;
	FVector CameraLocation = VictoryFocusLocation
		+ RadialDirection * CameraDistance + FVector::UpVector * CameraHeight;
	FCollisionQueryParams CameraQuery(SCENE_QUERY_STAT(AuraVictoryCameraMotion), false);
	CameraQuery.AddIgnoredActor(GetPawn());
	CameraQuery.AddIgnoredActor(VictoryCameraActor);
	if (VictoryBossActor) CameraQuery.AddIgnoredActor(VictoryBossActor);
	FHitResult ObstructionHit;
	if (GetWorld()->LineTraceSingleByChannel(
		ObstructionHit, VictoryFocusLocation, CameraLocation, ECC_Visibility, CameraQuery))
	{
		CameraLocation = ObstructionHit.Location + ObstructionHit.ImpactNormal * 28.f;
	}
	VictoryCameraActor->SetActorLocationAndRotation(
		CameraLocation, (VictoryFocusLocation - CameraLocation).Rotation(), false, nullptr,
		ETeleportType::TeleportPhysics);
	if (Progress >= 1.f)
	{
		bVictoryCinematicActive = false;
		UE_LOG(LogAuraInput, Display, TEXT("Victory camera motion completed."));
	}
}

float AAuraPlayerController::GetVictoryPresentationProgress() const
{
	return VictoryCameraMotionDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp(VictoryCinematicElapsed / VictoryCameraMotionDuration, 0.f, 1.f)
		: 1.f;
}

void AAuraPlayerController::PlayVictorySting()
{
	if (!bDemoCompleted || !IsLocalController()) return;
	if (VictorySting)
	{
		UGameplayStatics::PlaySound2D(this, VictorySting, 0.82f);
	}
	UE_LOG(LogAuraInput, Display,
		TEXT("Victory sting played. Sound=%s"), *GetNameSafe(VictorySting));
}

void AAuraPlayerController::RevealVictoryOverlay()
{
	if (!bDemoCompleted || !IsLocalController()) return;
	bVictoryCinematicActive = false;
	bVictoryOverlayVisible = true;
	if (VictoryAccentSound)
	{
		UGameplayStatics::PlaySound2D(this, VictoryAccentSound, 0.48f);
	}
	UE_LOG(LogAuraInput, Display,
		TEXT("Victory overlay revealed. Accent=%s"), *GetNameSafe(VictoryAccentSound));
}

void AAuraPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdateCoopRescue();
	ObserveCoopRescueSmoke();
	UpdateVictoryCinematic(DeltaTime);
	UpdateCameraOcclusion();
	UpdateNearbyEquipment();
	UpdateInteractableHighlights();
	UpdateEnemyHoverHighlight();
	if (bLanMenuOpen)
	{
		const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
		if (!AuraPawn || AuraPawn->IsDead() || GetNetMode() != NM_Standalone)
		{
			bLanMenuOpen = false;
			ApplyCursorMode(GetCameraMode());
		}
		else return;
	}
	if (bCampMenuOpen)
	{
		FString Failure;
		if (!ActiveCampVendor.IsValid()
			|| !ActiveCampVendor->CanServe(Cast<AAuraCharacter>(GetPawn()), Failure))
		{
			CloseCampMenu();
		}
		return;
	}
	if (bArchmageMenuOpen)
	{
		FString Failure;
		if (!ActiveArchmage.IsValid()
			|| !ActiveArchmage->CanServe(Cast<AAuraCharacter>(GetPawn()), Failure))
		{
			CloseArchmageMenu();
		}
		return;
	}
	if (bInventoryOpen)
	{
		const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
		if (bDemoCompleted || !AuraCharacter || AuraCharacter->IsDead())
		{
			bInventoryOpen = false;
			SelectedInventorySlot = INDEX_NONE;
			ResetInventoryDrag();
			CloseInventoryContextMenu();
			ApplyCursorMode(GetCameraMode());
		}
		else UpdateInventoryDrag();
		return;
	}
	if (bIsRunUpgradeSelectionActive) return;
	if (!bIsArcaneShardsTargeting && !bIsShockTargeting) return;

	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || AuraCharacter->IsDead())
	{
		if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
		if (bIsShockTargeting) CancelShockTargeting();
		return;
	}

	if (bIsArcaneShardsTargeting) UpdateArcaneShardsTargeting();
	if (bIsShockTargeting) UpdateShockTargeting();
}

void AAuraPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreCameraOcclusion();
	ClearInteractableHighlights();
	ClearEnemyHoverHighlight();
	RemoveServiceMenuWidget();
	Super::EndPlay(EndPlayReason);
}

void AAuraPlayerController::RestoreCameraOcclusion()
{
	for (const TPair<TWeakObjectPtr<UStaticMeshComponent>, FAuraCameraFadedWall>& Entry
		: CameraFadedWalls)
	{
		if (UStaticMeshComponent* Wall = Entry.Key.Get())
			Wall->SetMaterial(0, Entry.Value.OriginalMaterial.Get());
	}
	CameraFadedWalls.Empty();
	for (const TWeakObjectPtr<AAuraDungeonGate>& Gate : CameraOccludedGates)
	{
		if (AAuraDungeonGate* ExistingGate = Gate.Get())
			ExistingGate->SetCameraOccluded(false);
	}
	CameraOccludedGates.Empty();
}

void AAuraPlayerController::UpdateCameraOcclusion()
{
	const AAuraCharacter* AuraPawn = IsLocalController()
		? Cast<AAuraCharacter>(GetViewTarget()) : nullptr;
	const UCameraComponent* Camera = AuraPawn ? AuraPawn->GetFollowCamera() : nullptr;
	if (!GetWorld() || !AuraPawn || !Camera || AuraPawn->IsDead()
		|| GetViewTarget() != AuraPawn)
	{
		if (!CameraFadedWalls.IsEmpty() || !CameraOccludedGates.IsEmpty())
			RestoreCameraOcclusion();
		return;
	}
	if (CameraOcclusionWorld.Get() != GetWorld())
	{
		RestoreCameraOcclusion();
		CameraOcclusionWorld = GetWorld();
		CameraOcclusionCandidates.Reset();
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			UStaticMeshComponent* MeshComponent = It->GetStaticMeshComponent();
			if (MeshComponent && MeshComponent->GetStaticMesh()
				&& MeshComponent->GetStaticMesh()->GetFName() == FName(TEXT("SM_Wall_4x2")))
			{
				CameraOcclusionCandidates.Add(MeshComponent);
			}
		}
		for (TActorIterator<AAuraDungeonGate> It(GetWorld()); It; ++It)
		{
			if (UStaticMeshComponent* GateVisual = It->GetGateVisual())
				CameraOcclusionCandidates.Add(GateVisual);
		}
	}

	const FVector CameraLocation = Camera->GetComponentLocation();
	const FVector CharacterFocus = AuraPawn->GetActorLocation() + FVector(0.f, 0.f, 95.f);
	TSet<TWeakObjectPtr<UStaticMeshComponent>> WallsInView;
	TSet<TWeakObjectPtr<AAuraDungeonGate>> GatesInView;
	for (const TWeakObjectPtr<UStaticMeshComponent>& Candidate : CameraOcclusionCandidates)
	{
		UStaticMeshComponent* Wall = Candidate.Get();
		if (!Wall || !Wall->GetStaticMesh()) continue;
		const FTransform& WallTransform = Wall->GetComponentTransform();
		const FVector LocalStart = WallTransform.InverseTransformPosition(CameraLocation);
		const FVector LocalEnd = WallTransform.InverseTransformPosition(CharacterFocus);
		if (!FMath::LineBoxIntersection(Wall->GetStaticMesh()->GetBoundingBox(),
			LocalStart, LocalEnd, LocalEnd - LocalStart)) continue;
		if (AAuraDungeonGate* Gate = Cast<AAuraDungeonGate>(Wall->GetOwner()))
		{
			GatesInView.Add(Gate);
			if (!CameraOccludedGates.Contains(Gate))
			{
				CameraOccludedGates.Add(Gate);
				Gate->SetCameraOccluded(true);
			}
			continue;
		}
		if (!CameraFadedWalls.Contains(Wall))
		{
			if (!CameraWallFadeMaterial || !Wall->IsVisible()) continue;
			UMaterialInterface* OriginalMaterial = Wall->GetMaterial(0);
			if (!OriginalMaterial) continue;
			UMaterialInstanceDynamic* Fade = Wall->CreateDynamicMaterialInstance(
				0, CameraWallFadeMaterial);
			if (!Fade) continue;
			FAuraCameraFadedWall State;
			State.OriginalMaterial = OriginalMaterial;
			CameraFadedWalls.Add(Wall, State);
		}
		WallsInView.Add(Wall);
	}
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	for (auto It = CameraFadedWalls.CreateIterator(); It; ++It)
	{
		UStaticMeshComponent* Wall = It.Key().Get();
		if (!Wall)
		{
			It.RemoveCurrent();
			continue;
		}
		UMaterialInstanceDynamic* Fade = Cast<UMaterialInstanceDynamic>(Wall->GetMaterial(0));
		if (!Fade)
		{
			// Another system has taken ownership of the material; do not overwrite it.
			It.RemoveCurrent();
			continue;
		}
		const bool bOccluded = WallsInView.Contains(It.Key());
		It.Value().Opacity = FMath::FInterpTo(It.Value().Opacity,
			bOccluded ? CameraOccludedWallOpacity : 1.f, DeltaSeconds, 10.f);
		Fade->SetScalarParameterValue(TEXT("OcclusionOpacity"), It.Value().Opacity);
		if (!bOccluded && It.Value().Opacity >= 0.99f)
		{
			Wall->SetMaterial(0, It.Value().OriginalMaterial.Get());
			It.RemoveCurrent();
		}
	}
	for (auto It = CameraOccludedGates.CreateIterator(); It; ++It)
	{
		if (!GatesInView.Contains(*It))
		{
			if (AAuraDungeonGate* Gate = It->Get()) Gate->SetCameraOccluded(false);
			It.RemoveCurrent();
		}
	}
}

bool AAuraPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (HandleCoopRescueInput(Params)) return true;
	// Closing a local reading panel must not depend on finding its marker again.
	// Consume E before Enhanced Input can start Shock targeting.
	if (IsLocalController() && Params.Event == IE_Pressed
		&& (Params.Key == EKeys::E || Params.Key == EKeys::F))
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
			AuraHUD && AuraHUD->IsLoreRecordVisible())
		{
			AuraHUD->HideLoreRecord();
			return true;
		}
	}
	if (IsLocalController() && GetNetMode() == NM_Standalone
		&& Params.Event == IE_Pressed && Params.Key == EKeys::M
		&& !bInventoryOpen && !bArchmageMenuOpen && !bCampMenuOpen
		&& !bIsRunUpgradeSelectionActive && !bDemoCompleted)
	{
		if (!bLanMenuOpen)
		{
			if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
			if (bIsShockTargeting) CancelShockTargeting();
		}
		bLanMenuOpen = !bLanMenuOpen;
		FlushPressedKeys();
		if (AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn()))
		{
			AuraPawn->ConsumeMovementInputVector();
			AuraPawn->GetCharacterMovement()->StopMovementImmediately();
		}
		ApplyCursorMode(GetCameraMode());
		return true;
	}
	if (bLanMenuOpen)
	{
		if (Params.Event == IE_Pressed)
		{
			if (Params.Key == EKeys::Escape)
			{
				bLanMenuOpen = false;
				ApplyCursorMode(GetCameraMode());
				return true;
			}
			UAuraLanSessionSubsystem* Lan = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UAuraLanSessionSubsystem>() : nullptr;
			if (Lan)
			{
				int32 Action = INDEX_NONE;
				if (Params.Key == EKeys::H) Action = 0;
				else if (Params.Key == EKeys::R) Action = 1;
				else if (Params.Key == EKeys::One) Action = 2;
				else if (Params.Key == EKeys::Two) Action = 3;
				else if (Params.Key == EKeys::Three) Action = 4;
				else if (Params.Key == EKeys::Four) Action = 5;
				else if (Params.Key == EKeys::Five) Action = 6;
				else if (Params.Key == EKeys::LeftMouseButton)
				{
					if (const AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD()))
						Action = HUD->GetHoveredLanRoomAction();
				}
				if (Action == 0) Lan->HostRoom();
				else if (Action == 1) Lan->SearchRooms();
				else if (Action >= 2) Lan->JoinRoom(Action - 2);
			}
		}
		const bool bMovementKey = Params.Key == EKeys::W || Params.Key == EKeys::A
			|| Params.Key == EKeys::S || Params.Key == EKeys::D;
		if (bMovementKey && Params.Event == IE_Released) return Super::InputKey(Params);
		return true;
	}
	if (bCampMenuOpen)
	{
		// The UMG overlay handles pointer clicks. Retain Canvas hit testing only
		// if the overlay could not be created, while keyboard shortcuts still work.
		const bool bMouseClick = !ActiveServiceMenuWidget
			&& Params.Key == EKeys::LeftMouseButton && Params.Event == IE_Released;
		if (Params.Event == IE_Pressed || bMouseClick)
		{
			if (Params.Key == EKeys::Escape || Params.Key == EKeys::N)
			{
				if (bCampConfirming) bCampConfirming = false;
				else CloseCampMenu();
				return true;
			}
			if (Params.Key == EKeys::F && !bCampConfirming)
			{
				CloseCampMenu();
				return true;
			}
			if (Params.Key == EKeys::Y || Params.Key == EKeys::Enter)
			{
				if (bCampConfirming) ConfirmCampAction();
				return true;
			}
			if (Params.Key == EKeys::Tab && ActiveCampVendor.IsValid()
				&& ActiveCampVendor->GetServiceType() == EAuraCampService::Merchant)
			{
				bCampSelling = !bCampSelling;
				CampPage = 0;
				bCampConfirming = false;
				return true;
			}
			if (Params.Key == EKeys::PageDown || Params.Key == EKeys::RightBracket)
			{
				CampPage = FMath::Min(CampPage + 1,
					FMath::Max(0, (BuildCampServiceRows().Num() - 1) / 8));
				bCampConfirming = false;
				return true;
			}
			if (Params.Key == EKeys::PageUp || Params.Key == EKeys::LeftBracket)
			{
				CampPage = FMath::Max(0, CampPage - 1);
				bCampConfirming = false;
				return true;
			}
			int32 Selection = INDEX_NONE;
			if (bMouseClick)
			{
				if (const AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD()))
				{
					// Use the action highlighted on the last HUD frame. A fresh
					// GetMousePosition during capture may differ or fail.
					Selection = HUD->GetHighlightedCampAction();
				}
			}
			else if (Params.Key == EKeys::One || Params.Key == EKeys::NumPadOne) Selection = 1;
			else if (Params.Key == EKeys::Two || Params.Key == EKeys::NumPadTwo) Selection = 2;
			else if (Params.Key == EKeys::Three || Params.Key == EKeys::NumPadThree) Selection = 3;
			else if (Params.Key == EKeys::Four || Params.Key == EKeys::NumPadFour) Selection = 4;
			else if (Params.Key == EKeys::Five || Params.Key == EKeys::NumPadFive) Selection = 5;
			else if (Params.Key == EKeys::Six || Params.Key == EKeys::NumPadSix) Selection = 6;
			else if (Params.Key == EKeys::Seven || Params.Key == EKeys::NumPadSeven) Selection = 7;
			else if (Params.Key == EKeys::Eight || Params.Key == EKeys::NumPadEight) Selection = 8;
			HandleCampMenuAction(Selection);
		}
		const bool bMovementKey = Params.Key == EKeys::W || Params.Key == EKeys::A
			|| Params.Key == EKeys::S || Params.Key == EKeys::D;
		if (bMovementKey && Params.Event == IE_Released) return Super::InputKey(Params);
		return true;
	}
	if (bArchmageMenuOpen)
	{
		const bool bMouseClick = !ActiveServiceMenuWidget
			&& Params.Key == EKeys::LeftMouseButton && Params.Event == IE_Released;
		if (Params.Event == IE_Pressed || bMouseClick)
		{
			if (Params.Key == EKeys::Escape || Params.Key == EKeys::N)
			{
				if (ArchmagePendingAction != 0) ArchmagePendingAction = 0;
				else CloseArchmageMenu();
				return true;
			}
			if (Params.Key == EKeys::F && ArchmagePendingAction == 0)
			{
				CloseArchmageMenu();
				return true;
			}
			if (ArchmagePendingAction == 5 && Params.Key == EKeys::BackSpace)
			{
				ResetMageDraft();
				return true;
			}
			if (Params.Key == EKeys::Y || Params.Key == EKeys::Enter)
			{
				if (ArchmagePendingAction != 0) ConfirmMageAction();
				return true;
			}
			int32 Action = INDEX_NONE;
			if (bMouseClick)
			{
				if (const AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD()))
				{
					Action = HUD->GetHighlightedMageAction();
				}
			}
			else if (Params.Key == EKeys::One || Params.Key == EKeys::NumPadOne) Action = 1;
			else if (Params.Key == EKeys::Two || Params.Key == EKeys::NumPadTwo) Action = 2;
			else if (Params.Key == EKeys::Three || Params.Key == EKeys::NumPadThree) Action = 3;
			else if (Params.Key == EKeys::Four || Params.Key == EKeys::NumPadFour) Action = 4;
			else if (Params.Key == EKeys::Five || Params.Key == EKeys::NumPadFive) Action = 5;
			HandleArchmageMenuAction(Action);
		}
		const bool bMovementKey = Params.Key == EKeys::W || Params.Key == EKeys::A
			|| Params.Key == EKeys::S || Params.Key == EKeys::D;
		if (bMovementKey && Params.Event == IE_Released) return Super::InputKey(Params);
		return true;
	}
	if (Params.Key == EKeys::B && Params.Event == IE_Pressed)
	{
		if (!bIsRunUpgradeSelectionActive) ToggleInventory();
		return true;
	}
	if (bIsRunUpgradeSelectionActive && Params.Event == IE_Pressed)
	{
		int32 OptionIndex = INDEX_NONE;
		if (Params.Key == EKeys::LeftMouseButton) OptionIndex = GetHoveredRunUpgradeIndex();
		else if (Params.Key == EKeys::One || Params.Key == EKeys::NumPadOne) OptionIndex = 0;
		else if (Params.Key == EKeys::Two || Params.Key == EKeys::NumPadTwo) OptionIndex = 1;
		else if (Params.Key == EKeys::Three || Params.Key == EKeys::NumPadThree) OptionIndex = 2;

		if (OptionIndex != INDEX_NONE)
		{
			ChooseRunUpgradeByIndex(OptionIndex);
			return true;
		}
		// The choice is mandatory. Consume combat/camera presses until one card is picked.
		if (Params.Key == EKeys::LeftMouseButton || Params.Key == EKeys::RightMouseButton
			|| Params.Key == EKeys::Q || Params.Key == EKeys::E
			|| Params.Key == EKeys::F)
		{
			return true;
		}
	}
	if (Params.Event == IE_Pressed && !bDemoCompleted)
	{
		if (Params.Key == EKeys::T && GetNetMode() == NM_Standalone)
		{
			if (UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
				Run && Run->StartAbyssTrial())
			{
				UGameplayStatics::OpenLevel(this,
					FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
				return true;
			}
		}
		if (Params.Key == EKeys::N && GetNetMode() == NM_Standalone)
		{
			if (UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
				Run && Run->IsAtPostBossCamp())
			{
				Run->ResetRun();
				UGameplayStatics::OpenLevel(this,
					FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
				return true;
			}
		}
		if (Params.Key == EKeys::One || Params.Key == EKeys::NumPadOne)
		{
			UseQuickConsumable(EAuraConsumableEffect::Health);
			return true;
		}
		if (Params.Key == EKeys::Two || Params.Key == EKeys::NumPadTwo)
		{
			UseQuickConsumable(EAuraConsumableEffect::Mana);
			return true;
		}
		if (Params.Key == EKeys::F)
		{
			if (!TryInteractWithArchmage() && !TryInteractWithCampVendor()
				&& !TryInteractWithRitualSite() && !TryInteractWithRewardChest()
				&& !TryInteractWithLoreRecord())
			{
				PickupHighlightedWorldItem();
			}
			return true;
		}
		if (bInventoryOpen && Params.Key == EKeys::Escape)
		{
			ToggleInventory();
			return true;
		}
	}
	if (bInventoryOpen)
	{
		if (Params.Key == EKeys::LeftMouseButton && Params.Event == IE_Released)
		{
			FinishInventoryDrag();
			return true;
		}
		if (Params.Event == IE_Pressed)
		{
			if (Params.Key == EKeys::Tab)
			{
				if (!bInventoryQuestPage && !bInventorySkillPage)
				{
					bInventoryQuestPage = true;
				}
				else if (bInventoryQuestPage)
				{
					bInventoryQuestPage = false;
					bInventorySkillPage = true;
				}
				else bInventorySkillPage = false;
				SelectedInventorySlot = INDEX_NONE;
				ResetInventoryDrag();
				CloseInventoryContextMenu();
				return true;
			}
			if ((bInventoryQuestPage || bInventorySkillPage)
				&& (Params.Key == EKeys::LeftMouseButton
				|| Params.Key == EKeys::RightMouseButton || Params.Key == EKeys::Delete))
			{
				return true;
			}
			if (Params.Key == EKeys::LeftMouseButton)
			{
				ResetInventoryDrag();
				HandleInventoryLeftClick();
				BeginInventoryDragCandidate();
				return true;
			}
			if (Params.Key == EKeys::RightMouseButton)
			{
				HandleInventoryRightClick();
				return true;
			}
			if (Params.Key == EKeys::Delete)
			{
				HandleInventoryDrop();
				return true;
			}
		}
		const bool bMovementKey = Params.Key == EKeys::W || Params.Key == EKeys::A
			|| Params.Key == EKeys::S || Params.Key == EKeys::D;
		if (bMovementKey && Params.Event == IE_Released)
		{
			// A movement key may have been pressed before the backpack opened. Its
			// release must still reach PlayerInput or Enhanced Input will retain a
			// permanently pressed key until the player presses it again.
			return Super::InputKey(Params);
		}
		if (Params.Key == EKeys::LeftMouseButton || Params.Key == EKeys::RightMouseButton
			|| Params.Key == EKeys::Q || Params.Key == EKeys::E || bMovementKey)
		{
			return true;
		}
	}
	// GameAndUI cursor mode can consume the Enhanced Input action before it reaches
	// PrimaryAttack. Confirm here and do not forward this press, so the same click
	// can never also enqueue a FireBolt activation.
	if (Params.Key == EKeys::LeftMouseButton && Params.Event == IE_Pressed)
	{
		if (bIsArcaneShardsTargeting)
		{
			ConfirmArcaneShardsTargeting();
			return true;
		}
		if (bIsShockTargeting)
		{
			ConfirmShockTargeting();
			return true;
		}
		if (HandleNearbyEquipmentClick()) return true;
		// Sample on mouse-down: viewport capture can otherwise shift the cursor
		// before the Enhanced Input callback activates the predicted ability.
		PendingFireBoltClickTarget = GetFireBoltCursorTarget();
		PendingFireBoltClickExpiry = GetWorld() ? GetWorld()->GetTimeSeconds() + 0.2f : -1.f;
		bHasPendingFireBoltClick = true;
	}
	return Super::InputKey(Params);
}

void AAuraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = CastChecked<UEnhancedInputComponent>(InputComponent);
	if (MoveAction) EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAuraPlayerController::Move);
	if (LookAction) EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAuraPlayerController::Look);
	if (PrimaryAttackAction) EnhancedInput->BindAction(PrimaryAttackAction, ETriggerEvent::Started, this, &AAuraPlayerController::PrimaryAttack);
	if (Ability1Action)
	{
		EnhancedInput->BindAction(Ability1Action, ETriggerEvent::Started, this, &AAuraPlayerController::ToggleArcaneShardsTargeting);
	}
	if (Ability2Action)
	{
		EnhancedInput->BindAction(Ability2Action, ETriggerEvent::Started, this, &AAuraPlayerController::ToggleShockTargeting);
	}
	if (RestartAction) EnhancedInput->BindAction(RestartAction, ETriggerEvent::Started, this, &AAuraPlayerController::RestartDemo);
}

void AAuraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (AAuraPlayerState* AuraState = GetPlayerState<AAuraPlayerState>())
	{
		AuraState->EnsureGrowthInitialized();
	}
	if (AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(InPawn))
	{
		AuraPawn->SyncGrowthFromPlayerState(true);
	}
	if (UAuraEquipmentComponent* Equipment = GetAuraEquipment())
	{
		Equipment->RefreshEquipmentEffects();
	}
	RefreshInputContexts();
	ApplyCursorMode(GetCameraMode());
}

void AAuraPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	if (bArchmageMenuOpen) CloseArchmageMenu();
	if (bCampMenuOpen) CloseCampMenu();
	RefreshInputContexts();
	ApplyCursorMode(GetCameraMode());
}

void AAuraPlayerController::SetAuraCameraMode(EAuraCameraMode)
{
	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	if (bIsShockTargeting) CancelShockTargeting();
	if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn()))
	{
		AuraCharacter->SetCameraMode(EAuraCameraMode::TopDown);
		RefreshInputContexts();
		ApplyCursorMode(EAuraCameraMode::TopDown);
	}
}

EAuraCameraMode AAuraPlayerController::GetCameraMode() const
{
	return EAuraCameraMode::TopDown;
}

FAuraAimResult AAuraPlayerController::GetAimResult(float) const
{
	FAuraAimResult Result;
	FHitResult Hit;
	if (bUseDebugAimOverride)
	{
		Result.bBlockingHit = true;
		Result.TraceStart = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		Result.Location = DebugAimOverride;
		Result.ImpactNormal = FVector::UpVector;
		Result.HitActor = DebugAimActorOverride.Get();
		return Result;
	}

	Result.bBlockingHit = GetHitResultUnderCursor(TargetingTraceChannel, false, Hit);
	Result.TraceStart = PlayerCameraManager
		? PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	if (!Result.bBlockingHit && GetPawn())
	{
		const FRotator YawRotation(0.f, GetControlRotation().Yaw, 0.f);
		Hit.Location = GetPawn()->GetActorLocation()
			+ FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * 1500.f;
	}

	Result.Location = Hit.Location;
	Result.ImpactNormal = Result.bBlockingHit ? Hit.ImpactNormal : FVector::UpVector;
	Result.HitActor = Hit.GetActor();
	return Result;
}

AAuraEnemy* AAuraPlayerController::GetFireBoltCursorTarget() const
{
	if (!IsLocalController()) return nullptr;
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!AuraPawn || AuraPawn->IsDead()) return nullptr;
	const FAuraAimResult Aim = GetAimResult(0.f);
	if (!Aim.bBlockingHit) return nullptr;
	if (AAuraEnemy* Direct = Cast<AAuraEnemy>(Aim.HitActor.Get()))
	{
		return UAuraFireBoltAbility::CanLockTarget(AuraPawn, Direct) ? Direct : nullptr;
	}
	// Permit a miss of no more than half the player's collision width beyond
	// the target's capsule. Only upward-facing ground on the same floor counts.
	if (Aim.ImpactNormal.Z < 0.55f || !GetWorld()) return nullptr;
	const UCapsuleComponent* PlayerCapsule = AuraPawn->GetCapsuleComponent();
	const float Margin = PlayerCapsule ? PlayerCapsule->GetScaledCapsuleRadius() : 42.f;
	AAuraEnemy* Best = nullptr;
	float BestGap = TNumericLimits<float>::Max();
	for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
	{
		AAuraEnemy* Candidate = *It;
		const UCapsuleComponent* Capsule = Candidate->GetCapsuleComponent();
		if (!Capsule) continue;
		const float EnemyFloorZ = Candidate->GetActorLocation().Z
			- Capsule->GetScaledCapsuleHalfHeight();
		if (FMath::Abs(Aim.Location.Z - EnemyFloorZ) > 120.f) continue;
		const float Gap = FVector::Dist2D(Aim.Location, Candidate->GetActorLocation())
			- Capsule->GetScaledCapsuleRadius();
		if (Gap <= Margin && Gap < BestGap
			&& UAuraFireBoltAbility::CanLockTarget(AuraPawn, Candidate))
		{
			BestGap = Gap;
			Best = Candidate;
		}
	}
	return Best;
}

AAuraEnemy* AAuraPlayerController::ConsumeFireBoltClickTarget()
{
	if (bHasPendingFireBoltClick)
	{
		bHasPendingFireBoltClick = false;
		AAuraEnemy* Clicked = PendingFireBoltClickTarget.Get();
		PendingFireBoltClickTarget.Reset();
		const bool bFreshClick = GetWorld()
			&& GetWorld()->GetTimeSeconds() <= PendingFireBoltClickExpiry;
		PendingFireBoltClickExpiry = -1.f;
		if (bFreshClick)
		{
			const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
			return UAuraFireBoltAbility::CanLockTarget(AuraPawn, Clicked) ? Clicked : nullptr;
		}
		return nullptr;
	}
	// Supports ability activation initiated outside the regular mouse input path.
	return GetFireBoltCursorTarget();
}

bool AAuraPlayerController::GetArcaneShardsTargetPreview(FVector& OutLocation, bool& bOutClamped) const
{
	if (!bIsArcaneShardsTargeting || !bArcaneShardsPreviewValid) return false;
	OutLocation = ArcaneShardsPreviewLocation;
	bOutClamped = bArcaneShardsTargetClamped;
	return true;
}

bool AAuraPlayerController::ConsumeArcaneShardsCastTarget(FVector& OutLocation)
{
	if (!bHasCommittedArcaneShardsTarget) return false;
	OutLocation = CommittedArcaneShardsTarget;
	bHasCommittedArcaneShardsTarget = false;
	return true;
}

bool AAuraPlayerController::GetShockTargetPreview(FVector& OutLocation, bool& bOutClamped) const
{
	if (!bIsShockTargeting || !bShockPreviewValid) return false;
	OutLocation = ShockPreviewLocation;
	bOutClamped = bShockTargetClamped;
	return true;
}

bool AAuraPlayerController::ConsumeShockCastDirection(FVector& OutDirection)
{
	if (!bHasCommittedShockDirection) return false;
	OutDirection = CommittedShockDirection;
	bHasCommittedShockDirection = false;
	return true;
}

bool AAuraPlayerController::BeginRunUpgradeSelection(AAuraTreasureChest* SourceChest)
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!HasAuthority() || !SourceChest || bIsRunUpgradeSelectionActive
		|| !AuraCharacter || AuraCharacter->IsDead() || bDemoCompleted)
	{
		return false;
	}

	const TArray<EAuraRunUpgradeType> Options = BuildRunUpgradeOptions(SourceChest);
	if (Options.IsEmpty()) return false;

	EnterRunUpgradeSelection(SourceChest, Options);
	if (!IsLocalController()) ClientBeginRunUpgradeSelection(SourceChest, Options);
	UE_LOG(LogAuraInput, Display,
		TEXT("Run upgrade selection started. Chest=%s Options=%d ExistingRanks=%d"),
		*GetNameSafe(SourceChest), Options.Num(), AuraCharacter->GetRunUpgradeState().GetTotalRanks());
	return true;
}

void AAuraPlayerController::ClientBeginRunUpgradeSelection_Implementation(
	AAuraTreasureChest* SourceChest, const TArray<EAuraRunUpgradeType>& Options)
{
	EnterRunUpgradeSelection(SourceChest, Options);
}

void AAuraPlayerController::EnterRunUpgradeSelection(
	AAuraTreasureChest* SourceChest, const TArray<EAuraRunUpgradeType>& Options)
{
	if (!SourceChest || Options.IsEmpty()) return;
	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	if (bIsShockTargeting) CancelShockTargeting();

	bIsRunUpgradeSelectionActive = true;
	PendingTreasureChest = SourceChest;
	RunUpgradeOptions = Options;
	if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn()))
	{
		AuraCharacter->SetRunUpgradeSelectionProtected(true);
		AuraCharacter->GetCharacterMovement()->StopMovementImmediately();
		if (UAbilitySystemComponent* AbilitySystem = AuraCharacter->GetAbilitySystemComponent())
		{
			AbilitySystem->CancelAllAbilities();
		}
	}

	if (!IsLocalController()) return;
	SetIgnoreMoveInput(true);
	ApplyCursorMode(GetCameraMode());
	if (GetWorld() && GetWorld()->GetGameViewport())
	{
		// SceneViewport only forwards the initial visible-cursor mouse-down to
		// PlayerController in this capture mode. NoCapture made the cards look
		// interactive while silently dropping every click.
		GetWorld()->GetGameViewport()->SetMouseCaptureMode(
			EMouseCaptureMode::CaptureDuringMouseDown);
	}
}

TArray<EAuraRunUpgradeType> AAuraPlayerController::BuildRunUpgradeOptions(
	const AAuraTreasureChest* SourceChest) const
{
	TArray<EAuraRunUpgradeType> Candidates;
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !SourceChest) return Candidates;

	const int32 MaxRank = UAuraCombatBalanceData::Get().RunUpgrades.MaxRankPerUpgrade;
	for (const EAuraRunUpgradeType Type : AuraRunUpgrade::GetAllTypes())
	{
		if (AuraCharacter->GetRunUpgradeRank(Type) < MaxRank) Candidates.Add(Type);
	}

	const uint32 Seed = HashCombine(
		GetTypeHash(SourceChest->GetTreasureId()),
		GetTypeHash(AuraCharacter->GetRunUpgradeState().GetTotalRanks() + 7919));
	FRandomStream RandomStream(Seed);
	for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
	{
		Candidates.Swap(Index, RandomStream.RandRange(0, Index));
	}
	if (Candidates.Num() > 3) Candidates.SetNum(3);
	return Candidates;
}

int32 AAuraPlayerController::GetHoveredRunUpgradeIndex() const
{
	if (!bIsRunUpgradeSelectionActive || RunUpgradeOptions.IsEmpty()) return INDEX_NONE;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY)) return INDEX_NONE;
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	const FVector2D ViewportSize(static_cast<float>(ViewportX), static_cast<float>(ViewportY));
	const FVector2D MousePosition(MouseX, MouseY);
	for (int32 Index = 0; Index < RunUpgradeOptions.Num(); ++Index)
	{
		if (AuraRunUpgrade::GetSelectionCardRect(
			ViewportSize, Index, RunUpgradeOptions.Num()).IsInside(MousePosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool AAuraPlayerController::ChooseRunUpgradeByIndex(const int32 OptionIndex)
{
	if (!bIsRunUpgradeSelectionActive || !RunUpgradeOptions.IsValidIndex(OptionIndex)
		|| !PendingTreasureChest)
	{
		return false;
	}

	const EAuraRunUpgradeType ChosenType = RunUpgradeOptions[OptionIndex];
	if (HasAuthority()) return ResolveRunUpgradeSelection(PendingTreasureChest, ChosenType);
	ServerChooseRunUpgrade(PendingTreasureChest, ChosenType);
	return true;
}

void AAuraPlayerController::ServerChooseRunUpgrade_Implementation(
	AAuraTreasureChest* SourceChest, const EAuraRunUpgradeType Type)
{
	ResolveRunUpgradeSelection(SourceChest, Type);
}

bool AAuraPlayerController::ResolveRunUpgradeSelection(
	AAuraTreasureChest* SourceChest, const EAuraRunUpgradeType Type)
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!HasAuthority() || !bIsRunUpgradeSelectionActive || !SourceChest
		|| SourceChest != PendingTreasureChest || !RunUpgradeOptions.Contains(Type)
		|| !AuraCharacter || !AuraCharacter->ApplyRunUpgrade(Type))
	{
		return false;
	}

	const int32 NewRank = AuraCharacter->GetRunUpgradeRank(Type);
	EndRunUpgradeSelection(Type, NewRank, IsLocalController());
	if (!IsLocalController()) ClientCompleteRunUpgradeSelection(Type, NewRank);
	return true;
}

void AAuraPlayerController::ClientCompleteRunUpgradeSelection_Implementation(
	const EAuraRunUpgradeType Type, const int32 NewRank)
{
	EndRunUpgradeSelection(Type, NewRank, true);
}

void AAuraPlayerController::EndRunUpgradeSelection(
	const EAuraRunUpgradeType ChosenType, const int32 NewRank, const bool bShowFeedback)
{
	if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn()))
	{
		AuraCharacter->SetRunUpgradeSelectionProtected(false);
	}
	bIsRunUpgradeSelectionActive = false;
	PendingTreasureChest = nullptr;
	RunUpgradeOptions.Reset();

	if (IsLocalController())
	{
		SetIgnoreMoveInput(false);
		ApplyCursorMode(GetCameraMode());
		if (bShowFeedback)
		{
			if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
			{
				AuraHUD->ShowStatusMessage(
					FString::Printf(TEXT("已获得增益  |  %s  等级 %d"),
						*AuraRunUpgrade::GetTitle(ChosenType), NewRank),
					AuraRunUpgrade::GetColor(ChosenType), 2.25f);
			}
		}
	}
	UE_LOG(LogAuraInput, Display,
		TEXT("Run upgrade selection completed. Type=%s Rank=%d"),
		*AuraRunUpgrade::GetTitle(ChosenType), NewRank);
}

void AAuraPlayerController::AuraTestMoveForward()
{
	if (!GetPawn()) return;
	MovementSmokeTestStart = GetPawn()->GetActorLocation();
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.f));

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.f));
		const float Distance = GetPawn() ? FVector::Dist2D(MovementSmokeTestStart, GetPawn()->GetActorLocation()) : 0.f;
		UE_LOG(LogAuraInput, Display, TEXT("W-key movement smoke test completed. Distance=%.2f"), Distance);
	}, 1.f, false);
}

void AAuraPlayerController::AuraTestCameraKey()
{
	SetAuraCameraMode(EAuraCameraMode::TopDown);
	const EAuraCameraMode ModeBeforeInput = GetCameraMode();
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V, IE_Pressed, 1.f));
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [this, ModeBeforeInput]()
	{
		const bool bPassed = ModeBeforeInput == EAuraCameraMode::TopDown
			&& GetCameraMode() == EAuraCameraMode::TopDown
			&& bShowMouseCursor;
		UE_LOG(LogAuraInput, Display,
			TEXT("Single God-view camera smoke test completed. Passed=%s Before=%s After=%s VHasNoEffect=true Cursor=%s"),
			bPassed ? TEXT("true") : TEXT("false"), GetCameraModeName(ModeBeforeInput),
			GetCameraModeName(GetCameraMode()), bShowMouseCursor ? TEXT("true") : TEXT("false"));
	}, 0.15f, false);
}

void AAuraPlayerController::AuraTestLookInput()
{
	AuraTestTopDownLook();
}

void AAuraPlayerController::AuraTestStrafeKey()
{
	AuraTestTurnMovement(TEXT("A"));
}

void AAuraPlayerController::AuraTestTurnMovement(FString KeyName)
{
	SetAuraCameraMode(EAuraCameraMode::TopDown);
	if (!GetPawn()) return;
	KeyName = KeyName.ToUpper();
	FKey TestKey;
	if (KeyName == TEXT("A")) TestKey = EKeys::A;
	else if (KeyName == TEXT("S")) TestKey = EKeys::S;
	else if (KeyName == TEXT("D")) TestKey = EKeys::D;
	else
	{
		UE_LOG(LogAuraInput, Error, TEXT("Turn-movement smoke test only supports A, S, or D."));
		return;
	}

	const FVector LocationBeforeInput = GetPawn()->GetActorLocation();
	const float ActorYawBeforeInput = GetPawn()->GetActorRotation().Yaw;
	const float CameraYawBeforeInput = GetControlRotation().Yaw;
	InputKey(FInputKeyEventArgs::CreateSimulated(TestKey, IE_Pressed, 1.f));

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle,
		[this, TestKey, KeyName, LocationBeforeInput, ActorYawBeforeInput, CameraYawBeforeInput]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(TestKey, IE_Released, 0.f));
		const APawn* PlayerPawn = GetPawn();
		const FVector LocationAfterInput = PlayerPawn ? PlayerPawn->GetActorLocation() : LocationBeforeInput;
		const FVector MoveDirection = (LocationAfterInput - LocationBeforeInput).GetSafeNormal2D();
		const float FacingDot = PlayerPawn ? FVector::DotProduct(PlayerPawn->GetActorForwardVector(), MoveDirection) : -1.f;
		UE_LOG(LogAuraInput, Display,
			TEXT("%s-key turn-movement smoke test completed. Passed=%s Distance=%.2f ActorYawDelta=%.2f CameraYawDelta=%.2f FacingDot=%.3f"),
			*KeyName, FVector::Dist2D(LocationBeforeInput, LocationAfterInput) > 100.f && FacingDot > 0.9f
				&& FMath::Abs(FMath::FindDeltaAngleDegrees(CameraYawBeforeInput, GetControlRotation().Yaw)) < 0.1f
				? TEXT("true") : TEXT("false"),
			FVector::Dist2D(LocationBeforeInput, LocationAfterInput),
			PlayerPawn ? FMath::FindDeltaAngleDegrees(ActorYawBeforeInput, PlayerPawn->GetActorRotation().Yaw) : 0.f,
			FMath::FindDeltaAngleDegrees(CameraYawBeforeInput, GetControlRotation().Yaw), FacingDot);
	}, 0.75f, false);
}

void AAuraPlayerController::AuraTestTopDownLook()
{
	SetAuraCameraMode(EAuraCameraMode::TopDown);
	const FRotator RotationBeforeInput = GetControlRotation();
	const float CameraYawBeforeInput = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : RotationBeforeInput.Yaw;

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem || !LookAction)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Top-down mouse-look smoke test could not inject IA_Look."));
		return;
	}

	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Pressed, 1.f));
	Subsystem->InjectInputForAction(LookAction, FInputActionValue(FVector2D(12.f, -6.f)), {}, {});

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, RotationBeforeInput, CameraYawBeforeInput]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Released, 0.f));
		const FRotator RotationAfterInput = GetControlRotation();
		const float CameraYawAfterInput = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : RotationAfterInput.Yaw;
		const float YawDelta = FMath::FindDeltaAngleDegrees(RotationBeforeInput.Yaw, RotationAfterInput.Yaw);
		const float PitchDelta = FMath::FindDeltaAngleDegrees(RotationBeforeInput.Pitch, RotationAfterInput.Pitch);
		const float CameraYawDelta = FMath::FindDeltaAngleDegrees(CameraYawBeforeInput, CameraYawAfterInput);
		const bool bPassed = FMath::Abs(YawDelta) > 0.1f && FMath::Abs(PitchDelta) < 0.1f
			&& FMath::Abs(CameraYawDelta) > 0.1f && GetCameraMode() == EAuraCameraMode::TopDown;
		UE_LOG(LogAuraInput, Display,
			TEXT("God-view RMB look smoke test completed. Passed=%s YawDelta=%.2f PitchDelta=%.2f CameraYawDelta=%.2f"),
			bPassed ? TEXT("true") : TEXT("false"), YawDelta, PitchDelta, CameraYawDelta);
	}, 0.25f, false);
}

void AAuraPlayerController::AuraTestTopDownMovement()
{
	SetAuraCameraMode(EAuraCameraMode::TopDown);
	if (!GetPawn()) return;

	const FVector AStartLocation = GetPawn()->GetActorLocation();
	const float AStartCameraYaw = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : GetControlRotation().Yaw;
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::A, IE_Pressed, 1.f));

	FTimerHandle ATestTimer;
	GetWorldTimerManager().SetTimer(ATestTimer, [this, AStartLocation, AStartCameraYaw]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::A, IE_Released, 0.f));
		const FVector AEndLocation = GetPawn() ? GetPawn()->GetActorLocation() : AStartLocation;
		const float AEndCameraYaw = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : GetControlRotation().Yaw;
		UE_LOG(LogAuraInput, Display, TEXT("Top-down A-key smoke test completed. Distance=%.2f CameraYawDelta=%.2f"),
			FVector::Dist2D(AStartLocation, AEndLocation),
			FMath::FindDeltaAngleDegrees(AStartCameraYaw, AEndCameraYaw));

		const FVector SStartLocation = GetPawn() ? GetPawn()->GetActorLocation() : AEndLocation;
		const float SStartCameraYaw = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : GetControlRotation().Yaw;
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::S, IE_Pressed, 1.f));

		FTimerHandle STestTimer;
		GetWorldTimerManager().SetTimer(STestTimer, [this, SStartLocation, SStartCameraYaw]()
		{
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::S, IE_Released, 0.f));
			const FVector SEndLocation = GetPawn() ? GetPawn()->GetActorLocation() : SStartLocation;
			const float SEndCameraYaw = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : GetControlRotation().Yaw;
			UE_LOG(LogAuraInput, Display, TEXT("Top-down S-key smoke test completed. Distance=%.2f CameraYawDelta=%.2f"),
				FVector::Dist2D(SStartLocation, SEndLocation),
				FMath::FindDeltaAngleDegrees(SStartCameraYaw, SEndCameraYaw));
		}, 0.75f, false);
	}, 0.75f, false);
}

void AAuraPlayerController::AuraLogCharacterPresentation()
{
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !AuraCharacter->GetWeaponMesh())
	{
		UE_LOG(LogAuraInput, Error, TEXT("Character presentation diagnostics could not find the character or weapon mesh."));
		return;
	}

	const USkeletalMeshComponent* CharacterMesh = AuraCharacter->GetMesh();
	const USkeletalMeshComponent* WeaponMesh = AuraCharacter->GetWeaponMesh();
	const FName SocketName = WeaponMesh->GetAttachSocketName();
	const FTransform SocketWorldTransform = CharacterMesh->GetSocketTransform(SocketName, RTS_World);
	const FTransform WeaponWorldTransform = WeaponMesh->GetComponentTransform();
	const FVector CurrentStaffUp = WeaponWorldTransform.GetUnitAxis(EAxis::Z);
	const FQuat WorldAlignment = FQuat::FindBetweenNormals(CurrentStaffUp, FVector::UpVector);

	FTransform DesiredWeaponWorldTransform = WeaponWorldTransform;
	DesiredWeaponWorldTransform.SetRotation(WorldAlignment * WeaponWorldTransform.GetRotation());
	const FTransform SuggestedRelativeTransform = DesiredWeaponWorldTransform.GetRelativeTransform(SocketWorldTransform);

	UAnimInstance* AnimationInstance = CharacterMesh->GetAnimInstance();
	UE_LOG(LogAuraInput, Display,
		TEXT("Character presentation. AnimInstance=%s ActiveMontage=%s MovementSpeed=%.2f WeaponRelativeRotation=%s StaffUp=%s SuggestedUprightRotation=%s"),
		*GetNameSafe(AnimationInstance),
		*GetNameSafe(AnimationInstance ? AnimationInstance->GetCurrentActiveMontage() : nullptr),
		AuraCharacter->GetVelocity().Size2D(),
		*WeaponMesh->GetRelativeRotation().ToCompactString(),
		*CurrentStaffUp.ToCompactString(),
		*SuggestedRelativeTransform.Rotator().ToCompactString());
}

void AAuraPlayerController::AuraCaptureMovementPresentation()
{
	SetAuraCameraMode(EAuraCameraMode::TopDown);
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.f));

	FTimerHandle CaptureTimer;
	GetWorldTimerManager().SetTimer(CaptureTimer, [this]()
	{
		AuraLogCharacterPresentation();
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.6f, false);

	FTimerHandle ReleaseTimer;
	GetWorldTimerManager().SetTimer(ReleaseTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.f));
	}, 1.2f, false);
}

void AAuraPlayerController::AuraTestFireBolt(const bool)
{
	const APawn* ControlledPawn = GetPawn();
	AuraTestFacingFireBolt(ControlledPawn ? ControlledPawn->GetActorRotation().Yaw : 0.f, true);
}

void AAuraPlayerController::AuraTestFacingFireBolt(const float FacingYaw, const bool)
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter) return;
	const bool bIsolatedSmoke = FParse::Param(
		FCommandLine::Get(), TEXT("AuraFireBoltClickSmoke"));
	const FTransform OriginalTransform = AuraCharacter->GetActorTransform();
	const EMovementMode OriginalMovementMode =
		AuraCharacter->GetCharacterMovement()->MovementMode;
	const float OriginalGravity = AuraCharacter->GetCharacterMovement()->GravityScale;
	if (bIsolatedSmoke)
	{
		// Keep the test projectile away from the dungeon's nearby walls so its
		// direction can still be observed after the release animation.
		AuraCharacter->TeleportTo(OriginalTransform.GetLocation()
			+ FVector(0.f, 0.f, 2200.f), OriginalTransform.Rotator(), false, true);
		AuraCharacter->GetCharacterMovement()->GravityScale = 0.f;
		AuraCharacter->GetCharacterMovement()->DisableMovement();
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	const UGameViewportClient* GameViewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	const bool bClickRoutingConfigured = GameViewport
		&& GameViewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown;
	bUseDebugAimOverride = true;
	DebugAimActorOverride.Reset();
	AuraCharacter->SetActorRotation(FRotator(0.f, FacingYaw, 0.f));
	const FVector ExpectedDirection = AuraCharacter->GetActorForwardVector().GetSafeNormal2D();
	DebugAimOverride = AuraCharacter->GetActorLocation() + ExpectedDirection * 300.f;
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	const TWeakObjectPtr<AAuraEnemy> WeakEnemy(Enemies.IsEmpty() ? nullptr : Cast<AAuraEnemy>(Enemies[0]));
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
	FTimerHandle ReleaseTimer;
	GetWorldTimerManager().SetTimer(ReleaseTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.f));
	}, 0.05f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakCharacter, WeakEnemy, ExpectedDirection, FacingYaw,
			bClickRoutingConfigured, bIsolatedSmoke, OriginalTransform,
			OriginalMovementMode, OriginalGravity]()
	{
		bUseDebugAimOverride = false;
		TArray<AActor*> Projectiles;
		UGameplayStatics::GetAllActorsOfClass(this, AAuraFireBoltProjectile::StaticClass(), Projectiles);
		const AActor* Projectile = Projectiles.IsEmpty() ? nullptr : Projectiles.Last();
		const FVector ProjectileDirection = Projectile
			? Projectile->GetActorForwardVector().GetSafeNormal2D() : FVector::ZeroVector;
		const float FacingDot = FVector::DotProduct(ExpectedDirection, ProjectileDirection);
		const float ActorYawDelta = WeakCharacter.IsValid()
			? FMath::Abs(FMath::FindDeltaAngleDegrees(FacingYaw, WeakCharacter->GetActorRotation().Yaw)) : 180.f;
		const UAbilitySystemComponent* AbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		const float Mana = AbilitySystem
			? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		const bool bCooldownActive = AbilitySystem && AbilitySystem->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Cooldown.FireBolt")));
		float EnemyDot = 0.f;
		if (WeakCharacter.IsValid() && WeakEnemy.IsValid())
		{
			const FVector EnemyDirection = (WeakEnemy->GetActorLocation() - WeakCharacter->GetActorLocation()).GetSafeNormal2D();
			EnemyDot = FVector::DotProduct(ProjectileDirection, EnemyDirection);
		}
		const float MaxMana = AbilitySystem
			? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()) : -1.f;
		const float ExpectedMana = MaxMana - UAuraCombatBalanceData::Get().FireBolt.ManaCost;
		// Clicking empty ground must preserve the character's forward shot.
		const bool bPassed = Projectile && !ProjectileDirection.IsNearlyZero()
			&& FacingDot > 0.98f
			&& ActorYawDelta < 0.1f
			&& bClickRoutingConfigured
			&& FMath::IsNearlyEqual(Mana, ExpectedMana, 0.1f) && bCooldownActive;
		UE_LOG(LogAuraInput, Display,
			TEXT("Facing FireBolt click-routing smoke test completed. Passed=%s Mode=%s ClickRouting=%s FacingYaw=%.1f ProjectileYaw=%.1f FacingDot=%.3f EnemyDot=%.3f ActorYawDelta=%.3f Mana=%.1f Cooldown=%s"),
			bPassed ? TEXT("true") : TEXT("false"), TEXT("GodView"),
			bClickRoutingConfigured ? TEXT("valid") : TEXT("invalid"), FacingYaw,
			Projectile ? Projectile->GetActorRotation().Yaw : 0.f, FacingDot, EnemyDot, ActorYawDelta,
			Mana, bCooldownActive ? TEXT("true") : TEXT("false"));
		if (bIsolatedSmoke)
		{
			if (AAuraCharacter* TestPawn = WeakCharacter.Get())
			{
				TestPawn->TeleportTo(OriginalTransform.GetLocation(),
					OriginalTransform.Rotator(), false, true);
				TestPawn->GetCharacterMovement()->GravityScale = OriginalGravity;
				TestPawn->GetCharacterMovement()->SetMovementMode(OriginalMovementMode);
			}
			UE_LOG(LogAuraInput, Display,
				TEXT("AURA_FIREBOLT_CLICK Passed=%s FacingDot=%.3f Target=%s"),
				bPassed ? TEXT("true") : TEXT("false"), FacingDot,
				Projectile ? *GetNameSafe(Projectile) : TEXT("None"));
		}
		else ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.60f, false);

	FTimerHandle ZeroManaTimer;
	GetWorldTimerManager().SetTimer(ZeroManaTimer, [WeakCharacter]()
	{
		UAbilitySystemComponent* AbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		if (!AbilitySystem) return;

		const float ManaCost = UAuraCombatBalanceData::Get().FireBolt.ManaCost;
		const float TestMana = 0.f;
		AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), TestMana);
		const bool bCooldownActive = AbilitySystem->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Cooldown.FireBolt")));
		const bool bActivated = AbilitySystem->TryActivateAbilityByClass(
			UAuraFireBoltAbility::StaticClass());
		const float ManaAfter = AbilitySystem->GetNumericAttribute(
			UAuraAttributeSet::GetManaAttribute());
		const bool bPassed = FMath::IsNearlyZero(ManaCost) && !bCooldownActive && bActivated
			&& FMath::IsNearlyEqual(ManaAfter, TestMana, 0.05f);
		UE_LOG(LogAuraInput, Display,
			TEXT("FireBolt zero-mana fallback smoke test completed. Passed=%s Mana=%.1f Cost=%.1f CooldownBefore=%s Activated=%s"),
			bPassed ? TEXT("true") : TEXT("false"), ManaAfter, ManaCost,
			bCooldownActive ? TEXT("true") : TEXT("false"),
			bActivated ? TEXT("true") : TEXT("false"));
		AbilitySystem->SetNumericAttributeBase(
			UAuraAttributeSet::GetManaAttribute(),
			AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()));
	}, 1.10f, false);
}

void AAuraPlayerController::AuraTestArcaneShards(const bool)
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !Ability1Action)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Arcane Shards smoke test requires AuraCharacter and IA_Ability1."));
		return;
	}

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	AAuraEnemy* FirstEnemy = Enemies.IsEmpty() ? nullptr : Cast<AAuraEnemy>(Enemies[0]);
	if (!FirstEnemy)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Arcane Shards smoke test requires a placed AuraEnemy."));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	const UGameViewportClient* InitialGameViewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	const bool bNormalClickRoutingConfigured = InitialGameViewport
		&& InitialGameViewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown;
	AuraCharacter->SetActorRotation(FRotator::ZeroRotator);
	const float InitialYaw = AuraCharacter->GetActorRotation().Yaw;
	const float CastRange = GetDefault<UAuraArcaneShardsAbility>()->GetCastRange();
	const FVector RawTargetOffset(1200.f, 360.f, 0.f);
	const FVector TargetDirection = RawTargetOffset.GetSafeNormal2D();
	const FVector TargetSide(-TargetDirection.Y, TargetDirection.X, 0.f);
	const FVector EffectCenter = AuraCharacter->GetActorLocation() + TargetDirection * CastRange;
	const FVector RawTarget = AuraCharacter->GetActorLocation() + RawTargetOffset;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* SecondEnemy = GetWorld()->SpawnActor<AAuraEnemy>(
		FirstEnemy->GetClass(), EffectCenter + TargetSide * 120.f, FRotator(0.f, 180.f, 0.f), SpawnParameters);
	AAuraEnemy* OutsideEnemy = GetWorld()->SpawnActor<AAuraEnemy>(
		FirstEnemy->GetClass(), EffectCenter + TargetSide * 650.f, FRotator(0.f, 180.f, 0.f), SpawnParameters);
	if (!SecondEnemy || !OutsideEnemy)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Arcane Shards smoke test could not spawn comparison enemies."));
		return;
	}

	FirstEnemy->SetActorLocation(EffectCenter - TargetSide * 120.f,
		false, nullptr, ETeleportType::TeleportPhysics);
	for (AAuraEnemy* TestEnemy : { FirstEnemy, SecondEnemy, OutsideEnemy })
	{
		TestEnemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AIController = Cast<AAuraAIController>(TestEnemy->GetController()))
		{
			AIController->StopMovement();
			AIController->SetActorTickEnabled(false);
		}
		if (UAbilitySystemComponent* EnemyAbilitySystem = TestEnemy->GetAbilitySystemComponent())
		{
			EnemyAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 100.f);
		}
	}

	UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter->GetAbilitySystemComponent();
	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 50.f);
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakFirst(FirstEnemy);
	const TWeakObjectPtr<AAuraEnemy> WeakSecond(SecondEnemy);
	const TWeakObjectPtr<AAuraEnemy> WeakOutside(OutsideEnemy);

	bUseDebugGroundTargetOverride = true;
	DebugGroundTargetOverride = RawTarget;
	const TSharedRef<bool> TargetingPersistedAfterQRelease = MakeShared<bool>(false);
	const TSharedRef<bool> TargetingClickRoutingConfigured = MakeShared<bool>(false);
	UE_LOG(LogAuraInput, Display,
		TEXT("Arcane Shards click-confirm smoke test injecting Q. Mode=%s RawTarget=%s ExpectedClampedCenter=%s"),
		TEXT("GodView"),
		*RawTarget.ToCompactString(), *EffectCenter.ToCompactString());
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Q, IE_Pressed, 1.f));

	FTimerHandle QReleaseTimer;
	GetWorldTimerManager().SetTimer(QReleaseTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Q, IE_Released, 0.f));
	}, 0.08f, false);

	FTimerHandle AnimationCaptureTimer;
	GetWorldTimerManager().SetTimer(AnimationCaptureTimer,
		[this, TargetingPersistedAfterQRelease, TargetingClickRoutingConfigured]()
	{
		*TargetingPersistedAfterQRelease = bIsArcaneShardsTargeting;
		const UGameViewportClient* GameViewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		*TargetingClickRoutingConfigured = GameViewport
			&& GameViewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown;
		UE_LOG(LogAuraInput, Display,
			TEXT("Arcane Shards targeting after Q release. Active=%s Valid=%s Clamped=%s Location=%s Cursor=%s ClickRouting=%s"),
			bIsArcaneShardsTargeting ? TEXT("true") : TEXT("false"),
			bArcaneShardsPreviewValid ? TEXT("true") : TEXT("false"),
			bArcaneShardsTargetClamped ? TEXT("true") : TEXT("false"),
			*ArcaneShardsPreviewLocation.ToCompactString(), bShowMouseCursor ? TEXT("true") : TEXT("false"),
			*TargetingClickRoutingConfigured ? TEXT("CaptureDuringMouseDown") : TEXT("invalid"));
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.18f, false);

	FTimerHandle ConfirmClickTimer;
	GetWorldTimerManager().SetTimer(ConfirmClickTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
	}, 0.32f, false);

	FTimerHandle ConfirmClickReleaseTimer;
	GetWorldTimerManager().SetTimer(ConfirmClickReleaseTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.f));
	}, 0.36f, false);

	FTimerHandle DebugTargetResetTimer;
	GetWorldTimerManager().SetTimer(DebugTargetResetTimer, [this]()
	{
		bUseDebugGroundTargetOverride = false;
	}, 0.45f, false);

	FTimerHandle CaptureTimer;
	GetWorldTimerManager().SetTimer(CaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.82f, false);

	FTimerHandle CooldownAttemptTimer;
	GetWorldTimerManager().SetTimer(CooldownAttemptTimer, [this]()
	{
		ToggleArcaneShardsTargeting();
	}, 0.9f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakCharacter, WeakFirst, WeakSecond, WeakOutside, InitialYaw, EffectCenter,
			bNormalClickRoutingConfigured, TargetingPersistedAfterQRelease, TargetingClickRoutingConfigured]()
	{
		const UAbilitySystemComponent* CurrentPlayerAbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		const auto GetHealth = [](const TWeakObjectPtr<AAuraEnemy>& Enemy)
		{
			const UAbilitySystemComponent* AbilitySystem = Enemy.IsValid()
				? Enemy->GetAbilitySystemComponent() : nullptr;
			return AbilitySystem
				? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		};
		const float FirstHealth = GetHealth(WeakFirst);
		const float SecondHealth = GetHealth(WeakSecond);
		const float OutsideHealth = GetHealth(WeakOutside);
		const float Mana = CurrentPlayerAbilitySystem
			? CurrentPlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		const bool bCooldownActive = CurrentPlayerAbilitySystem
			&& CurrentPlayerAbilitySystem->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("Cooldown.ArcaneShards")));
		const float YawDelta = WeakCharacter.IsValid()
			? FMath::Abs(FMath::FindDeltaAngleDegrees(InitialYaw, WeakCharacter->GetActorRotation().Yaw)) : 180.f;
		const float TargetError = FVector::Dist2D(ArcaneShardsPreviewLocation, EffectCenter);
		const bool bCursorRestored = bShowMouseCursor;
		const FAuraArcaneShardsBalance& ShardsBalance = UAuraCombatBalanceData::Get().ArcaneShards;
		const float MaxMana = CurrentPlayerAbilitySystem
			? CurrentPlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()) : -1.f;
		const float DamageMultiplier = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilityDamageMultiplier() : 1.f;
		const float ExpectedMana = MaxMana - ShardsBalance.ManaCost;
		const float ExpectedHealth = 100.f - ShardsBalance.Damage * DamageMultiplier;
		const bool bPassed = FMath::IsNearlyEqual(Mana, ExpectedMana, 0.1f)
			&& FMath::IsNearlyEqual(FirstHealth, ExpectedHealth, 0.1f)
			&& FMath::IsNearlyEqual(SecondHealth, ExpectedHealth, 0.1f)
			&& FMath::IsNearlyEqual(OutsideHealth, 100.f, 0.1f)
			&& bCooldownActive && YawDelta < 0.1f && bArcaneShardsTargetClamped
			&& TargetError < 2.f && !bIsArcaneShardsTargeting && bCursorRestored
			&& *TargetingPersistedAfterQRelease && bNormalClickRoutingConfigured
			&& *TargetingClickRoutingConfigured;
		UE_LOG(LogAuraInput, Display,
			TEXT("Arcane Shards click-confirm smoke test completed. Passed=%s Mode=%s StayedTargetingAfterQRelease=%s NormalClickRouting=%s TargetingClickRouting=%s Mana=%.1f InnerHealth=(%.1f,%.1f) OutsideHealth=%.1f Cooldown=%s ActorYawDelta=%.3f TargetClamped=%s TargetError=%.2f CursorRestored=%s"),
			bPassed ? TEXT("true") : TEXT("false"), TEXT("GodView"),
			*TargetingPersistedAfterQRelease ? TEXT("true") : TEXT("false"),
			bNormalClickRoutingConfigured ? TEXT("valid") : TEXT("invalid"),
			*TargetingClickRoutingConfigured ? TEXT("valid") : TEXT("invalid"),
			Mana, FirstHealth, SecondHealth, OutsideHealth,
			bCooldownActive ? TEXT("true") : TEXT("false"), YawDelta,
			bArcaneShardsTargetClamped ? TEXT("true") : TEXT("false"), TargetError,
			bCursorRestored ? TEXT("true") : TEXT("false"));
	}, 1.08f, false);
}

void AAuraPlayerController::AuraTestShock(const bool)
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !PlayerAbilitySystem || !Ability2Action || !GetWorld())
	{
		UE_LOG(LogAuraInput, Error, TEXT("Shock smoke test requires AuraCharacter, ASC, and IA_Ability2."));
		return;
	}

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	AAuraEnemy* FirstEnemy = Enemies.IsEmpty() ? nullptr : Cast<AAuraEnemy>(Enemies[0]);
	if (!FirstEnemy)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Shock smoke test requires a placed AuraEnemy."));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	if (bIsShockTargeting) CancelShockTargeting();
	const UGameViewportClient* InitialGameViewport = GetWorld()->GetGameViewport();
	const bool bNormalClickRoutingConfigured = InitialGameViewport
		&& InitialGameViewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown;
	const float SelectedYaw = 32.f;
	const float InitialYaw = SelectedYaw + 95.f;
	AuraCharacter->SetActorLocation(
		AuraCharacter->GetActorLocation() + FVector(1800.f, 1300.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	AuraCharacter->SetActorRotation(FRotator(0.f, InitialYaw, 0.f));
	const FVector SelectedDirection = FRotator(0.f, SelectedYaw, 0.f).Vector().GetSafeNormal2D();
	const FVector SelectedSide(-SelectedDirection.Y, SelectedDirection.X, 0.f);
	const FVector PlayerLocation = AuraCharacter->GetActorLocation();
	// Deliberately place the target off the center line but inside the displayed
	// sweep width. Damage should assist to it while the beam remains on the exact
	// confirmed mouse direction.
	FirstEnemy->SetActorLocation(PlayerLocation + SelectedDirection * 520.f + SelectedSide * 80.f,
		false, nullptr, ETeleportType::TeleportPhysics);
	FirstEnemy->SetActorRotation((-SelectedDirection).Rotation());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* FarEnemy = GetWorld()->SpawnActor<AAuraEnemy>(FirstEnemy->GetClass(),
		PlayerLocation + SelectedDirection * 800.f, (-SelectedDirection).Rotation(), SpawnParameters);
	if (!FarEnemy)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Shock smoke test could not spawn the farther comparison enemy."));
		return;
	}
	if (AAuraAIController* FarAIController = Cast<AAuraAIController>(FarEnemy->GetController()))
	{
		FarAIController->StopMovement();
		FarAIController->SetActorTickEnabled(false);
	}

	for (AAuraEnemy* TestEnemy : { FirstEnemy, FarEnemy })
	{
		if (UAbilitySystemComponent* EnemyAbilitySystem = TestEnemy->GetAbilitySystemComponent())
		{
			EnemyAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 100.f);
		}
	}
	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 50.f);

	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakFirst(FirstEnemy);
	const TWeakObjectPtr<AAuraEnemy> WeakFar(FarEnemy);
	const TSharedRef<bool> TargetingPersistedAfterERelease = MakeShared<bool>(false);
	const TSharedRef<bool> TargetingClickRoutingConfigured = MakeShared<bool>(false);
	const TSharedRef<bool> PreviewDirectionCorrect = MakeShared<bool>(false);
	const TSharedRef<bool> NiagaraDirectionObserved = MakeShared<bool>(false);
	const TSharedRef<bool> NiagaraDirectionCorrect = MakeShared<bool>(false);
	const TSharedRef<bool> StunObserved = MakeShared<bool>(false);
	const TSharedRef<bool> AIBlockedObserved = MakeShared<bool>(false);
	bUseDebugGroundTargetOverride = true;
	DebugGroundTargetOverride = PlayerLocation + SelectedDirection * 700.f;
	UE_LOG(LogAuraInput, Display,
		TEXT("Shock directional-targeting smoke test injecting E. Mode=%s InitialYaw=%.1f SelectedYaw=%.1f First=%s Far=%s"),
		TEXT("GodView"), InitialYaw, SelectedYaw,
		*FirstEnemy->GetActorLocation().ToCompactString(), *FarEnemy->GetActorLocation().ToCompactString());
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Pressed, 1.f));

	FTimerHandle ReleaseTimer;
	GetWorldTimerManager().SetTimer(ReleaseTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Released, 0.f));
	}, 0.05f, false);

	FTimerHandle TargetingObservationTimer;
	GetWorldTimerManager().SetTimer(TargetingObservationTimer,
		[this, WeakCharacter, SelectedDirection, TargetingPersistedAfterERelease,
			TargetingClickRoutingConfigured, PreviewDirectionCorrect]()
	{
		FVector PreviewEnd;
		bool bClamped = false;
		const bool bPreviewValid = GetShockTargetPreview(PreviewEnd, bClamped);
		FVector PreviewDirection = WeakCharacter.IsValid()
			? PreviewEnd - WeakCharacter->GetActorLocation() : FVector::ZeroVector;
		PreviewDirection.Z = 0.f;
		PreviewDirection = PreviewDirection.GetSafeNormal();
		*TargetingPersistedAfterERelease = bIsShockTargeting;
		*PreviewDirectionCorrect = bPreviewValid
			&& FVector::DotProduct(PreviewDirection, SelectedDirection) > 0.999f;
		const UGameViewportClient* GameViewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		*TargetingClickRoutingConfigured = GameViewport
			&& GameViewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown;
		UE_LOG(LogAuraInput, Display,
			TEXT("Shock targeting after E release. Active=%s Valid=%s DirectionDot=%.4f Cursor=%s ClickRouting=%s"),
			bIsShockTargeting ? TEXT("true") : TEXT("false"), bPreviewValid ? TEXT("true") : TEXT("false"),
			FVector::DotProduct(PreviewDirection, SelectedDirection), bShowMouseCursor ? TEXT("true") : TEXT("false"),
			*TargetingClickRoutingConfigured ? TEXT("CaptureDuringMouseDown") : TEXT("invalid"));
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.18f, false);

	FTimerHandle ConfirmClickTimer;
	GetWorldTimerManager().SetTimer(ConfirmClickTimer, [this, PlayerLocation, SelectedDirection]()
	{
		// Simulate the cursor position changing as the viewport captures LMB. The
		// committed cast must still use the direction that was visibly previewed on
		// the preceding frame, not re-sample this changed raw input during the click.
		const FVector CaptureShiftDirection = SelectedDirection.RotateAngleAxis(75.f, FVector::UpVector);
		DebugGroundTargetOverride = PlayerLocation + CaptureShiftDirection * 700.f;
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
	}, 0.32f, false);

	FTimerHandle ConfirmClickReleaseTimer;
	GetWorldTimerManager().SetTimer(ConfirmClickReleaseTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.f));
	}, 0.36f, false);

	FTimerHandle DebugTargetResetTimer;
	GetWorldTimerManager().SetTimer(DebugTargetResetTimer, [this]()
	{
		bUseDebugGroundTargetOverride = false;
	}, 0.44f, false);

	// Inspect the live component while the beam is active. This validates the
	// asset's mixed coordinate contract instead of only comparing the C++ points
	// that were intended to be sent to Niagara.
	FTimerHandle NiagaraDirectionObservationTimer;
	GetWorldTimerManager().SetTimer(NiagaraDirectionObservationTimer,
		[this, SelectedDirection, NiagaraDirectionObserved, NiagaraDirectionCorrect]()
	{
		if (ObserveLiveShockNiagara(GetWorld(), SelectedDirection,
			*NiagaraDirectionObserved, *NiagaraDirectionCorrect))
		{
			return;
		}

		// Screenshot capture can advance several timers in one frame. Retry after
		// the ability's impact callback has finished spawning and initializing the
		// Niagara component instead of reporting a timing-dependent false negative.
		FTimerHandle RetryTimer;
		GetWorldTimerManager().SetTimer(RetryTimer,
			[this, SelectedDirection, NiagaraDirectionObserved, NiagaraDirectionCorrect]()
		{
			if (!ObserveLiveShockNiagara(GetWorld(), SelectedDirection,
				*NiagaraDirectionObserved, *NiagaraDirectionCorrect))
			{
				UE_LOG(LogAuraInput, Warning,
					TEXT("Shock live Niagara observation could not find an initialized NS_ElectricBeam component after retry."));
			}
		}, 0.03f, false);
	}, 0.75f, false);

	FTimerHandle ShockEffectCaptureTimer;
	GetWorldTimerManager().SetTimer(ShockEffectCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.92f, false);

	FTimerHandle StunObservationTimer;
	GetWorldTimerManager().SetTimer(StunObservationTimer,
		[this, WeakFirst, StunObserved, AIBlockedObserved]()
	{
		const AAuraEnemy* Enemy = WeakFirst.Get();
		const UAbilitySystemComponent* EnemyAbilitySystem = Enemy
			? Enemy->GetAbilitySystemComponent() : nullptr;
		const AAuraAIController* AIController = Enemy
			? Cast<AAuraAIController>(Enemy->GetController()) : nullptr;
		const bool bHasStunTag = EnemyAbilitySystem && EnemyAbilitySystem->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")));
		*StunObserved = Enemy && Enemy->IsStunned() && bHasStunTag;
		*AIBlockedObserved = Enemy && AIController
			&& AIController->GetAIState() == EAuraAIState::Stunned
			&& Enemy->GetCharacterMovement()->MovementMode == MOVE_None;
		UE_LOG(LogAuraInput, Display,
			TEXT("Shock stun observation. Stunned=%s Tag=%s AIState=%d MovementMode=%d"),
			Enemy && Enemy->IsStunned() ? TEXT("true") : TEXT("false"),
			bHasStunTag ? TEXT("true") : TEXT("false"),
			AIController ? static_cast<int32>(AIController->GetAIState()) : -1,
			Enemy ? static_cast<int32>(Enemy->GetCharacterMovement()->MovementMode) : -1);
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 1.20f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakCharacter, WeakFirst, WeakFar, StunObserved, AIBlockedObserved,
			TargetingPersistedAfterERelease, TargetingClickRoutingConfigured, PreviewDirectionCorrect,
			NiagaraDirectionObserved, NiagaraDirectionCorrect,
			InitialYaw, SelectedYaw, bNormalClickRoutingConfigured]()
	{
		const UAbilitySystemComponent* CurrentPlayerAbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		const UAbilitySystemComponent* FirstAbilitySystem = WeakFirst.IsValid()
			? WeakFirst->GetAbilitySystemComponent() : nullptr;
		const UAbilitySystemComponent* FarAbilitySystem = WeakFar.IsValid()
			? WeakFar->GetAbilitySystemComponent() : nullptr;
		const float Mana = CurrentPlayerAbilitySystem
			? CurrentPlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		const float FirstHealth = FirstAbilitySystem
			? FirstAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		const float FarHealth = FarAbilitySystem
			? FarAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		const bool bCooldownActive = CurrentPlayerAbilitySystem
			&& CurrentPlayerAbilitySystem->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Shock")));
		const bool bStunEnded = WeakFirst.IsValid() && !WeakFirst->IsStunned()
			&& FirstAbilitySystem && !FirstAbilitySystem->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")))
			&& WeakFirst->GetCharacterMovement()->MovementMode == MOVE_Walking;
		const AAuraAIController* FirstAI = WeakFirst.IsValid()
			? Cast<AAuraAIController>(WeakFirst->GetController()) : nullptr;
		const bool bAIResumed = FirstAI && FirstAI->GetAIState() != EAuraAIState::Stunned;
		const float FinalYaw = WeakCharacter.IsValid() ? WeakCharacter->GetActorRotation().Yaw : 0.f;
		const float SelectedYawDelta = WeakCharacter.IsValid()
			? FMath::Abs(FMath::FindDeltaAngleDegrees(SelectedYaw, FinalYaw)) : 180.f;
		const float TurnAmount = WeakCharacter.IsValid()
			? FMath::Abs(FMath::FindDeltaAngleDegrees(InitialYaw, FinalYaw)) : 0.f;
		const bool bCursorRestored = bShowMouseCursor;
		const UAuraCombatBalanceData& Balance = UAuraCombatBalanceData::Get();
		const float MaxMana = CurrentPlayerAbilitySystem
			? CurrentPlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()) : -1.f;
		const float DamageMultiplier = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilityDamageMultiplier() : 1.f;
		const float ExpectedMana = MaxMana - Balance.Shock.ManaCost;
		const float ExpectedHealth = 100.f - Balance.Shock.Damage * DamageMultiplier;
		const bool bPassed = FMath::IsNearlyEqual(Mana, ExpectedMana, 0.1f)
			&& FMath::IsNearlyEqual(FirstHealth, ExpectedHealth, 0.1f)
			&& FMath::IsNearlyEqual(FarHealth, 100.f, 0.1f)
			&& bCooldownActive && *StunObserved && *AIBlockedObserved
			&& bStunEnded && bAIResumed && SelectedYawDelta < 0.1f && TurnAmount > 90.f
			&& *TargetingPersistedAfterERelease && *TargetingClickRoutingConfigured
			&& *PreviewDirectionCorrect && *NiagaraDirectionObserved && *NiagaraDirectionCorrect
			&& bNormalClickRoutingConfigured
			&& !bIsShockTargeting && bCursorRestored;
		UE_LOG(LogAuraInput, Display,
			TEXT("Shock directional-targeting smoke test completed. Passed=%s Mode=%s StayedTargetingAfterERelease=%s NormalClickRouting=%s TargetingClickRouting=%s PreviewDirection=%s NiagaraVisual=%s Mana=%.1f FirstHealth=%.1f FarHealth=%.1f Cooldown=%s StunObserved=%s AIBlocked=%s StunEnded=%s AIResumed=%s SelectedYawDelta=%.3f TurnAmount=%.3f CursorRestored=%s"),
			bPassed ? TEXT("true") : TEXT("false"), TEXT("GodView"),
			*TargetingPersistedAfterERelease ? TEXT("true") : TEXT("false"),
			bNormalClickRoutingConfigured ? TEXT("valid") : TEXT("invalid"),
			*TargetingClickRoutingConfigured ? TEXT("valid") : TEXT("invalid"),
			*PreviewDirectionCorrect ? TEXT("valid") : TEXT("invalid"),
			*NiagaraDirectionCorrect ? TEXT("valid") : TEXT("invalid"),
			Mana, FirstHealth, FarHealth, bCooldownActive ? TEXT("true") : TEXT("false"),
			*StunObserved ? TEXT("true") : TEXT("false"),
			*AIBlockedObserved ? TEXT("true") : TEXT("false"),
			bStunEnded ? TEXT("true") : TEXT("false"), bAIResumed ? TEXT("true") : TEXT("false"),
			SelectedYawDelta, TurnAmount, bCursorRestored ? TEXT("true") : TEXT("false"));
	}, 3.20f, false);
}

void AAuraPlayerController::AuraTestPickups()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter ? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !AbilitySystem)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Pickup smoke test requires a possessed AuraCharacter with an ASC."));
		return;
	}

	TArray<AActor*> PickupActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraPickup::StaticClass(), PickupActors);
	AAuraPickup* HealthPickup = nullptr;
	AAuraPickup* ManaPickup = nullptr;
	for (AActor* PickupActor : PickupActors)
	{
		AAuraPickup* Pickup = Cast<AAuraPickup>(PickupActor);
		if (!Pickup) continue;
		if (Pickup->GetPickupType() == EAuraPickupType::Health) HealthPickup = Pickup;
		else ManaPickup = Pickup;
	}
	if (!HealthPickup || !ManaPickup || !HealthPickup->IsAvailable() || !ManaPickup->IsAvailable())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Pickup smoke test requires one available health pickup and one available mana pickup."));
		return;
	}

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	for (AActor* EnemyActor : Enemies)
	{
		if (AAuraEnemy* Enemy = Cast<AAuraEnemy>(EnemyActor))
		{
			Enemy->GetCharacterMovement()->DisableMovement();
			if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
			{
				AIController->StopMovement();
				AIController->SetActorTickEnabled(false);
			}
		}
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();

	struct FPickupSmokeState
	{
		bool bHealthConsumed = false;
		bool bManaConsumed = false;
		bool bStayedConsumedUntilReset = false;
		FVector OriginalPlayerLocation = FVector::ZeroVector;
	};
	const TSharedRef<FPickupSmokeState> TestState = MakeShared<FPickupSmokeState>();
	TestState->OriginalPlayerLocation = AuraCharacter->GetActorLocation();
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraPickup> WeakHealthPickup(HealthPickup);
	const TWeakObjectPtr<AAuraPickup> WeakManaPickup(ManaPickup);

	UE_LOG(LogAuraInput, Display,
		TEXT("Pickup smoke test started. HealthLocation=%s ManaLocation=%s Respawn=%.1fs"),
		*HealthPickup->GetActorLocation().ToCompactString(), *ManaPickup->GetActorLocation().ToCompactString(),
		HealthPickup->GetRespawnDelay());

	FTimerHandle InitialCaptureTimer;
	GetWorldTimerManager().SetTimer(InitialCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.12f, false);

	FTimerHandle AttributePreparationTimer;
	GetWorldTimerManager().SetTimer(AttributePreparationTimer, [WeakCharacter]()
	{
		if (!WeakCharacter.IsValid()) return;
		if (UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter->GetAbilitySystemComponent())
		{
			CurrentAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 40.f);
			CurrentAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 10.f);
		}
	}, 0.25f, false);

	FTimerHandle HealthPickupTimer;
	GetWorldTimerManager().SetTimer(HealthPickupTimer, [WeakCharacter, WeakHealthPickup, TestState]()
	{
		if (!WeakCharacter.IsValid() || !WeakHealthPickup.IsValid()) return;
		WeakCharacter->SetActorLocation(WeakHealthPickup->GetActorLocation(),
			false, nullptr, ETeleportType::TeleportPhysics);
		WeakCharacter->GetCapsuleComponent()->UpdateOverlaps();
	}, 0.32f, false);

	FTimerHandle ManaPickupTimer;
	GetWorldTimerManager().SetTimer(ManaPickupTimer,
		[WeakCharacter, WeakHealthPickup, WeakManaPickup, TestState]()
	{
		if (!WeakCharacter.IsValid() || !WeakHealthPickup.IsValid() || !WeakManaPickup.IsValid()) return;
		const UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter->GetAbilitySystemComponent();
		const float Health = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		TestState->bHealthConsumed = FMath::IsNearlyEqual(Health, 75.f, 0.1f)
			&& !WeakHealthPickup->IsAvailable();

		WeakCharacter->SetActorLocation(WeakManaPickup->GetActorLocation(),
			false, nullptr, ETeleportType::TeleportPhysics);
		WeakCharacter->GetCapsuleComponent()->UpdateOverlaps();
	}, 0.62f, false);

	FTimerHandle MoveAwayTimer;
	GetWorldTimerManager().SetTimer(MoveAwayTimer,
		[WeakCharacter, WeakManaPickup, TestState]()
	{
		if (!WeakCharacter.IsValid() || !WeakManaPickup.IsValid()) return;
		const UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter->GetAbilitySystemComponent();
		const float Mana = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		TestState->bManaConsumed = FMath::IsNearlyEqual(Mana, 30.f, 0.1f)
			&& !WeakManaPickup->IsAvailable();
		WeakCharacter->SetActorLocation(TestState->OriginalPlayerLocation,
			false, nullptr, ETeleportType::TeleportPhysics);
		WeakCharacter->GetCapsuleComponent()->UpdateOverlaps();
	}, 0.92f, false);

	FTimerHandle CheckpointResetTimer;
	GetWorldTimerManager().SetTimer(CheckpointResetTimer,
		[WeakHealthPickup, WeakManaPickup, TestState]()
	{
		if (!WeakHealthPickup.IsValid() || !WeakManaPickup.IsValid()) return;
		TestState->bStayedConsumedUntilReset = !WeakHealthPickup->IsAvailable()
			&& !WeakManaPickup->IsAvailable()
			&& !WeakHealthPickup->RespawnsAutomatically()
			&& !WeakManaPickup->RespawnsAutomatically();
		WeakHealthPickup->ResetForCheckpoint();
		WeakManaPickup->ResetForCheckpoint();
	}, 1.25f, false);

	FTimerHandle ResetCaptureTimer;
	GetWorldTimerManager().SetTimer(ResetCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 1.38f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakCharacter, WeakHealthPickup, WeakManaPickup, TestState]()
	{
		const UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		const float Health = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		const float Mana = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		const bool bHealthReset = WeakHealthPickup.IsValid() && WeakHealthPickup->IsAvailable();
		const bool bManaReset = WeakManaPickup.IsValid() && WeakManaPickup->IsAvailable();
		const bool bPassed = TestState->bHealthConsumed && TestState->bManaConsumed
			&& TestState->bStayedConsumedUntilReset && bHealthReset && bManaReset
			&& FMath::IsNearlyEqual(Health, 75.f, 0.1f)
			&& Mana >= 30.f && Mana <= 50.f;
		UE_LOG(LogAuraInput, Display,
			TEXT("Pickup smoke test completed. Passed=%s Health=%.1f Mana=%.1f HealthConsumed=%s ManaConsumed=%s StayedConsumed=%s HealthCheckpointReset=%s ManaCheckpointReset=%s"),
			bPassed ? TEXT("true") : TEXT("false"), Health, Mana,
			TestState->bHealthConsumed ? TEXT("true") : TEXT("false"),
			TestState->bManaConsumed ? TEXT("true") : TEXT("false"),
			TestState->bStayedConsumedUntilReset ? TEXT("true") : TEXT("false"),
			bHealthReset ? TEXT("true") : TEXT("false"),
			bManaReset ? TEXT("true") : TEXT("false"));
	}, 1.52f, false);
}

void AAuraPlayerController::AuraTestFeedbackVFX()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!IsLocalController() || !HasAuthority() || !AuraCharacter
		|| !AuraCharacter->AreProgressionFeedbackEffectsConfigured())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_FEEDBACK_VFX_SMOKE Passed=false Setup=false"));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();
	AuraCharacter->MulticastLevelUpEffect();
	UE_LOG(LogAuraInput, Display, TEXT("AURA_FEEDBACK_VFX_SEQUENCE Started=true"));

	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TSharedRef<bool> bManaComponentFollowed = MakeShared<bool>(false);
	FTimerHandle LevelUpReplayTimer;
	GetWorldTimerManager().SetTimer(LevelUpReplayTimer, [WeakCharacter]()
	{
		// Replay after the first instance has compiled the system in uncooked editor runs.
		if (WeakCharacter.IsValid()) WeakCharacter->MulticastLevelUpEffect();
	}, 2.8f, false);
	FTimerHandle LevelUpCaptureTimer;
	GetWorldTimerManager().SetTimer(LevelUpCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 3.05f, false);

	FTimerHandle HealthEffectTimer;
	GetWorldTimerManager().SetTimer(HealthEffectTimer, [WeakCharacter]()
	{
		if (WeakCharacter.IsValid()) WeakCharacter->MulticastHealthRestoreEffect();
	}, 4.20f, false);
	FTimerHandle HealthCaptureTimer;
	GetWorldTimerManager().SetTimer(HealthCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 4.45f, false);

	FTimerHandle ManaEffectTimer;
	GetWorldTimerManager().SetTimer(ManaEffectTimer, [WeakCharacter]()
	{
		if (WeakCharacter.IsValid()) WeakCharacter->MulticastManaRestoreEffect();
	}, 7.50f, false);
	FTimerHandle ManaMoveTimer;
	GetWorldTimerManager().SetTimer(ManaMoveTimer, [WeakCharacter]()
	{
		if (!WeakCharacter.IsValid()) return;
		WeakCharacter->SetActorLocation(
			WeakCharacter->GetActorLocation() + FVector(350.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
	}, 7.65f, false);
	FTimerHandle ManaFollowCheckTimer;
	GetWorldTimerManager().SetTimer(ManaFollowCheckTimer,
		[WeakCharacter, bManaComponentFollowed]()
	{
		if (!WeakCharacter.IsValid()) return;
		const FVector ExpectedLocation = WeakCharacter->GetActorLocation()
			- FVector(0.f, 0.f,
				WeakCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			const UNiagaraComponent* Component = *It;
			if (!Component || Component->GetWorld() != WeakCharacter->GetWorld()
				|| GetNameSafe(Component->GetAsset()) != TEXT("NS_Free_Spells_Buff_Mana"))
			{
				continue;
			}
			const float LocationError = FVector::Distance(
				Component->GetComponentLocation(), ExpectedLocation);
			*bManaComponentFollowed = Component->GetAttachParent()
				== WeakCharacter->GetRootComponent() && LocationError <= 1.f;
			UE_LOG(LogAuraInput, Display,
				TEXT("AURA_MANA_VFX_FOLLOW Passed=%s Attached=%s LocationError=%.3f"),
				*bManaComponentFollowed ? TEXT("true") : TEXT("false"),
				Component->GetAttachParent() == WeakCharacter->GetRootComponent()
					? TEXT("true") : TEXT("false"), LocationError);
			break;
		}
	}, 7.72f, false);
	FTimerHandle ManaCaptureTimer;
	GetWorldTimerManager().SetTimer(ManaCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 7.95f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [WeakCharacter, bManaComponentFollowed]()
	{
		const bool bPassed = WeakCharacter.IsValid()
			&& WeakCharacter->AreProgressionFeedbackEffectsConfigured()
			&& *bManaComponentFollowed;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_FEEDBACK_VFX_SMOKE Passed=%s Assets=%s ManaFollow=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			WeakCharacter.IsValid() && WeakCharacter->AreProgressionFeedbackEffectsConfigured()
				? TEXT("true") : TEXT("false"),
			*bManaComponentFollowed ? TEXT("true") : TEXT("false"));
	}, 8.75f, false);
}

void AAuraPlayerController::AuraTestManaBottleOnly()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	AAuraEnemy* RewardEnemy = Enemies.IsEmpty() ? nullptr : Cast<AAuraEnemy>(Enemies[0]);
	if (!AuraCharacter || !AbilitySystem || !RewardEnemy || !HasAuthority())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Mana economy smoke test requires an authoritative Aura character and one enemy."));
		return;
	}

	RewardEnemy->GetCharacterMovement()->DisableMovement();
	if (AAuraAIController* AIController = Cast<AAuraAIController>(RewardEnemy->GetController()))
	{
		AIController->StopMovement();
		AIController->SetActorTickEnabled(false);
	}

	struct FManaEconomySmokeState
	{
		float ManaAfterCombatWait = -1.f;
		float ManaAfterOutOfCombatWait = -1.f;
		float ManaAfterNormalKill = -1.f;
		float ManaAfterBottle = -1.f;
		bool bEnteredCombat = false;
		bool bExitedCombat = false;
	};
	const TSharedRef<FManaEconomySmokeState> TestState = MakeShared<FManaEconomySmokeState>();
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakRewardEnemy(RewardEnemy);

	AuraCharacter->NotifyCombatActivity();
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 10.f);
	TestState->bEnteredCombat = AuraCharacter->IsInCombat()
		&& AbilitySystem->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("State.InCombat")));

	FTimerHandle CombatObservationTimer;
	GetWorldTimerManager().SetTimer(CombatObservationTimer, [WeakCharacter, TestState]()
	{
		const UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		TestState->ManaAfterCombatWait = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
	}, 2.2f, false);

	FTimerHandle CombatExitObservationTimer;
	GetWorldTimerManager().SetTimer(CombatExitObservationTimer, [WeakCharacter, TestState]()
	{
		const UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		TestState->bExitedCombat = WeakCharacter.IsValid() && !WeakCharacter->IsInCombat()
			&& CurrentAbilitySystem && !CurrentAbilitySystem->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("State.InCombat")));
		TestState->ManaAfterOutOfCombatWait = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
	}, 3.2f, false);

	FTimerHandle RewardObservationTimer;
	GetWorldTimerManager().SetTimer(RewardObservationTimer,
		[WeakCharacter, WeakRewardEnemy, TestState]()
	{
		if (!WeakCharacter.IsValid() || !WeakRewardEnemy.IsValid()) return;
		UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter->GetAbilitySystemComponent();
		UAbilitySystemComponent* EnemyAbilitySystem = WeakRewardEnemy->GetAbilitySystemComponent();
		if (!CurrentAbilitySystem || !EnemyAbilitySystem) return;

		CurrentAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 0.f);
		EnemyAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 1.f);

		FGameplayEffectContextHandle DamageContext = CurrentAbilitySystem->MakeEffectContext();
		DamageContext.AddSourceObject(WeakCharacter.Get());
		FGameplayEffectSpecHandle DamageSpec = CurrentAbilitySystem->MakeOutgoingSpec(
			UAuraDamageGameplayEffect::StaticClass(), 1.f, DamageContext);
		if (DamageSpec.IsValid())
		{
			DamageSpec.Data->SetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -5.f);
			EnemyAbilitySystem->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
		}
		TestState->ManaAfterNormalKill = CurrentAbilitySystem->GetNumericAttribute(
			UAuraAttributeSet::GetManaAttribute());

		FGameplayEffectContextHandle BottleContext = CurrentAbilitySystem->MakeEffectContext();
		BottleContext.AddSourceObject(WeakCharacter.Get());
		FGameplayEffectSpecHandle BottleSpec = CurrentAbilitySystem->MakeOutgoingSpec(
			UAuraManaPickupEffect::StaticClass(), 1.f, BottleContext);
		if (BottleSpec.IsValid())
		{
			BottleSpec.Data->SetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Data.Restore")),
				UAuraCombatBalanceData::Get().Recovery.ManaPickupRestore);
			CurrentAbilitySystem->ApplyGameplayEffectSpecToSelf(*BottleSpec.Data.Get());
		}
		TestState->ManaAfterBottle = CurrentAbilitySystem->GetNumericAttribute(
			UAuraAttributeSet::GetManaAttribute());
	}, 4.2f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [WeakCharacter, TestState]()
	{
		UAbilitySystemComponent* CurrentAbilitySystem = WeakCharacter.IsValid()
			? WeakCharacter->GetAbilitySystemComponent() : nullptr;
		if (CurrentAbilitySystem && WeakCharacter.IsValid())
		{
			CurrentAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 40.f);
			CurrentAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 10.f);
			WeakCharacter->RestoreVitalsAtCheckpoint();
		}
		const float CheckpointHealth = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		const float CheckpointMana = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		const float MaxHealth = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()) : -1.f;
		const float MaxMana = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()) : -1.f;
		const bool bCheckpointRestored = FMath::IsNearlyEqual(CheckpointHealth, MaxHealth, 0.1f)
			&& FMath::IsNearlyEqual(CheckpointMana, MaxMana, 0.1f);
		const UAuraCombatBalanceData& Balance = UAuraCombatBalanceData::Get();
		const bool bNoPassiveRecovery = FMath::IsNearlyEqual(
			TestState->ManaAfterCombatWait, 10.f, 0.1f)
			&& FMath::IsNearlyEqual(TestState->ManaAfterOutOfCombatWait, 10.f, 0.1f);
		const bool bBottleRecovery = FMath::IsNearlyZero(
			TestState->ManaAfterNormalKill, 0.1f)
			&& FMath::IsNearlyEqual(TestState->ManaAfterBottle,
				Balance.Recovery.ManaPickupRestore, 0.1f);
		const bool bNumbersValid = FMath::IsNearlyEqual(
			GetDefault<UAuraFireBoltAbility>()->GetManaCost(), Balance.FireBolt.ManaCost, 0.1f)
			&& FMath::IsNearlyEqual(GetDefault<UAuraArcaneShardsAbility>()->GetManaCost(),
				Balance.ArcaneShards.ManaCost, 0.1f)
			&& FMath::IsNearlyEqual(GetDefault<UAuraArcaneShardsAbility>()->GetCooldownDuration(),
				Balance.ArcaneShards.Cooldown, 0.1f)
			&& FMath::IsNearlyEqual(GetDefault<UAuraShockAbility>()->GetManaCost(),
				Balance.Shock.ManaCost, 0.1f)
			&& FMath::IsNearlyEqual(GetDefault<UAuraShockAbility>()->GetCooldownDuration(),
				Balance.Shock.Cooldown, 0.1f)
			&& FMath::IsNearlyEqual(Balance.Recovery.ManaPickupRestore, 20.f, 0.1f);
		const bool bPassed = TestState->bEnteredCombat && TestState->bExitedCombat
			&& bNoPassiveRecovery && bBottleRecovery
			&& bCheckpointRestored && bNumbersValid;
		UE_LOG(LogAuraInput, Display,
			TEXT("Mana recovery smoke test completed. Passed=%s EnteredCombat=%s ExitedAfter3s=%s CombatWaitMana=%.1f OutOfCombatWaitMana=%.1f KillMana=%.1f BottleMana=%.1f NoPassive=%s BottleRecovery=%s CheckpointRestored=%s CheckpointMana=%.1f MaxMana=%.1f Numbers=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			TestState->bEnteredCombat ? TEXT("true") : TEXT("false"),
			TestState->bExitedCombat ? TEXT("true") : TEXT("false"),
			TestState->ManaAfterCombatWait, TestState->ManaAfterOutOfCombatWait,
			TestState->ManaAfterNormalKill, TestState->ManaAfterBottle,
			bNoPassiveRecovery ? TEXT("true") : TEXT("false"),
			bBottleRecovery ? TEXT("true") : TEXT("false"),
			bCheckpointRestored ? TEXT("true") : TEXT("false"),
			CheckpointMana, MaxMana,
			bNumbersValid ? TEXT("valid") : TEXT("invalid"));
	}, 5.25f, false);
}

void AAuraPlayerController::AuraTestCombatBalance()
{
	const UAuraCombatBalanceData& Balance = UAuraCombatBalanceData::Get();
	FString ValidationError;
	const bool bInvariantsValid = Balance.Validate(ValidationError);
	const bool bAuthoredAssetLoaded = Balance.GetPathName()
		== UAuraCombatBalanceData::GetDefaultAssetPath().ToString();

	const UAuraFireBoltAbility* FireBolt = GetDefault<UAuraFireBoltAbility>();
	const UAuraArcaneShardsAbility* ArcaneShards = GetDefault<UAuraArcaneShardsAbility>();
	const UAuraShockAbility* Shock = GetDefault<UAuraShockAbility>();
	const UAuraMeleeAbility* Melee = GetDefault<UAuraMeleeAbility>();
	const UAuraShamanBoltAbility* Shaman = GetDefault<UAuraShamanBoltAbility>();
	const UAuraBossAbility* Boss = GetDefault<UAuraBossAbility>();
	const bool bAbilityValuesValid = FireBolt && ArcaneShards && Shock && Melee && Shaman && Boss
		&& FMath::IsNearlyEqual(FireBolt->GetDamage(), Balance.FireBolt.Damage)
		&& FMath::IsNearlyEqual(FireBolt->GetManaCost(), Balance.FireBolt.ManaCost)
		&& FMath::IsNearlyEqual(FireBolt->GetCooldownDuration(), Balance.FireBolt.Cooldown)
		&& FMath::IsNearlyEqual(ArcaneShards->GetDamage(), Balance.ArcaneShards.Damage)
		&& FMath::IsNearlyEqual(ArcaneShards->GetManaCost(), Balance.ArcaneShards.ManaCost)
		&& FMath::IsNearlyEqual(Shock->GetDamage(), Balance.Shock.Damage)
		&& FMath::IsNearlyEqual(Shock->GetStunDuration(), Balance.Shock.StunDuration)
		&& FMath::IsNearlyEqual(Melee->GetDamage(), Balance.MeleeEnemy.Damage)
		&& FMath::IsNearlyEqual(Shaman->GetDamage(), Balance.Shaman.Damage)
		&& FMath::IsNearlyEqual(Boss->GetTailSpinDamage(), Balance.Boss.TailSpinDamage);

	const AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(GetPawn());
	const UAbilitySystemComponent* AbilitySystem = PlayerCharacter
		? PlayerCharacter->GetAbilitySystemComponent() : nullptr;
	const bool bPlayerValuesValid = AbilitySystem
		&& FMath::IsNearlyEqual(
			AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()),
			Balance.Player.MaxHealth)
		&& FMath::IsNearlyEqual(
			AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()),
			Balance.Player.MaxMana)
		&& PlayerCharacter
		&& FMath::IsNearlyEqual(
			PlayerCharacter->GetCombatExitDelay(), Balance.Player.CombatExitDelay);
	const bool bProgressionValuesValid = Balance.Progression.MaxLevel == 10
		&& Balance.Progression.GetCumulativeExperienceForLevel(10) == 1730
		&& Balance.Progression.ExperienceToNextLevel.Num() == 9
		&& FMath::IsNearlyEqual(Balance.Progression.MaxHealthPerLevel, 10.f)
		&& FMath::IsNearlyEqual(Balance.Progression.MaxManaPerLevel, 3.f)
		&& FMath::IsNearlyEqual(Balance.Progression.AbilityDamageBonusPerLevel, 0.04f)
		&& Balance.MeleeEnemy.KillExperienceReward == 25
		&& Balance.Shaman.KillExperienceReward == 75
		&& Balance.Boss.KillExperienceReward == 250;

	int32 EncounterProfileCount = 0;
	int32 RewardEncounterCount = 0;
	int32 RewardGuardianCount = 0;
	bool bEncounterValuesValid = true;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->IsRewardEncounter())
		{
			++RewardEncounterCount;
			RewardGuardianCount += It->GetConfiguredSpawnCount();
			bEncounterValuesValid &= It->GetRewardChest() != nullptr
				&& It->GetConfiguredSpawnCount() > 0;
			continue;
		}
		const int32 ExpectedCount = It->GetEncounterOrder() == 1
			? Balance.Encounters.OuterGuardEnemyCount
			: (It->GetEncounterOrder() == 2
				? Balance.Encounters.EliteShamanEnemyCount
				: Balance.Encounters.DemonLordEnemyCount);
		bEncounterValuesValid &= It->GetConfiguredSpawnCount() == ExpectedCount;
		++EncounterProfileCount;
	}
	bEncounterValuesValid &= EncounterProfileCount == 3
		&& RewardEncounterCount == 2 && RewardGuardianCount == 3;

	const bool bPassed = bAuthoredAssetLoaded && bInvariantsValid
		&& bAbilityValuesValid && bPlayerValuesValid && bProgressionValuesValid
		&& bEncounterValuesValid;
	UE_LOG(LogAuraInput, Display,
		TEXT("Combat balance smoke test completed. Passed=%s AssetLoaded=%s Version=%s Invariants=%s Abilities=%s Player=%s Progression=%s Encounters=%s EncounterProfiles=%d RewardEncounters=%d RewardGuardians=%d Error=%s"),
		bPassed ? TEXT("true") : TEXT("false"),
		bAuthoredAssetLoaded ? TEXT("true") : TEXT("false"),
		*Balance.BalanceVersion.ToString(), bInvariantsValid ? TEXT("true") : TEXT("false"),
		bAbilityValuesValid ? TEXT("true") : TEXT("false"),
		bPlayerValuesValid ? TEXT("true") : TEXT("false"),
		bProgressionValuesValid ? TEXT("true") : TEXT("false"),
		bEncounterValuesValid ? TEXT("true") : TEXT("false"), EncounterProfileCount,
		RewardEncounterCount, RewardGuardianCount,
		ValidationError.IsEmpty() ? TEXT("None") : *ValidationError);
}

void AAuraPlayerController::AuraTestProgression()
{
	AuraTestGrowthM12();
}

void AAuraPlayerController::AuraTestGrowthM12()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	UAbilitySystemComponent* ASC = AuraPawn ? AuraPawn->GetAbilitySystemComponent() : nullptr;
	AAuraArchmage* Mage = nullptr;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		Mage = *It;
		break;
	}
	if (!HasAuthority() || !AuraPawn || !State || !ASC || !Mage)
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_M12_GROWTH_SMOKE setup failed"));
		return;
	}
	const FTransform OriginalTransform = AuraPawn->GetActorTransform();
	AuraPawn->TeleportTo(Mage->GetActorLocation() + FVector(110.f, 0.f, 96.f),
		FRotator::ZeroRotator, false, true);
	FString Failure;
	const float BaseHealth = ASC->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
	ServerOpenArchmage_Implementation(Mage);
	const bool bMenuOpened = bArchmageMenuOpen;
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Two, IE_Pressed, 1.f));
	const bool bConfirmationEntered = ArchmagePendingAction == 2;
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Escape, IE_Pressed, 1.f));
	const bool bCancelPreservesState = ArchmagePendingAction == 0
		&& State->GetGrowthState().Souls == 0;
	CloseArchmageMenu();
	const bool bDefaultLocked = State->GetGrowthState().PaidLevel == 1
		&& State->GetGrowthState().TotalExperience == 0
		&& State->GetGrowthState().Souls == 0
		&& AuraPawn->GetSkillRank(EAuraPlayerSkill::FireBolt) == 1
		&& AuraPawn->GetSkillRank(EAuraPlayerSkill::ArcaneShards) == 0
		&& AuraPawn->GetSkillRank(EAuraPlayerSkill::Shock) == 0;
	State->AwardExperienceAndSouls(60, 30);
	const bool bXPAutoLevels = State->GetEligibleLevel() == 2
		&& AuraPawn->GetCharacterLevel() == 2
		&& ASC->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()) > BaseHealth;
	const bool bL2Once = State->GetGrowthState().PaidLevel == 2
		&& State->GetGrowthState().Souls == 30
		&& State->GetGrowthState().UnspentSkillPoints == 1
		&& AuraPawn->GetCharacterLevel() == 2;
	const bool bUnlockConstraint = State->GetGrowthState().UnspentSkillPoints == 1
		&& State->GetGrowthState().SkillRanks.FireBolt == 1
		&& State->GetGrowthState().SkillRanks.ArcaneShards == 0
		&& State->GetGrowthState().SkillRanks.Shock == 0;
	const FGuid QId = FGuid::NewGuid();
	ServerAllocateMageSkill_Implementation(Mage, EAuraPlayerSkill::ArcaneShards, QId);
	ServerAllocateMageSkill_Implementation(Mage, EAuraPlayerSkill::ArcaneShards, QId);
	const bool bQUnlocked = State->GetGrowthState().SkillRanks.ArcaneShards == 1
		&& State->GetGrowthState().UnspentSkillPoints == 0;
	State->AwardExperienceAndSouls(100, 40);
	const bool bOtherRequired = State->GetGrowthState().SkillRanks.ArcaneShards == 1
		&& State->GetGrowthState().SkillRanks.Shock == 0
		&& State->GetGrowthState().UnspentSkillPoints == 1;
	ServerAllocateMageSkill_Implementation(Mage, EAuraPlayerSkill::Shock, FGuid::NewGuid());
	const bool bBothCombos = State->GetGrowthState().PaidLevel == 3
		&& State->GetGrowthState().SkillRanks.Shock == 1
		&& State->GetGrowthState().UnspentSkillPoints == 0;
	State->AwardExperienceAndSouls(320, 175);
	const bool bMultiLevelAuto = AuraPawn->GetCharacterLevel() == 5
		&& State->GetEligibleLevel() == 5
		&& State->GetGrowthState().UnspentSkillPoints == 2;
	ServerAllocateMageSkill_Implementation(Mage, EAuraPlayerSkill::FireBolt, FGuid::NewGuid());
	ServerAllocateMageFirePath_Implementation(Mage, EAuraFireBoltPath::Pierce, FGuid::NewGuid());
	const bool bRankThree = State->GetGrowthState().PaidLevel == 5
		&& State->GetGrowthState().SkillRanks.FireBolt == 3
		&& State->GetGrowthState().SkillRanks.FireBoltPath == EAuraFireBoltPath::Pierce
		&& State->GetGrowthState().UnspentSkillPoints == 0
		&& State->GetGrowthState().Souls == 245;
	UAuraInventoryComponent* Inventory = State->GetInventoryComponent();
	UAuraItemDefinition* StoneDefinition = UAuraItemCatalog::Get().FindItem(
		TEXT("Consumable.RespecStone"));
	FGuid StoneId;
	if (Inventory && StoneDefinition)
	{
		FAuraItemInstance Stone;
		Stone.Definition = StoneDefinition;
		Stone.Quantity = 1;
		Stone.EnsureIdentity();
		StoneId = Stone.InstanceId;
		Inventory->TryAddItem(Stone, Failure, false);
	}
	FAuraSkillRankState Draft;
	Draft.FireBolt = 1;
	Draft.ArcaneShards = 2;
	Draft.Shock = 2;
	const bool bBadStoneRejected = !State->TryRespecSkills(Draft, FGuid::NewGuid(), Failure)
		&& State->GetGrowthState().SkillRanks.FireBolt == 3;
	const FGuid RespecId = FGuid::NewGuid();
	ServerRespecMageSkills_Implementation(Mage, 1, 2, 2,
		EAuraFireBoltPath::None, StoneId, RespecId, EAuraArcanePath::None, EAuraShockPath::None);
	ServerRespecMageSkills_Implementation(Mage, 1, 2, 2,
		EAuraFireBoltPath::None, StoneId, RespecId, EAuraArcanePath::None, EAuraShockPath::None);
	const bool bRespec = State->GetGrowthState().SkillRanks.FireBolt == 1
		&& State->GetGrowthState().SkillRanks.ArcaneShards == 2
		&& State->GetGrowthState().SkillRanks.Shock == 2
		&& Inventory && Inventory->GetTotalQuantity(TEXT("Consumable.RespecStone")) == 0
		&& State->GetGrowthState().GetSpentSkillPoints()
			+ State->GetGrowthState().UnspentSkillPoints == 4;
	FAuraGrowthState Snapshot;
	const UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	const bool bSnapshot = Run && Run->TryGetSavedGrowth(Snapshot)
		&& Snapshot.PaidLevel == 5 && Snapshot.TotalExperience == 480
		&& Snapshot.Souls == 245 && Snapshot.SkillRanks.ArcaneShards == 2;
	FActorSpawnParameters LegacyParams;
	LegacyParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraPlayerState* LegacyState = GetWorld()->SpawnActor<AAuraPlayerState>(
		AAuraPlayerState::StaticClass(), FTransform::Identity, LegacyParams);
	bool bLegacyMigration = false;
	if (LegacyState)
	{
		LegacyState->MigrateLegacyGrowth(3, 160);
		LegacyState->MigrateLegacyGrowth(5, 480);
		const FAuraGrowthState& Migrated = LegacyState->GetGrowthState();
		bLegacyMigration = Migrated.PaidLevel == 3
			&& Migrated.TotalExperience == 160
			&& Migrated.SkillRanks.FireBolt == 1
			&& Migrated.SkillRanks.ArcaneShards == 1
			&& Migrated.SkillRanks.Shock == 1
			&& Migrated.UnspentSkillPoints == 0;
		LegacyState->Destroy();
		if (UAuraRunSubsystem* MutableRun = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			MutableRun->UpdateSavedGrowth(Snapshot);
		}
	}
	AuraPawn->SetPaidProgression(1, 0);
	AuraPawn->SyncGrowthFromPlayerState(true);
	const bool bPawnRestore = AuraPawn->GetCharacterLevel() == 5
		&& AuraPawn->GetTotalExperience() == 480
		&& AuraPawn->GetSkillRank(EAuraPlayerSkill::Shock) == 2;
	const bool bFeedbackAssets = AuraPawn->AreProgressionFeedbackEffectsConfigured();
	const bool bPassed = bMenuOpened && bConfirmationEntered && bCancelPreservesState
		&& bDefaultLocked && bXPAutoLevels && bL2Once
		&& bUnlockConstraint && bQUnlocked && bOtherRequired && bBothCombos
		&& bMultiLevelAuto && bRankThree && bBadStoneRejected && bRespec
		&& bSnapshot && bLegacyMigration && bPawnRestore && bFeedbackAssets;
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_M12_GROWTH_SMOKE Passed=%s Menu=%s Confirm=%s Cancel=%s DefaultLocked=%s AutoLevel=%s L2Once=%s UnlockConstraint=%s Q=%s ERequired=%s BothCombos=%s MultiLevel=%s Rank3=%s BadStone=%s Respec=%s Snapshot=%s Legacy=%s PawnRestore=%s FeedbackAssets=%s"),
		bPassed ? TEXT("true") : TEXT("false"),
		bMenuOpened ? TEXT("true") : TEXT("false"),
		bConfirmationEntered ? TEXT("true") : TEXT("false"),
		bCancelPreservesState ? TEXT("true") : TEXT("false"),
		bDefaultLocked ? TEXT("true") : TEXT("false"),
		bXPAutoLevels ? TEXT("true") : TEXT("false"),
		bL2Once ? TEXT("true") : TEXT("false"),
		bUnlockConstraint ? TEXT("true") : TEXT("false"),
		bQUnlocked ? TEXT("true") : TEXT("false"),
		bOtherRequired ? TEXT("true") : TEXT("false"),
		bBothCombos ? TEXT("true") : TEXT("false"),
		bMultiLevelAuto ? TEXT("true") : TEXT("false"),
		bRankThree ? TEXT("true") : TEXT("false"),
		bBadStoneRejected ? TEXT("true") : TEXT("false"),
		bRespec ? TEXT("true") : TEXT("false"),
		bSnapshot ? TEXT("true") : TEXT("false"),
		bLegacyMigration ? TEXT("true") : TEXT("false"),
		bPawnRestore ? TEXT("true") : TEXT("false"),
		bFeedbackAssets ? TEXT("true") : TEXT("false"));
	AuraPawn->TeleportTo(OriginalTransform.GetLocation(), OriginalTransform.Rotator(), false, true);
}

void AAuraPlayerController::AuraTestServiceUIM17()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* GrowthOwner = GetPlayerState<AAuraPlayerState>();
	const UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	AAuraArchmage* Mage = nullptr;
	AAuraCampVendor* Merchant = nullptr;
	AAuraCampVendor* Smith = nullptr;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		Mage = *It;
		break;
	}
	for (TActorIterator<AAuraCampVendor> It(GetWorld()); It; ++It)
	{
		if (It->GetServiceType() == EAuraCampService::Merchant) Merchant = *It;
		if (It->GetServiceType() == EAuraCampService::Blacksmith) Smith = *It;
	}
	if (!HasAuthority() || !IsLocalController() || !AuraPawn || !GrowthOwner
		|| !Run || Run->IsPersistentSaveEnabled() || !Mage || !Merchant || !Smith)
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_M17_SERVICE_UI Passed=false Setup=false"));
		return;
	}
	const FTransform OriginalTransform = AuraPawn->GetActorTransform();
	AuraPawn->TeleportTo(Mage->GetActorLocation() + FVector(110.f, 0.f, 96.f),
		FRotator::ZeroRotator, false, true);
	ClientOpenArchmage_Implementation(Mage);
	const bool bMageWidget = ActiveServiceMenuWidget
		&& ActiveServiceMenuWidget->IsMageMenu();
	const bool bMageSelect = bMageWidget
		&& ActiveServiceMenuWidget->ClickActionForSmoke(2)
		&& ArchmagePendingAction == 2;
	const bool bMageCancel = bMageSelect
		&& ActiveServiceMenuWidget->ClickActionForSmoke(7)
		&& ArchmagePendingAction == 0;
	FString GrowthFailure;
	GrowthOwner->AwardExperienceAndSouls(160, 70);
	const bool bMageComboReady = GrowthOwner->GetGrowthState().PaidLevel == 3
		&& GrowthOwner->TryAllocateSkill(EAuraPlayerSkill::ArcaneShards, GrowthFailure)
		&& GrowthOwner->GetGrowthState().UnspentComboPoints == 1;
	const bool bMageComboEntry = bMageCancel && bMageComboReady
		&& ActiveServiceMenuWidget->ClickActionForSmoke(10)
		&& ArchmagePendingAction == 10;
	const bool bMageComboChoice = bMageComboEntry
		&& ActiveServiceMenuWidget->ClickActionForSmoke(2)
		&& MageDraftComboChoice == 0;
	const bool bMageComboCommit = bMageComboChoice
		&& ActiveServiceMenuWidget->ClickActionForSmoke(6)
		&& GrowthOwner->GetGrowthState().ComboRanks.Ember == 1
		&& GrowthOwner->GetGrowthState().UnspentComboPoints == 0;
	const bool bMageClose = bMageComboCommit
		&& ActiveServiceMenuWidget->ClickActionForSmoke(9)
		&& !bArchmageMenuOpen && !ActiveServiceMenuWidget;

	AuraPawn->TeleportTo(Merchant->GetActorLocation() + FVector(110.f, 0.f, 96.f),
		FRotator::ZeroRotator, false, true);
	ClientOpenCampVendor_Implementation(Merchant);
	const bool bMerchantWidget = ActiveServiceMenuWidget
		&& !ActiveServiceMenuWidget->IsMageMenu();
	const TArray<FAuraCampServiceRow> MerchantRows = BuildCampServiceRows();
	const bool bMerchantReselect = bMerchantWidget && MerchantRows.Num() >= 2
		&& ActiveServiceMenuWidget->ClickActionForSmoke(1) && bCampConfirming
		&& CampPendingRow.OfferId == MerchantRows[0].OfferId
		&& ActiveServiceMenuWidget->ClickActionForSmoke(2) && bCampConfirming
		&& CampPendingRow.OfferId == MerchantRows[1].OfferId;
	const bool bMerchantSell = bMerchantWidget
		&& ActiveServiceMenuWidget->ClickActionForSmoke(12) && bCampSelling;
	const bool bMerchantBuy = bMerchantSell
		&& ActiveServiceMenuWidget->ClickActionForSmoke(11) && !bCampSelling;
	const bool bMerchantClose = bMerchantBuy
		&& ActiveServiceMenuWidget->ClickActionForSmoke(15)
		&& !bCampMenuOpen && !ActiveServiceMenuWidget;

	AuraPawn->TeleportTo(Smith->GetActorLocation() + FVector(110.f, 0.f, 96.f),
		FRotator::ZeroRotator, false, true);
	ClientOpenCampVendor_Implementation(Smith);
	const bool bSmithWidget = ActiveServiceMenuWidget
		&& !ActiveServiceMenuWidget->IsMageMenu();
	const bool bSmithCraftTab = bSmithWidget
		&& ActiveServiceMenuWidget->ClickActionForSmoke(12) && SmithMode == 1;
	const TArray<FAuraCampServiceRow> SmithRows = BuildCampServiceRows();
	const bool bSmithReselect = bSmithCraftTab && SmithRows.Num() >= 2
		&& ActiveServiceMenuWidget->ClickActionForSmoke(1) && bCampConfirming
		&& CampPendingRow.OfferId == SmithRows[0].OfferId
		&& ActiveServiceMenuWidget->ClickActionForSmoke(2) && bCampConfirming
		&& CampPendingRow.OfferId == SmithRows[1].OfferId;
	const bool bSmithClose = bSmithWidget
		&& ActiveServiceMenuWidget->ClickActionForSmoke(15)
		&& !bCampMenuOpen && !ActiveServiceMenuWidget;
	AuraPawn->TeleportTo(OriginalTransform.GetLocation(), OriginalTransform.Rotator(), false, true);
	const bool bPassed = bMageWidget && bMageSelect && bMageCancel
		&& bMageComboEntry && bMageComboChoice && bMageComboCommit && bMageClose
		&& bMerchantWidget && bMerchantReselect && bMerchantSell
		&& bMerchantBuy && bMerchantClose
		&& bSmithWidget && bSmithReselect && bSmithClose;
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_M17_SERVICE_UI Passed=%s Mage=%d/%d/%d/%d Combo=%d/%d/%d Merchant=%d/%d/%d/%d/%d Smith=%d/%d/%d"),
		bPassed ? TEXT("true") : TEXT("false"), bMageWidget, bMageSelect, bMageCancel,
		bMageClose, bMageComboEntry, bMageComboChoice, bMageComboCommit,
		bMerchantWidget, bMerchantReselect, bMerchantSell, bMerchantBuy, bMerchantClose,
		bSmithWidget, bSmithReselect, bSmithClose);
}

void AAuraPlayerController::AuraTestRouteRewardsM22()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	FString SaveSlot;
	const bool bIsolatedSave = FParse::Value(FCommandLine::Get(),
		TEXT("AuraSaveSlot="), SaveSlot)
		&& SaveSlot.StartsWith(TEXT("AuraM22Reward_"));
	AAuraEncounterVolume* Outer = nullptr;
	AAuraEncounterVolume* North = nullptr;
	TSet<FName> BudgetIds;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 1) Outer = *It;
		if (It->GetEncounterId() == FName(TEXT("LowerNorthBoonGuardians"))) North = *It;
		if (!It->GetM22BudgetId().IsNone()) BudgetIds.Add(It->GetM22BudgetId());
	}
	if (!HasAuthority() || GetNetMode() != NM_Standalone || !AuraPawn || !State
		|| !Quest || !Run || (Run->IsPersistentSaveEnabled() && !bIsolatedSave)
		|| !Outer || !North
		|| State->GetGrowthState().TotalExperience != 0
		|| State->GetGrowthState().Souls != 0
		|| BudgetIds.Num() != 12
		|| Outer->GetM22BudgetId() != FName(TEXT("M22.OuterGuard"))
		|| North->GetM22BudgetId() != FName(TEXT("LowerNorthBoonGuardians")))
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_M22_ROUTE_REWARD Passed=false Setup=false Registered=%d"),
			BudgetIds.Num());
		return;
	}
	Outer->StartEncounter(AuraPawn, true);
	const int32 OuterKills = DefeatEncounterEnemiesForSmokeTest(Outer);
	TWeakObjectPtr<AAuraPlayerController> WeakSelf(this);
	TWeakObjectPtr<AAuraEncounterVolume> WeakOuter(Outer);
	TWeakObjectPtr<AAuraEncounterVolume> WeakNorth(North);
	TWeakObjectPtr<AAuraPlayerState> WeakState(State);
	TWeakObjectPtr<AAuraGameState> WeakQuest(Quest);
	TWeakObjectPtr<UAuraRunSubsystem> WeakRun(Run);
	FTimerHandle FirstCheck;
	GetWorldTimerManager().SetTimer(FirstCheck,
		[WeakSelf, WeakOuter, WeakNorth, WeakState, WeakQuest, WeakRun, OuterKills]()
	{
		AAuraPlayerController* Controller = WeakSelf.Get();
		AAuraEncounterVolume* OuterRoom = WeakOuter.Get();
		AAuraEncounterVolume* NorthRoom = WeakNorth.Get();
		AAuraPlayerState* Player = WeakState.Get();
		AAuraGameState* GameState = WeakQuest.Get();
		UAuraRunSubsystem* RunState = WeakRun.Get();
		if (!Controller || !OuterRoom || !NorthRoom || !Player || !GameState || !RunState)
		{
			UE_LOG(LogAuraInput, Error, TEXT("AURA_M22_ROUTE_REWARD Passed=false Phase=Outer"));
			return;
		}
		const bool bOuter = OuterKills > 0
			&& Player->GetGrowthState().TotalExperience >= 90
			&& Player->GetGrowthState().Souls >= 65
			&& GameState->IsRewardEncounterCleared(TEXT("M22.OuterGuard"))
			&& RunState->IsRewardEncounterCleared(TEXT("M22.OuterGuard"));
		const int32 OuterXP = Player->GetGrowthState().TotalExperience;
		const int32 OuterSouls = Player->GetGrowthState().Souls;
		OuterRoom->StartEncounter(Cast<AAuraCharacter>(Controller->GetPawn()), true);
		const bool bNoReplay = OuterRoom->GetEncounterState()
			== EAuraEncounterState::Completed
			&& Player->GetGrowthState().TotalExperience == OuterXP
			&& Player->GetGrowthState().Souls == OuterSouls;
		NorthRoom->StartEncounter(Cast<AAuraCharacter>(Controller->GetPawn()), true);
		const int32 NorthKills = DefeatEncounterEnemiesForSmokeTest(NorthRoom);
		FTimerHandle FinalCheck;
		Controller->GetWorldTimerManager().SetTimer(FinalCheck,
			[WeakNorth, WeakState, WeakQuest, WeakRun, bOuter, bNoReplay,
				OuterXP, OuterSouls, OuterKills, NorthKills]()
		{
			AAuraEncounterVolume* NorthFight = WeakNorth.Get();
			AAuraPlayerState* FinalPlayer = WeakState.Get();
			AAuraGameState* FinalQuest = WeakQuest.Get();
			UAuraRunSubsystem* FinalRun = WeakRun.Get();
			const bool bNorth = NorthFight && FinalPlayer && FinalQuest && FinalRun
				&& NorthKills > 0
				&& FinalPlayer->GetGrowthState().TotalExperience - OuterXP >= 170
				&& FinalPlayer->GetGrowthState().Souls - OuterSouls >= 110
				&& FinalQuest->IsRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"))
				&& FinalRun->IsRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"));
			const bool bSaved = !FinalRun || !FinalRun->IsPersistentSaveEnabled()
				|| FinalRun->FlushPersistentSave();
			UE_LOG(LogAuraInput, Display,
				TEXT("AURA_M22_ROUTE_REWARD Passed=%s Registered=12 Outer=%d NoReplay=%d North=%d Saved=%d Kills=%d/%d XP=%d Souls=%d"),
				bOuter && bNoReplay && bNorth && bSaved ? TEXT("true") : TEXT("false"),
				bOuter, bNoReplay, bNorth, bSaved, OuterKills, NorthKills,
				FinalPlayer ? FinalPlayer->GetGrowthState().TotalExperience : -1,
				FinalPlayer ? FinalPlayer->GetGrowthState().Souls : -1);
		}, 0.3f, false);
	}, 0.3f, false);
}

namespace
{
	struct FM22SpineSmokeCard
	{
		const TCHAR* Id;
		int32 Experience;
		int32 Souls;
		int32 Enemies;
	};
	constexpr FM22SpineSmokeCard M22SpineSmokeCards[] = {
		{TEXT("M22.OuterGuard"), 90, 65, 2},
		{TEXT("M22.OuterPractice"), 150, 110, 3},
		{TEXT("M22.UpperShaman"), 180, 140, 4},
		{TEXT("M22.UpperCrossroads"), 120, 100, 3},
		{TEXT("M22.MiddleInterlock"), 120, 115, 3},
		{TEXT("M22.DeepHallPincer"), 210, 165, 4},
		{TEXT("M22.CorePrep"), 170, 140, 3},
		{TEXT("M22.CorePractice"), 180, 145, 3},
	};
}

void AAuraPlayerController::AuraTestFullSpineM22()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* SmokeState = GetPlayerState<AAuraPlayerState>();
	AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	AAuraEncounterVolume* LastRoom = nullptr;
	TSet<FName> Found;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		const FName Id = It->GetM22BudgetId();
		for (const FM22SpineSmokeCard& Card : M22SpineSmokeCards)
		{
			if (Id == FName(Card.Id)) Found.Add(Id);
		}
		if (Id == FName(TEXT("M22.CorePractice"))) LastRoom = *It;
	}
	if (!HasAuthority() || GetNetMode() != NM_Standalone
		|| !FParse::Param(FCommandLine::Get(), TEXT("AuraNoPersistentSave"))
		|| !AuraPawn || !SmokeState || !Quest || Found.Num() != UE_ARRAY_COUNT(M22SpineSmokeCards)
		|| !LastRoom || SmokeState->GetGrowthState().TotalExperience != 0
		|| SmokeState->GetGrowthState().Souls != 0)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_M22_FULL_SPINE Passed=false Setup=false Found=%d"), Found.Num());
		return;
	}
	LastRoom->StartEncounter(AuraPawn, true);
	if (LastRoom->GetEncounterState() != EAuraEncounterState::Dormant)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_M22_FULL_SPINE Passed=false PrematureFinalRoom=true"));
		return;
	}
	AuraTestFullSpineM22Step(0);
}

void AAuraPlayerController::AuraTestFullSpineM22Step(const int32 Index)
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* SmokeState = GetPlayerState<AAuraPlayerState>();
	AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!AuraPawn || !SmokeState || !Quest)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_M22_FULL_SPINE Passed=false MissingRuntime=%d"), Index);
		return;
	}
	if (Index >= UE_ARRAY_COUNT(M22SpineSmokeCards))
	{
		const bool bPassed = SmokeState->GetGrowthState().TotalExperience >= 1220
			&& SmokeState->GetGrowthState().Souls >= 980;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M22_FULL_SPINE Passed=%s Rooms=%d XP=%d Souls=%d"),
			bPassed ? TEXT("true") : TEXT("false"), Index,
			SmokeState->GetGrowthState().TotalExperience,
			SmokeState->GetGrowthState().Souls);
		return;
	}
	const FM22SpineSmokeCard& Card = M22SpineSmokeCards[Index];
	AAuraEncounterVolume* Room = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetM22BudgetId() == FName(Card.Id)) { Room = *It; break; }
	}
	if (!Room || Room->GetEncounterState() != EAuraEncounterState::Dormant)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_M22_FULL_SPINE Passed=false RoomUnavailable=%s"), Card.Id);
		return;
	}
	const int32 XPBefore = SmokeState->GetGrowthState().TotalExperience;
	const int32 SoulsBefore = SmokeState->GetGrowthState().Souls;
	Room->StartEncounter(AuraPawn, true);
	const bool bCombatStarted = Room->GetEncounterState() == EAuraEncounterState::LockedCombat;
	const int32 Defeated = bCombatStarted ? DefeatEncounterEnemiesForSmokeTest(Room) : 0;
	TWeakObjectPtr<AAuraPlayerController> WeakSelf(this);
	TWeakObjectPtr<AAuraEncounterVolume> WeakRoom(Room);
	FTimerHandle CheckTimer;
	GetWorldTimerManager().SetTimer(CheckTimer,
		[WeakSelf, WeakRoom, Index, XPBefore, SoulsBefore, Defeated, bCombatStarted]()
	{
		AAuraPlayerController* Controller = WeakSelf.Get();
		AAuraEncounterVolume* CheckedRoom = WeakRoom.Get();
		if (!Controller || !CheckedRoom) return;
		AAuraPlayerState* CheckedPlayer = Controller->GetPlayerState<AAuraPlayerState>();
		AAuraGameState* CheckedQuest = Controller->GetWorld()->GetGameState<AAuraGameState>();
		const FM22SpineSmokeCard& CheckedCard = M22SpineSmokeCards[Index];
		const bool bPassed = CheckedPlayer && CheckedQuest && bCombatStarted
			&& Defeated == CheckedCard.Enemies
			&& CheckedRoom->GetEncounterState() == EAuraEncounterState::Completed
			&& CheckedQuest->IsRewardEncounterCleared(FName(CheckedCard.Id))
			&& CheckedPlayer->GetGrowthState().TotalExperience - XPBefore >= CheckedCard.Experience
			&& CheckedPlayer->GetGrowthState().Souls - SoulsBefore >= CheckedCard.Souls;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M22_SPINE_ROOM Id=%s Passed=%s Kills=%d XPDelta=%d SoulsDelta=%d"),
			CheckedCard.Id, bPassed ? TEXT("true") : TEXT("false"), Defeated,
			CheckedPlayer ? CheckedPlayer->GetGrowthState().TotalExperience - XPBefore : -1,
			CheckedPlayer ? CheckedPlayer->GetGrowthState().Souls - SoulsBefore : -1);
		if (!bPassed)
		{
			UE_LOG(LogAuraInput, Error,
				TEXT("AURA_M22_FULL_SPINE Passed=false FailedIndex=%d"), Index);
			return;
		}
		Controller->AuraTestFullSpineM22Step(Index + 1);
	}, 0.35f, false);
}

void AAuraPlayerController::AuraTestRouteRewardsReloadM22()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	FString SaveSlot;
	const bool bIsolatedSave = FParse::Value(FCommandLine::Get(),
		TEXT("AuraSaveSlot="), SaveSlot)
		&& SaveSlot.StartsWith(TEXT("AuraM22Reward_"));
	AAuraEncounterVolume* Outer = nullptr;
	AAuraEncounterVolume* North = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 1) Outer = *It;
		if (It->GetEncounterId() == FName(TEXT("LowerNorthBoonGuardians"))) North = *It;
	}
	if (!HasAuthority() || GetNetMode() != NM_Standalone || !bIsolatedSave
		|| !AuraPawn || !State || !Quest || !Run || !Outer || !North)
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_M22_ROUTE_RELOAD Passed=false Setup=false"));
		return;
	}
	const int32 XPBefore = State->GetGrowthState().TotalExperience;
	const int32 SoulsBefore = State->GetGrowthState().Souls;
	Outer->StartEncounter(AuraPawn, true);
	North->StartEncounter(AuraPawn, true);
	const bool bPassed = Run->IsPersistentSaveEnabled()
		&& Quest->IsRewardEncounterCleared(TEXT("M22.OuterGuard"))
		&& Quest->IsRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"))
		&& Run->IsRewardEncounterCleared(TEXT("M22.OuterGuard"))
		&& Run->IsRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"))
		&& Outer->GetEncounterState() == EAuraEncounterState::Completed
		&& North->GetEncounterState() == EAuraEncounterState::RewardAvailable
		&& XPBefore >= 260 && SoulsBefore >= 175
		&& State->GetGrowthState().TotalExperience == XPBefore
		&& State->GetGrowthState().Souls == SoulsBefore;
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_M22_ROUTE_RELOAD Passed=%s XP=%d Souls=%d OuterState=%d NorthState=%d"),
		bPassed ? TEXT("true") : TEXT("false"), XPBefore, SoulsBefore,
		static_cast<int32>(Outer->GetEncounterState()),
		static_cast<int32>(North->GetEncounterState()));
}

void AAuraPlayerController::AuraTestGrowthCoopM12()
{
	if (!HasAuthority() || !GetWorld()) return;
	const AGameStateBase* State = GetWorld()->GetGameState();
	if (!State || State->PlayerArray.Num() < 2)
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_M12_COOP_SERVER setup failed: two players required"));
		return;
	}
	AAuraPlayerState* First = Cast<AAuraPlayerState>(State->PlayerArray[0]);
	AAuraPlayerState* Second = Cast<AAuraPlayerState>(State->PlayerArray[1]);
	AAuraCharacter* FirstPawn = First ? Cast<AAuraCharacter>(First->GetPawn()) : nullptr;
	AAuraCharacter* SecondPawn = Second ? Cast<AAuraCharacter>(Second->GetPawn()) : nullptr;
	AAuraEncounterVolume* Room = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 1) { Room = *It; break; }
	}
	if (!FirstPawn || !SecondPawn || !Room || FirstPawn->IsDead() || SecondPawn->IsDead())
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_M12_COOP_SERVER missing player pawn or outer room"));
		return;
	}
	FirstPawn->TeleportTo(Room->GetActorLocation() + FVector(400.f, -160.f, 96.f),
		FRotator::ZeroRotator, false, true);
	SecondPawn->TeleportTo(Room->GetActorLocation() + FVector(400.f, 160.f, 96.f),
		FRotator::ZeroRotator, false, true);
	FirstPawn->GetCharacterMovement()->StopMovementImmediately();
	SecondPawn->GetCharacterMovement()->StopMovementImmediately();
	const int32 FirstXPBefore = First->GetGrowthState().TotalExperience;
	const int32 SecondXPBefore = Second->GetGrowthState().TotalExperience;
	const int32 FirstSoulsBefore = First->GetGrowthState().Souls;
	const int32 SecondSoulsBefore = Second->GetGrowthState().Souls;
	Room->StartEncounter(FirstPawn, true);
	const bool bBothEligible = Room->GetEligibleRewardPlayers().Contains(First)
		&& Room->GetEligibleRewardPlayers().Contains(Second)
		&& Room->GetEligibleRewardPlayers().Num() == 2;
	UAbilitySystemComponent* FirstASC = FirstPawn->GetAbilitySystemComponent();
	UAbilitySystemComponent* SecondASC = SecondPawn->GetAbilitySystemComponent();
	if (FirstASC && SecondASC)
	{
		FirstASC->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 0.f);
		SecondASC->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 0.f);
	}
	const TArray<AAuraEnemy*> Enemies = Room->GetActiveEnemies();
	int32 Defeated = 0;
	for (AAuraEnemy* Enemy : Enemies)
	{
		if (!Enemy) continue;
		Enemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AI->StopMovement();
			AI->SetActorTickEnabled(false);
		}
		if (ApplyEnemyDamageForSmokeTest(FirstPawn, Enemy, 10000.f) && Enemy->IsDead())
		{
			++Defeated;
		}
	}
	const int32 FirstKillXP = First->GetGrowthState().TotalExperience - FirstXPBefore;
	const int32 FirstKillSouls = First->GetGrowthState().Souls - FirstSoulsBefore;
	const bool bPersonalReward = Defeated > 0 && Defeated == Enemies.Num()
		&& FirstKillXP > 0 && FirstKillSouls > 0
		&& Second->GetGrowthState().TotalExperience - SecondXPBefore == FirstKillXP
		&& Second->GetGrowthState().Souls - SecondSoulsBefore == FirstKillSouls;
	const float FirstMana = FirstASC
		? FirstASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
	const float SecondMana = SecondASC
		? SecondASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
	const bool bKillerOnlyMana = FirstMana > 0.f && FMath::IsNearlyZero(SecondMana, 0.05f);
	const int32 AfterXP = First->GetGrowthState().TotalExperience;
	if (Enemies.Num() > 0 && Enemies[0]) Enemies[0]->TryAwardKillRewards(FirstPawn);
	const bool bNoDuplicate = First->GetGrowthState().TotalExperience == AfterXP;
	AAuraEncounterVolume* EliteRoom = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 2) { EliteRoom = *It; break; }
	}
	bool bEliteDefeated = false;
	if (EliteRoom)
	{
		FirstPawn->TeleportTo(EliteRoom->GetActorLocation() + FVector(400.f, -160.f, 96.f),
			FRotator::ZeroRotator, false, true);
		SecondPawn->TeleportTo(EliteRoom->GetActorLocation() + FVector(400.f, 160.f, 96.f),
			FRotator::ZeroRotator, false, true);
		EliteRoom->StartEncounter(FirstPawn, true);
		const TArray<AAuraEnemy*> EliteEnemies = EliteRoom->GetActiveEnemies();
		for (AAuraEnemy* EliteEnemy : EliteEnemies)
		{
			if (!EliteEnemy) continue;
			EliteEnemy->GetCharacterMovement()->DisableMovement();
			if (AAuraAIController* AI = Cast<AAuraAIController>(EliteEnemy->GetController()))
			{
				AI->StopMovement();
				AI->SetActorTickEnabled(false);
			}
			ApplyEnemyDamageForSmokeTest(FirstPawn, EliteEnemy, 10000.f);
		}
		bEliteDefeated = !EliteEnemies.IsEmpty()
			&& EliteRoom->GetRemainingEnemyCount() == 0;
	}
	TWeakObjectPtr<AAuraPlayerState> WeakFirst(First);
	TWeakObjectPtr<AAuraPlayerState> WeakSecond(Second);
	FTimerHandle GrowthRewardCheck;
	GetWorldTimerManager().SetTimer(GrowthRewardCheck,
		[WeakFirst, WeakSecond, bBothEligible, bPersonalReward,
			bKillerOnlyMana, bNoDuplicate, bEliteDefeated, Defeated,
			FirstMana, SecondMana]()
	{
		const AAuraPlayerState* FirstState = WeakFirst.Get();
		const AAuraPlayerState* SecondState = WeakSecond.Get();
		const bool bBudget = FirstState && SecondState
			&& FirstState->GetGrowthState().TotalExperience >= 270
			&& SecondState->GetGrowthState().TotalExperience
				== FirstState->GetGrowthState().TotalExperience
			&& FirstState->GetGrowthState().Souls >= 205
			&& SecondState->GetGrowthState().Souls
				== FirstState->GetGrowthState().Souls
			&& FirstState->GetEligibleLevel() >= 3
			&& SecondState->GetEligibleLevel() >= 3
			&& FirstState->GetGrowthState().PaidLevel == 1
			&& SecondState->GetGrowthState().PaidLevel == 1;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M12_COOP_SERVER Passed=%s Eligible=%d PersonalXPAndSouls=%d KillerManaOnly=%d NoDuplicate=%d EliteCatchUp=%d Defeated=%d XP=%d/%d Souls=%d/%d Mana=%.2f/%.2f"),
			bBothEligible && bPersonalReward && bKillerOnlyMana && bNoDuplicate
				&& bEliteDefeated && bBudget ? TEXT("true") : TEXT("false"),
			bBothEligible, bPersonalReward, bKillerOnlyMana, bNoDuplicate,
			bEliteDefeated && bBudget, Defeated,
			FirstState ? FirstState->GetGrowthState().TotalExperience : -1,
			SecondState ? SecondState->GetGrowthState().TotalExperience : -1,
			FirstState ? FirstState->GetGrowthState().Souls : -1,
			SecondState ? SecondState->GetGrowthState().Souls : -1,
			FirstMana, SecondMana);
	}, 0.5f, false);
	if (FParse::Param(FCommandLine::Get(), TEXT("AuraPostBossCoopM12Smoke")))
	{
		TWeakObjectPtr<AAuraPlayerController> WeakSelf(this);
		FTimerHandle VictoryTimer;
		GetWorldTimerManager().SetTimer(VictoryTimer, [WeakSelf]()
		{
			if (!WeakSelf.IsValid()) return;
			UWorld* World = WeakSelf->GetWorld();
			AAuraGameState* GameState = World ? World->GetGameState<AAuraGameState>() : nullptr;
			if (!GameState) return;
			GameState->MarkRunCompleted();
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get()))
				{
					Controller->HandleDemoVictory(nullptr);
				}
			}
		}, 0.75f, false);
		FTimerHandle CampVerifyTimer;
		GetWorldTimerManager().SetTimer(CampVerifyTimer, [WeakSelf]()
		{
			if (!WeakSelf.IsValid()) return;
			UWorld* World = WeakSelf->GetWorld();
			AAuraArchmage* Mage = nullptr;
			for (TActorIterator<AAuraArchmage> It(World); It; ++It) { Mage = *It; break; }
			int32 ReturnedPlayers = 0;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get());
				const AAuraCharacter* Pawn = Controller
					? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
				if (Mage && Pawn && !Controller->bDemoCompleted
					&& FVector::DistSquared2D(Pawn->GetActorLocation(), Mage->GetActorLocation())
						< FMath::Square(Mage->GetInteractionRadius()))
				{
					++ReturnedPlayers;
				}
			}
			UE_LOG(LogAuraInput, Display,
				TEXT("AURA_M12_COOP_CAMP_SERVER Passed=%s Returned=%d"),
				ReturnedPlayers == 2 ? TEXT("true") : TEXT("false"), ReturnedPlayers);
		}, 7.f, false);
	}
	FTimerHandle DoneTimer;
	GetWorldTimerManager().SetTimer(DoneTimer, []()
	{
		UE_LOG(LogAuraInput, Display, TEXT("AURA_M12_COOP_SERVER_DONE"));
	}, 9.f, false);
}

void AAuraPlayerController::AuraObserveGrowthM12()
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const FAuraGrowthState* Growth = State ? &State->GetGrowthState() : nullptr;
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_M12_COOP_CLIENT Passed=%s XP=%d Souls=%d PaidLevel=%d Q=%d E=%d"),
		Growth && Growth->TotalExperience >= 270 && Growth->Souls >= 205
			&& Growth->PaidLevel == 1 && Growth->SkillRanks.ArcaneShards == 0
			&& Growth->SkillRanks.Shock == 0 ? TEXT("true") : TEXT("false"),
		Growth ? Growth->TotalExperience : -1,
		Growth ? Growth->Souls : -1,
		Growth ? Growth->PaidLevel : -1,
		Growth ? Growth->SkillRanks.ArcaneShards : -1,
		Growth ? Growth->SkillRanks.Shock : -1);
	if (FParse::Param(FCommandLine::Get(), TEXT("AuraPostBossCoopM12Smoke")))
	{
		TWeakObjectPtr<AAuraPlayerController> WeakSelf(this);
		FTimerHandle ReturnTimer;
		GetWorldTimerManager().SetTimer(ReturnTimer, [WeakSelf]()
		{
			if (WeakSelf.IsValid()) WeakSelf->RestartDemo();
		}, 0.5f, false);
		FTimerHandle VerifyTimer;
		GetWorldTimerManager().SetTimer(VerifyTimer, [WeakSelf]()
		{
			if (!WeakSelf.IsValid()) return;
			AAuraArchmage* Mage = nullptr;
			for (TActorIterator<AAuraArchmage> It(WeakSelf->GetWorld()); It; ++It)
			{
				Mage = *It;
				break;
			}
			const AAuraCharacter* Pawn = Cast<AAuraCharacter>(WeakSelf->GetPawn());
			const bool bPassed = Mage && Pawn && !WeakSelf->bDemoCompleted
				&& !WeakSelf->IsMoveInputIgnored() && WeakSelf->GetViewTarget() == Pawn
				&& FVector::DistSquared2D(Pawn->GetActorLocation(), Mage->GetActorLocation())
					< FMath::Square(Mage->GetInteractionRadius());
			UE_LOG(LogAuraInput, Display,
				TEXT("AURA_M12_COOP_CAMP_CLIENT Passed=%s ReturnReady=%s Pawn=%s"),
				bPassed ? TEXT("true") : TEXT("false"),
				WeakSelf->bDemoCompleted ? TEXT("false") : TEXT("true"),
				*GetNameSafe(Pawn));
		}, 2.f, false);
	}
}

void AAuraPlayerController::AuraTestPostBossCampM12()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!Run || !State || !AuraPawn)
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_M12_POSTBOSS_CAMP setup failed"));
		return;
	}
	if (!Run->IsAtPostBossCamp())
	{
		FAuraGrowthState Prepared;
		Prepared.PaidLevel = 4;
		Prepared.TotalExperience = 480;
		Prepared.Souls = 125;
		Prepared.SkillRanks.FireBolt = 2;
		Prepared.SkillRanks.ArcaneShards = 1;
		Prepared.SkillRanks.Shock = 1;
		Prepared.UnspentSkillPoints = 0;
		Run->UpdateSavedGrowth(Prepared);
		Run->ResetRun();
		Run->RequestPostBossCamp();
		UGameplayStatics::OpenLevel(this,
			FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
		return;
	}
	AAuraArchmage* Mage = nullptr;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		Mage = *It;
		break;
	}
	const bool bArrived = Mage
		&& FVector::DistSquared2D(AuraPawn->GetActorLocation(), Mage->GetActorLocation())
			< FMath::Square(Mage->GetInteractionRadius())
		&& State->GetGrowthState().PaidLevel == 5
		&& State->GetGrowthState().TotalExperience == 480
		&& State->GetGrowthState().Souls == 125;
	const bool bAutoLeveled = State->GetGrowthState().PaidLevel == 5
		&& State->GetGrowthState().Souls == 125
		&& State->GetGrowthState().UnspentSkillPoints == 1;
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_M12_POSTBOSS_CAMP Passed=%s Arrived=%s AutoLevelL5=%s PaidLevel=%d XP=%d Souls=%d Points=%d"),
		bArrived && bAutoLeveled ? TEXT("true") : TEXT("false"),
		bArrived ? TEXT("true") : TEXT("false"),
		bAutoLeveled ? TEXT("true") : TEXT("false"),
		State->GetGrowthState().PaidLevel,
		State->GetGrowthState().TotalExperience,
		State->GetGrowthState().Souls,
		State->GetGrowthState().UnspentSkillPoints);
}

void AAuraPlayerController::AuraTestVictoryReturnM12()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	AAuraArchmage* Mage = nullptr;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		Mage = *It;
		break;
	}
	UAuraItemDefinition* StoneDefinition = UAuraItemCatalog::Get().FindItem(
		TEXT("Consumable.RespecStone"));
	if (!AuraPawn || !Inventory || !Mage || !StoneDefinition)
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_M12_VICTORY_RETURN setup failed"));
		return;
	}
	FAuraItemInstance Stone;
	Stone.Definition = StoneDefinition;
	Stone.Quantity = 1;
	Stone.EnsureIdentity();
	FString Failure;
	Inventory->TryAddItem(Stone, Failure, false);
	const int32 BeforeQuantity = Inventory->GetTotalQuantity(TEXT("Consumable.RespecStone"));
	bDemoCompleted = true;
	bVictoryOverlayVisible = true;
	SetIgnoreMoveInput(true);
	RestartDemo();
	const UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	const bool bPassed = BeforeQuantity > 0 && !bDemoCompleted
		&& !bVictoryOverlayVisible && Run && Run->IsAtPostBossCamp()
		&& FVector::DistSquared2D(AuraPawn->GetActorLocation(), Mage->GetActorLocation())
			< FMath::Square(Mage->GetInteractionRadius())
		&& Inventory->GetTotalQuantity(TEXT("Consumable.RespecStone")) == BeforeQuantity
		&& GetViewTarget() == AuraPawn && !IsMoveInputIgnored();
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_M12_VICTORY_RETURN Passed=%s InCamp=%s InventoryBefore=%d InventoryAfter=%d ViewTarget=%s MoveIgnored=%s"),
		bPassed ? TEXT("true") : TEXT("false"),
		Run && Run->IsAtPostBossCamp() ? TEXT("true") : TEXT("false"),
		BeforeQuantity, Inventory->GetTotalQuantity(TEXT("Consumable.RespecStone")),
		*GetNameSafe(GetViewTarget()), IsMoveInputIgnored() ? TEXT("true") : TEXT("false"));
}

void AAuraPlayerController::AuraTestRunUpgrades()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !AbilitySystem || !HasAuthority())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Run upgrade smoke test requires an authoritative Aura character with an ASC."));
		return;
	}
	if (!AuraCharacter->GetRunUpgradeState().IsEmpty())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Run upgrade smoke test requires a fresh character. ExistingRanks=%d"),
			AuraCharacter->GetRunUpgradeState().GetTotalRanks());
		return;
	}

	const UAuraCombatBalanceData& Balance = UAuraCombatBalanceData::Get();
	const float InitialMaxHealth = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	const float InitialMaxMana = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetMaxManaAttribute());
	bool bAllApplied = true;
	for (const EAuraRunUpgradeType Type : AuraRunUpgrade::GetAllTypes())
	{
		bAllApplied &= AuraCharacter->ApplyRunUpgrade(Type);
	}

	UAuraRunSubsystem* RunSubsystem = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	if (RunSubsystem)
	{
		RunSubsystem->SaveCheckpoint(GetWorld(), TEXT("RunUpgradeSmoke"),
			AuraCharacter->GetActorTransform(), AuraCharacter->GetCharacterLevel(),
			AuraCharacter->GetTotalExperience(), AuraCharacter->GetRunUpgradeState());
	}
	const bool bSecondFireRank = AuraCharacter->ApplyRunUpgrade(EAuraRunUpgradeType::FireBoltPower);
	const bool bThirdFireRank = AuraCharacter->ApplyRunUpgrade(EAuraRunUpgradeType::FireBoltPower);
	const bool bMaxRankBlocked = !AuraCharacter->ApplyRunUpgrade(EAuraRunUpgradeType::FireBoltPower);

	FTransform SavedTransform;
	FAuraRunUpgradeState SavedUpgrades;
	const bool bSnapshotValid = RunSubsystem
		&& RunSubsystem->TryGetCheckpointForWorld(GetWorld(), SavedTransform, nullptr, nullptr,
			&SavedUpgrades)
		&& SavedUpgrades == AuraCharacter->GetRunUpgradeState();
	if (RunSubsystem) RunSubsystem->ResetRun();

	const float MaxHealth = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	const float MaxMana = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetMaxManaAttribute());
	const float ProgressionMultiplier = 1.f
		+ FMath::Max(0, AuraCharacter->GetCharacterLevel() - 1)
			* Balance.Progression.AbilityDamageBonusPerLevel;
	const float ExpectedUniversalMultiplier = ProgressionMultiplier
		+ Balance.RunUpgrades.UniversalDamageBonusPerRank;
	const bool bValuesValid = FMath::IsNearlyEqual(
		MaxHealth, InitialMaxHealth + Balance.RunUpgrades.MaxHealthPerRank)
		&& FMath::IsNearlyEqual(
			MaxMana, InitialMaxMana + Balance.RunUpgrades.MaxManaPerRank)
		&& FMath::IsNearlyEqual(AuraCharacter->GetAbilityDamageMultiplier(),
			ExpectedUniversalMultiplier)
		&& FMath::IsNearlyEqual(AuraCharacter->GetFireBoltDamageMultiplier(),
			ExpectedUniversalMultiplier * (1.f
				+ 3.f * Balance.RunUpgrades.FireBoltDamageBonusPerRank))
		&& FMath::IsNearlyEqual(AuraCharacter->GetArcaneShardsDamageMultiplier(),
			ExpectedUniversalMultiplier * (1.f
				+ Balance.RunUpgrades.ArcaneShardsDamageBonusPerRank))
		&& FMath::IsNearlyEqual(AuraCharacter->GetArcaneShardsRadiusMultiplier(),
			1.f + Balance.RunUpgrades.ArcaneShardsRadiusBonusPerRank)
		&& FMath::IsNearlyEqual(AuraCharacter->GetShockDamageMultiplier(),
			ExpectedUniversalMultiplier * (1.f
				+ Balance.RunUpgrades.ShockDamageBonusPerRank))
		&& FMath::IsNearlyEqual(AuraCharacter->GetShockStunDurationBonus(),
			Balance.RunUpgrades.ShockStunBonusPerRank);
	const bool bRanksValid = AuraCharacter->GetRunUpgradeState().GetTotalRanks() == 8
		&& AuraCharacter->GetRunUpgradeRank(EAuraRunUpgradeType::FireBoltPower) == 3;
	const bool bPassed = bAllApplied && bSecondFireRank && bThirdFireRank
		&& bMaxRankBlocked && bRanksValid && bValuesValid && bSnapshotValid;
	UE_LOG(LogAuraInput, Display,
		TEXT("Run upgrade smoke test completed. Passed=%s Applied=%s MaxRankBlocked=%s Ranks=%d FireRank=%d MaxHealth=%.1f MaxMana=%.1f Universal=%.2f Fire=%.2f Shards=%.2f Radius=%.2f Shock=%.2f StunBonus=%.2f ManaRegen=Disabled Snapshot=%s"),
		bPassed ? TEXT("true") : TEXT("false"), bAllApplied ? TEXT("true") : TEXT("false"),
		bMaxRankBlocked ? TEXT("true") : TEXT("false"),
		AuraCharacter->GetRunUpgradeState().GetTotalRanks(),
		AuraCharacter->GetRunUpgradeRank(EAuraRunUpgradeType::FireBoltPower),
		MaxHealth, MaxMana, AuraCharacter->GetAbilityDamageMultiplier(),
		AuraCharacter->GetFireBoltDamageMultiplier(),
		AuraCharacter->GetArcaneShardsDamageMultiplier(),
		AuraCharacter->GetArcaneShardsRadiusMultiplier(),
		AuraCharacter->GetShockDamageMultiplier(),
		AuraCharacter->GetShockStunDurationBonus(),
		bSnapshotValid ? TEXT("true") : TEXT("false"));
}

void AAuraPlayerController::AuraTestDungeonFlow()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !HasAuthority())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Dungeon flow smoke test requires an authoritative Aura character."));
		return;
	}

	TArray<AActor*> EncounterActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEncounterVolume::StaticClass(), EncounterActors);
	TArray<AAuraEncounterVolume*> Encounters;
	for (AActor* Actor : EncounterActors)
	{
		if (AAuraEncounterVolume* Encounter = Cast<AAuraEncounterVolume>(Actor);
			Encounter && !Encounter->IsRewardEncounter())
		{
			Encounters.Add(Encounter);
		}
	}
	Encounters.Sort([](const AAuraEncounterVolume& Left, const AAuraEncounterVolume& Right)
	{
		return Left.GetEncounterOrder() < Right.GetEncounterOrder();
	});

	TArray<AActor*> CheckpointActors;
	UGameplayStatics::GetAllActorsOfClass(
		this, AAuraDungeonCheckpoint::StaticClass(), CheckpointActors);
	AAuraDungeonCheckpoint* Checkpoint = CheckpointActors.IsEmpty()
		? nullptr : Cast<AAuraDungeonCheckpoint>(CheckpointActors[0]);

	TArray<AActor*> GateActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraDungeonGate::StaticClass(), GateActors);
	const bool bLayoutValid = Encounters.Num() == 3 && Checkpoint && GateActors.Num() >= 3
		&& !Encounters[0]->IsFinalEncounter() && !Encounters[1]->IsFinalEncounter()
		&& Encounters[2]->IsFinalEncounter();
	if (!bLayoutValid)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Dungeon flow smoke test layout invalid. Encounters=%d Checkpoints=%d Gates=%d"),
			Encounters.Num(), CheckpointActors.Num(), GateActors.Num());
		return;
	}

	struct FDungeonFlowSmokeState
	{
		bool bFirstStarted = false;
		bool bFirstCompleted = false;
		bool bCheckpointActivated = false;
		bool bSecondStarted = false;
		bool bSecondCompleted = false;
		bool bBossStarted = false;
		bool bBossCompleted = false;
	};
	const TSharedRef<FDungeonFlowSmokeState> TestState = MakeShared<FDungeonFlowSmokeState>();
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraDungeonCheckpoint> WeakCheckpoint(Checkpoint);
	const TWeakObjectPtr<AAuraEncounterVolume> WeakFirst(Encounters[0]);
	const TWeakObjectPtr<AAuraEncounterVolume> WeakSecond(Encounters[1]);
	const TWeakObjectPtr<AAuraEncounterVolume> WeakBoss(Encounters[2]);

	Encounters[0]->StartEncounter(AuraCharacter, true);
	FTimerHandle FirstFightTimer;
	GetWorldTimerManager().SetTimer(FirstFightTimer, [WeakFirst, TestState]()
	{
		TestState->bFirstStarted = WeakFirst.IsValid()
			&& WeakFirst->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& WeakFirst->GetActiveEnemyCount() == 3;
		DefeatEncounterEnemiesForSmokeTest(WeakFirst.Get());
	}, 0.2f, false);

	FTimerHandle FirstResultTimer;
	GetWorldTimerManager().SetTimer(FirstResultTimer,
		[WeakFirst, WeakSecond, WeakCheckpoint, WeakCharacter, TestState]()
	{
		TestState->bFirstCompleted = WeakFirst.IsValid()
			&& WeakFirst->GetEncounterState() == EAuraEncounterState::Completed
			&& WeakFirst->AreControlledGatesOpen();
		if (WeakCheckpoint.IsValid() && WeakCharacter.IsValid())
		{
			WeakCheckpoint->ActivateCheckpoint(WeakCharacter.Get());
			const UAuraRunSubsystem* RunSubsystem =
				WeakCharacter->GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
			TestState->bCheckpointActivated = WeakCheckpoint->IsActivated()
				&& RunSubsystem && RunSubsystem->HasCheckpoint();
		}
		if (WeakSecond.IsValid() && WeakCharacter.IsValid())
		{
			WeakSecond->StartEncounter(WeakCharacter.Get(), true);
		}
	}, 0.45f, false);

	FTimerHandle SecondFightTimer;
	GetWorldTimerManager().SetTimer(SecondFightTimer, [WeakSecond, TestState]()
	{
		TestState->bSecondStarted = WeakSecond.IsValid()
			&& WeakSecond->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& WeakSecond->GetActiveEnemyCount() > 0;
		DefeatEncounterEnemiesForSmokeTest(WeakSecond.Get());
	}, 0.7f, false);

	FTimerHandle SecondResultTimer;
	GetWorldTimerManager().SetTimer(SecondResultTimer,
		[WeakSecond, WeakBoss, WeakCharacter, TestState]()
	{
		TestState->bSecondCompleted = WeakSecond.IsValid()
			&& WeakSecond->GetEncounterState() == EAuraEncounterState::Completed
			&& WeakSecond->AreControlledGatesOpen();
		if (WeakBoss.IsValid() && WeakCharacter.IsValid())
		{
			StartBossForLegacySmokeTest(WeakBoss.Get(), WeakCharacter.Get());
		}
	}, 0.95f, false);

	FTimerHandle BossFightTimer;
	GetWorldTimerManager().SetTimer(BossFightTimer, [WeakBoss, TestState]()
	{
		TestState->bBossStarted = WeakBoss.IsValid()
			&& WeakBoss->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& WeakBoss->GetActiveEnemyCount() == 1;
		DefeatEncounterEnemiesForSmokeTest(WeakBoss.Get());
	}, 1.2f, false);

	FTimerHandle BossSecondPhaseTimer;
	GetWorldTimerManager().SetTimer(BossSecondPhaseTimer, [WeakBoss]()
	{
		// The first lethal injection must stop at 50% and play the unskippable
		// phase break. This second injection verifies that phase two can then die.
		DefeatEncounterEnemiesForSmokeTest(WeakBoss.Get());
	}, 2.72f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[WeakController, WeakBoss, TestState, bLayoutValid]()
	{
		TestState->bBossCompleted = WeakBoss.IsValid()
			&& WeakBoss->GetEncounterState() == EAuraEncounterState::Completed
			&& WeakBoss->AreControlledGatesOpen();
		const bool bVictory = WeakController.IsValid() && WeakController->IsDemoCompleted();
		const bool bVictoryCinematic = WeakController.IsValid()
			&& WeakController->IsVictoryCinematicActive()
			&& WeakController->HasVictoryCamera()
			&& !WeakController->IsVictoryOverlayVisible();
		const bool bPassed = bLayoutValid && TestState->bFirstStarted
			&& TestState->bFirstCompleted && TestState->bCheckpointActivated
			&& TestState->bSecondStarted && TestState->bSecondCompleted
			&& TestState->bBossStarted && TestState->bBossCompleted
			&& bVictory && bVictoryCinematic;
		UE_LOG(LogAuraInput, Display,
			TEXT("Dungeon flow smoke test completed. Passed=%s Layout=%s FirstStarted=%s FirstCompleted=%s Checkpoint=%s SecondStarted=%s SecondCompleted=%s BossStarted=%s BossCompleted=%s Victory=%s Cinematic=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			bLayoutValid ? TEXT("true") : TEXT("false"),
			TestState->bFirstStarted ? TEXT("true") : TEXT("false"),
			TestState->bFirstCompleted ? TEXT("true") : TEXT("false"),
			TestState->bCheckpointActivated ? TEXT("true") : TEXT("false"),
			TestState->bSecondStarted ? TEXT("true") : TEXT("false"),
			TestState->bSecondCompleted ? TEXT("true") : TEXT("false"),
			TestState->bBossStarted ? TEXT("true") : TEXT("false"),
			TestState->bBossCompleted ? TEXT("true") : TEXT("false"),
			bVictory ? TEXT("true") : TEXT("false"),
			bVictoryCinematic ? TEXT("true") : TEXT("false"));
	}, 2.98f, false);
}

void AAuraPlayerController::AuraTestTreasureCaches()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !AbilitySystem || !HasAuthority())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Treasure cache smoke test requires an authoritative Aura character with an ASC."));
		return;
	}

	TArray<AActor*> TreasureActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraTreasureChest::StaticClass(), TreasureActors);
	TArray<AAuraTreasureChest*> Treasures;
	for (AActor* Actor : TreasureActors)
	{
		if (AAuraTreasureChest* Treasure = Cast<AAuraTreasureChest>(Actor))
		{
			Treasures.Add(Treasure);
		}
	}
	Treasures.Sort([](const AAuraTreasureChest& Left, const AAuraTreasureChest& Right)
	{
		if (!FMath::IsNearlyEqual(Left.GetActorLocation().Z, Right.GetActorLocation().Z))
		{
			return Left.GetActorLocation().Z < Right.GetActorLocation().Z;
		}
		return Left.GetActorLocation().Y > Right.GetActorLocation().Y;
	});

	TArray<AActor*> EncounterActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEncounterVolume::StaticClass(), EncounterActors);
	TArray<AAuraEncounterVolume*> RewardEncounters;
	for (AActor* Actor : EncounterActors)
	{
		if (AAuraEncounterVolume* Encounter = Cast<AAuraEncounterVolume>(Actor);
			Encounter && Encounter->IsRewardEncounter() && Encounter->GetRewardChest())
		{
			RewardEncounters.Add(Encounter);
		}
	}
	AAuraEncounterVolume* FirstRewardEncounter = nullptr;
	for (AAuraEncounterVolume* Encounter : RewardEncounters)
	{
		if (Encounter && Treasures.Num() > 0 && Encounter->GetRewardChest() == Treasures[0])
		{
			FirstRewardEncounter = Encounter;
			break;
		}
	}
	if (Treasures.Num() != 2 || RewardEncounters.Num() != 2 || !FirstRewardEncounter
		|| Treasures[0]->IsOpened() || Treasures[1]->IsOpened()
		|| !Treasures[0]->IsGuardLocked() || !Treasures[1]->IsGuardLocked())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Treasure cache smoke test expected two guarded caches linked to two reward encounters. Treasures=%d RewardEncounters=%d FirstEncounter=%s FirstLocked=%s SecondLocked=%s"),
			Treasures.Num(), RewardEncounters.Num(), *GetNameSafe(FirstRewardEncounter),
			Treasures.Num() > 0 && Treasures[0]->IsGuardLocked() ? TEXT("true") : TEXT("false"),
			Treasures.Num() > 1 && Treasures[1]->IsGuardLocked() ? TEXT("true") : TEXT("false"));
		return;
	}

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	for (AActor* EnemyActor : Enemies)
	{
		if (AAuraEnemy* Enemy = Cast<AAuraEnemy>(EnemyActor))
		{
			Enemy->GetCharacterMovement()->DisableMovement();
			if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
			{
				AIController->StopMovement();
				AIController->SetActorTickEnabled(false);
			}
		}
	}

	const FVector OriginalLocation = AuraCharacter->GetActorLocation();
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 40.f);
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 10.f);
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();

	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraTreasureChest> WeakFirstTreasure(Treasures[0]);
	const TWeakObjectPtr<AAuraTreasureChest> WeakSecondTreasure(Treasures[1]);
	const TSharedRef<FVector2D> FirstRewardValues = MakeShared<FVector2D>(-1.f, -1.f);
	const TSharedRef<bool> GuardBlockPassed = MakeShared<bool>(false);
	const TSharedRef<bool> EncounterStartedPassed = MakeShared<bool>(false);
	const TSharedRef<bool> EncounterClearedPassed = MakeShared<bool>(false);
	const TSharedRef<bool> UnlockPassed = MakeShared<bool>(false);
	const TSharedRef<bool> NoUpgradeSelectionPassed = MakeShared<bool>(false);
	const TSharedRef<bool> ItemDropsPassed = MakeShared<bool>(false);
	const int32 InitialRunUpgradeRanks = AuraCharacter->GetRunUpgradeState().GetTotalRanks();

	FVector TreasureLocation = Treasures[0]->GetActorLocation();
	TreasureLocation.Z += 96.f;
	AuraCharacter->SetActorLocation(TreasureLocation, false, nullptr, ETeleportType::TeleportPhysics);
	AuraCharacter->GetCapsuleComponent()->UpdateOverlaps();
	*GuardBlockPassed = Treasures[0]->IsGuardLocked() && !Treasures[0]->IsOpened()
		&& !bIsRunUpgradeSelectionActive
		&& Treasures[0]->GetRemainingGuardCount() == FirstRewardEncounter->GetConfiguredSpawnCount();

	AuraCharacter->SetActorLocation(
		OriginalLocation, false, nullptr, ETeleportType::TeleportPhysics);
	AuraCharacter->GetCapsuleComponent()->UpdateOverlaps();
	FVector EncounterTriggerLocation = FirstRewardEncounter->GetActorLocation();
	EncounterTriggerLocation.Z += 96.f;
	FirstRewardEncounter->StartEncounter(AuraCharacter, true);
	AuraCharacter->SetActorLocation(
		EncounterTriggerLocation, false, nullptr, ETeleportType::TeleportPhysics);
	AuraCharacter->GetCapsuleComponent()->UpdateOverlaps();
	*EncounterStartedPassed = FirstRewardEncounter->GetEncounterState()
		== EAuraEncounterState::LockedCombat
		&& FirstRewardEncounter->GetActiveEnemyCount() == FirstRewardEncounter->GetConfiguredSpawnCount()
		&& Treasures[0]->IsGuardLocked()
		&& Treasures[0]->GetRemainingGuardCount() == FirstRewardEncounter->GetActiveEnemyCount();
	DefeatEncounterEnemiesForSmokeTest(FirstRewardEncounter);
	const UAuraRunSubsystem* RunSubsystem =
		GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	*EncounterClearedPassed = FirstRewardEncounter->GetEncounterState()
			== EAuraEncounterState::RewardAvailable
		&& FirstRewardEncounter->GetActiveEnemyCount() == 0
		&& RunSubsystem
		&& RunSubsystem->IsRewardEncounterCleared(FirstRewardEncounter->GetEncounterId());
	*UnlockPassed = !Treasures[0]->IsGuardLocked() && !Treasures[0]->IsOpened();

	// Treasure is item-only: it must neither heal nor open the obsolete boon UI.
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 40.f);
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 10.f);
	UAuraLootSubsystem* LootSubsystem = GetWorld()->GetSubsystem<UAuraLootSubsystem>();
	const AAuraPlayerState* AuraState = AuraCharacter->GetPlayerState<AAuraPlayerState>();
	const int32 DropsBefore = LootSubsystem && AuraState
		? LootSubsystem->GetActivePersonalDropCount(AuraState) : -1;
	AuraCharacter->SetActorLocation(TreasureLocation, false, nullptr, ETeleportType::TeleportPhysics);
	AuraCharacter->GetCapsuleComponent()->UpdateOverlaps();
	TryInteractWithRewardChest();
	*NoUpgradeSelectionPassed = !bIsRunUpgradeSelectionActive
		&& AuraCharacter->GetRunUpgradeState().GetTotalRanks() == InitialRunUpgradeRanks;
	*ItemDropsPassed = LootSubsystem && AuraState
		&& LootSubsystem->GetActivePersonalDropCount(AuraState) > DropsBefore;
	FirstRewardValues->X = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	FirstRewardValues->Y = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetManaAttribute());

	FTimerHandle FirstResultTimer;
	GetWorldTimerManager().SetTimer(FirstResultTimer,
		[WeakCharacter, WeakFirstTreasure, OriginalLocation]()
	{
		if (!WeakCharacter.IsValid() || !WeakFirstTreasure.IsValid()) return;
		WeakCharacter->SetActorLocation(
			OriginalLocation, false, nullptr, ETeleportType::TeleportPhysics);
		WeakCharacter->GetCapsuleComponent()->UpdateOverlaps();
	}, 1.5f, false);

	FTimerHandle RepeatOverlapTimer;
	GetWorldTimerManager().SetTimer(RepeatOverlapTimer,
		[WeakCharacter, WeakFirstTreasure]()
	{
		if (!WeakCharacter.IsValid() || !WeakFirstTreasure.IsValid()) return;
		FVector Location = WeakFirstTreasure->GetActorLocation();
		Location.Z += 96.f;
		WeakCharacter->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		WeakCharacter->GetCapsuleComponent()->UpdateOverlaps();
	}, 1.65f, false);

	FTimerHandle FinalResultTimer;
	GetWorldTimerManager().SetTimer(FinalResultTimer,
		[WeakCharacter, WeakController, WeakFirstTreasure, WeakSecondTreasure,
			FirstRewardValues, GuardBlockPassed, EncounterStartedPassed,
			EncounterClearedPassed, UnlockPassed, NoUpgradeSelectionPassed,
			ItemDropsPassed, InitialRunUpgradeRanks]()
	{
		if (!WeakCharacter.IsValid() || !WeakFirstTreasure.IsValid()
			|| !WeakSecondTreasure.IsValid() || !WeakController.IsValid()) return;
		const UAbilitySystemComponent* CurrentAbilitySystem =
			WeakCharacter->GetAbilitySystemComponent();
		const float Health = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		const float Mana = CurrentAbilitySystem
			? CurrentAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		const bool bRewardCorrect = FMath::IsNearlyEqual(FirstRewardValues->X, 40.f, 0.1f)
			&& FMath::IsNearlyEqual(FirstRewardValues->Y, 10.f, 0.1f);
		const bool bNoNewBoon = WeakCharacter->GetRunUpgradeState().GetTotalRanks()
			== InitialRunUpgradeRanks;
		const bool bStateCorrect = WeakFirstTreasure->IsOpened()
			&& !WeakSecondTreasure->IsOpened()
			&& WeakSecondTreasure->IsGuardLocked()
			&& !WeakController->IsRunUpgradeSelectionActive();
		const bool bPassed = bRewardCorrect && bNoNewBoon && bStateCorrect
			&& *GuardBlockPassed && *EncounterStartedPassed
			&& *EncounterClearedPassed && *UnlockPassed && *NoUpgradeSelectionPassed
			&& *ItemDropsPassed;
		UE_LOG(LogAuraInput, Display,
			TEXT("Treasure cache smoke test completed. Passed=%s GuardBlocked=%s EncounterStarted=%s EncounterCleared=%s Unlocked=%s NoRecovery=%s NoUpgradeUI=%s ItemDrops=%s NoNewBoon=%s State=%s Health=%.1f Mana=%.1f Boons=%d FirstOpened=%s SecondOpened=%s SecondLocked=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			*GuardBlockPassed ? TEXT("true") : TEXT("false"),
			*EncounterStartedPassed ? TEXT("true") : TEXT("false"),
			*EncounterClearedPassed ? TEXT("true") : TEXT("false"),
			*UnlockPassed ? TEXT("true") : TEXT("false"),
			bRewardCorrect ? TEXT("true") : TEXT("false"),
			*NoUpgradeSelectionPassed ? TEXT("true") : TEXT("false"),
			*ItemDropsPassed ? TEXT("true") : TEXT("false"),
			bNoNewBoon ? TEXT("true") : TEXT("false"),
			bStateCorrect ? TEXT("true") : TEXT("false"),
			Health, Mana, WeakCharacter->GetRunUpgradeState().GetTotalRanks(),
			WeakFirstTreasure->IsOpened() ? TEXT("true") : TEXT("false"),
			WeakSecondTreasure->IsOpened() ? TEXT("true") : TEXT("false"),
			WeakSecondTreasure->IsGuardLocked() ? TEXT("true") : TEXT("false"));
	}, 1.9f, false);
}

void AAuraPlayerController::AuraTestRoomAmbush()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!HasAuthority() || !AuraPawn || !Quest) return;
	AAuraEncounterVolume* South = nullptr;
	AAuraEncounterVolume* North = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterId() == FName(TEXT("LowerSouthLootAmbush"))) South = *It;
		if (It->GetEncounterId() == FName(TEXT("UpperNorthRiskGuardians"))) North = *It;
	}
	if (!South || !North || !South->GetLootChest())
	{
		UE_LOG(LogAuraInput, Error, TEXT("Ambush room smoke test completed. Passed=false Setup=false"));
		return;
	}
	const bool bGrounded = FMath::IsNearlyEqual(South->GetRuntimeFloorZ(), 850.f, 5.f)
		&& FMath::IsNearlyEqual(North->GetRuntimeFloorZ(), 850.f, 5.f);
	bool bStanding = South->GetActiveEnemyCount() == 2
		&& North->GetActiveEnemyCount() == 2
		&& South->GetPreplacedGuardCount() == 2
		&& North->GetPreplacedGuardCount() == 2;
	for (AAuraEnemy* Enemy : South->GetActiveEnemies())
	{
		bStanding &= Enemy && Enemy->IsEncounterDormant()
			&& !Enemy->IsSpawnPresentationActive();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
			AI->SetActorTickEnabled(false);
	}
	for (AAuraEnemy* Enemy : North->GetActiveEnemies())
	{
		bStanding &= Enemy && Enemy->IsEncounterDormant()
			&& !Enemy->IsSpawnPresentationActive();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
			AI->SetActorTickEnabled(false);
	}
	auto EnterRoom = [AuraPawn](AAuraEncounterVolume* Room)
	{
		FVector Entry = Room->GetActorTransform().TransformPosition(
			FVector(Room->GetArmingEntryPlaneLocalX() + 80.f, 0.f, 0.f));
		Entry.Z = Room->GetRuntimeFloorZ() + 96.f;
		AuraPawn->SetActorLocation(Entry, false, nullptr, ETeleportType::TeleportPhysics);
		AuraPawn->GetCapsuleComponent()->UpdateOverlaps();
	};
	EnterRoom(South);
	const bool bEntryArmed = South->GetEncounterState() == EAuraEncounterState::Arming;
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakPlayer(AuraPawn);
	const TWeakObjectPtr<AAuraEncounterVolume> WeakSouth(South);
	const TWeakObjectPtr<AAuraEncounterVolume> WeakNorth(North);
	FTimerHandle SouthTimer;
	GetWorldTimerManager().SetTimer(SouthTimer,
		[WeakController, WeakPlayer, WeakSouth, WeakNorth, bGrounded, bStanding, bEntryArmed]()
	{
		if (!WeakController.IsValid() || !WeakPlayer.IsValid()
			|| !WeakSouth.IsValid() || !WeakNorth.IsValid()) return;
		AAuraPlayerController* Controller = WeakController.Get();
		AAuraEncounterVolume* SouthRoom = WeakSouth.Get();
		AAuraEncounterVolume* NorthRoom = WeakNorth.Get();
		AAuraCharacter* Character = WeakPlayer.Get();
		const bool bSouthLocked = SouthRoom->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& SouthRoom->GetActiveEnemyCount() == 2;
		DefeatEncounterEnemiesForSmokeTest(SouthRoom);
		AAuraGameState* QuestState = Controller->GetWorld()->GetGameState<AAuraGameState>();
		const bool bAwaitingChest = SouthRoom->GetEncounterState() == EAuraEncounterState::RewardAvailable
			&& QuestState && !QuestState->IsRewardEncounterCleared(SouthRoom->GetEncounterId());
		FVector ChestLocation = SouthRoom->GetLootChest()->GetActorLocation();
		ChestLocation.Z += 96.f;
		Character->SetActorLocation(ChestLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCapsuleComponent()->UpdateOverlaps();
		const bool bClaimed = SouthRoom->GetLootChest()->ClaimForCharacter(Character);
		const bool bChestTriggered = bClaimed
			&& SouthRoom->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& SouthRoom->GetRemainingEnemyCount() == 2;
		FTimerHandle AmbushTimer;
		Controller->GetWorldTimerManager().SetTimer(AmbushTimer,
			[WeakController, WeakPlayer, WeakSouth, WeakNorth, bGrounded, bStanding,
				bEntryArmed, bSouthLocked, bAwaitingChest, bChestTriggered]()
		{
			if (!WeakController.IsValid() || !WeakPlayer.IsValid()
				|| !WeakSouth.IsValid() || !WeakNorth.IsValid()) return;
			AAuraPlayerController* Controller = WeakController.Get();
			AAuraEncounterVolume* SouthRoom = WeakSouth.Get();
			AAuraEncounterVolume* NorthRoom = WeakNorth.Get();
			AAuraCharacter* Character = WeakPlayer.Get();
			const bool bChestWave = SouthRoom->GetActiveEnemyCount() == 2;
			DefeatEncounterEnemiesForSmokeTest(SouthRoom);
			const AAuraGameState* QuestState = Controller->GetWorld()->GetGameState<AAuraGameState>();
			const bool bSouthCompleted = SouthRoom->GetEncounterState() == EAuraEncounterState::Completed
				&& QuestState && QuestState->IsRewardEncounterCleared(SouthRoom->GetEncounterId());
			FVector Entry = NorthRoom->GetActorTransform().TransformPosition(
				FVector(NorthRoom->GetArmingEntryPlaneLocalX() + 80.f, 0.f, 0.f));
			Entry.Z = NorthRoom->GetRuntimeFloorZ() + 96.f;
			Character->SetActorLocation(Entry, false, nullptr, ETeleportType::TeleportPhysics);
			Character->GetCapsuleComponent()->UpdateOverlaps();
			const bool bNorthArmed = NorthRoom->GetEncounterState() == EAuraEncounterState::Arming;
			FTimerHandle NorthTimer;
			Controller->GetWorldTimerManager().SetTimer(NorthTimer,
				[WeakController, WeakNorth, bGrounded, bStanding, bEntryArmed,
					bSouthLocked, bAwaitingChest, bChestTriggered, bChestWave,
					bSouthCompleted, bNorthArmed]()
			{
				if (!WeakController.IsValid() || !WeakNorth.IsValid()) return;
				AAuraPlayerController* Controller = WeakController.Get();
				AAuraEncounterVolume* NorthRoom = WeakNorth.Get();
				const bool bNorthLocked = NorthRoom->GetEncounterState() == EAuraEncounterState::LockedCombat
					&& NorthRoom->GetActiveEnemyCount() == 2
					&& NorthRoom->GetRemainingEnemyCount() == 4;
				DefeatEncounterEnemiesForSmokeTest(NorthRoom);
				FTimerHandle ReinforcementTimer;
				Controller->GetWorldTimerManager().SetTimer(ReinforcementTimer,
					[WeakController, WeakNorth, bGrounded, bStanding, bEntryArmed,
						bSouthLocked, bAwaitingChest, bChestTriggered, bChestWave,
						bSouthCompleted, bNorthArmed, bNorthLocked]()
				{
					if (!WeakController.IsValid() || !WeakNorth.IsValid()) return;
					AAuraEncounterVolume* Room = WeakNorth.Get();
					const bool bKillWave = Room->GetActiveEnemyCount() == 2
						&& Room->GetRemainingEnemyCount() == 2;
					DefeatEncounterEnemiesForSmokeTest(Room);
					const bool bNorthCompleted = Room->GetEncounterState()
						== EAuraEncounterState::RewardAvailable;
					const bool bPassed = bGrounded && bStanding && bEntryArmed
						&& bSouthLocked && bAwaitingChest && bChestTriggered
						&& bChestWave && bSouthCompleted && bNorthArmed
						&& bNorthLocked && bKillWave && bNorthCompleted;
					UE_LOG(LogAuraInput, Display,
						TEXT("Ambush room smoke test completed. Passed=%s Grounded=%s Standing=%s SouthEntry=%s SouthLocked=%s ChestReady=%s ChestTriggered=%s ChestWave=%s SouthCompleted=%s NorthEntry=%s NorthLocked=%s KillWave=%s NorthCleared=%s"),
						bPassed ? TEXT("true") : TEXT("false"),
						bGrounded ? TEXT("true") : TEXT("false"),
						bStanding ? TEXT("true") : TEXT("false"),
						bEntryArmed ? TEXT("true") : TEXT("false"),
						bSouthLocked ? TEXT("true") : TEXT("false"),
						bAwaitingChest ? TEXT("true") : TEXT("false"),
						bChestTriggered ? TEXT("true") : TEXT("false"),
						bChestWave ? TEXT("true") : TEXT("false"),
						bSouthCompleted ? TEXT("true") : TEXT("false"),
						bNorthArmed ? TEXT("true") : TEXT("false"),
						bNorthLocked ? TEXT("true") : TEXT("false"),
						bKillWave ? TEXT("true") : TEXT("false"),
						bNorthCompleted ? TEXT("true") : TEXT("false"));
				}, 2.1f, false);
			}, 1.3f, false);
		}, 0.9f, false);
	}, 1.3f, false);
}

void AAuraPlayerController::AuraTestGateFade()
{
	if (!HasAuthority() || !GetWorld()) return;
	AAuraDungeonGate* Gate = nullptr;
	for (TActorIterator<AAuraDungeonGate> It(GetWorld()); It; ++It)
	{
		Gate = *It;
		break;
	}
	UStaticMeshComponent* Visual = Gate ? Gate->GetGateVisual() : nullptr;
	UMaterialInstanceDynamic* Fade = Visual
		? Cast<UMaterialInstanceDynamic>(Visual->GetMaterial(0)) : nullptr;
	if (!Gate || !Visual || !Fade)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Gate fade smoke test completed. Passed=false Setup=false Gate=%s Fade=%s"),
			*GetNameSafe(Gate), *GetNameSafe(Fade));
		return;
	}
	const bool bOriginallyOpen = Gate->IsOpen();
	Gate->SetGateOpen(true, true);
	const float OpenZ = Visual->GetRelativeLocation().Z;
	const bool bTransparent = Fade->K2_GetScalarParameterValue(TEXT("GateOpacity")) < 0.01f
		&& !Visual->IsVisible();
	Gate->SetGateOpen(false);
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraDungeonGate> WeakGate(Gate);
	FTimerHandle MidTimer;
	GetWorldTimerManager().SetTimer(MidTimer,
		[WeakController, WeakGate, OpenZ, bOriginallyOpen, bTransparent]()
	{
		if (!WeakController.IsValid() || !WeakGate.IsValid()) return;
		AAuraDungeonGate* CurrentGate = WeakGate.Get();
		UStaticMeshComponent* CurrentVisual = CurrentGate->GetGateVisual();
		UMaterialInstanceDynamic* CurrentFade = CurrentVisual
			? Cast<UMaterialInstanceDynamic>(CurrentVisual->GetMaterial(0)) : nullptr;
		const float MidOpacity = CurrentFade
			? CurrentFade->K2_GetScalarParameterValue(TEXT("GateOpacity")) : -1.f;
		const bool bFadingWhileFalling = CurrentVisual && CurrentVisual->IsVisible()
			&& MidOpacity > 0.02f && MidOpacity < 0.95f
			&& CurrentVisual->GetRelativeLocation().Z < OpenZ;
		FTimerHandle ClosedTimer;
		WeakController->GetWorldTimerManager().SetTimer(ClosedTimer,
			[WeakGate, bOriginallyOpen, bTransparent, bFadingWhileFalling, MidOpacity]()
		{
			if (!WeakGate.IsValid()) return;
			AAuraDungeonGate* CurrentGate = WeakGate.Get();
			UStaticMeshComponent* CurrentVisual = CurrentGate->GetGateVisual();
			UMaterialInstanceDynamic* CurrentFade = CurrentVisual
				? Cast<UMaterialInstanceDynamic>(CurrentVisual->GetMaterial(0)) : nullptr;
			const float ClosedOpacity = CurrentFade
				? CurrentFade->K2_GetScalarParameterValue(TEXT("GateOpacity")) : -1.f;
			const bool bSolid = !CurrentGate->IsOpen()
				&& CurrentVisual && CurrentVisual->IsVisible()
				&& ClosedOpacity > 0.98f;
			CurrentGate->SetCameraOccluded(true);
			const float OccludedOpacity = CurrentFade
				? CurrentFade->K2_GetScalarParameterValue(TEXT("GateOpacity")) : -1.f;
			const bool bOcclusionFades = CurrentFade && CurrentVisual->IsVisible()
				&& FMath::IsNearlyEqual(OccludedOpacity, 0.5f, 0.02f);
			CurrentGate->SetCameraOccluded(false);
			const bool bOcclusionRestores = CurrentFade
				&& CurrentFade->K2_GetScalarParameterValue(TEXT("GateOpacity")) > 0.98f
				&& CurrentVisual->IsVisible();
			CurrentGate->SetGateOpen(bOriginallyOpen, true);
			UE_LOG(LogAuraInput, Display,
				TEXT("Gate fade smoke test completed. Passed=%s Transparent=%s FallingFade=%s Solid=%s CameraFades=%s CameraRestores=%s MidOpacity=%.3f ClosedOpacity=%.3f"),
				bTransparent && bFadingWhileFalling && bSolid
					&& bOcclusionFades && bOcclusionRestores ? TEXT("true") : TEXT("false"),
				bTransparent ? TEXT("true") : TEXT("false"),
				bFadingWhileFalling ? TEXT("true") : TEXT("false"),
				bSolid ? TEXT("true") : TEXT("false"),
				bOcclusionFades ? TEXT("true") : TEXT("false"),
				bOcclusionRestores ? TEXT("true") : TEXT("false"),
				MidOpacity, ClosedOpacity);
		}, 2.f, false);
	}, 0.23f, false);
}

void AAuraPlayerController::AuraCaptureDungeonTour()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !GetWorld()) return;

	TArray<AActor*> EncounterActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEncounterVolume::StaticClass(), EncounterActors);
	TArray<AAuraEncounterVolume*> Encounters;
	for (AActor* Actor : EncounterActors)
	{
		if (AAuraEncounterVolume* Encounter = Cast<AAuraEncounterVolume>(Actor);
			Encounter && !Encounter->IsRewardEncounter())
		{
			Encounters.Add(Encounter);
		}
	}
	Encounters.Sort([](const AAuraEncounterVolume& Left, const AAuraEncounterVolume& Right)
	{
		return Left.GetEncounterOrder() < Right.GetEncounterOrder();
	});
	if (Encounters.Num() != 3)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Dungeon tour capture requires exactly three encounters."));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	SetControlRotation(FRotator(-60.f, 0.f, 0.f));
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	TArray<TWeakObjectPtr<AAuraEncounterVolume>> WeakEncounters;
	for (AAuraEncounterVolume* Encounter : Encounters) WeakEncounters.Add(Encounter);

	const FString ScreenshotDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
	IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);

	AuraCharacter->TeleportTo(FVector(1700.f, 0.f, 96.f), FRotator::ZeroRotator, false, true);
	Encounters[0]->StartEncounter(AuraCharacter, true);
	FTimerHandle OuterShotTimer;
	GetWorldTimerManager().SetTimer(OuterShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(ScreenshotDirectory, TEXT("DungeonTour_01_OuterGuard.png")), false, false);
	}, 0.8f, false);

	FTimerHandle StairSetupTimer;
	GetWorldTimerManager().SetTimer(StairSetupTimer,
		[WeakController, WeakCharacter]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()) return;
		TArray<AActor*> GateActors;
		UGameplayStatics::GetAllActorsOfClass(
			WeakController.Get(), AAuraDungeonGate::StaticClass(), GateActors);
		for (AActor* GateActor : GateActors)
		{
			if (AAuraDungeonGate* Gate = Cast<AAuraDungeonGate>(GateActor))
			{
				Gate->SetGateOpen(true, true);
			}
		}
		WeakCharacter->TeleportTo(FVector(3350.f, 0.f, 96.f), FRotator::ZeroRotator, false, true);
		WeakController->SetAuraCameraMode(EAuraCameraMode::TopDown);
		WeakController->SetControlRotation(FRotator(-12.f, 0.f, 0.f));
	}, 1.1f, false);
	FTimerHandle StairShotTimer;
	GetWorldTimerManager().SetTimer(StairShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(ScreenshotDirectory, TEXT("DungeonTour_00_StairAscent.png")), false, false);
	}, 1.9f, false);

	FTimerHandle EliteSetupTimer;
	GetWorldTimerManager().SetTimer(EliteSetupTimer,
		[WeakController, WeakCharacter, WeakEncounters]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()
			|| WeakEncounters.Num() < 2 || !WeakEncounters[1].IsValid()) return;
		WeakController->SetAuraCameraMode(EAuraCameraMode::TopDown);
		WeakController->SetControlRotation(FRotator(-60.f, 0.f, 0.f));
		WeakEncounters[1]->StartEncounter(WeakCharacter.Get(), true);
		WeakCharacter->TeleportTo(FVector(5350.f, 0.f, 696.f), FRotator::ZeroRotator, false, true);
	}, 2.15f, false);
	FTimerHandle EliteShotTimer;
	GetWorldTimerManager().SetTimer(EliteShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(ScreenshotDirectory, TEXT("DungeonTour_02_EliteShaman.png")), false, false);
	}, 2.95f, false);

	FTimerHandle BossSetupTimer;
	GetWorldTimerManager().SetTimer(BossSetupTimer,
		[WeakController, WeakCharacter, WeakEncounters]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()
			|| WeakEncounters.Num() < 3 || !WeakEncounters[2].IsValid()) return;
		WeakController->SetAuraCameraMode(EAuraCameraMode::TopDown);
		WeakController->SetControlRotation(FRotator(-60.f, 90.f, 0.f));
		StartBossForLegacySmokeTest(WeakEncounters[2].Get(), WeakCharacter.Get());
		WeakCharacter->TeleportTo(FVector(12300.f, 3500.f, 1296.f), FRotator(0.f, 90.f, 0.f), false, true);
	}, 3.3f, false);
	FTimerHandle BossShotTimer;
	GetWorldTimerManager().SetTimer(BossShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(ScreenshotDirectory, TEXT("DungeonTour_03_DemonLord.png")), false, false);
	}, 4.1f, false);

	FTimerHandle GodViewSetupTimer;
	GetWorldTimerManager().SetTimer(GodViewSetupTimer,
		[WeakController, WeakCharacter]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()) return;
		WeakCharacter->TeleportTo(FVector(12300.f, 3500.f, 1296.f), FRotator(0.f, 90.f, 0.f), false, true);
		WeakController->SetAuraCameraMode(EAuraCameraMode::TopDown);
		WeakController->SetControlRotation(FRotator(-12.f, 90.f, 0.f));
	}, 4.4f, false);
	FTimerHandle GodViewShotTimer;
	GetWorldTimerManager().SetTimer(GodViewShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(ScreenshotDirectory, TEXT("DungeonTour_04_GodViewBoss.png")), false, false);
	}, 5.2f, false);

	const auto CaptureM20 = [this, WeakController, WeakCharacter, ScreenshotDirectory](
		const FVector& Location, const TCHAR* Filename, const float SetupTime)
	{
		FTimerHandle SetupTimer;
		GetWorldTimerManager().SetTimer(SetupTimer,
			[WeakController, WeakCharacter, Location]()
		{
			if (!WeakController.IsValid() || !WeakCharacter.IsValid()) return;
			WeakController->SetAuraCameraMode(EAuraCameraMode::TopDown);
			WeakController->SetControlRotation(FRotator(-60.f, 0.f, 0.f));
			WeakCharacter->TeleportTo(Location, FRotator::ZeroRotator, false, true);
		}, SetupTime, false);
		FTimerHandle ShotTimer;
		GetWorldTimerManager().SetTimer(ShotTimer,
			[ScreenshotDirectory, Filename]()
		{
			FScreenshotRequest::RequestScreenshot(
				FPaths::Combine(ScreenshotDirectory, Filename), false, false);
		}, SetupTime + 0.65f, false);
	};
	CaptureM20(FVector(7700.f, 0.f, 696.f), TEXT("DungeonTour_05_M20_DeepStair.png"), 5.55f);
	CaptureM20(FVector(9000.f, 0.f, 1296.f), TEXT("DungeonTour_06_M20_DeepHub.png"), 6.55f);
	CaptureM20(FVector(9900.f, 2200.f, 1296.f), TEXT("DungeonTour_07_M20_DeepNorth.png"), 7.55f);
	CaptureM20(FVector(11100.f, -2200.f, 1296.f), TEXT("DungeonTour_08_M20_DeepSouth.png"), 8.55f);
	CaptureM20(FVector(12300.f, 1650.f, 1296.f), TEXT("DungeonTour_09_M20_CoreApproach.png"), 9.55f);

	FTimerHandle CaptureResultTimer;
	GetWorldTimerManager().SetTimer(CaptureResultTimer,
		[WeakController, ScreenshotDirectory]()
	{
		UE_LOG(LogAuraInput, Display,
			TEXT("Dungeon tour capture completed. ControllerValid=%s Directory=%s"),
			WeakController.IsValid() ? TEXT("true") : TEXT("false"), *ScreenshotDirectory);
	}, 10.6f, false);
}

void AAuraPlayerController::AuraCaptureDungeonBranches()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !GetWorld()) return;

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	for (AActor* EnemyActor : Enemies)
	{
		if (AAuraEnemy* Enemy = Cast<AAuraEnemy>(EnemyActor))
		{
			Enemy->GetCharacterMovement()->DisableMovement();
			if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
			{
				AIController->StopMovement();
				AIController->SetActorTickEnabled(false);
			}
		}
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	SetControlRotation(FRotator(-60.f, 0.f, 0.f));
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const FString ScreenshotDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
	IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);

	AuraCharacter->TeleportTo(FVector(1800.f, 0.f, 96.f), FRotator::ZeroRotator, false, true);
	FTimerHandle JunctionShotTimer;
	GetWorldTimerManager().SetTimer(JunctionShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("DungeonBranch_00_LowerJunction.png")), false, false);
	}, 0.7f, false);

	FTimerHandle NorthSetupTimer;
	GetWorldTimerManager().SetTimer(NorthSetupTimer, [WeakController, WeakCharacter]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()) return;
		WeakController->SetControlRotation(FRotator(-60.f, 90.f, 0.f));
		WeakCharacter->TeleportTo(FVector(2300.f, 2500.f, 96.f),
			FRotator(0.f, 90.f, 0.f), false, true);
	}, 1.0f, false);
	FTimerHandle NorthShotTimer;
	GetWorldTimerManager().SetTimer(NorthShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("DungeonBranch_01_NorthTreasure.png")), false, false);
	}, 1.7f, false);

	FTimerHandle SouthSetupTimer;
	GetWorldTimerManager().SetTimer(SouthSetupTimer, [WeakController, WeakCharacter]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()) return;
		WeakController->SetControlRotation(FRotator(-60.f, -90.f, 0.f));
		WeakCharacter->TeleportTo(FVector(1700.f, -2500.f, 96.f),
			FRotator(0.f, -90.f, 0.f), false, true);
	}, 2.0f, false);
	FTimerHandle SouthShotTimer;
	GetWorldTimerManager().SetTimer(SouthShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("DungeonBranch_02_SouthRecovery.png")), false, false);
	}, 2.7f, false);

	FTimerHandle UpperSetupTimer;
	GetWorldTimerManager().SetTimer(UpperSetupTimer, [WeakController, WeakCharacter]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()) return;
		WeakController->SetControlRotation(FRotator(-60.f, -90.f, 0.f));
		WeakCharacter->TeleportTo(FVector(5600.f, -2500.f, 696.f),
			FRotator(0.f, -90.f, 0.f), false, true);
	}, 3.0f, false);
	FTimerHandle UpperShotTimer;
	GetWorldTimerManager().SetTimer(UpperShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("DungeonBranch_03_UpperTreasure.png")), false, false);
	}, 3.7f, false);

	FTimerHandle BossRouteSetupTimer;
	GetWorldTimerManager().SetTimer(BossRouteSetupTimer, [WeakController, WeakCharacter]()
	{
		if (!WeakController.IsValid() || !WeakCharacter.IsValid()) return;
		WeakController->SetControlRotation(FRotator(-60.f, 90.f, 0.f));
		WeakCharacter->TeleportTo(FVector(12300.f, 1200.f, 1296.f),
			FRotator(0.f, 90.f, 0.f), false, true);
	}, 4.0f, false);
	FTimerHandle BossRouteShotTimer;
	GetWorldTimerManager().SetTimer(BossRouteShotTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("DungeonBranch_04_BossRoute.png")), false, false);
	}, 4.7f, false);

	FTimerHandle CaptureResultTimer;
	GetWorldTimerManager().SetTimer(CaptureResultTimer,
		[WeakController, ScreenshotDirectory]()
	{
		UE_LOG(LogAuraInput, Display,
			TEXT("Dungeon branch capture completed. ControllerValid=%s Directory=%s"),
			WeakController.IsValid() ? TEXT("true") : TEXT("false"), *ScreenshotDirectory);
	}, 5.1f, false);
}

void AAuraPlayerController::AuraTestCheckpointReload()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !GetWorld())
	{
		UE_LOG(LogAuraInput, Error, TEXT("Checkpoint reload smoke test requires a possessed Aura character."));
		return;
	}

	TArray<AActor*> CheckpointActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraDungeonCheckpoint::StaticClass(), CheckpointActors);
	AAuraDungeonCheckpoint* Checkpoint = CheckpointActors.Num() == 1
		? Cast<AAuraDungeonCheckpoint>(CheckpointActors[0]) : nullptr;
	if (!Checkpoint)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Checkpoint reload smoke test expected one checkpoint. Found=%d"), CheckpointActors.Num());
		return;
	}

	const int32 LevelTwoThreshold = UAuraCombatBalanceData::Get().Progression.BaseExperienceToNextLevel;
	if (AuraCharacter->GetTotalExperience() < LevelTwoThreshold)
	{
		AuraCharacter->AddExperience(LevelTwoThreshold - AuraCharacter->GetTotalExperience());
	}
	if (AuraCharacter->GetRunUpgradeState().IsEmpty())
	{
		AuraCharacter->ApplyRunUpgrade(EAuraRunUpgradeType::Spellweaver);
	}
	Checkpoint->ActivateCheckpoint(AuraCharacter);
	FTransform SavedTransform;
	int32 SavedLevel = 0;
	int32 SavedExperience = 0;
	FAuraRunUpgradeState SavedRunUpgrades;
	UAuraRunSubsystem* RunSubsystem = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	FName ClaimedTreasureId = NAME_None;
	FName ClearedRewardEncounterId = NAME_None;
	if (RunSubsystem)
	{
		TArray<AAuraEncounterVolume*> RewardEncounters;
		for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
		{
			if (It->IsRewardEncounter() && It->GetRewardChest()) RewardEncounters.Add(*It);
		}
		RewardEncounters.Sort([](const AAuraEncounterVolume& Left, const AAuraEncounterVolume& Right)
		{
			return Left.GetEncounterOrder() < Right.GetEncounterOrder();
		});
		if (!RewardEncounters.IsEmpty())
		{
			ClearedRewardEncounterId = RewardEncounters[0]->GetEncounterId();
			RunSubsystem->MarkRewardEncounterCleared(ClearedRewardEncounterId);
		}
		if (RewardEncounters.Num() > 1)
		{
			ClaimedTreasureId = RewardEncounters[1]->GetRewardChest()->GetTreasureId();
			RunSubsystem->MarkTreasureClaimed(ClaimedTreasureId);
		}
	}
	const bool bCheckpointSaved = RunSubsystem
		&& RunSubsystem->TryGetCheckpointForWorld(
			GetWorld(), SavedTransform, &SavedLevel, &SavedExperience, &SavedRunUpgrades)
		&& SavedLevel == AuraCharacter->GetCharacterLevel()
		&& SavedExperience == AuraCharacter->GetTotalExperience()
		&& SavedRunUpgrades == AuraCharacter->GetRunUpgradeState()
		&& RunSubsystem->IsRewardEncounterCleared(ClearedRewardEncounterId)
		&& RunSubsystem->IsTreasureClaimed(ClaimedTreasureId);
	UE_LOG(LogAuraInput, Display,
		TEXT("Checkpoint reload smoke test started. Saved=%s Id=%s Location=%s PlayerLevel=%d XP=%d Boons=%d ClearedRewardEncounter=%s ClaimedTreasure=%s"),
		bCheckpointSaved ? TEXT("true") : TEXT("false"),
		RunSubsystem ? *RunSubsystem->GetActiveCheckpointId().ToString() : TEXT("None"),
		*SavedTransform.GetLocation().ToCompactString(), SavedLevel, SavedExperience,
		SavedRunUpgrades.GetTotalRanks(), *ClearedRewardEncounterId.ToString(),
		*ClaimedTreasureId.ToString());
	if (!bCheckpointSaved) return;

	const FName LevelName(*UGameplayStatics::GetCurrentLevelName(this, true));
	FTimerHandle ReloadTimer;
	GetWorldTimerManager().SetTimer(ReloadTimer, [this, LevelName]()
	{
		UGameplayStatics::OpenLevel(this, LevelName);
	}, 0.25f, false);
}

void AAuraPlayerController::AuraTestDungeonNavigation()
{
	TArray<AActor*> GateActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraDungeonGate::StaticClass(), GateActors);
	for (AActor* GateActor : GateActors)
	{
		if (AAuraDungeonGate* Gate = Cast<AAuraDungeonGate>(GateActor))
		{
			Gate->SetGateOpen(true, true);
		}
	}

	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	FTimerHandle NavigationTimer;
	GetWorldTimerManager().SetTimer(NavigationTimer, [WeakController]()
	{
		if (!WeakController.IsValid()) return;
		AAuraPlayerController* Controller = WeakController.Get();
		UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(
			Controller->GetWorld());
		ANavigationData* DefaultNavData = NavigationSystem
			? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
			: nullptr;
		TArray<AActor*> RecastActors;
		UGameplayStatics::GetAllActorsOfClass(
			Controller, ARecastNavMesh::StaticClass(), RecastActors);
		const ARecastNavMesh* RecastNavMesh = RecastActors.IsEmpty()
			? nullptr : Cast<ARecastNavMesh>(RecastActors[0]);
		UE_LOG(LogAuraInput, Display,
			TEXT("Dungeon navigation runtime. NetMode=%d RecastActors=%d DefaultNavData=%s Tiles=%d Registered=%s RuntimeGeneration=%d"),
			static_cast<int32>(Controller->GetWorld()->GetNetMode()),
			RecastActors.Num(),
			DefaultNavData ? *DefaultNavData->GetName() : TEXT("None"),
			RecastNavMesh ? RecastNavMesh->GetNavMeshTilesCount() : 0,
			RecastNavMesh && RecastNavMesh->IsRegistered() ? TEXT("true") : TEXT("false"),
			RecastNavMesh ? static_cast<int32>(RecastNavMesh->GetRuntimeGenerationMode()) : -1);
		const FVector Start(250.f, 0.f, 96.f);
		const FVector End(12300.f, 4000.f, 946.f);
		FNavLocation ProjectedStart;
		FNavLocation ProjectedEnd;
		const bool bProjectedStart = NavigationSystem
			&& NavigationSystem->ProjectPointToNavigation(
				Start, ProjectedStart, FVector(400.f, 400.f, 600.f));
		const bool bProjectedEnd = NavigationSystem
			&& NavigationSystem->ProjectPointToNavigation(
				End, ProjectedEnd, FVector(400.f, 400.f, 600.f));

		const auto TestSegment = [Controller](
			const TCHAR* Label, const FVector& SegmentStart, const FVector& SegmentEnd)
		{
			UNavigationPath* SegmentPath = UNavigationSystemV1::FindPathToLocationSynchronously(
				Controller, SegmentStart, SegmentEnd, Controller->GetPawn());
			const bool bSegmentPassed = SegmentPath && SegmentPath->IsValid()
				&& !SegmentPath->IsPartial() && SegmentPath->PathPoints.Num() >= 2;
			UE_LOG(LogAuraInput, Display,
				TEXT("Dungeon navigation segment. Name=%s Passed=%s Valid=%s Partial=%s Points=%d From=%s To=%s"),
				Label,
				bSegmentPassed ? TEXT("true") : TEXT("false"),
				SegmentPath && SegmentPath->IsValid() ? TEXT("true") : TEXT("false"),
				SegmentPath && SegmentPath->IsPartial() ? TEXT("true") : TEXT("false"),
				SegmentPath ? SegmentPath->PathPoints.Num() : 0,
				*SegmentStart.ToCompactString(), *SegmentEnd.ToCompactString());
			if (SegmentPath && FCString::Strcmp(Label, TEXT("StairAscent")) == 0)
			{
				for (int32 PointIndex = 0; PointIndex < SegmentPath->PathPoints.Num(); ++PointIndex)
				{
					UE_LOG(LogAuraInput, Display,
						TEXT("Dungeon stair path point. Index=%d Location=%s"),
						PointIndex, *SegmentPath->PathPoints[PointIndex].ToCompactString());
				}
			}
			return bSegmentPassed;
		};

		const bool bLowerPassed = TestSegment(
			TEXT("LowerHall"), FVector(250.f, 0.f, 96.f), FVector(3000.f, 0.f, 96.f));
		const bool bStairPassed = TestSegment(
			TEXT("StairAscent"), FVector(3350.f, 0.f, 96.f), FVector(4500.f, 0.f, 516.f));
		const bool bStairDescentPassed = TestSegment(
			TEXT("StairDescent"), FVector(4500.f, 0.f, 516.f),
			FVector(3350.f, 0.f, 96.f));
		const bool bUpperPassed = TestSegment(
			TEXT("UpperHall"), FVector(4500.f, 0.f, 516.f), FVector(7500.f, 0.f, 516.f));
		const bool bLowerNorthPassed = TestSegment(
			TEXT("LowerNorthSeal"), FVector(2100.f, 0.f, 96.f),
			FVector(2100.f, 2600.f, 96.f));
		const bool bUpperSouthPassed = TestSegment(
			TEXT("UpperSouthSeal"), FVector(5700.f, 0.f, 516.f),
			FVector(5700.f, -2600.f, 516.f));
		const bool bOldWingPassed = TestSegment(
			TEXT("OldWing"), FVector(7500.f, 0.f, 516.f), FVector(7500.f, 4000.f, 516.f));
		const bool bDeepStairPassed = TestSegment(
			TEXT("DeepStair"), FVector(7500.f, 0.f, 516.f), FVector(8900.f, 0.f, 946.f));
		const bool bDeepNorthPassed = TestSegment(
			TEXT("DeepNorth"), FVector(8900.f, 0.f, 946.f), FVector(9900.f, 2500.f, 946.f));
		const bool bDeepSouthPassed = TestSegment(
			TEXT("DeepSouth"), FVector(8900.f, 0.f, 946.f), FVector(11100.f, -2500.f, 946.f));
		const bool bDeepCorePassed = TestSegment(
			TEXT("DeepCore"), FVector(11100.f, 0.f, 946.f), FVector(12300.f, 4000.f, 946.f));
		const bool bLowerNorthToCorePassed = TestSegment(
			TEXT("LowerNorthToCore"), FVector(2100.f, 2600.f, 96.f), End);
		const bool bUpperSouthToCorePassed = TestSegment(
			TEXT("UpperSouthToCore"), FVector(5700.f, -2600.f, 516.f), End);
		const bool bDeepNorthToCorePassed = TestSegment(
			TEXT("DeepNorthToCore"), FVector(9900.f, 2500.f, 946.f), End);
		const bool bDeepSouthToCorePassed = TestSegment(
			TEXT("DeepSouthToCore"), FVector(11100.f, -2500.f, 946.f), End);
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			Controller, Start, End, Controller->GetPawn());
		// Recast may simplify away every intermediate upper-floor waypoint;
		// the explicit ascent and upper-hall path checks prove this layer.
		const bool bReachedUpperFloor = bStairPassed && bUpperPassed;
		bool bReachedDeepFloor = false;
		bool bTurnedNorth = false;
		if (Path)
		{
			for (const FVector& Point : Path->PathPoints)
			{
				bReachedDeepFloor |= Point.Z > 800.f;
				bTurnedNorth |= Point.Y > 2500.f;
			}
		}
		const bool bPassed = bLowerPassed && bStairPassed && bStairDescentPassed && bUpperPassed
			&& bLowerNorthPassed && bUpperSouthPassed && bOldWingPassed
			&& bDeepStairPassed && bDeepNorthPassed && bDeepSouthPassed && bDeepCorePassed
			&& bLowerNorthToCorePassed && bUpperSouthToCorePassed
			&& bDeepNorthToCorePassed && bDeepSouthToCorePassed
			&& Path && Path->IsValid() && !Path->IsPartial()
			&& Path->PathPoints.Num() >= 3 && bReachedUpperFloor && bReachedDeepFloor
			&& bTurnedNorth;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M20_NAV Passed=%s StairDescent=%s DeepStair=%s DeepNorth=%s DeepSouth=%s DeepCore=%s FourSealReturns=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			bStairDescentPassed ? TEXT("true") : TEXT("false"),
			bDeepStairPassed ? TEXT("true") : TEXT("false"),
			bDeepNorthPassed ? TEXT("true") : TEXT("false"),
			bDeepSouthPassed ? TEXT("true") : TEXT("false"),
			bDeepCorePassed ? TEXT("true") : TEXT("false"),
			bLowerNorthToCorePassed && bUpperSouthToCorePassed
				&& bDeepNorthToCorePassed && bDeepSouthToCorePassed
				? TEXT("true") : TEXT("false"));
		UE_LOG(LogAuraInput, Display,
			TEXT("Dungeon navigation smoke test completed. Passed=%s NavSystem=%s ProjectedStart=%s ProjectedEnd=%s Valid=%s Partial=%s Points=%d ReachedUpper=%s ReachedDeep=%s TurnedNorth=%s Start=%s End=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			NavigationSystem ? TEXT("true") : TEXT("false"),
			bProjectedStart ? TEXT("true") : TEXT("false"),
			bProjectedEnd ? TEXT("true") : TEXT("false"),
			Path && Path->IsValid() ? TEXT("true") : TEXT("false"),
			Path && Path->IsPartial() ? TEXT("true") : TEXT("false"),
			Path ? Path->PathPoints.Num() : 0,
			bReachedUpperFloor ? TEXT("true") : TEXT("false"),
			bReachedDeepFloor ? TEXT("true") : TEXT("false"),
			bTurnedNorth ? TEXT("true") : TEXT("false"),
			*Start.ToCompactString(), *End.ToCompactString());
	}, 12.f, false);
}

void AAuraPlayerController::AuraTestLoreM20()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	AAuraLoreRecord* Record = nullptr;
	for (TActorIterator<AAuraLoreRecord> It(GetWorld()); It; ++It)
	{
		if (It->GetRecordId() == TEXT("ReverseRitualNotes"))
		{
			Record = *It;
			break;
		}
	}
	if (!AuraPawn || !AuraHUD || !Record)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_M20_LORE Passed=false Pawn=%s HUD=%s Record=%s"),
			AuraPawn ? TEXT("true") : TEXT("false"),
			AuraHUD ? TEXT("true") : TEXT("false"),
			Record ? TEXT("true") : TEXT("false"));
		return;
	}
	const FVector RecordLocation = Record->GetActorLocation();
	const bool bDifferentFloorRejected = !Record->IsInRange(
		FVector(RecordLocation.X, RecordLocation.Y, 96.f));
	const bool bTeleported = AuraPawn->TeleportTo(
		RecordLocation + FVector(-100.f, 0.f, 96.f),
		AuraPawn->GetActorRotation(), false, true);
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Pressed, 1.f));
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Released, 0.f));
	const bool bOpened = AuraHUD->GetVisibleLoreRecordId() == Record->GetRecordId();
	if (bOpened && FParse::Param(FCommandLine::Get(), TEXT("AuraLoreM20Capture")))
	{
		const FString ScreenshotDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
		IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
		FTimerHandle CaptureTimer;
		GetWorldTimerManager().SetTimer(CaptureTimer, [ScreenshotDirectory]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(
				ScreenshotDirectory, TEXT("M20_Lore_ReverseRitual.png")), false, false);
		}, 0.65f, false);
	}
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraHUD> WeakHUD(AuraHUD);
	const TWeakObjectPtr<AAuraCharacter> WeakPawn(AuraPawn);
	const TWeakObjectPtr<AAuraLoreRecord> WeakRecord(Record);
	FTimerHandle CloseTimer;
	GetWorldTimerManager().SetTimer(CloseTimer,
		[WeakController, WeakHUD, WeakPawn, WeakRecord, RecordLocation,
			bDifferentFloorRejected, bTeleported, bOpened]()
	{
		if (!WeakController.IsValid() || !WeakHUD.IsValid()
			|| !WeakPawn.IsValid() || !WeakRecord.IsValid()) return;
		AAuraPlayerController* Controller = WeakController.Get();
		AAuraCharacter* Pawn = WeakPawn.Get();
		AAuraLoreRecord* LoreRecord = WeakRecord.Get();
		const FVector FarLocation = RecordLocation
			+ FVector(LoreRecord->GetInteractionRadius() + 500.f, 0.f, 96.f);
		Pawn->SetActorLocation(FarLocation, false, nullptr, ETeleportType::TeleportPhysics);
		const bool bOutsideRange = !LoreRecord->IsInRange(Pawn->GetActorLocation());
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Pressed, 1.f));
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Released, 0.f));
		const bool bClosedWithF = !WeakHUD->IsLoreRecordVisible();
		const bool bFarReopenRejected = !Controller->TryInteractWithLoreRecord()
			&& !WeakHUD->IsLoreRecordVisible();
		Pawn->SetActorLocation(RecordLocation + FVector(-100.f, 0.f, 96.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Pressed, 1.f));
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Released, 0.f));
		const bool bReopened = WeakHUD->GetVisibleLoreRecordId() == LoreRecord->GetRecordId();
		Pawn->SetActorLocation(FarLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Pressed, 1.f));
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Released, 0.f));
		const bool bClosedWithE = !WeakHUD->IsLoreRecordVisible()
			&& !Controller->bIsShockTargeting;
		AAuraLoreRecord* EntranceRecord = nullptr;
		for (TActorIterator<AAuraLoreRecord> It(Controller->GetWorld()); It; ++It)
		{
			if (It->GetRecordId() == TEXT("EntranceDispatch"))
			{
				EntranceRecord = *It;
				break;
			}
		}
		bool bEntranceOpened = false;
		bool bEntranceClosedAway = false;
		if (EntranceRecord)
		{
			const FVector EntranceLocation = EntranceRecord->GetActorLocation();
			Pawn->SetActorLocation(EntranceLocation + FVector(-100.f, 0.f, 96.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Pressed, 1.f));
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Released, 0.f));
			bEntranceOpened = WeakHUD->GetVisibleLoreRecordId() == EntranceRecord->GetRecordId();
			Pawn->SetActorLocation(EntranceLocation
				+ FVector(EntranceRecord->GetInteractionRadius() + 500.f, 0.f, 96.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			const bool bEntranceOutsideRange = !EntranceRecord->IsInRange(Pawn->GetActorLocation());
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Pressed, 1.f));
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Released, 0.f));
			bEntranceClosedAway = bEntranceOutsideRange && !WeakHUD->IsLoreRecordVisible()
				&& !Controller->bIsShockTargeting;
		}
		const bool bPassed = bDifferentFloorRejected && bTeleported && bOpened
			&& bOutsideRange && bClosedWithF && bFarReopenRejected
			&& bReopened && bClosedWithE && bEntranceOpened && bEntranceClosedAway;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M20_LORE Passed=%s FloorIsolation=%s Teleported=%s Opened=%s OutsideRange=%s ClosedWithF=%s FarReopenRejected=%s Reopened=%s ClosedWithE=%s EntranceOpened=%s EntranceClosedAway=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			bDifferentFloorRejected ? TEXT("true") : TEXT("false"),
			bTeleported ? TEXT("true") : TEXT("false"),
			bOpened ? TEXT("true") : TEXT("false"),
			bOutsideRange ? TEXT("true") : TEXT("false"),
			bClosedWithF ? TEXT("true") : TEXT("false"),
			bFarReopenRejected ? TEXT("true") : TEXT("false"),
			bReopened ? TEXT("true") : TEXT("false"),
			bClosedWithE ? TEXT("true") : TEXT("false"),
			bEntranceOpened ? TEXT("true") : TEXT("false"),
			bEntranceClosedAway ? TEXT("true") : TEXT("false"));
	}, 1.4f, false);
}

void AAuraPlayerController::AuraTestDungeonM21()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraEncounterVolume* EliteRoom = nullptr;
	int32 RoomCount = 0;
	bool bPopulationValid = true;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		AAuraEncounterVolume* Room = *It;
		++RoomCount;
		int32 ExpectedCount = 0;
		int32 ExpectedStanding = 0;
		float ExpectedFloorZ = 0.f;
		switch (Room->GetEncounterOrder())
		{
		case 1: ExpectedCount = 2; ExpectedStanding = 2; break;
		case 2: ExpectedCount = 4; ExpectedStanding = 1; ExpectedFloorZ = 420.f; EliteRoom = Room; break;
		case 3: ExpectedCount = 1; ExpectedFloorZ = 850.f; break;
		case 101: ExpectedCount = 3; ExpectedStanding = 2; break;
		case 102: ExpectedCount = 3; ExpectedStanding = 1; ExpectedFloorZ = 420.f; break;
		case 103: ExpectedCount = 2; ExpectedStanding = 2; ExpectedFloorZ = 850.f; break;
		case 104: ExpectedCount = 4; ExpectedStanding = 2; ExpectedFloorZ = 850.f; break;
		case 105: ExpectedCount = 3; ExpectedStanding = 2; break;
		case 106: ExpectedCount = 3; ExpectedStanding = 2; ExpectedFloorZ = 420.f; break;
		case 107: ExpectedCount = 4; ExpectedStanding = 2; ExpectedFloorZ = 420.f; break;
		case 11: ExpectedCount = 3; break;
		case 12: ExpectedCount = 3; ExpectedFloorZ = 420.f; break;
		case 13: ExpectedCount = 3; ExpectedFloorZ = 850.f; break;
		case 14: ExpectedCount = 4; ExpectedFloorZ = 850.f; break;
		case 15: ExpectedCount = 3; ExpectedFloorZ = 850.f; break;
		case 16: ExpectedCount = 3; ExpectedFloorZ = 850.f; break;
		default: bPopulationValid = false; continue;
		}
		const bool bRoomValid = Room->GetConfiguredSpawnCount() == ExpectedCount
			&& Room->GetPreplacedGuardCount() == ExpectedStanding
			&& Room->GetActiveEnemyCount() == ExpectedStanding
			&& FMath::IsNearlyEqual(Room->GetRuntimeFloorZ(), ExpectedFloorZ, 3.f);
		bPopulationValid &= bRoomValid;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M21_ROOM Order=%d Passed=%s Configured=%d Standing=%d Active=%d FloorZ=%.1f"),
			Room->GetEncounterOrder(), bRoomValid ? TEXT("true") : TEXT("false"),
			Room->GetConfiguredSpawnCount(), Room->GetPreplacedGuardCount(),
			Room->GetActiveEnemyCount(), Room->GetRuntimeFloorZ());
	}
	bPopulationValid &= RoomCount == 16 && EliteRoom && AuraPawn;

	struct FStairSample { float X; float ExpectedZ; bool bRamp; };
	const FStairSample StairSamples[] = {
		{ 7700.f, 420.f, false }, { 7900.f, 508.f, true },
		{ 8100.f, 635.f, true }, { 8300.f, 762.f, true },
		{ 8500.f, 850.f, false }
	};
	bool bStairCollisionValid = true;
	for (const FStairSample& Sample : StairSamples)
	{
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(
			Hit, FVector(Sample.X, 0.f, 1500.f),
			FVector(Sample.X, 0.f, 300.f), ECC_WorldStatic);
		const bool bSamplePassed = bHit
			&& FMath::Abs(Hit.ImpactPoint.Z - Sample.ExpectedZ) < 45.f
			&& Hit.ImpactNormal.Z >= 0.8f
			&& (!Sample.bRamp || (Hit.GetActor()
				&& FVector::DistSquared(Hit.GetActor()->GetActorLocation(),
					FVector(8108.f, 0.f, 622.3f)) < FMath::Square(5.f)));
		bStairCollisionValid &= bSamplePassed;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M21_STAIR X=%.0f Passed=%s Hit=%s Z=%.1f NormalZ=%.2f"),
			Sample.X, bSamplePassed ? TEXT("true") : TEXT("false"),
			*GetNameSafe(Hit.GetActor()), bHit ? Hit.ImpactPoint.Z : -1.f,
			bHit ? Hit.ImpactNormal.Z : -1.f);
	}
	UNavigationPath* StairPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		this, FVector(7500.f, 0.f, 516.f), FVector(8700.f, 0.f, 946.f), AuraPawn);
	const bool bStairNavigationValid = StairPath && StairPath->IsValid()
		&& !StairPath->IsPartial() && StairPath->PathPoints.Num() >= 2;
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_M21_STAIR_NAV Passed=%s Valid=%s Partial=%s Points=%d"),
		bStairNavigationValid ? TEXT("true") : TEXT("false"),
		StairPath && StairPath->IsValid() ? TEXT("true") : TEXT("false"),
		StairPath && StairPath->IsPartial() ? TEXT("true") : TEXT("false"),
		StairPath ? StairPath->PathPoints.Num() : 0);
	if (!AuraPawn || !EliteRoom)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("AURA_M21_RUNTIME Passed=false Pawn=%s EliteRoom=%s"),
			AuraPawn ? TEXT("true") : TEXT("false"),
			EliteRoom ? TEXT("true") : TEXT("false"));
		return;
	}
	const FVector CombatLocation(EliteRoom->GetActorLocation().X, 0.f,
		EliteRoom->GetRuntimeFloorZ() + 96.f);
	const bool bTeleported = AuraPawn->TeleportTo(
		CombatLocation, EliteRoom->GetActorRotation(), false, true);
	EliteRoom->StartEncounter(AuraPawn, false);
	const bool bArming = EliteRoom->GetEncounterState() == EAuraEncounterState::Arming;
	const TWeakObjectPtr<AAuraEncounterVolume> WeakEliteRoom(EliteRoom);
	FTimerHandle ReinforcementTimer;
	GetWorldTimerManager().SetTimer(ReinforcementTimer,
		[WeakEliteRoom, bPopulationValid, bStairCollisionValid,
			bStairNavigationValid, bTeleported, bArming, RoomCount]()
	{
		const AAuraEncounterVolume* Room = WeakEliteRoom.Get();
		bool bShamanStillAlive = false;
		if (Room)
		{
			for (const AAuraEnemy* Enemy : Room->GetActiveEnemies())
			{
				bShamanStillAlive |= Enemy && !Enemy->IsDead()
					&& Enemy->GetClass()->GetName().Contains(TEXT("EliteShaman"));
			}
		}
		const bool bReinforcementsSpawned = Room
			&& Room->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& Room->GetActiveEnemyCount() == 4
			&& Room->GetRemainingEnemyCount() == 4 && bShamanStillAlive;
		const bool bPassed = bPopulationValid && bStairCollisionValid
			&& bStairNavigationValid && bTeleported && bArming
			&& bReinforcementsSpawned;
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_M21_RUNTIME Passed=%s Rooms=%d Population=%s StairCollision=%s StairNavigation=%s Teleported=%s Arming=%s ReinforcementsWithLiveShaman=%s Active=%d Remaining=%d"),
			bPassed ? TEXT("true") : TEXT("false"), RoomCount,
			bPopulationValid ? TEXT("true") : TEXT("false"),
			bStairCollisionValid ? TEXT("true") : TEXT("false"),
			bStairNavigationValid ? TEXT("true") : TEXT("false"),
			bTeleported ? TEXT("true") : TEXT("false"),
			bArming ? TEXT("true") : TEXT("false"),
			bReinforcementsSpawned ? TEXT("true") : TEXT("false"),
			Room ? Room->GetActiveEnemyCount() : -1,
			Room ? Room->GetRemainingEnemyCount() : -1);
	}, 2.1f, false);
}

void AAuraPlayerController::AuraTestCombatLoop()
{
	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	AAuraEnemy* TargetEnemy = Enemies.IsEmpty() ? nullptr : Cast<AAuraEnemy>(Enemies[0]);
	if (!TargetEnemy || !GetPawn())
	{
		UE_LOG(LogAuraInput, Error, TEXT("Combat loop smoke test requires a placed AuraEnemy and a possessed pawn."));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	const TWeakObjectPtr<AAuraEnemy> WeakTarget(TargetEnemy);
	const auto FireTestShot = [this, WeakTarget](int32 ShotNumber)
	{
		if (!WeakTarget.IsValid())
		{
			UE_LOG(LogAuraInput, Error, TEXT("Combat loop target disappeared before shot %d."), ShotNumber);
			return;
		}
		if (APawn* ControlledPawn = GetPawn())
		{
			const FVector TargetDirection = (WeakTarget->GetActorLocation() - ControlledPawn->GetActorLocation()).GetSafeNormal2D();
			if (!TargetDirection.IsNearlyZero()) ControlledPawn->SetActorRotation(TargetDirection.Rotation());
		}
		UE_LOG(LogAuraInput, Display, TEXT("Combat loop firing shot %d at %s."),
			ShotNumber, *WeakTarget->GetName());
		PrimaryAttack();
	};

	FireTestShot(1);
	constexpr float ShotInterval = 1.5f;
	for (int32 ShotIndex = 1; ShotIndex < 4; ++ShotIndex)
	{
		FTimerHandle ShotTimer;
		GetWorldTimerManager().SetTimer(ShotTimer, [FireTestShot, ShotIndex]()
		{
			FireTestShot(ShotIndex + 1);
		}, ShotIndex * ShotInterval, false);
	}

	FTimerHandle AnimationTimer;
	GetWorldTimerManager().SetTimer(AnimationTimer, this,
		&AAuraPlayerController::AuraLogCharacterPresentation, 0.12f, false);

	FTimerHandle HitCaptureTimer;
	GetWorldTimerManager().SetTimer(HitCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 0.82f, false);

	FTimerHandle DeathCaptureTimer;
	GetWorldTimerManager().SetTimer(DeathCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 5.45f, false);

	FTimerHandle DeathPoseCaptureTimer;
	GetWorldTimerManager().SetTimer(DeathPoseCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 6.45f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [this, WeakTarget]()
	{
		bUseDebugAimOverride = false;
		const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
		const UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter
			? AuraCharacter->GetAbilitySystemComponent() : nullptr;
		const UAbilitySystemComponent* EnemyAbilitySystem = WeakTarget.IsValid()
			? WeakTarget->GetAbilitySystemComponent() : nullptr;
		const float PlayerMana = PlayerAbilitySystem
			? PlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
		const float EnemyHealth = EnemyAbilitySystem
			? EnemyAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : 0.f;
		UE_LOG(LogAuraInput, Display,
			TEXT("Combat loop smoke test completed. PlayerMana=%.1f EnemyHealth=%.1f TargetValid=%s"),
			PlayerMana, EnemyHealth, WeakTarget.IsValid() ? TEXT("true") : TEXT("false"));
	}, 6.8f, false);
}

void AAuraPlayerController::AuraTestCreatureFireBoltHitboxes()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UWorld* World = GetWorld();
	UAbilitySystemComponent* SourceAbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	UClass* GoblinClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear.BP_AuraGoblinSpear_C"));
	UClass* ShroomClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom.BP_AuraShroom_C"));
	if (!HasAuthority() || !World || !AuraCharacter || !SourceAbilitySystem
		|| !GoblinClass || !ShroomClass)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Creature FireBolt hitbox smoke test completed. Passed=false Reason=MissingRuntimeDependency"));
		return;
	}

	const UCapsuleComponent* PlayerCapsule = AuraCharacter->GetCapsuleComponent();
	const float FloorZ = AuraCharacter->GetActorLocation().Z
		- (PlayerCapsule ? PlayerCapsule->GetScaledCapsuleHalfHeight() : 96.f);
	const USkeletalMeshComponent* WeaponMesh = AuraCharacter->GetWeaponMesh();
	const float ProjectileZ = WeaponMesh
		? (WeaponMesh->GetComponentLocation() + WeaponMesh->GetUpVector() * 68.f).Z
		: AuraCharacter->GetActorLocation().Z + 80.f;
	const FVector Forward = AuraCharacter->GetActorForwardVector().GetSafeNormal2D();
	const FVector TestForward = Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
	const FVector TestRight = FVector::CrossProduct(FVector::UpVector, TestForward).GetSafeNormal();

	auto SpawnStationaryTarget = [World, FloorZ, AuraCharacter](
		UClass* EnemyClass, const FVector& HorizontalLocation) -> AAuraEnemy*
	{
		const AAuraEnemy* EnemyDefault = EnemyClass
			? EnemyClass->GetDefaultObject<AAuraEnemy>() : nullptr;
		const UCapsuleComponent* DefaultCapsule = EnemyDefault
			? EnemyDefault->GetCapsuleComponent() : nullptr;
		const float HalfHeight = DefaultCapsule
			? DefaultCapsule->GetUnscaledCapsuleHalfHeight() : 96.f;
		FVector SpawnLocation = HorizontalLocation;
		SpawnLocation.Z = FloorZ + HalfHeight;
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = AuraCharacter;
		SpawnParameters.Instigator = AuraCharacter;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AAuraEnemy* Enemy = World->SpawnActor<AAuraEnemy>(
			EnemyClass, SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
		if (!Enemy) return nullptr;
		if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AIController->StopMovement();
			AIController->SetActorTickEnabled(false);
		}
		return Enemy;
	};

	const FVector LaneCenter = AuraCharacter->GetActorLocation() + TestForward * 650.f;
	AAuraEnemy* Goblin = SpawnStationaryTarget(
		GoblinClass, LaneCenter - TestRight * 220.f);
	AAuraEnemy* Shroom = SpawnStationaryTarget(
		ShroomClass, LaneCenter + TestRight * 220.f);
	if (!Goblin || !Shroom)
	{
		if (Goblin) Goblin->Destroy();
		if (Shroom) Shroom->Destroy();
		UE_LOG(LogAuraInput, Error,
			TEXT("Creature FireBolt hitbox smoke test completed. Passed=false Reason=SpawnFailed"));
		return;
	}

	const float GoblinHealthBefore = Goblin->GetAbilitySystemComponent()->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	const float ShroomHealthBefore = Shroom->GetAbilitySystemComponent()->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	auto FacePoint = [AuraCharacter](const FVector& TargetPoint)
	{
		const FVector Direction = (TargetPoint
			- AuraCharacter->GetActorLocation()).GetSafeNormal2D();
		if (!Direction.IsNearlyZero()) AuraCharacter->SetActorRotation(Direction.Rotation());
	};

	const TWeakObjectPtr<AAuraEnemy> WeakGoblin(Goblin);
	const TWeakObjectPtr<AAuraEnemy> WeakShroom(Shroom);
	// Deliberately aim 55uu off the Shroom centre. This covers the practical case
	// where the player fires through the visible edge of its broad mushroom cap.
	FacePoint(Shroom->GetActorLocation() + TestRight * 55.f);
	PrimaryAttack();

	FTimerHandle GoblinShotTimer;
	GetWorldTimerManager().SetTimer(GoblinShotTimer,
		[this, AuraCharacter, WeakGoblin]()
	{
		if (!WeakGoblin.IsValid()) return;
		const FVector Direction = (WeakGoblin->GetActorLocation()
			- AuraCharacter->GetActorLocation()).GetSafeNormal2D();
		if (!Direction.IsNearlyZero()) AuraCharacter->SetActorRotation(Direction.Rotation());
		PrimaryAttack();
	}, 1.15f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[WeakGoblin, WeakShroom, GoblinHealthBefore, ShroomHealthBefore,
		 ProjectileZ, FloorZ]()
	{
		const float GoblinHealthAfter = WeakGoblin.IsValid()
			? WeakGoblin->GetAbilitySystemComponent()->GetNumericAttribute(
				UAuraAttributeSet::GetHealthAttribute()) : GoblinHealthBefore;
		const float ShroomHealthAfter = WeakShroom.IsValid()
			? WeakShroom->GetAbilitySystemComponent()->GetNumericAttribute(
				UAuraAttributeSet::GetHealthAttribute()) : ShroomHealthBefore;
		const float GoblinTop = WeakGoblin.IsValid() && WeakGoblin->GetCapsuleComponent()
			? WeakGoblin->GetActorLocation().Z
				+ WeakGoblin->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : FloorZ;
		const float ShroomTop = WeakShroom.IsValid() && WeakShroom->GetCapsuleComponent()
			? WeakShroom->GetActorLocation().Z
				+ WeakShroom->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : FloorZ;
		const bool bGoblinHit = GoblinHealthAfter < GoblinHealthBefore - 0.1f;
		const bool bShroomHit = ShroomHealthAfter < ShroomHealthBefore - 0.1f;
		const bool bPassed = bGoblinHit && bShroomHit;
		UE_LOG(LogAuraInput, Display,
			TEXT("Creature FireBolt hitbox smoke test completed. Passed=%s IdleProjectileZ=%.1f FloorZ=%.1f GoblinTop=%.1f GoblinHealth=%.1f->%.1f ShroomTop=%.1f ShroomHealth=%.1f->%.1f"),
			bPassed ? TEXT("true") : TEXT("false"), ProjectileZ, FloorZ,
			GoblinTop, GoblinHealthBefore, GoblinHealthAfter,
			ShroomTop, ShroomHealthBefore, ShroomHealthAfter);
		if (WeakGoblin.IsValid()) WeakGoblin->Destroy();
		if (WeakShroom.IsValid()) WeakShroom->Destroy();
	}, 2.25f, false);
}

void AAuraPlayerController::AuraTestEnemyAI()
{
	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	AAuraEnemy* Enemy = nullptr;
	for (AActor* Candidate : Enemies)
	{
		AAuraEnemy* CandidateEnemy = Cast<AAuraEnemy>(Candidate);
		if (CandidateEnemy
			&& CandidateEnemy->GetEnemyArchetype() == EAuraEnemyArchetype::Ghoul)
		{
			Enemy = CandidateEnemy;
			break;
		}
	}
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!Enemy || !AuraCharacter)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Enemy AI smoke test requires a placed AuraEnemy and a possessed AuraCharacter."));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	const float InitialDistance = FVector::Dist2D(AuraCharacter->GetActorLocation(), Enemy->GetActorLocation());
	const TWeakObjectPtr<AAuraEnemy> WeakEnemy(Enemy);
	UE_LOG(LogAuraInput, Display, TEXT("Enemy AI smoke test started. InitialDistance=%.1f Enemy=%s Controller=%s"),
		InitialDistance, *Enemy->GetName(), *GetNameSafe(Enemy->GetController()));

	FTimerHandle ChaseCaptureTimer;
	GetWorldTimerManager().SetTimer(ChaseCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 1.4f, false);

	FTimerHandle AttackCaptureTimer;
	GetWorldTimerManager().SetTimer(AttackCaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 4.4f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [this, WeakEnemy, InitialDistance]()
	{
		const AAuraCharacter* CurrentCharacter = Cast<AAuraCharacter>(GetPawn());
		const UAbilitySystemComponent* PlayerAbilitySystem = CurrentCharacter
			? CurrentCharacter->GetAbilitySystemComponent() : nullptr;
		const AAuraAIController* AIController = WeakEnemy.IsValid()
			? Cast<AAuraAIController>(WeakEnemy->GetController()) : nullptr;
		const float PlayerHealth = PlayerAbilitySystem
			? PlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		const float FinalDistance = CurrentCharacter && WeakEnemy.IsValid()
			? FVector::Dist2D(CurrentCharacter->GetActorLocation(), WeakEnemy->GetActorLocation()) : -1.f;
		const bool bReturnedAfterKill = AIController
			&& (AIController->GetAIState() == EAuraAIState::Returning
				|| AIController->GetAIState() == EAuraAIState::Idle);
		const bool bPassed = PlayerHealth <= 0.f && CurrentCharacter && CurrentCharacter->IsDead()
			&& bReturnedAfterKill;
		UE_LOG(LogAuraInput, Display,
			TEXT("Enemy AI smoke test completed. Passed=%s InitialDistance=%.1f FinalDistance=%.1f PlayerHealth=%.1f PlayerDead=%s AIState=%d"),
			bPassed ? TEXT("true") : TEXT("false"),
			InitialDistance, FinalDistance, PlayerHealth,
			CurrentCharacter && CurrentCharacter->IsDead() ? TEXT("true") : TEXT("false"),
			AIController ? static_cast<int32>(AIController->GetAIState()) : -1);
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 18.f, false);

	FTimerHandle RestartTimer;
	GetWorldTimerManager().SetTimer(RestartTimer, [this]()
	{
		const AAuraCharacter* CurrentCharacter = Cast<AAuraCharacter>(GetPawn());
		if (CurrentCharacter && CurrentCharacter->IsDead())
		{
			UE_LOG(LogAuraInput, Display, TEXT("Enemy AI smoke test injecting R to validate restart input."));
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::R, IE_Pressed, 1.f));
		}
		else
		{
			UE_LOG(LogAuraInput, Error, TEXT("Enemy AI smoke test could not validate restart because the player is alive."));
		}
	}, 21.f, false);
}

void AAuraPlayerController::AuraRestartDemo()
{
	RestartDemo();
}

void AAuraPlayerController::AuraTestShamanRanged()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !PlayerAbilitySystem || !GetWorld())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Shaman ranged smoke test requires AuraCharacter, ASC, and a valid world."));
		return;
	}

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	AAuraEnemy* ShamanTemplate = nullptr;
	for (AActor* EnemyActor : Enemies)
	{
		AAuraEnemy* Enemy = Cast<AAuraEnemy>(EnemyActor);
		if (!Enemy) continue;
		if (Enemy->IsRangedCombatant() && !ShamanTemplate)
		{
			ShamanTemplate = Enemy;
			continue;
		}
		if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AIController->StopMovement();
			AIController->SetActorTickEnabled(false);
		}
		Enemy->GetCharacterMovement()->DisableMovement();
	}
	if (!ShamanTemplate)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Shaman ranged smoke test requires a placed ranged Shaman blueprint."));
		return;
	}

	AAuraEnemy* TestShaman = ShamanTemplate;
	AAuraAIController* ShamanController =
		Cast<AAuraAIController>(TestShaman->GetController());
	UAbilitySystemComponent* ShamanAbilitySystem = TestShaman->GetAbilitySystemComponent();
	if (!ShamanController || !ShamanAbilitySystem)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Shaman ranged smoke test requires the placed Shaman's AI controller and ASC."));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	const FVector TestHome = ShamanController->GetHomeLocation();
	const FVector ClosePlayerLocation = TestHome + FVector(0.f, 250.f, 0.f);
	const FVector CastPlayerLocation = TestHome + FVector(0.f, 700.f, 0.f);
	ShamanController->StopMovement();
	TestShaman->SetEncounterDormant(false);
	ShamanController->SetCombatTargetForTest(AuraCharacter);
	ShamanController->SetActorTickEnabled(true);
	TestShaman->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TestShaman->GetCharacterMovement()->StopMovementImmediately();
	TestShaman->SetActorLocation(TestHome, false, nullptr, ETeleportType::TeleportPhysics);
	TestShaman->SetActorRotation((ClosePlayerLocation - TestHome).Rotation());
	AuraCharacter->SetActorLocation(
		ClosePlayerLocation, false, nullptr, ETeleportType::TeleportPhysics);
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();
	AuraCharacter->SetActorRotation(FRotator::ZeroRotator);
	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 100.f);
	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 100.f);

	const float InitialRetreatDistance = FVector::Dist2D(
		AuraCharacter->GetActorLocation(), TestShaman->GetActorLocation());
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakShaman(TestShaman);
	const TSharedRef<bool> RetreatObserved = MakeShared<bool>(false);
	const TSharedRef<bool> TelegraphObserved = MakeShared<bool>(false);
	const TSharedRef<bool> InterruptedObserved = MakeShared<bool>(false);
	const TSharedRef<bool> CooldownObserved = MakeShared<bool>(false);
	const TSharedRef<bool> RecastObserved = MakeShared<bool>(false);
	const TSharedRef<bool> GroundBurstObserved = MakeShared<bool>(false);

	UE_LOG(LogAuraInput, Display,
		TEXT("Shaman ranged smoke test started. InitialDistance=%.1f Retreat=%.1f Preferred=%.1f Range=%.1f"),
		InitialRetreatDistance, TestShaman->GetRangedRetreatDistance(),
		TestShaman->GetRangedPreferredDistance(), TestShaman->GetRangedAttackRange());

	FTimerHandle RetreatTimer;
	GetWorldTimerManager().SetTimer(RetreatTimer,
		[this, WeakCharacter, WeakShaman, InitialRetreatDistance, RetreatObserved,
			CastPlayerLocation, TestHome]()
	{
		AAuraCharacter* Character = WeakCharacter.Get();
		AAuraEnemy* Shaman = WeakShaman.Get();
		AAuraAIController* AIController = Shaman
			? Cast<AAuraAIController>(Shaman->GetController()) : nullptr;
		if (!Character || !Shaman || !AIController) return;
		const float RetreatDistance = FVector::Dist2D(
			Character->GetActorLocation(), Shaman->GetActorLocation());
		*RetreatObserved = RetreatDistance > InitialRetreatDistance + 25.f
			&& AIController->GetAIState() == EAuraAIState::Retreating;
		UE_LOG(LogAuraInput, Display,
			TEXT("Shaman retreat observation. Passed=%s Initial=%.1f Current=%.1f AIState=%d"),
			*RetreatObserved ? TEXT("true") : TEXT("false"),
			InitialRetreatDistance, RetreatDistance,
			static_cast<int32>(AIController->GetAIState()));

		AIController->StopMovement();
		Shaman->GetCharacterMovement()->StopMovementImmediately();
		Shaman->SetActorLocation(TestHome, false, nullptr, ETeleportType::TeleportPhysics);
		Shaman->SetActorRotation((CastPlayerLocation - TestHome).Rotation());
		Character->SetActorLocation(
			CastPlayerLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
	}, 0.65f, false);

	FTimerHandle TelegraphAndStunTimer;
	GetWorldTimerManager().SetTimer(TelegraphAndStunTimer,
		[this, WeakCharacter, WeakShaman, TelegraphObserved]()
	{
		AAuraCharacter* Character = WeakCharacter.Get();
		AAuraEnemy* Shaman = WeakShaman.Get();
		UAbilitySystemComponent* SourceAbilitySystem = Character
			? Character->GetAbilitySystemComponent() : nullptr;
		UAbilitySystemComponent* TargetAbilitySystem = Shaman
			? Shaman->GetAbilitySystemComponent() : nullptr;
		if (!Character || !Shaman || !SourceAbilitySystem || !TargetAbilitySystem) return;

		const float TelegraphError = FVector::Dist2D(
			Shaman->GetRangedCastTargetLocation(), Character->GetActorLocation());
		*TelegraphObserved = Shaman->IsRangedCasting() && TelegraphError < 20.f;
		UE_LOG(LogAuraInput, Display,
			TEXT("Shaman telegraph observation. Passed=%s Casting=%s TargetError=%.1f"),
			*TelegraphObserved ? TEXT("true") : TEXT("false"),
			Shaman->IsRangedCasting() ? TEXT("true") : TEXT("false"), TelegraphError);

		if (Shaman->IsRangedCasting())
		{
			FGameplayEffectContextHandle StunContext = SourceAbilitySystem->MakeEffectContext();
			StunContext.AddSourceObject(Character);
			FGameplayEffectSpecHandle StunSpec = SourceAbilitySystem->MakeOutgoingSpec(
				UAuraShockStunEffect::StaticClass(), 1.f, StunContext);
			if (StunSpec.IsValid())
			{
				StunSpec.Data->SetSetByCallerMagnitude(
					FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")),
					UAuraCombatBalanceData::Get().Shock.StunDuration);
				TargetAbilitySystem->ApplyGameplayEffectSpecToSelf(*StunSpec.Data.Get());
			}
		}
	}, 1.15f, false);

	FTimerHandle InterruptTimer;
	GetWorldTimerManager().SetTimer(InterruptTimer,
		[WeakCharacter, WeakShaman, InterruptedObserved]()
	{
		const AAuraCharacter* Character = WeakCharacter.Get();
		const AAuraEnemy* Shaman = WeakShaman.Get();
		const UAbilitySystemComponent* PlayerASC = Character
			? Character->GetAbilitySystemComponent() : nullptr;
		const float Health = PlayerASC
			? PlayerASC->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		*InterruptedObserved = Shaman && Shaman->IsStunned()
			&& !Shaman->IsRangedCasting() && FMath::IsNearlyEqual(Health, 100.f, 0.1f);
		UE_LOG(LogAuraInput, Display,
			TEXT("Shaman interrupt observation. Passed=%s Stunned=%s Casting=%s PlayerHealth=%.1f"),
			*InterruptedObserved ? TEXT("true") : TEXT("false"),
			Shaman && Shaman->IsStunned() ? TEXT("true") : TEXT("false"),
			Shaman && Shaman->IsRangedCasting() ? TEXT("true") : TEXT("false"), Health);
	}, 1.38f, false);

	FTimerHandle CooldownTimer;
	GetWorldTimerManager().SetTimer(CooldownTimer,
		[WeakShaman, CooldownObserved]()
	{
		AAuraEnemy* Shaman = WeakShaman.Get();
		UAbilitySystemComponent* AbilitySystem = Shaman
			? Shaman->GetAbilitySystemComponent() : nullptr;
		const bool bCooldownActive = AbilitySystem && AbilitySystem->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Enemy.ShamanBolt")));
		*CooldownObserved = Shaman && !Shaman->IsStunned()
			&& !Shaman->IsRangedCasting() && bCooldownActive;
		UE_LOG(LogAuraInput, Display,
			TEXT("Shaman cooldown observation. Passed=%s Stunned=%s Casting=%s Cooldown=%s"),
			*CooldownObserved ? TEXT("true") : TEXT("false"),
			Shaman && Shaman->IsStunned() ? TEXT("true") : TEXT("false"),
			Shaman && Shaman->IsRangedCasting() ? TEXT("true") : TEXT("false"),
			bCooldownActive ? TEXT("true") : TEXT("false"));
		if (AbilitySystem)
		{
			if (FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromClass(
				UAuraShamanBoltAbility::StaticClass()))
			{
				if (UAuraShamanBoltAbility* Ability =
					Cast<UAuraShamanBoltAbility>(Spec->GetPrimaryInstance()))
				{
					Ability->ForceNextGroundBurstForTest();
				}
			}
		}
	}, 3.35f, false);

	FTimerHandle RecastTimer;
	GetWorldTimerManager().SetTimer(RecastTimer,
		[this, WeakShaman, RecastObserved, GroundBurstObserved]()
	{
		const AAuraEnemy* Shaman = WeakShaman.Get();
		*RecastObserved = Shaman && Shaman->IsRangedCasting();
		*GroundBurstObserved = Shaman && Shaman->IsRangedGroundBurstCasting();
		UE_LOG(LogAuraInput, Display,
			TEXT("Shaman recast observation. Passed=%s Casting=%s GroundBurst=%s"),
			*RecastObserved ? TEXT("true") : TEXT("false"),
			Shaman && Shaman->IsRangedCasting() ? TEXT("true") : TEXT("false"),
			*GroundBurstObserved ? TEXT("true") : TEXT("false"));
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 4.05f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[WeakCharacter, WeakShaman, RetreatObserved, TelegraphObserved,
			InterruptedObserved, CooldownObserved, RecastObserved, GroundBurstObserved]()
	{
		const AAuraCharacter* Character = WeakCharacter.Get();
		const AAuraEnemy* Shaman = WeakShaman.Get();
		const UAbilitySystemComponent* PlayerASC = Character
			? Character->GetAbilitySystemComponent() : nullptr;
		const float FinalHealth = PlayerASC
			? PlayerASC->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		const UAuraShamanBoltAbility* Ability = GetDefault<UAuraShamanBoltAbility>();
		const bool bDamageObserved = FMath::IsNearlyEqual(
			FinalHealth, 100.f - UAuraCombatBalanceData::Get().Shaman.GroundBurstDamage, 0.1f);
		const bool bConstantsValid = Ability
			&& FMath::IsNearlyEqual(Ability->GetDamage(), 20.f)
			&& FMath::IsNearlyEqual(Ability->GetWindupTime(), 0.8f)
			&& FMath::IsNearlyEqual(Ability->GetCooldownDuration(), 3.f)
			&& FMath::IsNearlyEqual(Ability->GetGroundBurstDamage(), 16.f)
			&& FMath::IsNearlyEqual(Ability->GetGroundBurstRadius(), 220.f)
			&& FMath::IsNearlyEqual(Ability->GetGroundBurstWindup(), 1.1f)
			&& FMath::IsNearlyEqual(Ability->GetGroundBurstCooldown(), 8.f);
		const bool bPassed = Shaman && Shaman->IsRangedCombatant()
			&& *RetreatObserved && *TelegraphObserved && *InterruptedObserved
			&& *CooldownObserved && *RecastObserved && *GroundBurstObserved
			&& bDamageObserved && bConstantsValid;
		UE_LOG(LogAuraInput, Display,
			TEXT("Shaman ranged smoke test completed. Passed=%s Retreat=%s Telegraph=%s Interrupted=%s Cooldown=%s Recast=%s GroundBurst=%s Damage=%s Constants=%s FinalHealth=%.1f"),
			bPassed ? TEXT("true") : TEXT("false"),
			*RetreatObserved ? TEXT("true") : TEXT("false"),
			*TelegraphObserved ? TEXT("true") : TEXT("false"),
			*InterruptedObserved ? TEXT("true") : TEXT("false"),
			*CooldownObserved ? TEXT("true") : TEXT("false"),
			*RecastObserved ? TEXT("true") : TEXT("false"),
			*GroundBurstObserved ? TEXT("true") : TEXT("false"),
			bDamageObserved ? TEXT("true") : TEXT("false"),
			bConstantsValid ? TEXT("true") : TEXT("false"), FinalHealth);
	}, 5.65f, false);
}

void AAuraPlayerController::AuraTestEnemyAttackVariety()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !PlayerAbilitySystem || !HasAuthority() || !GetWorld())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Enemy attack variety smoke test requires an authoritative player and ASC."));
		return;
	}

	AAuraEncounterVolume* OuterEncounter = nullptr;
	AAuraEncounterVolume* ShamanEncounter = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 1) OuterEncounter = *It;
		else if (It->GetEncounterOrder() == 2) ShamanEncounter = *It;
	}
	if (!OuterEncounter || !ShamanEncounter)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Enemy attack variety smoke test could not find encounters 1 and 2."));
		return;
	}
	if (OuterEncounter->GetEncounterState() == EAuraEncounterState::Dormant)
	{
		OuterEncounter->StartEncounter(AuraCharacter, true);
	}
	if (ShamanEncounter->GetEncounterState() == EAuraEncounterState::Dormant)
	{
		ShamanEncounter->StartEncounter(AuraCharacter, true);
	}

	AAuraEnemy* Ghoul = nullptr;
	AAuraEnemy* Goblin = nullptr;
	AAuraEnemy* Shroom = nullptr;
	AAuraEnemy* Shaman = nullptr;
	for (AAuraEnemy* Enemy : OuterEncounter->GetActiveEnemies())
	{
		if (!Enemy) continue;
		switch (Enemy->GetEnemyArchetype())
		{
		case EAuraEnemyArchetype::Ghoul: Ghoul = Enemy; break;
		case EAuraEnemyArchetype::GoblinSpearman: Goblin = Enemy; break;
		case EAuraEnemyArchetype::CaveShroom: Shroom = Enemy; break;
		default: break;
		}
	}
	for (AAuraEnemy* Enemy : ShamanEncounter->GetActiveEnemies())
	{
		if (Enemy && Enemy->GetEnemyArchetype() == EAuraEnemyArchetype::ShamanElite)
		{
			Shaman = Enemy;
			break;
		}
	}
	// The authored first room is now a pure goblin pack. Keep this combat-kit
	// regression independent of room population by spawning missing archetypes.
	const auto SpawnSmokeEnemy = [this, AuraCharacter](const TCHAR* ClassPath,
		const FVector& Offset) -> AAuraEnemy*
	{
		UClass* EnemyClass = LoadClass<AAuraEnemy>(nullptr, ClassPath);
		if (!EnemyClass) return nullptr;
		FActorSpawnParameters Parameters;
		Parameters.Owner = this;
		Parameters.Instigator = AuraCharacter;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		return GetWorld()->SpawnActor<AAuraEnemy>(EnemyClass,
			AuraCharacter->GetActorLocation() + Offset, FRotator::ZeroRotator, Parameters);
	};
	if (!Ghoul)
	{
		Ghoul = SpawnSmokeEnemy(
			TEXT("/Game/Aura/Blueprints/BP_AuraEnemy.BP_AuraEnemy_C"),
			FVector(170.f, 0.f, 0.f));
	}
	if (!Shroom)
	{
		Shroom = SpawnSmokeEnemy(
			TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom.BP_AuraShroom_C"),
			FVector(0.f, 170.f, 0.f));
	}
	if (Goblin)
	{
		AAuraEnemy* EncounterGoblin = Goblin;
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.Instigator = AuraCharacter;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const FVector TestLocation = AuraCharacter->GetActorLocation()
			+ FVector(-325.f, 0.f, 0.f);
		if (AAuraEnemy* TestGoblin = GetWorld()->SpawnActor<AAuraEnemy>(
			Goblin->GetClass(), TestLocation,
			(AuraCharacter->GetActorLocation() - TestLocation).Rotation(), SpawnParameters))
		{
			if (AAuraAIController* EncounterController =
				Cast<AAuraAIController>(EncounterGoblin->GetController()))
			{
				EncounterController->StopMovement();
				EncounterController->SetActorTickEnabled(false);
			}
			EncounterGoblin->GetCharacterMovement()->DisableMovement();
			Goblin = TestGoblin;
		}
	}
	if (Shaman)
	{
		AAuraEnemy* EncounterShaman = Shaman;
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.Instigator = AuraCharacter;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const FVector TestSpawnLocation = AuraCharacter->GetActorLocation() + FVector(700.f, 0.f, 0.f);
		if (AAuraEnemy* TestShaman = GetWorld()->SpawnActor<AAuraEnemy>(
			Shaman->GetClass(), TestSpawnLocation,
			(AuraCharacter->GetActorLocation() - TestSpawnLocation).Rotation(), SpawnParameters))
		{
			if (AAuraAIController* EncounterController =
				Cast<AAuraAIController>(EncounterShaman->GetController()))
			{
				EncounterController->StopMovement();
				EncounterController->SetActorTickEnabled(false);
			}
			EncounterShaman->GetCharacterMovement()->DisableMovement();
			Shaman = TestShaman;
		}
	}
	if (!Ghoul || !Goblin || !Shroom || !Shaman)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Enemy attack variety smoke test missing archetype. Ghoul=%s Goblin=%s Shroom=%s Shaman=%s"),
			*GetNameSafe(Ghoul), *GetNameSafe(Goblin), *GetNameSafe(Shroom), *GetNameSafe(Shaman));
		return;
	}

	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 1000.f);
	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 1000.f);
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();
	const FVector PlayerLocation = AuraCharacter->GetActorLocation();
	struct FEnemyPlacement
	{
		AAuraEnemy* Enemy;
		FVector Offset;
	};
	const FEnemyPlacement Placements[] = {
		{ Ghoul, FVector(170.f, 0.f, 0.f) },
		{ Goblin, FVector(-325.f, 0.f, 0.f) },
		{ Shroom, FVector(0.f, 170.f, 0.f) },
		{ Shaman, FVector(700.f, 0.f, 0.f) }
	};
	for (const FEnemyPlacement& Placement : Placements)
	{
		AAuraEnemy* Enemy = Placement.Enemy;
		if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AIController->StopMovement();
			AIController->SetActorTickEnabled(true);
		}
		Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Enemy->GetCharacterMovement()->StopMovementImmediately();
		const FVector EnemyLocation = PlayerLocation + Placement.Offset;
		Enemy->SetActorLocation(EnemyLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Enemy->SetActorRotation((PlayerLocation - EnemyLocation).Rotation());
	}

	struct FAttackVarietyState
	{
		bool bGhoulSwipe = false;
		bool bGhoulFrenzy = false;
		bool bGoblinJab = false;
		bool bGoblinJabTelegraphed = false;
		bool bGoblinLunge = false;
		bool bFarGoblinLunge = false;
		bool bFarGoblinJab = false;
		bool bShroomBash = false;
		bool bShroomSpore = false;
		bool bSporeExposureSeen = false;
		bool bShamanBolt = false;
		bool bShamanVolley = false;
	};
	const TSharedRef<FAttackVarietyState> State = MakeShared<FAttackVarietyState>();
	const TSharedRef<FTimerHandle> ObservationHandle = MakeShared<FTimerHandle>();
	const TWeakObjectPtr<AAuraEnemy> WeakGhoul(Ghoul);
	const TWeakObjectPtr<AAuraEnemy> WeakGoblin(Goblin);
	const TWeakObjectPtr<AAuraEnemy> WeakShroom(Shroom);
	const TWeakObjectPtr<AAuraEnemy> WeakShaman(Shaman);
	const bool bAbilitiesGranted =
		Ghoul->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UAuraMeleeAbility::StaticClass())
		&& Goblin->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UAuraMeleeAbility::StaticClass())
		&& Shroom->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UAuraMeleeAbility::StaticClass())
		&& Shaman->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UAuraShamanBoltAbility::StaticClass());

	FTimerDelegate ObserveDelegate;
	const TWeakObjectPtr<AAuraCharacter> WeakPlayer(AuraCharacter);
	ObserveDelegate.BindLambda([State, WeakGhoul, WeakGoblin, WeakShroom, WeakShaman, WeakPlayer]()
	{
		if (const AAuraCharacter* Player = WeakPlayer.Get())
			State->bSporeExposureSeen |= Player->HasSporeExposure();
		if (const AAuraEnemy* Enemy = WeakGhoul.Get())
		{
			State->bGhoulSwipe |= Enemy->GetMeleeAction() == EAuraMeleeAction::GhoulSwipe;
			State->bGhoulFrenzy |= Enemy->GetMeleeAction() == EAuraMeleeAction::GhoulFrenzy;
		}
		if (const AAuraEnemy* Enemy = WeakGoblin.Get())
		{
			const EAuraMeleeAction Action = Enemy->GetMeleeAction();
			const FAuraMeleeEnemyBalance& Balance = UAuraCombatBalanceData::Get().MeleeEnemy;
			const bool bOutsideJabReach = WeakPlayer.IsValid()
				&& FVector::Dist2D(Enemy->GetActorLocation(), WeakPlayer->GetActorLocation())
					> Balance.GoblinJabRange + Balance.GoblinJabStepDistance;
			State->bGoblinJab |= Action == EAuraMeleeAction::GoblinJab;
			State->bGoblinJabTelegraphed |= Action == EAuraMeleeAction::GoblinJab
				&& FMath::IsNearlyEqual(Enemy->GetMeleeTelegraphRadius(),
					Balance.GoblinJabRange + Balance.GoblinJabStepDistance);
			State->bGoblinLunge |= Action == EAuraMeleeAction::GoblinLunge;
			State->bFarGoblinLunge |= bOutsideJabReach
				&& Action == EAuraMeleeAction::GoblinLunge;
			State->bFarGoblinJab |= bOutsideJabReach
				&& Action == EAuraMeleeAction::GoblinJab;
		}
		if (const AAuraEnemy* Enemy = WeakShroom.Get())
		{
			State->bShroomBash |= Enemy->GetMeleeAction() == EAuraMeleeAction::ShroomBash;
			State->bShroomSpore |= Enemy->GetMeleeAction() == EAuraMeleeAction::ShroomSporeBurst;
		}
		if (const AAuraEnemy* Enemy = WeakShaman.Get())
		{
			State->bShamanBolt |= Enemy->IsRangedCasting()
				&& !Enemy->IsRangedVolleyCasting()
				&& !Enemy->IsRangedGroundBurstCasting();
			State->bShamanVolley |= Enemy->IsRangedVolleyCasting();
		}
	});
	GetWorldTimerManager().SetTimer(*ObservationHandle, ObserveDelegate, 0.04f, true, 0.04f);

	UE_LOG(LogAuraInput, Display,
		TEXT("Enemy attack variety smoke test started. Abilities=%s Ghoul=%s Goblin=%s Shroom=%s Shaman=%s"),
		bAbilitiesGranted ? TEXT("true") : TEXT("false"), *Ghoul->GetName(),
		*Goblin->GetName(), *Shroom->GetName(), *Shaman->GetName());

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, State, ObservationHandle, WeakGhoul, WeakGoblin, WeakShroom, WeakShaman,
			bAbilitiesGranted]()
	{
		GetWorldTimerManager().ClearTimer(*ObservationHandle);
		const AAuraCharacter* CurrentCharacter = Cast<AAuraCharacter>(GetPawn());
		const bool bEnemiesAlive = WeakGhoul.IsValid() && !WeakGhoul->IsDead()
			&& WeakGoblin.IsValid() && !WeakGoblin->IsDead()
			&& WeakShroom.IsValid() && !WeakShroom->IsDead()
			&& WeakShaman.IsValid() && !WeakShaman->IsDead();
		const bool bPassed = bAbilitiesGranted && bEnemiesAlive
			&& CurrentCharacter && !CurrentCharacter->IsDead()
			&& State->bGhoulSwipe && State->bGhoulFrenzy
			&& State->bGoblinJab && State->bGoblinJabTelegraphed
			&& State->bGoblinLunge && State->bFarGoblinLunge
			&& !State->bFarGoblinJab
			&& State->bShroomBash && State->bShroomSpore && State->bSporeExposureSeen
			&& State->bShamanBolt && State->bShamanVolley;
		UE_LOG(LogAuraInput, Display,
			TEXT("Enemy attack variety smoke test completed. Passed=%s Abilities=%s Alive=%s GhoulSwipe=%s GhoulFrenzy=%s GoblinJab=%s JabWarning=%s GoblinLunge=%s FarLunge=%s FarJab=%s ShroomBash=%s ShroomSpore=%s SporeExposure=%s ShamanBolt=%s ShamanVolley=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			bAbilitiesGranted ? TEXT("true") : TEXT("false"),
			bEnemiesAlive ? TEXT("true") : TEXT("false"),
			State->bGhoulSwipe ? TEXT("true") : TEXT("false"),
			State->bGhoulFrenzy ? TEXT("true") : TEXT("false"),
			State->bGoblinJab ? TEXT("true") : TEXT("false"),
			State->bGoblinJabTelegraphed ? TEXT("true") : TEXT("false"),
			State->bGoblinLunge ? TEXT("true") : TEXT("false"),
			State->bFarGoblinLunge ? TEXT("true") : TEXT("false"),
			State->bFarGoblinJab ? TEXT("true") : TEXT("false"),
			State->bShroomBash ? TEXT("true") : TEXT("false"),
			State->bShroomSpore ? TEXT("true") : TEXT("false"),
			State->bSporeExposureSeen ? TEXT("true") : TEXT("false"),
			State->bShamanBolt ? TEXT("true") : TEXT("false"),
			State->bShamanVolley ? TEXT("true") : TEXT("false"));
	}, 16.5f, false);
}

void AAuraPlayerController::AuraTestBossArsenal()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !PlayerAbilitySystem || !HasAuthority() || !GetWorld())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Boss arsenal smoke test requires an authoritative player and ASC."));
		return;
	}

	AAuraEncounterVolume* BossEncounter = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->IsFinalEncounter())
		{
			BossEncounter = *It;
			break;
		}
	}
	if (!BossEncounter)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Boss arsenal smoke test could not find final encounter."));
		return;
	}
	if (BossEncounter->GetEncounterState() == EAuraEncounterState::Dormant)
	{
		StartBossForLegacySmokeTest(BossEncounter, AuraCharacter);
	}

	AAuraEnemy* Boss = nullptr;
	for (AAuraEnemy* Enemy : BossEncounter->GetActiveEnemies())
	{
		if (Enemy && Enemy->IsBossEnemy())
		{
			Boss = Enemy;
			break;
		}
	}
	UAbilitySystemComponent* BossAbilitySystem = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	if (!Boss || !BossAbilitySystem)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Boss arsenal smoke test failed to spawn the Demon Lord."));
		return;
	}

	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 5000.f);
	PlayerAbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 5000.f);
	const auto PlacePlayer = [](AAuraCharacter* PlayerCharacter, AAuraEnemy* CurrentBoss,
		const float Distance)
	{
		if (!PlayerCharacter || !CurrentBoss) return;
		FVector Direction = CurrentBoss->GetActorForwardVector().GetSafeNormal2D();
		if (Direction.IsNearlyZero()) Direction = FVector::ForwardVector;
		FVector Location = CurrentBoss->GetActorLocation() + Direction * Distance;
		Location.Z = CurrentBoss->GetActorLocation().Z;
		PlayerCharacter->TeleportTo(Location,
			(CurrentBoss->GetActorLocation() - Location).Rotation(), false, true);
		PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
	};
	PlacePlayer(AuraCharacter, Boss, 245.f);

	struct FBossArsenalSmokeState
	{
		bool bClaw = false;
		bool bTail = false;
		bool bThrow = false;
		bool bSummon = false;
		bool bCharge = false;
		bool bSummonSpawned = false;
		bool bClawSectorGeometry = false;
		bool bChargeCorridorGeometry = false;
		bool bPhaseTwo = false;
	};
	const TSharedRef<FBossArsenalSmokeState> State = MakeShared<FBossArsenalSmokeState>();
	const TSharedRef<FTimerHandle> ObservationHandle = MakeShared<FTimerHandle>();
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakBoss(Boss);
	const bool bAbilityGranted =
		BossAbilitySystem->FindAbilitySpecFromClass(UAuraBossAbility::StaticClass()) != nullptr;
	const bool bSummonClassesConfigured = Boss->GetBossSummonClasses().Num() >= 3;

	FTimerDelegate ObserveDelegate;
	ObserveDelegate.BindLambda([State, WeakBoss]()
	{
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		if (!CurrentBoss) return;
		const EAuraBossAction Action = CurrentBoss->GetBossAction();
		State->bClaw |= Action == EAuraBossAction::ClawLeft
			|| Action == EAuraBossAction::ClawRight;
		State->bTail |= Action == EAuraBossAction::TailSpin;
		State->bThrow |= Action == EAuraBossAction::HellfireThrow;
		State->bSummon |= Action == EAuraBossAction::SummonMinions;
		State->bCharge |= Action == EAuraBossAction::DemonicCharge;
		State->bPhaseTwo |= CurrentBoss->GetBossPhase() == EAuraBossPhase::PhaseTwo;
		if (Action == EAuraBossAction::ClawLeft || Action == EAuraBossAction::ClawRight)
		{
			State->bClawSectorGeometry |= FMath::IsNearlyEqual(
				CurrentBoss->GetBossTelegraphRadius(), CurrentBoss->GetBossMeleeRange(), 1.f);
		}
		if (Action == EAuraBossAction::DemonicCharge)
		{
			State->bChargeCorridorGeometry |= FMath::IsNearlyEqual(
				CurrentBoss->GetBossTelegraphRadius(),
				UAuraCombatBalanceData::Get().Boss.ChargeHalfWidth, 1.f)
				&& FVector::Dist2D(CurrentBoss->GetActorLocation(),
					CurrentBoss->GetBossTelegraphLocation()) > 100.f;
		}
		for (TActorIterator<AAuraEnemy> It(CurrentBoss->GetWorld()); It; ++It)
		{
			const AAuraEnemy* Candidate = *It;
			if (Candidate && !Candidate->IsDead()
				&& Candidate->GetBossSummoner() == CurrentBoss)
			{
				State->bSummonSpawned = true;
				break;
			}
		}
	});
	GetWorldTimerManager().SetTimer(*ObservationHandle, ObserveDelegate, 0.04f, true, 0.04f);

	UE_LOG(LogAuraInput, Display,
		TEXT("Boss arsenal smoke test started. Boss=%s Ability=%s SummonClasses=%d"),
		*Boss->GetName(), bAbilityGranted ? TEXT("true") : TEXT("false"),
		Boss->GetBossSummonClasses().Num());

	FTimerHandle TransitionTimer;
	GetWorldTimerManager().SetTimer(TransitionTimer,
		[WeakCharacter, WeakBoss]()
	{
		ApplyEnemyDamageForSmokeTest(WeakCharacter.Get(), WeakBoss.Get(), 10000.f);
	}, 3.2f, false);

	const auto SchedulePlacement = [this, WeakCharacter, WeakBoss, PlacePlayer](
		const float Delay, const float Distance)
	{
		FTimerHandle PlacementTimer;
		GetWorldTimerManager().SetTimer(PlacementTimer,
			[WeakCharacter, WeakBoss, PlacePlayer, Distance]()
		{
			PlacePlayer(WeakCharacter.Get(), WeakBoss.Get(), Distance);
		}, Delay, false);
	};
	// Stay inside the boss room while remaining beyond tail range; 820 uu can
	// place the smoke-test target behind a wall, suppressing ranged casts.
	SchedulePlacement(4.9f, 550.f);
	SchedulePlacement(9.0f, 245.f);
	SchedulePlacement(13.0f, 550.f);
	SchedulePlacement(17.0f, 245.f);
	SchedulePlacement(21.0f, 550.f);
	SchedulePlacement(25.0f, 245.f);

	FTimerHandle DefeatTimer;
	GetWorldTimerManager().SetTimer(DefeatTimer,
		[WeakCharacter, WeakBoss]()
	{
		ApplyEnemyDamageForSmokeTest(WeakCharacter.Get(), WeakBoss.Get(), 10000.f);
	}, 27.5f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, State, ObservationHandle, WeakCharacter, WeakBoss,
			bAbilityGranted, bSummonClassesConfigured]()
	{
		GetWorldTimerManager().ClearTimer(*ObservationHandle);
		const AAuraCharacter* CurrentPlayer = WeakCharacter.Get();
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		const bool bPlayerAlive = CurrentPlayer && !CurrentPlayer->IsDead();
		const bool bBossDefeated = CurrentBoss && CurrentBoss->IsDead();
		bool bSummonsCleaned = CurrentBoss != nullptr;
		if (CurrentBoss)
		{
			for (TActorIterator<AAuraEnemy> It(CurrentBoss->GetWorld()); It; ++It)
			{
				if ((*It)->GetBossSummoner() == CurrentBoss)
				{
					bSummonsCleaned = false;
					break;
				}
			}
		}
		const bool bPassed = bAbilityGranted && bSummonClassesConfigured
			&& bPlayerAlive && bBossDefeated && bSummonsCleaned
			&& State->bPhaseTwo && State->bClaw && State->bTail && State->bThrow
			&& State->bSummon && State->bCharge && State->bSummonSpawned
			&& State->bClawSectorGeometry && State->bChargeCorridorGeometry;
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss arsenal smoke test completed. Passed=%s Ability=%s Classes=%s PlayerAlive=%s BossDefeated=%s SummonsCleaned=%s PhaseTwo=%s Claw=%s Tail=%s Throw=%s Summon=%s Charge=%s Minion=%s ClawSector=%s ChargeCorridor=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			bAbilityGranted ? TEXT("true") : TEXT("false"),
			bSummonClassesConfigured ? TEXT("true") : TEXT("false"),
			bPlayerAlive ? TEXT("true") : TEXT("false"),
			bBossDefeated ? TEXT("true") : TEXT("false"),
			bSummonsCleaned ? TEXT("true") : TEXT("false"),
			State->bPhaseTwo ? TEXT("true") : TEXT("false"),
			State->bClaw ? TEXT("true") : TEXT("false"),
			State->bTail ? TEXT("true") : TEXT("false"),
			State->bThrow ? TEXT("true") : TEXT("false"),
			State->bSummon ? TEXT("true") : TEXT("false"),
			State->bCharge ? TEXT("true") : TEXT("false"),
			State->bSummonSpawned ? TEXT("true") : TEXT("false"),
			State->bClawSectorGeometry ? TEXT("true") : TEXT("false"),
			State->bChargeCorridorGeometry ? TEXT("true") : TEXT("false"));
	}, 30.f, false);
}

void AAuraPlayerController::AuraTestBossBarrage()
{
	const bool bCancelSmoke = FParse::Param(
		FCommandLine::Get(), TEXT("AuraBossBarrageCancelSmoke"));
	const bool bCapture = FParse::Param(
		FCommandLine::Get(), TEXT("AuraBossBarrageCapture"));
	const FString ScreenshotDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
	if (bCapture) IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
	AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* PlayerASC = PlayerCharacter
		? PlayerCharacter->GetAbilitySystemComponent() : nullptr;
	if (!HasAuthority() || !GetWorld() || !PlayerASC)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Boss barrage smoke test completed. Passed=false Setup=player"));
		return;
	}

	AAuraEncounterVolume* BossEncounter = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->IsFinalEncounter()) { BossEncounter = *It; break; }
	}
	if (!BossEncounter)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Boss barrage smoke test completed. Passed=false Setup=encounter"));
		return;
	}
	if (BossEncounter->GetEncounterState() == EAuraEncounterState::Dormant)
	{
		StartBossForLegacySmokeTest(BossEncounter, PlayerCharacter);
	}
	AAuraEnemy* Boss = nullptr;
	for (AAuraEnemy* Enemy : BossEncounter->GetActiveEnemies())
	{
		if (Enemy && Enemy->IsBossEnemy()) { Boss = Enemy; break; }
	}
	UAbilitySystemComponent* BossASC = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	if (!BossASC || !BossASC->FindAbilitySpecFromClass(UAuraBossAbility::StaticClass()))
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Boss barrage smoke test completed. Passed=false Setup=ability"));
		return;
	}

	PlayerASC->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 5000.f);
	PlayerASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 5000.f);
	const auto PlacePlayer = [](AAuraCharacter* TestPawn, AAuraEnemy* CurrentBoss,
		const float Distance)
	{
		if (!TestPawn || !CurrentBoss) return;
		FVector Direction = CurrentBoss->GetActorForwardVector().GetSafeNormal2D();
		if (Direction.IsNearlyZero()) Direction = FVector::ForwardVector;
		FVector Location = CurrentBoss->GetActorLocation() + Direction * Distance;
		Location.Z = CurrentBoss->GetActorLocation().Z;
		TestPawn->TeleportTo(Location,
			(CurrentBoss->GetActorLocation() - Location).Rotation(), false, true);
		TestPawn->GetCharacterMovement()->StopMovementImmediately();
	};
	PlacePlayer(PlayerCharacter, Boss, 245.f);

	struct FBarrageSmokeState
	{
		bool bPhaseOneObserved = false;
		bool bPhaseTwoObserved = false;
		bool bBarrageStarted = false;
		bool bBarrageOnlyInPhaseTwo = true;
		bool bFanWarning = false;
		bool bChannelEnded = false;
		bool bSpawnFailed = false;
		bool bFacingWithinWarnedFan = true;
		bool bTargetShifted = false;
		bool bAimFollowedShiftedTarget = false;
		bool bCancelTriggered = false;
		bool bNoShotsAfterCancel = true;
		bool bCapturedWarning = false;
		bool bCapturedFiring = false;
		int32 MaxShots = 0;
		float FirstShotTime = -1.f;
		float LastShotTime = -1.f;
		FVector InitialFacing = FVector::ZeroVector;
	};
	const TSharedRef<FBarrageSmokeState> State = MakeShared<FBarrageSmokeState>();
	const TSharedRef<FTimerHandle> ObservationHandle = MakeShared<FTimerHandle>();
	const TWeakObjectPtr<AAuraCharacter> WeakPlayer(PlayerCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakBoss(Boss);
	FTimerDelegate Observe;
	Observe.BindLambda([State, WeakBoss, WeakPlayer, bCancelSmoke,
		bCapture, ScreenshotDirectory]()
	{
		AAuraEnemy* CurrentBoss = WeakBoss.Get();
		if (!CurrentBoss || !CurrentBoss->GetWorld()) return;
		const EAuraBossAction Action = CurrentBoss->GetBossAction();
		const EAuraBossPhase Phase = CurrentBoss->GetBossPhase();
		State->bPhaseOneObserved |= Phase == EAuraBossPhase::PhaseOne
			&& Action != EAuraBossAction::InfernalBarrage;
		State->bPhaseTwoObserved |= Phase == EAuraBossPhase::PhaseTwo;
		if (Action == EAuraBossAction::InfernalBarrage)
		{
			if (!State->bBarrageStarted)
			{
				State->InitialFacing = CurrentBoss->GetActorForwardVector().GetSafeNormal2D();
			}
			State->bBarrageStarted = true;
			State->bBarrageOnlyInPhaseTwo &= Phase == EAuraBossPhase::PhaseTwo;
			State->bFanWarning |= FMath::IsNearlyEqual(
				CurrentBoss->GetBossTelegraphRadius(),
				UAuraCombatBalanceData::Get().Boss.BarrageRange, 1.f)
				&& FVector::Dist2D(CurrentBoss->GetActorLocation(),
					CurrentBoss->GetBossTelegraphLocation()) > 400.f;
			State->bFacingWithinWarnedFan &= FMath::Abs(FMath::FindDeltaAngleDegrees(
				State->InitialFacing.Rotation().Yaw,
				CurrentBoss->GetActorRotation().Yaw))
				<= UAuraCombatBalanceData::Get().Boss.BarrageHalfAngleDegrees + 1.f;
			if (bCapture && !State->bCapturedWarning
				&& CurrentBoss->GetBossTelegraphRadius() > 0.f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(
					ScreenshotDirectory, TEXT("AuraBossBarrageWarning.png")), false, false);
				State->bCapturedWarning = true;
			}
		}
		else if (State->bBarrageStarted && Action == EAuraBossAction::None)
		{
			State->bChannelEnded = true;
		}
		UAbilitySystemComponent* CurrentASC = CurrentBoss->GetAbilitySystemComponent();
		FGameplayAbilitySpec* Spec = CurrentASC
			? CurrentASC->FindAbilitySpecFromClass(UAuraBossAbility::StaticClass()) : nullptr;
		UAuraBossAbility* Ability = Spec
			? Cast<UAuraBossAbility>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Ability) return;
		const int32 Shots = Ability->GetBarrageShotsFired();
		if (State->bCancelTriggered && Shots > 0) State->bNoShotsAfterCancel = false;
		if (Shots > 0 && State->FirstShotTime < 0.f)
		{
			State->FirstShotTime = CurrentBoss->GetWorld()->GetTimeSeconds();
		}
		if (Shots > State->MaxShots)
		{
			State->MaxShots = Shots;
			State->LastShotTime = CurrentBoss->GetWorld()->GetTimeSeconds();
		}
		State->bSpawnFailed |= Ability->DidBarrageProjectileSpawnFail();
		if (!bCancelSmoke && !State->bTargetShifted && Shots >= 4)
		{
			AAuraCharacter* CurrentPlayer = WeakPlayer.Get();
			if (CurrentPlayer)
			{
				const FVector Sideways = FVector::CrossProduct(
					FVector::UpVector, State->InitialFacing).GetSafeNormal2D();
				State->bTargetShifted = CurrentPlayer->TeleportTo(
					CurrentPlayer->GetActorLocation() + Sideways * 140.f,
					CurrentPlayer->GetActorRotation(), false, true);
				CurrentPlayer->GetCharacterMovement()->StopMovementImmediately();
			}
		}
		if (State->bTargetShifted && Shots >= 6)
		{
			State->bAimFollowedShiftedTarget |=
				FMath::Abs(Ability->GetBarrageAimYawOffset()) > 7.f;
		}
		if (bCapture && !State->bCapturedFiring && Shots >= 7)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(
				ScreenshotDirectory, TEXT("AuraBossBarrageFiring.png")), false, false);
			State->bCapturedFiring = true;
		}
		if (bCancelSmoke && !State->bCancelTriggered && Shots >= 4)
		{
			State->bCancelTriggered = ApplyEnemyStunForSmokeTest(
				WeakPlayer.Get(), CurrentBoss);
		}
	});
	GetWorldTimerManager().SetTimer(*ObservationHandle, Observe, 0.04f, true, 0.04f);

	FTimerHandle TransitionTimer;
	GetWorldTimerManager().SetTimer(TransitionTimer, [WeakPlayer, WeakBoss]()
	{
		ApplyEnemyDamageForSmokeTest(WeakPlayer.Get(), WeakBoss.Get(), 10000.f);
	}, 0.70f, false);
	FTimerHandle PlacementTimer;
	GetWorldTimerManager().SetTimer(PlacementTimer,
		[WeakPlayer, WeakBoss, PlacePlayer]()
	{
		PlacePlayer(WeakPlayer.Get(), WeakBoss.Get(), 550.f);
	}, 0.90f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, State, ObservationHandle, WeakBoss, bCancelSmoke]()
	{
		GetWorldTimerManager().ClearTimer(*ObservationHandle);
		const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
		const float ExpectedSpan = (Balance.BarrageShotCount - 1)
			* Balance.BarrageShotInterval;
		const float ActualSpan = State->LastShotTime - State->FirstShotTime;
		const bool bTiming = State->FirstShotTime >= 0.f
			&& FMath::IsNearlyEqual(ActualSpan, ExpectedSpan, 0.35f);
		const bool bBarrageOutcome = bCancelSmoke
			? (State->bCancelTriggered && State->bNoShotsAfterCancel
				&& State->MaxShots >= 4 && State->MaxShots < Balance.BarrageShotCount)
			: (State->MaxShots == Balance.BarrageShotCount && bTiming
				&& State->bTargetShifted && State->bAimFollowedShiftedTarget);
		const bool bPassed = State->bPhaseOneObserved && State->bPhaseTwoObserved
			&& State->bBarrageStarted && State->bBarrageOnlyInPhaseTwo
			&& State->bFanWarning && State->bChannelEnded
			&& !State->bSpawnFailed && State->bFacingWithinWarnedFan && bBarrageOutcome;
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss barrage smoke test completed. Passed=%s Mode=%s PhaseOne=%s PhaseTwo=%s PhaseGate=%s Warning=%s Shots=%d/%d Span=%.2f/%.2f SpawnFailed=%s FacingInFan=%s Shifted=%s Reaimed=%s Ended=%s Cancelled=%s NoPostCancelShots=%s Boss=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			bCancelSmoke ? TEXT("Cancel") : TEXT("Full"),
			State->bPhaseOneObserved ? TEXT("true") : TEXT("false"),
			State->bPhaseTwoObserved ? TEXT("true") : TEXT("false"),
			State->bBarrageOnlyInPhaseTwo ? TEXT("true") : TEXT("false"),
			State->bFanWarning ? TEXT("true") : TEXT("false"),
			State->MaxShots, Balance.BarrageShotCount, ActualSpan, ExpectedSpan,
			State->bSpawnFailed ? TEXT("true") : TEXT("false"),
			State->bFacingWithinWarnedFan ? TEXT("true") : TEXT("false"),
			State->bTargetShifted ? TEXT("true") : TEXT("false"),
			State->bAimFollowedShiftedTarget ? TEXT("true") : TEXT("false"),
			State->bChannelEnded ? TEXT("true") : TEXT("false"),
			State->bCancelTriggered ? TEXT("true") : TEXT("false"),
			State->bNoShotsAfterCancel ? TEXT("true") : TEXT("false"),
			*GetNameSafe(WeakBoss.Get()));
	}, 8.0f, false);
}

void AAuraPlayerController::AuraTestBossPhases()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AuraCharacter || !PlayerAbilitySystem || !HasAuthority())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Boss phase smoke test requires an authoritative Aura character with an ASC."));
		return;
	}

	TArray<AActor*> EncounterActors;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEncounterVolume::StaticClass(), EncounterActors);
	AAuraEncounterVolume* BossEncounter = nullptr;
	for (AActor* Actor : EncounterActors)
	{
		AAuraEncounterVolume* Encounter = Cast<AAuraEncounterVolume>(Actor);
		if (Encounter && Encounter->IsFinalEncounter())
		{
			BossEncounter = Encounter;
			break;
		}
	}
	if (!BossEncounter)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Boss phase smoke test could not find the final encounter."));
		return;
	}
	if (BossEncounter->GetEncounterState() == EAuraEncounterState::Dormant)
	{
		StartBossForLegacySmokeTest(BossEncounter, AuraCharacter);
	}

	AAuraEnemy* Boss = nullptr;
	for (AAuraEnemy* Enemy : BossEncounter->GetActiveEnemies())
	{
		if (Enemy && Enemy->IsBossEnemy())
		{
			Boss = Enemy;
			break;
		}
	}
	UAbilitySystemComponent* BossAbilitySystem = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	if (!Boss || !BossAbilitySystem)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Boss phase smoke test final encounter did not spawn a configured boss."));
		return;
	}

	PlayerAbilitySystem->SetNumericAttributeBase(
		UAuraAttributeSet::GetHealthAttribute(),
		PlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()));
	const FVector BossLocation = Boss->GetActorLocation();
	FVector PlayerLocation = BossLocation + Boss->GetActorForwardVector() * 245.f;
	PlayerLocation.Z = BossLocation.Z;
	AuraCharacter->TeleportTo(
		PlayerLocation, (BossLocation - PlayerLocation).Rotation(), false, true);
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();

	struct FBossPhaseSmokeState
	{
		EAuraBossAction FirstClaw = EAuraBossAction::None;
		bool bAlternatingClaws = false;
		bool bTransitionStarted = false;
		bool bTransitionProtected = false;
		bool bTransitionStunRejected = false;
		bool bPhaseTwoActive = false;
		bool bTailTelegraph = false;
		bool bTailInterrupted = false;
		bool bThrowTelegraph = false;
		bool bDeathPresentationStarted = false;
		bool bDeathRagdollActivated = false;
		bool bVictoryCinematicStarted = false;
		bool bVictoryOverlayRevealed = false;
	};
	const TSharedRef<FBossPhaseSmokeState> TestState = MakeShared<FBossPhaseSmokeState>();
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakBoss(Boss);
	const TWeakObjectPtr<AAuraEncounterVolume> WeakEncounter(BossEncounter);
	const float ExpectedThresholdHealth =
		BossAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute())
		* Boss->GetBossPhaseThreshold();
	const bool bAbilityGranted =
		BossAbilitySystem->FindAbilitySpecFromClass(UAuraBossAbility::StaticClass()) != nullptr;

	UE_LOG(LogAuraInput, Display,
		TEXT("Boss phase smoke test started. Boss=%s Health=%.1f Threshold=%.1f Transition=%.2fs AbilityGranted=%s"),
		*Boss->GetName(),
		BossAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()),
		ExpectedThresholdHealth, Boss->GetBossPhaseTransitionDuration(),
		bAbilityGranted ? TEXT("true") : TEXT("false"));

	FTimerHandle FirstClawTimer;
	GetWorldTimerManager().SetTimer(FirstClawTimer, [WeakBoss, TestState]()
	{
		if (const AAuraEnemy* CurrentBoss = WeakBoss.Get())
		{
			TestState->FirstClaw = CurrentBoss->GetBossAction();
			UE_LOG(LogAuraInput, Display,
				TEXT("Boss first claw observation. Action=%d"),
				static_cast<int32>(TestState->FirstClaw));
		}
	}, 0.38f, false);

	FTimerHandle SecondClawAndTransitionTimer;
	GetWorldTimerManager().SetTimer(SecondClawAndTransitionTimer,
		[WeakCharacter, WeakBoss, TestState]()
	{
		AAuraCharacter* Character = WeakCharacter.Get();
		AAuraEnemy* CurrentBoss = WeakBoss.Get();
		if (!Character || !CurrentBoss) return;
		const EAuraBossAction SecondClaw = CurrentBoss->GetBossAction();
		const bool bFirstWasClaw = TestState->FirstClaw == EAuraBossAction::ClawLeft
			|| TestState->FirstClaw == EAuraBossAction::ClawRight;
		const bool bSecondWasClaw = SecondClaw == EAuraBossAction::ClawLeft
			|| SecondClaw == EAuraBossAction::ClawRight;
		TestState->bAlternatingClaws = bFirstWasClaw && bSecondWasClaw
			&& TestState->FirstClaw != SecondClaw;

		ApplyEnemyDamageForSmokeTest(Character, CurrentBoss, 10000.f);
		ApplyEnemyStunForSmokeTest(Character, CurrentBoss);
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss second claw observation. First=%d Second=%d Alternating=%s"),
			static_cast<int32>(TestState->FirstClaw), static_cast<int32>(SecondClaw),
			TestState->bAlternatingClaws ? TEXT("true") : TEXT("false"));
	}, 1.9f, false);

	FTimerHandle TransitionObservationTimer;
	GetWorldTimerManager().SetTimer(TransitionObservationTimer,
		[WeakCharacter, WeakBoss, TestState, ExpectedThresholdHealth]()
	{
		AAuraCharacter* Character = WeakCharacter.Get();
		AAuraEnemy* CurrentBoss = WeakBoss.Get();
		UAbilitySystemComponent* BossASC = CurrentBoss
			? CurrentBoss->GetAbilitySystemComponent() : nullptr;
		if (!Character || !CurrentBoss || !BossASC) return;

		const float Health = BossASC->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
		TestState->bTransitionStarted = CurrentBoss->IsBossTransitioning()
			&& FMath::IsNearlyEqual(Health, ExpectedThresholdHealth, 0.1f)
			&& BossASC->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("State.Boss.PhaseTransition")));
		TestState->bTransitionStunRejected = !CurrentBoss->IsStunned();
		ApplyEnemyDamageForSmokeTest(Character, CurrentBoss, 10000.f);
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss transition observation. Started=%s StunRejected=%s Health=%.1f"),
			TestState->bTransitionStarted ? TEXT("true") : TEXT("false"),
			TestState->bTransitionStunRejected ? TEXT("true") : TEXT("false"), Health);
	}, 2.06f, false);

	FTimerHandle TransitionProtectionTimer;
	GetWorldTimerManager().SetTimer(TransitionProtectionTimer,
		[WeakBoss, TestState, ExpectedThresholdHealth]()
	{
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		const UAbilitySystemComponent* BossASC = CurrentBoss
			? CurrentBoss->GetAbilitySystemComponent() : nullptr;
		const float Health = BossASC
			? BossASC->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		TestState->bTransitionProtected = CurrentBoss && CurrentBoss->IsBossTransitioning()
			&& !CurrentBoss->IsDead()
			&& FMath::IsNearlyEqual(Health, ExpectedThresholdHealth, 0.1f);
	}, 2.2f, false);

	FTimerHandle PhaseTwoAndTailTimer;
	GetWorldTimerManager().SetTimer(PhaseTwoAndTailTimer,
		[WeakCharacter, WeakBoss, TestState]()
	{
		AAuraCharacter* Character = WeakCharacter.Get();
		AAuraEnemy* CurrentBoss = WeakBoss.Get();
		UAbilitySystemComponent* BossASC = CurrentBoss
			? CurrentBoss->GetAbilitySystemComponent() : nullptr;
		if (!Character || !CurrentBoss || !BossASC) return;

		TestState->bPhaseTwoActive = CurrentBoss->GetBossPhase() == EAuraBossPhase::PhaseTwo
			&& BossASC->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("State.Boss.PhaseTwo")))
			&& FMath::IsNearlyEqual(CurrentBoss->GetCharacterMovement()->MaxWalkSpeed, 310.f, 0.1f);
		TestState->bTailTelegraph = CurrentBoss->GetBossAction() == EAuraBossAction::TailSpin
			&& FMath::IsNearlyEqual(
				CurrentBoss->GetBossTelegraphRadius(), CurrentBoss->GetBossTailSpinRadius(), 0.1f);
		if (TestState->bTailTelegraph)
		{
			ApplyEnemyStunForSmokeTest(Character, CurrentBoss);
		}
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss phase-two observation. Active=%s TailTelegraph=%s Action=%d"),
			TestState->bPhaseTwoActive ? TEXT("true") : TEXT("false"),
			TestState->bTailTelegraph ? TEXT("true") : TEXT("false"),
			static_cast<int32>(CurrentBoss->GetBossAction()));
	}, 3.52f, false);

	FTimerHandle TailInterruptTimer;
	GetWorldTimerManager().SetTimer(TailInterruptTimer, [WeakBoss, TestState]()
	{
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		TestState->bTailInterrupted = CurrentBoss && TestState->bTailTelegraph
			&& CurrentBoss->IsStunned()
			&& CurrentBoss->GetBossAction() == EAuraBossAction::None
			&& CurrentBoss->GetBossTelegraphRadius() <= 0.f;
	}, 3.7f, false);

	FTimerHandle PrepareThrowTimer;
	GetWorldTimerManager().SetTimer(PrepareThrowTimer, [WeakCharacter, WeakBoss]()
	{
		AAuraCharacter* Character = WeakCharacter.Get();
		AAuraEnemy* CurrentBoss = WeakBoss.Get();
		if (!Character || !CurrentBoss) return;
		FVector FarLocation = CurrentBoss->GetActorLocation()
			+ CurrentBoss->GetActorForwardVector() * 760.f;
		FarLocation.Z = CurrentBoss->GetActorLocation().Z;
		Character->TeleportTo(FarLocation,
			(CurrentBoss->GetActorLocation() - FarLocation).Rotation(), false, true);
		Character->GetCharacterMovement()->StopMovementImmediately();
	}, 4.85f, false);

	FTimerHandle ThrowObservationTimer;
	GetWorldTimerManager().SetTimer(ThrowObservationTimer, [WeakBoss, TestState]()
	{
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		TestState->bThrowTelegraph = CurrentBoss
			&& CurrentBoss->GetBossAction() == EAuraBossAction::HellfireThrow
			&& FMath::IsNearlyEqual(CurrentBoss->GetBossTelegraphRadius(),
				CurrentBoss->GetBossThrowWarningRadius(), 0.1f);
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss throw observation. Passed=%s Action=%d Radius=%.1f"),
			TestState->bThrowTelegraph ? TEXT("true") : TEXT("false"),
			CurrentBoss ? static_cast<int32>(CurrentBoss->GetBossAction()) : -1,
			CurrentBoss ? CurrentBoss->GetBossTelegraphRadius() : -1.f);
	}, 5.28f, false);

	FTimerHandle DefeatBossTimer;
	GetWorldTimerManager().SetTimer(DefeatBossTimer, [WeakCharacter, WeakBoss]()
	{
		ApplyEnemyDamageForSmokeTest(WeakCharacter.Get(), WeakBoss.Get(), 10000.f);
	}, 6.25f, false);

	FTimerHandle DeathStartTimer;
	GetWorldTimerManager().SetTimer(DeathStartTimer,
		[WeakController, WeakBoss, TestState]()
	{
		const AAuraPlayerController* Controller = WeakController.Get();
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		TestState->bDeathPresentationStarted = CurrentBoss && CurrentBoss->IsDead()
			&& CurrentBoss->HasBossDeathPresentationStarted();
		TestState->bVictoryCinematicStarted = Controller && Controller->IsDemoCompleted()
			&& Controller->IsVictoryCinematicActive() && Controller->HasVictoryCamera()
			&& !Controller->IsVictoryOverlayVisible();
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss death opening observation. Death=%s Cinematic=%s Camera=%s OverlayHidden=%s"),
			TestState->bDeathPresentationStarted ? TEXT("true") : TEXT("false"),
			TestState->bVictoryCinematicStarted ? TEXT("true") : TEXT("false"),
			Controller && Controller->HasVictoryCamera() ? TEXT("true") : TEXT("false"),
			Controller && !Controller->IsVictoryOverlayVisible() ? TEXT("true") : TEXT("false"));
	}, 6.52f, false);

	FTimerHandle DeathRagdollTimer;
	GetWorldTimerManager().SetTimer(DeathRagdollTimer,
		[this, WeakBoss, TestState]()
	{
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		TestState->bDeathRagdollActivated = CurrentBoss
			&& CurrentBoss->IsBossDeathRagdollActive();
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss death ragdoll observation. Passed=%s"),
			TestState->bDeathRagdollActivated ? TEXT("true") : TEXT("false"));
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 7.85f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakController, WeakBoss, WeakEncounter, TestState, bAbilityGranted]()
	{
		const AAuraPlayerController* Controller = WeakController.Get();
		const AAuraEnemy* CurrentBoss = WeakBoss.Get();
		const AAuraEncounterVolume* Encounter = WeakEncounter.Get();
		TestState->bVictoryOverlayRevealed = Controller
			&& Controller->IsVictoryOverlayVisible()
			&& !Controller->IsVictoryCinematicActive();
		const UAuraBossAbility* BossAbility = GetDefault<UAuraBossAbility>();
		const bool bNumbersValid = BossAbility
			&& FMath::IsNearlyEqual(BossAbility->GetPhaseOneClawDamage(), 18.f)
			&& FMath::IsNearlyEqual(BossAbility->GetPhaseTwoClawDamage(), 22.f)
			&& FMath::IsNearlyEqual(BossAbility->GetTailSpinDamage(), 28.f)
			&& FMath::IsNearlyEqual(BossAbility->GetThrowDamage(), 24.f)
			&& FMath::IsNearlyEqual(BossAbility->GetTailSpinWindup(), 0.95f)
			&& FMath::IsNearlyEqual(BossAbility->GetThrowWindup(), 0.82f)
			&& CurrentBoss
			&& FMath::IsNearlyEqual(CurrentBoss->GetBossDeathRagdollDelay(), 1.45f)
			&& Controller
			&& FMath::IsNearlyEqual(Controller->GetVictoryOverlayDelay(), 3.2f);
		const bool bDefeated = CurrentBoss && CurrentBoss->IsDead()
			&& Encounter && Encounter->GetEncounterState() == EAuraEncounterState::Completed
			&& Controller && Controller->IsDemoCompleted();
		const bool bPassed = bAbilityGranted && TestState->bAlternatingClaws
			&& TestState->bTransitionStarted && TestState->bTransitionProtected
			&& TestState->bTransitionStunRejected && TestState->bPhaseTwoActive
			&& TestState->bTailTelegraph && TestState->bTailInterrupted
			&& TestState->bThrowTelegraph && TestState->bDeathPresentationStarted
			&& TestState->bDeathRagdollActivated && TestState->bVictoryCinematicStarted
			&& TestState->bVictoryOverlayRevealed && bNumbersValid && bDefeated;
		UE_LOG(LogAuraInput, Display,
			TEXT("Boss phase smoke test completed. Passed=%s Ability=%s Claws=%s Transition=%s Protected=%s StunRejected=%s PhaseTwo=%s Tail=%s Interrupted=%s Throw=%s Death=%s Ragdoll=%s Cinematic=%s Overlay=%s Numbers=%s Defeated=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			bAbilityGranted ? TEXT("true") : TEXT("false"),
			TestState->bAlternatingClaws ? TEXT("true") : TEXT("false"),
			TestState->bTransitionStarted ? TEXT("true") : TEXT("false"),
			TestState->bTransitionProtected ? TEXT("true") : TEXT("false"),
			TestState->bTransitionStunRejected ? TEXT("true") : TEXT("false"),
			TestState->bPhaseTwoActive ? TEXT("true") : TEXT("false"),
			TestState->bTailTelegraph ? TEXT("true") : TEXT("false"),
			TestState->bTailInterrupted ? TEXT("true") : TEXT("false"),
			TestState->bThrowTelegraph ? TEXT("true") : TEXT("false"),
			TestState->bDeathPresentationStarted ? TEXT("true") : TEXT("false"),
			TestState->bDeathRagdollActivated ? TEXT("true") : TEXT("false"),
			TestState->bVictoryCinematicStarted ? TEXT("true") : TEXT("false"),
			TestState->bVictoryOverlayRevealed ? TEXT("true") : TEXT("false"),
			bNumbersValid ? TEXT("true") : TEXT("false"),
			bDefeated ? TEXT("true") : TEXT("false"));
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 9.62f, false);
}

void AAuraPlayerController::AuraTestMeleeSync()
{
	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	AAuraEnemy* Enemy = nullptr;
	for (AActor* Candidate : Enemies)
	{
		AAuraEnemy* CandidateEnemy = Cast<AAuraEnemy>(Candidate);
		if (CandidateEnemy
			&& CandidateEnemy->GetEnemyArchetype() == EAuraEnemyArchetype::Ghoul)
		{
			Enemy = CandidateEnemy;
			break;
		}
	}
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!Enemy || !AuraCharacter) return;

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
	{
		AIController->StopMovement();
	}
	const FRotator CameraYaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Forward = CameraYaw.Vector();
	Enemy->SetActorLocation(AuraCharacter->GetActorLocation() + Forward * 180.f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Enemy->SetActorRotation((AuraCharacter->GetActorLocation() - Enemy->GetActorLocation()).Rotation());
	const UAbilitySystemComponent* PlayerAbilitySystem = AuraCharacter->GetAbilitySystemComponent();
	const float InitialHealth = PlayerAbilitySystem
		? PlayerAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
	UE_LOG(LogAuraInput, Display, TEXT("Melee sync smoke test started. InitialHealth=%.1f"), InitialHealth);

	FTimerHandle CaptureTimer;
	GetWorldTimerManager().SetTimer(CaptureTimer, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1"));
	}, 1.08f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [this, InitialHealth]()
	{
		const AAuraCharacter* CurrentCharacter = Cast<AAuraCharacter>(GetPawn());
		const UAbilitySystemComponent* AbilitySystem = CurrentCharacter
			? CurrentCharacter->GetAbilitySystemComponent() : nullptr;
		const float FinalHealth = AbilitySystem
			? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		UE_LOG(LogAuraInput, Display,
			TEXT("Melee sync smoke test completed. Passed=%s InitialHealth=%.1f FinalHealth=%.1f"),
			FMath::IsNearlyEqual(InitialHealth - FinalHealth, 15.f) ? TEXT("true") : TEXT("false"),
			InitialHealth, FinalHealth);
	}, 1.25f, false);
}

void AAuraPlayerController::Move(const FInputActionValue& InputActionValue)
{
	if (bDemoCompleted || bIsRunUpgradeSelectionActive || bInventoryOpen) return;
	APawn* ControlledPawn = GetPawn();
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(ControlledPawn);
	if (!ControlledPawn || (AuraCharacter && AuraCharacter->IsDead())) return;

	const FVector2D Input = InputActionValue.Get<FVector2D>();
	if (bRescueInputHeld && !Input.IsNearlyZero()) CancelCoopRescue();
	const UCameraComponent* Camera = AuraCharacter ? AuraCharacter->GetFollowCamera() : nullptr;
	FVector Forward = Camera ? Camera->GetForwardVector() : FVector::ForwardVector;
	FVector Right = Camera ? Camera->GetRightVector() : FVector::RightVector;
	Forward.Z = 0.f;
	Right.Z = 0.f;
	ControlledPawn->AddMovementInput(Forward.GetSafeNormal(), Input.Y);
	ControlledPawn->AddMovementInput(Right.GetSafeNormal(), Input.X);
}

void AAuraPlayerController::Look(const FInputActionValue& InputActionValue)
{
	if (bDemoCompleted || bIsRunUpgradeSelectionActive || bInventoryOpen) return;
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if ((AuraCharacter && AuraCharacter->IsDead()) || bIsArcaneShardsTargeting || bIsShockTargeting) return;
	const FVector2D Input = InputActionValue.Get<FVector2D>();
	if (!IsInputKeyDown(EKeys::RightMouseButton)) return;
	AddYawInput(Input.X);
}

void AAuraPlayerController::PrimaryAttack()
{
	if (bDemoCompleted || bIsRunUpgradeSelectionActive || bInventoryOpen) return;
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (AuraCharacter && AuraCharacter->IsDead()) return;
	if (bIsArcaneShardsTargeting)
	{
		ConfirmArcaneShardsTargeting();
		return;
	}
	if (bIsShockTargeting)
	{
		ConfirmShockTargeting();
		return;
	}
	UAbilitySystemComponent* AbilitySystem = AuraCharacter ? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem) return;

	const bool bActivated = AbilitySystem->TryActivateAbilityByClass(UAuraFireBoltAbility::StaticClass());
	if (!bActivated)
	{
		bHasPendingFireBoltClick = false;
		PendingFireBoltClickTarget.Reset();
	}
	UE_LOG(LogAuraInput, Display, TEXT("Primary attack requested. Activated=%s"), bActivated ? TEXT("true") : TEXT("false"));
	if (!bActivated)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			const float Mana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
			const float ManaCost = GetDefault<UAuraFireBoltAbility>()->GetManaCost();
			AuraHUD->ShowStatusMessage(
				Mana < ManaCost ? TEXT("法力不足") : TEXT("火球术冷却中"),
				Mana < ManaCost ? FLinearColor(0.3f, 0.55f, 1.f) : FLinearColor(1.f, 0.65f, 0.12f));
		}
	}
}

void AAuraPlayerController::ToggleArcaneShardsTargeting()
{
	if (bDemoCompleted || bIsRunUpgradeSelectionActive || bInventoryOpen) return;
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || AuraCharacter->IsDead()) return;
	if (bIsArcaneShardsTargeting)
	{
		CancelArcaneShardsTargeting();
		return;
	}
	UAbilitySystemComponent* AbilitySystem = AuraCharacter ? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem) return;
	if (AuraCharacter->GetSkillRank(EAuraPlayerSkill::ArcaneShards) <= 0)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			AuraHUD->ShowStatusMessage(TEXT("尚未学习奥术碎片"), FLinearColor(0.72f, 0.35f, 1.f));
		}
		return;
	}

	const float Mana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const float ManaCost = GetDefault<UAuraArcaneShardsAbility>()->GetManaCost();
	const bool bCooldownActive = AbilitySystem->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Cooldown.ArcaneShards")));
	if (Mana < ManaCost || bCooldownActive)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			AuraHUD->ShowStatusMessage(
				Mana < ManaCost ? TEXT("法力不足") : TEXT("奥术碎片冷却中"),
				Mana < ManaCost ? FLinearColor(0.3f, 0.55f, 1.f) : FLinearColor(0.72f, 0.35f, 1.f));
		}
		UE_LOG(LogAuraInput, Display,
			TEXT("Arcane Shards targeting rejected. Mana=%.1f Cost=%.1f Cooldown=%s"),
			Mana, ManaCost, bCooldownActive ? TEXT("true") : TEXT("false"));
		return;
	}
	if (bIsShockTargeting) CancelShockTargeting();

	bIsArcaneShardsTargeting = true;
	bArcaneShardsPreviewValid = false;
	bArcaneShardsTargetClamped = false;
	bHasCommittedArcaneShardsTarget = false;
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	SetIgnoreLookInput(true);

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	SetInputMode(InputMode);
	if (GetWorld() && GetWorld()->GetGameViewport())
	{
		// SceneViewport forwards an uncaptured visible-cursor click only for a temporary
		// capture mode. NoCapture made LMB delivery depend on stale viewport focus/capture.
		GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
	}

	UpdateArcaneShardsTargeting();
	UE_LOG(LogAuraInput, Display, TEXT("Arcane Shards targeting started. Mode=%s"),
		GetCameraModeName(GetCameraMode()));
}

void AAuraPlayerController::ConfirmArcaneShardsTargeting()
{
	if (!bIsArcaneShardsTargeting) return;
	// Mouse capture can shift the raw cursor on LMB. Commit the last visible preview.
	if (!bArcaneShardsPreviewValid)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			AuraHUD->ShowStatusMessage(TEXT("请选择有效的地面目标"), FLinearColor(1.f, 0.42f, 0.12f));
		}
		return;
	}

	CommittedArcaneShardsTarget = ArcaneShardsPreviewLocation;
	bHasCommittedArcaneShardsTarget = true;
	bIsArcaneShardsTargeting = false;
	ApplyCursorMode(GetCameraMode());
	UE_LOG(LogAuraInput, Display,
		TEXT("Arcane Shards target confirmed. Location=%s Clamped=%s"),
		*CommittedArcaneShardsTarget.ToCompactString(),
		bArcaneShardsTargetClamped ? TEXT("true") : TEXT("false"));
	ActivateArcaneShards();
}

void AAuraPlayerController::CancelArcaneShardsTargeting()
{
	if (!bIsArcaneShardsTargeting) return;
	bIsArcaneShardsTargeting = false;
	bArcaneShardsPreviewValid = false;
	bArcaneShardsTargetClamped = false;
	bHasCommittedArcaneShardsTarget = false;
	ApplyCursorMode(GetCameraMode());
	UE_LOG(LogAuraInput, Display, TEXT("Arcane Shards targeting canceled."));
}

void AAuraPlayerController::ActivateArcaneShards()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter ? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem || !bHasCommittedArcaneShardsTarget) return;

	const bool bActivated = AbilitySystem->TryActivateAbilityByClass(UAuraArcaneShardsAbility::StaticClass());
	bHasCommittedArcaneShardsTarget = false;
	UE_LOG(LogAuraInput, Display, TEXT("Ability 1 requested. Ability=ArcaneShards Activated=%s"),
		bActivated ? TEXT("true") : TEXT("false"));
	if (!bActivated)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			const float Mana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
			const float ManaCost = GetDefault<UAuraArcaneShardsAbility>()->GetManaCost();
			AuraHUD->ShowStatusMessage(
				Mana < ManaCost ? TEXT("法力不足") : TEXT("奥术碎片冷却中"),
				Mana < ManaCost ? FLinearColor(0.3f, 0.55f, 1.f) : FLinearColor(0.72f, 0.35f, 1.f));
		}
	}
}

void AAuraPlayerController::ActivateShock()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter ? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem || !bHasCommittedShockDirection) return;
	const bool bActivated = AbilitySystem->TryActivateAbilityByClass(UAuraShockAbility::StaticClass());
	bHasCommittedShockDirection = false;
	UE_LOG(LogAuraInput, Display, TEXT("Ability 2 requested. Ability=Shock Activated=%s"),
		bActivated ? TEXT("true") : TEXT("false"));
	if (!bActivated)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			const float Mana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
			const float ManaCost = GetDefault<UAuraShockAbility>()->GetManaCost();
			AuraHUD->ShowStatusMessage(
				Mana < ManaCost ? TEXT("法力不足") : TEXT("闪电术冷却中"),
				Mana < ManaCost ? FLinearColor(0.3f, 0.55f, 1.f) : FLinearColor(0.2f, 0.85f, 1.f));
		}
	}
}

void AAuraPlayerController::ToggleShockTargeting()
{
	if (bDemoCompleted || bIsRunUpgradeSelectionActive || bInventoryOpen) return;
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || AuraCharacter->IsDead()) return;
	if (bIsShockTargeting)
	{
		CancelShockTargeting();
		return;
	}

	UAbilitySystemComponent* AbilitySystem = AuraCharacter->GetAbilitySystemComponent();
	if (!AbilitySystem) return;
	if (AuraCharacter->GetSkillRank(EAuraPlayerSkill::Shock) <= 0)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			AuraHUD->ShowStatusMessage(TEXT("尚未学习闪电术"), FLinearColor(0.2f, 0.85f, 1.f));
		}
		return;
	}
	const float Mana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const float ManaCost = GetDefault<UAuraShockAbility>()->GetManaCost();
	const bool bCooldownActive = AbilitySystem->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Shock")));
	if (Mana < ManaCost || bCooldownActive)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			AuraHUD->ShowStatusMessage(
				Mana < ManaCost ? TEXT("法力不足") : TEXT("闪电术冷却中"),
				Mana < ManaCost ? FLinearColor(0.3f, 0.55f, 1.f) : FLinearColor(0.2f, 0.85f, 1.f));
		}
		UE_LOG(LogAuraInput, Display, TEXT("Shock targeting rejected. Mana=%.1f Cost=%.1f Cooldown=%s"),
			Mana, ManaCost, bCooldownActive ? TEXT("true") : TEXT("false"));
		return;
	}

	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	bIsShockTargeting = true;
	bShockPreviewValid = false;
	bShockTargetClamped = false;
	ShockPreviewDirection = FVector::ZeroVector;
	bHasCommittedShockDirection = false;
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	SetIgnoreLookInput(true);

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	SetInputMode(InputMode);
	if (GetWorld() && GetWorld()->GetGameViewport())
	{
		GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
	}

	UpdateShockTargeting();
	UE_LOG(LogAuraInput, Display, TEXT("Shock direction targeting started. Mode=%s"),
		GetCameraModeName(GetCameraMode()));
}

void AAuraPlayerController::ConfirmShockTargeting()
{
	if (!bIsShockTargeting) return;

	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	const FVector Direction = ShockPreviewDirection.GetSafeNormal2D();
	const bool bCanCast = AuraCharacter && bShockPreviewValid && !Direction.IsNearlyZero();
	if (!bCanCast)
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
		{
			AuraHUD->ShowStatusMessage(TEXT("请选择施法方向"), FLinearColor(1.f, 0.42f, 0.12f));
		}
		return;
	}

	CommittedShockDirection = Direction;
	if (AuraCharacter->GetSkillRankState().ShockPath == EAuraShockPath::ThunderJudgment)
		CommittedShockDirection = ShockPreviewLocation;
	bHasCommittedShockDirection = true;
	AuraCharacter->SetActorRotation(Direction.Rotation());
	bIsShockTargeting = false;
	ApplyCursorMode(GetCameraMode());
	UE_LOG(LogAuraInput, Display, TEXT("Shock direction confirmed. Direction=%s Clamped=%s"),
		*CommittedShockDirection.ToCompactString(), bShockTargetClamped ? TEXT("true") : TEXT("false"));
	ActivateShock();
}

void AAuraPlayerController::CancelShockTargeting()
{
	if (!bIsShockTargeting) return;
	bIsShockTargeting = false;
	bShockPreviewValid = false;
	bShockTargetClamped = false;
	ShockPreviewDirection = FVector::ZeroVector;
	bHasCommittedShockDirection = false;
	ApplyCursorMode(GetCameraMode());
	UE_LOG(LogAuraInput, Display, TEXT("Shock direction targeting canceled."));
}

void AAuraPlayerController::UpdateArcaneShardsTargeting()
{
	FVector TargetLocation;
	bArcaneShardsPreviewValid = FindGroundTargetUnderMouse(
		GetDefault<UAuraArcaneShardsAbility>()->GetCastRange(), TargetLocation, bArcaneShardsTargetClamped);
	if (bArcaneShardsPreviewValid) ArcaneShardsPreviewLocation = TargetLocation;
}

void AAuraPlayerController::UpdateShockTargeting()
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		bShockPreviewValid = false;
		ShockPreviewDirection = FVector::ZeroVector;
		return;
	}

	const float CastRange = GetDefault<UAuraShockAbility>()->GetCastRange();
	FVector CursorGroundLocation;
	bShockPreviewValid = FindGroundTargetUnderMouse(
		CastRange, CursorGroundLocation, bShockTargetClamped);
	if (!bShockPreviewValid) return;
	if (const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(ControlledPawn);
		AuraPawn && AuraPawn->GetSkillRankState().ShockPath == EAuraShockPath::ThunderJudgment)
	{
		ShockPreviewLocation = CursorGroundLocation;
		FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(JudgmentPreview), false, ControlledPawn);
		FHitResult Floor, Wall;
		bShockPreviewValid = FMath::Abs(CursorGroundLocation.Z-ControlledPawn->GetActorLocation().Z) <= 230.f
			&& GetWorld()->LineTraceSingleByObjectType(Floor,CursorGroundLocation+FVector(0,0,180),CursorGroundLocation-FVector(0,0,180),Objects,Query)
			&& Floor.ImpactNormal.Z >= .5f && FMath::Abs(Floor.ImpactPoint.Z-CursorGroundLocation.Z) <= 60.f
			&& !GetWorld()->LineTraceSingleByObjectType(Wall,ControlledPawn->GetActorLocation(),Floor.ImpactPoint+FVector(0,0,45),Objects,Query);
		if (!bShockPreviewValid) return;
		ShockPreviewDirection = (CursorGroundLocation - ControlledPawn->GetActorLocation()).GetSafeNormal2D();
		if (ShockPreviewDirection.IsNearlyZero()) ShockPreviewDirection = ControlledPawn->GetActorForwardVector();
		return;
	}

	FVector Direction = CursorGroundLocation - ControlledPawn->GetActorLocation();
	Direction.Z = 0.f;
	if (Direction.SizeSquared() < FMath::Square(10.f))
	{
		bShockPreviewValid = false;
		ShockPreviewDirection = FVector::ZeroVector;
		return;
	}

	ShockPreviewDirection = Direction.GetSafeNormal();
	ShockPreviewLocation = ControlledPawn->GetActorLocation() + ShockPreviewDirection * CastRange;
	ShockPreviewLocation.Z = CursorGroundLocation.Z;
	if (const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(ControlledPawn);
		AuraPawn && AuraPawn->GetSkillRankState().ShockPath == EAuraShockPath::ThunderJudgment)
		ShockPreviewLocation = CursorGroundLocation;
}

bool AAuraPlayerController::FindGroundTargetUnderMouse(
	const float MaxRange, FVector& OutLocation, bool& bOutClamped)
{
	const APawn* ControlledPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!ControlledPawn || !World || bForceInvalidGroundTargetForSmoke) return false;
	bOutClamped = false;

	FVector RawTarget = FVector::ZeroVector;
	if (bUseDebugGroundTargetOverride)
	{
		RawTarget = DebugGroundTargetOverride;
	}
	else
	{
		FHitResult CursorHit;
		if (GetHitResultUnderCursor(TargetingTraceChannel, false, CursorHit) && CursorHit.bBlockingHit)
		{
			RawTarget = CursorHit.ImpactPoint;
		}
		else
		{
			FVector WorldOrigin;
			FVector WorldDirection;
			if (!DeprojectMousePositionToWorld(WorldOrigin, WorldDirection)
				|| FMath::IsNearlyZero(WorldDirection.Z))
			{
				return false;
			}

			float GroundZ = ControlledPawn->GetActorLocation().Z;
			if (const ACharacter* GroundCharacter = Cast<ACharacter>(ControlledPawn))
			{
				GroundZ -= GroundCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			}
			const float PlaneDistance = (GroundZ - WorldOrigin.Z) / WorldDirection.Z;
			if (PlaneDistance <= 0.f) return false;
			RawTarget = WorldOrigin + WorldDirection * PlaneDistance;
		}
	}

	const FVector PawnLocation = ControlledPawn->GetActorLocation();
	FVector PlanarOffset = RawTarget - PawnLocation;
	PlanarOffset.Z = 0.f;
	bOutClamped = PlanarOffset.SizeSquared() > FMath::Square(MaxRange);
	if (bOutClamped)
	{
		PlanarOffset = PlanarOffset.GetSafeNormal() * MaxRange;
	}

	FVector GroundCandidate = PawnLocation + PlanarOffset;
	GroundCandidate.Z = RawTarget.Z;
	FHitResult GroundHit;
	FCollisionObjectQueryParams GroundObjectQuery;
	GroundObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(AuraAbilityCursorGround), false, ControlledPawn);
	const FVector TraceStart(GroundCandidate.X, GroundCandidate.Y,
		FMath::Max(PawnLocation.Z, GroundCandidate.Z) + 2000.f);
	const FVector TraceEnd = TraceStart - FVector(0.f, 0.f, 5000.f);
	if (World->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceEnd, GroundObjectQuery, GroundQuery))
	{
		GroundCandidate = GroundHit.ImpactPoint;
	}
	else if (const ACharacter* GroundCharacter = Cast<ACharacter>(ControlledPawn))
	{
		GroundCandidate.Z = PawnLocation.Z - GroundCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	GroundCandidate.Z += 3.f;
	OutLocation = GroundCandidate;
	return true;
}

void AAuraPlayerController::AuraTestNetworkAbilities()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	if (!IsLocalController() || !AuraCharacter || !AbilitySystem || AuraCharacter->IsDead())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Network ability smoke test requires a living locally controlled AuraCharacter."));
		return;
	}

	FVector Forward = AuraCharacter->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero()) Forward = FVector::ForwardVector;
	CommittedArcaneShardsTarget = AuraCharacter->GetActorLocation() + Forward * 420.f;
	bHasCommittedArcaneShardsTarget = true;
	const bool bArcaneRequested = AbilitySystem->TryActivateAbilityByClass(
		UAuraArcaneShardsAbility::StaticClass());
	bHasCommittedArcaneShardsTarget = false;
	UE_LOG(LogAuraInput, Display,
		TEXT("Network smoke client requested Arcane Shards. Activated=%s Target=%s"),
		bArcaneRequested ? TEXT("true") : TEXT("false"),
		*CommittedArcaneShardsTarget.ToCompactString());

	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	FTimerHandle ShockTimer;
	GetWorldTimerManager().SetTimer(ShockTimer, [WeakController]()
	{
		AAuraPlayerController* Controller = WeakController.Get();
		AAuraCharacter* Character = Controller
			? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
		UAbilitySystemComponent* CurrentAbilitySystem = Character
			? Character->GetAbilitySystemComponent() : nullptr;
		if (!Controller || !Character || !CurrentAbilitySystem || Character->IsDead()) return;

		Controller->CommittedShockDirection = Character->GetActorForwardVector()
			.RotateAngleAxis(30.f, FVector::UpVector).GetSafeNormal2D();
		Controller->bHasCommittedShockDirection = true;
		const bool bShockRequested = CurrentAbilitySystem->TryActivateAbilityByClass(
			UAuraShockAbility::StaticClass());
		Controller->bHasCommittedShockDirection = false;
		UE_LOG(LogAuraInput, Display,
			TEXT("Network smoke client requested Shock. Activated=%s Direction=%s"),
			bShockRequested ? TEXT("true") : TEXT("false"),
			*Controller->CommittedShockDirection.ToCompactString());
	}, 1.1f, false);
}

bool AAuraPlayerController::PrepareComboSmokeRanks(
	const TArray<EAuraComboMarkType>& RequiredCombos)
{
	if (!HasAuthority() || RequiredCombos.IsEmpty() || RequiredCombos.Num() > 3) return false;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	if (!State || !Run || Run->IsPersistentSaveEnabled()
		|| State->GetGrowthState().PaidLevel != 1
		|| State->GetGrowthState().TotalExperience != 0) return false;
	const int32 TargetLevel = RequiredCombos.Num() == 1 ? 3
		: (RequiredCombos.Num() == 2 ? 6 : 9);
	State->AwardExperienceAndSouls(
		UAuraCombatBalanceData::Get().Progression.GetCumulativeExperienceForLevel(TargetLevel),
		0);
	FString Failure;
	if (!State->TryAllocateSkill(EAuraPlayerSkill::ArcaneShards, Failure)
		|| !State->TryAllocateSkill(EAuraPlayerSkill::Shock, Failure)) return false;
	for (const EAuraComboMarkType Type : RequiredCombos)
		if (!State->TryAllocateCombo(Type, Failure)) return false;
	for (const EAuraComboMarkType Type : RequiredCombos)
	{
		if (State->GetGrowthState().ComboRanks.GetRank(Type) < 1) return false;
	}
	return AAuraPlayerState::IsValidGrowthSnapshot(State->GetGrowthState());
}

void AAuraPlayerController::AuraTestCombos()
{
	AAuraCharacter* Caster = Cast<AAuraCharacter>(GetPawn());
	if (!HasAuthority() || !Caster || Caster->IsDead() || !GetWorld()
		|| !PrepareComboSmokeRanks({EAuraComboMarkType::Ember, EAuraComboMarkType::Fracture}))
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_COMBO_SMOKE setup failed: authoritative live caster required"));
		return;
	}

	TArray<AAuraEnemy*> Targets;
	const FVector Origin = Caster->GetActorLocation() + FVector(600.f, 0.f, 2200.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const float Offset : { 0.f, 120.f, 290.f, 470.f })
	{
		AAuraEnemy* Enemy = GetWorld()->SpawnActor<AAuraEnemy>(AAuraEnemy::StaticClass(),
			Origin + FVector(0.f, Offset, 0.f), FRotator::ZeroRotator, Params);
		if (!Enemy || !Enemy->GetAbilitySystemComponent())
		{
			UE_LOG(LogAuraInput, Error, TEXT("AURA_COMBO_SMOKE setup failed: enemy spawn"));
			return;
		}
		Enemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AI->StopMovement();
			AI->SetActorTickEnabled(false);
		}
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetMaxHealthAttribute(), 1000.f);
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetHealthAttribute(), 1000.f);
		Targets.Add(Enemy);
	}
	AAuraEnemy* Primary = Targets[0];
	AAuraEnemy* Near = Targets[1];
	AAuraEnemy* Mid = Targets[2];
	AAuraEnemy* Far = Targets[3];
	const int32 FireCast = Caster->AllocateComboCastId();
	const int32 QCast = Caster->AllocateComboCastId();
	Caster->ApplyComboMark(Primary, EAuraComboMarkType::Ember, FireCast);
	const bool bEmberRecorded = Caster->HasComboMark(Primary, EAuraComboMarkType::Ember);
	const bool bWrongTargetBlocked = !Caster->TryConsumeComboMark(
		Near, EAuraComboMarkType::Ember, QCast);
	const bool bWrongOrderBlocked = !Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Fracture, QCast);
	const bool bConsumedOnce = Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Ember, QCast)
		&& !Caster->HasComboMark(Primary, EAuraComboMarkType::Ember);
	Caster->ApplyComboMark(Primary, EAuraComboMarkType::Ember,
		Caster->AllocateComboCastId());
	const bool bDuplicateCastBlocked = !Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Ember, QCast);
	const bool bLaterCastCanConsume = Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Ember, Caster->AllocateComboCastId());

	bool bOtherCasterIsolated = true;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AAuraCharacter* OtherCaster = Cast<AAuraCharacter>(It->Get() ? It->Get()->GetPawn() : nullptr);
		if (!OtherCaster || OtherCaster == Caster) continue;
		OtherCaster->ApplyComboMark(Primary, EAuraComboMarkType::Ember,
			OtherCaster->AllocateComboCastId());
		bOtherCasterIsolated = !Caster->HasComboMark(Primary, EAuraComboMarkType::Ember)
			&& !Caster->TryConsumeComboMark(Primary, EAuraComboMarkType::Ember,
				Caster->AllocateComboCastId());
		OtherCaster->TryConsumeComboMark(Primary, EAuraComboMarkType::Ember,
			OtherCaster->AllocateComboCastId());
		break;
	}

	const auto Health = [](const AAuraEnemy* Enemy)
	{
		return Enemy->GetAbilitySystemComponent()->GetNumericAttribute(
			UAuraAttributeSet::GetHealthAttribute());
	};
	const float PrimaryBefore = Health(Primary);
	const float NearBefore = Health(Near);
	const float MidBefore = Health(Mid);
	const float FarBefore = Health(Far);
	Caster->ExecuteEmberBurst(Primary->GetActorLocation(), UAuraCombatBalanceData::Get().ArcaneShards.Damage);
	const bool bBurstRadius = Health(Primary) < PrimaryBefore && Health(Near) < NearBefore
		&& FMath::IsNearlyEqual(Health(Mid), MidBefore)
		&& FMath::IsNearlyEqual(Health(Far), FarBefore);
	const float PrimaryAfterBurst = Health(Primary);
	const float NearAfterBurst = Health(Near);
	const float MidAfterBurst = Health(Mid);
	Caster->ExecuteFractureChain(Primary, UAuraCombatBalanceData::Get().Shock.Damage);
	const bool bChainLimit = FMath::IsNearlyEqual(Health(Primary), PrimaryAfterBurst)
		&& Health(Near) < NearAfterBurst && Health(Mid) < MidAfterBurst
		&& FMath::IsNearlyEqual(Health(Far), FarBefore)
		&& !Near->IsStunned() && !Mid->IsStunned()
		&& !Caster->HasComboMark(Near, EAuraComboMarkType::Fracture)
		&& !Caster->HasComboMark(Mid, EAuraComboMarkType::Fracture);

	Caster->ApplyComboMark(Primary, EAuraComboMarkType::Ember,
		Caster->AllocateComboCastId());
	const bool bVisibleDuringWindow = Caster->HasComboMark(Primary, EAuraComboMarkType::Ember);
	const bool bInitialPassed = bEmberRecorded && bWrongTargetBlocked && bWrongOrderBlocked
		&& bConsumedOnce && bDuplicateCastBlocked && bLaterCastCanConsume
		&& bOtherCasterIsolated && bBurstRadius && bChainLimit && bVisibleDuringWindow;
	const TWeakObjectPtr<AAuraCharacter> WeakCaster(Caster);
	const TWeakObjectPtr<AAuraEnemy> WeakPrimary(Primary);
	TArray<TWeakObjectPtr<AAuraEnemy>> WeakTargets;
	for (AAuraEnemy* Enemy : Targets) WeakTargets.Add(Enemy);
	FTimerHandle ExpiryTimer;
	GetWorldTimerManager().SetTimer(ExpiryTimer,
		[WeakCaster, WeakPrimary, WeakTargets, bInitialPassed, bEmberRecorded,
			bWrongTargetBlocked, bWrongOrderBlocked, bConsumedOnce, bDuplicateCastBlocked,
			bLaterCastCanConsume, bOtherCasterIsolated, bBurstRadius, bChainLimit]()
	{
		const bool bExpired = WeakCaster.IsValid() && WeakPrimary.IsValid()
			&& !WeakCaster->HasComboMark(WeakPrimary.Get(), EAuraComboMarkType::Ember)
			&& WeakCaster->GetComboMarks().IsEmpty();
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_COMBO_SMOKE Passed=%s Recorded=%s WrongTarget=%s WrongOrder=%s ConsumeOnce=%s DuplicateCast=%s LaterCast=%s OtherCaster=%s BurstRadius=%s ChainLimit=%s Expired=%s"),
			bInitialPassed && bExpired ? TEXT("true") : TEXT("false"),
			bEmberRecorded ? TEXT("true") : TEXT("false"),
			bWrongTargetBlocked ? TEXT("true") : TEXT("false"),
			bWrongOrderBlocked ? TEXT("true") : TEXT("false"),
			bConsumedOnce ? TEXT("true") : TEXT("false"),
			bDuplicateCastBlocked ? TEXT("true") : TEXT("false"),
			bLaterCastCanConsume ? TEXT("true") : TEXT("false"),
			bOtherCasterIsolated ? TEXT("true") : TEXT("false"),
			bBurstRadius ? TEXT("true") : TEXT("false"),
			bChainLimit ? TEXT("true") : TEXT("false"),
			bExpired ? TEXT("true") : TEXT("false"));
		for (const TWeakObjectPtr<AAuraEnemy>& Enemy : WeakTargets)
		{
			if (Enemy.IsValid()) Enemy->Destroy();
		}
	}, UAuraCombatBalanceData::Get().Combo.MarkDuration + 0.3f, false);
}

void AAuraPlayerController::AuraLogComboMarks()
{
	const AAuraCharacter* Caster = Cast<AAuraCharacter>(GetPawn());
	int32 Active = 0;
	if (Caster)
	{
		for (const FAuraComboMark& Mark : Caster->GetComboMarks())
		{
			if (IsValid(Mark.Target) && Mark.ExpireServerTime > Caster->GetComboServerTime()) ++Active;
		}
	}
	UE_LOG(LogAuraInput, Display,
		TEXT("AURA_COMBO_OBSERVE Local=%s Authority=%s Player=%s ActivePersonalMarks=%d"),
		IsLocalController() ? TEXT("true") : TEXT("false"),
		HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(Caster), Active);
}

void AAuraPlayerController::AuraTestSkillRanks()
{
	AAuraCharacter* Caster = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* ASC = Caster ? Caster->GetAbilitySystemComponent() : nullptr;
	if (!HasAuthority() || !Caster || !ASC || !GetWorld()
		|| !PrepareComboSmokeRanks({EAuraComboMarkType::Fracture}))
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_SKILL_RANK_SMOKE setup failed"));
		return;
	}
	const FAuraSkillRankState OriginalRanks = Caster->GetSkillRankState();
	const float BaseFireDamage = Caster->GetFireBoltDamageMultiplier();
	const float BaseQRadius = Caster->GetArcaneShardsRadiusMultiplier();
	const float BaseEDamage = Caster->GetShockDamageMultiplier();
	const bool bLock = Caster->SetSkillRank(EAuraPlayerSkill::ArcaneShards, 0)
		&& !ASC->FindAbilitySpecFromClass(UAuraArcaneShardsAbility::StaticClass())
		&& !ASC->TryActivateAbilityByClass(UAuraArcaneShardsAbility::StaticClass());
	const bool bUnlock = Caster->SetSkillRank(EAuraPlayerSkill::ArcaneShards, 1)
		&& ASC->FindAbilitySpecFromClass(UAuraArcaneShardsAbility::StaticClass()) != nullptr;
	const bool bRankTwo = Caster->SetSkillRank(EAuraPlayerSkill::ArcaneShards, 2)
		&& Caster->SetSkillRank(EAuraPlayerSkill::FireBolt, 2)
		&& Caster->SetSkillRank(EAuraPlayerSkill::Shock, 2)
		&& Caster->GetFireBoltDamageMultiplier() > BaseFireDamage
		&& Caster->GetArcaneShardsRadiusMultiplier() > BaseQRadius
		&& Caster->GetShockDamageMultiplier() > BaseEDamage
		&& ASC->FindAbilitySpecFromClass(UAuraArcaneShardsAbility::StaticClass())->Level == 2;
	const bool bInvalidRankRejected = !Caster->SetSkillRank(EAuraPlayerSkill::FireBolt, 5);
	const float RankTwoFireDamage = Caster->GetFireBoltDamageMultiplier();
	const bool bRankFour = Caster->SetSkillRank(EAuraPlayerSkill::FireBolt, 4)
		&& Caster->GetFireBoltDamageMultiplier() > RankTwoFireDamage;
	const bool bRankThree = Caster->SetSkillRank(EAuraPlayerSkill::FireBolt, 3)
		&& Caster->SetSkillRank(EAuraPlayerSkill::ArcaneShards, 3)
		&& Caster->SetSkillRank(EAuraPlayerSkill::Shock, 3);

	TArray<AAuraEnemy*> Enemies;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Origin = Caster->GetActorLocation() + FVector(500.f, 0.f, 2200.f);
	const TArray<FVector> Positions = {
		Origin, Origin + FVector(0.f, 130.f, 0.f), Origin + FVector(0.f, 260.f, 0.f),
		Origin + FVector(0.f, 1000.f, 0.f), Origin + FVector(200.f, 1000.f, 0.f),
		Origin + FVector(400.f, 1000.f, 0.f), Origin + FVector(600.f, 1000.f, 0.f)
	};
	UClass* ShamanClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman.BP_AuraEliteShaman_C"));
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		AAuraEnemy* Enemy = GetWorld()->SpawnActor<AAuraEnemy>(
			Index == 1 && ShamanClass ? ShamanClass : AAuraEnemy::StaticClass(),
			Positions[Index], FRotator::ZeroRotator, Params);
		if (!Enemy || !Enemy->GetAbilitySystemComponent())
		{
			UE_LOG(LogAuraInput, Error, TEXT("AURA_SKILL_RANK_SMOKE enemy spawn failed"));
			return;
		}
		Enemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AI->StopMovement();
			AI->SetActorTickEnabled(false);
		}
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetMaxHealthAttribute(), 1000.f);
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetHealthAttribute(), 1000.f);
		Enemies.Add(Enemy);
	}
	AAuraEnemy* Primary = Enemies[0];
	AAuraEnemy* Secondary = Enemies[1];
	Secondary->BeginRangedCast(Primary->GetActorLocation());
	const bool bWasTelegraphing = Secondary->IsRangedCasting();
	Caster->ExecuteFractureChain(Primary, UAuraCombatBalanceData::Get().Shock.Damage);
	const bool bInterruptOnly = bWasTelegraphing && !Secondary->IsRangedCasting()
		&& !Secondary->IsStunned() && Enemies[2]->GetAbilitySystemComponent()
			->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) < 1000.f;

	const FAuraSkillRankBalance& RankBalance = UAuraCombatBalanceData::Get().SkillRanks;
	const FTransform FieldTransform(FRotator::ZeroRotator, Primary->GetActorLocation());
	AAuraArcaneSlowField* Field = GetWorld()->SpawnActorDeferred<AAuraArcaneSlowField>(
		AAuraArcaneSlowField::StaticClass(), FieldTransform, Caster, Caster,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Field)
	{
		Field->Configure(Caster, RankBalance.ArcaneShardsFieldRadius,
			RankBalance.ArcaneShardsFieldDuration);
		UGameplayStatics::FinishSpawningActor(Field, FieldTransform);
	}
	const bool bSlowApplied = Field && Primary->GetAbilitySystemComponent()->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("State.Slowed")))
		&& Primary->GetCharacterMovement()->MaxWalkSpeed
			< UAuraCombatBalanceData::Get().MeleeEnemy.WalkSpeed;

	FActorSpawnParameters ProjectileParams;
	ProjectileParams.Owner = Caster;
	ProjectileParams.Instigator = Caster;
	ProjectileParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraFireBoltProjectile* Projectile = GetWorld()->SpawnActor<AAuraFireBoltProjectile>(
		AAuraFireBoltProjectile::StaticClass(), Positions[3] - FVector(100.f, 0.f, 0.f),
		FRotator::ZeroRotator, ProjectileParams);
	if (Projectile)
	{
		Projectile->SetDamage(10.f);
		Projectile->SetComboCastId(Caster->AllocateComboCastId());
		Projectile->SetExtraPierces(RankBalance.FireBoltExtraPierces);
	}
	const TWeakObjectPtr<AAuraCharacter> WeakCaster(Caster);
	TArray<TWeakObjectPtr<AAuraEnemy>> WeakEnemies;
	for (AAuraEnemy* Enemy : Enemies) WeakEnemies.Add(Enemy);
	const bool bInitialPassed = bLock && bUnlock && bRankTwo
		&& bInvalidRankRejected && bRankFour && bRankThree && bInterruptOnly && bSlowApplied
		&& Projectile != nullptr;
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[WeakCaster, WeakEnemies, OriginalRanks, bInitialPassed,
			bLock, bUnlock, bRankTwo, bInvalidRankRejected, bRankFour,
			bRankThree, bInterruptOnly, bSlowApplied]()
	{
		const auto Health = [](const TWeakObjectPtr<AAuraEnemy>& Enemy)
		{
			return Enemy.IsValid() ? Enemy->GetAbilitySystemComponent()
				->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : -1.f;
		};
		const bool bPierce = WeakEnemies.Num() == 7
			&& Health(WeakEnemies[3]) < 1000.f && Health(WeakEnemies[4]) < 1000.f
			&& FMath::IsNearlyEqual(Health(WeakEnemies[5]), 1000.f)
			&& FMath::IsNearlyEqual(Health(WeakEnemies[6]), 1000.f);
		const bool bSlowExpired = WeakEnemies[0].IsValid()
			&& !WeakEnemies[0]->GetAbilitySystemComponent()->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("State.Slowed")))
			&& FMath::IsNearlyEqual(WeakEnemies[0]->GetCharacterMovement()->MaxWalkSpeed,
				UAuraCombatBalanceData::Get().MeleeEnemy.WalkSpeed);
		if (WeakCaster.IsValid())
		{
			WeakCaster->SetSkillRank(EAuraPlayerSkill::FireBolt, OriginalRanks.FireBolt);
			WeakCaster->SetSkillRank(EAuraPlayerSkill::ArcaneShards, OriginalRanks.ArcaneShards);
			WeakCaster->SetSkillRank(EAuraPlayerSkill::Shock, OriginalRanks.Shock);
		}
		UE_LOG(LogAuraInput, Display,
			TEXT("AURA_SKILL_RANK_SMOKE Passed=%s Lock=%s Unlock=%s RankTwo=%s InvalidRank=%s RankFour=%s RankThree=%s Interrupt=%s SlowApplied=%s SlowExpired=%s Pierce=%s"),
			bInitialPassed && bPierce && bSlowExpired ? TEXT("true") : TEXT("false"),
			bLock ? TEXT("true") : TEXT("false"), bUnlock ? TEXT("true") : TEXT("false"),
			bRankTwo ? TEXT("true") : TEXT("false"),
			bInvalidRankRejected ? TEXT("true") : TEXT("false"),
			bRankFour ? TEXT("true") : TEXT("false"),
			bRankThree ? TEXT("true") : TEXT("false"),
			bInterruptOnly ? TEXT("true") : TEXT("false"),
			bSlowApplied ? TEXT("true") : TEXT("false"),
			bSlowExpired ? TEXT("true") : TEXT("false"),
			bPierce ? TEXT("true") : TEXT("false"));
		for (const TWeakObjectPtr<AAuraEnemy>& Enemy : WeakEnemies)
		{
			if (Enemy.IsValid()) Enemy->Destroy();
		}
	}, RankBalance.ArcaneShardsFieldDuration + 0.75f, false);
}

void AAuraPlayerController::AuraTestComboAbilities()
{
	AAuraCharacter* Caster = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* ASC = Caster ? Caster->GetAbilitySystemComponent() : nullptr;
	if (!HasAuthority() || !IsLocalController() || !Caster || !ASC || !GetWorld()
		|| !PrepareComboSmokeRanks({EAuraComboMarkType::Ember,
			EAuraComboMarkType::Fracture, EAuraComboMarkType::Overload}))
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_COMBO_ABILITY_SMOKE setup failed"));
		return;
	}
	const FTransform OriginalTransform = Caster->GetActorTransform();
	const float OriginalGravityScale = Caster->GetCharacterMovement()->GravityScale;
	const FVector TestOrigin = OriginalTransform.GetLocation() + FVector(0.f, 0.f, 2200.f);
	Caster->TeleportTo(TestOrigin, FRotator::ZeroRotator, false, true);
	Caster->SetActorRotation(FRotator::ZeroRotator);
	Caster->GetCharacterMovement()->StopMovementImmediately();
	Caster->GetCharacterMovement()->GravityScale = 0.f;
	Caster->GetCharacterMovement()->DisableMovement();
	ASC->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(),
		ASC->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* Primary = GetWorld()->SpawnActor<AAuraEnemy>(AAuraEnemy::StaticClass(),
		TestOrigin + FVector(420.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
	AAuraEnemy* Secondary = GetWorld()->SpawnActor<AAuraEnemy>(AAuraEnemy::StaticClass(),
		TestOrigin + FVector(420.f, 220.f, 0.f), FRotator::ZeroRotator, Params);
	if (!Primary || !Secondary)
	{
		UE_LOG(LogAuraInput, Error, TEXT("AURA_COMBO_ABILITY_SMOKE enemy spawn failed"));
		Caster->TeleportTo(OriginalTransform.GetLocation(), OriginalTransform.Rotator(), false, true);
		Caster->GetCharacterMovement()->GravityScale = OriginalGravityScale;
		Caster->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		return;
	}
	for (AAuraEnemy* Enemy : { Primary, Secondary })
	{
		Enemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AI->StopMovement();
			AI->SetActorTickEnabled(false);
		}
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetMaxHealthAttribute(), 1000.f);
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetHealthAttribute(), 1000.f);
	}
	const bool bTestRankThreeQ = FParse::Param(FCommandLine::Get(), TEXT("AuraComboAbilityRank3Q"));
	const int32 OriginalQRank = Caster->GetSkillRank(EAuraPlayerSkill::ArcaneShards);
	const int32 OriginalERank = Caster->GetSkillRank(EAuraPlayerSkill::Shock);
	Caster->SetSkillRank(EAuraPlayerSkill::ArcaneShards, bTestRankThreeQ ? 3 : 1);
	Caster->SetSkillRank(EAuraPlayerSkill::Shock, 1);
	// Simulate a direct click on the intended combo target. Merely placing the
	// cursor near it must no longer acquire a FireBolt lock.
	bUseDebugAimOverride = true;
	DebugAimOverride = Primary->GetActorLocation();
	DebugAimActorOverride = Primary;
	const bool bFireActivated = ASC->TryActivateAbilityByClass(UAuraFireBoltAbility::StaticClass());
	bUseDebugAimOverride = false;
	DebugAimActorOverride.Reset();
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakCaster(Caster);
	const TWeakObjectPtr<AAuraEnemy> WeakPrimary(Primary);
	const TWeakObjectPtr<AAuraEnemy> WeakSecondary(Secondary);
	FTimerHandle QTimer;
	GetWorldTimerManager().SetTimer(QTimer,
		[WeakController, WeakCaster, WeakPrimary, WeakSecondary,
			OriginalTransform, OriginalGravityScale, OriginalQRank, OriginalERank,
			bTestRankThreeQ, bFireActivated]()
	{
		AAuraPlayerController* Controller = WeakController.Get();
		AAuraCharacter* Character = WeakCaster.Get();
		AAuraEnemy* Target = WeakPrimary.Get();
		if (!Controller || !Character || !Target || !WeakSecondary.IsValid()) return;
		UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent();
		const bool bEmber = Character->HasComboMark(Target, EAuraComboMarkType::Ember);
		const float HealthAfterFire = Target->GetAbilitySystemComponent()->GetNumericAttribute(
			UAuraAttributeSet::GetHealthAttribute());
		const float ManaBeforeQ = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
		Controller->CommittedArcaneShardsTarget = Target->GetActorLocation();
		Controller->bHasCommittedArcaneShardsTarget = true;
		const bool bQActivated = AbilitySystem->TryActivateAbilityByClass(
			UAuraArcaneShardsAbility::StaticClass());
		Controller->bHasCommittedArcaneShardsTarget = false;
		const float ManaAfterQ = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
		const bool bQCost = FMath::IsNearlyEqual(
			ManaBeforeQ - ManaAfterQ, UAuraCombatBalanceData::Get().ArcaneShards.ManaCost, 0.1f);
		FTimerHandle ETimer;
		Controller->GetWorldTimerManager().SetTimer(ETimer,
			[WeakController, WeakCaster, WeakPrimary, WeakSecondary,
				OriginalTransform, OriginalGravityScale, OriginalQRank, OriginalERank,
				bTestRankThreeQ, bFireActivated, bEmber,
				bQActivated, bQCost, HealthAfterFire]()
		{
			AAuraPlayerController* CurrentController = WeakController.Get();
			AAuraCharacter* CurrentCaster = WeakCaster.Get();
			AAuraEnemy* CurrentPrimary = WeakPrimary.Get();
			AAuraEnemy* CurrentSecondary = WeakSecondary.Get();
			if (!CurrentController || !CurrentCaster || !CurrentPrimary || !CurrentSecondary) return;
			UAbilitySystemComponent* CurrentASC = CurrentCaster->GetAbilitySystemComponent();
			const float HealthAfterQ = CurrentPrimary->GetAbilitySystemComponent()
				->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
			const float SecondaryHealthAfterQ = CurrentSecondary->GetAbilitySystemComponent()
				->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
			const bool bBurst = bEmber && !CurrentCaster->HasComboMark(
				CurrentPrimary, EAuraComboMarkType::Ember)
				&& HealthAfterFire - HealthAfterQ
					>= UAuraCombatBalanceData::Get().ArcaneShards.Damage * 1.35f;
			const bool bFracture = CurrentCaster->HasComboMark(
				CurrentPrimary, EAuraComboMarkType::Fracture);
			bool bRankThreeField = !bTestRankThreeQ;
			if (bTestRankThreeQ)
			{
				for (TActorIterator<AAuraArcaneSlowField> It(CurrentCaster->GetWorld()); It; ++It)
				{
					if (FVector::DistSquared(It->GetActorLocation(), CurrentPrimary->GetActorLocation())
						< FMath::Square(50.f))
					{
						bRankThreeField = CurrentPrimary->GetAbilitySystemComponent()
							->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Slowed")));
						break;
					}
				}
			}
			const float ManaBeforeE = CurrentASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
			CurrentController->CommittedShockDirection = FVector::ForwardVector;
			CurrentController->bHasCommittedShockDirection = true;
			const bool bEActivated = CurrentASC->TryActivateAbilityByClass(
				UAuraShockAbility::StaticClass());
			CurrentController->bHasCommittedShockDirection = false;
			const float ManaAfterE = CurrentASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
			const bool bECost = FMath::IsNearlyEqual(
				ManaBeforeE - ManaAfterE, UAuraCombatBalanceData::Get().Shock.ManaCost, 0.1f);
			FTimerHandle ResultTimer;
			CurrentController->GetWorldTimerManager().SetTimer(ResultTimer,
				[WeakCaster, WeakPrimary, WeakSecondary, OriginalTransform, OriginalGravityScale,
					OriginalQRank, OriginalERank, bRankThreeField,
					bFireActivated, bEmber, bQActivated, bQCost, bBurst,
					bFracture, bEActivated, bECost, HealthAfterFire, HealthAfterQ,
					SecondaryHealthAfterQ]()
			{
				AAuraCharacter* FinalCaster = WeakCaster.Get();
				AAuraEnemy* FinalPrimary = WeakPrimary.Get();
				AAuraEnemy* FinalSecondary = WeakSecondary.Get();
				if (!FinalCaster || !FinalPrimary || !FinalSecondary) return;
				const float FinalPrimaryHealth = FinalPrimary->GetAbilitySystemComponent()
					->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
				const float FinalSecondaryHealth = FinalSecondary->GetAbilitySystemComponent()
					->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
				const bool bChain = bFracture
					&& !FinalCaster->HasComboMark(FinalPrimary, EAuraComboMarkType::Fracture)
					&& FinalPrimaryHealth < HealthAfterQ
					&& FinalSecondaryHealth < SecondaryHealthAfterQ;
				const bool bOverload = FinalCaster->HasComboMark(
					FinalPrimary, EAuraComboMarkType::Overload);
				const bool bPassed = bFireActivated && bEmber && bQActivated && bQCost
					&& bBurst && bFracture && bEActivated && bECost && bChain
					&& bOverload && bRankThreeField;
				UE_LOG(LogAuraInput, Display,
					TEXT("AURA_COMBO_ABILITY_SMOKE Passed=%s Fire=%s Ember=%s Q=%s QCost=%s Burst=%s Fracture=%s E=%s ECost=%s Chain=%s Overload=%s Rank3Field=%s Health=%.1f->%.1f->%.1f Secondary=%.1f->%.1f"),
					bPassed ? TEXT("true") : TEXT("false"),
					bFireActivated ? TEXT("true") : TEXT("false"),
					bEmber ? TEXT("true") : TEXT("false"),
					bQActivated ? TEXT("true") : TEXT("false"),
					bQCost ? TEXT("true") : TEXT("false"),
					bBurst ? TEXT("true") : TEXT("false"),
					bFracture ? TEXT("true") : TEXT("false"),
					bEActivated ? TEXT("true") : TEXT("false"),
					bECost ? TEXT("true") : TEXT("false"),
					bChain ? TEXT("true") : TEXT("false"),
					bOverload ? TEXT("true") : TEXT("false"),
					bRankThreeField ? TEXT("true") : TEXT("false"),
					HealthAfterFire, HealthAfterQ, FinalPrimaryHealth,
					SecondaryHealthAfterQ, FinalSecondaryHealth);
				FinalCaster->SetSkillRank(EAuraPlayerSkill::ArcaneShards, OriginalQRank);
				FinalCaster->SetSkillRank(EAuraPlayerSkill::Shock, OriginalERank);
				FinalCaster->TeleportTo(OriginalTransform.GetLocation(), OriginalTransform.Rotator(), false, true);
				FinalCaster->GetCharacterMovement()->GravityScale = OriginalGravityScale;
				FinalCaster->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				FinalPrimary->Destroy();
				FinalSecondary->Destroy();
				}, 0.42f, false);
		}, 0.72f, false);
	}, 0.85f, false);
}

void AAuraPlayerController::AuraTestNetworkRespawn()
{
	if (!IsLocalController()) return;
	if (HasAuthority()) ServerRunNetworkRespawnSmoke_Implementation();
	else ServerRunNetworkRespawnSmoke();
}

void AAuraPlayerController::AuraTestInventory()
{
	if (!IsLocalController()) return;
	if (HasAuthority()) RunInventorySmoke();
	else ServerRunInventorySmoke();
}

void AAuraPlayerController::AuraTestInventoryHeldMovement()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!IsLocalController() || !GetWorld() || !AuraCharacter || AuraCharacter->IsDead())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Inventory held-movement regression requires a live local AuraCharacter."));
		return;
	}
	if (bInventoryOpen) ToggleInventory();
	SetAuraCameraMode(EAuraCameraMode::TopDown);
	FlushPressedKeys();
	AuraCharacter->ConsumeMovementInputVector();
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();

	const FVector StartLocation = AuraCharacter->GetActorLocation();
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TSharedRef<float> MovedBeforeOpen = MakeShared<float>(0.f);
	const TSharedRef<FVector> LocationAfterClose = MakeShared<FVector>(StartLocation);
	const TSharedRef<bool> BackpackOpened = MakeShared<bool>(false);
	const TSharedRef<bool> BackpackClosed = MakeShared<bool>(false);
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.f));

	FTimerHandle OpenTimer;
	GetWorldTimerManager().SetTimer(OpenTimer,
		[this, WeakCharacter, StartLocation, MovedBeforeOpen, LocationAfterClose,
			BackpackOpened, BackpackClosed]()
	{
		if (!WeakCharacter.IsValid()) return;
		*MovedBeforeOpen = FVector::Dist2D(StartLocation, WeakCharacter->GetActorLocation());
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::B, IE_Pressed, 1.f));
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::B, IE_Released, 0.f));
		*BackpackOpened = bInventoryOpen;

		// This is the event that used to be swallowed while the backpack was open.
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.f));

		FTimerHandle CloseTimer;
		GetWorldTimerManager().SetTimer(CloseTimer,
			[this, WeakCharacter, LocationAfterClose, BackpackClosed]()
		{
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::B, IE_Pressed, 1.f));
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::B, IE_Released, 0.f));
			*BackpackClosed = !bInventoryOpen;
			if (WeakCharacter.IsValid()) *LocationAfterClose = WeakCharacter->GetActorLocation();
		}, 0.15f, false);
	}, 0.35f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakCharacter, MovedBeforeOpen, LocationAfterClose, BackpackOpened, BackpackClosed]()
	{
		const FVector FinalLocation = WeakCharacter.IsValid()
			? WeakCharacter->GetActorLocation() : *LocationAfterClose;
		const float DriftAfterClose = FVector::Dist2D(*LocationAfterClose, FinalLocation);
		const float FinalSpeed = WeakCharacter.IsValid() ? WeakCharacter->GetVelocity().Size2D() : -1.f;
		const bool bWStillDown = IsInputKeyDown(EKeys::W);
		const bool bPassed = *BackpackOpened && *BackpackClosed && !bInventoryOpen
			&& *MovedBeforeOpen > 25.f && DriftAfterClose < 5.f
			&& FinalSpeed < 1.f && !bWStillDown;
		UE_LOG(LogAuraInput, Display,
			TEXT("Inventory held-movement regression completed. Passed=%s MovedBeforeOpen=%.2f DriftAfterClose=%.2f FinalSpeed=%.2f BackpackOpened=%s BackpackClosed=%s WDown=%s"),
			bPassed ? TEXT("true") : TEXT("false"), *MovedBeforeOpen, DriftAfterClose, FinalSpeed,
			*BackpackOpened ? TEXT("true") : TEXT("false"),
			*BackpackClosed ? TEXT("true") : TEXT("false"),
			bWStillDown ? TEXT("true") : TEXT("false"));
	}, 1.05f, false);
}

void AAuraPlayerController::AuraCaptureInventoryInteraction()
{
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAuraItemDefinition* Weapon = UAuraItemCatalog::Get().FindItem(
		TEXT("Equipment.ArcaneStaff"));
	UAuraItemDefinition* Armor = UAuraItemCatalog::Get().FindItem(
		TEXT("Equipment.WardenArmor"));
	UAuraItemDefinition* Head = UAuraItemCatalog::Get().FindItem(
		TEXT("Equipment.SeerHeadpiece"));
	UAuraItemDefinition* Shoes = UAuraItemCatalog::Get().FindItem(
		TEXT("Equipment.WindShoes"));
	if (!IsLocalController() || !HasAuthority() || !Inventory || !AuraCharacter
		|| !Weapon || !Armor || !Head || !Shoes)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Inventory interaction capture setup failed. Local=%s Authority=%s Inventory=%s Character=%s Definitions=%s/%s/%s/%s"),
			IsLocalController() ? TEXT("true") : TEXT("false"),
			HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(Inventory),
			*GetNameSafe(AuraCharacter), *GetNameSafe(Weapon), *GetNameSafe(Armor),
			*GetNameSafe(Head), *GetNameSafe(Shoes));
		return;
	}

	Inventory->DebugClearInventory();
	const auto AddEquipment = [Inventory](UAuraItemDefinition* Definition,
		const EAuraItemRarity Rarity, const int32 Level, const int32 Seed)
	{
		FAuraItemInstance Item;
		Item.Definition = Definition;
		Item.Quantity = 1;
		Item.Rarity = Rarity;
		Item.ItemLevel = Level;
		Item.EnsureIdentity();
		AuraItems::RollEquipmentModifiers(Item, Seed);
		FString Failure;
		return Inventory->TryAddItem(Item, Failure, false) == 1;
	};
	const bool bAdded = AddEquipment(Weapon, EAuraItemRarity::Legendary, 5, 6101)
		&& AddEquipment(Armor, EAuraItemRarity::Epic, 4, 6102)
		&& AddEquipment(Head, EAuraItemRarity::Fine, 3, 6103)
		&& AddEquipment(Shoes, EAuraItemRarity::Quality, 2, 6104);
	int32 WeaponSlot = INDEX_NONE;
	int32 ArmorSlot = INDEX_NONE;
	int32 ShoesSlot = INDEX_NONE;
	for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (Entry.Item.Definition == Weapon) WeaponSlot = Entry.SlotIndex;
		else if (Entry.Item.Definition == Armor) ArmorSlot = Entry.SlotIndex;
		else if (Entry.Item.Definition == Shoes) ShoesSlot = Entry.SlotIndex;
	}
	AAuraWorldItem* DroppedArmor = nullptr;
	AAuraWorldItem* DroppedShoes = nullptr;
	FString DropFailure;
	const bool bArmorDropped = ArmorSlot != INDEX_NONE
		&& Inventory->TryDropSlot(ArmorSlot, 1, DropFailure, &DroppedArmor);
	DropFailure.Reset();
	const bool bShoesDropped = ShoesSlot != INDEX_NONE
		&& Inventory->TryDropSlot(ShoesSlot, 1, DropFailure, &DroppedShoes);
	if (DroppedShoes) DroppedShoes->AddActorWorldOffset(FVector(0.f, 82.f, 0.f));

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	if (!bInventoryOpen) ToggleInventory();
	SelectedInventorySlot = WeaponSlot;
	InventoryContextSlot = WeaponSlot;
	int32 ViewportX = 1280;
	int32 ViewportY = 720;
	GetViewportSize(ViewportX, ViewportY);
	InventoryContextMenuPosition = FVector2D(
		ViewportX * 0.67f, ViewportY * 0.42f);
	SetMouseLocation(ViewportX / 2, ViewportY / 2);
	NextNearbyEquipmentRefreshTime = 0.f;
	UpdateNearbyEquipment();
	const bool bManualDropsAtSpawn = DroppedArmor && DroppedShoes
		&& DroppedArmor->RequiresManualPickup() && DroppedShoes->RequiresManualPickup();

	const FString ScreenshotDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
	IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
	const TWeakObjectPtr<AAuraWorldItem> WeakArmor(DroppedArmor);
	const TWeakObjectPtr<AAuraWorldItem> WeakShoes(DroppedShoes);
	FTimerHandle CaptureTimer;
	GetWorldTimerManager().SetTimer(CaptureTimer, [ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("InventoryInteractionPreview.png")), false, false);
	}, 0.45f, false);

	FTimerHandle FPickupTimer;
	GetWorldTimerManager().SetTimer(FPickupTimer, [this]()
	{
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Pressed, 1.f));
	}, 0.70f, false);

	FTimerHandle ListClickTimer;
	GetWorldTimerManager().SetTimer(ListClickTimer, [this, WeakShoes]()
	{
		// Render-offscreen runs do not expose a reliable OS cursor. Exercise the
		// same explicit target request used after a nearby-list row is clicked.
		RequestPickupWorldItem(WeakShoes.Get());
	}, 0.92f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakArmor, WeakShoes, Armor, Shoes, bAdded, bArmorDropped,
		bShoesDropped, bManualDropsAtSpawn, WeaponSlot]()
	{
		const UAuraInventoryComponent* CurrentInventory = GetAuraInventory();
		const bool bFPickupPassed = CurrentInventory && Armor
			&& CurrentInventory->GetTotalQuantity(Armor->ItemId) == 1
			&& !WeakArmor.IsValid();
		const bool bListClickPassed = CurrentInventory && Shoes
			&& CurrentInventory->GetTotalQuantity(Shoes->ItemId) == 1
			&& !WeakShoes.IsValid();
		const bool bNearbyCleared = NearbyWorldItems.IsEmpty()
			&& !HighlightedWorldItem.IsValid();
		const bool bDetailsOpen = bInventoryOpen && SelectedInventorySlot == WeaponSlot;
		const bool bContextOpen = InventoryContextSlot == WeaponSlot;
		const bool bPassed = bAdded && bArmorDropped && bShoesDropped
			&& bManualDropsAtSpawn && bFPickupPassed && bListClickPassed
			&& bNearbyCleared && bDetailsOpen && bContextOpen;
		UE_LOG(LogAuraInput, Display,
			TEXT("Inventory interaction preview completed. Passed=%s Added=%s Dropped=%s/%s ManualDrops=%s FPickup=%s ListClickPickup=%s NearbyCleared=%s DetailsOpen=%s ContextOpen=%s"),
			bPassed ? TEXT("true") : TEXT("false"), bAdded ? TEXT("true") : TEXT("false"),
			bArmorDropped ? TEXT("true") : TEXT("false"),
			bShoesDropped ? TEXT("true") : TEXT("false"),
			bManualDropsAtSpawn ? TEXT("true") : TEXT("false"),
			bFPickupPassed ? TEXT("true") : TEXT("false"),
			bListClickPassed ? TEXT("true") : TEXT("false"),
			bNearbyCleared ? TEXT("true") : TEXT("false"),
			bDetailsOpen ? TEXT("true") : TEXT("false"),
			bContextOpen ? TEXT("true") : TEXT("false"));
	}, 1.20f, false);
}

void AAuraPlayerController::AuraCaptureWorldLootMarkers()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* AuraState = GetPlayerState<AAuraPlayerState>();
	UAuraLootSubsystem* LootSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UAuraLootSubsystem>() : nullptr;
	TArray<UAuraItemDefinition*> Definitions = {
		UAuraItemCatalog::Get().FindItem(TEXT("Equipment.ArcaneStaff")),
		UAuraItemCatalog::Get().FindItem(TEXT("Equipment.WardenArmor")),
		UAuraItemCatalog::Get().FindItem(TEXT("Equipment.SeerHeadpiece")),
		UAuraItemCatalog::Get().FindItem(TEXT("Equipment.WindShoes")),
		UAuraItemCatalog::Get().FindItem(TEXT("Equipment.ArcaneStaff"))
	};
	if (!IsLocalController() || !HasAuthority() || !AuraCharacter || !AuraState
		|| !LootSubsystem || Definitions.Contains(nullptr))
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("World loot marker capture setup failed. Local=%s Authority=%s Character=%s State=%s Loot=%s Definitions=%d"),
			IsLocalController() ? TEXT("true") : TEXT("false"),
			HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(AuraCharacter),
			*GetNameSafe(AuraState), *GetNameSafe(LootSubsystem), Definitions.Num());
		return;
	}

	if (bInventoryOpen) ToggleInventory();
	for (TActorIterator<AAuraWorldItem> It(GetWorld()); It; ++It)
	{
		if (It->IsExclusiveOwnerDrop()) It->Destroy();
	}
	SetAuraCameraMode(EAuraCameraMode::TopDown);
	SetControlRotation(FRotator(-58.f, AuraCharacter->GetActorRotation().Yaw, 0.f));
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();

	const TArray<EAuraItemRarity> Rarities = {
		EAuraItemRarity::Common,
		EAuraItemRarity::Quality,
		EAuraItemRarity::Fine,
		EAuraItemRarity::Epic,
		EAuraItemRarity::Legendary
	};
	const FVector Forward = AuraCharacter->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = AuraCharacter->GetActorRightVector().GetSafeNormal2D();
	TArray<TWeakObjectPtr<AAuraWorldItem>> SpawnedMarkers;
	bool bAllGrounded = true;
	for (int32 Index = 0; Index < Rarities.Num(); ++Index)
	{
		FAuraItemInstance Item;
		Item.Definition = Definitions[Index];
		Item.Quantity = 1;
		Item.Rarity = Rarities[Index];
		Item.ItemLevel = Index + 1;
		Item.SourceType = EAuraItemSourceType::Debug;
		Item.SourceId = TEXT("LootMarkerPreview");
		Item.EnsureIdentity();
		AuraItems::RollEquipmentModifiers(Item, 7200 + Index);
		const FVector DesiredLocation = AuraCharacter->GetActorLocation()
			+ Forward * 320.f + Right * ((Index - 2) * 120.f);
		AAuraWorldItem* Marker = LootSubsystem->SpawnSpecificPersonalItem(
			*AuraState, Item, DesiredLocation, 20.f);
		if (Marker)
		{
			SpawnedMarkers.Add(Marker);
			bAllGrounded &= Marker->GetActorLocation().Z
				< AuraCharacter->GetActorLocation().Z - 40.f;
		}
	}

	const FString ScreenshotDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
	IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
	if (!SpawnedMarkers.IsEmpty() && SpawnedMarkers.Last().IsValid())
	{
		SpawnedMarkers.Last()->SetLocallyHighlighted(true);
	}

	FTimerHandle FrontCaptureTimer;
	GetWorldTimerManager().SetTimer(FrontCaptureTimer,
		[ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("WorldLootRarityMarkers.png")), false, false);
	}, 1.f, false);

	const float InitialYaw = AuraCharacter->GetActorRotation().Yaw;
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	FTimerHandle RightViewTimer;
	GetWorldTimerManager().SetTimer(RightViewTimer,
		[WeakController, InitialYaw]()
	{
		if (WeakController.IsValid())
		{
			WeakController->SetControlRotation(FRotator(-58.f, InitialYaw + 50.f, 0.f));
		}
	}, 1.3f, false);
	FTimerHandle RightCaptureTimer;
	GetWorldTimerManager().SetTimer(RightCaptureTimer,
		[ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("WorldLootRarityMarkers_Right.png")), false, false);
	}, 1.7f, false);

	FTimerHandle LeftViewTimer;
	GetWorldTimerManager().SetTimer(LeftViewTimer,
		[WeakController, InitialYaw]()
	{
		if (WeakController.IsValid())
		{
			WeakController->SetControlRotation(FRotator(-58.f, InitialYaw - 50.f, 0.f));
		}
	}, 2.f, false);
	FTimerHandle LeftCaptureTimer;
	GetWorldTimerManager().SetTimer(LeftCaptureTimer,
		[ScreenshotDirectory]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(
			ScreenshotDirectory, TEXT("WorldLootRarityMarkers_Left.png")), false, false);
	}, 2.4f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[SpawnedMarkers, ScreenshotDirectory, bAllGrounded]()
	{
		int32 VisibleMarkers = 0;
		int32 CompactGroundedVisuals = 0;
		float LargestVisualDimension = 0.f;
		float HighestFloorOffset = -TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<AAuraWorldItem>& Marker : SpawnedMarkers)
		{
			if (!Marker.IsValid()) continue;
			if (Marker->IsVisibleToLocalPlayer()) ++VisibleMarkers;
			if (Marker->HasCompactGroundedEquipmentVisual()) ++CompactGroundedVisuals;
			LargestVisualDimension = FMath::Max(
				LargestVisualDimension, Marker->GetEquipmentVisualLongestDimension());
			HighestFloorOffset = FMath::Max(
				HighestFloorOffset, Marker->GetEquipmentVisualFloorOffset());
		}
		UE_LOG(LogAuraInput, Display,
			TEXT("World loot marker capture completed. Passed=%s Spawned=%d Visible=%d GroundedRoots=%s CompactGroundedVisuals=%d LargestVisual=%.1f HighestFloorOffset=%.1f Angles=3 Screenshot=%s"),
			SpawnedMarkers.Num() == 5 && VisibleMarkers == 5 && bAllGrounded
				&& CompactGroundedVisuals == 5
				? TEXT("true") : TEXT("false"),
			SpawnedMarkers.Num(), VisibleMarkers,
			bAllGrounded ? TEXT("true") : TEXT("false"), CompactGroundedVisuals,
			LargestVisualDimension, HighestFloorOffset, *ScreenshotDirectory);
	}, 2.65f, false);
}

void AAuraPlayerController::AuraTestManualPickupNetwork()
{
	if (!IsLocalController() || !GetWorld()) return;
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAuraItemDefinition* Armor = UAuraItemCatalog::Get().FindItem(
		TEXT("Equipment.WardenArmor"));
	if (!Inventory || !Armor)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Manual pickup network smoke setup failed. Inventory=%s Armor=%s"),
			*GetNameSafe(Inventory), *GetNameSafe(Armor));
		return;
	}

	struct FManualPickupNetworkState
	{
		int32 BaselineQuantity = 0;
		bool bGrantObserved = false;
		bool bDropRequested = false;
		bool bWorldObserved = false;
		TWeakObjectPtr<AAuraWorldItem> DroppedItem;
	};
	const TSharedRef<FManualPickupNetworkState> State =
		MakeShared<FManualPickupNetworkState>();
	State->BaselineQuantity = Inventory->GetTotalQuantity(Armor->ItemId);
	AuraGiveItem(Armor->ItemId.ToString(), 1);
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<UAuraItemDefinition> WeakArmor(Armor);

	FTimerHandle DropTimer;
	GetWorldTimerManager().SetTimer(DropTimer,
		[WeakController, WeakArmor, State]()
	{
		AAuraPlayerController* Controller = WeakController.Get();
		UAuraInventoryComponent* CurrentInventory = Controller
			? Controller->GetAuraInventory() : nullptr;
		UAuraItemDefinition* Definition = WeakArmor.Get();
		if (!Controller || !CurrentInventory || !Definition) return;
		for (const FAuraInventoryEntry& Entry : CurrentInventory->GetEntries())
		{
			if (Entry.Item.Definition != Definition) continue;
			State->bGrantObserved = CurrentInventory->GetTotalQuantity(Definition->ItemId)
				== State->BaselineQuantity + 1;
			CurrentInventory->RequestDropSlot(Entry.SlotIndex, 1);
			State->bDropRequested = true;
			break;
		}
	}, 0.9f, false);

	FTimerHandle PickupTimer;
	GetWorldTimerManager().SetTimer(PickupTimer,
		[WeakController, WeakArmor, State]()
	{
		AAuraPlayerController* Controller = WeakController.Get();
		UAuraItemDefinition* Definition = WeakArmor.Get();
		if (!Controller || !Definition) return;
		Controller->NextNearbyEquipmentRefreshTime = 0.f;
		Controller->UpdateNearbyEquipment();
		for (const TWeakObjectPtr<AAuraWorldItem>& Candidate : Controller->NearbyWorldItems)
		{
			AAuraWorldItem* WorldItem = Candidate.Get();
			if (!WorldItem || WorldItem->GetItemInstance().Definition != Definition
				|| WorldItem->GetItemInstance().SourceType != EAuraItemSourceType::PlayerDrop)
			{
				continue;
			}
			State->bWorldObserved = WorldItem->RequiresManualPickup();
			State->DroppedItem = WorldItem;
			if (Controller->HighlightedWorldItem.IsValid()
				&& Controller->HighlightedWorldItem.Get() != WorldItem)
			{
				Controller->HighlightedWorldItem->SetLocallyHighlighted(false);
			}
			Controller->HighlightedWorldItem = WorldItem;
			WorldItem->SetLocallyHighlighted(true);
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(
				EKeys::F, IE_Pressed, 1.f));
			break;
		}
	}, 1.8f, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[WeakController, WeakArmor, State]()
	{
		AAuraPlayerController* Controller = WeakController.Get();
		UAuraInventoryComponent* CurrentInventory = Controller
			? Controller->GetAuraInventory() : nullptr;
		UAuraItemDefinition* Definition = WeakArmor.Get();
		const bool bQuantityRestored = CurrentInventory && Definition
			&& CurrentInventory->GetTotalQuantity(Definition->ItemId)
				== State->BaselineQuantity + 1;
		const bool bWorldConsumed = !State->DroppedItem.IsValid();
		const bool bPassed = State->bGrantObserved && State->bDropRequested
			&& State->bWorldObserved && bQuantityRestored && bWorldConsumed;
		UE_LOG(LogAuraInput, Display,
			TEXT("Manual pickup network smoke completed. Passed=%s Authority=%s Grant=%s Drop=%s WorldObserved=%s QuantityRestored=%s WorldConsumed=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			Controller && Controller->HasAuthority() ? TEXT("true") : TEXT("false"),
			State->bGrantObserved ? TEXT("true") : TEXT("false"),
			State->bDropRequested ? TEXT("true") : TEXT("false"),
			State->bWorldObserved ? TEXT("true") : TEXT("false"),
			bQuantityRestored ? TEXT("true") : TEXT("false"),
			bWorldConsumed ? TEXT("true") : TEXT("false"));
	}, 2.8f, false);
}

void AAuraPlayerController::AuraGiveItem(FString ItemId, const int32 Quantity)
{
	if (ItemId.IsEmpty() || Quantity <= 0) return;
	if (HasAuthority()) ServerGrantDebugItem_Implementation(FName(*ItemId), Quantity);
	else ServerGrantDebugItem(FName(*ItemId), Quantity);
}

void AAuraPlayerController::AuraDropInventorySlot(const int32 SlotIndex, const int32 Quantity)
{
	if (UAuraInventoryComponent* Inventory = GetAuraInventory())
	{
		Inventory->RequestDropSlot(SlotIndex, Quantity);
	}
}

void AAuraPlayerController::AuraLogInventory()
{
	const UAuraInventoryComponent* Inventory = GetAuraInventory();
	if (!Inventory)
	{
		UE_LOG(LogAuraInput, Error, TEXT("Inventory log failed: no inventory component."));
		return;
	}
	UE_LOG(LogAuraInput, Display, TEXT("Inventory snapshot. Owner=%s Slots=%d/%d"),
		*GetNameSafe(PlayerState), Inventory->GetOccupiedSlotCount(), Inventory->GetCapacity());
	for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
	{
		UE_LOG(LogAuraInput, Display,
			TEXT("  Slot=%d Instance=%s Item=%s Quantity=%d Rarity=%s Level=%d"),
			Entry.SlotIndex, *Entry.Item.InstanceId.ToString(),
			Entry.Item.Definition ? *Entry.Item.Definition->ItemId.ToString() : TEXT("None"),
			Entry.Item.Quantity, AuraItems::GetRarityName(Entry.Item.Rarity), Entry.Item.ItemLevel);
	}
}

void AAuraPlayerController::AuraTestEquipment()
{
	if (!IsLocalController()) return;
	if (HasAuthority()) RunEquipmentSmoke();
	else ServerRunEquipmentSmoke();
}

void AAuraPlayerController::AuraLogEquipment()
{
	const UAuraEquipmentComponent* Equipment = GetAuraEquipment();
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	const UAbilitySystemComponent* AbilitySystem = AuraPawn
		? AuraPawn->GetAbilitySystemComponent() : nullptr;
	if (!Equipment || !AbilitySystem)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Equipment log failed. Equipment=%s ASC=%s"),
			*GetNameSafe(Equipment), *GetNameSafe(AbilitySystem));
		return;
	}
	UE_LOG(LogAuraInput, Display,
		TEXT("Equipment snapshot. Owner=%s Equipped=%d Effects=%d MaxHealth=%.1f MaxMana=%.1f Strength=%.1f Intelligence=%.1f Armor=%.1f MoveSpeed=%.1f DamageMultiplier=%.3f"),
		*GetNameSafe(PlayerState), Equipment->GetEquippedCount(),
		Equipment->GetActiveEffectCountForDebug(),
		AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()),
		AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()),
		AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetStrengthAttribute()),
		AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetIntelligenceAttribute()),
		AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetArmorAttribute()),
		AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMoveSpeedAttribute()),
		AuraPawn ? AuraPawn->GetAbilityDamageMultiplier() : 1.f);
	for (const FAuraEquippedItem& Entry : Equipment->GetEquippedItems())
	{
		UE_LOG(LogAuraInput, Display,
			TEXT("  Slot=%s Instance=%s Item=%s Rarity=%s Level=%d Seed=%d"),
			AuraItems::GetEquipmentSlotName(Entry.Slot),
			*Entry.Item.InstanceId.ToString(), Entry.Item.Definition
				? *Entry.Item.Definition->ItemId.ToString() : TEXT("None"),
			AuraItems::GetRarityName(Entry.Item.Rarity), Entry.Item.ItemLevel,
			Entry.Item.RollSeed);
	}
	if (const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		for (const APlayerState* VisiblePlayerState : GameState->PlayerArray)
		{
			const AAuraPlayerState* AuraPlayerState = Cast<AAuraPlayerState>(VisiblePlayerState);
			const AAuraCharacter* VisibleCharacter = AuraPlayerState
				? Cast<AAuraCharacter>(AuraPlayerState->GetPawn()) : nullptr;
			const UAbilitySystemComponent* VisibleASC = VisibleCharacter
				? VisibleCharacter->GetAbilitySystemComponent() : nullptr;
			const UAuraEquipmentComponent* VisibleEquipment = AuraPlayerState
				? AuraPlayerState->GetEquipmentComponent() : nullptr;
			if (!AuraPlayerState || !VisibleASC || !VisibleEquipment) continue;
			UE_LOG(LogAuraInput, Display,
				TEXT("Network equipment view. Viewer=%s Subject=%s IsOwner=%s VisibleItems=%d MaxHealth=%.1f MaxMana=%.1f Strength=%.1f Intelligence=%.1f Armor=%.1f MoveSpeed=%.1f"),
				*GetNameSafe(PlayerState), *GetNameSafe(AuraPlayerState),
				AuraPlayerState == PlayerState ? TEXT("true") : TEXT("false"),
				VisibleEquipment->GetEquippedCount(),
				VisibleASC->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()),
				VisibleASC->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()),
				VisibleASC->GetNumericAttribute(UAuraAttributeSet::GetStrengthAttribute()),
				VisibleASC->GetNumericAttribute(UAuraAttributeSet::GetIntelligenceAttribute()),
				VisibleASC->GetNumericAttribute(UAuraAttributeSet::GetArmorAttribute()),
				VisibleASC->GetNumericAttribute(UAuraAttributeSet::GetMoveSpeedAttribute()));
		}
	}
}

void AAuraPlayerController::AuraTestLoot()
{
	if (!IsLocalController()) return;
	if (HasAuthority()) RunLootSmoke();
	else ServerRunLootSmoke();
}

void AAuraPlayerController::AuraLogLoot()
{
	int32 TotalWorldItems = 0;
	int32 VisiblePersonalItems = 0;
	int32 OwnedPersonalItems = 0;
	int32 ForeignPersonalItems = 0;
	for (TActorIterator<AAuraWorldItem> It(GetWorld()); It; ++It)
	{
		++TotalWorldItems;
		if (!It->IsExclusiveOwnerDrop()) continue;
		++VisiblePersonalItems;
		const bool bOwned = It->GetReservedPlayer() == PlayerState;
		if (bOwned) ++OwnedPersonalItems;
		else ++ForeignPersonalItems;
		const FAuraItemInstance& Item = It->GetItemInstance();
		UE_LOG(LogAuraInput, Display,
			TEXT("  Visible personal loot. Actor=%s Owned=%s Reserved=%s Item=%s Rarity=%s Level=%d Seed=%d SourceType=%s SourceId=%s"),
			*It->GetName(), bOwned ? TEXT("true") : TEXT("false"),
			*GetNameSafe(It->GetReservedPlayer()), Item.Definition
				? *Item.Definition->ItemId.ToString() : TEXT("None"),
			AuraItems::GetRarityName(Item.Rarity), Item.ItemLevel, Item.RollSeed,
			AuraItems::GetSourceName(Item.SourceType), *Item.SourceId.ToString());
	}
	const UAuraInventoryComponent* Inventory = GetAuraInventory();
	UE_LOG(LogAuraInput, Display,
		TEXT("Personal loot snapshot. Viewer=%s Authority=%s TotalWorld=%d VisiblePersonal=%d Owned=%d Foreign=%d InventorySlots=%d"),
		*GetNameSafe(PlayerState), HasAuthority() ? TEXT("true") : TEXT("false"),
		TotalWorldItems, VisiblePersonalItems, OwnedPersonalItems, ForeignPersonalItems,
		Inventory ? Inventory->GetOccupiedSlotCount() : -1);
}

void AAuraPlayerController::AuraTestRoomStateMachine()
{
	if (!IsLocalController()) return;
	if (HasAuthority()) RunRoomSmoke();
	else ServerRunRoomSmoke();
}

void AAuraPlayerController::AuraLogRoomState()
{
	int32 RoomCount = 0;
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		++RoomCount;
		const float ReentryDistance = AuraCharacter
			? FVector::Dist(AuraCharacter->GetActorLocation(),
				It->GetRoomReentryTransform().GetLocation()) : -1.f;
		UE_LOG(LogAuraInput, Display,
			TEXT("Room state view. Viewer=%s Room=%s Order=%d Type=%d State=%d Remaining=%d ActiveLocal=%d GatesOpen=%s BoonChest=%s LootChest=%s ReentryDistance=%.1f"),
			*GetNameSafe(PlayerState), *It->GetName(), It->GetEncounterOrder(),
			static_cast<int32>(It->GetEncounterType()),
			static_cast<int32>(It->GetEncounterState()),
			It->GetRemainingEnemyCount(), It->GetActiveEnemyCount(),
			It->AreControlledGatesOpen() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(It->GetRewardChest()), *GetNameSafe(It->GetLootChest()),
			ReentryDistance);
	}
	UE_LOG(LogAuraInput, Display,
		TEXT("Room state snapshot. Viewer=%s Authority=%s Rooms=%d Character=%s Location=%s"),
		*GetNameSafe(PlayerState), HasAuthority() ? TEXT("true") : TEXT("false"),
		RoomCount, *GetNameSafe(AuraCharacter),
		AuraCharacter ? *AuraCharacter->GetActorLocation().ToCompactString() : TEXT("None"));
}

void AAuraPlayerController::ServerRunInventorySmoke_Implementation()
{
	RunInventorySmoke();
}

void AAuraPlayerController::ServerRunEquipmentSmoke_Implementation()
{
	RunEquipmentSmoke();
}

void AAuraPlayerController::ServerRunLootSmoke_Implementation()
{
	RunLootSmoke();
}

void AAuraPlayerController::ServerRunRoomSmoke_Implementation()
{
	RunRoomSmoke();
}

void AAuraPlayerController::ServerGrantDebugItem_Implementation(
	const FName ItemId, const int32 Quantity)
{
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAuraItemDefinition* Definition = UAuraItemCatalog::Get().FindItem(ItemId);
	if (!Inventory || !Definition || Quantity <= 0)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Debug item grant rejected. Inventory=%s ItemId=%s Definition=%s Quantity=%d"),
			Inventory ? TEXT("valid") : TEXT("missing"), *ItemId.ToString(),
			*GetNameSafe(Definition), Quantity);
		return;
	}
	FAuraItemInstance Item;
	Item.Definition = Definition;
	Item.Quantity = Quantity;
	Item.Rarity = Definition->DefaultRarity;
	Item.EnsureIdentity();
	FString Failure;
	const int32 Added = Inventory->TryAddItem(Item, Failure);
	UE_LOG(LogAuraInput, Display,
		TEXT("Debug item grant. Item=%s Requested=%d Added=%d Failure=%s"),
		*ItemId.ToString(), Quantity, Added, Failure.IsEmpty() ? TEXT("None") : *Failure);
}

void AAuraPlayerController::ServerPickupWorldItem_Implementation(AAuraWorldItem* WorldItem)
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !WorldItem || !WorldItem->RequiresManualPickup()) return;
	WorldItem->TryPickupBy(AuraCharacter);
}

void AAuraPlayerController::ServerInteractRewardChest_Implementation(AActor* ChestActor)
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || !ChestActor) return;
	if (AAuraTreasureChest* TreasureChest = Cast<AAuraTreasureChest>(ChestActor))
	{
		TreasureChest->ClaimForCharacter(AuraCharacter);
	}
	else if (AAuraLootChest* LootChest = Cast<AAuraLootChest>(ChestActor))
	{
		LootChest->ClaimForCharacter(AuraCharacter);
	}
}

void AAuraPlayerController::RunInventorySmoke()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAbilitySystemComponent* AbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	FString CatalogError;
	const UAuraItemCatalog& Catalog = UAuraItemCatalog::Get();
	const bool bCatalogValid = Catalog.Validate(CatalogError);
	UAuraItemDefinition* HealthPotion = Catalog.FindItem(TEXT("Consumable.HealthPotion"));
	UAuraItemDefinition* ManaPotion = Catalog.FindItem(TEXT("Consumable.ManaPotion"));
	UAuraItemDefinition* Weapon = Catalog.FindItem(TEXT("Equipment.ArcaneStaff"));
	UAuraItemDefinition* Armor = Catalog.FindItem(TEXT("Equipment.WardenArmor"));
	UAuraItemDefinition* Head = Catalog.FindItem(TEXT("Equipment.SeerHeadpiece"));
	UAuraItemDefinition* Shoes = Catalog.FindItem(TEXT("Equipment.WindShoes"));
	if (!HasAuthority() || !AuraCharacter || !Inventory || !AbilitySystem || !bCatalogValid
		|| !HealthPotion || !ManaPotion || !Weapon || !Armor || !Head || !Shoes)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Inventory smoke setup failed. Authority=%s Character=%s Inventory=%s ASC=%s Catalog=%s Error=%s Definitions=%s/%s/%s/%s/%s/%s"),
			HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(AuraCharacter),
			*GetNameSafe(Inventory), *GetNameSafe(AbilitySystem),
			bCatalogValid ? TEXT("valid") : TEXT("invalid"),
			CatalogError.IsEmpty() ? TEXT("None") : *CatalogError,
			*GetNameSafe(HealthPotion), *GetNameSafe(ManaPotion), *GetNameSafe(Weapon),
			*GetNameSafe(Armor), *GetNameSafe(Head), *GetNameSafe(Shoes));
		return;
	}

	const float OriginalHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float OriginalMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const auto MakeItem = [](UAuraItemDefinition* Definition, const int32 Quantity,
		const EAuraItemRarity Rarity = EAuraItemRarity::Common)
	{
		FAuraItemInstance Item;
		Item.Definition = Definition;
		Item.Quantity = Quantity;
		Item.Rarity = Rarity;
		Item.EnsureIdentity();
		return Item;
	};
	const auto Add = [Inventory, &MakeItem](UAuraItemDefinition* Definition,
		const int32 Quantity, const EAuraItemRarity Rarity = EAuraItemRarity::Common)
	{
		FString Failure;
		return Inventory->TryAddItem(MakeItem(Definition, Quantity, Rarity), Failure);
	};

	Inventory->DebugClearInventory();
	const int32 HealthAdded = Add(HealthPotion, 7);
	const int32 ManaAdded = Add(ManaPotion, 2);
	const int32 WeaponAdded = Add(Weapon, 1, EAuraItemRarity::Fine);
	const int32 ArmorAdded = Add(Armor, 1, EAuraItemRarity::Quality);
	const int32 HeadAdded = Add(Head, 1, EAuraItemRarity::Epic);
	const int32 ShoesAdded = Add(Shoes, 1);
	int32 HealthStackCount = 0;
	for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (Entry.Item.Definition == HealthPotion) ++HealthStackCount;
	}
	const bool bStackingPassed = HealthAdded == 7 && ManaAdded == 2
		&& WeaponAdded == 1 && ArmorAdded == 1 && HeadAdded == 1 && ShoesAdded == 1
		&& Inventory->GetTotalQuantity(HealthPotion->ItemId) == 7
		&& HealthStackCount == 2;

	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 40.f);
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 10.f);
	FString UseFailure;
	const bool bHealthUsed = Inventory->TryUseFirstConsumable(
		EAuraConsumableEffect::Health, UseFailure);
	UseFailure.Reset();
	const bool bManaUsed = Inventory->TryUseFirstConsumable(
		EAuraConsumableEffect::Mana, UseFailure);
	const float HealthAfterUse = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float ManaAfterUse = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const bool bUsePassed = bHealthUsed && bManaUsed
		&& FMath::IsNearlyEqual(HealthAfterUse, 75.f, 0.1f)
		&& FMath::IsNearlyEqual(ManaAfterUse, 30.f, 0.1f)
		&& Inventory->GetTotalQuantity(HealthPotion->ItemId) == 6
		&& Inventory->GetTotalQuantity(ManaPotion->ItemId) == 1;

	int32 WeaponSlot = INDEX_NONE;
	int32 ArmorSlot = INDEX_NONE;
	for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (Entry.Item.Definition == Weapon) WeaponSlot = Entry.SlotIndex;
		if (Entry.Item.Definition == Armor) ArmorSlot = Entry.SlotIndex;
	}
	FString MoveFailure;
	const bool bMoved = WeaponSlot != INDEX_NONE
		&& Inventory->TryMoveItem(WeaponSlot, Inventory->GetCapacity() - 1, MoveFailure)
		&& Inventory->FindEntryAtSlot(Inventory->GetCapacity() - 1)
		&& Inventory->FindEntryAtSlot(Inventory->GetCapacity() - 1)->Item.Definition == Weapon;
	AAuraWorldItem* DroppedActor = nullptr;
	FString DropFailure;
	const bool bDropped = ArmorSlot != INDEX_NONE
		&& Inventory->TryDropSlot(ArmorSlot, 1, DropFailure, &DroppedActor);
	const int32 RepickedQuantity = DroppedActor && !DroppedActor->IsActorBeingDestroyed()
		? DroppedActor->TryPickupBy(AuraCharacter) : 0;
	const bool bDropPickupPassed = bDropped && RepickedQuantity == 1
		&& Inventory->GetTotalQuantity(Armor->ItemId) == 1;

	while (Inventory->GetOccupiedSlotCount() < Inventory->GetCapacity())
	{
		if (Add(Shoes, 1) != 1) break;
	}
	FString FullFailure;
	const int32 OverflowAdded = Inventory->TryAddItem(MakeItem(Head, 1), FullFailure);
	AAuraWorldItem* FullInventoryWorldItem = nullptr;
	const FTransform FullInventoryItemTransform(
		AuraCharacter->GetActorRotation(), AuraCharacter->GetActorLocation() + FVector(0.f, 0.f, 300.f));
	FullInventoryWorldItem = GetWorld()->SpawnActorDeferred<AAuraWorldItem>(
		AAuraWorldItem::StaticClass(), FullInventoryItemTransform,
		AuraCharacter, AuraCharacter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (FullInventoryWorldItem)
	{
		FullInventoryWorldItem->InitializeItem(MakeItem(Head, 1), nullptr, 0.f, false);
		FullInventoryWorldItem->FinishSpawning(FullInventoryItemTransform);
	}
	const int32 FullInventoryPickupAdded = FullInventoryWorldItem
		? FullInventoryWorldItem->TryPickupBy(AuraCharacter) : INDEX_NONE;
	const bool bWorldItemRetained = FullInventoryWorldItem
		&& !FullInventoryWorldItem->IsActorBeingDestroyed()
		&& FullInventoryWorldItem->GetItemInstance().Quantity == 1;
	const bool bCapacityPassed = Inventory->GetOccupiedSlotCount() == Inventory->GetCapacity()
		&& OverflowAdded == 0 && FullFailure.Contains(TEXT("full"), ESearchCase::IgnoreCase)
		&& FullInventoryPickupAdded == 0 && bWorldItemRetained;
	if (FullInventoryWorldItem) FullInventoryWorldItem->Destroy();

	Inventory->DebugClearInventory();
	Add(HealthPotion, 3);
	Add(ManaPotion, 3);
	Add(Weapon, 1, EAuraItemRarity::Fine);
	Add(Armor, 1, EAuraItemRarity::Quality);
	Add(Head, 1, EAuraItemRarity::Epic);
	Add(Shoes, 1);
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), OriginalHealth);
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), OriginalMana);

	const bool bPassed = bCatalogValid && bStackingPassed && bUsePassed && bMoved
		&& bDropPickupPassed && bCapacityPassed;
	UE_LOG(LogAuraInput, Display,
		TEXT("Inventory smoke test completed. Passed=%s Catalog=%s Stacking=%s Use=%s Move=%s DropPickup=%s Capacity=%s HealthAfter=%.1f ManaAfter=%.1f FinalSlots=%d FinalHealthPotions=%d FinalManaPotions=%d"),
		bPassed ? TEXT("true") : TEXT("false"), bCatalogValid ? TEXT("true") : TEXT("false"),
		bStackingPassed ? TEXT("true") : TEXT("false"), bUsePassed ? TEXT("true") : TEXT("false"),
		bMoved ? TEXT("true") : TEXT("false"), bDropPickupPassed ? TEXT("true") : TEXT("false"),
		bCapacityPassed ? TEXT("true") : TEXT("false"), HealthAfterUse, ManaAfterUse,
		Inventory->GetOccupiedSlotCount(), Inventory->GetTotalQuantity(HealthPotion->ItemId),
		Inventory->GetTotalQuantity(ManaPotion->ItemId));
	AuraLogInventory();

	if (bPassed && FParse::Param(FCommandLine::Get(), TEXT("AuraInventoryRespawnSmoke")))
	{
		const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
		FTimerHandle InventoryRespawnKillTimer;
		GetWorldTimerManager().SetTimer(InventoryRespawnKillTimer, [WeakController]()
		{
			AAuraPlayerController* Controller = WeakController.Get();
			AAuraCharacter* Character = Controller
				? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
			UAbilitySystemComponent* CurrentAbilitySystem = Character
				? Character->GetAbilitySystemComponent() : nullptr;
			if (!Controller || !Character || !CurrentAbilitySystem) return;
			CurrentAbilitySystem->SetNumericAttributeBase(
				UAuraAttributeSet::GetHealthAttribute(), 0.f);
			UE_LOG(LogAuraInput, Display,
				TEXT("Inventory respawn smoke applied lethal health change. Character=%s"),
				*GetNameSafe(Character));
		}, 0.25f, false);

		FTimerHandle InventoryRespawnVerifyTimer;
		GetWorldTimerManager().SetTimer(InventoryRespawnVerifyTimer, [WeakController]()
		{
			AAuraPlayerController* Controller = WeakController.Get();
			AAuraCharacter* Character = Controller
				? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
			UAuraInventoryComponent* CurrentInventory = Controller
				? Controller->GetAuraInventory() : nullptr;
			const UAuraItemCatalog& CurrentCatalog = UAuraItemCatalog::Get();
			const UAuraItemDefinition* CurrentHealthPotion =
				CurrentCatalog.FindItem(TEXT("Consumable.HealthPotion"));
			const UAuraItemDefinition* CurrentManaPotion =
				CurrentCatalog.FindItem(TEXT("Consumable.ManaPotion"));
			const bool bRespawnPassed = Controller && Character && !Character->IsDead()
				&& CurrentInventory && CurrentHealthPotion && CurrentManaPotion
				&& CurrentInventory->GetOccupiedSlotCount() == 6
				&& CurrentInventory->GetTotalQuantity(CurrentHealthPotion->ItemId) == 3
				&& CurrentInventory->GetTotalQuantity(CurrentManaPotion->ItemId) == 3;
			UE_LOG(LogAuraInput, Display,
				TEXT("Inventory respawn persistence completed. Passed=%s Character=%s Dead=%s Slots=%d HealthPotions=%d ManaPotions=%d"),
				bRespawnPassed ? TEXT("true") : TEXT("false"), *GetNameSafe(Character),
				Character && Character->IsDead() ? TEXT("true") : TEXT("false"),
				CurrentInventory ? CurrentInventory->GetOccupiedSlotCount() : -1,
				CurrentInventory && CurrentHealthPotion
					? CurrentInventory->GetTotalQuantity(CurrentHealthPotion->ItemId) : -1,
				CurrentInventory && CurrentManaPotion
					? CurrentInventory->GetTotalQuantity(CurrentManaPotion->ItemId) : -1);
		}, 5.25f, false);
	}
}

void AAuraPlayerController::RunEquipmentSmoke()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAuraEquipmentComponent* Equipment = GetAuraEquipment();
	UAbilitySystemComponent* AbilitySystem = AuraCharacter
		? AuraCharacter->GetAbilitySystemComponent() : nullptr;
	const UAuraItemCatalog& Catalog = UAuraItemCatalog::Get();
	UAuraItemDefinition* Weapon = Catalog.FindItem(TEXT("Equipment.ArcaneStaff"));
	UAuraItemDefinition* Armor = Catalog.FindItem(TEXT("Equipment.WardenArmor"));
	UAuraItemDefinition* Head = Catalog.FindItem(TEXT("Equipment.SeerHeadpiece"));
	UAuraItemDefinition* Shoes = Catalog.FindItem(TEXT("Equipment.WindShoes"));
	if (!HasAuthority() || !AuraCharacter || !Inventory || !Equipment || !AbilitySystem
		|| !Weapon || !Armor || !Head || !Shoes)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Equipment smoke setup failed. Authority=%s Character=%s Inventory=%s Equipment=%s ASC=%s Definitions=%s/%s/%s/%s"),
			HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(AuraCharacter),
			*GetNameSafe(Inventory), *GetNameSafe(Equipment), *GetNameSafe(AbilitySystem),
			*GetNameSafe(Weapon), *GetNameSafe(Armor), *GetNameSafe(Head),
			*GetNameSafe(Shoes));
		return;
	}

	Equipment->DebugClearEquipment();
	Inventory->DebugClearInventory();
	const TArray<FGameplayAttribute> Attributes = {
		UAuraAttributeSet::GetMaxHealthAttribute(), UAuraAttributeSet::GetMaxManaAttribute(),
		UAuraAttributeSet::GetStrengthAttribute(), UAuraAttributeSet::GetIntelligenceAttribute(),
		UAuraAttributeSet::GetArmorAttribute(), UAuraAttributeSet::GetMoveSpeedAttribute()
	};
	const TArray<FGameplayTag> StatTags = {
		FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.MaxHealth")),
		FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.MaxMana")),
		FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Strength")),
		FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Intelligence")),
		FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Armor")),
		FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.MoveSpeed"))
	};
	TArray<float> BaselineAttributes;
	for (const FGameplayAttribute& Attribute : Attributes)
	{
		BaselineAttributes.Add(AbilitySystem->GetNumericAttribute(Attribute));
	}
	const float BaselineDamageMultiplier = AuraCharacter->GetAbilityDamageMultiplier();
	const float BaselineWalkSpeed = AuraCharacter->GetCharacterMovement()->MaxWalkSpeed;

	const auto MakeEquipment = [](UAuraItemDefinition* Definition,
		const EAuraItemRarity Rarity, const int32 Level, const int32 Seed)
	{
		FAuraItemInstance Item;
		Item.Definition = Definition;
		Item.Quantity = 1;
		Item.Rarity = Rarity;
		Item.ItemLevel = Level;
		Item.EnsureIdentity();
		AuraItems::RollEquipmentModifiers(Item, Seed);
		return Item;
	};
	const auto AddItem = [Inventory](const FAuraItemInstance& Item)
	{
		FString Failure;
		return Inventory->TryAddItem(Item, Failure, false) == 1
			&& Inventory->FindEntryByInstanceId(Item.InstanceId) != nullptr;
	};
	const auto EquipItem = [Equipment](const FGuid& InstanceId)
	{
		FString Failure;
		return Equipment->TryEquipItem(InstanceId, Failure);
	};
	const auto StatsMatch = [AbilitySystem, Equipment, &Attributes,
		&StatTags, &BaselineAttributes]()
	{
		for (int32 Index = 0; Index < Attributes.Num(); ++Index)
		{
			const float Expected = BaselineAttributes[Index]
				+ Equipment->GetEquippedStatMagnitude(StatTags[Index]);
			const float Actual = AbilitySystem->GetNumericAttribute(Attributes[Index]);
			if (!FMath::IsNearlyEqual(Actual, Expected, 0.15f)) return false;
		}
		return true;
	};

	FAuraItemInstance DeterministicA = MakeEquipment(
		Head, EAuraItemRarity::Legendary, 7, 7007);
	FAuraItemInstance DeterministicB = MakeEquipment(
		Head, EAuraItemRarity::Legendary, 7, 7007);
	const bool bQualityRulesPassed = FMath::IsNearlyEqual(
		AuraItems::GetRarityMultiplier(EAuraItemRarity::Common), 1.f)
		&& FMath::IsNearlyEqual(AuraItems::GetRarityMultiplier(EAuraItemRarity::Quality), 1.08f)
		&& FMath::IsNearlyEqual(AuraItems::GetRarityMultiplier(EAuraItemRarity::Fine), 1.18f)
		&& FMath::IsNearlyEqual(AuraItems::GetRarityMultiplier(EAuraItemRarity::Epic), 1.32f)
		&& FMath::IsNearlyEqual(AuraItems::GetRarityMultiplier(EAuraItemRarity::Legendary), 1.5f)
		&& AuraItems::GetRarityAffixCount(EAuraItemRarity::Common) == 0
		&& AuraItems::GetRarityAffixCount(EAuraItemRarity::Quality) == 1
		&& AuraItems::GetRarityAffixCount(EAuraItemRarity::Fine) == 2
		&& AuraItems::GetRarityAffixCount(EAuraItemRarity::Epic) == 3
		&& AuraItems::GetRarityAffixCount(EAuraItemRarity::Legendary) == 4;
	const bool bDeterminismPassed = DeterministicA.RollSeed == 7007
		&& DeterministicA.RolledModifiers.Num() == 4
		&& DeterministicA.RolledModifiers == DeterministicB.RolledModifiers;

	const FAuraItemInstance WeaponItem = MakeEquipment(
		Weapon, EAuraItemRarity::Fine, 2, 1102);
	const FAuraItemInstance ArmorItem = MakeEquipment(
		Armor, EAuraItemRarity::Quality, 2, 2202);
	const FAuraItemInstance HeadItem = MakeEquipment(
		Head, EAuraItemRarity::Epic, 2, 3302);
	const FAuraItemInstance ShoesItem = MakeEquipment(
		Shoes, EAuraItemRarity::Legendary, 2, 4402);
	const bool bAdded = AddItem(WeaponItem) && AddItem(ArmorItem)
		&& AddItem(HeadItem) && AddItem(ShoesItem);
	const bool bEquipped = bAdded && EquipItem(WeaponItem.InstanceId)
		&& EquipItem(ArmorItem.InstanceId) && EquipItem(HeadItem.InstanceId)
		&& EquipItem(ShoesItem.InstanceId);
	const float ExpectedIntelligenceBonus = Equipment->GetEquippedStatMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Intelligence")));
	const bool bInitialStatsPassed = bEquipped && Equipment->GetEquippedCount() == 4
		&& Equipment->GetActiveEffectCountForDebug() == 4 && StatsMatch()
		&& FMath::IsNearlyEqual(AuraCharacter->GetAbilityDamageMultiplier(),
			BaselineDamageMultiplier + FMath::Min(0.5f, ExpectedIntelligenceBonus * 0.01f), 0.002f)
		&& FMath::IsNearlyEqual(AuraCharacter->GetCharacterMovement()->MaxWalkSpeed,
			BaselineWalkSpeed + AbilitySystem->GetNumericAttribute(
				UAuraAttributeSet::GetMoveSpeedAttribute()), 0.15f);

	const FAuraItemInstance ReplacementWeapon = MakeEquipment(
		Weapon, EAuraItemRarity::Legendary, 3, 5503);
	const bool bReplacementAdded = AddItem(ReplacementWeapon);
	const bool bReplaced = bReplacementAdded && EquipItem(ReplacementWeapon.InstanceId);
	const FAuraItemInstance* EquippedWeapon = Equipment->GetEquippedItem(
		EAuraEquipmentSlot::Weapon);
	const bool bReplacementPassed = bReplaced && EquippedWeapon
		&& EquippedWeapon->InstanceId == ReplacementWeapon.InstanceId
		&& Inventory->FindEntryByInstanceId(WeaponItem.InstanceId)
		&& Equipment->GetEquippedCount() == 4
		&& Equipment->GetActiveEffectCountForDebug() == 4 && StatsMatch();

	FString UnequipFailure;
	const bool bUnequipped = Equipment->TryUnequipSlot(
		EAuraEquipmentSlot::Armor, UnequipFailure);
	const bool bUnequipPassed = bUnequipped
		&& !Equipment->GetEquippedItem(EAuraEquipmentSlot::Armor)
		&& Inventory->FindEntryByInstanceId(ArmorItem.InstanceId)
		&& Equipment->GetEquippedCount() == 3
		&& Equipment->GetActiveEffectCountForDebug() == 3 && StatsMatch();
	const bool bReequipped = bUnequipped && EquipItem(ArmorItem.InstanceId);
	const bool bReequipPassed = bReequipped
		&& Equipment->GetEquippedCount() == 4
		&& Equipment->GetActiveEffectCountForDebug() == 4 && StatsMatch();

	TArray<float> BeforeRefresh;
	for (const FGameplayAttribute& Attribute : Attributes)
	{
		BeforeRefresh.Add(AbilitySystem->GetNumericAttribute(Attribute));
	}
	Equipment->RefreshEquipmentEffects();
	Equipment->RefreshEquipmentEffects();
	bool bRefreshPassed = Equipment->GetActiveEffectCountForDebug() == 4;
	for (int32 Index = 0; Index < Attributes.Num(); ++Index)
	{
		bRefreshPassed &= FMath::IsNearlyEqual(BeforeRefresh[Index],
			AbilitySystem->GetNumericAttribute(Attributes[Index]), 0.15f);
	}

	const float MaxHealth = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	const float TestStartHealth = FMath::Min(MaxHealth, 100.f);
	const float ArmorValue = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetArmorAttribute());
	AbilitySystem->SetNumericAttributeBase(
		UAuraAttributeSet::GetHealthAttribute(), TestStartHealth);
	const auto ApplyTestDamage = [AbilitySystem, AuraCharacter](
		const float RawDamage, const bool bBypassArmor)
	{
		FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
		Context.AddInstigator(AuraCharacter, AuraCharacter);
		FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(
			UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
		if (!Spec.IsValid()) return false;
		Spec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -RawDamage);
		if (bBypassArmor)
		{
			Spec.Data->AddDynamicAssetTag(
				FGameplayTag::RequestGameplayTag(TEXT("Damage.BypassArmor")));
		}
		AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		return true;
	};
	const bool bMitigatedApplied = ApplyTestDamage(40.f, false);
	const float ExpectedAfterArmor = TestStartHealth
		- UAuraAttributeSet::CalculateMitigatedDamage(40.f, ArmorValue);
	const float HealthAfterArmor = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	const bool bBypassApplied = ApplyTestDamage(10.f, true);
	const float HealthAfterBypass = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	const bool bArmorPassed = bMitigatedApplied && bBypassApplied
		&& FMath::IsNearlyEqual(HealthAfterArmor, ExpectedAfterArmor, 0.15f)
		&& FMath::IsNearlyEqual(HealthAfterBypass, ExpectedAfterArmor - 10.f, 0.15f);
	AbilitySystem->SetNumericAttributeBase(
		UAuraAttributeSet::GetHealthAttribute(), MaxHealth);

	const bool bPassed = bQualityRulesPassed && bDeterminismPassed
		&& bInitialStatsPassed && bReplacementPassed && bUnequipPassed
		&& bReequipPassed && bRefreshPassed && bArmorPassed;
	UE_LOG(LogAuraInput, Display,
		TEXT("Equipment smoke test completed. Passed=%s Quality=%s Determinism=%s InitialStats=%s Replacement=%s Unequip=%s Reequip=%s Idempotence=%s Armor=%s Equipped=%d Effects=%d ArmorValue=%.1f Damage40After=%.2f Bypass10After=%.2f"),
		bPassed ? TEXT("true") : TEXT("false"),
		bQualityRulesPassed ? TEXT("true") : TEXT("false"),
		bDeterminismPassed ? TEXT("true") : TEXT("false"),
		bInitialStatsPassed ? TEXT("true") : TEXT("false"),
		bReplacementPassed ? TEXT("true") : TEXT("false"),
		bUnequipPassed ? TEXT("true") : TEXT("false"),
		bReequipPassed ? TEXT("true") : TEXT("false"),
		bRefreshPassed ? TEXT("true") : TEXT("false"),
		bArmorPassed ? TEXT("true") : TEXT("false"),
		Equipment->GetEquippedCount(), Equipment->GetActiveEffectCountForDebug(),
		ArmorValue, HealthAfterArmor, HealthAfterBypass);
	AuraLogEquipment();

	if (bPassed && FParse::Param(FCommandLine::Get(), TEXT("AuraEquipmentRespawnSmoke")))
	{
		TArray<float> ExpectedAttributes;
		for (const FGameplayAttribute& Attribute : Attributes)
		{
			ExpectedAttributes.Add(AbilitySystem->GetNumericAttribute(Attribute));
		}
		const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
		FTimerHandle EquipmentRespawnKillTimer;
		GetWorldTimerManager().SetTimer(EquipmentRespawnKillTimer,
			[WeakController]()
			{
				AAuraPlayerController* Controller = WeakController.Get();
				AAuraCharacter* Character = Controller
					? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
				UAbilitySystemComponent* CurrentASC = Character
					? Character->GetAbilitySystemComponent() : nullptr;
				if (!CurrentASC) return;
				CurrentASC->SetNumericAttributeBase(
					UAuraAttributeSet::GetHealthAttribute(), 0.f);
				UE_LOG(LogAuraInput, Display,
					TEXT("Equipment respawn smoke applied lethal health change. Character=%s"),
					*GetNameSafe(Character));
			}, 0.25f, false);

		FTimerHandle EquipmentRespawnVerifyTimer;
		GetWorldTimerManager().SetTimer(EquipmentRespawnVerifyTimer,
			[WeakController, Attributes, ExpectedAttributes]()
			{
				AAuraPlayerController* Controller = WeakController.Get();
				AAuraCharacter* Character = Controller
					? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
				UAuraEquipmentComponent* CurrentEquipment = Controller
					? Controller->GetAuraEquipment() : nullptr;
				UAbilitySystemComponent* CurrentASC = Character
					? Character->GetAbilitySystemComponent() : nullptr;
				bool bAttributesPassed = CurrentASC
					&& Attributes.Num() == ExpectedAttributes.Num();
				for (int32 Index = 0; bAttributesPassed && Index < Attributes.Num(); ++Index)
				{
					bAttributesPassed &= FMath::IsNearlyEqual(
						CurrentASC->GetNumericAttribute(Attributes[Index]),
						ExpectedAttributes[Index], 0.15f);
				}
				const bool bRespawnPassed = Controller && Character && !Character->IsDead()
					&& CurrentEquipment && CurrentEquipment->GetEquippedCount() == 4
					&& CurrentEquipment->GetActiveEffectCountForDebug() == 4
					&& bAttributesPassed
					&& FMath::IsNearlyEqual(CurrentASC->GetNumericAttribute(
						UAuraAttributeSet::GetHealthAttribute()), CurrentASC->GetNumericAttribute(
						UAuraAttributeSet::GetMaxHealthAttribute()), 0.15f);
				UE_LOG(LogAuraInput, Display,
					TEXT("Equipment respawn persistence completed. Passed=%s Character=%s Dead=%s Equipped=%d Effects=%d Attributes=%s Health=%.1f/%.1f"),
					bRespawnPassed ? TEXT("true") : TEXT("false"), *GetNameSafe(Character),
					Character && Character->IsDead() ? TEXT("true") : TEXT("false"),
					CurrentEquipment ? CurrentEquipment->GetEquippedCount() : -1,
					CurrentEquipment ? CurrentEquipment->GetActiveEffectCountForDebug() : -1,
					bAttributesPassed ? TEXT("true") : TEXT("false"),
					CurrentASC ? CurrentASC->GetNumericAttribute(
						UAuraAttributeSet::GetHealthAttribute()) : -1.f,
					CurrentASC ? CurrentASC->GetNumericAttribute(
						UAuraAttributeSet::GetMaxHealthAttribute()) : -1.f);
			}, 5.25f, false);
	}
}

void AAuraPlayerController::RunLootSmoke()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* AuraState = GetPlayerState<AAuraPlayerState>();
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAuraLootSubsystem* LootSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UAuraLootSubsystem>() : nullptr;
	const UAuraLootProfileData& LootData = UAuraLootProfileData::Get();
	const UAuraItemCatalog& Catalog = UAuraItemCatalog::Get();
	UAuraItemDefinition* HealthPotion = Catalog.FindItem(TEXT("Consumable.HealthPotion"));
	UAuraItemDefinition* Shoes = Catalog.FindItem(TEXT("Equipment.WindShoes"));
	FString LootDataError;
	FString CatalogError;
	const bool bLootDataValid = LootData.Validate(LootDataError);
	const bool bCatalogValid = Catalog.Validate(CatalogError);
	if (!HasAuthority() || !AuraCharacter || !AuraState || !Inventory || !LootSubsystem
		|| !HealthPotion || !Shoes || !bLootDataValid || !bCatalogValid)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Loot smoke setup failed. Authority=%s Character=%s PlayerState=%s Inventory=%s Subsystem=%s Health=%s Shoes=%s LootData=%s Catalog=%s LootError=%s CatalogError=%s"),
			HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(AuraCharacter),
			*GetNameSafe(AuraState), *GetNameSafe(Inventory), *GetNameSafe(LootSubsystem),
			*GetNameSafe(HealthPotion), *GetNameSafe(Shoes),
			bLootDataValid ? TEXT("valid") : TEXT("invalid"),
			bCatalogValid ? TEXT("valid") : TEXT("invalid"),
			LootDataError.IsEmpty() ? TEXT("None") : *LootDataError,
			CatalogError.IsEmpty() ? TEXT("None") : *CatalogError);
		return;
	}

	for (TActorIterator<AAuraWorldItem> It(GetWorld()); It; ++It)
	{
		if (It->IsExclusiveOwnerDrop()) It->Destroy();
	}

	const TArray<FAuraItemInstance> DeterministicA = LootSubsystem->RollLoot(
		EAuraLootProfileType::Boss, 3, 424242);
	const TArray<FAuraItemInstance> DeterministicB = LootSubsystem->RollLoot(
		EAuraLootProfileType::Boss, 3, 424242);
	bool bDeterministic = DeterministicA.Num() == DeterministicB.Num();
	for (int32 Index = 0; bDeterministic && Index < DeterministicA.Num(); ++Index)
	{
		const FAuraItemInstance& Left = DeterministicA[Index];
		const FAuraItemInstance& Right = DeterministicB[Index];
		bDeterministic = Left.Definition == Right.Definition
			&& Left.Quantity == Right.Quantity && Left.ItemLevel == Right.ItemLevel
			&& Left.Rarity == Right.Rarity && Left.RollSeed == Right.RollSeed
			&& Left.RolledModifiers == Right.RolledModifiers;
	}

	constexpr int32 DistributionIterations = 1000;
	int32 CommonDropCount = 0;
	int32 BossDropCount = 0;
	int32 BossEquipmentCount = 0;
	int32 BossLegendaryCount = 0;
	bool bGeneratedItemsValid = true;
	for (int32 Iteration = 1; Iteration <= DistributionIterations; ++Iteration)
	{
		CommonDropCount += LootSubsystem->RollLoot(
			EAuraLootProfileType::CommonEnemy, 3, 100000 + Iteration).Num();
		const TArray<FAuraItemInstance> BossItems = LootSubsystem->RollLoot(
			EAuraLootProfileType::Boss, 3, 200000 + Iteration);
		BossDropCount += BossItems.Num();
		for (const FAuraItemInstance& Item : BossItems)
		{
			bGeneratedItemsValid &= Item.IsValid() && Item.Definition;
			if (!Item.Definition) continue;
			if (Item.Definition->ItemType == EAuraItemType::Equipment)
			{
				++BossEquipmentCount;
				if (Item.Rarity == EAuraItemRarity::Legendary) ++BossLegendaryCount;
				bGeneratedItemsValid &= Item.Quantity == 1
					&& Item.RolledModifiers.Num()
						== AuraItems::GetRarityAffixCount(Item.Rarity)
					&& Item.ItemLevel >= 1;
			}
			else
			{
				bGeneratedItemsValid &= Item.Definition->ItemType == EAuraItemType::Consumable
					&& Item.Rarity == EAuraItemRarity::Common && Item.Quantity == 1;
			}
		}
	}
	const FAuraLootProfile& CommonProfile = LootData.CommonEnemy;
	const FAuraLootProfile& BossProfile = LootData.Boss;
	const float ExpectedCommonDrops = DistributionIterations
		* (CommonProfile.GuaranteedRolls
			+ CommonProfile.BonusRolls * CommonProfile.BonusRollChance);
	const float ExpectedBossDrops = DistributionIterations
		* (BossProfile.GuaranteedRolls
			+ BossProfile.BonusRolls * BossProfile.BonusRollChance);
	const bool bCommonDistribution = FMath::Abs(
		CommonDropCount - ExpectedCommonDrops) <= FMath::Max(50.f, ExpectedCommonDrops * 0.12f);
	const bool bBossDistribution = FMath::Abs(
		BossDropCount - ExpectedBossDrops) <= FMath::Max(60.f, ExpectedBossDrops * 0.06f);
	const float EquipmentRate = BossDropCount > 0
		? static_cast<float>(BossEquipmentCount) / BossDropCount : 0.f;
	const bool bEquipmentDistribution = FMath::Abs(
		EquipmentRate - BossProfile.EquipmentChance) <= 0.05f;
	float LegendaryWeight = 0.f;
	float TotalRarityWeight = 0.f;
	for (const FAuraLootRarityWeight& Weight : BossProfile.RarityWeights)
	{
		TotalRarityWeight += FMath::Max(0.f, Weight.Weight);
		if (Weight.Rarity == EAuraItemRarity::Legendary)
		{
			LegendaryWeight += FMath::Max(0.f, Weight.Weight);
		}
	}
	const float ExpectedLegendaryRate = TotalRarityWeight > 0.f
		? LegendaryWeight / TotalRarityWeight : 0.f;
	const float LegendaryRate = BossEquipmentCount > 0
		? static_cast<float>(BossLegendaryCount) / BossEquipmentCount : 0.f;
	const bool bRarityDistribution = FMath::Abs(
		LegendaryRate - ExpectedLegendaryRate) <= 0.04f;
	const bool bDistributionPassed = bCommonDistribution && bBossDistribution
		&& bEquipmentDistribution && bRarityDistribution && bGeneratedItemsValid;

	Inventory->DebugClearInventory();
	const auto MakeTestItem = [](UAuraItemDefinition* Definition)
	{
		FAuraItemInstance Item;
		Item.Definition = Definition;
		Item.Quantity = 1;
		Item.Rarity = EAuraItemRarity::Common;
		Item.ItemLevel = 1;
		Item.SourceType = EAuraItemSourceType::Debug;
		Item.SourceId = TEXT("LootSmoke");
		Item.EnsureIdentity();
		return Item;
	};
	for (int32 Slot = 0; Slot < Inventory->GetCapacity(); ++Slot)
	{
		FString Failure;
		Inventory->TryAddItem(MakeTestItem(Shoes), Failure, false);
	}
	const FVector NearLocation = AuraCharacter->GetActorLocation()
		+ AuraCharacter->GetActorForwardVector().GetSafeNormal2D() * 170.f
		+ FVector(0.f, 0.f, 25.f);
	AAuraWorldItem* FullInventoryItem = LootSubsystem->SpawnSpecificPersonalItem(
		*AuraState, MakeTestItem(HealthPotion), NearLocation, 120.f);
	const int32 FullPickup = FullInventoryItem
		? FullInventoryItem->TryPickupBy(AuraCharacter) : INDEX_NONE;
	const bool bFullInventoryRetention = FullInventoryItem && FullPickup == 0
		&& !FullInventoryItem->IsActorBeingDestroyed()
		&& FullInventoryItem->GetItemInstance().Quantity == 1;
	Inventory->DebugClearInventory();
	const int32 CorrectPickup = FullInventoryItem
		? FullInventoryItem->TryPickupBy(AuraCharacter) : 0;
	const FAuraInventoryEntry* PickedEntry = Inventory->GetEntries().IsEmpty()
		? nullptr : &Inventory->GetEntries()[0];
	const bool bCorrectPickup = CorrectPickup == 1 && PickedEntry
		&& PickedEntry->Item.Definition == HealthPotion
		&& PickedEntry->Item.SourceType == EAuraItemSourceType::Debug;

	AAuraWorldItem* DistantItem = LootSubsystem->SpawnSpecificPersonalItem(
		*AuraState, MakeTestItem(HealthPotion),
		AuraCharacter->GetActorLocation()
			+ AuraCharacter->GetActorForwardVector().GetSafeNormal2D() * 600.f,
		120.f);
	const bool bDistanceRejected = DistantItem
		&& DistantItem->TryPickupBy(AuraCharacter) == 0
		&& !DistantItem->IsActorBeingDestroyed();
	if (DistantItem) DistantItem->Destroy();

	APlayerState* OtherPlayerState = nullptr;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (APlayerState* Candidate : GameState->PlayerArray)
		{
			if (Candidate && Candidate != AuraState)
			{
				OtherPlayerState = Candidate;
				break;
			}
		}
	}
	bool bWrongOwnerRejected = true;
	bool bWrongOwnerHidden = true;
	if (OtherPlayerState)
	{
		AAuraWorldItem* ForeignItem = LootSubsystem->SpawnSpecificPersonalItem(
			*OtherPlayerState, MakeTestItem(HealthPotion), NearLocation, 120.f);
		bWrongOwnerHidden = ForeignItem && !ForeignItem->IsVisibleToLocalPlayer();
		bWrongOwnerRejected = ForeignItem
			&& ForeignItem->TryPickupBy(AuraCharacter) == 0
			&& !ForeignItem->IsActorBeingDestroyed();
		if (ForeignItem) ForeignItem->Destroy();
	}

	AAuraWorldItem* ExpiringItem = LootSubsystem->SpawnSpecificPersonalItem(
		*AuraState, MakeTestItem(HealthPotion),
		AuraCharacter->GetActorLocation()
			- AuraCharacter->GetActorForwardVector().GetSafeNormal2D() * 170.f,
		0.35f);
	const bool bExpirationArmed = ExpiringItem && ExpiringItem->GetLifeSpan() > 0.f;
	const TWeakObjectPtr<AAuraWorldItem> WeakExpiringItem(ExpiringItem);
	FTimerHandle ExpirationVerifyTimer;
	GetWorldTimerManager().SetTimer(ExpirationVerifyTimer, [WeakExpiringItem]()
	{
		UE_LOG(LogAuraInput, Display,
			TEXT("Personal loot expiration smoke completed. Passed=%s"),
			!WeakExpiringItem.IsValid() ? TEXT("true") : TEXT("false"));
	}, 0.8f, false);

	FActorSpawnParameters EnemySpawnParameters;
	EnemySpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* LootTestEnemy = GetWorld()->SpawnActor<AAuraEnemy>(
		AAuraEnemy::StaticClass(), AuraCharacter->GetActorLocation()
			+ AuraCharacter->GetActorForwardVector().GetSafeNormal2D() * 320.f,
		FRotator::ZeroRotator, EnemySpawnParameters);
	int32 DropsBefore = 0;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (APlayerState* State : GameState->PlayerArray)
		{
			DropsBefore += LootSubsystem->GetActivePersonalDropCount(State);
		}
	}
	if (LootTestEnemy)
	{
		LootTestEnemy->SetActorEnableCollision(false);
		LootTestEnemy->SetLootProfile(EAuraLootProfileType::Boss);
		LootTestEnemy->TryAwardKillRewards(AuraCharacter);
		LootTestEnemy->Destroy();
	}
	int32 DropsAfter = 0;
	bool bEveryPlayerReceivedLoot = LootTestEnemy != nullptr;
	int32 EligiblePlayers = 0;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (APlayerState* State : GameState->PlayerArray)
		{
			if (!State || !State->GetPawn()) continue;
			++EligiblePlayers;
			int32 EnemyDropCount = 0;
			for (TActorIterator<AAuraWorldItem> It(GetWorld()); It; ++It)
			{
				if (It->GetReservedPlayer() == State
					&& It->GetItemInstance().SourceType == EAuraItemSourceType::EnemyDrop)
				{
					++EnemyDropCount;
				}
			}
			bEveryPlayerReceivedLoot &= EnemyDropCount >= BossProfile.GuaranteedRolls;
			DropsAfter += LootSubsystem->GetActivePersonalDropCount(State);
		}
	}
	const bool bEnemyHookPassed = bEveryPlayerReceivedLoot && EligiblePlayers > 0
		&& DropsAfter - DropsBefore >= EligiblePlayers * BossProfile.GuaranteedRolls;
	const bool bSecurityPassed = bFullInventoryRetention && bCorrectPickup
		&& bDistanceRejected && bWrongOwnerRejected && bWrongOwnerHidden
		&& bExpirationArmed;
	const bool bPassed = bLootDataValid && bCatalogValid && bDeterministic
		&& bDistributionPassed && bSecurityPassed && bEnemyHookPassed;
	UE_LOG(LogAuraInput, Display,
		TEXT("Personal loot smoke test completed. Passed=%s Data=%s Catalog=%s Determinism=%s Distribution=%s Security=%s EnemyHook=%s CommonDrops=%d/%.0f BossDrops=%d/%.0f EquipmentRate=%.3f/%.3f LegendaryRate=%.3f/%.3f FullRetention=%s CorrectPickup=%s DistanceRejected=%s WrongOwnerRejected=%s WrongOwnerHidden=%s ExpirationArmed=%s EligiblePlayers=%d DropsBefore=%d DropsAfter=%d"),
		bPassed ? TEXT("true") : TEXT("false"),
		bLootDataValid ? TEXT("true") : TEXT("false"),
		bCatalogValid ? TEXT("true") : TEXT("false"),
		bDeterministic ? TEXT("true") : TEXT("false"),
		bDistributionPassed ? TEXT("true") : TEXT("false"),
		bSecurityPassed ? TEXT("true") : TEXT("false"),
		bEnemyHookPassed ? TEXT("true") : TEXT("false"),
		CommonDropCount, ExpectedCommonDrops, BossDropCount, ExpectedBossDrops,
		EquipmentRate, BossProfile.EquipmentChance,
		LegendaryRate, ExpectedLegendaryRate,
		bFullInventoryRetention ? TEXT("true") : TEXT("false"),
		bCorrectPickup ? TEXT("true") : TEXT("false"),
		bDistanceRejected ? TEXT("true") : TEXT("false"),
		bWrongOwnerRejected ? TEXT("true") : TEXT("false"),
		bWrongOwnerHidden ? TEXT("true") : TEXT("false"),
		bExpirationArmed ? TEXT("true") : TEXT("false"),
		EligiblePlayers, DropsBefore, DropsAfter);
	AuraLogLoot();

	if (bPassed && FParse::Param(FCommandLine::Get(), TEXT("AuraLootRespawnSmoke")))
	{
		const int32 ExpectedDrops = FMath::Max(0,
			LootSubsystem->GetActivePersonalDropCount(AuraState)
			- (WeakExpiringItem.IsValid() ? 1 : 0));
		const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
		FTimerHandle LootRespawnKillTimer;
		GetWorldTimerManager().SetTimer(LootRespawnKillTimer,
			[WeakController]()
			{
				AAuraPlayerController* Controller = WeakController.Get();
				AAuraCharacter* Character = Controller
					? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
				UAbilitySystemComponent* CurrentASC = Character
					? Character->GetAbilitySystemComponent() : nullptr;
				if (CurrentASC)
				{
					CurrentASC->SetNumericAttributeBase(
						UAuraAttributeSet::GetHealthAttribute(), 0.f);
				}
			}, 0.25f, false);

		FTimerHandle LootRespawnVerifyTimer;
		GetWorldTimerManager().SetTimer(LootRespawnVerifyTimer,
			[WeakController, ExpectedDrops]()
			{
				AAuraPlayerController* Controller = WeakController.Get();
				AAuraCharacter* Character = Controller
					? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
				AAuraPlayerState* State = Controller
					? Controller->GetPlayerState<AAuraPlayerState>() : nullptr;
				UAuraLootSubsystem* CurrentLoot = Controller && Controller->GetWorld()
					? Controller->GetWorld()->GetSubsystem<UAuraLootSubsystem>() : nullptr;
				const int32 CurrentDrops = CurrentLoot && State
					? CurrentLoot->GetActivePersonalDropCount(State) : -1;
				const bool bRespawnPassed = Character && !Character->IsDead()
					&& CurrentDrops == ExpectedDrops;
				UE_LOG(LogAuraInput, Display,
					TEXT("Personal loot respawn persistence completed. Passed=%s Dead=%s ExpectedDrops=%d CurrentDrops=%d"),
					bRespawnPassed ? TEXT("true") : TEXT("false"),
					Character && Character->IsDead() ? TEXT("true") : TEXT("false"),
					ExpectedDrops, CurrentDrops);
			}, 5.25f, false);
	}
}

void AAuraPlayerController::RunRoomSmoke()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	const float LateJoinHold = FParse::Param(
		FCommandLine::Get(), TEXT("AuraRoomLateJoinWindow")) ? 20.f : 0.f;
	if (!HasAuthority() || !AuraCharacter || AuraCharacter->IsDead() || !GetWorld())
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Room state machine smoke setup failed. Authority=%s Character=%s Dead=%s"),
			HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(AuraCharacter),
			AuraCharacter && AuraCharacter->IsDead() ? TEXT("true") : TEXT("false"));
		return;
	}

	AAuraEncounterVolume* AmbushRoom = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterType() == EAuraEncounterType::Ambush && It->GetLootChest())
		{
			AmbushRoom = *It;
			break;
		}
	}
	AAuraLootChest* LootChest = AmbushRoom ? AmbushRoom->GetLootChest() : nullptr;
	const bool bLayoutValid = AmbushRoom && LootChest
		&& AmbushRoom->GetEncounterState() == EAuraEncounterState::Dormant
		&& AmbushRoom->GetConfiguredSpawnCount() == 2
		&& FMath::IsNearlyEqual(AmbushRoom->GetArmingDuration(), 1.f, 0.05f)
		&& AmbushRoom->AreControlledGatesOpen() && LootChest->IsGuardLocked();
	if (!bLayoutValid)
	{
		UE_LOG(LogAuraInput, Error,
			TEXT("Room state machine smoke layout invalid. Room=%s Chest=%s State=%d Spawns=%d Arming=%.2f GatesOpen=%s ChestLocked=%s"),
			*GetNameSafe(AmbushRoom), *GetNameSafe(LootChest),
			AmbushRoom ? static_cast<int32>(AmbushRoom->GetEncounterState()) : -1,
			AmbushRoom ? AmbushRoom->GetConfiguredSpawnCount() : -1,
			AmbushRoom ? AmbushRoom->GetArmingDuration() : -1.f,
			AmbushRoom && AmbushRoom->AreControlledGatesOpen() ? TEXT("true") : TEXT("false"),
			LootChest && LootChest->IsGuardLocked() ? TEXT("true") : TEXT("false"));
		return;
	}

	for (TActorIterator<AAuraWorldItem> It(GetWorld()); It; ++It)
	{
		if (It->IsExclusiveOwnerDrop()) It->Destroy();
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	int32 PositionIndex = 0;
	TArray<TWeakObjectPtr<AAuraCharacter>> StagedCharacters;
	TArray<FVector> StagedLocations;
	if (GameState)
	{
		for (APlayerState* State : GameState->PlayerArray)
		{
			AAuraCharacter* TeamCharacter = State
				? Cast<AAuraCharacter>(State->GetPawn()) : nullptr;
			if (!TeamCharacter || TeamCharacter->IsDead()) continue;
			const FVector StagingLocation = TeamCharacter == AuraCharacter
				? AmbushRoom->GetActorTransform().TransformPosition(
					FVector(AmbushRoom->GetArmingEntryPlaneLocalX() - 20.f, 0.f, 96.f))
				: AmbushRoom->GetActorLocation() - AmbushRoom->GetActorForwardVector() * 1700.f
					+ AmbushRoom->GetActorRightVector() * PositionIndex++ * 150.f
					+ FVector(0.f, 0.f, 96.f);
			TeamCharacter->TeleportTo(
				StagingLocation, AmbushRoom->GetActorRotation(), false, true);
			TeamCharacter->GetCharacterMovement()->StopMovementImmediately();
			TeamCharacter->GetCapsuleComponent()->UpdateOverlaps();
			StagedCharacters.Add(TeamCharacter);
			StagedLocations.Add(TeamCharacter->GetActorLocation());
		}
	}

	struct FRoomSmokeState
	{
		bool bBoundarySafe = false;
		bool bOneSecondHold = false;
		bool bDeepRoomDwell = false;
		bool bGateOpenDuringHold = false;
		bool bNoEarlySpawn = false;
		bool bHeldBeforeLock = false;
		bool bSpawnTelegraphWindow = false;
		bool bSpawnTelegraphObserved = false;
		bool bNoForcedTeleport = false;
		bool bLocked = false;
		bool bFirstWave = false;
		bool bLeashRecovered = false;
		bool bDeathTriggered = false;
		bool bRespawnedInside = false;
		bool bFirstWaveProgressed = false;
		bool bSecondWave = false;
		bool bRewardAvailable = false;
		bool bGateReopened = false;
		bool bChestUnlocked = false;
		bool bEveryPlayerClaimed = false;
		bool bDuplicateRejected = false;
		bool bCompleted = false;
		int32 ClaimingPlayers = 0;
	};
	const TSharedRef<FRoomSmokeState> TestState = MakeShared<FRoomSmokeState>();
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakCharacter(AuraCharacter);
	const TWeakObjectPtr<AAuraEncounterVolume> WeakRoom(AmbushRoom);
	const TWeakObjectPtr<AAuraLootChest> WeakChest(LootChest);

	AmbushRoom->StartEncounter(AuraCharacter, false);
	TestState->bBoundarySafe = AmbushRoom->GetEncounterState()
		== EAuraEncounterState::Arming
		&& AmbushRoom->GetArmingTimeRemaining() <= 0.05f;
	TestState->bGateOpenDuringHold = AmbushRoom->AreControlledGatesOpen();
	TestState->bNoEarlySpawn = AmbushRoom->GetActiveEnemyCount() == 0
		&& AmbushRoom->GetRemainingEnemyCount() == 0;
	TestState->bNoForcedTeleport = StagedCharacters.Num() == StagedLocations.Num();
	for (int32 Index = 0; Index < StagedCharacters.Num(); ++Index)
	{
		const AAuraCharacter* StagedCharacter = StagedCharacters[Index].Get();
		TestState->bNoForcedTeleport &= StagedCharacter
			&& FVector::DistSquared(StagedCharacter->GetActorLocation(), StagedLocations[Index])
				< FMath::Square(5.f);
	}

	FTimerHandle EnterRoomTimer;
	GetWorldTimerManager().SetTimer(EnterRoomTimer,
		[WeakCharacter, WeakRoom, TestState]()
	{
		AAuraCharacter* CurrentCharacter = WeakCharacter.Get();
		AAuraEncounterVolume* Room = WeakRoom.Get();
		if (!CurrentCharacter || !Room) return;
		TestState->bBoundarySafe &= Room->GetEncounterState()
			== EAuraEncounterState::Arming
			&& Room->GetArmingTimeRemaining() <= 0.05f
			&& Room->AreControlledGatesOpen()
			&& Room->GetActiveEnemyCount() == 0;
		CurrentCharacter->TeleportTo(
			Room->GetActorTransform().TransformPosition(FVector(
				Room->GetArmingEntryPlaneLocalX() + 20.f, 0.f, 96.f)),
			Room->GetActorRotation(), false, true);
		CurrentCharacter->GetCapsuleComponent()->UpdateOverlaps();
	}, 0.25f, false);

	FTimerHandle DwellStartedTimer;
	GetWorldTimerManager().SetTimer(DwellStartedTimer,
		[WeakCharacter, WeakRoom, TestState]()
	{
		const AAuraCharacter* CurrentCharacter = WeakCharacter.Get();
		const AAuraEncounterVolume* Room = WeakRoom.Get();
		TestState->bOneSecondHold = Room && CurrentCharacter
			&& Room->GetEncounterState() == EAuraEncounterState::Arming
			&& Room->AreControlledGatesOpen()
			&& Room->GetActorTransform().InverseTransformPosition(
				CurrentCharacter->GetActorLocation()).X
				< Room->GetArmingEntryPlaneLocalX() + 60.f
			&& Room->GetArmingTimeRemaining() > 0.65f
			&& Room->GetArmingTimeRemaining() <= 1.1f;
		TestState->bGateOpenDuringHold &= TestState->bOneSecondHold;
		TestState->bNoEarlySpawn &= Room && Room->GetActiveEnemyCount() == 0
			&& Room->GetRemainingEnemyCount() == 0;
	}, 0.4f, false);

	FTimerHandle AdvanceInsideTimer;
	GetWorldTimerManager().SetTimer(AdvanceInsideTimer,
		[WeakCharacter, WeakRoom]()
	{
		AAuraCharacter* CurrentCharacter = WeakCharacter.Get();
		AAuraEncounterVolume* Room = WeakRoom.Get();
		if (!CurrentCharacter || !Room) return;
		CurrentCharacter->TeleportTo(
			Room->GetActorTransform().TransformPosition(FVector(700.f, 0.f, 96.f)),
			Room->GetActorRotation(), false, true);
		CurrentCharacter->GetCapsuleComponent()->UpdateOverlaps();
	}, 0.5f, false);

	FTimerHandle DeepRoomTimer;
	GetWorldTimerManager().SetTimer(DeepRoomTimer,
		[WeakCharacter, WeakRoom, TestState]()
	{
		const AAuraCharacter* CurrentCharacter = WeakCharacter.Get();
		const AAuraEncounterVolume* Room = WeakRoom.Get();
		TestState->bDeepRoomDwell = Room && CurrentCharacter
			&& Room->GetEncounterState() == EAuraEncounterState::Arming
			&& Room->GetActorTransform().InverseTransformPosition(
				CurrentCharacter->GetActorLocation()).X > 600.f
			&& Room->GetArmingTimeRemaining() > 0.35f
			&& Room->GetActiveEnemyCount() == 0;
	}, 0.7f, false);

	FTimerHandle HoldTimer;
	GetWorldTimerManager().SetTimer(HoldTimer,
		[WeakRoom, TestState]()
	{
		const AAuraEncounterVolume* Room = WeakRoom.Get();
		TestState->bHeldBeforeLock = Room
			&& Room->GetEncounterState() == EAuraEncounterState::Arming
			&& Room->AreControlledGatesOpen()
			&& Room->GetActiveEnemyCount() == 0
			&& Room->GetArmingTimeRemaining() > 0.1f;
	}, 0.9f, false);

	FTimerHandle SpawnTelegraphTimer;
	GetWorldTimerManager().SetTimer(SpawnTelegraphTimer,
		[WeakController, WeakRoom, TestState]()
	{
		const AAuraEncounterVolume* Room = WeakRoom.Get();
		TestState->bSpawnTelegraphWindow = Room
			&& Room->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& !Room->AreControlledGatesOpen()
			&& Room->GetActiveEnemyCount() == 0
			&& Room->GetRemainingEnemyCount() == 2;
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			const UNiagaraComponent* Component = *It;
			if (Component && !Component->IsTemplate()
				&& WeakController.IsValid()
				&& Component->GetWorld() == WeakController->GetWorld()
				&& GetNameSafe(Component->GetAsset()) == TEXT("NS_GroundSummon")
				&& Component->IsActive())
			{
				TestState->bSpawnTelegraphObserved = true;
				break;
			}
		}
		TestState->bNoEarlySpawn &= TestState->bSpawnTelegraphWindow;
		if (TestState->bSpawnTelegraphObserved)
		{
			const FString ScreenshotDirectory = FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
			IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(
				ScreenshotDirectory, TEXT("EncounterSpawnTelegraph.png")), false, false);
		}
	}, 1.45f, false);

	FTimerHandle LockTimer;
	GetWorldTimerManager().SetTimer(LockTimer,
		[WeakCharacter, WeakRoom, WeakChest, TestState]()
	{
		AAuraCharacter* CurrentCharacter = WeakCharacter.Get();
		AAuraEncounterVolume* Room = WeakRoom.Get();
		AAuraLootChest* Chest = WeakChest.Get();
		if (!CurrentCharacter || !Room || !Chest) return;
		TestState->bLocked = Room->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& !Room->AreControlledGatesOpen() && Chest->IsGuardLocked();
		TestState->bFirstWave = Room->GetActiveEnemyCount() == 1
			&& Room->GetRemainingEnemyCount() == 2;

		for (AAuraEnemy* Enemy : Room->GetActiveEnemies())
		{
			if (!Enemy) continue;
			Enemy->GetCharacterMovement()->DisableMovement();
			if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
			{
				AIController->StopMovement();
				AIController->SetActorTickEnabled(false);
			}
			Enemy->TeleportTo(Room->GetActorLocation()
				+ Room->GetActorForwardVector() * 5000.f,
				Enemy->GetActorRotation(), false, true);
		}

		if (UAbilitySystemComponent* AbilitySystem = CurrentCharacter->GetAbilitySystemComponent())
		{
			AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 0.f);
		}
		TestState->bDeathTriggered = CurrentCharacter->IsDead();
	}, 2.15f, false);

	FTimerHandle LeashTimer;
	GetWorldTimerManager().SetTimer(LeashTimer,
		[WeakCharacter, WeakRoom, TestState]()
	{
		const AAuraCharacter* CurrentCharacter = WeakCharacter.Get();
		const AAuraEncounterVolume* Room = WeakRoom.Get();
		if (!CurrentCharacter || !Room) return;
		const TArray<AAuraEnemy*> Enemies = Room->GetActiveEnemies();
		TestState->bLeashRecovered = Enemies.Num() == 1
			&& FVector::DistSquared(Enemies[0]->GetActorLocation(), Room->GetActorLocation())
				< FMath::Square(3200.f);
		TestState->bDeathTriggered &= CurrentCharacter->IsDead();
	}, 5.1f, false);

	FTimerHandle RespawnTimer;
	GetWorldTimerManager().SetTimer(RespawnTimer,
		[WeakCharacter, WeakRoom, TestState]()
	{
		AAuraCharacter* CurrentCharacter = WeakCharacter.Get();
		AAuraEncounterVolume* Room = WeakRoom.Get();
		if (!CurrentCharacter || !Room) return;
		const AGameStateBase* CurrentGameState = Room->GetWorld()->GetGameState();
		const APlayerState* State = CurrentCharacter->GetPlayerState();
		const int32 PlayerIndex = CurrentGameState
			? FMath::Max(0, CurrentGameState->PlayerArray.IndexOfByKey(State)) : 0;
		const int32 PlayerCount = CurrentGameState
			? FMath::Max(1, CurrentGameState->PlayerArray.Num()) : 1;
		const FVector ExpectedLocation = Room->GetRoomReentryTransform(
			PlayerIndex, PlayerCount).GetLocation();
		TestState->bRespawnedInside = !CurrentCharacter->IsDead()
			&& Room->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& FVector::DistSquared(CurrentCharacter->GetActorLocation(), ExpectedLocation)
				< FMath::Square(180.f);
		DefeatEncounterEnemiesForSmokeTest(Room);
		TestState->bFirstWaveProgressed = Room->GetActiveEnemyCount() == 0
			&& Room->GetRemainingEnemyCount() == 1
			&& Room->GetEncounterState() == EAuraEncounterState::LockedCombat;
	}, 12.25f + LateJoinHold, false);

	FTimerHandle SecondWaveTimer;
	GetWorldTimerManager().SetTimer(SecondWaveTimer,
		[WeakRoom, WeakChest, WeakCharacter, TestState]()
	{
		AAuraEncounterVolume* Room = WeakRoom.Get();
		AAuraLootChest* Chest = WeakChest.Get();
		AAuraCharacter* PrimaryCharacter = WeakCharacter.Get();
		if (!Room || !Chest || !PrimaryCharacter) return;
		TestState->bSecondWave = Room->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& Room->GetActiveEnemyCount() == 1 && Room->GetRemainingEnemyCount() == 1;
		for (AAuraEnemy* Enemy : Room->GetActiveEnemies())
		{
			if (Enemy)
			{
				Enemy->GetCharacterMovement()->DisableMovement();
				if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
				{
					AIController->StopMovement();
					AIController->SetActorTickEnabled(false);
				}
			}
		}
		DefeatEncounterEnemiesForSmokeTest(Room);
		TestState->bRewardAvailable = Room->GetEncounterState()
			== EAuraEncounterState::RewardAvailable;
		TestState->bGateReopened = Room->AreControlledGatesOpen();
		TestState->bChestUnlocked = !Chest->IsGuardLocked();

		const AGameStateBase* CurrentGameState = Room->GetWorld()->GetGameState();
		bool bAllClaimed = CurrentGameState != nullptr;
		int32 ClaimingPlayers = 0;
		if (CurrentGameState)
		{
			for (APlayerState* State : CurrentGameState->PlayerArray)
			{
				AAuraCharacter* TeamCharacter = State
					? Cast<AAuraCharacter>(State->GetPawn()) : nullptr;
				if (!TeamCharacter || TeamCharacter->IsDead()) continue;
				++ClaimingPlayers;
				const FVector ClaimLocation = Chest->GetActorLocation()
					+ Chest->GetActorRightVector() * ClaimingPlayers * 35.f
					+ FVector(0.f, 0.f, 96.f);
				TeamCharacter->TeleportTo(
					ClaimLocation, Chest->GetActorRotation(), false, true);
				TeamCharacter->GetCapsuleComponent()->UpdateOverlaps();
				if (!Chest->HasBeenClaimedBy(State)) Chest->ClaimForCharacter(TeamCharacter);
				bAllClaimed &= Chest->HasBeenClaimedBy(State);
			}
		}
		TestState->ClaimingPlayers = ClaimingPlayers;
		TestState->bEveryPlayerClaimed = bAllClaimed && ClaimingPlayers > 0;
		TestState->bDuplicateRejected = !Chest->ClaimForCharacter(PrimaryCharacter);
		TestState->bCompleted = Room->GetEncounterState() == EAuraEncounterState::Completed
			&& Chest->IsOpened();
	}, 14.2f + LateJoinHold, false);

	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[WeakController, WeakRoom, WeakChest, TestState, bLayoutValid]()
	{
		const bool bPassed = bLayoutValid && TestState->bBoundarySafe
			&& TestState->bOneSecondHold && TestState->bDeepRoomDwell
			&& TestState->bGateOpenDuringHold && TestState->bNoEarlySpawn
			&& TestState->bHeldBeforeLock && TestState->bSpawnTelegraphWindow
			&& TestState->bSpawnTelegraphObserved
			&& TestState->bNoForcedTeleport && TestState->bLocked
			&& TestState->bFirstWave && TestState->bLeashRecovered
			&& TestState->bDeathTriggered && TestState->bRespawnedInside
			&& TestState->bFirstWaveProgressed && TestState->bSecondWave
			&& TestState->bRewardAvailable && TestState->bGateReopened
			&& TestState->bChestUnlocked && TestState->bEveryPlayerClaimed
			&& TestState->bDuplicateRejected && TestState->bCompleted;
		UE_LOG(LogAuraInput, Display,
			TEXT("Room state machine smoke test completed. Passed=%s Layout=%s BoundarySafe=%s OneSecondHold=%s DeepRoomDwell=%s GateOpenDuringHold=%s NoEarlySpawn=%s HeldBeforeLock=%s SpawnTelegraphWindow=%s SpawnTelegraphObserved=%s NoForcedTeleport=%s Locked=%s FirstWave=%s Leash=%s Death=%s RespawnInside=%s FirstWaveProgress=%s SecondWave=%s RewardAvailable=%s GateReopened=%s ChestUnlocked=%s AllPlayersClaimed=%s DuplicateRejected=%s Completed=%s ClaimingPlayers=%d FinalState=%d"),
			bPassed ? TEXT("true") : TEXT("false"),
			bLayoutValid ? TEXT("true") : TEXT("false"),
			TestState->bBoundarySafe ? TEXT("true") : TEXT("false"),
			TestState->bOneSecondHold ? TEXT("true") : TEXT("false"),
			TestState->bDeepRoomDwell ? TEXT("true") : TEXT("false"),
			TestState->bGateOpenDuringHold ? TEXT("true") : TEXT("false"),
			TestState->bNoEarlySpawn ? TEXT("true") : TEXT("false"),
			TestState->bHeldBeforeLock ? TEXT("true") : TEXT("false"),
			TestState->bSpawnTelegraphWindow ? TEXT("true") : TEXT("false"),
			TestState->bSpawnTelegraphObserved ? TEXT("true") : TEXT("false"),
			TestState->bNoForcedTeleport ? TEXT("true") : TEXT("false"),
			TestState->bLocked ? TEXT("true") : TEXT("false"),
			TestState->bFirstWave ? TEXT("true") : TEXT("false"),
			TestState->bLeashRecovered ? TEXT("true") : TEXT("false"),
			TestState->bDeathTriggered ? TEXT("true") : TEXT("false"),
			TestState->bRespawnedInside ? TEXT("true") : TEXT("false"),
			TestState->bFirstWaveProgressed ? TEXT("true") : TEXT("false"),
			TestState->bSecondWave ? TEXT("true") : TEXT("false"),
			TestState->bRewardAvailable ? TEXT("true") : TEXT("false"),
			TestState->bGateReopened ? TEXT("true") : TEXT("false"),
			TestState->bChestUnlocked ? TEXT("true") : TEXT("false"),
			TestState->bEveryPlayerClaimed ? TEXT("true") : TEXT("false"),
			TestState->bDuplicateRejected ? TEXT("true") : TEXT("false"),
			TestState->bCompleted ? TEXT("true") : TEXT("false"),
			TestState->ClaimingPlayers,
			WeakRoom.IsValid() ? static_cast<int32>(WeakRoom->GetEncounterState()) : -1);
		if (WeakController.IsValid()) WeakController->AuraLogRoomState();
	}, 14.65f + LateJoinHold, false);
}

void AAuraPlayerController::RestartDemo()
{
	if (bIsRunUpgradeSelectionActive) return;
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || (!AuraCharacter->IsDead() && !bDemoCompleted)) return;
	if (AuraCharacter->IsDead() && GetNetMode() != NM_Standalone)
	{
		return;
	}
	if (bDemoCompleted && GetNetMode() != NM_Standalone)
	{
		if (HasAuthority()) ServerRequestPostBossCamp_Implementation();
		else ServerRequestPostBossCamp();
		return;
	}
	if (bDemoCompleted)
	{
		if (TeleportPawnToPostBossCamp())
		{
			FinishPostBossCampReturn();
			if (UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
			{
				Run->RequestPostBossCamp();
				Run->ConsumePostBossCampRequest();
			}
			return;
		}
	}
	else if (AuraCharacter->IsDead())
	{
		if (UAuraRunSubsystem* RunSubsystem = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
			RunSubsystem && !RunSubsystem->HasCheckpoint())
		{
			RunSubsystem->ResetRun(true);
		}
	}

	const FString LevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	UE_LOG(LogAuraInput, Display, TEXT("Restarting demo level: %s"), *LevelName);
	UGameplayStatics::OpenLevel(this, FName(*LevelName));
}

bool AAuraPlayerController::TeleportPawnToPostBossCamp()
{
	if (!HasAuthority() || !GetWorld()) return false;
	AAuraCharacter* LivingPawn = Cast<AAuraCharacter>(GetPawn());
	if (!LivingPawn) return false;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
		const int32 CampSlot = GetNetMode() == NM_Standalone || !State
			? 0 : FMath::Abs(State->GetPlayerId()) % 4;
		const float SideOffset = GetNetMode() == NM_Standalone
			? 0.f : -135.f + 90.f * CampSlot;
		const FVector CampLocation = It->GetActorLocation()
			+ FVector(-160.f, SideOffset, 96.f);
		if (LivingPawn->IsDead())
		{
			LivingPawn->ReviveAtTransform(FTransform(FRotator::ZeroRotator, CampLocation));
			if (AAuraPlayerState* AuraState = GetPlayerState<AAuraPlayerState>()) AuraState->SetPlayerDead(false);
		}
		if (!LivingPawn->TeleportTo(CampLocation, FRotator::ZeroRotator, false, true))
		{
			return false;
		}
		LivingPawn->RestoreVitalsAtCheckpoint();
		UE_LOG(LogAuraInput, Display,
			TEXT("M12 postboss return in same world. Player=%s Location=%s InventoryPreserved=%s"),
			*GetName(), *LivingPawn->GetActorLocation().ToCompactString(),
			GetAuraInventory() ? TEXT("true") : TEXT("false"));
		return true;
	}
	return false;
}

void AAuraPlayerController::FinishPostBossCampReturn()
{
	GetWorldTimerManager().ClearTimer(VictoryStingTimer);
	GetWorldTimerManager().ClearTimer(VictoryOverlayTimer);
	bDemoCompleted = false;
	bVictoryCinematicActive = false;
	bVictoryOverlayVisible = false;
	VictoryBossActor = nullptr;
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);
	if (!IsLocalController()) return;
	if (APawn* LivingPawn = GetPawn()) SetViewTarget(LivingPawn);
	if (VictoryCameraActor)
	{
		VictoryCameraActor->Destroy();
		VictoryCameraActor = nullptr;
	}
	SetCinematicMode(false, false, false, true, true);
	FlushPressedKeys();
	ApplyCursorMode(GetCameraMode());
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	const FString Objective = GetNetMode() != NM_Standalone
		? TEXT("大魔法师 [F] 学习技能与进化  |  等待下一局")
		: (Run && Run->IsAbyssTrialUnlocked()
			? TEXT("大魔法师 [F]  |  [N] 普通模式  |  [T] 深渊试炼")
			: TEXT("大魔法师 [F] 学习技能与进化  |  [N] 开始下一局"));
	ShowCoopMessage(TEXT("已返回营地  |  可拜访大魔法师"),
		FLinearColor(0.8f, 0.57f, 1.f), 3.f, Objective);
}

void AAuraPlayerController::ServerRequestPostBossCamp_Implementation()
{
	const AAuraGameState* State = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!bDemoCompleted || !State || !State->IsRunCompleted()) return;
	if (!TeleportPawnToPostBossCamp()) return;
	FinishPostBossCampReturn();
	if (!IsLocalController()) ClientFinishPostBossCampReturn();
}

void AAuraPlayerController::ClientFinishPostBossCampReturn_Implementation()
{
	FinishPostBossCampReturn();
}

void AAuraPlayerController::ToggleInventory()
{
	if (bIsRunUpgradeSelectionActive || bArchmageMenuOpen || bCampMenuOpen
		|| bDemoCompleted) return;
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || AuraCharacter->IsDead()) return;
	if (!bInventoryOpen)
	{
		if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
		if (bIsShockTargeting) CancelShockTargeting();
	}
	bInventoryOpen = !bInventoryOpen;
	// Switching between gameplay and backpack input while a movement key is held
	// can prevent its physical release from reaching Enhanced Input. Reset both
	// the key state and movement state at the boundary so closing the backpack can
	// never resume a stale direction.
	FlushPressedKeys();
	AuraCharacter->ConsumeMovementInputVector();
	AuraCharacter->GetCharacterMovement()->StopMovementImmediately();
	if (!bInventoryOpen)
	{
		bInventoryQuestPage = false;
		bInventorySkillPage = false;
		SelectedInventorySlot = INDEX_NONE;
		ResetInventoryDrag();
		CloseInventoryContextMenu();
	}
	ApplyCursorMode(GetCameraMode());
	UE_LOG(LogAuraInput, Display, TEXT("Backpack panel %s. Slots=%d/%d"),
		bInventoryOpen ? TEXT("opened") : TEXT("closed"),
		GetAuraInventory() ? GetAuraInventory()->GetOccupiedSlotCount() : 0,
		GetAuraInventory() ? GetAuraInventory()->GetCapacity() : 0);
}

void AAuraPlayerController::HandleInventoryLeftClick()
{
	AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAuraEquipmentComponent* Equipment = GetAuraEquipment();
	if (!AuraHUD || !Inventory || !Equipment) return;
	if (HandleNearbyEquipmentClick()) return;

	if (InventoryContextSlot != INDEX_NONE)
	{
		const int32 Action = AuraHUD->GetHoveredInventoryContextAction();
		const FAuraInventoryEntry* ContextEntry =
			Inventory->FindEntryAtSlot(InventoryContextSlot);
		if (Action == 0 && ContextEntry && ContextEntry->Item.Definition)
		{
			Equipment->RequestEquipItem(ContextEntry->Item.InstanceId);
			SelectedInventorySlot = INDEX_NONE;
		}
		else if (Action == 1 && ContextEntry)
		{
			Inventory->RequestDropSlot(
				InventoryContextSlot, ContextEntry->Item.Quantity);
			SelectedInventorySlot = INDEX_NONE;
		}
		CloseInventoryContextMenu();
		if (Action != INDEX_NONE) return;
	}

	const int32 HoveredSlot = AuraHUD->GetHoveredInventorySlot();
	if (HoveredSlot == INDEX_NONE)
	{
		SelectedInventorySlot = INDEX_NONE;
		return;
	}
	// A click inspects the item; movement only starts after a held-pointer drag.
	SelectedInventorySlot = Inventory->FindEntryAtSlot(HoveredSlot)
		? HoveredSlot : INDEX_NONE;
}

void AAuraPlayerController::BeginInventoryDragCandidate()
{
	if (!bInventoryOpen || bInventoryQuestPage || bInventorySkillPage) return;
	const AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	const UAuraInventoryComponent* Inventory = GetAuraInventory();
	if (!AuraHUD || !Inventory || SelectedInventorySlot == INDEX_NONE
		|| AuraHUD->GetHoveredInventorySlot() != SelectedInventorySlot) return;
	const FAuraInventoryEntry* Entry = Inventory->FindEntryAtSlot(SelectedInventorySlot);
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!Entry || !Entry->Item.InstanceId.IsValid()
		|| !GetMousePosition(MouseX, MouseY)) return;
	InventoryDragSourceSlot = SelectedInventorySlot;
	InventoryDragInstanceId = Entry->Item.InstanceId;
	InventoryDragStartPosition = FVector2D(MouseX, MouseY);
	InventoryDragHoverSlot = SelectedInventorySlot;
}

void AAuraPlayerController::UpdateInventoryDrag()
{
	if (InventoryDragSourceSlot == INDEX_NONE) return;
	const UAuraInventoryComponent* Inventory = GetAuraInventory();
	const FAuraInventoryEntry* Entry = Inventory
		? Inventory->FindEntryAtSlot(InventoryDragSourceSlot) : nullptr;
	if (!Entry || Entry->Item.InstanceId != InventoryDragInstanceId)
	{
		ResetInventoryDrag();
		return;
	}
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		if (bInventoryDragging) InventoryDragHoverSlot = INDEX_NONE;
		return;
	}
	if (!bInventoryDragging && FVector2D::DistSquared(
		FVector2D(MouseX, MouseY), InventoryDragStartPosition) >= FMath::Square(8.f))
	{
		bInventoryDragging = true;
		CloseInventoryContextMenu();
	}
	if (bInventoryDragging)
	{
		const AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
		InventoryDragHoverSlot = AuraHUD ? AuraHUD->GetHoveredInventorySlot() : INDEX_NONE;
	}
}

void AAuraPlayerController::FinishInventoryDrag()
{
	UpdateInventoryDrag();
	if (bInventoryDragging && InventoryDragHoverSlot != INDEX_NONE
		&& InventoryDragHoverSlot != InventoryDragSourceSlot)
	{
		if (UAuraInventoryComponent* Inventory = GetAuraInventory())
		{
			Inventory->RequestMoveItem(InventoryDragSourceSlot,
				InventoryDragHoverSlot, InventoryDragInstanceId);
			SelectedInventorySlot = InventoryDragHoverSlot;
		}
	}
	ResetInventoryDrag();
}

void AAuraPlayerController::ResetInventoryDrag()
{
	InventoryDragSourceSlot = INDEX_NONE;
	InventoryDragHoverSlot = INDEX_NONE;
	InventoryDragInstanceId.Invalidate();
	InventoryDragStartPosition = FVector2D::ZeroVector;
	bInventoryDragging = false;
}

void AAuraPlayerController::HandleInventoryRightClick()
{
	ResetInventoryDrag();
	AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAuraEquipmentComponent* Equipment = GetAuraEquipment();
	if (!AuraHUD || !Inventory || !Equipment) return;
	const EAuraEquipmentSlot HoveredEquipment = AuraHUD->GetHoveredEquipmentSlot();
	if (HoveredEquipment != EAuraEquipmentSlot::None)
	{
		Equipment->RequestUnequipSlot(HoveredEquipment);
		SelectedInventorySlot = INDEX_NONE;
		CloseInventoryContextMenu();
		return;
	}
	const int32 HoveredSlot = AuraHUD->GetHoveredInventorySlot();
	const FAuraInventoryEntry* Entry = Inventory->FindEntryAtSlot(HoveredSlot);
	if (!Entry || !Entry->Item.Definition)
	{
		CloseInventoryContextMenu();
		return;
	}
	if (Entry->Item.Definition->ItemType == EAuraItemType::Equipment)
	{
		SelectedInventorySlot = HoveredSlot;
		InventoryContextSlot = HoveredSlot;
		float MouseX = 0.f;
		float MouseY = 0.f;
		GetMousePosition(MouseX, MouseY);
		InventoryContextMenuPosition = FVector2D(MouseX, MouseY);
	}
	else
	{
		Inventory->RequestUseItem(Entry->Item.InstanceId);
		SelectedInventorySlot = INDEX_NONE;
		CloseInventoryContextMenu();
	}
}

void AAuraPlayerController::HandleInventoryDrop()
{
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	const FAuraInventoryEntry* Entry = Inventory
		? Inventory->FindEntryAtSlot(SelectedInventorySlot) : nullptr;
	if (Inventory && Entry)
	{
		Inventory->RequestDropSlot(SelectedInventorySlot, Entry->Item.Quantity);
	}
	SelectedInventorySlot = INDEX_NONE;
	CloseInventoryContextMenu();
}

void AAuraPlayerController::UpdateNearbyEquipment()
{
	if (!IsLocalController() || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextNearbyEquipmentRefreshTime) return;
	NextNearbyEquipmentRefreshTime = Now + 0.08f;
	if (!PendingPickupWorldItem.IsValid() || Now >= PendingPickupUntil)
	{
		PendingPickupWorldItem.Reset();
	}

	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	const APlayerState* AuraState = GetPlayerState<APlayerState>();
	if (!AuraCharacter || AuraCharacter->IsDead() || !AuraState)
	{
		ClearNearbyEquipment();
		return;
	}

	constexpr float NearbyRadius = 240.f;
	TArray<TWeakObjectPtr<AAuraWorldItem>> NewNearbyItems;
	for (TActorIterator<AAuraWorldItem> It(GetWorld()); It; ++It)
	{
		AAuraWorldItem* WorldItem = *It;
		if (!WorldItem || WorldItem == PendingPickupWorldItem.Get()
			|| !WorldItem->RequiresManualPickup()
			|| !WorldItem->IsVisibleToLocalPlayer()
			|| !WorldItem->IsPickupEligibleFor(AuraState)
			|| FVector::DistSquared(AuraCharacter->GetActorLocation(),
				WorldItem->GetActorLocation()) > FMath::Square(NearbyRadius))
		{
			continue;
		}
		NewNearbyItems.Add(WorldItem);
	}
	NewNearbyItems.Sort([AuraCharacter](
		const TWeakObjectPtr<AAuraWorldItem>& Left,
		const TWeakObjectPtr<AAuraWorldItem>& Right)
	{
		const float LeftDistance = Left.IsValid()
			? FVector::DistSquared(AuraCharacter->GetActorLocation(), Left->GetActorLocation())
			: MAX_flt;
		const float RightDistance = Right.IsValid()
			? FVector::DistSquared(AuraCharacter->GetActorLocation(), Right->GetActorLocation())
			: MAX_flt;
		return LeftDistance < RightDistance;
	});
	if (NewNearbyItems.Num() > 6) NewNearbyItems.SetNum(6);
	NearbyWorldItems = MoveTemp(NewNearbyItems);

	AAuraWorldItem* DesiredHighlight = nullptr;
	if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
	{
		AAuraWorldItem* HoveredItem = AuraHUD->GetHoveredNearbyWorldItem();
		if (NearbyWorldItems.ContainsByPredicate(
			[HoveredItem](const TWeakObjectPtr<AAuraWorldItem>& Item)
			{
				return Item.Get() == HoveredItem;
			}))
		{
			DesiredHighlight = HoveredItem;
		}
	}
	if (!DesiredHighlight && !NearbyWorldItems.IsEmpty())
	{
		DesiredHighlight = NearbyWorldItems[0].Get();
	}
	if (HighlightedWorldItem.Get() != DesiredHighlight)
	{
		if (HighlightedWorldItem.IsValid())
		{
			HighlightedWorldItem->SetLocallyHighlighted(false);
		}
		HighlightedWorldItem = DesiredHighlight;
		if (HighlightedWorldItem.IsValid())
		{
			HighlightedWorldItem->SetLocallyHighlighted(true);
		}
	}
}

void AAuraPlayerController::ClearNearbyEquipment()
{
	if (HighlightedWorldItem.IsValid())
	{
		HighlightedWorldItem->SetLocallyHighlighted(false);
	}
	HighlightedWorldItem.Reset();
	NearbyWorldItems.Reset();
}

bool AAuraPlayerController::HandleNearbyEquipmentClick()
{
	if (!bShowMouseCursor) return false;
	AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	AAuraWorldItem* HoveredItem = AuraHUD
		? AuraHUD->GetHoveredNearbyWorldItem() : nullptr;
	if (!HoveredItem) return false;
	RequestPickupWorldItem(HoveredItem);
	return true;
}

void AAuraPlayerController::RequestPickupWorldItem(AAuraWorldItem* WorldItem)
{
	if (!WorldItem || !WorldItem->RequiresManualPickup() || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (PendingPickupWorldItem.Get() == WorldItem && Now < PendingPickupUntil) return;
	PendingPickupWorldItem = WorldItem;
	PendingPickupUntil = Now + 0.45f;
	if (HighlightedWorldItem.Get() == WorldItem)
	{
		WorldItem->SetLocallyHighlighted(false);
		HighlightedWorldItem.Reset();
	}
	if (HasAuthority())
	{
		ServerPickupWorldItem_Implementation(WorldItem);
	}
	else
	{
		ServerPickupWorldItem(WorldItem);
	}
}

void AAuraPlayerController::PickupHighlightedWorldItem()
{
	RequestPickupWorldItem(HighlightedWorldItem.Get());
}

bool AAuraPlayerController::TryInteractWithRewardChest()
{
	AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || AuraCharacter->IsDead() || !GetWorld()) return false;

	constexpr float InteractionDistance = 320.f;
	float BestDistanceSquared = FMath::Square(InteractionDistance);
	AActor* BestChest = nullptr;
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();

	for (TActorIterator<AAuraTreasureChest> It(GetWorld()); It; ++It)
	{
		AAuraTreasureChest* Chest = *It;
		if (!Chest) continue;
		if (AuraPlayerState && AuraPlayerState->HasClaimedTreasure(Chest->GetTreasureId())) continue;
		const float DistanceSquared = FVector::DistSquared(
			AuraCharacter->GetActorLocation(), Chest->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestChest = Chest;
		}
	}
	for (TActorIterator<AAuraLootChest> It(GetWorld()); It; ++It)
	{
		AAuraLootChest* Chest = *It;
		if (!Chest || (AuraPlayerState && Chest->HasBeenClaimedBy(AuraPlayerState))) continue;
		const float DistanceSquared = FVector::DistSquared(
			AuraCharacter->GetActorLocation(), Chest->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestChest = Chest;
		}
	}

	if (!BestChest) return false;
	if (HasAuthority()) ServerInteractRewardChest_Implementation(BestChest);
	else ServerInteractRewardChest(BestChest);
	return true;
}

bool AAuraPlayerController::TryInteractWithRitualSite()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!AuraPawn || AuraPawn->IsDead() || !GetWorld()) return false;
	const AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	AAuraRitualSite* Best = nullptr;
	float BestDistanceSq = BIG_NUMBER;
	for (TActorIterator<AAuraRitualSite> It(GetWorld()); It; ++It)
	{
		AAuraRitualSite* Site = *It;
		if (!Site || (Quest && (Site->IsBossAltar()
			? Quest->IsBossSummoned() : Quest->HasActivatedSeal(Site->GetSealId())))) continue;
		const float DistanceSq = FVector::DistSquared2D(
			Site->GetActorLocation(), AuraPawn->GetActorLocation());
		if (DistanceSq <= FMath::Square(Site->GetInteractionRadius())
			&& DistanceSq < BestDistanceSq)
		{
			Best = Site;
			BestDistanceSq = DistanceSq;
		}
	}
	if (!Best) return false;
	if (bInventoryOpen) ToggleInventory();
	const FGuid RequestId = FGuid::NewGuid();
	if (HasAuthority()) ServerInteractRitualSite_Implementation(Best, RequestId);
	else ServerInteractRitualSite(Best, RequestId);
	return true;
}

bool AAuraPlayerController::TryInteractWithLoreRecord()
{
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!IsLocalController() || !AuraPawn || AuraPawn->IsDead() || !GetWorld())
	{
		return false;
	}
	AAuraLoreRecord* Best = nullptr;
	float BestDistanceSq = BIG_NUMBER;
	for (TActorIterator<AAuraLoreRecord> It(GetWorld()); It; ++It)
	{
		AAuraLoreRecord* Record = *It;
		if (!Record || !Record->IsInRange(AuraPawn->GetActorLocation())) continue;
		const float DistanceSq = FVector::DistSquared(
			Record->GetActorLocation(), AuraPawn->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			Best = Record;
			BestDistanceSq = DistanceSq;
		}
	}
	if (!Best) return false;
	if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD()))
	{
		AuraHUD->ShowLoreRecord(Best->GetRecordId(), Best->GetRecordTitle(),
			Best->GetLineOne(), Best->GetLineTwo(), Best->GetLineThree(),
			Best->GetAccentColor());
		UE_LOG(LogAuraInput, Display, TEXT("AURA_M20_RECORD_READ Id=%s"),
			*Best->GetRecordId().ToString());
		return true;
	}
	return false;
}

void AAuraPlayerController::ServerInteractRitualSite_Implementation(
	AAuraRitualSite* Site, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	const bool bSuccess = IsValid(Site) && Site->TryInteract(
		Cast<AAuraCharacter>(GetPawn()), Failure);
	if (!bSuccess)
	{
		ShowCoopMessage(Failure.IsEmpty() ? TEXT("仪式交互失败") : Failure,
			FLinearColor(1.f, 0.35f, 0.25f), 2.f);
	}
}

bool AAuraPlayerController::TryInteractWithArchmage()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!AuraPawn || AuraPawn->IsDead() || !GetWorld()) return false;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		AAuraArchmage* Mage = *It;
		if (!Mage || FVector::DistSquared2D(Mage->GetActorLocation(),
			AuraPawn->GetActorLocation()) > FMath::Square(Mage->GetInteractionRadius()))
		{
			continue;
		}
		ServerOpenArchmage(Mage);
		return true;
	}
	return false;
}

void AAuraPlayerController::ServerOpenArchmage_Implementation(AAuraArchmage* Mage)
{
	FString Failure;
	if (!ValidateMageService(Mage, Failure))
	{
		ClientMageTransactionResult(false, Failure);
		return;
	}
	ClientOpenArchmage(Mage);
}

void AAuraPlayerController::ClientOpenArchmage_Implementation(AAuraArchmage* Mage)
{
	if (!Mage) return;
	if (bInventoryOpen) ToggleInventory();
	if (bCampMenuOpen) CloseCampMenu();
	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	if (bIsShockTargeting) CancelShockTargeting();
	ActiveArchmage = Mage;
	bArchmageMenuOpen = true;
	ArchmagePendingAction = 0;
	bMageTransactionPending = false;
	MageDraftComboChoice = INDEX_NONE;
	MageDraftFirePath = EAuraFireBoltPath::None;
	FlushPressedKeys();
	if (AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn()))
	{
		AuraPawn->ConsumeMovementInputVector();
		AuraPawn->GetCharacterMovement()->StopMovementImmediately();
	}
	ShowServiceMenuWidget(true);
	ApplyCursorMode(GetCameraMode());
}

void AAuraPlayerController::CloseArchmageMenu()
{
	RemoveServiceMenuWidget();
	bArchmageMenuOpen = false;
	ArchmagePendingAction = 0;
	bMageTransactionPending = false;
	MageDraftComboChoice = INDEX_NONE;
	MageDraftFirePath = EAuraFireBoltPath::None;
	ActiveArchmage.Reset();
	FlushPressedKeys();
	if (AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn()))
	{
		AuraPawn->ConsumeMovementInputVector();
		AuraPawn->GetCharacterMovement()->StopMovementImmediately();
	}
	ApplyCursorMode(GetCameraMode());
}

void AAuraPlayerController::HandleArchmageMenuAction(const int32 Action)
{
 if (!bArchmageMenuOpen) return;
 if (Action == 9) { CloseArchmageMenu(); return; }
 if (ArchmagePendingAction == 5)
 {
  if (Action >= 2 && Action <= 4) AddMageDraftRank(static_cast<EAuraPlayerSkill>(Action - 2));
  else if (Action == 11 && MageDraftFireRank >= 3) MageDraftFirePath = EAuraFireBoltPath::Pierce;
  else if (Action == 12 && MageDraftFireRank >= 3) MageDraftFirePath = EAuraFireBoltPath::TripleVolley;
  else if (Action == 13 && MageDraftQRank >= 3) MageDraftArcanePath = EAuraArcanePath::CrystalPrison;
  else if (Action == 14 && MageDraftQRank >= 3) MageDraftArcanePath = EAuraArcanePath::CrystalEcho;
  else if (Action == 15 && MageDraftERank >= 3) MageDraftShockPath = EAuraShockPath::StormField;
  else if (Action == 16 && MageDraftERank >= 3) MageDraftShockPath = EAuraShockPath::ThunderJudgment;
  else if (Action == 6) ConfirmMageAction();
  else if (Action == 7) ArchmagePendingAction = 0;
  else if (Action == 8) ResetMageDraft();
 }
 else if (ArchmagePendingAction >= 11 && ArchmagePendingAction <= 13)
 {
  if (Action == 2 || Action == 3)
  {
   const int32 Choice = Action - 1;
   if (ArchmagePendingAction == 11) MageDraftFirePath = static_cast<EAuraFireBoltPath>(Choice);
   else if (ArchmagePendingAction == 12) MageDraftArcanePath = static_cast<EAuraArcanePath>(Choice);
   else MageDraftShockPath = static_cast<EAuraShockPath>(Choice);
  }
  else if (Action == 6) ConfirmMageAction();
  else if (Action == 7) ArchmagePendingAction = 0;
 }
 else if (ArchmagePendingAction != 0)
 {
  if (Action == 6) ConfirmMageAction();
  else if (Action == 7) ArchmagePendingAction = 0;
 }
 else if (Action >= 2 && Action <= 5) BeginMageAction(Action);
}

void AAuraPlayerController::ShowServiceMenuWidget(const bool bMageMenu)
{
	RemoveServiceMenuWidget();
	if (!IsLocalController()) return;
	ActiveServiceMenuWidget = CreateWidget<UAuraServiceMenuWidget>(
		this, UAuraServiceMenuWidget::StaticClass());
	if (!ActiveServiceMenuWidget) return;
	ActiveServiceMenuWidget->InitializeMenu(this, bMageMenu);
	if (!ActiveServiceMenuWidget->AddToPlayerScreen(50))
	{
		ActiveServiceMenuWidget = nullptr;
		UE_LOG(LogAuraInput, Warning, TEXT("NPC menu widget could not be added to the viewport; using Canvas input fallback."));
	}
}

void AAuraPlayerController::RemoveServiceMenuWidget()
{
	if (ActiveServiceMenuWidget)
	{
		ActiveServiceMenuWidget->RemoveFromParent();
		ActiveServiceMenuWidget = nullptr;
	}
}

bool AAuraPlayerController::ValidateMageService(AAuraArchmage* Mage,
	FString& OutFailureReason) const
{
	if (!HasAuthority() || !IsValid(Mage))
	{
		OutFailureReason = TEXT("大魔法师暂不可用");
		return false;
	}
	return Mage->CanServe(Cast<AAuraCharacter>(GetPawn()), OutFailureReason);
}

bool AAuraPlayerController::RegisterMageRequest(const FGuid& RequestId)
{
	if (!RequestId.IsValid() || RecentMageRequests.Contains(RequestId)
		|| RecentMageRequests.Num() >= 256) return false;
	RecentMageRequests.Add(RequestId);
	return true;
}

void AAuraPlayerController::ServerAllocateMageSkill_Implementation(
	AAuraArchmage* Mage, const EAuraPlayerSkill Skill, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = State && ValidateMageService(Mage, Failure)
		&& State->TryAllocateSkill(Skill, Failure);
	ClientMageTransactionResult(bSuccess, bSuccess ? TEXT("技能加点成功") : Failure);
}

void AAuraPlayerController::ServerAllocateMageFirePath_Implementation(
	AAuraArchmage* Mage, const EAuraFireBoltPath Path, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = State && ValidateMageService(Mage, Failure)
		&& State->TryAllocateSkill(EAuraPlayerSkill::FireBolt, Failure, Path);
	ClientMageTransactionResult(bSuccess,
		bSuccess ? (Path == EAuraFireBoltPath::Pierce
			? TEXT("已习得火球穿透") : TEXT("已习得三火球齐射")) : Failure);
}

void AAuraPlayerController::ServerAllocateMageCombo_Implementation(
	AAuraArchmage* Mage, const EAuraComboMarkType Combo, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = State && ValidateMageService(Mage, Failure)
		&& State->TryAllocateCombo(Combo, Failure);
	ClientMageTransactionResult(bSuccess, bSuccess ? TEXT("连携修习成功") : Failure);
}

void AAuraPlayerController::ServerRespecMageSkills_Implementation(
	AAuraArchmage* Mage, const int32 FireRank, const int32 QRank,
	const int32 ERank, const EAuraFireBoltPath FirePath,
	const FGuid StoneId, const FGuid RequestId, const EAuraArcanePath QPath, const EAuraShockPath EPath)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	FAuraSkillRankState Candidate;
	Candidate.FireBolt = FireRank;
	Candidate.FireBoltPath = FirePath;
	Candidate.ArcaneShards = QRank;
	Candidate.Shock = ERank;
	Candidate.ArcanePath = QPath;
	Candidate.ShockPath = EPath;
	const bool bSuccess = State && ValidateMageService(Mage, Failure)
		&& State->TryRespecSkills(Candidate, StoneId, Failure);
	ClientMageTransactionResult(bSuccess, bSuccess ? TEXT("技能洗点成功") : Failure);
}

void AAuraPlayerController::ClientMageTransactionResult_Implementation(
	const bool bSucceeded, const FString& Message)
{
	bMageTransactionPending = false;
	if (bSucceeded) ArchmagePendingAction = 0;
	if (AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD()))
	{
		HUD->ShowStatusMessage(Message.IsEmpty() ? TEXT("服务暂不可用") : Message,
			bSucceeded ? FLinearColor(0.45f, 1.f, 0.6f)
				: FLinearColor(1.f, 0.35f, 0.3f), 2.5f);
	}
}

int32 AAuraPlayerController::GetMageDraftRank(const EAuraPlayerSkill Skill) const
{
	switch (Skill)
	{
	case EAuraPlayerSkill::FireBolt: return MageDraftFireRank;
	case EAuraPlayerSkill::ArcaneShards: return MageDraftQRank;
	case EAuraPlayerSkill::Shock: return MageDraftERank;
	default: return 0;
	}
}

void AAuraPlayerController::ResetMageDraft()
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const int32 Level = State ? State->GetGrowthState().PaidLevel : 1;
	MageDraftFireRank = 1;
	MageDraftFirePath = EAuraFireBoltPath::None;
	MageDraftQRank = 0;
	MageDraftERank = 0;
	MageDraftArcanePath = EAuraArcanePath::None;
	MageDraftShockPath = EAuraShockPath::None;
	MageDraftPoints = FAuraGrowthState::GetEarnedSkillPoints(Level);
}

void AAuraPlayerController::AddMageDraftRank(const EAuraPlayerSkill Skill)
{
	if (ArchmagePendingAction != 5) return;
	const int32 MaxRank = UAuraCombatBalanceData::Get().SkillRanks.MaxRank;
	if (MageDraftPoints <= 0) return;
	int32* Rank = Skill == EAuraPlayerSkill::FireBolt ? &MageDraftFireRank
		: (Skill == EAuraPlayerSkill::ArcaneShards ? &MageDraftQRank : &MageDraftERank);
	if (*Rank >= MaxRank) return;
	++*Rank;
	if (Skill == EAuraPlayerSkill::FireBolt && *Rank == 3)
		MageDraftFirePath = EAuraFireBoltPath::None;
	if (Skill == EAuraPlayerSkill::ArcaneShards && *Rank == 3) MageDraftArcanePath = EAuraArcanePath::None;
	if (Skill == EAuraPlayerSkill::Shock && *Rank == 3) MageDraftShockPath = EAuraShockPath::None;
	--MageDraftPoints;
}

void AAuraPlayerController::BeginMageAction(const int32 Action)
{
 if (Action < 2 || Action > 5) return;
 ArchmagePendingAction = Action;
 MageDraftFirePath = EAuraFireBoltPath::None;
 MageDraftArcanePath = EAuraArcanePath::None;
 MageDraftShockPath = EAuraShockPath::None;
 const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
 if (State)
 {
  const auto& R = State->GetGrowthState().SkillRanks;
  if (Action == 2 && R.FireBolt == 2) ArchmagePendingAction = 11;
  if (Action == 3 && (R.ArcaneShards == 2 || R.bArcaneEvolutionPending)) ArchmagePendingAction = 12;
  if (Action == 4 && (R.Shock == 2 || R.bShockEvolutionPending)) ArchmagePendingAction = 13;
 }
 if (Action == 5) ResetMageDraft();
}

void AAuraPlayerController::ServerChooseMageEvolution_Implementation(AAuraArchmage* Mage,
 const EAuraPlayerSkill Skill, const int32 Choice, const FGuid RequestId)
{
 if (!RegisterMageRequest(RequestId)) return;
 FString Failure;
 AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
 const bool bSuccess = State && ValidateMageService(Mage, Failure) && State->TryChooseEvolution(Skill, Choice, Failure);
 ClientMageTransactionResult(bSuccess, bSuccess ? TEXT("技能进化选择成功") : Failure);
}

void AAuraPlayerController::ConfirmMageAction()
{
 AAuraArchmage* Mage = ActiveArchmage.Get();
 if (!Mage || ArchmagePendingAction == 0 || bMageTransactionPending) return;
 const bool bNeedsFire = ArchmagePendingAction == 11 || (ArchmagePendingAction == 5 && MageDraftFireRank >= 3);
 const bool bNeedsQ = ArchmagePendingAction == 12 || (ArchmagePendingAction == 5 && MageDraftQRank >= 3);
 const bool bNeedsE = ArchmagePendingAction == 13 || (ArchmagePendingAction == 5 && MageDraftERank >= 3);
 if ((bNeedsFire && MageDraftFirePath == EAuraFireBoltPath::None)
  || (bNeedsQ && MageDraftArcanePath == EAuraArcanePath::None) || (bNeedsE && MageDraftShockPath == EAuraShockPath::None))
 {
  if (AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD())) HUD->ShowStatusMessage(TEXT("请为三级及以上技能选择进化"));
  return;
 }
 bMageTransactionPending = true;
 const FGuid RequestId = FGuid::NewGuid();
 switch (ArchmagePendingAction)
 {
 case 2: ServerAllocateMageSkill(Mage, EAuraPlayerSkill::FireBolt, RequestId); break;
 case 3: ServerAllocateMageSkill(Mage, EAuraPlayerSkill::ArcaneShards, RequestId); break;
 case 4: ServerAllocateMageSkill(Mage, EAuraPlayerSkill::Shock, RequestId); break;
 case 11: ServerAllocateMageFirePath(Mage, MageDraftFirePath, RequestId); break;
 case 12: ServerChooseMageEvolution(Mage, EAuraPlayerSkill::ArcaneShards, static_cast<int32>(MageDraftArcanePath), RequestId); break;
 case 13: ServerChooseMageEvolution(Mage, EAuraPlayerSkill::Shock, static_cast<int32>(MageDraftShockPath), RequestId); break;
 case 5:
 {
  const UAuraInventoryComponent* Inventory = GetAuraInventory();
  const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
  const bool bFree = State && State->GetGrowthState().PaidLevel >= 6 && !State->GetGrowthState().bFreeRespecClaimed;
  const FGuid Stone = !bFree && Inventory ? Inventory->FindFirstConsumable(EAuraConsumableEffect::Respec) : FGuid();
  ServerRespecMageSkills(Mage, MageDraftFireRank, MageDraftQRank, MageDraftERank, MageDraftFirePath,
   Stone, RequestId, MageDraftArcanePath, MageDraftShockPath);
  break;
 }
 default: bMageTransactionPending = false; break;
 }
}

TArray<FAuraCampServiceRow> AAuraPlayerController::BuildCampServiceRows() const
{
	TArray<FAuraCampServiceRow> Rows;
	const AAuraCampVendor* Vendor = ActiveCampVendor.Get();
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const UAuraInventoryComponent* Inventory = GetAuraInventory();
	if (!Vendor || !State || !Inventory) return Rows;
	const FAuraCampEconomyBalance& Economy = UAuraCombatBalanceData::Get().CampEconomy;
	if (Vendor->GetServiceType() == EAuraCampService::Merchant && !bCampSelling)
	{
		for (int32 OfferId = 1; OfferId <= 13; ++OfferId)
		{
			FName ItemId;
			EAuraItemRarity Rarity;
			int32 Cost = 0;
			int32 Limit = 0;
			if (!Economy.GetOffer(OfferId, ItemId, Rarity, Cost, Limit,
				State->GetGrowthState().PaidLevel)) continue;
			UAuraItemDefinition* Definition = UAuraItemCatalog::Get().FindItem(ItemId);
			if (!Definition) continue;
			FAuraCampServiceRow& Row = Rows.AddDefaulted_GetRef();
			Row.OfferId = OfferId;
			Row.Price = Cost;
			Row.Item.Definition = Definition;
			Row.Item.Rarity = Rarity;
			Row.Item.ItemLevel = Definition->ItemType == EAuraItemType::Equipment
				? State->GetGrowthState().PaidLevel : 1;
			const bool bSoldOut = Limit > 0
				&& (OfferId == 13 ? State->GetGrowthState().SmithMetalPurchased
					: State->GetGrowthState().RespecStonesPurchased) >= Limit;
			Row.Label = FString::Printf(TEXT("%s%s  |  %d 灵魂%s"),
				*Definition->DisplayName.ToString(),
				Definition->ItemType == EAuraItemType::Equipment
					? *FString::Printf(TEXT(" [%s]"), AuraItems::GetRarityName(Rarity))
					: TEXT(""),
				Cost, bSoldOut ? TEXT("  已售罄") : TEXT(""));
		}
	}
	else if (Vendor->GetServiceType() == EAuraCampService::Merchant)
	{
		for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
		{
			const UAuraItemDefinition* Definition = Entry.Item.Definition;
			if (!Definition || Definition->ItemType == EAuraItemType::Quest
				|| (!Definition->bDroppable && Definition->ItemId
					!= FName(TEXT("Material.SmithMetal")))) continue;
			FAuraCampServiceRow& Row = Rows.AddDefaulted_GetRef();
			Row.InstanceId = Entry.Item.InstanceId;
			Row.Item = Entry.Item;
			Row.Price = Economy.GetSellPrice(Entry.Item);
			Row.Label = FString::Printf(TEXT("%s%s  x%d  |  +%d 灵魂"),
				*Definition->DisplayName.ToString(),
				Entry.Item.EnhancementLevel > 0
					? *FString::Printf(TEXT(" +%d"), Entry.Item.EnhancementLevel) : TEXT(""),
				Entry.Item.Quantity, Row.Price);
		}
	}
	else if (SmithMode == 1)
	{
		for (const EAuraEquipmentSlot Slot : {EAuraEquipmentSlot::Weapon,
			EAuraEquipmentSlot::Armor, EAuraEquipmentSlot::Head,
			EAuraEquipmentSlot::Shoes})
		{
			const FName ItemId = Slot == EAuraEquipmentSlot::Weapon
				? FName(TEXT("Equipment.ArcaneStaff"))
				: Slot == EAuraEquipmentSlot::Armor ? FName(TEXT("Equipment.WardenArmor"))
				: Slot == EAuraEquipmentSlot::Head ? FName(TEXT("Equipment.SeerHeadpiece"))
				: FName(TEXT("Equipment.WindShoes"));
			UAuraItemDefinition* Definition = UAuraItemCatalog::Get().FindItem(ItemId);
			if (!Definition) continue;
			FAuraCampServiceRow& Row = Rows.AddDefaulted_GetRef();
			Row.OfferId = static_cast<int32>(Slot);
			Row.Price = Economy.CraftSoulCost;
			Row.Item.Definition = Definition;
			Row.Item.ItemLevel = State->GetGrowthState().PaidLevel;
			Row.Label = FString::Printf(TEXT("打造%s  |  %d 金属 + %d 灵魂"),
				*Definition->DisplayName.ToString(), Economy.CraftMetalCost,
				Economy.CraftSoulCost);
		}
	}
	else if (SmithMode == 2)
	{
		for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
		{
			const UAuraItemDefinition* Definition = Entry.Item.Definition;
			if (!Definition || Definition->ItemType != EAuraItemType::Equipment
				|| !Definition->bDroppable) continue;
			FAuraCampServiceRow& Row = Rows.AddDefaulted_GetRef();
			Row.InstanceId = Entry.Item.InstanceId;
			Row.Item = Entry.Item;
			Row.Price = FMath::Clamp(static_cast<int32>(Entry.Item.Rarity) + 1, 1, 5);
			Row.Label = FString::Printf(TEXT("熔炼%s [%s]  |  +%d 金属"),
				*Definition->DisplayName.ToString(),
				AuraItems::GetRarityName(Entry.Item.Rarity), Row.Price);
		}
	}
	else
	{
		const UAuraEquipmentComponent* Equipment = GetAuraEquipment();
		if (Equipment)
		{
			for (const FAuraEquippedItem& EquippedEntry : Equipment->GetEquippedItems())
			{
				const FAuraItemInstance* Equipped = &EquippedEntry.Item;
				FAuraCampServiceRow& Row = Rows.AddDefaulted_GetRef();
				Row.InstanceId = Equipped->InstanceId;
				Row.Item = *Equipped;
				Row.bEquipped = true;
				Row.Price = Economy.GetForgeSoulCost(Equipped->EnhancementLevel + 1,
					EquippedEntry.Slot);
				Row.Label = FString::Printf(TEXT("[已装备] %s +%d  |  %s"),
					*Equipped->Definition->DisplayName.ToString(), Equipped->EnhancementLevel,
					Row.Price == MAX_int32 ? TEXT("已满级")
						: *FString::Printf(TEXT("%d 灵魂"), Row.Price));
			}
		}
		for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
		{
			const UAuraItemDefinition* Definition = Entry.Item.Definition;
			if (!Definition || Definition->ItemType != EAuraItemType::Equipment
				|| Definition->EquipmentSlot == EAuraEquipmentSlot::None) continue;
			FAuraCampServiceRow& Row = Rows.AddDefaulted_GetRef();
			Row.InstanceId = Entry.Item.InstanceId;
			Row.Item = Entry.Item;
			Row.Price = Economy.GetForgeSoulCost(Entry.Item.EnhancementLevel + 1,
				Definition->EquipmentSlot);
			Row.Label = FString::Printf(TEXT("%s +%d  |  %s"),
				*Definition->DisplayName.ToString(), Entry.Item.EnhancementLevel,
				Row.Price == MAX_int32 ? TEXT("已满级")
					: *FString::Printf(TEXT("%d 灵魂"), Row.Price));
		}
	}
	return Rows;
}

bool AAuraPlayerController::TryInteractWithCampVendor()
{
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!AuraPawn || AuraPawn->IsDead() || !GetWorld()) return false;
	AAuraCampVendor* Best = nullptr;
	float BestDistanceSq = BIG_NUMBER;
	for (TActorIterator<AAuraCampVendor> It(GetWorld()); It; ++It)
	{
		const float DistanceSq = FVector::DistSquared2D(
			It->GetActorLocation(), AuraPawn->GetActorLocation());
		if (DistanceSq < FMath::Square(It->GetInteractionRadius())
			&& DistanceSq < BestDistanceSq)
		{
			Best = *It;
			BestDistanceSq = DistanceSq;
		}
	}
	if (!Best) return false;
	ServerOpenCampVendor(Best);
	return true;
}

bool AAuraPlayerController::ValidateCampService(AAuraCampVendor* Vendor,
	FString& OutFailureReason) const
{
	if (!HasAuthority() || !IsValid(Vendor))
	{
		OutFailureReason = TEXT("营地服务暂不可用");
		return false;
	}
	return Vendor->CanServe(Cast<AAuraCharacter>(GetPawn()), OutFailureReason);
}

void AAuraPlayerController::ServerOpenCampVendor_Implementation(AAuraCampVendor* Vendor)
{
	FString Failure;
	if (!ValidateCampService(Vendor, Failure))
	{
		ClientCampTransactionResult(false, Failure);
		return;
	}
	ClientOpenCampVendor(Vendor);
}

void AAuraPlayerController::ClientOpenCampVendor_Implementation(AAuraCampVendor* Vendor)
{
	if (!Vendor) return;
	if (bInventoryOpen) ToggleInventory();
	if (bArchmageMenuOpen) CloseArchmageMenu();
	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	if (bIsShockTargeting) CancelShockTargeting();
	ActiveCampVendor = Vendor;
	bCampMenuOpen = true;
	bCampSelling = false;
	SmithMode = 0;
	bCampConfirming = false;
	bCampTransactionPending = false;
	CampPage = 0;
	FlushPressedKeys();
	if (AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn()))
	{
		AuraPawn->ConsumeMovementInputVector();
		AuraPawn->GetCharacterMovement()->StopMovementImmediately();
	}
	ShowServiceMenuWidget(false);
	ApplyCursorMode(GetCameraMode());
}

void AAuraPlayerController::CloseCampMenu()
{
	RemoveServiceMenuWidget();
	bCampMenuOpen = false;
	bCampConfirming = false;
	bCampTransactionPending = false;
	ActiveCampVendor.Reset();
	FlushPressedKeys();
	if (AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn()))
	{
		AuraPawn->ConsumeMovementInputVector();
		AuraPawn->GetCharacterMovement()->StopMovementImmediately();
	}
	ApplyCursorMode(GetCameraMode());
}

void AAuraPlayerController::HandleCampMenuAction(const int32 Action)
{
	if (!bCampMenuOpen) return;
	if (Action == 15)
	{
		CloseCampMenu();
	}
	else if (ActiveCampVendor.IsValid()
		&& ActiveCampVendor->GetServiceType() == EAuraCampService::Blacksmith
		&& (Action == 11 || Action == 12 || Action == 16))
	{
		SmithMode = Action == 11 ? 0 : (Action == 12 ? 1 : 2);
		CampPage = 0;
		bCampConfirming = false;
	}
	else if ((Action == 11 || Action == 12) && ActiveCampVendor.IsValid()
		&& ActiveCampVendor->GetServiceType() == EAuraCampService::Merchant)
	{
		bCampSelling = Action == 12;
		CampPage = 0;
		bCampConfirming = false;
	}
	else if (Action == 13 || Action == 14)
	{
		CampPage = Action == 13 ? FMath::Max(0, CampPage - 1)
			: FMath::Min(CampPage + 1,
				FMath::Max(0, (BuildCampServiceRows().Num() - 1) / 8));
		bCampConfirming = false;
	}
	else if (Action >= 1 && Action <= 8)
	{
		BeginCampSelection(Action - 1);
	}
	else if (Action == 9 && bCampConfirming) ConfirmCampAction();
	else if (Action == 10 && bCampConfirming) bCampConfirming = false;
}

void AAuraPlayerController::BeginCampSelection(const int32 LocalRow)
{
	if (bCampTransactionPending) return;
	const TArray<FAuraCampServiceRow> Rows = BuildCampServiceRows();
	const int32 Index = CampPage * 8 + LocalRow;
	if (!Rows.IsValidIndex(Index)) return;
	CampPendingRow = Rows[Index];
	bCampConfirming = true;
}

void AAuraPlayerController::ConfirmCampAction()
{
	AAuraCampVendor* Vendor = ActiveCampVendor.Get();
	if (!Vendor || !bCampConfirming || bCampTransactionPending) return;
	bCampTransactionPending = true;
	const FGuid RequestId = FGuid::NewGuid();
	if (Vendor->GetServiceType() == EAuraCampService::Blacksmith)
	{
		if (SmithMode == 1) ServerCraftCampEquipment(Vendor,
			static_cast<EAuraEquipmentSlot>(CampPendingRow.OfferId), RequestId);
		else if (SmithMode == 2) ServerSmeltCampEquipment(Vendor,
			CampPendingRow.InstanceId, RequestId);
		else ServerForgeCampWeapon(Vendor, CampPendingRow.InstanceId, RequestId);
	}
	else if (bCampSelling)
	{
		ServerSellCampItem(Vendor, CampPendingRow.InstanceId, RequestId);
	}
	else
	{
		ServerBuyCampOffer(Vendor, CampPendingRow.OfferId, RequestId);
	}
}

void AAuraPlayerController::ServerBuyCampOffer_Implementation(
	AAuraCampVendor* Vendor, const int32 OfferId, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = ValidateCampService(Vendor, Failure)
		&& Vendor->GetServiceType() == EAuraCampService::Merchant
		&& State && State->TryPurchaseMerchantOffer(OfferId, Failure);
	ClientCampTransactionResult(bSuccess,
		bSuccess ? TEXT("购买成功") : (Failure.IsEmpty() ? TEXT("购买失败") : Failure));
}

void AAuraPlayerController::ServerSellCampItem_Implementation(
	AAuraCampVendor* Vendor, const FGuid InstanceId, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = ValidateCampService(Vendor, Failure)
		&& Vendor->GetServiceType() == EAuraCampService::Merchant
		&& State && State->TrySellMerchantItem(InstanceId, Failure);
	ClientCampTransactionResult(bSuccess,
		bSuccess ? TEXT("出售成功") : (Failure.IsEmpty() ? TEXT("出售失败") : Failure));
}

void AAuraPlayerController::ServerForgeCampWeapon_Implementation(
	AAuraCampVendor* Vendor, const FGuid InstanceId, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = ValidateCampService(Vendor, Failure)
		&& Vendor->GetServiceType() == EAuraCampService::Blacksmith
		&& State && State->TryForgeWeapon(InstanceId, Failure);
	ClientCampTransactionResult(bSuccess,
		bSuccess ? TEXT("装备强化成功") : (Failure.IsEmpty() ? TEXT("强化失败") : Failure));
}

void AAuraPlayerController::ServerCraftCampEquipment_Implementation(
	AAuraCampVendor* Vendor, const EAuraEquipmentSlot Slot, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = ValidateCampService(Vendor, Failure)
		&& Vendor->GetServiceType() == EAuraCampService::Blacksmith
		&& State && State->TryCraftEquipment(Slot, Failure);
	ClientCampTransactionResult(bSuccess,
		bSuccess ? TEXT("打造完成，装备已放入背包")
			: (Failure.IsEmpty() ? TEXT("打造失败") : Failure));
}

void AAuraPlayerController::ServerSmeltCampEquipment_Implementation(
	AAuraCampVendor* Vendor, const FGuid InstanceId, const FGuid RequestId)
{
	if (!RegisterMageRequest(RequestId)) return;
	FString Failure;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const bool bSuccess = ValidateCampService(Vendor, Failure)
		&& Vendor->GetServiceType() == EAuraCampService::Blacksmith
		&& State && State->TrySmeltEquipment(InstanceId, Failure);
	ClientCampTransactionResult(bSuccess,
		bSuccess ? TEXT("熔炼完成，已获得金属")
			: (Failure.IsEmpty() ? TEXT("熔炼失败") : Failure));
}

void AAuraPlayerController::ClientCampTransactionResult_Implementation(
	const bool bSucceeded, const FString& Message)
{
	bCampTransactionPending = false;
	if (bSucceeded) bCampConfirming = false;
	if (AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD()))
	{
		HUD->ShowStatusMessage(Message,
			bSucceeded ? FLinearColor(0.5f, 1.f, 0.5f)
				: FLinearColor(1.f, 0.35f, 0.3f), 2.5f);
	}
}

void AAuraPlayerController::CloseInventoryContextMenu()
{
	InventoryContextSlot = INDEX_NONE;
	InventoryContextMenuPosition = FVector2D::ZeroVector;
}

void AAuraPlayerController::UseQuickConsumable(const EAuraConsumableEffect Effect)
{
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(GetPawn());
	if (!AuraCharacter || AuraCharacter->IsDead()) return;
	if (UAuraInventoryComponent* Inventory = GetAuraInventory())
	{
		Inventory->RequestUseFirstConsumable(Effect);
	}
}

UAuraInventoryComponent* AAuraPlayerController::GetAuraInventory() const
{
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	return AuraPlayerState ? AuraPlayerState->GetInventoryComponent() : nullptr;
}

UAuraEquipmentComponent* AAuraPlayerController::GetAuraEquipment() const
{
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	return AuraPlayerState ? AuraPlayerState->GetEquipmentComponent() : nullptr;
}

void AAuraPlayerController::RefreshInputContexts()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer) return;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem) return;

	if (!bInputContextsInitialized)
	{
		Subsystem->ClearAllMappings();
		if (CommonMappingContext) Subsystem->AddMappingContext(CommonMappingContext, 1);
		bInputContextsInitialized = true;
	}

	if (TopDownMappingContext) Subsystem->RemoveMappingContext(TopDownMappingContext);
	UInputMappingContext* ModeContext = TopDownMappingContext.Get();
	if (ModeContext) Subsystem->AddMappingContext(ModeContext, 0);

	UE_LOG(LogAuraInput, Display, TEXT("Installed input contexts. Common=%s (%d mappings), Mode=%s (%d mappings)"),
		*GetNameSafe(CommonMappingContext), CommonMappingContext ? CommonMappingContext->GetMappings().Num() : 0,
		*GetNameSafe(ModeContext), ModeContext ? ModeContext->GetMappings().Num() : 0);
}

void AAuraPlayerController::ApplyCursorMode(EAuraCameraMode)
{
	if (bIsRunUpgradeSelectionActive || bInventoryOpen || bArchmageMenuOpen
		|| bLanMenuOpen
		|| bCampMenuOpen)
	{
		bShowMouseCursor = true;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;
		SetIgnoreLookInput(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		SetInputMode(InputMode);
		return;
	}
	bShowMouseCursor = true;
	bEnableClickEvents = bShowMouseCursor;
	bEnableMouseOverEvents = bShowMouseCursor;
	SetIgnoreLookInput(false);

	if (bShowMouseCursor)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(true);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
		SetInputMode(InputMode);
		if (GetWorld() && GetWorld()->GetGameViewport())
		{
			// LMB attacks and RMB camera rotation both need the initial mouse-down routed
			// through SceneViewport. Look() still limits camera rotation to RMB only.
			GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
		}
	}
}
