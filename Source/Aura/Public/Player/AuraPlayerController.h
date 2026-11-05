// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Game/AuraRunUpgrade.h"
#include "Character/AuraCharacter.h"
#include "Items/AuraItemTypes.h"
#include "GameFramework/PlayerController.h"
#include "Player/AuraCameraMode.h"
#include "AuraPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class USoundBase;
class ACameraActor;
class AAuraEnemy;
class AAuraTreasureChest;
class AAuraWorldItem;
class AAuraArchmage;
class AAuraCampVendor;
class AAuraRitualSite;
class AAuraDungeonGate;
class UAuraEquipmentComponent;
class UAuraInventoryComponent;
class UAuraServiceMenuWidget;
class UStaticMeshComponent;
class UMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
enum class EAuraConsumableEffect : uint8;
enum class EAuraEquipmentSlot : uint8;
struct FInputActionValue;

struct FAuraCampServiceRow
{
	int32 OfferId = 0;
	FGuid InstanceId;
	int32 Price = 0;
	bool bEquipped = false;
	FString Label;
	FAuraItemInstance Item;
};

struct FAuraCameraFadedWall
{
	TWeakObjectPtr<UMaterialInterface> OriginalMaterial;
	float Opacity = 1.f;
};

UCLASS()
class AURA_API AAuraPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAuraPlayerController();

	UFUNCTION(BlueprintCallable, Category = "Aura|Camera")
	void SetAuraCameraMode(EAuraCameraMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Aura|Camera")
	EAuraCameraMode GetCameraMode() const;

	UFUNCTION(BlueprintCallable, Category = "Aura|Targeting")
	FAuraAimResult GetAimResult(float TraceDistance = 10000.f) const;
	/** The fire bolt may lock only the enemy directly under the mouse cursor. */
	AAuraEnemy* GetFireBoltCursorTarget() const;
	/** Uses the target sampled on mouse-down, before viewport capture can move the cursor. */
	AAuraEnemy* ConsumeFireBoltClickTarget();

	/** True after Q enters Arcane Shards targeting and until LMB confirms or Q cancels. */
	UFUNCTION(BlueprintPure, Category = "Aura|Targeting")
	bool IsArcaneShardsTargeting() const { return bIsArcaneShardsTargeting; }

	/** Supplies the clamped ground preview to the HUD. */
	bool GetArcaneShardsTargetPreview(FVector& OutLocation, bool& bOutClamped) const;

	/** Called by the instanced ability when activation begins. */
	bool ConsumeArcaneShardsCastTarget(FVector& OutLocation);

	/** True after E enters Shock direction targeting and until LMB confirms or E cancels. */
	UFUNCTION(BlueprintPure, Category = "Aura|Targeting")
	bool IsShockTargeting() const { return bIsShockTargeting; }

	/** Supplies the full-range directional preview endpoint to the HUD. */
	bool GetShockTargetPreview(FVector& OutLocation, bool& bOutClamped) const;

	/** Called by Shock when activation begins so it uses the confirmed mouse direction. */
	bool ConsumeShockCastDirection(FVector& OutDirection);

	/** Called by an authoritative treasure cache to offer three run boons. */
	bool BeginRunUpgradeSelection(AAuraTreasureChest* SourceChest);
	bool ChooseRunUpgradeByIndex(int32 OptionIndex);
	bool IsRunUpgradeSelectionActive() const { return bIsRunUpgradeSelectionActive; }
	const TArray<EAuraRunUpgradeType>& GetRunUpgradeOptions() const { return RunUpgradeOptions; }
	int32 GetHoveredRunUpgradeIndex() const;

	void HandleDemoVictory(AAuraEnemy* DefeatedBoss = nullptr);
	/** Server routes shared encounter/checkpoint feedback to the owning client's HUD. */
	void ShowCoopMessage(const FString& Message, const FLinearColor& Color,
		float Duration, const FString& Objective = FString());
	/** Clears transient encounter guidance while a room silently arms. */
	void ClearCoopGuidance();
	bool IsDemoCompleted() const { return bDemoCompleted; }
	bool IsVictoryCinematicActive() const { return bVictoryCinematicActive; }
	bool IsVictoryOverlayVisible() const { return bVictoryOverlayVisible; }
	bool HasVictoryCamera() const { return VictoryCameraActor != nullptr; }
	bool IsInventoryOpen() const { return bInventoryOpen; }
	bool IsInventoryQuestPage() const { return bInventoryQuestPage; }
	bool IsInventorySkillPage() const { return bInventorySkillPage; }
	bool IsLanMenuOpen() const { return bLanMenuOpen; }
	bool IsArchmageMenuOpen() const { return bArchmageMenuOpen; }
	int32 GetArchmagePendingAction() const { return ArchmagePendingAction; }
	int32 GetMageDraftRank(EAuraPlayerSkill Skill) const;
	int32 GetMageDraftPoints() const { return MageDraftPoints; }
	int32 GetMageDraftComboChoice() const { return MageDraftComboChoice; }
	EAuraFireBoltPath GetMageDraftFirePath() const { return MageDraftFirePath; }
	EAuraArcanePath GetMageDraftArcanePath() const { return MageDraftArcanePath; }
	EAuraShockPath GetMageDraftShockPath() const { return MageDraftShockPath; }
	bool IsCampMenuOpen() const { return bCampMenuOpen; }
	bool IsCampSelling() const { return bCampSelling; }
	int32 GetSmithMode() const { return SmithMode; }
	bool IsCampConfirming() const { return bCampConfirming; }
	int32 GetCampPage() const { return CampPage; }
	AAuraCampVendor* GetActiveCampVendor() const { return ActiveCampVendor.Get(); }
	const FAuraCampServiceRow& GetCampPendingRow() const { return CampPendingRow; }
	TArray<FAuraCampServiceRow> BuildCampServiceRows() const;
	/** Invoked by the real UMG hit targets and by the keyboard fallback. */
	void HandleCampMenuAction(int32 Action);
	void HandleArchmageMenuAction(int32 Action);
	int32 GetSelectedInventorySlot() const { return SelectedInventorySlot; }
	bool IsInventoryDragging() const { return bInventoryDragging; }
	int32 GetInventoryDragSourceSlot() const { return InventoryDragSourceSlot; }
	int32 GetInventoryDragHoverSlot() const { return InventoryDragHoverSlot; }
	int32 GetInventoryContextSlot() const { return InventoryContextSlot; }
	FVector2D GetInventoryContextMenuPosition() const { return InventoryContextMenuPosition; }
	const TArray<TWeakObjectPtr<AAuraWorldItem>>& GetNearbyWorldItems() const
	{
		return NearbyWorldItems;
	}
	AAuraWorldItem* GetHighlightedWorldItem() const { return HighlightedWorldItem.Get(); }
	float GetVictoryPresentationProgress() const;
	float GetVictoryOverlayDelay() const { return VictoryOverlayDelay; }
	bool IsCoopSpectating() const { return bCoopSpectating; }
	AAuraCharacter* GetSpectatedTeammate() const { return SpectatedTeammate.Get(); }
	AAuraCharacter* FindRescueCandidate() const;
	UFUNCTION(Exec)
	void AuraTestCoopRescue();
	UFUNCTION(Exec)
	void AuraTestElementalSkills();
	UFUNCTION(Exec)
	void AuraTestElementalSave();
	UFUNCTION(Exec)
	void AuraObserveElementalSkills();
	UFUNCTION(Client, Reliable)
	void ClientCastElementalSmoke(FVector QTarget, FVector ETarget);
	UFUNCTION(Client, Reliable)
	void ClientRescueChannelEnded(int32 RequestId);

	/** Useful for smoke tests and the in-editor console. */
	UFUNCTION(Exec)
	void AuraTestMoveForward();

	UFUNCTION(Exec)
	void AuraTestCameraKey();

	UFUNCTION(Exec)
	void AuraTestLookInput();

	UFUNCTION(Exec)
	void AuraTestStrafeKey();

	UFUNCTION(Exec)
	void AuraTestTurnMovement(FString KeyName);

	UFUNCTION(Exec)
	void AuraTestTopDownLook();

	UFUNCTION(Exec)
	void AuraTestTopDownMovement();

	UFUNCTION(Exec)
	void AuraLogCharacterPresentation();

	UFUNCTION(Exec)
	void AuraCaptureMovementPresentation();

	UFUNCTION(Exec)
	void AuraTestFireBolt(bool bLegacyTopDown = true);

	UFUNCTION(Exec)
	void AuraTestFacingFireBolt(float FacingYaw = 180.f, bool bLegacyTopDown = true);

	UFUNCTION(Exec)
	void AuraTestArcaneShards(bool bLegacyTopDown = true);

	UFUNCTION(Exec)
	void AuraTestShock(bool bLegacyTopDown = true);

	UFUNCTION(Exec)
	void AuraTestTargetingPolish();
	UFUNCTION(Exec)
	void AuraTestFireBoltTurn();
	UFUNCTION(Exec)
	void AuraTestPursuitM25();
	UFUNCTION(Exec)
	void AuraTestInventoryMoveM25();
	UFUNCTION(Exec)
	void AuraTestInventoryMoveCoopServerM25();
	UFUNCTION(Exec)
	void AuraTestInventoryMoveCoopClientM25();

	UFUNCTION(Exec)
	void AuraTestPickups();

	UFUNCTION(Exec)
	void AuraTestFeedbackVFX();

	/** Renders every enemy skill-VFX family together and verifies forced cleanup. */
	UFUNCTION(Exec)
	void AuraTestEnemySkillVFX();

	UFUNCTION(Exec)
	void AuraTestManaBottleOnly();

	UFUNCTION(Exec)
	void AuraTestCombatBalance();

	UFUNCTION(Exec)
	void AuraTestProgression();
	UFUNCTION(Exec)
	void AuraTestGrowthM12();
	UFUNCTION(Exec)
	void AuraTestGrowthM21();
	UFUNCTION(Exec)
	void AuraTestGrowthCoopM21();
	UFUNCTION(Exec)
	void AuraObserveGrowthM21();
	UFUNCTION(Exec)
	void AuraTestSaveReadOnlyM21();
	UFUNCTION(Exec)
	void AuraTestLoadedSaveM21();
	UFUNCTION(Exec)
	void AuraTestSeedV2SaveM21();
	UFUNCTION(Exec)
	void AuraTestFractureRankM21();
	UFUNCTION(Exec)
	void AuraTestRouteRewardsM22();
	UFUNCTION(Exec)
	void AuraTestFullSpineM22();
	UFUNCTION(Exec)
	void AuraTestRouteRewardsReloadM22();
	UFUNCTION(Exec)
	void AuraTestServiceUIM17();
	UFUNCTION(Exec)
	void AuraTestGrowthCoopM12();
	UFUNCTION(Exec)
	void AuraObserveGrowthM12();
	UFUNCTION(Exec)
	void AuraTestPostBossCampM12();
	UFUNCTION(Exec)
	void AuraTestVictoryReturnM12();
	UFUNCTION(Exec)
	void AuraTestCampEconomyM13();
	UFUNCTION(Exec)
	void AuraTestEconomyM23();
	UFUNCTION(Exec)
	void AuraTestEconomyCoopM23();
	UFUNCTION(Exec)
	void AuraObserveEconomyM23();
	UFUNCTION(Exec)
	void AuraTestSeedV3SaveM23();
	UFUNCTION(Exec)
	void AuraTestCampCoopM13();
	UFUNCTION(Exec)
	void AuraObserveCampM13();
	UFUNCTION(Exec)
	void AuraTestSealRitualM14();
	UFUNCTION(Exec)
	void AuraTestSealCoopM14();
	UFUNCTION(Exec)
	void AuraObserveSealM14();
	UFUNCTION(Exec)
	void AuraTestSealChoiceM16();
	UFUNCTION(Exec)
	void AuraTestAbyssTrialM16();
	UFUNCTION(Exec)
	void AuraTestWriteLegacySaveM16();
	UFUNCTION(Exec)
	void AuraTestVerifyLegacySaveM16();
	UFUNCTION(Exec)
	void AuraTestLegendaryM16();
	UFUNCTION(Exec)
	void AuraTestPackagedAnimationM16();
	UFUNCTION(Exec)
	void AuraTestSeedSaveM15();
	UFUNCTION(Exec)
	void AuraTestVerifySaveM15();
	UFUNCTION(Exec)
	void AuraTestNetSetupM15();
	UFUNCTION(Exec)
	void AuraTestNetPurchaseM15();
	UFUNCTION(Exec)
	void AuraTestLanHostM15();
	UFUNCTION(Exec)
	void AuraTestLanJoinM15();

	UFUNCTION(Exec)
	void AuraTestRunUpgrades();

	UFUNCTION(Exec)
	void AuraTestDungeonFlow();

	UFUNCTION(Exec)
	void AuraTestCheckpointReload();

	UFUNCTION(Exec)
	void AuraTestDungeonNavigation();
	void AuraTestLoreM20();
	void AuraTestDungeonM21();

	UFUNCTION(Exec)
	void AuraTestTreasureCaches();

	UFUNCTION(Exec)
	void AuraTestRoomAmbush();

	UFUNCTION(Exec)
	void AuraTestGateFade();
	UFUNCTION(Exec)
	void AuraTestCameraWallFade();
	UFUNCTION(Exec)
	void AuraTestInteractableGlow();

	UFUNCTION(Exec)
	void AuraCaptureDungeonTour();

	UFUNCTION(Exec)
	void AuraCaptureDungeonBranches();

	UFUNCTION(Exec)
	void AuraTestCombatLoop();

	/** Fires real projectiles through the small-enemy capsules at the player's cast height. */
	UFUNCTION(Exec)
	void AuraTestCreatureFireBoltHitboxes();

	UFUNCTION(Exec)
	void AuraTestEnemyAI();

	UFUNCTION(Exec)
	void AuraRestartDemo();

	UFUNCTION(Exec)
	void AuraTestMeleeSync();

	UFUNCTION(Exec)
	void AuraTestShamanRanged();

	UFUNCTION(Exec)
	void AuraTestBossPhases();

	/** Exercises every Demon Lord attack family, including summoned minion ownership. */
	UFUNCTION(Exec)
	void AuraTestBossArsenal();

	/** Checks the phase-two barrage warning, timed shot count, and channel cleanup. */
	UFUNCTION(Exec)
	void AuraTestBossBarrage();

	/** Validates boss armor, ward absorption/overflow, expiry, and death cleanup. */
	UFUNCTION(Exec)
	void AuraTestBossWard();

	/** Exercises the Ghoul, Goblin, Shroom, and Shaman attack rotations in one run. */
	UFUNCTION(Exec)
	void AuraTestEnemyAttackVariety();

	/** Verifies authored elite slots, standing/ground spawning, scaling, and enrage. */
	UFUNCTION(Exec)
	void AuraTestEliteVariants();
	UFUNCTION(Exec)
	void AuraTestEliteSkillKits();
	UFUNCTION(Exec)
	void AuraTestEnemyPoise();
	void AuraTestEliteReplicationServer();
	void AuraObserveEliteVariants();
	void AuraObserveEliteSkillReplication();

	/** Lightweight client-side cast sequence used by the headless co-op regression test. */
	UFUNCTION(Exec)
	void AuraTestNetworkAbilities();

	/** Authoritative M11 combo state, area damage, chain limit, and expiry regression. */
	UFUNCTION(Exec)
	void AuraTestCombos();
	UFUNCTION(Exec)
	void AuraTestComboM18();
	UFUNCTION(Exec)
	void AuraTestEnemySynergyM19();

	UFUNCTION(Exec)
	void AuraLogComboMarks();

	UFUNCTION(Exec)
	void AuraTestSkillRanks();

	UFUNCTION(Exec)
	void AuraTestComboAbilities();

	UFUNCTION(Exec)
	void AuraTestNetworkRespawn();

	UFUNCTION(Exec)
	void AuraTestInventory();

	UFUNCTION(Exec)
	void AuraTestInventoryHeldMovement();

	UFUNCTION(Exec)
	void AuraCaptureInventoryInteraction();

	UFUNCTION(Exec)
	void AuraCaptureWorldLootMarkers();

	UFUNCTION(Exec)
	void AuraTestManualPickupNetwork();

	UFUNCTION(Exec)
	void AuraGiveItem(FString ItemId, int32 Quantity = 1);

	UFUNCTION(Exec)
	void AuraDropInventorySlot(int32 SlotIndex, int32 Quantity = 1);

	UFUNCTION(Exec)
	void AuraLogInventory();

	UFUNCTION(Exec)
	void AuraTestEquipment();

	UFUNCTION(Exec)
	void AuraLogEquipment();

	UFUNCTION(Exec)
	void AuraTestLoot();

	UFUNCTION(Exec)
	void AuraLogLoot();

	UFUNCTION(Exec)
	void AuraTestRoomStateMachine();

	UFUNCTION(Exec)
	void AuraLogRoomState();

protected:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnRep_Pawn() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputMappingContext> CommonMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputMappingContext> TopDownMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputAction> PrimaryAttackAction;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputAction> Ability1Action;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputAction> Ability2Action;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Input")
	TObjectPtr<UInputAction> RestartAction;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Targeting")
	TEnumAsByte<ECollisionChannel> TargetingTraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Audio")
	TObjectPtr<USoundBase> VictorySting;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Audio")
	TObjectPtr<USoundBase> VictoryAccentSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Camera", meta = (ClampMin = "0.0"))
	float VictoryCameraBlendTime = 0.55f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Camera", meta = (ClampMin = "100.0"))
	float VictoryCameraDistance = 650.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Camera", meta = (ClampMin = "0.0"))
	float VictoryCameraHeight = 180.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Camera", meta = (ClampMin = "0.1"))
	float VictoryCameraMotionDuration = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Camera", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float VictoryCameraOrbitDegrees = 18.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Camera", meta = (ClampMin = "20.0", ClampMax = "120.0"))
	float VictoryCameraFOV = 48.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Timing", meta = (ClampMin = "0.0"))
	float VictoryStingDelay = 1.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Victory|Timing", meta = (ClampMin = "0.1"))
	float VictoryOverlayDelay = 3.2f;

private:
	void UpdateCoopRescue();
	void ObserveCoopRescueSmoke();
	UFUNCTION(Client, Reliable)
	void ClientDriveCoopRescueSmoke(int32 Command);
	UFUNCTION(Server, Reliable)
	void ServerReportCoopRescueSmoke(uint8 Observations);
	uint8 RescueSmokeObservations = 0;
	int32 RescueSmokeStartAttempts = 0;
	bool HandleCoopRescueInput(const FInputKeyEventArgs& Params);
	void CancelCoopRescue();
	UFUNCTION(Server, Reliable)
	void ServerBeginRescue(AAuraCharacter* Target, int32 RequestId);
	UFUNCTION(Server, Reliable)
	void ServerCancelRescue(int32 RequestId);
	UFUNCTION(Client, Reliable)
	void ClientRescueRequestResult(int32 RequestId, bool bAccepted);
	bool bCoopSpectating = false;
	bool bRescueInputHeld = false;
	int32 NextRescueRequestId = 0;
	TWeakObjectPtr<AAuraCharacter> SpectatedTeammate;

	void AuraTestFullSpineM22Step(int32 Index);
	/** Uses real growth transactions to unlock combos for isolated legacy smoke tests. */
	bool PrepareComboSmokeRanks(const TArray<EAuraComboMarkType>& RequiredCombos);

	UFUNCTION(Client, Reliable)
	void ClientShowCoopMessage(const FString& Message, FLinearColor Color,
		float Duration, const FString& Objective);

	UFUNCTION(Client, Reliable)
	void ClientClearCoopGuidance();

	UFUNCTION(Server, Reliable)
	void ServerRequestRespawn();

	UFUNCTION(Server, Reliable)
	void ServerRunNetworkRespawnSmoke();

	UFUNCTION(Server, Reliable)
	void ServerRunInventorySmoke();

	UFUNCTION(Server, Reliable)
	void ServerRunEquipmentSmoke();

	UFUNCTION(Server, Reliable)
	void ServerRunLootSmoke();

	UFUNCTION(Server, Reliable)
	void ServerRunRoomSmoke();

	UFUNCTION(Server, Reliable)
	void ServerGrantDebugItem(FName ItemId, int32 Quantity);

	UFUNCTION(Server, Reliable)
	void ServerPickupWorldItem(AAuraWorldItem* WorldItem);

	UFUNCTION(Server, Reliable)
	void ServerInteractRewardChest(AActor* ChestActor);

	UFUNCTION(Server, Reliable)
	void ServerInteractRitualSite(AAuraRitualSite* Site, FGuid RequestId);

	UFUNCTION(Server, Reliable)
	void ServerOpenArchmage(AAuraArchmage* Mage);
	UFUNCTION(Client, Reliable)
	void ClientOpenArchmage(AAuraArchmage* Mage);
	UFUNCTION(Server, Reliable)
	void ServerAllocateMageSkill(AAuraArchmage* Mage, EAuraPlayerSkill Skill, FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerAllocateMageFirePath(AAuraArchmage* Mage, EAuraFireBoltPath Path,
		FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerChooseMageEvolution(AAuraArchmage* Mage, EAuraPlayerSkill Skill, int32 Choice, FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerAllocateMageCombo(AAuraArchmage* Mage, EAuraComboMarkType Combo, FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerRespecMageSkills(AAuraArchmage* Mage, int32 FireRank, int32 QRank,
		int32 ERank, EAuraFireBoltPath FirePath, FGuid StoneId, FGuid RequestId,
		EAuraArcanePath QPath = EAuraArcanePath::None, EAuraShockPath EPath = EAuraShockPath::None);
	UFUNCTION(Client, Reliable)
	void ClientMageTransactionResult(bool bSucceeded, const FString& Message);
	UFUNCTION(Server, Reliable)
	void ServerOpenCampVendor(AAuraCampVendor* Vendor);
	UFUNCTION(Client, Reliable)
	void ClientOpenCampVendor(AAuraCampVendor* Vendor);
	UFUNCTION(Server, Reliable)
	void ServerBuyCampOffer(AAuraCampVendor* Vendor, int32 OfferId, FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerSellCampItem(AAuraCampVendor* Vendor, FGuid InstanceId, FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerForgeCampWeapon(AAuraCampVendor* Vendor, FGuid InstanceId, FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerCraftCampEquipment(AAuraCampVendor* Vendor, EAuraEquipmentSlot Slot,
		FGuid RequestId);
	UFUNCTION(Server, Reliable)
	void ServerSmeltCampEquipment(AAuraCampVendor* Vendor, FGuid InstanceId,
		FGuid RequestId);
	UFUNCTION(Client, Reliable)
	void ClientCampTransactionResult(bool bSucceeded, const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientBeginRunUpgradeSelection(AAuraTreasureChest* SourceChest,
		const TArray<EAuraRunUpgradeType>& Options);

	UFUNCTION(Server, Reliable)
	void ServerChooseRunUpgrade(AAuraTreasureChest* SourceChest, EAuraRunUpgradeType Type);

	UFUNCTION(Client, Reliable)
	void ClientCompleteRunUpgradeSelection(EAuraRunUpgradeType Type, int32 NewRank);

	UFUNCTION(Client, Reliable)
	void ClientHandleDemoVictory(AAuraEnemy* DefeatedBoss, FVector BossFocusLocation,
		FVector CameraApproachDirection, float BossScale);
	UFUNCTION(Server, Reliable)
	void ServerRequestPostBossCamp();
	UFUNCTION(Client, Reliable)
	void ClientFinishPostBossCampReturn();
	bool TeleportPawnToPostBossCamp();
	void FinishPostBossCampReturn();

	void ApplyDemoVictory(AAuraEnemy* DefeatedBoss, const FVector& BossFocusLocation,
		const FVector& CameraApproachDirection, float BossScale);
	void StartVictoryCinematic(AAuraEnemy* DefeatedBoss, const FVector& BossFocusLocation,
		const FVector& CameraApproachDirection);
	void UpdateVictoryCinematic(float DeltaTime);
	void PlayVictorySting();
	void RevealVictoryOverlay();
	TArray<EAuraRunUpgradeType> BuildRunUpgradeOptions(
		const AAuraTreasureChest* SourceChest) const;
	void EnterRunUpgradeSelection(AAuraTreasureChest* SourceChest,
		const TArray<EAuraRunUpgradeType>& Options);
	bool ResolveRunUpgradeSelection(AAuraTreasureChest* SourceChest,
		EAuraRunUpgradeType Type);
	void EndRunUpgradeSelection(EAuraRunUpgradeType ChosenType, int32 NewRank,
		bool bShowFeedback);
	void Move(const FInputActionValue& InputActionValue);
	void Look(const FInputActionValue& InputActionValue);
	void PrimaryAttack();
	void ToggleArcaneShardsTargeting();
	void ConfirmArcaneShardsTargeting();
	void CancelArcaneShardsTargeting();
	void ActivateArcaneShards();
	void ToggleShockTargeting();
	void ConfirmShockTargeting();
	void CancelShockTargeting();
	void ActivateShock();
	void UpdateArcaneShardsTargeting();
	void UpdateShockTargeting();
	bool FindGroundTargetUnderMouse(float MaxRange, FVector& OutLocation, bool& bOutClamped);
	void RestartDemo();
	void ToggleInventory();
	void UseQuickConsumable(EAuraConsumableEffect Effect);
	void RunInventorySmoke();
	void RunEquipmentSmoke();
	void RunLootSmoke();
	void RunRoomSmoke();
	void HandleInventoryLeftClick();
	void BeginInventoryDragCandidate();
	void UpdateInventoryDrag();
	void FinishInventoryDrag();
	void ResetInventoryDrag();
	void HandleInventoryRightClick();
	void HandleInventoryDrop();
	void UpdateNearbyEquipment();
	void UpdateInteractableHighlights();
	void ClearInteractableHighlights();
	void UpdateEnemyHoverHighlight();
	void ClearEnemyHoverHighlight();
	void ClearNearbyEquipment();
	bool HandleNearbyEquipmentClick();
	void RequestPickupWorldItem(AAuraWorldItem* WorldItem);
	void PickupHighlightedWorldItem();
	bool TryInteractWithRewardChest();
	bool TryInteractWithRitualSite();
	bool TryInteractWithLoreRecord();
	bool TryInteractWithArchmage();
	bool TryInteractWithCampVendor();
	void CloseCampMenu();
	void ShowServiceMenuWidget(bool bMageMenu);
	void RemoveServiceMenuWidget();
	void BeginCampSelection(int32 LocalRow);
	void ConfirmCampAction();
	bool ValidateCampService(AAuraCampVendor* Vendor, FString& OutFailureReason) const;
	void CloseArchmageMenu();
	void BeginMageAction(int32 Action);
	void ConfirmMageAction();
	void ResetMageDraft();
	void AddMageDraftRank(EAuraPlayerSkill Skill);
	bool ValidateMageService(AAuraArchmage* Mage, FString& OutFailureReason) const;
	bool RegisterMageRequest(const FGuid& RequestId);
	void CloseInventoryContextMenu();
	UAuraInventoryComponent* GetAuraInventory() const;
	UAuraEquipmentComponent* GetAuraEquipment() const;
	void RefreshInputContexts();
	void ApplyCursorMode(EAuraCameraMode NewMode);
	void UpdateCameraOcclusion();
	void RestoreCameraOcclusion();
	FVector MovementSmokeTestStart = FVector::ZeroVector;
	bool bUseDebugAimOverride = false;
	FVector DebugAimOverride = FVector::ZeroVector;
	TWeakObjectPtr<AAuraEnemy> DebugAimActorOverride;
	TWeakObjectPtr<AAuraEnemy> PendingFireBoltClickTarget;
	float PendingFireBoltClickExpiry = -1.f;
	bool bHasPendingFireBoltClick = false;
	bool bUseDebugGroundTargetOverride = false;
	FVector DebugGroundTargetOverride = FVector::ZeroVector;
	bool bForceInvalidGroundTargetForSmoke = false;
	bool bIsArcaneShardsTargeting = false;
	bool bArcaneShardsPreviewValid = false;
	bool bArcaneShardsTargetClamped = false;
	FVector ArcaneShardsPreviewLocation = FVector::ZeroVector;
	bool bHasCommittedArcaneShardsTarget = false;
	FVector CommittedArcaneShardsTarget = FVector::ZeroVector;
	bool bIsShockTargeting = false;
	bool bShockPreviewValid = false;
	bool bShockTargetClamped = false;
	FVector ShockPreviewLocation = FVector::ZeroVector;
	FVector ShockPreviewDirection = FVector::ForwardVector;
	bool bHasCommittedShockDirection = false;
	FVector CommittedShockDirection = FVector::ForwardVector;
	bool bIsRunUpgradeSelectionActive = false;
	bool bInventoryOpen = false;
	bool bInventoryQuestPage = false;
	bool bInventorySkillPage = false;
	bool bLanMenuOpen = false;
	bool bArchmageMenuOpen = false;
	int32 ArchmagePendingAction = 0;
	bool bMageTransactionPending = false;
	int32 MageDraftFireRank = 1;
	int32 MageDraftQRank = 0;
	int32 MageDraftERank = 0;
	int32 MageDraftPoints = 0;
	int32 MageDraftComboChoice = INDEX_NONE;
	EAuraFireBoltPath MageDraftFirePath = EAuraFireBoltPath::None;
	EAuraArcanePath MageDraftArcanePath = EAuraArcanePath::None;
	EAuraShockPath MageDraftShockPath = EAuraShockPath::None;
	TWeakObjectPtr<AAuraArchmage> ActiveArchmage;
	TWeakObjectPtr<AAuraCampVendor> ActiveCampVendor;
	UPROPERTY(Transient)
	TObjectPtr<UAuraServiceMenuWidget> ActiveServiceMenuWidget;
	bool bCampMenuOpen = false;
	bool bCampSelling = false;
	int32 SmithMode = 0; // 0 enhancement, 1 slot-selected crafting, 2 smelting.
	bool bCampConfirming = false;
	bool bCampTransactionPending = false;
	int32 CampPage = 0;
	FAuraCampServiceRow CampPendingRow;
	TArray<FGuid> RecentMageRequests;
	int32 SelectedInventorySlot = INDEX_NONE;
	int32 InventoryDragSourceSlot = INDEX_NONE;
	int32 InventoryDragHoverSlot = INDEX_NONE;
	FGuid InventoryDragInstanceId;
	FVector2D InventoryDragStartPosition = FVector2D::ZeroVector;
	bool bInventoryDragging = false;
	int32 InventoryContextSlot = INDEX_NONE;
	FVector2D InventoryContextMenuPosition = FVector2D::ZeroVector;
	TArray<TWeakObjectPtr<AAuraWorldItem>> NearbyWorldItems;
	TWeakObjectPtr<AAuraWorldItem> HighlightedWorldItem;
	TWeakObjectPtr<AAuraWorldItem> PendingPickupWorldItem;
	/** Local-only overlay state; never changes shared meshes, collision, or replication. */
	TMap<TWeakObjectPtr<UMeshComponent>, TWeakObjectPtr<UMaterialInterface>>
		InteractableOriginalOverlays;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Interaction|Visuals")
	TObjectPtr<UMaterialInterface> InteractableGlowMaterial;
	float NextInteractableHighlightRefreshTime = 0.f;
	/** Cursor feedback is client-only; the existing gold outline supplies the hull shader. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EnemyHoverOutlineMaterial;
	TWeakObjectPtr<AAuraEnemy> HoveredFireBoltEnemy;
	TMap<TWeakObjectPtr<UMeshComponent>, TWeakObjectPtr<UMaterialInterface>>
		EnemyHoverOriginalOverlays;
	/** Local-only material swaps; collision and replicated map state are untouched. */
	TMap<TWeakObjectPtr<UStaticMeshComponent>, FAuraCameraFadedWall> CameraFadedWalls;
	TSet<TWeakObjectPtr<AAuraDungeonGate>> CameraOccludedGates;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> CameraOcclusionCandidates;
	TWeakObjectPtr<UWorld> CameraOcclusionWorld;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Camera|Occlusion")
	TObjectPtr<UMaterialInterface> CameraWallFadeMaterial;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Camera|Occlusion",
		meta = (ClampMin = "0.2", ClampMax = "0.9"))
	float CameraOccludedWallOpacity = 0.5f;
	float NextNearbyEquipmentRefreshTime = 0.f;
	float PendingPickupUntil = 0.f;
	TArray<EAuraRunUpgradeType> RunUpgradeOptions;

	UPROPERTY(Transient)
	TObjectPtr<AAuraTreasureChest> PendingTreasureChest;
	bool bInputContextsInitialized = false;
	bool bDemoCompleted = false;
	bool bVictoryCinematicActive = false;
	bool bVictoryOverlayVisible = false;
	float VictoryCinematicElapsed = 0.f;
	float VictoryCameraScale = 1.f;
	FVector VictoryFocusLocation = FVector::ZeroVector;
	FVector VictoryCameraApproachDirection = FVector::ForwardVector;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> VictoryCameraActor;

	UPROPERTY(Transient)
	TObjectPtr<AAuraEnemy> VictoryBossActor;

	FTimerHandle VictoryStingTimer;
	FTimerHandle VictoryOverlayTimer;
};
