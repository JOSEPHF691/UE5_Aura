// Copyright yosep
#include "Player/AuraPlayerController.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraElementalSpell.h"
#include "AbilitySystem/AuraElementalStatusComponent.h"
#include "AbilitySystem/Abilities/AuraArcaneShardsAbility.h"
#include "AbilitySystem/Abilities/AuraShockAbility.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Character/AuraEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/AuraArchmage.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"
#include "UI/AuraServiceMenuWidget.h"
#include "UnrealClient.h"

namespace
{
	struct FElementalTest { bool bPassed = true; int32 Checks = 0; };
	void Check(const TSharedRef<FElementalTest>& T, const TCHAR* Name, bool Passed)
	{
		T->bPassed &= Passed; ++T->Checks;
		UE_LOG(LogTemp, Display, TEXT("AURA_ELEMENTAL_CHECK Name=%s Passed=%s"), Name, Passed ? TEXT("true") : TEXT("false"));
	}
	void Later(UWorld* W, float Delay, TFunction<void()> Action)
	{
		FTimerHandle H; W->GetTimerManager().SetTimer(H, [Action = MoveTemp(Action)]() { Action(); }, Delay, false);
	}
	float Health(AAuraEnemy* E) { return E->GetAbilitySystemComponent()->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()); }
	void Capture(const TCHAR* Name)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("AuraElementalCapture"))) FScreenshotRequest::RequestScreenshot(Name, false, false);
	}
	void ClearCooldowns(AAuraCharacter* Pawn)
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.ArcaneShards")));
		Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Shock")));
		Pawn->GetAbilitySystemComponent()->RemoveActiveEffectsWithGrantedTags(Tags);
		Pawn->GetAbilitySystemComponent()->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 200.f);
	}
}

void AAuraPlayerController::AuraTestElementalSave()
{
	FString Slot, Mode;
	FParse::Value(FCommandLine::Get(), TEXT("AuraSaveSlot="), Slot);
	FParse::Value(FCommandLine::Get(), TEXT("ElementalSaveMode="), Mode);
	if (!HasAuthority() || !Slot.StartsWith(TEXT("ElementalTest_"))) return;
	bool bPassed = false;
	if (Mode == TEXT("Seed") || Mode == TEXT("Invalid") || Mode == TEXT("Future"))
	{
		UAuraProfileSaveGame* Save = NewObject<UAuraProfileSaveGame>();
		Save->SchemaVersion = Mode == TEXT("Future") ? 99 : 5;
		Save->bHasProfile = true;
		auto& G = Save->Growth;
		G.PaidLevel = 10; G.TotalExperience = UAuraCombatBalanceData::Get().Progression.GetCumulativeExperienceForLevel(10);
		G.Souls = 456; G.SkillRanks.FireBolt = 3; G.SkillRanks.FireBoltPath = EAuraFireBoltPath::TripleVolley;
		G.SkillRanks.ArcaneShards = 3; G.SkillRanks.Shock = 3; G.UnspentSkillPoints = 1;
		G.ComboRanks.Ember = 1; G.ComboRanks.Fracture = 1; G.ComboRanks.Overload = 1; G.UnspentComboPoints = 0;
		G.CraftPityMisses = 1; G.SmithMetalPurchased = 2;
		if (Mode == TEXT("Invalid")) G.UnspentComboPoints = 7;
		bPassed = UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
	}
	else
	{
		UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
		FAuraGrowthState G;
		if (Mode == TEXT("Read"))
		{
			bPassed = Run && Run->TryGetSavedGrowth(G) && AAuraPlayerState::IsValidGrowthSnapshot(G)
				&& G.PaidLevel == 10 && G.Souls == 456 && G.CraftPityMisses == 1 && G.SmithMetalPurchased == 2
				&& G.UnspentSkillPoints == 1 && G.UnspentComboPoints == 0 && G.ComboRanks.GetSpentPoints() == 0
				&& G.SkillRanks.FireBoltPath == EAuraFireBoltPath::TripleVolley
				&& G.SkillRanks.bArcaneEvolutionPending && G.SkillRanks.bShockEvolutionPending;
		}
		else bPassed = Run && !Run->IsPersistentSaveEnabled() && !Run->FlushPersistentSave()
			&& GetPlayerState<AAuraPlayerState>()->GetGrowthState().PaidLevel == 1;
	}
	UE_LOG(LogTemp, Display, TEXT("AURA_ELEMENTAL_SAVE Mode=%s Passed=%s"), *Mode, bPassed ? TEXT("true") : TEXT("false"));
	UE_LOG(LogTemp, Display, TEXT("AURA_ELEMENTAL_SAVE_DONE"));
}

void AAuraPlayerController::AuraTestElementalSkills()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* GrowthOwner = GetPlayerState<AAuraPlayerState>();
	const UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	if (!HasAuthority() || !AuraPawn || !GrowthOwner || !Run || Run->IsPersistentSaveEnabled()) return;
	const TSharedRef<FElementalTest> T = MakeShared<FElementalTest>();
	UWorld* W = GetWorld();
	const FVector Origin(120000, 120000, 2000);
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Floor = W->SpawnActor<AStaticMeshActor>(Origin - FVector(0,0,120), FRotator::ZeroRotator, Params);
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->SetReplicates(true);
	Floor->SetReplicateMovement(true);
	Floor->GetStaticMeshComponent()->SetIsReplicated(true);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(160,160,1));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->GetStaticMeshComponent()->RecreatePhysicsState();
	AuraPawn->TeleportTo(Origin, FRotator::ZeroRotator, false, true);
	AuraPawn->GetCharacterMovement()->DisableMovement();
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		if (AAuraCharacter* Other = Cast<AAuraCharacter>(It->Get()->GetPawn()))
			if (Other != AuraPawn) { Other->TeleportTo(Origin + FVector(0,-350,0), FRotator::ZeroRotator, false, true); Other->GetCharacterMovement()->DisableMovement(); }
	const auto Spawn = [this, W, AuraPawn, Params](const FVector& Location, bool Elite = false)
	{
		UClass* Class = LoadClass<AAuraEnemy>(nullptr, TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear.BP_AuraGoblinSpear_C"));
		const FTransform Transform(FRotator::ZeroRotator, Location);
		AAuraEnemy* E = W->SpawnActorDeferred<AAuraEnemy>(Class, Transform, this, AuraPawn, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		E->ConfigurePreplacedGuard(); if (Elite) E->ConfigureEliteVariant(FVector(1.18f));
		UGameplayStatics::FinishSpawningActor(E, Transform);
		E->GetCharacterMovement()->DisableMovement();
		if (AAIController* AI = Cast<AAIController>(E->GetController())) { AI->StopMovement(); AI->SetActorTickEnabled(false); }
		UAbilitySystemComponent* ASC = E->GetAbilitySystemComponent();
		ASC->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(),10000);
		ASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(),10000);
		ASC->SetNumericAttributeBase(UAuraAttributeSet::GetArmorAttribute(),0);
		return E;
	};
	GrowthOwner->AwardExperienceAndSouls(UAuraCombatBalanceData::Get().Progression.GetCumulativeExperienceForLevel(10),0);
	FString Failure;
	Check(T,TEXT("EveryLevelSkillPointNoCombo"),GrowthOwner->GetGrowthState().PaidLevel == 10
		&& GrowthOwner->GetGrowthState().UnspentSkillPoints == 9 && GrowthOwner->GetGrowthState().UnspentComboPoints == 0
		&& !GrowthOwner->TryAllocateCombo(EAuraComboMarkType::Ember,Failure));
	for (int32 I=0; I<2; ++I) { GrowthOwner->TryAllocateSkill(EAuraPlayerSkill::ArcaneShards,Failure); GrowthOwner->TryAllocateSkill(EAuraPlayerSkill::Shock,Failure); }
	GrowthOwner->TryAllocateSkill(EAuraPlayerSkill::FireBolt,Failure);
	Check(T,TEXT("RankThreeRequiresExplicitEvolution"),!GrowthOwner->TryAllocateSkill(EAuraPlayerSkill::ArcaneShards,Failure)
		&& !GrowthOwner->TryChooseEvolution(EAuraPlayerSkill::Shock,7,Failure));
	AAuraArchmage* Mage = W->SpawnActor<AAuraArchmage>(Origin + FVector(0,120,-75),FRotator::ZeroRotator,Params);
	if (IsLocalController())
	{
		ClientOpenArchmage_Implementation(Mage);
		const auto Click = [this](int32 Action) { return ActiveServiceMenuWidget && ActiveServiceMenuWidget->ClickActionForSmoke(Action); };
		bool UI = Click(2) && ArchmagePendingAction == 11 && Click(3) && Click(6);
		UI &= Click(3) && ArchmagePendingAction == 12 && Click(2) && Click(6);
		UI &= Click(4) && ArchmagePendingAction == 13 && Click(2) && Click(6);
		Check(T,TEXT("RealMageButtonsAllThreeEvolutions"),UI && GrowthOwner->GetGrowthState().SkillRanks.ArcanePath==EAuraArcanePath::CrystalPrison
			&& GrowthOwner->GetGrowthState().SkillRanks.ShockPath==EAuraShockPath::StormField
			&& GrowthOwner->GetGrowthState().SkillRanks.FireBoltPath==EAuraFireBoltPath::TripleVolley);
		Click(3); Capture(TEXT("AuraElemental_Mage.png"));
		Later(W,0.3f,[this]() { if (ActiveServiceMenuWidget) ActiveServiceMenuWidget->ClickActionForSmoke(9); });
	}
	else
	{
		GrowthOwner->TryAllocateSkill(EAuraPlayerSkill::FireBolt,Failure,EAuraFireBoltPath::TripleVolley);
		GrowthOwner->TryChooseEvolution(EAuraPlayerSkill::ArcaneShards,1,Failure);
		GrowthOwner->TryChooseEvolution(EAuraPlayerSkill::Shock,1,Failure);
	}
	Check(T,TEXT("AllPathsSnapshotValid"),AAuraPlayerState::IsValidGrowthSnapshot(GrowthOwner->GetGrowthState()));
	FString BalanceError;
	Check(T,TEXT("LoadedBalanceAssetValid"),UAuraCombatBalanceData::Get().Validate(BalanceError));
	AAuraPlayerState* MigratedOwner = W->SpawnActor<AAuraPlayerState>();
	FAuraGrowthState Legacy = GrowthOwner->GetGrowthState();
	Legacy.ComboRanks.Ember=1; Legacy.ComboRanks.Fracture=1; Legacy.ComboRanks.Overload=1;
	Legacy.UnspentComboPoints=0;
	const FAuraGrowthState Migrated=AAuraPlayerState::MigrateV5GrowthSnapshot(Legacy);
	Check(T,TEXT("ValidateV5BeforeMigration"),AAuraPlayerState::IsValidV5GrowthSnapshot(Legacy) && AAuraPlayerState::IsValidGrowthSnapshot(Migrated));
	MigratedOwner->RestoreGrowth(Migrated);
	const int32 Points=MigratedOwner->GetGrowthState().UnspentSkillPoints;
	Check(T,TEXT("FreeLegacyChoicesPreservePoints"),MigratedOwner->TryChooseEvolution(EAuraPlayerSkill::ArcaneShards,2,Failure)
		&& MigratedOwner->TryChooseEvolution(EAuraPlayerSkill::Shock,2,Failure)
		&& MigratedOwner->GetGrowthState().UnspentSkillPoints==Points
		&& !MigratedOwner->TryChooseEvolution(EAuraPlayerSkill::Shock,1,Failure));
	FAuraGrowthState Invalid=Migrated; Invalid.SkillRanks.bShockEvolutionPending=false;
	Check(T,TEXT("RejectMissingPathAndBadBudget"),!AAuraPlayerState::IsValidGrowthSnapshot(Invalid));
	Invalid=Legacy; Invalid.UnspentComboPoints=7;
	Check(T,TEXT("RejectBadV5ComboBudget"),!AAuraPlayerState::IsValidV5GrowthSnapshot(Invalid));
	AAuraEnemy* Normal=Spawn(Origin+FVector(400,0,0));
	AAuraEnemy* Elite=Spawn(Origin+FVector(400,700,0),true);
	AAuraEnemy* Expiry=Spawn(Origin+FVector(900,700,0));
	AAuraEnemy* Para=Spawn(Origin+FVector(-500,700,0));
	Normal->Tags.Add(TEXT("ElementalNormal")); Elite->Tags.Add(TEXT("ElementalElite"));
	Normal->GetElementalStatus()->ApplySlow(); Elite->GetElementalStatus()->ApplySlow();
	Check(T,TEXT("NormalEliteSlowResistance"),FMath::IsNearlyEqual(Normal->GetElementalStatus()->GetSlowFraction(),.30f)
		&& FMath::IsNearlyEqual(Elite->GetElementalStatus()->GetSlowFraction(),.15f));
	const int32 FireId=AuraPawn->AllocateComboCastId(), LightningId=AuraPawn->AllocateComboCastId();
	Normal->GetElementalStatus()->OnFireHit(AuraPawn,FireId,10);
	for(FConstPlayerControllerIterator It=W->GetPlayerControllerIterator();It;++It)
	{
		AAuraCharacter* Other=Cast<AAuraCharacter>(It->Get()->GetPawn());
		if(!Other || Other==AuraPawn) continue;
		const float Before=Health(Normal);
		Normal->GetElementalStatus()->OnLightningHit(Other,Other->AllocateComboCastId(),20);
		Check(T,TEXT("OtherPlayerCannotConsumePersonalEmber"),Health(Normal)==Before);
	}
	AAuraElementalSpell::ApplyQ(AuraPawn,Normal->GetActorLocation(),1,100,false);
	Check(T,TEXT("FireThenQDoesNotConsumeEmber"),Normal->GetElementalStatus()->GetStatus().bHasEmber);
	const float BeforeE=Health(Normal);
	Normal->GetElementalStatus()->OnLightningHit(AuraPawn,LightningId,20);
	const float AfterE=Health(Normal);
	Normal->GetElementalStatus()->OnFireHit(AuraPawn,AuraPawn->AllocateComboCastId(),10);
	Normal->GetElementalStatus()->OnLightningHit(AuraPawn,LightningId,20);
	Check(T,TEXT("LightningConsumesEmberOncePerCast"),AfterE<BeforeE && FMath::IsNearlyEqual(Health(Normal),AfterE));
	Para->GetElementalStatus()->OnLightningHit(AuraPawn,AuraPawn->AllocateComboCastId(),1);
	Check(T,TEXT("BaseLightningNoLongImmediateStun"),!Para->IsStunned() && Para->GetElementalStatus()->GetStatus().ParalysisUntil>0);
	AAuraEnemy* Lethal=Spawn(Origin+FVector(-3000,0,0));
	AAuraEnemy* LethalNear=Spawn(Origin+FVector(-3000,150,0));
	Lethal->GetAbilitySystemComponent()->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(),1);
	Lethal->GetElementalStatus()->OnFireHit(AuraPawn,AuraPawn->AllocateComboCastId(),1);
	AAuraElementalSpell::ApplyLightning(AuraPawn,Lethal,50,AuraPawn->AllocateComboCastId());
	Check(T,TEXT("LethalLightningStillDetonatesEmber"),Lethal->IsDead() && FMath::IsNearlyEqual(Health(LethalNear),9980,.1f));
	AAuraEnemy* Ignite=Spawn(Origin+FVector(-3000,1000,0));
	auto& TestBalance=const_cast<UAuraCombatBalanceData&>(UAuraCombatBalanceData::Get()).Elemental;
	const float OldChance=TestBalance.IgniteBaseChance;
	TestBalance.IgniteBaseChance=1.f; // Deterministic test fixture; restored before returning to gameplay.
	const int32 IgniteCast=AuraPawn->AllocateComboCastId();
	Ignite->GetElementalStatus()->OnFireHit(AuraPawn,IgniteCast,10);
	TestBalance.IgniteBaseChance=OldChance;
	const float IgniteExpiry=Ignite->GetElementalStatus()->GetStatus().IgniteUntil;
	Later(W,.5f,[T,AuraPawn,Ignite,IgniteCast,IgniteExpiry]()
	{
		Ignite->GetElementalStatus()->OnFireHit(AuraPawn,IgniteCast,1000);
		Check(T,TEXT("SameCastDoesNotRerollOrRefreshIgnite"),Ignite->GetElementalStatus()->GetStatus().IgniteUntil==IgniteExpiry);
	});
	Later(W,3.7f,[T,Ignite]() { Check(T,TEXT("IgniteThreeTicksAndExpires"),FMath::IsNearlyEqual(Health(Ignite),9994.6f,.15f)
		&& Ignite->GetElementalStatus()->GetStatus().IgniteUntil==0.f); });
	const bool Frozen=Elite->GetElementalStatus()->ApplyCrystal(AuraPawn,50);
	const bool FrozenExpiry=Expiry->GetElementalStatus()->ApplyCrystal(AuraPawn,50);
	const float InitialCrystal=Elite->GetElementalStatus()->GetStatus().CrystalHealth;
	for(int32 I=0; I<3; ++I)
	{
		UAuraElementalStatusComponent::DealDamage(AuraPawn,Elite,10);
		Elite->GetElementalStatus()->OnFireHit(AuraPawn,FireId,10);
	}
	Check(T,TEXT("ThreeSameCastOrbsDamageCrystalIndividually"),Frozen && FMath::IsNearlyEqual(InitialCrystal-Elite->GetElementalStatus()->GetStatus().CrystalHealth,30,.1f));
	Check(T,TEXT("EliteCrystalShorterAndCannotRefresh"),FrozenExpiry && Elite->GetElementalStatus()->GetStatus().CrystalUntil
		< Expiry->GetElementalStatus()->GetStatus().CrystalUntil && !Elite->GetElementalStatus()->ApplyCrystal(AuraPawn,50));
	const float PreSecondary=Elite->GetElementalStatus()->GetStatus().CrystalHealth;
	UAuraElementalStatusComponent::DealDamage(AuraPawn,Elite,1,true);
	Check(T,TEXT("SecondaryDamageDoesNotBreakCrystal"),FMath::IsNearlyEqual(Elite->GetElementalStatus()->GetStatus().CrystalHealth,PreSecondary));
	UAuraElementalStatusComponent::DealDamage(AuraPawn,Elite,PreSecondary+1);
	const float CrystalBreakHealth=Health(Elite);
	Later(W,.15f,[T,Elite,CrystalBreakHealth]() { Check(T,TEXT("CrystalExplodesExactlyOnce"),FMath::IsNearlyEqual(Health(Elite),CrystalBreakHealth-30.f,.1f)); });
	Check(T,TEXT("BrokenCrystalReleasesEarlyProtected"),!Elite->GetElementalStatus()->HasCrystal() && !Elite->IsStunned()
		&& !Elite->GetElementalStatus()->CanAcceptHardControl());
	const FTransform BossTransform(FRotator::ZeroRotator,Origin+FVector(3000,0,0));
	UClass* BossClass=LoadClass<AAuraEnemy>(nullptr,TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon.BP_AuraBossDemon_C"));
	AAuraEnemy* Boss=W->SpawnActorDeferred<AAuraEnemy>(BossClass,BossTransform,this,AuraPawn,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Boss->ConfigurePreplacedGuard(); UGameplayStatics::FinishSpawningActor(Boss,BossTransform);
	Boss->GetCharacterMovement()->DisableMovement();
	if(AAIController* AI=Cast<AAIController>(Boss->GetController())) AI->SetActorTickEnabled(false);
	Boss->GetElementalStatus()->ApplySlow();
	Check(T,TEXT("BossSlowAndNormalControlImmunity"),FMath::IsNearlyEqual(Boss->GetElementalStatus()->GetSlowFraction(),.08f)
		&& !Boss->GetElementalStatus()->ApplyCrystal(AuraPawn,50));
	Boss->ApplyPoiseDamage(10000);
	const float Window=Boss->GetPoiseBreakRemaining();
	const bool BossCrystal=Boss->GetElementalStatus()->ApplyCrystal(AuraPawn,50);
	Check(T,TEXT("BossCrystalDoesNotExtendBreakWindow"),BossCrystal && Window>0 && Boss->GetPoiseBreakRemaining()<=Window+.01f);
	Later(W,1.f,[T,Boss]() { Check(T,TEXT("BossRecoversFromCrystalWindow"),!Boss->IsStunned() && !Boss->GetElementalStatus()->HasCrystal()); });
	Later(W,1.32f,[T,Para]() { Check(T,TEXT("ParalysisActuallyPulses"),Para->IsStunned()); });
	Later(W,1.65f,[T,Para]() { Check(T,TEXT("ParalysisPulseEndsAndProtects"),!Para->IsStunned() && !Para->GetElementalStatus()->CanAcceptHardControl()); });
	const float ExpiryHealth=Health(Expiry);
	Later(W,3.7f,[T,Expiry,ExpiryHealth,Elite]()
	{
		Check(T,TEXT("NaturalCrystalEscapeNoExplosion"),!Expiry->IsStunned() && !Expiry->GetElementalStatus()->HasCrystal() && FMath::IsNearlyEqual(Health(Expiry),ExpiryHealth));
		Check(T,TEXT("SlowExpires"),FMath::IsNearlyZero(Elite->GetElementalStatus()->GetSlowFraction()));
	});
	AAuraEnemy* StormFront=Spawn(Origin+FVector(700,-900,0));
	AAuraEnemy* StormBack=Spawn(Origin+FVector(-400,-900,0));
	const FTransform StormTransform(FRotator::ZeroRotator,Origin+FVector(0,-900,0));
	AAuraElementalSpell* Storm=W->SpawnActorDeferred<AAuraElementalSpell>(AAuraElementalSpell::StaticClass(),StormTransform,AuraPawn,AuraPawn,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Storm->Configure(AuraPawn,EAuraElementalZone::Storm,100,950,AuraPawn->AllocateComboCastId());
	UGameplayStatics::FinishSpawningActor(Storm,StormTransform);
	Later(W,3.25f,[T,StormFront,StormBack]() {
		UE_LOG(LogTemp,Display,TEXT("AURA_ELEMENTAL_VALUES StormFront=%.2f StormBack=%.2f"),Health(StormFront),Health(StormBack));
		Check(T,TEXT("StormConeFivePulsesNoRearDamage"),FMath::IsNearlyEqual(Health(StormFront),9925,.1f) && Health(StormBack)==10000); });
	const FVector EchoOrigin=Origin+FVector(0,3000,-70);
	AAuraEnemy* Blocked=Spawn(EchoOrigin+FVector(405,0,70));
	AAuraEnemy* Open=Spawn(EchoOrigin+FVector(-405,0,70));
	AStaticMeshActor* Wall=W->SpawnActor<AStaticMeshActor>(EchoOrigin+FVector(200,0,100),FRotator::ZeroRotator,Params);
	Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
	Wall->SetActorScale3D(FVector(.5f,6,5)); Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	const FTransform EchoTransform(FRotator::ZeroRotator,EchoOrigin);
	AAuraElementalSpell* EchoZone=W->SpawnActorDeferred<AAuraElementalSpell>(AAuraElementalSpell::StaticClass(),EchoTransform,AuraPawn,AuraPawn,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	EchoZone->Configure(AuraPawn,EAuraElementalZone::Echo,100,300,AuraPawn->AllocateComboCastId());
	UGameplayStatics::FinishSpawningActor(EchoZone,EchoTransform);
	Later(W,2.f,[T,Blocked,Open]() { Check(T,TEXT("EchoWallOcclusionAndIndividualChildDamage"),Health(Blocked)==10000 && FMath::IsNearlyEqual(Health(Open),9955,.1f)); });
	const FVector JudgmentOrigin=Origin+FVector(-2500,-2500,0);
	TArray<AAuraEnemy*> UniqueTargets;
	for(float Angle:{0.f,120.f,240.f}) UniqueTargets.Add(Spawn(JudgmentOrigin+FVector(250,0,0).RotateAngleAxis(Angle,FVector::UpVector)));
	const FTransform JudgmentTransform(FRotator::ZeroRotator,JudgmentOrigin);
	AAuraElementalSpell* Judgment=W->SpawnActorDeferred<AAuraElementalSpell>(AAuraElementalSpell::StaticClass(),JudgmentTransform,AuraPawn,AuraPawn,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Judgment->Configure(AuraPawn,EAuraElementalZone::Judgment,100,300,AuraPawn->AllocateComboCastId());
	UGameplayStatics::FinishSpawningActor(Judgment,JudgmentTransform);
	Later(W,2.f,[T,UniqueTargets]()
	{
		bool Each=true; for(AAuraEnemy* E:UniqueTargets) Each&=FMath::IsNearlyEqual(Health(E),9950,.1f);
		Check(T,TEXT("JudgmentChoosesThreeFreshTargets"),Each);
	});
	// Exercise real GAS cast entry points, including commit and the 3rd-rank paths.
	AAuraEnemy* RealCrystal=Spawn(Origin+FVector(350,-250,0));
	Later(W,.45f,[this,T,AuraPawn,RealCrystal]()
	{
		ClearCooldowns(AuraPawn);
		if(!IsLocalController()) { ClientCastElementalSmoke(RealCrystal->GetActorLocation()-FVector(0,0,75),FVector::ForwardVector); return; }
		CommittedArcaneShardsTarget=RealCrystal->GetActorLocation()-FVector(0,0,75); bHasCommittedArcaneShardsTarget=true;
		const bool Q=AuraPawn->GetAbilitySystemComponent()->TryActivateAbilityByClass(UAuraArcaneShardsAbility::StaticClass());
		bHasCommittedArcaneShardsTarget=false;
		CommittedShockDirection=FVector::ForwardVector; bHasCommittedShockDirection=true;
		const bool E=AuraPawn->GetAbilitySystemComponent()->TryActivateAbilityByClass(UAuraShockAbility::StaticClass());
		bHasCommittedShockDirection=false;
		Check(T,TEXT("RealCrystalAndStormAbilityActivation"),Q && E);
	});
	Later(W,1.1f,[T,RealCrystal]() { Check(T,TEXT("RealQActuallyFreezesTarget"),RealCrystal->GetElementalStatus()->HasCrystal()); Capture(TEXT("AuraElemental_CrystalStorm.png")); });
	Later(W,4.1f,[this,T,W,AuraPawn,GrowthOwner,Spawn,Origin]()
	{
		FAuraSkillRankState R=GrowthOwner->GetGrowthState().SkillRanks;
		R.ArcanePath=EAuraArcanePath::CrystalEcho; R.ShockPath=EAuraShockPath::ThunderJudgment;
		FString Failure;
		Check(T,TEXT("RespecAllPathsWithoutLosingRanks"),GrowthOwner->TryRespecSkills(R,FGuid(),Failure)
			&& GrowthOwner->GetGrowthState().SkillRanks.ArcanePath==EAuraArcanePath::CrystalEcho
			&& GrowthOwner->GetGrowthState().SkillRanks.ShockPath==EAuraShockPath::ThunderJudgment);
		AAuraEnemy* Echo=Spawn(Origin+FVector(250,0,0));
		AAuraEnemy* FarEcho=Spawn(Origin+FVector(1250,0,0));
		AAuraEnemy* Strike=Spawn(Origin+FVector(350,-450,0));
		ClearCooldowns(AuraPawn);
		if(!IsLocalController())
		{
			Later(W,.3f,[this,Origin]() { ClientCastElementalSmoke(Origin+FVector(250,0,-70),Origin+FVector(350,-450,-70)); });
		}
		else
		{
		CommittedArcaneShardsTarget=Origin+FVector(250,0,-70); bHasCommittedArcaneShardsTarget=true;
		const bool Q=AuraPawn->GetAbilitySystemComponent()->TryActivateAbilityByClass(UAuraArcaneShardsAbility::StaticClass());
		bHasCommittedArcaneShardsTarget=false;
		CommittedShockDirection=Origin+FVector(350,-450,-70); bHasCommittedShockDirection=true;
		const bool E=AuraPawn->GetAbilitySystemComponent()->TryActivateAbilityByClass(UAuraShockAbility::StaticClass());
		bHasCommittedShockDirection=false;
		Check(T,TEXT("RealEchoAndJudgmentTargetData"),Q && E);
		}
		Later(W,2.2f,[T,Echo,FarEcho,Strike]()
		{
			UE_LOG(LogTemp,Display,TEXT("AURA_ELEMENTAL_VALUES Echo=%.2f FarEcho=%.2f Strike=%.2f"),Health(Echo),Health(FarEcho),Health(Strike));
			Check(T,TEXT("EchoExpandsBeyondInitialArea"),Health(Echo)<10000 && Health(FarEcho)<10000);
			Check(T,TEXT("JudgmentActuallyStrikesSelectedArea"),Health(Strike)<10000 && Strike->GetElementalStatus()->GetStatus().ParalysisUntil>0);
		});
		Later(W,.7f,[]() { Capture(TEXT("AuraElemental_EchoJudgment.png")); });
	});
	Later(W,10.f,[this,T,W]()
	{
		int32 Zones=0; for(TActorIterator<AAuraElementalSpell> It(W);It;++It) ++Zones;
		Check(T,TEXT("AllBoundedZonesCleanedUp"),Zones==0);
		UE_LOG(LogTemp,Display,TEXT("AURA_ELEMENTAL_RESULT Passed=%s Checks=%d"),T->bPassed?TEXT("true"):TEXT("false"),T->Checks);
		UE_LOG(LogTemp,Display,TEXT("AURA_ELEMENTAL_DONE"));
	});
}

void AAuraPlayerController::AuraObserveElementalSkills()
{
	int32 Statuses=0,Zones=0,Crystals=0;
	for(TActorIterator<AAuraEnemy> It(GetWorld());It;++It)
	{
		const auto& S=It->GetElementalStatus()->GetStatus();
		if(S.ParalysisUntil>0 || S.SlowUntil>0) ++Statuses;
		if(S.CrystalHealth>0) ++Crystals;
	}
	for(TActorIterator<AAuraElementalSpell> It(GetWorld());It;++It) ++Zones;
	bool HasPaths=false;
	for(TActorIterator<AAuraCharacter> It(GetWorld());It;++It)
		HasPaths|=It->GetSkillRankState().ArcanePath==EAuraArcanePath::CrystalPrison && It->GetSkillRankState().ShockPath==EAuraShockPath::StormField;
	const bool Passed=Statuses>0 && Zones>0 && HasPaths;
	if(!Passed && GetWorld()->GetTimeSeconds()<35.f)
	{
		const TWeakObjectPtr<AAuraPlayerController> WeakThis(this);
		Later(GetWorld(),.2f,[WeakThis]() { if(WeakThis.IsValid()) WeakThis->AuraObserveElementalSkills(); });
		return;
	}
	UE_LOG(LogTemp,Display,TEXT("AURA_ELEMENTAL_CLIENT Passed=%s Statuses=%d Zones=%d Crystals=%d"),
		Passed ? TEXT("true"):TEXT("false"),Statuses,Zones,Crystals);
}

void AAuraPlayerController::ClientCastElementalSmoke_Implementation(const FVector QTarget,const FVector ETarget)
{
	if(!FParse::Param(FCommandLine::Get(),TEXT("AuraElementalObserve"))) return;
	AAuraCharacter* AuraPawn=Cast<AAuraCharacter>(GetPawn());
	if(!AuraPawn || !AuraPawn->GetAbilitySystemComponent()) return;
	CommittedArcaneShardsTarget=QTarget; bHasCommittedArcaneShardsTarget=true;
	const bool Q=AuraPawn->GetAbilitySystemComponent()->TryActivateAbilityByClass(UAuraArcaneShardsAbility::StaticClass());
	bHasCommittedArcaneShardsTarget=false;
	CommittedShockDirection=ETarget; bHasCommittedShockDirection=true;
	const bool E=AuraPawn->GetAbilitySystemComponent()->TryActivateAbilityByClass(UAuraShockAbility::StaticClass());
	bHasCommittedShockDirection=false;
	UE_LOG(LogTemp,Display,TEXT("AURA_ELEMENTAL_CLIENT_CAST Q=%s E=%s"),Q?TEXT("true"):TEXT("false"),E?TEXT("true"):TEXT("false"));
}
