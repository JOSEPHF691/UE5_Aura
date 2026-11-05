// Copyright yosep

#include "UI/AuraHUD.h"

#include "AbilitySystem/Abilities/AuraArcaneShardsAbility.h"
#include "AbilitySystem/Abilities/AuraFireBoltAbility.h"
#include "AbilitySystem/Abilities/AuraShockAbility.h"
#include "AbilitySystem/AuraArcaneSlowField.h"
#include "AbilitySystem/AuraElementalSpell.h"
#include "AbilitySystem/AuraElementalStatusComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Coop/AuraReviveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "GameplayAbilitySpec.h"
#include "Game/AuraRunUpgrade.h"
#include "Game/AuraGameState.h"
#include "Game/AuraLanSessionSubsystem.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Items/AuraItemTypes.h"
#include "Items/AuraWorldItem.h"
#include "Level/AuraCampVendor.h"
#include "Kismet/GameplayStatics.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "UObject/ConstructorHelpers.h"

namespace AuraHUDColors
{
	const FLinearColor Panel(0.012f, 0.016f, 0.028f, 0.84f);
	const FLinearColor PanelInner(0.045f, 0.055f, 0.085f, 0.95f);
	const FLinearColor Border(0.76f, 0.57f, 0.2f, 0.95f);
	const FLinearColor Health(0.74f, 0.08f, 0.08f, 1.f);
	const FLinearColor Mana(0.08f, 0.32f, 0.88f, 1.f);
	const FLinearColor Experience(0.58f, 0.28f, 0.92f, 1.f);
	const FLinearColor Text(0.94f, 0.95f, 1.f, 1.f);
	const FLinearColor MutedText(0.63f, 0.67f, 0.76f, 1.f);
	const FLinearColor Reticle(0.98f, 0.79f, 0.28f, 0.95f);
}

namespace
{
	constexpr int32 InventoryColumns = 6;
	constexpr int32 InventoryRows = 4;

	int32 EquipmentSlotIndex(const EAuraEquipmentSlot Slot)
	{
		switch (Slot)
		{
		case EAuraEquipmentSlot::Weapon: return 0;
		case EAuraEquipmentSlot::Armor: return 1;
		case EAuraEquipmentSlot::Head: return 2;
		case EAuraEquipmentSlot::Shoes: return 3;
		default: return INDEX_NONE;
		}
	}

	EAuraEquipmentSlot EquipmentSlotAtIndex(const int32 Index)
	{
		switch (Index)
		{
		case 0: return EAuraEquipmentSlot::Weapon;
		case 1: return EAuraEquipmentSlot::Armor;
		case 2: return EAuraEquipmentSlot::Head;
		case 3: return EAuraEquipmentSlot::Shoes;
		default: return EAuraEquipmentSlot::None;
		}
	}

	const UAuraItemDefinition* DefinitionForEquipmentSlot(const EAuraEquipmentSlot Slot)
	{
		FName ItemId = NAME_None;
		switch (Slot)
		{
		case EAuraEquipmentSlot::Weapon: ItemId = TEXT("Equipment.ArcaneStaff"); break;
		case EAuraEquipmentSlot::Armor: ItemId = TEXT("Equipment.WardenArmor"); break;
		case EAuraEquipmentSlot::Head: ItemId = TEXT("Equipment.SeerHeadpiece"); break;
		case EAuraEquipmentSlot::Shoes: ItemId = TEXT("Equipment.WindShoes"); break;
		default: break;
		}
		return ItemId.IsNone() ? nullptr : UAuraItemCatalog::Get().FindItem(ItemId);
	}

	struct FAuraInventoryPanelLayout
	{
		float Scale = 1.f;
		FBox2D Panel;
		FVector2D EquipmentOrigin = FVector2D::ZeroVector;
		FVector2D GridOrigin = FVector2D::ZeroVector;
		float InventorySlotWidth = 0.f;
		float InventorySlotHeight = 0.f;
		float EquipmentSlotWidth = 0.f;
		float EquipmentSlotHeight = 0.f;
		float ContentHeight = 0.f;
		float Gap = 0.f;

		FBox2D GetInventorySlotRect(const int32 SlotIndex) const
		{
			const int32 Column = SlotIndex % InventoryColumns;
			const int32 Row = SlotIndex / InventoryColumns;
			const FVector2D Min = GridOrigin + FVector2D(
				Column * (InventorySlotWidth + Gap),
				Row * (InventorySlotHeight + Gap));
			return FBox2D(Min, Min + FVector2D(InventorySlotWidth, InventorySlotHeight));
		}

		FBox2D GetEquipmentSlotRect(const EAuraEquipmentSlot Slot) const
		{
			const int32 Index = EquipmentSlotIndex(Slot);
			if (Index == INDEX_NONE) return FBox2D(EForceInit::ForceInit);
			const int32 Column = Index % 2;
			const int32 Row = Index / 2;
			const FVector2D Min = EquipmentOrigin + FVector2D(
				Column * (EquipmentSlotWidth + Gap),
				Row * (EquipmentSlotHeight + Gap));
			return FBox2D(Min, Min + FVector2D(EquipmentSlotWidth, EquipmentSlotHeight));
		}
	};

	FAuraInventoryPanelLayout MakeInventoryPanelLayout(
		const FVector2D Viewport, const bool bReserveNearbyPanel = false)
	{
		FAuraInventoryPanelLayout Result;
		Result.Scale = FMath::Clamp(FMath::Min(
			Viewport.X / 1280.f, Viewport.Y / 720.f), 0.75f, 2.f);
		const float Padding = 22.f * Result.Scale;
		const float HeaderHeight = 70.f * Result.Scale;
		const float FooterHeight = 50.f * Result.Scale;
		const float ContentGap = 22.f * Result.Scale;
		const float EquipmentWidth = 280.f * Result.Scale;
		Result.Gap = 7.f * Result.Scale;
		Result.InventorySlotWidth = 96.f * Result.Scale;
		Result.InventorySlotHeight = 72.f * Result.Scale;
		Result.EquipmentSlotWidth = 136.5f * Result.Scale;
		Result.EquipmentSlotHeight = 60.f * Result.Scale;
		const float GridWidth = InventoryColumns * Result.InventorySlotWidth
			+ (InventoryColumns - 1) * Result.Gap;
		const float GridHeight = InventoryRows * Result.InventorySlotHeight
			+ (InventoryRows - 1) * Result.Gap;
		Result.ContentHeight = GridHeight;
		const float PanelWidth = Padding * 2.f + EquipmentWidth + ContentGap + GridWidth;
		const float PanelHeight = Padding * 2.f + HeaderHeight + GridHeight + FooterHeight;
		const float PanelX = bReserveNearbyPanel
			? FMath::Max(10.f * Result.Scale,
				Viewport.X - 290.f * Result.Scale - PanelWidth)
			: Viewport.X * 0.5f - PanelWidth * 0.5f;
		const FVector2D PanelMin(PanelX,
			Viewport.Y * 0.5f - PanelHeight * 0.5f);
		Result.Panel = FBox2D(PanelMin, PanelMin + FVector2D(PanelWidth, PanelHeight));
		Result.EquipmentOrigin = PanelMin + FVector2D(Padding, Padding + HeaderHeight);
		Result.GridOrigin = Result.EquipmentOrigin + FVector2D(EquipmentWidth + ContentGap, 0.f);
		return Result;
	}

	FBox2D MakeInventoryContextMenuRect(
		const FVector2D Viewport, const FVector2D RequestedPosition, const float Scale,
		const float RightReservedWidth = 0.f)
	{
		const FVector2D Size(176.f * Scale, 78.f * Scale);
		const FVector2D Margin(10.f * Scale);
		const float MaximumX = FMath::Max(Margin.X,
			Viewport.X - Size.X - Margin.X - RightReservedWidth);
		const FVector2D Min(
			FMath::Clamp(RequestedPosition.X, Margin.X, MaximumX),
			FMath::Clamp(RequestedPosition.Y, Margin.Y, Viewport.Y - Size.Y - Margin.Y));
		return FBox2D(Min, Min + Size);
	}

	struct FAuraNearbyPanelLayout
	{
		float Scale = 1.f;
		float HeaderHeight = 0.f;
		float RowHeight = 0.f;
		FBox2D Panel;

		FBox2D GetRowRect(const int32 Index) const
		{
			const FVector2D Min(
				Panel.Min.X + 7.f * Scale,
				Panel.Min.Y + HeaderHeight + Index * RowHeight);
			return FBox2D(Min, FVector2D(
				Panel.Max.X - 7.f * Scale, Min.Y + RowHeight - 5.f * Scale));
		}
	};

	FAuraNearbyPanelLayout MakeNearbyPanelLayout(
		const FVector2D Viewport, const int32 ItemCount)
	{
		FAuraNearbyPanelLayout Result;
		Result.Scale = FMath::Clamp(FMath::Min(
			Viewport.X / 1280.f, Viewport.Y / 720.f), 0.75f, 2.f);
		Result.HeaderHeight = 37.f * Result.Scale;
		Result.RowHeight = 64.f * Result.Scale;
		const float Width = 270.f * Result.Scale;
		const float Height = Result.HeaderHeight
			+ FMath::Max(1, ItemCount) * Result.RowHeight + 5.f * Result.Scale;
		const FVector2D Min(
			Viewport.X - Width - 20.f * Result.Scale,
			Viewport.Y * 0.27f);
		Result.Panel = FBox2D(Min, Min + FVector2D(Width, Height));
		return Result;
	}
}

AAuraHUD::AAuraHUD()
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> FireBoltIconAsset(
		TEXT("/Game/Assets/UI/Spells/FireBolt.FireBolt"));
	if (FireBoltIconAsset.Succeeded()) FireBoltIcon = FireBoltIconAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> ArcaneShardsIconAsset(
		TEXT("/Game/Assets/UI/Spells/ArcaneShards.ArcaneShards"));
	if (ArcaneShardsIconAsset.Succeeded()) ArcaneShardsIcon = ArcaneShardsIconAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> ShockIconAsset(
		TEXT("/Game/Assets/UI/Spells/Shock.Shock"));
	if (ShockIconAsset.Succeeded()) ShockIcon = ShockIconAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> GoblinWindupAsset(
		TEXT("/Game/Free_Spells/Textures/T_Free_Spells_Flash.T_Free_Spells_Flash"));
	if (GoblinWindupAsset.Succeeded()) GoblinWindupTexture = GoblinWindupAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> GoblinStrikeAsset(
		TEXT("/Game/Free_Spells/Textures/T_Free_Spells_Star.T_Free_Spells_Star"));
	if (GoblinStrikeAsset.Succeeded()) GoblinStrikeTexture = GoblinStrikeAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> ShroomBashAsset(
		TEXT("/Game/Free_Spells/Textures/T_Free_Spells_Crack1.T_Free_Spells_Crack1"));
	if (ShroomBashAsset.Succeeded()) ShroomBashTexture = ShroomBashAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> ShroomBurstAsset(
		TEXT("/Game/Free_Spells/Textures/T_Free_Spells_Flash2.T_Free_Spells_Flash2"));
	if (ShroomBurstAsset.Succeeded()) ShroomBurstTexture = ShroomBurstAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> ArcaneTargetAsset(
		TEXT("/Game/Free_Spells/Textures/T_Free_Spells_Circle.T_Free_Spells_Circle"));
	if (ArcaneTargetAsset.Succeeded()) ArcaneTargetTexture = ArcaneTargetAsset.Object;
}

bool AAuraHUD::AreCombatCueTexturesConfigured() const
{
	return GoblinWindupTexture && GoblinStrikeTexture && ShroomBashTexture
		&& ShroomBurstTexture && ArcaneTargetTexture;
}

void AAuraHUD::ShowArcaneShardsTextureImpact(const FVector& Location,
	const float Radius, const float Duration)
{
	if (!GetWorld() || !ArcaneTargetTexture) return;
	ArcaneTextureImpactLocation = Location;
	ArcaneTextureImpactRadius = FMath::Max(1.f, Radius);
	ArcaneTextureImpactStartTime = GetWorld()->GetTimeSeconds();
	ArcaneTextureImpactEndTime = ArcaneTextureImpactStartTime
		+ FMath::Clamp(Duration, 0.1f, 2.f);
}

void AAuraHUD::DrawHUD()
{
	Super::DrawHUD();
	HighlightedMageAction = INDEX_NONE;
	HighlightedCampAction = INDEX_NONE;
	if (!Canvas || !PlayerOwner) return;

	const float UIScale = FMath::Clamp(FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 720.f), 0.75f, 2.f);
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	if (AuraController && AuraController->IsDemoCompleted())
	{
		DrawVictoryCinematic(UIScale);
		DrawVictoryOverlay(UIScale);
		return;
	}
	DrawPlayerVitals(UIScale);
	DrawSealTracker(UIScale);
	const bool bWatchingTeammate = AuraController && AuraController->IsCoopSpectating();
	if (!bWatchingTeammate)
	{
		DrawAbilityPanel(UIScale);
		DrawConsumableQuickSlots(UIScale);
		DrawRunUpgradeSummary(UIScale);
	}
	if (AuraController && AuraController->IsLanMenuOpen())
	{
		DrawLanRoomPanel(UIScale);
		return;
	}
	if (AuraController && AuraController->IsArchmageMenuOpen())
	{
		DrawArchmagePanel(UIScale);
		DrawTransientMessage(UIScale);
		return;
	}
	if (AuraController && AuraController->IsCampMenuOpen())
	{
		DrawCampServicePanel(UIScale);
		DrawTransientMessage(UIScale);
		return;
	}
	if (AuraController && AuraController->IsInventoryOpen())
	{
		if (AuraController->IsInventoryQuestPage()) DrawQuestJournal(UIScale);
		else if (AuraController->IsInventorySkillPage()) DrawSkillTree(UIScale);
		else
		{
			DrawInventoryPanel(UIScale);
			DrawNearbyEquipmentPanel(UIScale);
		}
		DrawTransientMessage(UIScale);
		return;
	}
	DrawTargetingReticle(UIScale);
	DrawArcaneShardsTargeting(UIScale);
	DrawArcaneShardsTextureImpact(UIScale);
	DrawShockTargeting(UIScale);
	DrawEnemyHealthBars(UIScale);
	DrawBossHealthBar(UIScale);
	DrawComboMarks(UIScale);
	DrawArcaneSlowFields(UIScale);
	DrawFloatingDamage(UIScale);
	if (!bWatchingTeammate) DrawObjective(UIScale);
	DrawNearbyEquipmentPanel(UIScale);
	DrawTransientMessage(UIScale);
	DrawLoreRecord(UIScale);
	DrawPlayerHitFlash(UIScale);
	DrawDefeatOverlay(UIScale);
	DrawCoopRescue(UIScale);
	DrawVictoryOverlay(UIScale);
	DrawRunUpgradeSelection(UIScale);
}

void AAuraHUD::DrawSealTracker(const float UIScale)
{
	const AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!Canvas || !Quest || Quest->IsRunCompleted()) return;
	const float Width = 282.f * UIScale;
	const float X = Canvas->ClipX - Width - 18.f * UIScale;
	const float Y = 20.f * UIScale;
	DrawRect(AuraHUDColors::Panel, X, Y, Width, 105.f * UIScale);
	DrawOutline(X, Y, Width, 105.f * UIScale,
		Quest->AreAllSealsActivated() ? FLinearColor(0.38f, 0.85f, 1.f)
			: AuraHUDColors::Border, UIScale);
	DrawText(FString::Printf(TEXT("%s封印  %d/%d  （4条路线）"),
		Quest->IsAbyssTrialActive() ? TEXT("深渊") : TEXT("地牢"),
		FMath::Min(Quest->GetActivatedSealCount(), AAuraGameState::SealsNeededForBoss),
		AAuraGameState::SealsNeededForBoss),
		AuraHUDColors::Text, X + 12.f * UIScale, Y + 10.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.89f * UIScale, false);
	const TArray<FName>& Seals = AAuraGameState::RequiredSealIds();
	for (int32 Index = 0; Index < Seals.Num(); ++Index)
	{
		const bool bBound = Quest->HasActivatedSeal(Seals[Index]);
		const float RowX = X + (12.f + Index * 66.f) * UIScale;
		DrawRect(bBound ? FLinearColor(0.14f, 0.52f, 0.75f)
			: FLinearColor(0.15f, 0.16f, 0.22f), RowX, Y + 45.f * UIScale,
			59.f * UIScale, 28.f * UIScale);
		DrawCenteredText(bBound ? TEXT("已封印") : TEXT("未封印"), AuraHUDColors::Text,
			RowX + 29.5f * UIScale, Y + 51.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.7f * UIScale);
	}
	DrawText(Quest->IsBossSummoned() ? TEXT("逆向仪式已启动，首领显形")
		: (Quest->AreAllSealsActivated() ? TEXT("首领之门已开启，前往祭坛")
			: TEXT("按 B 查看任务日志")),
		AuraHUDColors::MutedText, X + 12.f * UIScale, Y + 82.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.73f * UIScale, false);
}

void AAuraHUD::DrawQuestJournal(const float UIScale)
{
	const AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!Canvas || !Quest) return;
	const float Width = 760.f * UIScale;
	const float Height = 535.f * UIScale;
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = (Canvas->ClipY - Height) * 0.5f;
	DrawRect(AuraHUDColors::Panel, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, AuraHUDColors::Border, 2.f * UIScale);
	DrawText(TEXT("守印远征  |  四处封印任选三处"), AuraHUDColors::Text,
		X + 28.f * UIScale, Y + 23.f * UIScale,
		GEngine ? GEngine->GetMediumFont() : nullptr, 0.91f * UIScale, false);
	DrawText(TEXT("地底裂隙正在腐化守印设施；封印进度全队共享。"),
		AuraHUDColors::MutedText, X + 28.f * UIScale, Y + 65.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * UIScale, false);
	const FName SealIds[] = { TEXT("LowerNorth"), TEXT("LowerSouth"),
		TEXT("UpperSouth"), TEXT("UpperNorth") };
	const FName EncounterIds[] = { TEXT("LowerNorthBoonGuardians"),
		TEXT("LowerSouthLootAmbush"), TEXT("UpperSouthBoonGuardian"),
		TEXT("UpperNorthRiskGuardians") };
	const TCHAR* Titles[] = { TEXT("外层北侧 · 守卫密藏"),
		TEXT("深层南侧 · 伏击"), TEXT("中层南侧 · 精英守卫"),
		TEXT("深层北侧 · 高风险精英群") };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bBound = Quest->HasActivatedSeal(SealIds[Index]);
		const bool bCleared = Quest->IsRewardEncounterCleared(EncounterIds[Index]);
		const float RowY = Y + (106.f + Index * 77.f) * UIScale;
		DrawRect(AuraHUDColors::PanelInner, X + 26.f * UIScale, RowY,
			Width - 52.f * UIScale, 69.f * UIScale);
		DrawOutline(X + 26.f * UIScale, RowY, Width - 52.f * UIScale,
			69.f * UIScale, bBound ? FLinearColor(0.3f, 0.75f, 1.f)
				: AuraHUDColors::Border, UIScale);
		DrawText(Titles[Index], AuraHUDColors::Text, X + 43.f * UIScale,
			RowY + 12.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr,
			0.75f * UIScale, false);
		DrawText(bBound ? TEXT("已封印 · 全队进度已记录")
			: (bCleared ? TEXT("守卫已击败 · 在封印处按 F")
				: TEXT("击败该路线的守卫")),
			bBound ? FLinearColor(0.4f, 0.85f, 1.f) : AuraHUDColors::MutedText,
			X + 43.f * UIScale, RowY + 38.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.65f * UIScale, false);
	}
	const FString StoryBeat = Quest->IsRunCompleted()
		? TEXT("裂隙源头已被击败，守印设施暂归沉寂。")
		: (Quest->IsBossSummoned()
			? TEXT("逆向仪式让裂隙之主显形；趁现在击败它。")
			: (Quest->AreAllSealsActivated()
				? TEXT("三枚守印足以逆转仪式：到核心祭坛让首领显形。")
				: (Quest->GetActivatedSealCount() >= 2
					? TEXT("残页揭示：重新封门已无用，必须逆转仪式。")
					: (Quest->GetActivatedSealCount() == 1
						? TEXT("旧铭文说明：封印只是锁，裂隙腐化已渗出。")
						: TEXT("查明腐化源头，沿支路搜寻三枚守印。")))));
	DrawText(StoryBeat, AuraHUDColors::MutedText,
		X + 29.f * UIScale, Y + 417.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * UIScale, false);
	DrawText(Quest->IsRunCompleted() ? TEXT("裂隙之主已被击败")
		: (Quest->IsBossSummoned() ? TEXT("首领已经显形 · 击败它")
			: (Quest->AreAllSealsActivated()
				? TEXT("三处守印齐备 · 在祭坛按 F 完成逆向仪式")
				: FString::Printf(TEXT("已激活 %d/3 处封印 · 任选三条路线"),
					Quest->GetActivatedSealCount()))),
		AuraHUDColors::Text, X + 29.f * UIScale, Y + 450.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.78f * UIScale, false);
	DrawText(TEXT("Tab 查看技能  |  B / Esc 关闭"), AuraHUDColors::MutedText,
		X + 29.f * UIScale, Y + 497.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.68f * UIScale, false);
}

void AAuraHUD::DrawSkillTree(const float Scale)
{
 const AAuraPlayerState* State=PlayerOwner?PlayerOwner->GetPlayerState<AAuraPlayerState>():nullptr;
 if(!Canvas || !State) return;
 const auto& G=State->GetGrowthState();
 const float X=(Canvas->ClipX-760.f*Scale)*0.5f,Y=(Canvas->ClipY-520.f*Scale)*0.5f;
 DrawRect(AuraHUDColors::Panel,X,Y,760.f*Scale,520.f*Scale);
 const auto Text=[&](const FString& S,float Row) { DrawText(S,AuraHUDColors::Text,X+30.f*Scale,Y+Row*Scale,GEngine->GetSmallFont(),0.84f*Scale,false); };
 Text(TEXT("技能与进化 | 大魔法师处升级、选择分支及洗点"),25);
 Text(FString::Printf(TEXT("角色等级 %d/10   经验 %d   技能点 %d"),G.PaidLevel,G.TotalExperience,G.UnspentSkillPoints),65);
 Text(FString::Printf(TEXT("火球术 %d/4 | 命中附着余烬，概率点燃"),G.SkillRanks.FireBolt),115);
 Text(G.SkillRanks.FireBoltPath==EAuraFireBoltPath::Pierce?TEXT("进化：穿透"):G.SkillRanks.FireBoltPath==EAuraFireBoltPath::TripleVolley?TEXT("进化：三火球，每第五次普攻强化"):TEXT("三级进化：穿透 / 三火球"),150);
 Text(FString::Printf(TEXT("奥术晶簇 Q %d/4 | 范围伤害、减速，精英及首领抗性更高"),G.SkillRanks.ArcaneShards),210);
 Text(G.SkillRanks.ArcanePath==EAuraArcanePath::CrystalPrison?TEXT("进化：水晶囚笼，直接攻击击碎后爆炸"):G.SkillRanks.ArcanePath==EAuraArcanePath::CrystalEcho?TEXT("进化：晶簇回响，向外追加三轮递减晶簇"):TEXT("三级进化：水晶囚笼 / 晶簇回响（待选择）"),245);
 Text(FString::Printf(TEXT("闪电术 E %d/4 | 雷电伤害、间歇麻痹，可引爆自己的余烬"),G.SkillRanks.Shock),305);
 Text(G.SkillRanks.ShockPath==EAuraShockPath::StormField?TEXT("进化：雷电领域，扇形持续雷场"):G.SkillRanks.ShockPath==EAuraShockPath::ThunderJudgment?TEXT("进化：降下雷罚，地面选区随机三道落雷"):TEXT("三级进化：雷电领域 / 降下雷罚（待选择）"),340);
 Text(TEXT("固有交互：火球命中 → 雷电引爆；无需单独学习"),410);
 Text(TEXT("普通、精英及首领使用不同控制抗性，共享控制保护"),445);
 Text(TEXT("Tab 查看装备 | B / Esc 关闭"),485);
}

int32 AAuraHUD::GetHoveredLanRoomAction() const
{
	if (!Canvas || !PlayerOwner) return INDEX_NONE;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PlayerOwner->GetMousePosition(MouseX, MouseY)) return INDEX_NONE;
	const float Scale = FMath::Clamp(FMath::Min(Canvas->ClipX / 1280.f,
		Canvas->ClipY / 720.f), 0.75f, 2.f);
	const float X = (Canvas->ClipX - 640.f * Scale) * 0.5f;
	const float Y = (Canvas->ClipY - 450.f * Scale) * 0.5f;
	if (MouseY >= Y + 77.f * Scale && MouseY <= Y + 119.f * Scale)
	{
		if (MouseX >= X + 20.f * Scale && MouseX <= X + 310.f * Scale) return 0;
		if (MouseX >= X + 330.f * Scale && MouseX <= X + 620.f * Scale) return 1;
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const float RowY = Y + (145.f + Index * 47.f) * Scale;
		if (MouseX >= X + 20.f * Scale && MouseX <= X + 620.f * Scale
			&& MouseY >= RowY && MouseY <= RowY + 39.f * Scale) return Index + 2;
	}
	return INDEX_NONE;
}

void AAuraHUD::DrawLanRoomPanel(const float Scale)
{
	const UAuraLanSessionSubsystem* Lan = PlayerOwner && PlayerOwner->GetGameInstance()
		? PlayerOwner->GetGameInstance()->GetSubsystem<UAuraLanSessionSubsystem>() : nullptr;
	if (!Canvas || !Lan) return;
	const float X = (Canvas->ClipX - 640.f * Scale) * 0.5f;
	const float Y = (Canvas->ClipY - 450.f * Scale) * 0.5f;
	DrawRect(AuraHUDColors::Panel, X, Y, 640.f * Scale, 450.f * Scale);
	DrawOutline(X, Y, 640.f * Scale, 450.f * Scale, AuraHUDColors::Border, 2.f * Scale);
	DrawText(TEXT("本地联机  |  局域网房间"), AuraHUDColors::Text,
		X + 22.f * Scale, Y + 18.f * Scale,
		GEngine ? GEngine->GetMediumFont() : nullptr, 0.91f * Scale, false);
	DrawText(TEXT("最多 2 人  |  同一局域网  |  联机进度仅在本局有效"),
		AuraHUDColors::MutedText, X + 22.f * Scale, Y + 48.f * Scale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.69f * Scale, false);
	const int32 Hovered = GetHoveredLanRoomAction();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float ButtonX = X + (Index == 0 ? 20.f : 330.f) * Scale;
		DrawRect(Hovered == Index ? FLinearColor(0.11f, 0.23f, 0.32f)
			: AuraHUDColors::PanelInner, ButtonX, Y + 77.f * Scale,
			290.f * Scale, 42.f * Scale);
		DrawOutline(ButtonX, Y + 77.f * Scale, 290.f * Scale, 42.f * Scale,
			AuraHUDColors::Border, Scale);
		DrawCenteredText(Index == 0 ? TEXT("创建房间") : TEXT("刷新房间"),
			AuraHUDColors::Text, ButtonX + 145.f * Scale,
			Y + 87.f * Scale, GEngine ? GEngine->GetSmallFont() : nullptr,
			0.80f * Scale);
	}
	const TArray<FString>& Rooms = Lan->GetRoomLabels();
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const float RowY = Y + (145.f + Index * 47.f) * Scale;
		DrawRect(Hovered == Index + 2 && Rooms.IsValidIndex(Index)
			? FLinearColor(0.10f, 0.19f, 0.29f) : AuraHUDColors::PanelInner,
			X + 20.f * Scale, RowY, 600.f * Scale, 39.f * Scale);
		DrawOutline(X + 20.f * Scale, RowY, 600.f * Scale, 39.f * Scale,
			AuraHUDColors::Border, 0.65f * Scale);
		DrawText(Rooms.IsValidIndex(Index)
			? FString::Printf(TEXT("[%d] %s"), Index + 1, *Rooms[Index])
			: (Index == 0 ? TEXT("暂无房间，点击刷新重新搜索") : TEXT("")),
			Rooms.IsValidIndex(Index) ? AuraHUDColors::Text : AuraHUDColors::MutedText,
			X + 32.f * Scale, RowY + 8.f * Scale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.74f * Scale, false);
	}
	DrawText(Lan->GetStatus(), Lan->IsBusy() ? AuraHUDColors::Border : AuraHUDColors::Text,
		X + 22.f * Scale, Y + 394.f * Scale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * Scale, false);
	DrawText(TEXT("M / Esc 关闭  |  通过局域网搜索并加入房间"),
		AuraHUDColors::MutedText, X + 22.f * Scale, Y + 421.f * Scale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.66f * Scale, false);
}

void AAuraHUD::DrawInventoryPanel(const float UIScale)
{
	const AAuraPlayerState* AuraPlayerState = PlayerOwner
		? PlayerOwner->GetPlayerState<AAuraPlayerState>() : nullptr;
	const UAuraInventoryComponent* Inventory = AuraPlayerState
		? AuraPlayerState->GetInventoryComponent() : nullptr;
	const UAuraEquipmentComponent* Equipment = AuraPlayerState
		? AuraPlayerState->GetEquipmentComponent() : nullptr;
	const AAuraCharacter* Character = PlayerOwner
		? Cast<AAuraCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const UAbilitySystemComponent* AbilitySystem = Character
		? Character->GetAbilitySystemComponent() : nullptr;
	if (!Inventory || !Equipment || !Canvas) return;

	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	// Keep the backpack anchored while nearby items enter/leave the list; the
	// reserved right rail prevents the whole panel from jumping after a pickup.
	const bool bReserveNearbyPanel = true;
	const FAuraInventoryPanelLayout Layout = MakeInventoryPanelLayout(
		FVector2D(Canvas->ClipX, Canvas->ClipY), bReserveNearbyPanel);
	const FVector2D PanelSize = Layout.Panel.GetSize();
	const float PanelX = Layout.Panel.Min.X;
	const float PanelY = Layout.Panel.Min.Y;
	const int32 SelectedSlot = AuraController
		? AuraController->GetSelectedInventorySlot() : INDEX_NONE;
	const bool bDraggingItem = AuraController && AuraController->IsInventoryDragging();
	const int32 DragSourceSlot = bDraggingItem
		? AuraController->GetInventoryDragSourceSlot() : INDEX_NONE;
	const int32 DragHoverSlot = bDraggingItem
		? AuraController->GetInventoryDragHoverSlot() : INDEX_NONE;
	const FAuraInventoryEntry* DragEntry = Inventory->FindEntryAtSlot(DragSourceSlot);
	const FAuraInventoryEntry* SelectedEntry = Inventory->FindEntryAtSlot(SelectedSlot);
	const FAuraItemInstance* InspectedItem = SelectedEntry
		&& SelectedEntry->Item.Definition ? &SelectedEntry->Item : nullptr;

	DrawRect(FLinearColor(0.006f, 0.009f, 0.018f, 0.94f),
		PanelX, PanelY, PanelSize.X, PanelSize.Y);
	DrawOutline(PanelX, PanelY, PanelSize.X, PanelSize.Y,
		AuraHUDColors::Border, 2.f * UIScale);
	DrawCenteredText(TEXT("装备与背包"), AuraHUDColors::Text,
		Layout.Panel.GetCenter().X, PanelY + 16.f * UIScale,
		GEngine ? GEngine->GetMediumFont() : nullptr, 1.1f * UIScale);
	DrawCenteredText(FString::Printf(TEXT("格子 %d / %d"),
		Inventory->GetOccupiedSlotCount(), Inventory->GetCapacity()),
		AuraHUDColors::MutedText, Layout.Panel.GetCenter().X,
		PanelY + 49.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * UIScale);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const EAuraEquipmentSlot Slot = EquipmentSlotAtIndex(Index);
		const FBox2D Rect = Layout.GetEquipmentSlotRect(Slot);
		const FAuraItemInstance* Item = Equipment->GetEquippedItem(Slot);
		const FLinearColor Accent = Item
			? AuraItems::GetRarityColor(Item->Rarity)
			: FLinearColor(0.22f, 0.25f, 0.34f, 0.95f);
		DrawRect(Item ? AuraHUDColors::PanelInner
			: FLinearColor(0.018f, 0.023f, 0.036f, 0.9f),
			Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);
		DrawOutline(Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y,
			Accent, (Item ? 2.f : 1.f) * UIScale);
		const UAuraItemDefinition* DisplayDefinition = Item && Item->Definition
			? Item->Definition.Get() : DefinitionForEquipmentSlot(Slot);
		UTexture2D* Icon = DisplayDefinition
			? DisplayDefinition->Icon.LoadSynchronous() : nullptr;
		DrawItemIcon(Icon, Rect.Min.X + 12.f * UIScale, Rect.Min.Y + 7.f * UIScale,
			Rect.GetSize().X - 24.f * UIScale, Rect.GetSize().Y - 14.f * UIScale,
			Item ? FLinearColor::White : FLinearColor(0.22f, 0.25f, 0.34f, 0.3f));
	}

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
	const float DetailX = Layout.EquipmentOrigin.X;
	const float DetailY = Layout.EquipmentOrigin.Y
		+ (Layout.EquipmentSlotHeight * 2.f + Layout.Gap) + 8.f * UIScale;
	const float DetailWidth = Layout.EquipmentSlotWidth * 2.f + Layout.Gap;
	const float DetailHeight = Layout.EquipmentOrigin.Y + Layout.ContentHeight - DetailY;
	DrawRect(FLinearColor(0.018f, 0.023f, 0.038f, 0.96f),
		DetailX, DetailY, DetailWidth, DetailHeight);
	DrawOutline(DetailX, DetailY, DetailWidth, DetailHeight,
		InspectedItem ? AuraItems::GetRarityColor(InspectedItem->Rarity)
			: FLinearColor(0.22f, 0.25f, 0.34f, 0.95f), UIScale);

	if (InspectedItem && InspectedItem->Definition)
	{
		const UAuraItemDefinition* Definition = InspectedItem->Definition;
		const FLinearColor RarityColor = AuraItems::GetRarityColor(InspectedItem->Rarity);
		DrawText(Definition->DisplayName.ToString().ToUpper(), AuraHUDColors::Text,
			DetailX + 9.f * UIScale, DetailY + 7.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.64f * UIScale, false);
		DrawText(FString::Printf(TEXT("%s   等级 %d   +%d   •   %s"),
			AuraItems::GetRarityName(InspectedItem->Rarity), InspectedItem->ItemLevel,
			InspectedItem->EnhancementLevel,
			Definition->ItemType == EAuraItemType::Equipment
				? AuraItems::GetEquipmentSlotName(Definition->EquipmentSlot)
				: (Definition->ItemType == EAuraItemType::Material ? TEXT("材料")
					: TEXT("消耗品"))),
			RarityColor, DetailX + 9.f * UIScale, DetailY + 26.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.49f * UIScale, false);
		DrawItemIcon(Definition->Icon.LoadSynchronous(),
			DetailX + 9.f * UIScale, DetailY + 47.f * UIScale,
			78.f * UIScale, 75.f * UIScale);

		const float PropertyX = DetailX + 98.f * UIScale;
		float PropertyY = DetailY + 45.f * UIScale;
		if (Definition->ItemType == EAuraItemType::Equipment)
		{
			const TArray<FAuraItemStatModifier> FinalModifiers =
				AuraItems::BuildFinalStatModifiers(*InspectedItem);
			const FAuraItemInstance* Equipped =
				Equipment->GetEquippedItem(Definition->EquipmentSlot);
			for (int32 Index = 0; Index < FinalModifiers.Num() && Index < 6; ++Index)
			{
				const FAuraItemStatModifier& Modifier = FinalModifiers[Index];
				const float EquippedMagnitude = Equipped
					? AuraItems::GetStatMagnitude(
						AuraItems::BuildFinalStatModifiers(*Equipped), Modifier.StatTag) : 0.f;
				const float Delta = Modifier.Magnitude - EquippedMagnitude;
				const FString Comparison = Equipped && !FMath::IsNearlyZero(Delta, 0.05f)
					? FString::Printf(TEXT("   (%+.0f)"), Delta) : FString();
				DrawText(FString::Printf(TEXT("+%.0f  %s%s"), Modifier.Magnitude,
					AuraItems::GetStatName(Modifier.StatTag), *Comparison),
					Delta < -0.05f ? FLinearColor(1.f, 0.38f, 0.25f) : AuraHUDColors::Text,
					PropertyX, PropertyY, GEngine ? GEngine->GetSmallFont() : nullptr,
					0.51f * UIScale, false);
				PropertyY += 17.f * UIScale;
			}
		}
		else if (Definition->ItemType == EAuraItemType::Material)
		{
			DrawText(TEXT("锻造材料"), AuraHUDColors::Text,
				PropertyX, PropertyY, GEngine ? GEngine->GetSmallFont() : nullptr,
				0.53f * UIScale, false);
		}
		else if (Definition->ConsumableEffect == EAuraConsumableEffect::Respec)
		{
			DrawText(TEXT("在大魔法师处使用"), AuraHUDColors::Text,
				PropertyX, PropertyY, GEngine ? GEngine->GetSmallFont() : nullptr,
				0.53f * UIScale, false);
		}
		else
		{
			DrawText(FString::Printf(TEXT("恢复  +%.0f  %s"), Definition->RestoreAmount,
				Definition->ConsumableEffect == EAuraConsumableEffect::Health
					? TEXT("生命") : TEXT("法力")), AuraHUDColors::Text,
				PropertyX, PropertyY, GEngine ? GEngine->GetSmallFont() : nullptr,
				0.53f * UIScale, false);
		}
		const FString SourceDetail = FString::Printf(TEXT("来源：%s"),
			AuraItems::GetSourceName(InspectedItem->SourceType));
		DrawText(SourceDetail, AuraHUDColors::MutedText,
			DetailX + 9.f * UIScale, DetailY + DetailHeight - 19.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.45f * UIScale, false);
	}
	else
	{
		DrawText(TEXT("当前角色属性"), AuraHUDColors::Border,
			DetailX + 9.f * UIScale, DetailY + 9.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.6f * UIScale, false);
		for (int32 Index = 0; Index < StatTags.Num(); ++Index)
		{
			const float Current = AbilitySystem
				? AbilitySystem->GetNumericAttribute(Attributes[Index]) : 0.f;
			DrawText(FString::Printf(TEXT("%s   %.0f"),
				AuraItems::GetStatName(StatTags[Index]), Current), AuraHUDColors::Text,
				DetailX + 12.f * UIScale,
				DetailY + (34.f + Index * 21.f) * UIScale,
				GEngine ? GEngine->GetSmallFont() : nullptr, 0.57f * UIScale, false);
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < Inventory->GetCapacity(); ++SlotIndex)
	{
		if (SlotIndex / InventoryColumns >= InventoryRows) break;
		const FBox2D Rect = Layout.GetInventorySlotRect(SlotIndex);
		const float X = Rect.Min.X;
		const float Y = Rect.Min.Y;
		const float SlotWidth = Rect.GetSize().X;
		const float SlotHeight = Rect.GetSize().Y;
		const FAuraInventoryEntry* Entry = Inventory->FindEntryAtSlot(SlotIndex);
		FLinearColor BorderColor = Entry
			? AuraItems::GetRarityColor(Entry->Item.Rarity)
			: FLinearColor(0.19f, 0.22f, 0.3f, 0.92f);
		if (bDraggingItem && SlotIndex == DragHoverSlot && SlotIndex != DragSourceSlot)
		{
			BorderColor = DragEntry && Entry
				&& DragEntry->Item.CanStackWith(Entry->Item)
				? FLinearColor(0.19f, 0.9f, 0.45f)
				: FLinearColor(0.28f, 0.7f, 1.f);
		}
		DrawRect(Entry ? FLinearColor(0.045f, 0.055f, 0.085f, 0.98f)
			: FLinearColor(0.018f, 0.023f, 0.036f, 0.9f),
			X, Y, SlotWidth, SlotHeight);
		DrawOutline(X, Y, SlotWidth, SlotHeight, BorderColor,
			(SlotIndex == DragHoverSlot && bDraggingItem ? 4.f
				: (SlotIndex == SelectedSlot ? 4.f : (Entry ? 2.f : 1.f))) * UIScale);
		if (!Entry || !Entry->Item.Definition) continue;
		DrawItemIcon(Entry->Item.Definition->Icon.LoadSynchronous(),
			X + 10.f * UIScale, Y + 7.f * UIScale,
			SlotWidth - 20.f * UIScale, SlotHeight - 14.f * UIScale);
		if (bDraggingItem && SlotIndex == DragSourceSlot)
		{
			DrawRect(FLinearColor(0.012f, 0.018f, 0.035f, 0.64f),
				X, Y, SlotWidth, SlotHeight);
		}
		if (Entry->Item.Quantity > 1)
		{
			DrawCenteredText(FString::Printf(TEXT("x%d"), Entry->Item.Quantity),
				AuraHUDColors::Text, X + SlotWidth - 17.f * UIScale,
				Y + 53.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.6f * UIScale);
		}
	}

	DrawCenteredText(TEXT("左键查看 / 按住拖动   右键打开菜单   Delete 丢弃   Tab 切换任务 / 技能   B / Esc 关闭"),
		AuraHUDColors::MutedText, Layout.Panel.GetCenter().X,
		Layout.Panel.Max.Y - 31.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.64f * UIScale);
	if (bDraggingItem && DragEntry && DragEntry->Item.Definition)
	{
		float MouseX = 0.f;
		float MouseY = 0.f;
		if (PlayerOwner->GetMousePosition(MouseX, MouseY))
		{
			const float GhostSize = 62.f * UIScale;
			const float GhostX = MouseX - GhostSize * 0.5f;
			const float GhostY = MouseY - GhostSize * 0.5f;
			DrawRect(FLinearColor(0.02f, 0.03f, 0.055f, 0.82f),
				GhostX, GhostY, GhostSize, GhostSize);
			DrawOutline(GhostX, GhostY, GhostSize, GhostSize,
				AuraItems::GetRarityColor(DragEntry->Item.Rarity), 2.f * UIScale);
			DrawItemIcon(DragEntry->Item.Definition->Icon.LoadSynchronous(),
				GhostX + 5.f * UIScale, GhostY + 5.f * UIScale,
				GhostSize - 10.f * UIScale, GhostSize - 10.f * UIScale);
		}
	}

	if (AuraController && AuraController->GetInventoryContextSlot() != INDEX_NONE
		&& Inventory->FindEntryAtSlot(AuraController->GetInventoryContextSlot()))
	{
		const FBox2D Menu = MakeInventoryContextMenuRect(
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			AuraController->GetInventoryContextMenuPosition(), UIScale,
			290.f * UIScale);
		const int32 HoveredAction = GetHoveredInventoryContextAction();
		DrawRect(FLinearColor(0.008f, 0.012f, 0.024f, 0.985f),
			Menu.Min.X, Menu.Min.Y, Menu.GetSize().X, Menu.GetSize().Y);
		DrawOutline(Menu.Min.X, Menu.Min.Y, Menu.GetSize().X, Menu.GetSize().Y,
			AuraHUDColors::Border, 1.5f * UIScale);
		const float ActionHeight = Menu.GetSize().Y * 0.5f;
		for (int32 Action = 0; Action < 2; ++Action)
		{
			const float ActionY = Menu.Min.Y + Action * ActionHeight;
			DrawRect(HoveredAction == Action
				? FLinearColor(0.14f, 0.18f, 0.29f, 0.98f)
				: FLinearColor(0.035f, 0.045f, 0.075f, 0.98f),
				Menu.Min.X + 3.f * UIScale, ActionY + 3.f * UIScale,
				Menu.GetSize().X - 6.f * UIScale, ActionHeight - 5.f * UIScale);
			DrawCenteredText(Action == 0 ? TEXT("装备") : TEXT("丢弃"),
				Action == 0 ? AuraHUDColors::Text : FLinearColor(1.f, 0.47f, 0.3f),
				Menu.GetCenter().X, ActionY + 10.f * UIScale,
				GEngine ? GEngine->GetSmallFont() : nullptr, 0.62f * UIScale);
		}
	}
}

int32 AAuraHUD::GetHoveredInventorySlot() const
{
	if (!PlayerOwner) return INDEX_NONE;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PlayerOwner->GetMousePosition(MouseX, MouseY)) return INDEX_NONE;
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerOwner->GetViewportSize(ViewportX, ViewportY);
	const FAuraInventoryPanelLayout Layout = MakeInventoryPanelLayout(
		FVector2D(ViewportX, ViewportY), true);
	const FVector2D Mouse(MouseX, MouseY);
	for (int32 SlotIndex = 0; SlotIndex < InventoryColumns * InventoryRows; ++SlotIndex)
	{
		if (Layout.GetInventorySlotRect(SlotIndex).IsInside(Mouse)) return SlotIndex;
	}
	return INDEX_NONE;
}

EAuraEquipmentSlot AAuraHUD::GetHoveredEquipmentSlot() const
{
	if (!PlayerOwner) return EAuraEquipmentSlot::None;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PlayerOwner->GetMousePosition(MouseX, MouseY)) return EAuraEquipmentSlot::None;
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerOwner->GetViewportSize(ViewportX, ViewportY);
	const FAuraInventoryPanelLayout Layout = MakeInventoryPanelLayout(
		FVector2D(ViewportX, ViewportY), true);
	const FVector2D Mouse(MouseX, MouseY);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const EAuraEquipmentSlot Slot = EquipmentSlotAtIndex(Index);
		if (Layout.GetEquipmentSlotRect(Slot).IsInside(Mouse)) return Slot;
	}
	return EAuraEquipmentSlot::None;
}

int32 AAuraHUD::GetHoveredInventoryContextAction() const
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	if (!AuraController || !AuraController->IsInventoryOpen()
		|| AuraController->GetInventoryContextSlot() == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PlayerOwner->GetMousePosition(MouseX, MouseY)) return INDEX_NONE;
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerOwner->GetViewportSize(ViewportX, ViewportY);
	const bool bReserveNearbyPanel = true;
	const FAuraInventoryPanelLayout Layout = MakeInventoryPanelLayout(
		FVector2D(ViewportX, ViewportY), bReserveNearbyPanel);
	const FBox2D Menu = MakeInventoryContextMenuRect(
		FVector2D(ViewportX, ViewportY),
		AuraController->GetInventoryContextMenuPosition(), Layout.Scale,
		290.f * Layout.Scale);
	if (!Menu.IsInside(FVector2D(MouseX, MouseY))) return INDEX_NONE;
	return MouseY < Menu.GetCenter().Y ? 0 : 1;
}

AAuraWorldItem* AAuraHUD::GetHoveredNearbyWorldItem() const
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	if (!AuraController) return nullptr;
	const TArray<TWeakObjectPtr<AAuraWorldItem>>& Items =
		AuraController->GetNearbyWorldItems();
	if (Items.IsEmpty()) return nullptr;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PlayerOwner->GetMousePosition(MouseX, MouseY)) return nullptr;
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerOwner->GetViewportSize(ViewportX, ViewportY);
	const FAuraNearbyPanelLayout Layout = MakeNearbyPanelLayout(
		FVector2D(ViewportX, ViewportY), Items.Num());
	const FVector2D Mouse(MouseX, MouseY);
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		if (Layout.GetRowRect(Index).IsInside(Mouse)) return Items[Index].Get();
	}
	return nullptr;
}

void AAuraHUD::DrawNearbyEquipmentPanel(const float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	if (!AuraController || !Canvas) return;
	const TArray<TWeakObjectPtr<AAuraWorldItem>>& Items =
		AuraController->GetNearbyWorldItems();
	if (Items.IsEmpty()) return;
	const FAuraNearbyPanelLayout Layout = MakeNearbyPanelLayout(
		FVector2D(Canvas->ClipX, Canvas->ClipY), Items.Num());
	DrawRect(FLinearColor(0.006f, 0.01f, 0.02f, 0.9f),
		Layout.Panel.Min.X, Layout.Panel.Min.Y,
		Layout.Panel.GetSize().X, Layout.Panel.GetSize().Y);
	DrawOutline(Layout.Panel.Min.X, Layout.Panel.Min.Y,
		Layout.Panel.GetSize().X, Layout.Panel.GetSize().Y,
		FLinearColor(0.45f, 0.5f, 0.62f, 0.9f), UIScale);
	DrawText(TEXT("附近物品"), AuraHUDColors::Text,
		Layout.Panel.Min.X + 10.f * UIScale,
		Layout.Panel.Min.Y + 9.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.61f * UIScale, false);

	AAuraWorldItem* HoveredItem = GetHoveredNearbyWorldItem();
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		AAuraWorldItem* WorldItem = Items[Index].Get();
		if (!WorldItem || !WorldItem->GetItemInstance().Definition) continue;
		const FAuraItemInstance& Item = WorldItem->GetItemInstance();
		const UAuraItemDefinition* Definition = Item.Definition;
		const FBox2D Row = Layout.GetRowRect(Index);
		const FLinearColor RarityColor = AuraItems::GetRarityColor(Item.Rarity);
		const bool bHighlighted = AuraController->GetHighlightedWorldItem() == WorldItem;
		const bool bHovered = HoveredItem == WorldItem;
		DrawRect(bHighlighted || bHovered
			? FLinearColor(0.09f, 0.12f, 0.2f, 0.98f)
			: FLinearColor(0.028f, 0.037f, 0.062f, 0.96f),
			Row.Min.X, Row.Min.Y, Row.GetSize().X, Row.GetSize().Y);
		DrawOutline(Row.Min.X, Row.Min.Y, Row.GetSize().X, Row.GetSize().Y,
			RarityColor, (bHighlighted || bHovered ? 2.5f : 1.f) * UIScale);
		DrawItemIcon(Definition->Icon.LoadSynchronous(),
			Row.Min.X + 6.f * UIScale, Row.Min.Y + 5.f * UIScale,
			48.f * UIScale, Row.GetSize().Y - 10.f * UIScale);
		DrawText(Definition->DisplayName.ToString().ToUpper(), AuraHUDColors::Text,
			Row.Min.X + 60.f * UIScale, Row.Min.Y + 8.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.54f * UIScale, false);
		DrawText(FString::Printf(TEXT("%s  •  等级 %d"),
			AuraItems::GetRarityName(Item.Rarity), Item.ItemLevel), RarityColor,
			Row.Min.X + 60.f * UIScale, Row.Min.Y + 31.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.45f * UIScale, false);
		if (bHighlighted)
		{
			DrawCenteredText(TEXT("F"), FLinearColor(0.02f, 0.025f, 0.04f, 1.f),
				Row.Max.X - 17.f * UIScale, Row.Min.Y + 15.f * UIScale,
				GEngine ? GEngine->GetSmallFont() : nullptr, 0.56f * UIScale);
			DrawOutline(Row.Max.X - 29.f * UIScale, Row.Min.Y + 8.f * UIScale,
				24.f * UIScale, 27.f * UIScale, RarityColor, 2.f * UIScale);
		}
	}
}

void AAuraHUD::ShowPlayerHit(float Damage)
{
	if (!GetWorld()) return;
	PlayerHitFlashEndTime = GetWorld()->GetTimeSeconds() + 0.28f;
	ShowStatusMessage(FString::Printf(TEXT("生命 -%.0f"), Damage), FLinearColor(1.f, 0.18f, 0.12f));
}

void AAuraHUD::ShowDamageNumber(AActor* TargetActor, float Damage)
{
	if (!TargetActor || !GetWorld()) return;

	FAuraFloatingDamage& Entry = FloatingDamageNumbers.AddDefaulted_GetRef();
	Entry.WorldLocation = TargetActor->GetActorLocation() + FVector(0.f, 0.f, 135.f);
	Entry.Amount = Damage;
	Entry.SpawnTime = GetWorld()->GetTimeSeconds();
	HitMarkerEndTime = Entry.SpawnTime + 0.18f;
}

void AAuraHUD::ShowStatusMessage(
	const FString& Message, const FLinearColor& Color, const float Duration)
{
	if (!GetWorld()) return;
	StatusMessage = Message;
	StatusMessageColor = Color;
	StatusMessageEndTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, Duration);
}

void AAuraHUD::ShowLoreRecord(const FName RecordId, const FString& Title,
	const FString& LineOne, const FString& LineTwo, const FString& LineThree,
	const FLinearColor& Accent)
{
	if (!GetWorld() || RecordId.IsNone()) return;
	VisibleLoreRecordId = RecordId;
	LoreTitle = Title;
	LoreLines[0] = LineOne;
	LoreLines[1] = LineTwo;
	LoreLines[2] = LineThree;
	LoreAccent = Accent;
	LoreEndTime = GetWorld()->GetTimeSeconds() + 12.f;
}

void AAuraHUD::HideLoreRecord()
{
	VisibleLoreRecordId = NAME_None;
	LoreEndTime = 0.f;
}

bool AAuraHUD::IsLoreRecordVisible() const
{
	return GetWorld() && !VisibleLoreRecordId.IsNone()
		&& GetWorld()->GetTimeSeconds() < LoreEndTime;
}

FName AAuraHUD::GetVisibleLoreRecordId() const
{
	return IsLoreRecordVisible() ? VisibleLoreRecordId : NAME_None;
}

void AAuraHUD::SetObjective(const FString& NewObjective)
{
	ObjectiveText = NewObjective;
}

int32 AAuraHUD::GetHoveredMageAction() const
{
 const AAuraPlayerController* Controller=Cast<AAuraPlayerController>(PlayerOwner);
 if(!Canvas || !Controller) return INDEX_NONE;
 float MX,MY;
 if(!PlayerOwner->GetMousePosition(MX,MY)) return INDEX_NONE;
 const float Scale=FMath::Clamp(FMath::Min(Canvas->ClipX/1280.f,Canvas->ClipY/720.f),0.75f,2.f);
 const float X=(MX-(Canvas->ClipX-570.f*Scale)*0.5f)/Scale,Y=(MY-(Canvas->ClipY-430.f*Scale)*0.5f)/Scale;
 if(X<16.f || X>554.f) return INDEX_NONE;
 if(X>=505.f && Y>=16.f && Y<=49.f) return 9;
 const int32 P=Controller->GetArchmagePendingAction();
 if(P==0)
 { for(int32 I=0;I<4;++I) if(Y>=122.f+I*46.f && Y<=160.f+I*46.f) return I+2; }
 else
 {
  if(Y>=349.f && Y<=390.f) { if(X<=264.f) return 6; if(X>=306.f) return 7; }
  if(P>=11 && P<=13)
   for(int32 I=0;I<2;++I) if(Y>=167.f+I*43.f && Y<=204.f+I*43.f) return I+2;
  if(P==5)
  {
   if(X>=430.f && Y>=295.f && Y<=333.f) return 8;
   for(int32 I=0;I<3;++I)
   {
    if(Y<167.f+I*43.f || Y>204.f+I*43.f) continue;
    if(Controller->GetMageDraftRank(static_cast<EAuraPlayerSkill>(I))>=3)
    { if(X>=380.f) return 12+I*2; if(X>=221.f) return 11+I*2; }
    return I+2;
   }
  }
 }
 return INDEX_NONE;
}

void AAuraHUD::DrawArchmagePanel(const float Scale)
{
 const AAuraPlayerController* Controller=Cast<AAuraPlayerController>(PlayerOwner);
 const AAuraPlayerState* State=PlayerOwner?PlayerOwner->GetPlayerState<AAuraPlayerState>():nullptr;
 if(!Canvas || !Controller || !State) return;
 const auto& G=State->GetGrowthState();
 const float Left=(Canvas->ClipX-570.f*Scale)*0.5f,Top=(Canvas->ClipY-430.f*Scale)*0.5f;
 DrawRect(AuraHUDColors::Panel,Left,Top,570.f*Scale,430.f*Scale);
 DrawOutline(Left,Top,570.f*Scale,430.f*Scale,AuraHUDColors::Border,Scale);
 const auto Text=[&](const FString& S,float X,float Y,float Size=0.8f)
 { DrawText(S,AuraHUDColors::Text,Left+X*Scale,Top+Y*Scale,GEngine->GetSmallFont(),Size*Scale,false); };
 HighlightedMageAction=GetHoveredMageAction();
 const auto Button=[&](int32 Action,const FString& S,float X,float Y,float W,bool Chosen=false)
 {
  DrawRect(Chosen?FLinearColor(0.1f,0.4f,0.3f):HighlightedMageAction==Action?FLinearColor(0.15f,0.28f,0.4f):AuraHUDColors::PanelInner,
   Left+X*Scale,Top+Y*Scale,W*Scale,37.f*Scale);
  Text(S,X+10.f,Y+8.f,0.76f);
 };
 DrawCenteredText(TEXT("大魔法师 | 技能升级与进化"),AuraHUDColors::Border,Canvas->ClipX*0.5f,Top+20.f*Scale,GEngine->GetMediumFont(),0.9f*Scale);
 Button(9,TEXT("关闭"),505,16,48);
 Text(FString::Printf(TEXT("等级 %d  灵魂 %d  可用技能点 %d"),G.PaidLevel,G.Souls,G.UnspentSkillPoints),24,64);
 Text(FString::Printf(TEXT("火球 %d/4   Q %d/4   E %d/4"),G.SkillRanks.FireBolt,G.SkillRanks.ArcaneShards,G.SkillRanks.Shock),24,92);
 const int32 P=Controller->GetArchmagePendingAction();
 if(P==0)
 {
  const FString Rows[]={TEXT("升级火球术"),G.SkillRanks.bArcaneEvolutionPending?TEXT("Q：免费补选三级进化"):TEXT("学习 / 升级奥术晶簇（Q）"),
   G.SkillRanks.bShockEvolutionPending?TEXT("E：免费补选三级进化"):TEXT("学习 / 升级闪电术（E）"),TEXT("洗点：重新分配技能与进化")};
  for(int32 I=0;I<4;++I) Button(I+2,Rows[I],16,122.f+46.f*I,538);
  Text(TEXT("每级获得技能点；技能三级选择进化"),24,326,0.75f);
  Text(TEXT("固有互动：火球附着余烬，雷电命中引爆"),24,357,0.73f);
  return;
 }
 Text(P==5?TEXT("洗点：点击技能加点，为三级技能选择分支"):TEXT("确认升级 / 选择技能进化"),24,132);
 if(P==5)
 {
  const FString Names[]={TEXT("火球"),TEXT("奥术晶簇"),TEXT("闪电术")};
  const FString A[]={TEXT("穿透"),TEXT("水晶囚笼"),TEXT("雷电领域")},B[]={TEXT("三火球"),TEXT("晶簇回响"),TEXT("降下雷罚")};
  const int32 Choices[]={static_cast<int32>(Controller->GetMageDraftFirePath()),static_cast<int32>(Controller->GetMageDraftArcanePath()),static_cast<int32>(Controller->GetMageDraftShockPath())};
  for(int32 I=0;I<3;++I)
  {
   const int32 Rank=Controller->GetMageDraftRank(static_cast<EAuraPlayerSkill>(I));
   const float Y=167.f+43.f*I;
   Button(I+2,FString::Printf(TEXT("%s %d +"),*Names[I],Rank),16,Y,200);
   if(Rank>=3) { Button(11+2*I,A[I],221,Y,156,Choices[I]==1); Button(12+2*I,B[I],380,Y,174,Choices[I]==2); }
  }
  Text(FString::Printf(TEXT("剩余点数 %d · L6 首次免费，否则消耗洗点石"),Controller->GetMageDraftPoints()),24,307,0.66f);
  Button(8,TEXT("重置加点"),430,295,118);
 }
 else if(P>=11 && P<=13)
 {
  const FString Options[][2]={{TEXT("穿透：火球额外穿过 1 名敌人"),TEXT("三火球：第五次普攻依次发射三颗火球")},
   {TEXT("水晶囚笼：冻结目标，击碎晶壳爆炸"),TEXT("晶簇回响：向四周扩散三轮递减晶簇")},
   {TEXT("雷电领域：扇形攻击，留下持续雷场"),TEXT("降下雷罚：选定区域，随机降下三道雷罚")}};
  const int32 Choice=P==11?static_cast<int32>(Controller->GetMageDraftFirePath()):P==12?static_cast<int32>(Controller->GetMageDraftArcanePath()):static_cast<int32>(Controller->GetMageDraftShockPath());
  for(int32 I=0;I<2;++I) Button(I+2,Options[P-11][I],16,167.f+43.f*I,538,Choice==I+1);
  Text(TEXT("两条路线互斥；旧档待选进化不扣技能点"),24,302,0.72f);
 }
 else
 {
  Text(P==2?TEXT("火球：伤害成长、锁定追踪、概率点燃"):P==3?TEXT("Q：伤害与减速；三级选择水晶或回响"):TEXT("E：伤害与间歇麻痹；三级选择雷场或雷罚"),24,205,0.78f);
 }
 Button(6,TEXT("确认"),22,349,242);
 Button(7,TEXT("取消"),306,349,242);
}

int32 AAuraHUD::GetHoveredCampAction() const
{
	const AAuraPlayerController* Controller = Cast<AAuraPlayerController>(PlayerOwner);
	if (!Canvas || !Controller || !Controller->IsCampMenuOpen()) return 0;
	float MouseX, MouseY;
	if (!Controller->GetMousePosition(MouseX, MouseY)) return 0;
	const float Scale = FMath::Clamp(FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 720.f), 0.75f, 2.f);
	const float X = (MouseX - Canvas->ClipX * 0.5f) / Scale + 430.f;
	const float Y = (MouseY - Canvas->ClipY * 0.5f) / Scale + 290.f;
	const auto Inside = [X, Y](float Left, float Top, float W, float H)
	{ return X >= Left && X <= Left + W && Y >= Top && Y <= Top + H; };
	if (Inside(808, 16, 38, 33)) return 15;
	const AAuraCampVendor* Vendor = Controller->GetActiveCampVendor();
	if (Vendor && Vendor->GetServiceType() == EAuraCampService::Blacksmith)
	{
		const int32 Actions[] = {11,12,16};
		for (int32 Tab = 0; Tab < 3; ++Tab) if (Inside(540 + Tab * 88, 16, 80, 33)) return Actions[Tab];
	}
	else
	{
		if (Inside(540,16,128,33)) return 11;
		if (Inside(676,16,128,33)) return 12;
	}
	const int32 Count = Controller->BuildCampServiceRows().Num();
	for (int32 Row = 0; Row < 8; ++Row)
		if (Controller->GetCampPage() * 8 + Row < Count && Inside(18,100 + Row * 45,504,38)) return Row + 1;
	if (Inside(24,468,106,38)) return 13;
	if (Inside(414,468,106,38)) return 14;
	if (Controller->IsCampConfirming())
	{
		if (Inside(24,514,236,41)) return 9;
		if (Inside(280,514,240,41)) return 10;
	}
	return 0;
}

void AAuraHUD::DrawCampServicePanel(const float Scale)
{
	const AAuraPlayerController* Controller = Cast<AAuraPlayerController>(PlayerOwner);
	const AAuraPlayerState* State = PlayerOwner
		? PlayerOwner->GetPlayerState<AAuraPlayerState>() : nullptr;
	const AAuraCampVendor* Vendor = Controller ? Controller->GetActiveCampVendor() : nullptr;
	if (!Canvas || !Controller || !State || !Vendor) return;
	const bool bSmith = Vendor->GetServiceType() == EAuraCampService::Blacksmith;
	const float Left = Canvas->ClipX * 0.5f - 430.f * Scale;
	const float Top = Canvas->ClipY * 0.5f - 290.f * Scale;
	const FLinearColor Accent = bSmith ? FLinearColor(1.f, 0.37f, 0.16f)
		: FLinearColor(1.f, 0.72f, 0.28f);
	DrawRect(AuraHUDColors::Panel, Left, Top, 860.f * Scale, 580.f * Scale);
	DrawOutline(Left, Top, 860.f * Scale, 580.f * Scale, Accent, Scale);
	DrawText(bSmith ? (Controller->GetSmithMode() == 0 ? TEXT("锻造大师  |  装备强化")
		: (Controller->GetSmithMode() == 1 ? TEXT("锻造大师  |  随机打造")
			: TEXT("锻造大师  |  装备熔炼")))
		: (Controller->IsCampSelling() ? TEXT("商人  |  出售") : TEXT("商人  |  购买")),
		Accent, Left + 22.f * Scale, Top + 17.f * Scale,
		GEngine ? GEngine->GetMediumFont() : nullptr, 0.84f * Scale, false);
	HighlightedCampAction = GetHoveredCampAction();
	const int32 Hovered = HighlightedCampAction;
	if (bSmith)
	{
		const TCHAR* TabNames[3] = { TEXT("强化"), TEXT("打造"), TEXT("熔炼") };
		const int32 TabActions[3] = { 11, 12, 16 };
		for (int32 Tab = 0; Tab < 3; ++Tab)
		{
			const float ButtonX = Left + (540.f + Tab * 88.f) * Scale;
			const bool bActive = Controller->GetSmithMode() == Tab;
			DrawRect(Hovered == TabActions[Tab] ? FLinearColor(0.26f, 0.3f, 0.38f)
				: (bActive ? FLinearColor(0.25f, 0.2f, 0.12f) : AuraHUDColors::PanelInner),
				ButtonX, Top + 16.f * Scale, 80.f * Scale, 33.f * Scale);
			DrawOutline(ButtonX, Top + 16.f * Scale, 80.f * Scale, 33.f * Scale,
				bActive ? Accent : AuraHUDColors::MutedText, Scale);
			DrawCenteredText(TabNames[Tab], AuraHUDColors::Text,
				ButtonX + 40.f * Scale, Top + 22.f * Scale,
				GEngine ? GEngine->GetSmallFont() : nullptr, 0.75f * Scale);
		}
	}
	else
	{
		for (int32 Tab = 0; Tab < 2; ++Tab)
		{
			const bool bActive = Controller->IsCampSelling() == (Tab == 1);
			const float ButtonX = Left + (Tab == 0 ? 540.f : 676.f) * Scale;
			DrawRect(Hovered == 11 + Tab ? FLinearColor(0.26f, 0.3f, 0.38f)
				: (bActive ? FLinearColor(0.25f, 0.2f, 0.12f)
					: AuraHUDColors::PanelInner), ButtonX, Top + 16.f * Scale,
				128.f * Scale, 33.f * Scale);
			DrawOutline(ButtonX, Top + 16.f * Scale, 128.f * Scale,
				33.f * Scale, bActive ? Accent : AuraHUDColors::MutedText, Scale);
			DrawCenteredText(Tab == 0 ? TEXT("购买") : TEXT("出售"),
				AuraHUDColors::Text, ButtonX + 64.f * Scale, Top + 22.f * Scale,
				GEngine ? GEngine->GetSmallFont() : nullptr, 0.8f * Scale);
		}
	}
	DrawRect(Hovered == 15 ? FLinearColor(0.38f, 0.12f, 0.12f)
		: AuraHUDColors::PanelInner, Left + 808.f * Scale, Top + 16.f * Scale,
		38.f * Scale, 33.f * Scale);
	DrawCenteredText(TEXT("关"), AuraHUDColors::Text, Left + 827.f * Scale,
		Top + 22.f * Scale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.8f * Scale);
	const UAuraInventoryComponent* Inventory = State->GetInventoryComponent();
	DrawText(FString::Printf(TEXT("灵魂 %d     奥术矿石 %d     金属 %d"),
		State->GetGrowthState().Souls,
		Inventory ? Inventory->GetTotalQuantity(TEXT("Material.ArcaneOreShard")) : 0,
		Inventory ? Inventory->GetTotalQuantity(TEXT("Material.SmithMetal")) : 0),
		AuraHUDColors::Text, Left + 23.f * Scale, Top + 61.f * Scale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.86f * Scale, false);
	const TArray<FAuraCampServiceRow> Rows = Controller->BuildCampServiceRows();
	const int32 Page = Controller->GetCampPage();
	for (int32 LocalIndex = 0; LocalIndex < 8; ++LocalIndex)
	{
		const int32 Index = Page * 8 + LocalIndex;
		if (!Rows.IsValidIndex(Index)) break;
		const FAuraCampServiceRow& Row = Rows[Index];
		const float Y = Top + (100.f + LocalIndex * 45.f) * Scale;
		const FAuraCampServiceRow& Pending = Controller->GetCampPendingRow();
		const bool bSelected = Controller->IsCampConfirming()
			&& Row.OfferId == Pending.OfferId
			&& Row.InstanceId == Pending.InstanceId
			&& Row.Label == Pending.Label;
		DrawRect(Hovered == LocalIndex + 1 ? FLinearColor(0.12f, 0.24f, 0.34f)
			: (bSelected ? FLinearColor(0.12f, 0.31f, 0.28f)
				: AuraHUDColors::PanelInner), Left + 18.f * Scale, Y,
			504.f * Scale, 38.f * Scale);
		DrawOutline(Left + 18.f * Scale, Y, 504.f * Scale, 38.f * Scale,
			bSelected ? FLinearColor(0.25f, 0.86f, 0.64f)
				: (Row.Item.Definition ? AuraItems::GetRarityColor(Row.Item.Rarity) : Accent),
			bSelected ? 1.8f * Scale : 0.7f * Scale);
		DrawText(Row.Label,
			AuraHUDColors::Text, Left + 27.f * Scale, Y + 8.f * Scale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.75f * Scale, false);
	}
	if (Rows.IsEmpty())
	{
		DrawText(bSmith ? (Controller->GetSmithMode() == 1 ? TEXT("打造材料数据不可用")
			: (Controller->GetSmithMode() == 2 ? TEXT("背包中没有可熔炼装备")
				: TEXT("没有可强化的装备")))
			: (Controller->IsCampSelling() ? TEXT("没有可出售的物品") : TEXT("暂无商品")),
			AuraHUDColors::MutedText, Left + 32.f * Scale, Top + 123.f * Scale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.85f * Scale, false);
	}
	DrawRect(AuraHUDColors::PanelInner, Left + 540.f * Scale,
		Top + 100.f * Scale, 300.f * Scale, 355.f * Scale);
	DrawOutline(Left + 540.f * Scale, Top + 100.f * Scale,
		300.f * Scale, 355.f * Scale, Accent, Scale);
	if (Controller->IsCampConfirming())
	{
		const FAuraCampServiceRow& Row = Controller->GetCampPendingRow();
		if (const UAuraItemDefinition* Definition = Row.Item.Definition)
		{
			DrawItemIcon(Definition->Icon.LoadSynchronous(),
				Left + 555.f * Scale, Top + 116.f * Scale,
				94.f * Scale, 94.f * Scale);
			DrawText(Definition->DisplayName.ToString().ToUpper(), AuraHUDColors::Text,
				Left + 555.f * Scale, Top + 216.f * Scale,
				GEngine ? GEngine->GetSmallFont() : nullptr, 0.82f * Scale, false);
			DrawText(FString::Printf(TEXT("%s   等级 %d   +%d"),
					AuraItems::GetRarityName(Row.Item.Rarity), Row.Item.ItemLevel,
					Row.Item.EnhancementLevel), Accent,
					Left + 555.f * Scale, Top + 242.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.7f * Scale, false);
			if (bSmith && Controller->GetSmithMode() == 0)
			{
				const int32 Next = Row.Item.EnhancementLevel + 1;
				const FAuraCampEconomyBalance& Economy = UAuraCombatBalanceData::Get().CampEconomy;
				DrawText(Next > 3 ? TEXT("已达强化上限")
					: FString::Printf(TEXT("强化至 +%d  ·  %d 灵魂 / %d 矿石"), Next,
						Economy.GetForgeSoulCost(Next, Definition->EquipmentSlot),
						Economy.GetForgeMaterialCost(Next)),
					AuraHUDColors::Text, Left + 555.f * Scale, Top + 276.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.66f * Scale, false);
				if (Next <= 3)
				{
				FAuraItemInstance Enhanced = Row.Item;
				Enhanced.EnhancementLevel = Next;
				const TArray<FAuraItemStatModifier> OldStats = AuraItems::BuildFinalStatModifiers(Row.Item);
				const TArray<FAuraItemStatModifier> NewStats = AuraItems::BuildFinalStatModifiers(Enhanced);
				for (int32 Index = 0; Index < OldStats.Num() && Index < 3; ++Index)
				{
					const FAuraItemStatModifier& Stat = OldStats[Index];
					const float NewValue = AuraItems::GetStatMagnitude(NewStats, Stat.StatTag);
					DrawText(FString::Printf(TEXT("%s  %.1f > %.1f"),
						AuraItems::GetStatName(Stat.StatTag), Stat.Magnitude, NewValue),
						AuraHUDColors::Text, Left + 555.f * Scale,
						Top + (308.f + Index * 26.f) * Scale,
						GEngine ? GEngine->GetSmallFont() : nullptr, 0.68f * Scale, false);
				}
				}
			}
			else if (bSmith && Controller->GetSmithMode() == 1)
			{
				const FAuraCampEconomyBalance& Economy = UAuraCombatBalanceData::Get().CampEconomy;
				DrawText(FString::Printf(TEXT("花费 %d 金属 + %d 灵魂"),
					Economy.CraftMetalCost, Economy.CraftSoulCost), AuraHUDColors::Text,
					Left + 555.f * Scale, Top + 276.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * Scale, false);
				DrawText(FString::Printf(TEXT("保底进度 %d/2：第三次必出精良及以上"),
					State->GetGrowthState().CraftPityMisses), Accent,
					Left + 555.f * Scale, Top + 308.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.68f * Scale, false);
				DrawText(TEXT("品质随机；低级 55/35/10，中级 30/40/25/5"),
					AuraHUDColors::MutedText, Left + 555.f * Scale, Top + 339.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.56f * Scale, false);
				DrawText(TEXT("高阶 10/35/40/13/2；依次普通→传说"),
					AuraHUDColors::MutedText, Left + 555.f * Scale, Top + 366.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.56f * Scale, false);
			}
			else if (bSmith)
			{
				DrawText(FString::Printf(TEXT("销毁这件装备，获得 %d 金属"), Row.Price),
					AuraHUDColors::Text, Left + 555.f * Scale, Top + 276.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.68f * Scale, false);
				DrawText(TEXT("此操作不可撤销；已装备物品请先卸下"),
					FLinearColor(1.f, 0.4f, 0.3f), Left + 555.f * Scale,
					Top + 308.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.62f * Scale, false);
			}
			else
			{
				DrawText(FString::Printf(TEXT("%s %d 灵魂"),
					Controller->IsCampSelling() ? TEXT("获得") : TEXT("花费"), Row.Price),
					AuraHUDColors::Text, Left + 555.f * Scale, Top + 279.f * Scale,
					GEngine ? GEngine->GetSmallFont() : nullptr, 0.73f * Scale, false);
				const TArray<FAuraItemStatModifier> Stats = AuraItems::BuildFinalStatModifiers(Row.Item);
				for (int32 Index = 0; Index < Stats.Num() && Index < 3; ++Index)
				{
					DrawText(FString::Printf(TEXT("+%.1f %s"), Stats[Index].Magnitude,
						AuraItems::GetStatName(Stats[Index].StatTag)),
						AuraHUDColors::Text, Left + 555.f * Scale,
						Top + (310.f + Index * 26.f) * Scale,
						GEngine ? GEngine->GetSmallFont() : nullptr, 0.68f * Scale, false);
				}
			}
		}
		DrawRect(Hovered == 9 ? FLinearColor(0.12f, 0.46f, 0.26f)
			: FLinearColor(0.08f, 0.3f, 0.19f, 0.95f),
			Left + 24.f * Scale, Top + 514.f * Scale, 236.f * Scale, 41.f * Scale);
		DrawRect(Hovered == 10 ? FLinearColor(0.42f, 0.14f, 0.14f)
			: FLinearColor(0.25f, 0.08f, 0.08f, 0.95f),
			Left + 280.f * Scale, Top + 514.f * Scale, 240.f * Scale, 41.f * Scale);
		DrawText(bSmith ? (Controller->GetSmithMode() == 0 ? TEXT("确认强化")
			: (Controller->GetSmithMode() == 1 ? TEXT("确认打造") : TEXT("确认熔炼")))
			: TEXT("确认交易"), AuraHUDColors::Text,
			Left + 39.f * Scale, Top + 522.f * Scale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.85f * Scale, false);
		DrawText(TEXT("取消"), AuraHUDColors::Text,
			Left + 295.f * Scale, Top + 522.f * Scale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.85f * Scale, false);
	}
	DrawText(FString::Printf(TEXT("第 %d / %d 页"), Page + 1,
		FMath::Max(1, (Rows.Num() + 7) / 8)), AuraHUDColors::MutedText,
		Left + 180.f * Scale, Top + 473.f * Scale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.73f * Scale, false);
	for (int32 Direction = 0; Direction < 2; ++Direction)
	{
		const float ButtonX = Left + (Direction == 0 ? 24.f : 414.f) * Scale;
		DrawRect(Hovered == 13 + Direction ? FLinearColor(0.21f, 0.29f, 0.39f)
			: AuraHUDColors::PanelInner, ButtonX, Top + 468.f * Scale,
			106.f * Scale, 38.f * Scale);
		DrawCenteredText(Direction == 0 ? TEXT("上一页") : TEXT("下一页"),
			AuraHUDColors::Text, ButtonX + 53.f * Scale, Top + 476.f * Scale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.75f * Scale);
	}
	DrawText(bSmith ? TEXT("选择强化 / 打造 / 熔炼；鼠标点击确认")
		: TEXT("点击上方按钮切换购买 / 出售"), AuraHUDColors::MutedText,
		Left + 540.f * Scale, Top + 477.f * Scale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.7f * Scale, false);
}

void AAuraHUD::DrawPlayerVitals(float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	const AAuraCharacter* Character = AuraController && AuraController->IsCoopSpectating()
		&& AuraController->GetSpectatedTeammate() ? AuraController->GetSpectatedTeammate()
		: Cast<AAuraCharacter>(PlayerOwner->GetPawn());
	const UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem) return;

	const float Health = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float MaxHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
	const float Mana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const float MaxMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute());
	const int32 Experience = Character->GetExperienceIntoCurrentLevel();
	const int32 ExperienceRequired = Character->GetExperienceRequiredForNextLevel();
	const bool bMaxLevel = Character->IsAtMaxLevel();
	const bool bExperienceCapped = Character->GetEligibleLevel()
		>= UAuraCombatBalanceData::Get().Progression.MaxLevel;

	const float Width = 285.f * UIScale;
	const float Height = 24.f * UIScale;
	const float X = 34.f * UIScale;
	const float ManaY = Canvas->ClipY - 43.f * UIScale;
	const float HealthY = ManaY - 34.f * UIScale;
	const float ExperienceY = HealthY - 27.f * UIScale;
	const float ExperienceHeight = 12.f * UIScale;

	DrawRect(AuraHUDColors::Panel, X - 10.f * UIScale, ExperienceY - 26.f * UIScale,
		Width + 20.f * UIScale, 121.f * UIScale);
	const AAuraPlayerState* State = Character->GetPlayerState<AAuraPlayerState>();
	DrawText(FString::Printf(TEXT("%s  |  等级 %d  |  灵魂 %d"),
		AuraController && AuraController->IsCoopSpectating() ? TEXT("观战队友") : TEXT("奥拉"),
		Character->GetCharacterLevel(),
		State ? State->GetGrowthState().Souls : 0),
		AuraHUDColors::Border, X, ExperienceY - 23.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.f * UIScale, false);
	const float ExperienceRatio = bExperienceCapped ? 1.f
		: (ExperienceRequired > 0
			? FMath::Clamp(static_cast<float>(Experience) / ExperienceRequired, 0.f, 1.f) : 0.f);
	DrawRect(AuraHUDColors::PanelInner, X, ExperienceY, Width, ExperienceHeight);
	DrawRect(AuraHUDColors::Experience, X, ExperienceY,
		Width * ExperienceRatio, ExperienceHeight);
	DrawOutline(X, ExperienceY, Width, ExperienceHeight, AuraHUDColors::Border, UIScale);
	DrawCenteredText(bMaxLevel ? TEXT("已达最高等级")
		: FString::Printf(TEXT("经验  %d / %d"), Experience, ExperienceRequired),
		AuraHUDColors::Text, X + Width * 0.5f, ExperienceY - 1.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.82f * UIScale);
	DrawBar(TEXT("生命"), Health, MaxHealth, AuraHUDColors::Health, X, HealthY, Width, Height, UIScale);
	DrawBar(TEXT("法力"),
		Mana, MaxMana, AuraHUDColors::Mana, X, ManaY, Width, Height, UIScale);
	if (Character->HasSporeExposure())
	{
		DrawRect(FLinearColor(0.22f, 0.27f, 0.08f, 0.9f),
			X - 10.f * UIScale, ExperienceY - 55.f * UIScale,
			Width + 20.f * UIScale, 24.f * UIScale);
		DrawText(TEXT("孢子缠身 · 减速 · 小心突进/连击"),
			FLinearColor(0.91f, 1.f, 0.52f), X, ExperienceY - 52.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.78f * UIScale, false);
	}
}

void AAuraHUD::DrawAbilityPanel(float UIScale)
{
	const AAuraCharacter* Character = PlayerOwner
		? Cast<AAuraCharacter>(PlayerOwner->GetPawn()) : nullptr;
	float FireBoltDuration = 0.f;
	float FireBoltMana = 0.f;
	const float FireBoltRemaining = GetFireBoltCooldown(FireBoltDuration, FireBoltMana);
	float ShardsDuration = 0.f;
	float ShardsMana = 0.f;
	const float ShardsRemaining = GetArcaneShardsCooldown(ShardsDuration, ShardsMana);
	float ShockDuration = 0.f;
	float ShockMana = 0.f;
	const float ShockRemaining = GetShockCooldown(ShockDuration, ShockMana);

	const float Width = 116.f * UIScale;
	const float Height = 76.f * UIScale;
	const float Gap = 12.f * UIScale;
	const float TotalWidth = Width * 3.f + Gap * 2.f;
	const float StartX = Canvas->ClipX * 0.5f - TotalWidth * 0.5f;
	const float Y = Canvas->ClipY - 105.f * UIScale;

	const auto DrawAbilitySlot = [this, UIScale, Width, Height, Y](float X, const FString& Name,
		const FString& KeyLabel, float CooldownRemaining, float CooldownDuration, float ManaCost,
		UTexture2D* Icon, const FLinearColor& Accent, const int32 Rank)
	{
		const float CooldownRatio = CooldownDuration > 0.f
			? FMath::Clamp(CooldownRemaining / CooldownDuration, 0.f, 1.f) : 0.f;
		DrawRect(AuraHUDColors::Panel, X - 5.f * UIScale, Y - 5.f * UIScale,
			Width + 10.f * UIScale, Height + 10.f * UIScale);
		DrawRect(AuraHUDColors::PanelInner, X, Y, Width, Height);
		if (Icon)
		{
			DrawTexture(Icon, X + 7.f * UIScale, Y + 9.f * UIScale,
				34.f * UIScale, 34.f * UIScale, 0.f, 0.f, 1.f, 1.f);
		}
		if (CooldownRatio > 0.f)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f), X, Y, Width, Height * CooldownRatio);
		}
		if (Rank <= 0)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.78f), X, Y, Width, Height);
			DrawCenteredText(TEXT("未解锁"), AuraHUDColors::MutedText,
				X + Width * 0.5f, Y + 28.f * UIScale,
				GEngine ? GEngine->GetMediumFont() : nullptr, 0.70f * UIScale);
			DrawOutline(X, Y, Width, Height, AuraHUDColors::MutedText, UIScale);
			return;
		}
		DrawOutline(X, Y, Width, Height, Accent, 1.5f * UIScale);
		DrawCenteredText(Name, AuraHUDColors::Text, X + 79.f * UIScale,
			Y + 9.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.84f * UIScale);
		DrawCenteredText(FString::Printf(TEXT("%d级"), Rank), Accent,
			X + 24.f * UIScale, Y + 51.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.82f * UIScale);
		DrawCenteredText(CooldownRemaining > 0.02f
				? FString::Printf(TEXT("%.1f"), CooldownRemaining) : KeyLabel,
			CooldownRemaining > 0.02f ? Accent : AuraHUDColors::Reticle,
			X + 79.f * UIScale, Y + 28.f * UIScale,
			GEngine ? GEngine->GetMediumFont() : nullptr, 0.84f * UIScale);
		DrawCenteredText(ManaCost > 0.f ? FString::Printf(TEXT("%.0f 法力"), ManaCost) : TEXT("免费"),
			AuraHUDColors::MutedText,
			X + Width * 0.5f, Y + 57.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.8f * UIScale);
	};

	DrawAbilitySlot(StartX, TEXT("火球术"), TEXT("左键"), FireBoltRemaining,
		FireBoltDuration, FireBoltMana, FireBoltIcon, AuraHUDColors::Border,
		Character ? Character->GetSkillRank(EAuraPlayerSkill::FireBolt) : 0);
	const AAuraPlayerState* State = PlayerOwner
		? PlayerOwner->GetPlayerState<AAuraPlayerState>() : nullptr;
	if (State && State->GetGrowthState().SkillRanks.FireBoltPath
		== EAuraFireBoltPath::TripleVolley)
	{
		const int32 Charge = State->GetFireBoltVolleyCharge();
		DrawCenteredText(FString::Printf(TEXT("三火球 %d/5"), Charge),
			Charge == 4 ? FLinearColor(1.f, 0.58f, 0.19f) : AuraHUDColors::Border,
			StartX + Width * 0.5f, Y - 25.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.7f * UIScale);
	}
	DrawAbilitySlot(StartX + Width + Gap, TEXT("奥术碎片"), TEXT("Q"), ShardsRemaining,
		ShardsDuration, ShardsMana, ArcaneShardsIcon, FLinearColor(0.68f, 0.38f, 1.f, 1.f),
		Character ? Character->GetSkillRank(EAuraPlayerSkill::ArcaneShards) : 0);
	DrawAbilitySlot(StartX + (Width + Gap) * 2.f, TEXT("闪电术"), TEXT("E"), ShockRemaining,
		ShockDuration, ShockMana, ShockIcon, FLinearColor(0.18f, 0.82f, 1.f, 1.f),
		Character ? Character->GetSkillRank(EAuraPlayerSkill::Shock) : 0);
}

void AAuraHUD::DrawConsumableQuickSlots(const float UIScale)
{
	const AAuraPlayerState* AuraPlayerState = PlayerOwner
		? PlayerOwner->GetPlayerState<AAuraPlayerState>() : nullptr;
	const UAuraInventoryComponent* Inventory = AuraPlayerState
		? AuraPlayerState->GetInventoryComponent() : nullptr;
	if (!Inventory || !Canvas) return;
	const UAuraItemCatalog& Catalog = UAuraItemCatalog::Get();
	const UAuraItemDefinition* HealthPotion = Catalog.FindItem(
		TEXT("Consumable.HealthPotion"));
	const UAuraItemDefinition* ManaPotion = Catalog.FindItem(
		TEXT("Consumable.ManaPotion"));
	const int32 HealthCount = HealthPotion
		? Inventory->GetTotalQuantity(HealthPotion->ItemId) : 0;
	const int32 ManaCount = ManaPotion
		? Inventory->GetTotalQuantity(ManaPotion->ItemId) : 0;
	const float SlotWidth = 102.f * UIScale;
	const float SlotHeight = 48.f * UIScale;
	const float Gap = 10.f * UIScale;
	const float StartX = Canvas->ClipX - SlotWidth * 2.f - Gap - 28.f * UIScale;
	const float Y = Canvas->ClipY - SlotHeight - 28.f * UIScale;
	const auto DrawQuickSlot = [this, UIScale, SlotWidth, SlotHeight, Y](
		const float X, const TCHAR* Key, UTexture2D* Icon,
		const int32 Count, const FLinearColor& Accent)
	{
		DrawRect(AuraHUDColors::Panel, X, Y, SlotWidth, SlotHeight);
		DrawOutline(X, Y, SlotWidth, SlotHeight, Accent, 1.3f * UIScale);
		DrawItemIcon(Icon, X + 5.f * UIScale, Y + 5.f * UIScale,
			38.f * UIScale, 38.f * UIScale,
			Count > 0 ? FLinearColor::White : FLinearColor(0.4f, 0.4f, 0.4f, 0.5f));
		DrawCenteredText(FString::Printf(TEXT("[%s]"), Key),
			Count > 0 ? AuraHUDColors::Text : AuraHUDColors::MutedText,
			X + 71.f * UIScale, Y + 5.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.61f * UIScale);
		DrawCenteredText(FString::Printf(TEXT("x%d"), Count), Accent,
			X + 71.f * UIScale, Y + 25.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.66f * UIScale);
	};
	DrawQuickSlot(StartX, TEXT("1"), HealthPotion ? HealthPotion->Icon.LoadSynchronous() : nullptr, HealthCount,
		FLinearColor(0.92f, 0.2f, 0.16f, 1.f));
	DrawQuickSlot(StartX + SlotWidth + Gap, TEXT("2"), ManaPotion ? ManaPotion->Icon.LoadSynchronous() : nullptr, ManaCount,
		FLinearColor(0.2f, 0.48f, 1.f, 1.f));
}

void AAuraHUD::DrawRunUpgradeSummary(const float UIScale)
{
	const AAuraCharacter* Character = Cast<AAuraCharacter>(PlayerOwner ? PlayerOwner->GetPawn() : nullptr);
	if (!Character || Character->GetRunUpgradeState().IsEmpty()) return;

	TArray<EAuraRunUpgradeType> ActiveTypes;
	for (const EAuraRunUpgradeType Type : AuraRunUpgrade::GetAllTypes())
	{
		if (Character->GetRunUpgradeRank(Type) > 0) ActiveTypes.Add(Type);
	}
	const float Width = 218.f * UIScale;
	const float RowHeight = 22.f * UIScale;
	const float X = Canvas->ClipX - Width - 24.f * UIScale;
	const float Y = 126.f * UIScale;
	const float Height = (31.f + ActiveTypes.Num() * 22.f) * UIScale;
	DrawRect(AuraHUDColors::Panel, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, AuraHUDColors::Border, UIScale);
	DrawCenteredText(TEXT("本局增益"), AuraHUDColors::Border, X + Width * 0.5f,
		Y + 8.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.78f * UIScale);
	for (int32 Index = 0; Index < ActiveTypes.Num(); ++Index)
	{
		const EAuraRunUpgradeType Type = ActiveTypes[Index];
		const float RowY = Y + 31.f * UIScale + Index * RowHeight;
		DrawText(AuraRunUpgrade::GetTitle(Type), AuraRunUpgrade::GetColor(Type),
			X + 10.f * UIScale, RowY, GEngine ? GEngine->GetSmallFont() : nullptr,
			0.68f * UIScale, false);
		DrawText(FString::Printf(TEXT("%d级"), Character->GetRunUpgradeRank(Type)),
			AuraHUDColors::Text, X + Width - 31.f * UIScale, RowY,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.68f * UIScale, false);
	}
}

void AAuraHUD::DrawRunUpgradeSelection(const float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	const AAuraCharacter* Character = Cast<AAuraCharacter>(PlayerOwner ? PlayerOwner->GetPawn() : nullptr);
	if (!AuraController || !Character || !AuraController->IsRunUpgradeSelectionActive()) return;
	const TArray<EAuraRunUpgradeType>& Options = AuraController->GetRunUpgradeOptions();
	if (Options.IsEmpty()) return;

	DrawRect(FLinearColor(0.002f, 0.004f, 0.012f, 0.88f),
		0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawCenteredText(TEXT("选择一项本局增益"), FLinearColor(1.f, 0.76f, 0.22f, 1.f),
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.19f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 1.5f * UIScale);
	DrawCenteredText(TEXT("点击卡牌选择  |  也可按 1 / 2 / 3"), AuraHUDColors::MutedText,
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.27f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.9f * UIScale);

	const FVector2D ViewportSize(Canvas->ClipX, Canvas->ClipY);
	const int32 HoveredIndex = AuraController->GetHoveredRunUpgradeIndex();
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		const EAuraRunUpgradeType Type = Options[Index];
		const FBox2D Card = AuraRunUpgrade::GetSelectionCardRect(
			ViewportSize, Index, Options.Num());
		const FVector2D Size = Card.GetSize();
		const bool bHovered = HoveredIndex == Index;
		const FLinearColor Accent = AuraRunUpgrade::GetColor(Type);
		DrawRect(bHovered ? FLinearColor(0.09f, 0.105f, 0.16f, 0.98f)
			: AuraHUDColors::Panel, Card.Min.X, Card.Min.Y, Size.X, Size.Y);
		DrawOutline(Card.Min.X, Card.Min.Y, Size.X, Size.Y, Accent,
			(bHovered ? 3.f : 1.5f) * UIScale);
		DrawCenteredText(FString::Printf(TEXT("[%d]"), Index + 1), Accent,
			Card.GetCenter().X, Card.Min.Y + 18.f * UIScale,
			GEngine ? GEngine->GetMediumFont() : nullptr, 1.05f * UIScale);
		DrawCenteredText(AuraRunUpgrade::GetTitle(Type), AuraHUDColors::Text,
			Card.GetCenter().X, Card.Min.Y + 58.f * UIScale,
			GEngine ? GEngine->GetMediumFont() : nullptr, 0.95f * UIScale);

		FString PrimaryDescription;
		FString SecondaryDescription;
		AuraRunUpgrade::GetDescriptionLines(Type,
			Character->GetRunUpgradeRank(Type) + 1,
			PrimaryDescription, SecondaryDescription);
		DrawCenteredText(PrimaryDescription, Accent, Card.GetCenter().X,
			Card.Min.Y + 112.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.78f * UIScale);
		DrawCenteredText(SecondaryDescription, AuraHUDColors::MutedText,
			Card.GetCenter().X, Card.Min.Y + 145.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.66f * UIScale);
		DrawCenteredText(bHovered ? TEXT("点击领取") : TEXT("选择"),
			bHovered ? Accent : AuraHUDColors::MutedText, Card.GetCenter().X,
			Card.Max.Y - 30.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * UIScale);
	}
}

void AAuraHUD::DrawTargetingReticle(float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	const AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(PlayerOwner->GetPawn());
	if (!AuraController || AuraController->IsRunUpgradeSelectionActive()
		|| (AuraCharacter && AuraCharacter->IsDead())) return;

	float CenterX = Canvas->ClipX * 0.5f;
	float CenterY = Canvas->ClipY * 0.5f;
	PlayerOwner->GetMousePosition(CenterX, CenterY);
	const float Radius = 15.f * UIScale;
	constexpr int32 Segments = 24;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float AngleA = 2.f * PI * static_cast<float>(Segment) / Segments;
		const float AngleB = 2.f * PI * static_cast<float>(Segment + 1) / Segments;
		DrawLine(CenterX + FMath::Cos(AngleA) * Radius, CenterY + FMath::Sin(AngleA) * Radius,
			CenterX + FMath::Cos(AngleB) * Radius, CenterY + FMath::Sin(AngleB) * Radius,
			AuraHUDColors::Reticle, 1.5f * UIScale);
	}
	DrawLine(CenterX - 4.f * UIScale, CenterY, CenterX + 4.f * UIScale, CenterY,
		AuraHUDColors::Reticle, 1.f * UIScale);
	DrawLine(CenterX, CenterY - 4.f * UIScale, CenterX, CenterY + 4.f * UIScale,
		AuraHUDColors::Reticle, 1.f * UIScale);

	if (GetWorld() && GetWorld()->GetTimeSeconds() < HitMarkerEndTime)
	{
		const float Size = 8.f * UIScale;
		DrawLine(CenterX - Size, CenterY - Size, CenterX - 2.f, CenterY - 2.f,
			FLinearColor::White, 2.f * UIScale);
		DrawLine(CenterX + Size, CenterY - Size, CenterX + 2.f, CenterY - 2.f,
			FLinearColor::White, 2.f * UIScale);
		DrawLine(CenterX - Size, CenterY + Size, CenterX - 2.f, CenterY + 2.f,
			FLinearColor::White, 2.f * UIScale);
		DrawLine(CenterX + Size, CenterY + Size, CenterX + 2.f, CenterY + 2.f,
			FLinearColor::White, 2.f * UIScale);
	}

	DrawCenteredText(GetNetMode() == NM_Standalone
		? TEXT("上帝视角  |  鼠标右键：旋转  |  M：联机")
		: TEXT("上帝视角  |  鼠标右键：旋转"), AuraHUDColors::MutedText,
		165.f * UIScale,
		18.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.8f * UIScale);
}

void AAuraHUD::DrawArcaneShardsTargeting(const float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	const APawn* PlayerPawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	if (!AuraController || !PlayerPawn) return;

	FVector TargetLocation;
	bool bTargetClamped = false;
	if (!AuraController->GetArcaneShardsTargetPreview(TargetLocation, bTargetClamped)) return;

	const UAuraArcaneShardsAbility* ShardsAbility = GetDefault<UAuraArcaneShardsAbility>();
	if (const AAuraCharacter* Character = Cast<AAuraCharacter>(PlayerPawn))
	{
		if (const UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent())
		{
			if (const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromClass(
				UAuraArcaneShardsAbility::StaticClass()))
			{
				if (const UAuraArcaneShardsAbility* Instance =
					Cast<UAuraArcaneShardsAbility>(Spec->GetPrimaryInstance()))
				{
					ShardsAbility = Instance;
				}
			}
		}
	}
	FVector RangeCenter = PlayerPawn->GetActorLocation();
	RangeCenter.Z = TargetLocation.Z;
	const FLinearColor RangeColor(0.68f, 0.52f, 0.22f, 0.62f);
	const FLinearColor TargetColor = bTargetClamped
		? FLinearColor(1.f, 0.28f, 0.08f, 0.95f)
		: FLinearColor(0.72f, 0.32f, 1.f, 0.95f);
	const float TargetPulse = GetWorld()
		? 1.f + FMath::Sin(GetWorld()->GetTimeSeconds() * 5.5f) * 0.045f : 1.f;
	DrawWorldTexture(ArcaneTargetTexture, TargetLocation,
		ShardsAbility->GetEffectRadius(),
		FLinearColor(TargetColor.R, TargetColor.G, TargetColor.B, 0.62f),
		TargetPulse);
	DrawWorldCircle(RangeCenter, ShardsAbility->GetCastRange(), RangeColor, 1.5f * UIScale);
	DrawWorldCircle(TargetLocation, ShardsAbility->GetEffectRadius(), TargetColor, 3.f * UIScale);

	FVector2D TargetScreen;
	if (!PlayerOwner->ProjectWorldLocationToScreen(TargetLocation + FVector(0.f, 0.f, 8.f), TargetScreen, true)) return;
	const float CrossSize = 11.f * UIScale;
	DrawLine(TargetScreen.X - CrossSize, TargetScreen.Y, TargetScreen.X + CrossSize, TargetScreen.Y,
		TargetColor, 2.f * UIScale);
	DrawLine(TargetScreen.X, TargetScreen.Y - CrossSize, TargetScreen.X, TargetScreen.Y + CrossSize,
		TargetColor, 2.f * UIScale);
	DrawCenteredText(bTargetClamped ? TEXT("已达最大距离 · 左键释放") : TEXT("左键释放 · Q 取消"),
		TargetColor, TargetScreen.X, TargetScreen.Y - 35.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.82f * UIScale);
}

void AAuraHUD::DrawArcaneShardsTextureImpact(const float UIScale)
{
	if (!GetWorld() || ArcaneTextureImpactEndTime <= ArcaneTextureImpactStartTime) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < ArcaneTextureImpactStartTime || Now >= ArcaneTextureImpactEndTime) return;
	const float Progress = FMath::Clamp((Now - ArcaneTextureImpactStartTime)
		/ (ArcaneTextureImpactEndTime - ArcaneTextureImpactStartTime), 0.f, 1.f);
	const float Alpha = FMath::Clamp((1.f - Progress) / 0.42f, 0.f, 1.f);
	DrawWorldTexture(ArcaneTargetTexture, ArcaneTextureImpactLocation,
		ArcaneTextureImpactRadius, FLinearColor(0.7f, 0.38f, 1.f, Alpha),
		0.82f + Progress * 0.34f);
}

void AAuraHUD::DrawShockTargeting(const float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	const APawn* PlayerPawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	if (!AuraController || !PlayerPawn) return;

	FVector TargetLocation;
	bool bTargetClamped = false;
	if (!AuraController->GetShockTargetPreview(TargetLocation, bTargetClamped)) return;
	if (const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(PlayerPawn))
	{
		if (AuraPawn->GetSkillRankState().ShockPath == EAuraShockPath::ThunderJudgment)
		{
			DrawWorldCircle(TargetLocation, UAuraCombatBalanceData::Get().Elemental.JudgmentRadius,
				FLinearColor(0.2f,0.8f,1.f), 3.f * UIScale);
			return;
		}
		if (AuraPawn->GetSkillRankState().ShockPath == EAuraShockPath::StormField)
		{
			DrawWorldSector(PlayerPawn->GetActorLocation(), TargetLocation - PlayerPawn->GetActorLocation(),
				GetDefault<UAuraShockAbility>()->GetCastRange(), UAuraCombatBalanceData::Get().Elemental.StormHalfAngle,
				FLinearColor(0.2f,0.8f,1.f), 3.f * UIScale);
			return;
		}
	}

	const UAuraShockAbility* ShockAbility = GetDefault<UAuraShockAbility>();
	FVector RangeCenter = PlayerPawn->GetActorLocation();
	RangeCenter.Z = TargetLocation.Z;
	FVector Direction = TargetLocation - RangeCenter;
	Direction.Z = 0.f;
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero()) return;

	const FVector Side(-Direction.Y, Direction.X, 0.f);
	const float HalfWidth = ShockAbility->GetAimAssistRadius();
	const FVector CorridorStart = RangeCenter + Direction * 65.f;
	const FVector LeftStart = CorridorStart + Side * HalfWidth;
	const FVector RightStart = CorridorStart - Side * HalfWidth;
	const FVector LeftEnd = TargetLocation + Side * HalfWidth;
	const FVector RightEnd = TargetLocation - Side * HalfWidth;
	const FLinearColor RangeColor(0.12f, 0.62f, 0.86f, 0.58f);
	const FLinearColor DirectionColor = bTargetClamped
		? FLinearColor(1.f, 0.5f, 0.1f, 0.96f)
		: FLinearColor(0.16f, 0.88f, 1.f, 0.96f);

	DrawWorldCircle(RangeCenter, ShockAbility->GetCastRange(), RangeColor, 1.5f * UIScale);
	DrawWorldCircle(TargetLocation, HalfWidth, DirectionColor, 2.4f * UIScale, 32);

	FVector2D StartScreen;
	FVector2D TargetScreen;
	FVector2D LeftStartScreen;
	FVector2D RightStartScreen;
	FVector2D LeftEndScreen;
	FVector2D RightEndScreen;
	if (!PlayerOwner->ProjectWorldLocationToScreen(CorridorStart, StartScreen, true)
		|| !PlayerOwner->ProjectWorldLocationToScreen(TargetLocation, TargetScreen, true)
		|| !PlayerOwner->ProjectWorldLocationToScreen(LeftStart, LeftStartScreen, true)
		|| !PlayerOwner->ProjectWorldLocationToScreen(RightStart, RightStartScreen, true)
		|| !PlayerOwner->ProjectWorldLocationToScreen(LeftEnd, LeftEndScreen, true)
		|| !PlayerOwner->ProjectWorldLocationToScreen(RightEnd, RightEndScreen, true))
	{
		return;
	}

	DrawLine(StartScreen.X, StartScreen.Y, TargetScreen.X, TargetScreen.Y,
		DirectionColor, 2.2f * UIScale);
	DrawLine(LeftStartScreen.X, LeftStartScreen.Y, LeftEndScreen.X, LeftEndScreen.Y,
		DirectionColor, 1.5f * UIScale);
	DrawLine(RightStartScreen.X, RightStartScreen.Y, RightEndScreen.X, RightEndScreen.Y,
		DirectionColor, 1.5f * UIScale);
	DrawLine(LeftEndScreen.X, LeftEndScreen.Y, RightEndScreen.X, RightEndScreen.Y,
		DirectionColor, 1.5f * UIScale);
	DrawCenteredText(bTargetClamped ? TEXT("已达最大距离 · 左键释放") : TEXT("左键释放 · E 取消"),
		DirectionColor, TargetScreen.X, TargetScreen.Y - 36.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.82f * UIScale);
}

void AAuraHUD::DrawEnemyHealthBars(float UIScale)
{
	if (!GetWorld()) return;
	if (GetWorld()->GetTimeSeconds() >= NextEnemyCacheRefreshTime) RefreshEnemyCache();

	const APawn* PlayerPawn = Cast<APawn>(PlayerOwner->GetViewTarget());
	for (const TWeakObjectPtr<AAuraEnemy>& EnemyPointer : CachedEnemies)
	{
		const AAuraEnemy* Enemy = EnemyPointer.Get();
		const UAbilitySystemComponent* AbilitySystem = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
		if (!Enemy || !AbilitySystem || Enemy->IsSpawnPresentationActive()
			|| (PlayerPawn && FVector::DistSquared(PlayerPawn->GetActorLocation(), Enemy->GetActorLocation()) > FMath::Square(4000.f)))
		{
			continue;
		}

		const float Health = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
		const float MaxHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
		if (Health <= 0.f || MaxHealth <= 0.f) continue;
		DrawEnemyCombatTextureCues(*Enemy, UIScale);
		if (Enemy->IsBossEnemy() && Enemy->GetBossTelegraphRadius() > 0.f)
		{
			const EAuraBossAction BossAction = Enemy->GetBossAction();
			const FVector TelegraphEnd = Enemy->GetBossTelegraphLocation();
			FVector TelegraphStart = Enemy->GetActorLocation();
			TelegraphStart.Z = TelegraphEnd.Z;
			if (BossAction == EAuraBossAction::ClawLeft
				|| BossAction == EAuraBossAction::ClawRight)
			{
				DrawWorldSector(TelegraphStart, TelegraphEnd - TelegraphStart,
					Enemy->GetBossTelegraphRadius(),
					UAuraCombatBalanceData::Get().Boss.ClawHalfAngleDegrees,
					FLinearColor(1.f, 0.24f, 0.04f, 0.96f), 3.2f * UIScale, 18);
			}
			else if (BossAction == EAuraBossAction::DemonicCharge)
			{
				DrawWorldCorridor(TelegraphStart, TelegraphEnd,
					Enemy->GetBossTelegraphRadius(),
					FLinearColor(0.94f, 0.08f, 0.03f, 0.98f), 3.5f * UIScale);
			}
			else if (BossAction == EAuraBossAction::InfernalBarrage)
			{
				DrawWorldSector(TelegraphStart, TelegraphEnd - TelegraphStart,
					Enemy->GetBossTelegraphRadius(),
					UAuraCombatBalanceData::Get().Boss.BarrageHalfAngleDegrees,
					FLinearColor(1.f, 0.48f, 0.03f, 0.98f), 3.5f * UIScale, 24);
			}
			else if (BossAction == EAuraBossAction::DemonicWard)
			{
				DrawWorldCircle(TelegraphEnd, Enemy->GetBossTelegraphRadius(),
					FLinearColor(0.10f, 0.75f, 1.f, 0.95f), 3.2f * UIScale, 48);
			}
			else
			{
				const FLinearColor BossWarningColor = BossAction == EAuraBossAction::TailSpin
					? FLinearColor(1.f, 0.16f, 0.03f, 0.96f)
					: (BossAction == EAuraBossAction::SummonMinions
						? FLinearColor(0.72f, 0.12f, 1.f, 0.96f)
						: FLinearColor(1.f, 0.42f, 0.04f, 0.96f));
				DrawWorldCircle(TelegraphEnd, Enemy->GetBossTelegraphRadius(),
					BossWarningColor, 3.2f * UIScale, 48);
			}
		}
		if (Enemy->IsRangedCasting())
		{
			if (Enemy->IsRangedGroundBurstCasting())
			{
				const FVector GroundTarget = Enemy->GetRangedCastTargetLocation();
				const float GroundRadius = UAuraCombatBalanceData::Get().Shaman.GroundBurstRadius;
				DrawWorldCircle(GroundTarget, GroundRadius,
					FLinearColor(0.18f, 0.88f, 0.82f, 0.96f), 3.2f * UIScale, 40);
			}
			else if (Enemy->IsRangedVolleyCasting())
			{
				const FVector FanTarget = Enemy->GetRangedCastTargetLocation();
				FVector FanOrigin = Enemy->GetActorLocation();
				FanOrigin.Z = FanTarget.Z;
				DrawWorldSector(FanOrigin, FanTarget - FanOrigin,
					Enemy->GetRangedAttackRange(),
					UAuraCombatBalanceData::Get().Shaman.VolleySpreadDegrees * 0.5f,
					FLinearColor(1.f, 0.2f, 0.03f, 0.95f), 3.f * UIScale, 18);
			}
			else
			{
				DrawWorldCircle(Enemy->GetRangedCastTargetLocation(), 95.f,
					FLinearColor(1.f, 0.2f, 0.03f, 0.95f), 3.f * UIScale, 36);
			}
		}
		if (Enemy->GetMeleeTelegraphRadius() > 0.f)
		{
			const EAuraMeleeAction MeleeAction = Enemy->GetMeleeAction();
			const bool bBespokeTextureCue = MeleeAction == EAuraMeleeAction::GoblinJab
				|| MeleeAction == EAuraMeleeAction::GoblinLunge
				|| MeleeAction == EAuraMeleeAction::EliteGoblinSweep
				|| MeleeAction == EAuraMeleeAction::ShroomSporeBurst;
			if (bBespokeTextureCue)
			{
				// The exact authored texture is rendered above; do not layer the old
				// geometric sector/corridor/circle over it.
			}
			else
			{
			const FLinearColor TelegraphColor =
				MeleeAction == EAuraMeleeAction::EliteShroomSeed
					? FLinearColor(0.28f, 0.95f, 0.18f, 0.95f)
					: FLinearColor(1.f, 0.18f, 0.08f, 0.95f);
			DrawWorldCircle(Enemy->GetMeleeTelegraphLocation(),
				Enemy->GetMeleeTelegraphRadius(), TelegraphColor, 3.f * UIScale, 40);
			}
		}
		const FAuraEliteSporeFieldState& SporeField = Enemy->GetEliteSporeField();
		if (SporeField.bActive)
		{
			const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
			const float ServerTime = GameState
				? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
			if (ServerTime < SporeField.ExpireServerTime)
			{
				const bool bArmed = ServerTime >= SporeField.ArmedServerTime;
				DrawWorldCircle(SporeField.Location, SporeField.Radius,
					bArmed ? FLinearColor(0.18f, 0.9f, 0.18f, 0.85f)
						: FLinearColor(0.95f, 0.85f, 0.18f, 0.92f),
					bArmed ? 2.5f * UIScale : 3.2f * UIScale, 36);
			}
		}
		if (Enemy->IsBossEnemy()) continue;

		const bool bElite = Enemy->IsEliteVariant();
		const bool bEnraged = Enemy->IsEliteEnraged();
		FVector2D ScreenPosition;
		if (!PlayerOwner->ProjectWorldLocationToScreen(
			Enemy->GetActorLocation() + FVector(0.f, 0.f,
				FMath::Max(125.f, Enemy->GetCapsuleComponent()
					? Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 35.f
					: 125.f)), ScreenPosition, true))
		{
			continue;
		}

		const float Width = (bElite ? 130.f : 108.f) * UIScale;
		const float Height = (bElite ? 10.f : 8.f) * UIScale;
		const float X = ScreenPosition.X - Width * 0.5f;
		const float Y = ScreenPosition.Y;
		const bool bCasting = Enemy->IsRangedCasting();
		const FString MeleeActionLabel = Enemy->GetMeleeActionLabel();
		const FString DisplayName = bElite
			? FString(TEXT("精英·")) + Enemy->GetEnemyDisplayName()
			: Enemy->GetEnemyDisplayName();
		FString EnemyLabel = DisplayName;
		if (Enemy->IsPoiseBroken()) EnemyLabel += TEXT("  |  破势硬直");
		else if (Enemy->IsStunned()) EnemyLabel += TEXT("  |  眩晕");
		else
		{
			if (bEnraged) EnemyLabel += TEXT("  |  狂暴");
			if (bCasting) EnemyLabel += Enemy->IsRangedGroundBurstCasting()
				? TEXT("  |  风爆法阵")
				: (Enemy->IsRangedVolleyCasting()
					? TEXT("  |  扇形齐射") : TEXT("  |  施法中"));
			else if (!MeleeActionLabel.IsEmpty()) EnemyLabel += TEXT("  |  ") + MeleeActionLabel;
		}
		FLinearColor LabelColor = (bCasting || !MeleeActionLabel.IsEmpty())
			? FLinearColor(1.f, 0.46f, 0.08f, 1.f) : AuraHUDColors::Text;
		if (bElite) LabelColor = FLinearColor(1.f, 0.77f, 0.2f, 1.f);
		if (bEnraged) LabelColor = FLinearColor(1.f, 0.18f, 0.1f, 1.f);
		if (Enemy->IsStunned()) LabelColor = FLinearColor(0.2f, 0.88f, 1.f, 1.f);
		DrawCenteredText(EnemyLabel, LabelColor, ScreenPosition.X,
			Y - 17.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * UIScale);
		DrawRect(AuraHUDColors::Panel, X - 3.f * UIScale, Y - 3.f * UIScale,
			Width + 6.f * UIScale, Height + 14.f * UIScale);
		DrawRect(AuraHUDColors::PanelInner, X, Y, Width, Height);
		DrawRect(bEnraged ? FLinearColor(0.95f, 0.1f, 0.08f)
			: (bElite ? FLinearColor(0.94f, 0.53f, 0.08f) : AuraHUDColors::Health),
			X, Y, Width * FMath::Clamp(Health / MaxHealth, 0.f, 1.f), Height);
		DrawOutline(X, Y, Width, Height,
			bElite ? FLinearColor(1.f, 0.78f, 0.25f, 1.f) : AuraHUDColors::Border, UIScale);
		const float PoiseY = Y + Height + 5.f * UIScale;
		const float PoiseHeight = 3.f * UIScale;
		DrawRect(AuraHUDColors::PanelInner, X, PoiseY, Width, PoiseHeight);
		DrawRect(Enemy->IsPoiseBroken() ? FLinearColor(1.f, 0.2f, 0.1f)
			: FLinearColor(1.f, 0.83f, 0.27f), X, PoiseY,
			Width * FMath::Clamp(Enemy->GetPoiseCurrent()
				/ Enemy->GetPoiseMax(), 0.f, 1.f), PoiseHeight);
	}
}

void AAuraHUD::DrawEnemyCombatTextureCues(
	const AAuraEnemy& Enemy, const float UIScale)
{
	if (!GetWorld()) return;
	const EAuraMeleeAction Action = Enemy.GetMeleeAction();
	const float WindupProgress = Enemy.GetMeleeTelegraphProgress();
	const bool bGoblinAction = Action == EAuraMeleeAction::GoblinJab
		|| Action == EAuraMeleeAction::GoblinLunge
		|| Action == EAuraMeleeAction::EliteGoblinSweep;
	// One authored flash: it rises and falls beside the blade, then stays fully
	// gone for the final part of the windup so it never overlaps the strike cue.
	constexpr float GoblinFlashGoneProgress = 0.78f;
	if (bGoblinAction && Enemy.GetMeleeTelegraphRadius() > 0.f
		&& WindupProgress < GoblinFlashGoneProgress)
	{
		FVector WeaponAnchor = Enemy.GetActorLocation()
			+ Enemy.GetActorForwardVector() * 46.f + FVector(0.f, 0.f, 45.f);
		if (const USkeletalMeshComponent* WeaponMesh = Enemy.GetWeaponMesh();
			WeaponMesh && WeaponMesh->IsVisible())
		{
			WeaponAnchor = WeaponMesh->Bounds.Origin;
		}
		const float NormalizedFlash = WindupProgress / GoblinFlashGoneProgress;
		const float FlashAlpha = FMath::Sin(NormalizedFlash * PI);
		const float Pulse = 0.82f + FlashAlpha * 0.34f;
		DrawWorldSprite(GoblinWindupTexture, WeaponAnchor,
			64.f * UIScale * Pulse,
			FLinearColor(1.f, 0.72f, 0.18f, FlashAlpha * 0.98f));
	}
	else if (Action == EAuraMeleeAction::ShroomSporeBurst
		&& Enemy.GetMeleeTelegraphRadius() > 0.f)
	{
		// Build from transparent to fully readable. The separate impact cue below
		// supplies the brief explosion and disappears instead of lingering.
		float Reveal = FMath::Clamp(WindupProgress / 0.82f, 0.f, 1.f);
		Reveal = Reveal * Reveal * (3.f - 2.f * Reveal);
		DrawWorldGroundTexture(ShroomBurstTexture,
			Enemy.GetMeleeTelegraphLocation(), Enemy.GetMeleeTelegraphRadius(),
			FLinearColor(0.42f, 1.f, 0.28f, Reveal * 0.82f),
			0.9f + Reveal * 0.1f);
	}

	EAuraMeleeAction CueAction = EAuraMeleeAction::None;
	FVector CueLocation = FVector::ZeroVector;
	FVector CueDirection = FVector::ForwardVector;
	float CueProgress = 0.f;
	if (!Enemy.GetLocalMeleeTextureCue(
		CueAction, CueLocation, CueDirection, CueProgress)) return;
	const float FadeAlpha = FMath::Clamp((1.f - CueProgress) / 0.38f, 0.f, 1.f);
	const float Expansion = 0.82f + CueProgress * 0.52f;
	if (CueAction == EAuraMeleeAction::GoblinJab
		|| CueAction == EAuraMeleeAction::GoblinLunge
		|| CueAction == EAuraMeleeAction::EliteGoblinSweep)
	{
		const FAuraMeleeEnemyBalance& Balance = UAuraCombatBalanceData::Get().MeleeEnemy;
		const FAuraEliteEnemyBalance& Elite = UAuraCombatBalanceData::Get().EliteEnemy;
		const float TrailLength = CueAction == EAuraMeleeAction::GoblinJab
			? Balance.GoblinJabRange + Balance.GoblinJabStepDistance
			: (CueAction == EAuraMeleeAction::GoblinLunge
				? Balance.GoblinLungeWarningLength : Elite.GoblinSweepRange);
		const float TrailHead = FMath::Clamp(CueProgress / 0.36f, 0.f, 1.f);
		const float TrailFade = 1.f - FMath::Clamp(
			(CueProgress - 0.56f) / 0.44f, 0.f, 1.f);
		const float BodyFlash = 1.f - FMath::Clamp(CueProgress / 0.62f, 0.f, 1.f);
		DrawWorldSprite(GoblinStrikeTexture, CueLocation,
			(90.f - CueProgress * 16.f) * UIScale,
			FLinearColor(1.f, 0.58f, 0.12f, BodyFlash));

		// A row of cross-stars fills the swept path in about half of the cue's
		// lifetime, creating a body-to-flash dash rather than one floating spark.
		constexpr int32 TrailSamples = 8;
		for (int32 Sample = 1; Sample < TrailSamples; ++Sample)
		{
			const float Fraction = static_cast<float>(Sample)
				/ static_cast<float>(TrailSamples - 1);
			if (Fraction > TrailHead + KINDA_SMALL_NUMBER) break;
			const float HeadEmphasis = FMath::Clamp(
				1.f - (TrailHead - Fraction) * 0.72f, 0.34f, 1.f);
			DrawWorldSprite(GoblinStrikeTexture,
				CueLocation + CueDirection * TrailLength * Fraction,
				FMath::Lerp(42.f, 28.f, Fraction) * UIScale,
				FLinearColor(1.f, 0.38f + 0.25f * HeadEmphasis,
					0.08f, TrailFade * HeadEmphasis));
		}
	}
	else if (CueAction == EAuraMeleeAction::ShroomBash)
	{
		DrawWorldGroundTexture(ShroomBashTexture,
			CueLocation + FVector(0.f, 0.f, 2.f), 92.f,
			FLinearColor(0.68f, 1.f, 0.42f, FadeAlpha), Expansion,
			CueDirection.Rotation().Yaw);
	}
	else if (CueAction == EAuraMeleeAction::ShroomSporeBurst)
	{
		const float BurstAlpha = 1.f - FMath::Clamp(
			(CueProgress - 0.06f) / 0.94f, 0.f, 1.f);
		const float BurstExpansion = 0.96f + CueProgress * 0.5f;
		DrawWorldGroundTexture(ShroomBurstTexture, CueLocation,
			UAuraCombatBalanceData::Get().MeleeEnemy.ShroomSporeRadius,
			FLinearColor(0.72f, 1.f, 0.4f, BurstAlpha), BurstExpansion);
	}
}

void AAuraHUD::DrawComboMarks(const float Scale)
{
 if(!GetWorld()) return;
 if(GetWorld()->GetTimeSeconds()>=NextEnemyCacheRefreshTime) RefreshEnemyCache();
 const float Time=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
 for(const auto& Pointer:CachedEnemies)
 {
  const AAuraEnemy* Enemy=Pointer.Get();
  if(!Enemy || Enemy->IsDead()) continue;
  const auto& S=Enemy->GetElementalStatus()->GetStatus();
  FString Label;
  if(S.bHasEmber) Label+=TEXT("余烬 ");
  if(S.IgniteUntil>Time) Label+=TEXT("点燃 ");
  if(S.ParalysisUntil>Time) Label+=TEXT("麻痹 ");
  if(S.SlowUntil>Time) Label+=FString::Printf(TEXT("减速%.0f%% "),S.SlowFraction*100.f);
  if(S.CrystalHealth>0.f) Label+=FString::Printf(TEXT("晶壳 %.0f/%.0f"),S.CrystalHealth,S.CrystalMaxHealth);
  if(Label.IsEmpty()) continue;
  FVector2D Screen;
  if(PlayerOwner->ProjectWorldLocationToScreen(Enemy->GetActorLocation()+FVector(0.f,0.f,Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+55.f),Screen))
   DrawCenteredText(Label,FLinearColor(0.8f,0.8f,1.f),Screen.X,Screen.Y,GEngine->GetSmallFont(),0.68f*Scale);
 }
}

void AAuraHUD::DrawArcaneSlowFields(const float Scale)
{
 if(!GetWorld()) return;
 for(TActorIterator<AAuraElementalSpell> It(GetWorld());It;++It)
 {
  if(It->GetMode()==EAuraElementalZone::Storm)
   DrawWorldSector(It->GetActorLocation(),It->GetActorForwardVector(),It->GetRadius(),
    UAuraCombatBalanceData::Get().Elemental.StormHalfAngle,FLinearColor(0.15f,0.65f,1.f,0.7f),2.f*Scale);
  else if(It->GetMode()==EAuraElementalZone::Judgment)
  {
   DrawWorldCircle(It->GetActorLocation(),It->GetRadius(),FLinearColor(0.2f,0.55f,1.f,0.6f),1.5f*Scale);
   if(It->HasPendingStrike()) DrawWorldCircle(It->GetPendingStrike(),UAuraCombatBalanceData::Get().Elemental.JudgmentImpactRadius,FLinearColor(0.7f,0.8f,1.f),3.f*Scale);
  }
 }
}

void AAuraHUD::DrawBossHealthBar(const float UIScale)
{
	if (!GetWorld()) return;
	if (GetWorld()->GetTimeSeconds() >= NextEnemyCacheRefreshTime) RefreshEnemyCache();

	const AAuraEnemy* Boss = nullptr;
	const UAbilitySystemComponent* BossAbilitySystem = nullptr;
	for (const TWeakObjectPtr<AAuraEnemy>& EnemyPointer : CachedEnemies)
	{
		const AAuraEnemy* Candidate = EnemyPointer.Get();
		const UAbilitySystemComponent* CandidateAbilitySystem = Candidate
			? Candidate->GetAbilitySystemComponent() : nullptr;
		if (!Candidate || !Candidate->IsBossEnemy() || Candidate->IsDead()
			|| Candidate->IsSpawnPresentationActive()
			|| !CandidateAbilitySystem)
		{
			continue;
		}
		Boss = Candidate;
		BossAbilitySystem = CandidateAbilitySystem;
		break;
	}
	if (!Boss || !BossAbilitySystem) return;

	const float Health = BossAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float MaxHealth = BossAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
	if (Health <= 0.f || MaxHealth <= 0.f) return;

	const float Width = 550.f * UIScale;
	const float Height = 17.f * UIScale;
	const float X = Canvas->ClipX * 0.5f - Width * 0.5f;
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	const float Y = (AuraController && AuraController->IsCoopSpectating() ? 146.f : 92.f) * UIScale;
	const bool bWardActive = Boss->HasBossWard();
	const FString ActionLabel = Boss->GetBossActionLabel();
	FString BossLabel = FString::Printf(TEXT("%s  |  %s"),
		*Boss->GetEnemyDisplayName(), *Boss->GetBossPhaseLabel());
	if (Boss->IsBossTransitioning())
	{
		BossLabel += TEXT("  |  无敌状态");
	}
	else if (Boss->IsPoiseBroken())
	{
		BossLabel += TEXT("  |  破势硬直");
	}
	else if (!ActionLabel.IsEmpty())
	{
		BossLabel += TEXT("  |  ") + ActionLabel;
	}
	if (bWardActive)
	{
		BossLabel += FString::Printf(TEXT("  |  护盾 %.0f"), Boss->GetBossWardHealth());
	}

	const FLinearColor Accent = Boss->GetBossPhase() == EAuraBossPhase::PhaseTwo
		? FLinearColor(1.f, 0.18f, 0.035f, 1.f)
		: (Boss->IsBossTransitioning()
			? FLinearColor(1.f, 0.58f, 0.05f, 1.f)
			: FLinearColor(0.82f, 0.12f, 0.08f, 1.f));
	DrawRect(AuraHUDColors::Panel, X - 9.f * UIScale, Y - 26.f * UIScale,
		Width + 18.f * UIScale, Height + (bWardActive ? 48.f : 37.f) * UIScale);
	DrawCenteredText(BossLabel, Accent, Canvas->ClipX * 0.5f,
		Y - 22.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.88f * UIScale);
	DrawRect(AuraHUDColors::PanelInner, X, Y, Width, Height);
	DrawRect(Accent, X, Y, Width * FMath::Clamp(Health / MaxHealth, 0.f, 1.f), Height);
	DrawOutline(X, Y, Width, Height, Accent, 1.5f * UIScale);
	DrawCenteredText(FString::Printf(TEXT("%.0f / %.0f"), Health, MaxHealth),
		AuraHUDColors::Text, Canvas->ClipX * 0.5f, Y + 1.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.72f * UIScale);
	if (bWardActive)
	{
		const float WardY = Y + Height + 5.f * UIScale;
		DrawRect(AuraHUDColors::PanelInner, X, WardY, Width, 6.f * UIScale);
		DrawRect(FLinearColor(0.10f, 0.68f, 1.f), X, WardY,
			Width * FMath::Clamp(Boss->GetBossWardHealth()
				/ FMath::Max(1.f, Boss->GetBossWardCapacity()), 0.f, 1.f), 6.f * UIScale);
	}
	const float PoiseY = Y + Height + (bWardActive ? 16.f : 5.f) * UIScale;
	DrawRect(AuraHUDColors::PanelInner, X, PoiseY, Width, 5.f * UIScale);
	DrawRect(FLinearColor(1.f, 0.77f, 0.22f), X, PoiseY,
		Width * FMath::Clamp(Boss->GetPoiseCurrent() / Boss->GetPoiseMax(), 0.f, 1.f),
		5.f * UIScale);
}

void AAuraHUD::DrawFloatingDamage(float UIScale)
{
	if (!GetWorld()) return;
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	for (int32 Index = FloatingDamageNumbers.Num() - 1; Index >= 0; --Index)
	{
		const FAuraFloatingDamage& Entry = FloatingDamageNumbers[Index];
		const float Age = CurrentTime - Entry.SpawnTime;
		if (Age >= FloatingDamageLifetime)
		{
			FloatingDamageNumbers.RemoveAtSwap(Index);
			continue;
		}

		FVector2D ScreenPosition;
		if (!PlayerOwner->ProjectWorldLocationToScreen(Entry.WorldLocation, ScreenPosition, true)) continue;
		const float Progress = FMath::Clamp(Age / FloatingDamageLifetime, 0.f, 1.f);
		ScreenPosition.Y -= (30.f + 58.f * Progress) * UIScale;
		FLinearColor DamageColor(1.f, 0.58f, 0.08f, 1.f - FMath::Square(Progress));
		DrawCenteredText(FString::Printf(TEXT("-%.0f"), Entry.Amount), DamageColor,
			ScreenPosition.X, ScreenPosition.Y, GEngine ? GEngine->GetMediumFont() : nullptr,
			(1.2f + 0.2f * (1.f - Progress)) * UIScale);
	}
}

void AAuraHUD::DrawObjective(const float UIScale)
{
	FString DisplayObjective = ObjectiveText;
	if (DisplayObjective.IsEmpty() || DisplayObjective == TEXT("探索地牢"))
	{
		const AAuraGameState* Quest = GetWorld()
			? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
		if (Quest && !Quest->IsRunCompleted())
		{
			DisplayObjective = Quest->IsBossSummoned()
				? TEXT("击败最终首领")
				: (Quest->AreAllSealsActivated()
					? TEXT("三处守印齐备：前往祭坛完成逆向仪式")
					: TEXT("探索支路，激活任意三处封印  |  B 查看任务"));
		}
	}
	if (DisplayObjective.IsEmpty()) return;
	const float Width = 500.f * UIScale;
	const float Height = 43.f * UIScale;
	const float X = Canvas->ClipX * 0.5f - Width * 0.5f;
	const float Y = 24.f * UIScale;
	DrawRect(AuraHUDColors::Panel, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, AuraHUDColors::Border, UIScale);
	DrawCenteredText(DisplayObjective, AuraHUDColors::Text, Canvas->ClipX * 0.5f,
		Y + 11.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 1.f * UIScale);
}

void AAuraHUD::DrawTransientMessage(float UIScale)
{
	if (!GetWorld() || StatusMessage.IsEmpty() || GetWorld()->GetTimeSeconds() >= StatusMessageEndTime) return;
	const float Width = 300.f * UIScale;
	const float Height = 34.f * UIScale;
	const float X = Canvas->ClipX * 0.5f - Width * 0.5f;
	const float Y = Canvas->ClipY * 0.70f;
	DrawRect(AuraHUDColors::Panel, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, StatusMessageColor, UIScale);
	DrawCenteredText(StatusMessage, StatusMessageColor, Canvas->ClipX * 0.5f,
		Y + 8.f * UIScale, GEngine ? GEngine->GetSmallFont() : nullptr, 0.9f * UIScale);
}

void AAuraHUD::DrawLoreRecord(const float UIScale)
{
	if (!Canvas || !IsLoreRecordVisible()) return;
	const float Width = FMath::Min(720.f * UIScale, Canvas->ClipX - 24.f * UIScale);
	const float Height = 210.f * UIScale;
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = Canvas->ClipY * 0.42f;
	DrawRect(AuraHUDColors::PanelInner, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, LoreAccent, 2.f * UIScale);
	DrawText(LoreTitle, LoreAccent, X + 24.f * UIScale, Y + 17.f * UIScale,
		GEngine ? GEngine->GetMediumFont() : nullptr, 0.95f * UIScale, false);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		DrawText(LoreLines[Index], AuraHUDColors::Text,
			X + 24.f * UIScale, Y + (62.f + Index * 36.f) * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 1.07f * UIScale, false);
	}
	DrawText(TEXT("按 E 或 F 收起 · 可随时回到此处重读"), AuraHUDColors::MutedText,
		X + 24.f * UIScale, Y + 181.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.78f * UIScale, false);
}

void AAuraHUD::DrawPlayerHitFlash(float UIScale)
{
	if (!GetWorld() || GetWorld()->GetTimeSeconds() >= PlayerHitFlashEndTime) return;
	const float Edge = 42.f * UIScale;
	const FLinearColor FlashColor(0.72f, 0.f, 0.f, 0.34f);
	DrawRect(FlashColor, 0.f, 0.f, Canvas->ClipX, Edge);
	DrawRect(FlashColor, 0.f, Canvas->ClipY - Edge, Canvas->ClipX, Edge);
	DrawRect(FlashColor, 0.f, Edge, Edge, Canvas->ClipY - Edge * 2.f);
	DrawRect(FlashColor, Canvas->ClipX - Edge, Edge, Edge, Canvas->ClipY - Edge * 2.f);
}

void AAuraHUD::DrawDefeatOverlay(float UIScale)
{
	const AAuraCharacter* Character = Cast<AAuraCharacter>(PlayerOwner ? PlayerOwner->GetPawn() : nullptr);
	if (!Character || !Character->IsDead()) return;
	if (GetNetMode() != NM_Standalone) return;

	DrawRect(FLinearColor(0.01f, 0.005f, 0.008f, 0.78f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawCenteredText(TEXT("战败"), FLinearColor(0.92f, 0.08f, 0.06f, 1.f),
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.39f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 2.1f * UIScale);
	DrawCenteredText(TEXT("按 R 重新开始"), AuraHUDColors::Text,
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.53f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.05f * UIScale);
}

void AAuraHUD::DrawCoopRescue(const float UIScale)
{
	if (GetNetMode() == NM_Standalone || !PlayerOwner) return;
	const AAuraPlayerController* Controller = Cast<AAuraPlayerController>(PlayerOwner);
	const AAuraCharacter* Character = Cast<AAuraCharacter>(PlayerOwner->GetPawn());
	const AAuraGameState* State = GetWorld()->GetGameState<AAuraGameState>();
	if (!Character || !Controller || !State) return;
	const FLinearColor RescueColor(0.35f, 1.f, 0.62f);
	if (State->IsTeamWipePending())
	{
		DrawRect(FLinearColor(0.025f, 0.f, 0.f, 0.48f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
		DrawCenteredText(TEXT("全队倒下"), FLinearColor(1.f, 0.32f, 0.25f), Canvas->ClipX * 0.5f,
			Canvas->ClipY * 0.4f, GEngine->GetMediumFont(), 1.5f * UIScale);
		DrawCenteredText(FString::Printf(TEXT("%.0f 秒后返回检查点"),
			FMath::Max(0.f, State->GetTeamWipeEndTime() - State->GetServerWorldTimeSeconds())),
			AuraHUDColors::Text, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.47f,
			GEngine->GetSmallFont(), UIScale);
		return;
	}
	const UAuraReviveComponent* OwnRevive = Character->GetReviveComponent();
	const AAuraCharacter* RescueTarget = OwnRevive->GetRescueTarget();
	if (Character->IsDead())
	{
		const AAuraCharacter* Watched = Controller->GetSpectatedTeammate();
		const APlayerState* WatchedState = Watched ? Watched->GetPlayerState() : nullptr;
		DrawRect(FLinearColor(0.01f, 0.02f, 0.035f, 0.85f),
			Canvas->ClipX * 0.25f, 40.f * UIScale, Canvas->ClipX * 0.5f, 78.f * UIScale);
		DrawCenteredText(WatchedState
			? FString::Printf(TEXT("正在观战：%s"), *WatchedState->GetPlayerName())
			: TEXT("等待队友救援"), AuraHUDColors::Text, Canvas->ClipX * 0.5f,
			52.f * UIScale, GEngine->GetSmallFont(), UIScale);
		DrawCenteredText(TEXT("你已倒下 · 队友靠近后按住 F 5 秒可救起"), RescueColor,
			Canvas->ClipX * 0.5f, 86.f * UIScale, GEngine->GetSmallFont(), 0.8f * UIScale);
		RescueTarget = Character;
	}
	// Stable capsule anchor, independent of cosmetic ragdoll motion.
	for (const APlayerState* Player : State->PlayerArray)
	{
		const AAuraCharacter* Other = Player ? Cast<AAuraCharacter>(Player->GetPawn()) : nullptr;
		if (!Other || !Other->GetReviveComponent()->IsWaitingForRescue()) continue;
		FVector2D Position;
		if (PlayerOwner->ProjectWorldLocationToScreen(
			Other->GetReviveComponent()->GetRescueTransform().GetLocation() + FVector(0.f, 0.f, 70.f), Position))
		{
			DrawCenteredText(TEXT("＋ 等待救援"), RescueColor, Position.X, Position.Y,
				GEngine->GetSmallFont(), 0.85f * UIScale);
		}
	}
	if (RescueTarget && RescueTarget->GetReviveComponent()->GetRescuer())
	{
		const float Progress = RescueTarget->GetReviveComponent()->GetRescueProgress();
		const float Width = 380.f * UIScale;
		const float X = (Canvas->ClipX - Width) * 0.5f;
		const float Y = Canvas->ClipY * 0.7f;
		DrawRect(AuraHUDColors::Panel, X, Y, Width, 60.f * UIScale);
		DrawCenteredText(FString::Printf(TEXT("%s  %.1f / 5.0 秒"), Character->IsDead()
			? TEXT("队友正在救援你") : TEXT("正在救援队友"), Progress * 5.f),
			AuraHUDColors::Text, Canvas->ClipX * 0.5f, Y + 7.f * UIScale,
			GEngine->GetSmallFont(), 0.9f * UIScale);
		DrawRect(AuraHUDColors::PanelInner, X + 12.f * UIScale, Y + 37.f * UIScale,
			Width - 24.f * UIScale, 12.f * UIScale);
		DrawRect(RescueColor, X + 12.f * UIScale, Y + 37.f * UIScale,
			(Width - 24.f * UIScale) * Progress, 12.f * UIScale);
	}
	else if (!Character->IsDead() && !Controller->IsInventoryOpen()
		&& !Controller->IsCampMenuOpen() && !Controller->IsArchmageMenuOpen()
		&& Controller->FindRescueCandidate())
	{
		DrawCenteredText(TEXT("按住 [F] 救援队友 · 需要 5 秒"), RescueColor,
			Canvas->ClipX * 0.5f, Canvas->ClipY * 0.7f, GEngine->GetSmallFont(), UIScale);
	}
	if (!Character->IsDead() && OwnRevive->IsProtected())
		DrawCenteredText(TEXT("起身保护中 · 攻击或施法后结束"), RescueColor,
			Canvas->ClipX * 0.5f, Canvas->ClipY * 0.81f, GEngine->GetSmallFont(), 0.8f * UIScale);
}

void AAuraHUD::DrawVictoryCinematic(const float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	if (!AuraController || !AuraController->IsDemoCompleted()
		|| AuraController->IsVictoryOverlayVisible())
	{
		return;
	}

	const float Progress = AuraController->GetVictoryPresentationProgress();
	const float FadeAlpha = FMath::Clamp(Progress * 4.f, 0.f, 1.f);
	const float BarHeight = 58.f * UIScale;
	const FLinearColor LetterboxColor(0.002f, 0.003f, 0.006f, 0.94f * FadeAlpha);
	DrawRect(LetterboxColor, 0.f, 0.f, Canvas->ClipX, BarHeight);
	DrawRect(LetterboxColor, 0.f, Canvas->ClipY - BarHeight, Canvas->ClipX, BarHeight);

	if (Progress >= 0.16f)
	{
		const float TextAlpha = FMath::Clamp((Progress - 0.16f) * 4.f, 0.f, 1.f);
		DrawCenteredText(TEXT("魔王已被击败"),
			FLinearColor(1.f, 0.68f, 0.14f, TextAlpha), Canvas->ClipX * 0.5f,
			Canvas->ClipY - 43.f * UIScale,
			GEngine ? GEngine->GetSmallFont() : nullptr, 1.05f * UIScale);
	}
}

void AAuraHUD::DrawVictoryOverlay(const float UIScale)
{
	const AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(PlayerOwner);
	if (!AuraController || !AuraController->IsVictoryOverlayVisible()) return;

	DrawRect(FLinearColor(0.01f, 0.018f, 0.025f, 0.82f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawCenteredText(TEXT("地牢通关"), FLinearColor(1.f, 0.72f, 0.16f, 1.f),
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.36f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 2.05f * UIScale);
	DrawCenteredText(TEXT("魔王终被击败"), AuraHUDColors::Text,
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.49f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.05f * UIScale);
	DrawCenteredText(TEXT("按 R 返回营地  |  可拜访大魔法师"), AuraHUDColors::MutedText,
		Canvas->ClipX * 0.5f, Canvas->ClipY * 0.58f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.92f * UIScale);
}

void AAuraHUD::DrawBar(const FString& Label, float CurrentValue, float MaxValue, const FLinearColor& FillColor,
	float X, float Y, float Width, float Height, float UIScale)
{
	const float Ratio = MaxValue > 0.f ? FMath::Clamp(CurrentValue / MaxValue, 0.f, 1.f) : 0.f;
	DrawRect(AuraHUDColors::PanelInner, X, Y, Width, Height);
	DrawRect(FillColor, X, Y, Width * Ratio, Height);
	DrawOutline(X, Y, Width, Height, AuraHUDColors::Border, UIScale);
	DrawText(Label, AuraHUDColors::Text, X + 7.f * UIScale, Y + 4.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.92f * UIScale, false);
	const FString ValueText = FString::Printf(TEXT("%.0f / %.0f"), CurrentValue, MaxValue);
	DrawText(ValueText, AuraHUDColors::Text, X + Width - 74.f * UIScale, Y + 4.f * UIScale,
		GEngine ? GEngine->GetSmallFont() : nullptr, 0.92f * UIScale, false);
}

void AAuraHUD::DrawItemIcon(UTexture2D* Icon, const float X, const float Y,
	const float Width, const float Height, const FLinearColor& Tint)
{
	if (!Icon || Width <= 0.f || Height <= 0.f) return;
	const float TextureWidth = FMath::Max(1.f, static_cast<float>(Icon->GetSizeX()));
	const float TextureHeight = FMath::Max(1.f, static_cast<float>(Icon->GetSizeY()));
	const float FitScale = FMath::Min(Width / TextureWidth, Height / TextureHeight);
	const float DrawWidth = TextureWidth * FitScale;
	const float DrawHeight = TextureHeight * FitScale;
	DrawTexture(Icon, X + (Width - DrawWidth) * 0.5f,
		Y + (Height - DrawHeight) * 0.5f, DrawWidth, DrawHeight,
		0.f, 0.f, 1.f, 1.f, Tint, BLEND_Translucent);
}

void AAuraHUD::DrawOutline(float X, float Y, float Width, float Height, const FLinearColor& Color, float Thickness)
{
	DrawLine(X, Y, X + Width, Y, Color, Thickness);
	DrawLine(X + Width, Y, X + Width, Y + Height, Color, Thickness);
	DrawLine(X + Width, Y + Height, X, Y + Height, Color, Thickness);
	DrawLine(X, Y + Height, X, Y, Color, Thickness);
}

void AAuraHUD::DrawCenteredText(const FString& Text, const FLinearColor& Color, float CenterX, float Y,
	UFont* Font, float Scale)
{
	if (!Canvas || !Font) return;
	float Width = 0.f;
	float Height = 0.f;
	Canvas->StrLen(Font, Text, Width, Height);
	DrawText(Text, Color, CenterX - Width * Scale * 0.5f, Y, Font, Scale, false);
}

void AAuraHUD::DrawWorldCircle(const FVector& Center, const float Radius, const FLinearColor& Color,
	const float Thickness, const int32 Segments)
{
	if (!PlayerOwner || Segments < 3) return;

	FVector2D PreviousScreen = FVector2D::ZeroVector;
	bool bPreviousProjected = false;
	for (int32 Segment = 0; Segment <= Segments; ++Segment)
	{
		const float Angle = 2.f * PI * static_cast<float>(Segment) / static_cast<float>(Segments);
		const FVector WorldPoint = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 5.f);
		FVector2D ScreenPoint;
		const bool bProjected = PlayerOwner->ProjectWorldLocationToScreen(WorldPoint, ScreenPoint, true);
		if (bProjected && bPreviousProjected)
		{
			DrawLine(PreviousScreen.X, PreviousScreen.Y, ScreenPoint.X, ScreenPoint.Y, Color, Thickness);
		}
		PreviousScreen = ScreenPoint;
		bPreviousProjected = bProjected;
	}
}

void AAuraHUD::DrawWorldTexture(UTexture2D* Texture, const FVector& Center,
	const float WorldRadius, const FLinearColor& Tint, const float Scale)
{
	if (!Texture || !PlayerOwner || WorldRadius <= 0.f || Scale <= 0.f) return;
	FVector2D CenterScreen;
	if (!PlayerOwner->ProjectWorldLocationToScreen(
		Center + FVector(0.f, 0.f, 5.f), CenterScreen, true)) return;

	float MinX = CenterScreen.X;
	float MaxX = CenterScreen.X;
	float MinY = CenterScreen.Y;
	float MaxY = CenterScreen.Y;
	int32 ProjectedPoints = 0;
	const FVector Offsets[] =
	{
		FVector(WorldRadius, 0.f, 5.f), FVector(-WorldRadius, 0.f, 5.f),
		FVector(0.f, WorldRadius, 5.f), FVector(0.f, -WorldRadius, 5.f)
	};
	for (const FVector& Offset : Offsets)
	{
		FVector2D ScreenPoint;
		if (!PlayerOwner->ProjectWorldLocationToScreen(Center + Offset, ScreenPoint, true)) continue;
		MinX = FMath::Min(MinX, ScreenPoint.X);
		MaxX = FMath::Max(MaxX, ScreenPoint.X);
		MinY = FMath::Min(MinY, ScreenPoint.Y);
		MaxY = FMath::Max(MaxY, ScreenPoint.Y);
		++ProjectedPoints;
	}
	if (ProjectedPoints < 2) return;
	const float HalfWidth = FMath::Clamp((MaxX - MinX) * 0.5f * Scale, 6.f, 450.f);
	const float HalfHeight = FMath::Clamp((MaxY - MinY) * 0.5f * Scale, 6.f, 450.f);
	const FLinearColor AdditiveTint(
		Tint.R * Tint.A, Tint.G * Tint.A, Tint.B * Tint.A, 1.f);
	DrawTexture(Texture, CenterScreen.X - HalfWidth, CenterScreen.Y - HalfHeight,
		HalfWidth * 2.f, HalfHeight * 2.f, 0.f, 0.f, 1.f, 1.f,
		AdditiveTint, BLEND_Additive);
}

void AAuraHUD::DrawWorldGroundTexture(UTexture2D* Texture,
	const FVector& Center, const float WorldRadius, const FLinearColor& Tint,
	const float Scale, const float RotationDegrees)
{
	if (!Texture || !Texture->GetResource() || !PlayerOwner || !Canvas
		|| WorldRadius <= 0.f || Scale <= 0.f || Tint.A <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float Radius = WorldRadius * Scale;
	const FVector Forward = FRotator(0.f, RotationDegrees, 0.f).Vector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	const FVector RaisedCenter = Center + FVector(0.f, 0.f, 3.f);
	const FVector WorldCorners[4] =
	{
		RaisedCenter - Forward * Radius - Right * Radius,
		RaisedCenter + Forward * Radius - Right * Radius,
		RaisedCenter + Forward * Radius + Right * Radius,
		RaisedCenter - Forward * Radius + Right * Radius
	};
	FVector2D ScreenCorners[4];
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		if (!PlayerOwner->ProjectWorldLocationToScreen(
			WorldCorners[Corner], ScreenCorners[Corner], true))
		{
			return;
		}
	}
	const FLinearColor AdditiveTint(
		Tint.R * Tint.A, Tint.G * Tint.A, Tint.B * Tint.A, 1.f);
	TArray<FCanvasUVTri> Triangles;
	Triangles.SetNum(2);
	auto SetTriangle = [&AdditiveTint](FCanvasUVTri& Triangle,
		const FVector2D& P0, const FVector2D& UV0,
		const FVector2D& P1, const FVector2D& UV1,
		const FVector2D& P2, const FVector2D& UV2)
	{
		Triangle.V0_Pos = P0;
		Triangle.V0_UV = UV0;
		Triangle.V0_Color = AdditiveTint;
		Triangle.V1_Pos = P1;
		Triangle.V1_UV = UV1;
		Triangle.V1_Color = AdditiveTint;
		Triangle.V2_Pos = P2;
		Triangle.V2_UV = UV2;
		Triangle.V2_Color = AdditiveTint;
	};
	SetTriangle(Triangles[0], ScreenCorners[0], FVector2D(0.f, 0.f),
		ScreenCorners[1], FVector2D(1.f, 0.f),
		ScreenCorners[2], FVector2D(1.f, 1.f));
	SetTriangle(Triangles[1], ScreenCorners[0], FVector2D(0.f, 0.f),
		ScreenCorners[2], FVector2D(1.f, 1.f),
		ScreenCorners[3], FVector2D(0.f, 1.f));
	FCanvasTriangleItem TriangleItem(Triangles, Texture->GetResource());
	TriangleItem.BlendMode = SE_BLEND_Additive;
	Canvas->DrawItem(TriangleItem);
}

void AAuraHUD::DrawWorldSprite(UTexture2D* Texture, const FVector& Center,
	const float ScreenSize, const FLinearColor& Tint)
{
	if (!Texture || !PlayerOwner || ScreenSize <= 0.f) return;
	FVector2D ScreenPoint;
	if (!PlayerOwner->ProjectWorldLocationToScreen(Center, ScreenPoint, true)) return;
	const float TextureWidth = FMath::Max(1.f, static_cast<float>(Texture->GetSizeX()));
	const float TextureHeight = FMath::Max(1.f, static_cast<float>(Texture->GetSizeY()));
	const float FitScale = ScreenSize / FMath::Max(TextureWidth, TextureHeight);
	const float DrawWidth = TextureWidth * FitScale;
	const float DrawHeight = TextureHeight * FitScale;
	const FLinearColor AdditiveTint(
		Tint.R * Tint.A, Tint.G * Tint.A, Tint.B * Tint.A, 1.f);
	DrawTexture(Texture, ScreenPoint.X - DrawWidth * 0.5f,
		ScreenPoint.Y - DrawHeight * 0.5f, DrawWidth, DrawHeight,
		0.f, 0.f, 1.f, 1.f, AdditiveTint, BLEND_Additive);
}

void AAuraHUD::DrawWorldCorridor(const FVector& Start, const FVector& End,
	const float HalfWidth, const FLinearColor& Color, const float Thickness)
{
	if (!PlayerOwner || HalfWidth <= 0.f) return;
	FVector Direction = End - Start;
	Direction.Z = 0.f;
	if (!Direction.Normalize()) return;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	const FVector Lift(0.f, 0.f, 5.f);
	const FVector WorldPoints[] = {
		Start - Right * HalfWidth + Lift,
		Start + Right * HalfWidth + Lift,
		End - Right * HalfWidth + Lift,
		End + Right * HalfWidth + Lift,
		Start + Lift,
		End + Lift
	};
	FVector2D ScreenPoints[6];
	for (int32 Index = 0; Index < 6; ++Index)
	{
		if (!PlayerOwner->ProjectWorldLocationToScreen(WorldPoints[Index], ScreenPoints[Index], true))
		{
			return;
		}
	}
	DrawLine(ScreenPoints[0].X, ScreenPoints[0].Y, ScreenPoints[2].X, ScreenPoints[2].Y,
		Color, Thickness);
	DrawLine(ScreenPoints[1].X, ScreenPoints[1].Y, ScreenPoints[3].X, ScreenPoints[3].Y,
		Color, Thickness);
	DrawLine(ScreenPoints[0].X, ScreenPoints[0].Y, ScreenPoints[1].X, ScreenPoints[1].Y,
		Color, Thickness);
	DrawLine(ScreenPoints[2].X, ScreenPoints[2].Y, ScreenPoints[3].X, ScreenPoints[3].Y,
		Color, Thickness);
	FLinearColor GuideColor = Color;
	GuideColor.A *= 0.55f;
	DrawLine(ScreenPoints[4].X, ScreenPoints[4].Y, ScreenPoints[5].X, ScreenPoints[5].Y,
		GuideColor, FMath::Max(1.f, Thickness * 0.55f));
	for (int32 Stripe = 1; Stripe <= 3; ++Stripe)
	{
		const float Alpha = static_cast<float>(Stripe) / 4.f;
		const FVector LeftPoint = FMath::Lerp(WorldPoints[0], WorldPoints[2], Alpha);
		const FVector RightPoint = FMath::Lerp(WorldPoints[1], WorldPoints[3], Alpha);
		FVector2D LeftScreen;
		FVector2D RightScreen;
		if (PlayerOwner->ProjectWorldLocationToScreen(LeftPoint, LeftScreen, true)
			&& PlayerOwner->ProjectWorldLocationToScreen(RightPoint, RightScreen, true))
		{
			DrawLine(LeftScreen.X, LeftScreen.Y, RightScreen.X, RightScreen.Y,
				GuideColor, FMath::Max(1.f, Thickness * 0.38f));
		}
	}
}

void AAuraHUD::DrawWorldSector(const FVector& Center, const FVector& Direction,
	const float Radius, const float HalfAngleDegrees, const FLinearColor& Color,
	const float Thickness, const int32 ArcSegments)
{
	if (!PlayerOwner || Radius <= 0.f || ArcSegments < 2) return;
	FVector Forward = Direction.GetSafeNormal2D();
	if (Forward.IsNearlyZero()) return;
	const FVector Origin = Center + FVector(0.f, 0.f, 5.f);
	FVector2D OriginScreen;
	if (!PlayerOwner->ProjectWorldLocationToScreen(Origin, OriginScreen, true)) return;

	FVector2D PreviousArcScreen = FVector2D::ZeroVector;
	bool bPreviousProjected = false;
	FVector2D LeftBoundaryScreen = FVector2D::ZeroVector;
	FVector2D RightBoundaryScreen = FVector2D::ZeroVector;
	for (int32 Segment = 0; Segment <= ArcSegments; ++Segment)
	{
		const float Alpha = static_cast<float>(Segment) / static_cast<float>(ArcSegments);
		const float Angle = FMath::Lerp(-HalfAngleDegrees, HalfAngleDegrees, Alpha);
		const FVector ArcPoint = Origin
			+ Forward.RotateAngleAxis(Angle, FVector::UpVector) * Radius;
		FVector2D ArcScreen;
		const bool bProjected = PlayerOwner->ProjectWorldLocationToScreen(
			ArcPoint, ArcScreen, true);
		if (Segment == 0 && bProjected) LeftBoundaryScreen = ArcScreen;
		if (Segment == ArcSegments && bProjected) RightBoundaryScreen = ArcScreen;
		if (bProjected && bPreviousProjected)
		{
			DrawLine(PreviousArcScreen.X, PreviousArcScreen.Y, ArcScreen.X, ArcScreen.Y,
				Color, Thickness);
		}
		PreviousArcScreen = ArcScreen;
		bPreviousProjected = bProjected;
	}
	DrawLine(OriginScreen.X, OriginScreen.Y, LeftBoundaryScreen.X, LeftBoundaryScreen.Y,
		Color, Thickness);
	DrawLine(OriginScreen.X, OriginScreen.Y, RightBoundaryScreen.X, RightBoundaryScreen.Y,
		Color, Thickness);

	FLinearColor GuideColor = Color;
	GuideColor.A *= 0.5f;
	for (const float GuideAngle : { -HalfAngleDegrees, 0.f, HalfAngleDegrees })
	{
		const FVector GuidePoint = Origin
			+ Forward.RotateAngleAxis(GuideAngle, FVector::UpVector) * Radius;
		FVector2D GuideScreen;
		if (PlayerOwner->ProjectWorldLocationToScreen(GuidePoint, GuideScreen, true))
		{
			DrawLine(OriginScreen.X, OriginScreen.Y, GuideScreen.X, GuideScreen.Y,
				GuideColor, FMath::Max(1.f, Thickness * 0.45f));
		}
	}
}

void AAuraHUD::RefreshEnemyCache()
{
	CachedEnemies.Reset();
	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraEnemy::StaticClass(), Enemies);
	for (AActor* EnemyActor : Enemies)
	{
		if (AAuraEnemy* Enemy = Cast<AAuraEnemy>(EnemyActor)) CachedEnemies.Add(Enemy);
	}
	NextEnemyCacheRefreshTime = GetWorld() ? GetWorld()->GetTimeSeconds() + 0.5f : 0.f;
}

float AAuraHUD::GetFireBoltCooldown(float& OutDuration, float& OutManaCost) const
{
	OutDuration = 0.f;
	OutManaCost = 0.f;
	const AAuraCharacter* Character = PlayerOwner ? Cast<AAuraCharacter>(PlayerOwner->GetPawn()) : nullptr;
	UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem) return 0.f;

	const UAuraFireBoltAbility* FireBoltAbility = nullptr;
	if (const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromClass(UAuraFireBoltAbility::StaticClass()))
	{
		FireBoltAbility = Cast<UAuraFireBoltAbility>(Spec->GetPrimaryInstance());
	}
	if (!FireBoltAbility) FireBoltAbility = GetDefault<UAuraFireBoltAbility>();

	OutDuration = FireBoltAbility->GetCooldownDuration();
	OutManaCost = FireBoltAbility->GetManaCost();
	return FireBoltAbility->GetCooldownRemaining();
}

float AAuraHUD::GetArcaneShardsCooldown(float& OutDuration, float& OutManaCost) const
{
	OutDuration = 0.f;
	OutManaCost = 0.f;
	const AAuraCharacter* Character = PlayerOwner ? Cast<AAuraCharacter>(PlayerOwner->GetPawn()) : nullptr;
	UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem) return 0.f;

	const UAuraArcaneShardsAbility* ShardsAbility = nullptr;
	if (const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromClass(UAuraArcaneShardsAbility::StaticClass()))
	{
		ShardsAbility = Cast<UAuraArcaneShardsAbility>(Spec->GetPrimaryInstance());
	}
	if (!ShardsAbility) ShardsAbility = GetDefault<UAuraArcaneShardsAbility>();

	OutDuration = ShardsAbility->GetCooldownDuration();
	OutManaCost = ShardsAbility->GetManaCost();
	return ShardsAbility->GetCooldownRemaining();
}

float AAuraHUD::GetShockCooldown(float& OutDuration, float& OutManaCost) const
{
	OutDuration = 0.f;
	OutManaCost = 0.f;
	const AAuraCharacter* Character = PlayerOwner ? Cast<AAuraCharacter>(PlayerOwner->GetPawn()) : nullptr;
	UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem) return 0.f;

	const UAuraShockAbility* ShockAbility = nullptr;
	if (const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromClass(UAuraShockAbility::StaticClass()))
	{
		ShockAbility = Cast<UAuraShockAbility>(Spec->GetPrimaryInstance());
	}
	if (!ShockAbility) ShockAbility = GetDefault<UAuraShockAbility>();

	OutDuration = ShockAbility->GetCooldownDuration();
	OutManaCost = ShockAbility->GetManaCost();
	return ShockAbility->GetCooldownRemaining();
}
