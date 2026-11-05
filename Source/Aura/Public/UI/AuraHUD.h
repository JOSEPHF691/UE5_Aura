// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AuraHUD.generated.h"

class AAuraEnemy;
class AAuraWorldItem;
class UFont;
class UTexture2D;
enum class EAuraEquipmentSlot : uint8;

struct FAuraFloatingDamage
{
	FVector WorldLocation = FVector::ZeroVector;
	float Amount = 0.f;
	float SpawnTime = 0.f;
};

UCLASS()
class AURA_API AAuraHUD : public AHUD
{
	GENERATED_BODY()

public:
	AAuraHUD();
	virtual void DrawHUD() override;

	void ShowDamageNumber(AActor* TargetActor, float Damage);
	void ShowPlayerHit(float Damage);
	void ShowStatusMessage(const FString& Message, const FLinearColor& Color = FLinearColor::White,
		float Duration = 1.25f);
	void ShowLoreRecord(FName RecordId, const FString& Title, const FString& LineOne,
		const FString& LineTwo, const FString& LineThree, const FLinearColor& Accent);
	void HideLoreRecord();
	bool IsLoreRecordVisible() const;
	FName GetVisibleLoreRecordId() const;
	void SetObjective(const FString& NewObjective);
	bool AreCombatCueTexturesConfigured() const;
	void ShowArcaneShardsTextureImpact(const FVector& Location, float Radius,
		float Duration = 0.55f);
	int32 GetHoveredInventorySlot() const;
	int32 GetHoveredMageAction() const;
	int32 GetHoveredCampAction() const;
	int32 GetHighlightedMageAction() const { return HighlightedMageAction; }
	int32 GetHighlightedCampAction() const { return HighlightedCampAction; }
	int32 GetHoveredLanRoomAction() const;
	EAuraEquipmentSlot GetHoveredEquipmentSlot() const;
	int32 GetHoveredInventoryContextAction() const;
	AAuraWorldItem* GetHoveredNearbyWorldItem() const;

private:
	void DrawPlayerVitals(float UIScale);
	void DrawAbilityPanel(float UIScale);
	void DrawConsumableQuickSlots(float UIScale);
	void DrawRunUpgradeSummary(float UIScale);
	void DrawRunUpgradeSelection(float UIScale);
	void DrawInventoryPanel(float UIScale);
	void DrawQuestJournal(float UIScale);
	void DrawSkillTree(float UIScale);
	void DrawSealTracker(float UIScale);
	void DrawArchmagePanel(float UIScale);
	void DrawCampServicePanel(float UIScale);
	void DrawLanRoomPanel(float UIScale);
	void DrawNearbyEquipmentPanel(float UIScale);
	void DrawTargetingReticle(float UIScale);
	void DrawArcaneShardsTargeting(float UIScale);
	void DrawArcaneShardsTextureImpact(float UIScale);
	void DrawShockTargeting(float UIScale);
	void DrawEnemyHealthBars(float UIScale);
	void DrawEnemyCombatTextureCues(const AAuraEnemy& Enemy, float UIScale);
	void DrawComboMarks(float UIScale);
	void DrawArcaneSlowFields(float UIScale);
	void DrawBossHealthBar(float UIScale);
	void DrawFloatingDamage(float UIScale);
	void DrawObjective(float UIScale);
	void DrawTransientMessage(float UIScale);
	void DrawLoreRecord(float UIScale);
	void DrawPlayerHitFlash(float UIScale);
	void DrawDefeatOverlay(float UIScale);
	void DrawCoopRescue(float UIScale);
	void DrawVictoryCinematic(float UIScale);
	void DrawVictoryOverlay(float UIScale);
	void DrawBar(const FString& Label, float CurrentValue, float MaxValue, const FLinearColor& FillColor,
		float X, float Y, float Width, float Height, float UIScale);
	void DrawItemIcon(UTexture2D* Icon, float X, float Y, float Width, float Height,
		const FLinearColor& Tint = FLinearColor::White);
	void DrawOutline(float X, float Y, float Width, float Height, const FLinearColor& Color, float Thickness);
	void DrawCenteredText(const FString& Text, const FLinearColor& Color, float CenterX, float Y,
		UFont* Font, float Scale);
	void DrawWorldCircle(const FVector& Center, float Radius, const FLinearColor& Color,
		float Thickness, int32 Segments = 64);
	void DrawWorldCorridor(const FVector& Start, const FVector& End, float HalfWidth,
		const FLinearColor& Color, float Thickness);
	void DrawWorldSector(const FVector& Center, const FVector& Direction, float Radius,
		float HalfAngleDegrees, const FLinearColor& Color, float Thickness, int32 ArcSegments = 20);
	void DrawWorldTexture(UTexture2D* Texture, const FVector& Center, float WorldRadius,
		const FLinearColor& Tint, float Scale = 1.f);
	void DrawWorldGroundTexture(UTexture2D* Texture, const FVector& Center,
		float WorldRadius, const FLinearColor& Tint, float Scale = 1.f,
		float RotationDegrees = 0.f);
	void DrawWorldSprite(UTexture2D* Texture, const FVector& Center, float ScreenSize,
		const FLinearColor& Tint);
	void RefreshEnemyCache();
	float GetFireBoltCooldown(float& OutDuration, float& OutManaCost) const;
	float GetArcaneShardsCooldown(float& OutDuration, float& OutManaCost) const;
	float GetShockCooldown(float& OutDuration, float& OutManaCost) const;

	UPROPERTY()
	TObjectPtr<UTexture2D> FireBoltIcon;

	UPROPERTY()
	TObjectPtr<UTexture2D> ArcaneShardsIcon;

	UPROPERTY()
	TObjectPtr<UTexture2D> ShockIcon;

	UPROPERTY()
	TObjectPtr<UTexture2D> GoblinWindupTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> GoblinStrikeTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> ShroomBashTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> ShroomBurstTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> ArcaneTargetTexture;

	TArray<FAuraFloatingDamage> FloatingDamageNumbers;
	TArray<TWeakObjectPtr<AAuraEnemy>> CachedEnemies;
	FString ObjectiveText = TEXT("探索地牢");
	FString StatusMessage;
	FLinearColor StatusMessageColor = FLinearColor::White;
	float StatusMessageEndTime = 0.f;
	FName VisibleLoreRecordId = NAME_None;
	FString LoreTitle;
	FString LoreLines[3];
	FLinearColor LoreAccent = FLinearColor::White;
	float LoreEndTime = 0.f;
	float HitMarkerEndTime = 0.f;
	float PlayerHitFlashEndTime = 0.f;
	float NextEnemyCacheRefreshTime = 0.f;
	FVector ArcaneTextureImpactLocation = FVector::ZeroVector;
	float ArcaneTextureImpactRadius = 0.f;
	float ArcaneTextureImpactStartTime = 0.f;
	float ArcaneTextureImpactEndTime = 0.f;
	int32 HighlightedMageAction = INDEX_NONE;
	int32 HighlightedCampAction = INDEX_NONE;
	static constexpr float FloatingDamageLifetime = 1.8f;
};
