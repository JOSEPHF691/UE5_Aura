// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AI/AuraAIController.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "UI/AuraHUD.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraEnemySkillVFXSmoke, Log, All);

namespace
{
	const TArray<FName>& GetExpectedEnemySkillVFXAssets()
	{
		static const TArray<FName> Assets =
		{
			TEXT("NS_Free_Spells_Aura_Notes"),
			TEXT("NS_Free_Spells_Buff_Air"),
			TEXT("NS_Free_Spells_Aura_Fire"),
			TEXT("NS_Free_Spells_Explosion"),
			TEXT("NS_Free_Spells_Buff_Fire")
		};
		return Assets;
	}

	int32 CollectActiveEnemySkillVFX(UWorld* World, TSet<FName>* OutObserved = nullptr,
		const bool bActiveOnly = false)
	{
		if (!World) return 0;
		const TArray<FName>& Expected = GetExpectedEnemySkillVFXAssets();
		int32 ActiveCount = 0;
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			const UNiagaraComponent* Component = *It;
			if (!Component || Component->IsTemplate() || Component->GetWorld() != World
				|| !Component->GetAsset())
			{
				continue;
			}
			if (bActiveOnly && !Component->IsActive()) continue;
			const FName AssetName = Component->GetAsset()->GetFName();
			if (!Expected.Contains(AssetName)) continue;
			++ActiveCount;
			if (OutObserved) OutObserved->Add(AssetName);
		}
		return ActiveCount;
	}

	FVector GetEnemyFeetLocation(const AAuraEnemy* Enemy)
	{
		if (!Enemy) return FVector::ZeroVector;
		const float HalfHeight = Enemy->GetCapsuleComponent()
			? Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		return Enemy->GetActorLocation() - FVector(0.f, 0.f, HalfHeight) + FVector(0.f, 0.f, 4.f);
	}

	struct FEnemySkillVFXSmokeState
	{
		TWeakObjectPtr<AAuraEnemy> Goblin;
		TWeakObjectPtr<AAuraEnemy> Shroom;
		TWeakObjectPtr<AAuraEnemy> Shaman;
		TWeakObjectPtr<AAuraEnemy> Boss;
		TSet<FName> ObservedAssets;
		bool bSpawned = false;
		bool bAssetsConfigured = false;
		bool bTexturesConfigured = false;
	};

	int32 GetSpawnedCueCount(const FEnemySkillVFXSmokeState& State)
	{
		int32 Count = 0;
		if (State.Goblin.IsValid()) Count += State.Goblin->GetLocalSkillVFXSpawnCount();
		if (State.Shroom.IsValid()) Count += State.Shroom->GetLocalSkillVFXSpawnCount();
		if (State.Shaman.IsValid()) Count += State.Shaman->GetLocalSkillVFXSpawnCount();
		if (State.Boss.IsValid()) Count += State.Boss->GetLocalSkillVFXSpawnCount();
		return Count;
	}
}

void AAuraPlayerController::AuraTestEnemySkillVFX()
{
	AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(GetPawn());
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsLocalController() || !PlayerCharacter || !World)
	{
		UE_LOG(LogAuraEnemySkillVFXSmoke, Error,
			TEXT("AURA_ENEMY_SKILL_VFX_RESULT Passed=false Setup=false"));
		return;
	}

	UClass* GoblinClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear.BP_AuraGoblinSpear_C"));
	UClass* ShroomClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom.BP_AuraShroom_C"));
	UClass* ShamanClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman.BP_AuraEliteShaman_C"));
	UClass* BossClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon.BP_AuraBossDemon_C"));
	if (!GoblinClass || !ShroomClass || !ShamanClass || !BossClass)
	{
		UE_LOG(LogAuraEnemySkillVFXSmoke, Error,
			TEXT("AURA_ENEMY_SKILL_VFX_RESULT Passed=false Classes=false"));
		return;
	}

	SetAuraCameraMode(EAuraCameraMode::TopDown);
	PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
	const FVector Forward = PlayerCharacter->GetActorForwardVector().GetSafeNormal2D();
	const FVector SafeForward = Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, SafeForward).GetSafeNormal();
	const FVector Center = PlayerCharacter->GetActorLocation();

	auto SpawnProbe = [this, World, PlayerCharacter, Center](UClass* EnemyClass,
		const FVector& Offset, const bool bElite) -> AAuraEnemy*
	{
		const AAuraEnemy* Defaults = EnemyClass
			? EnemyClass->GetDefaultObject<AAuraEnemy>() : nullptr;
		const float HalfHeight = Defaults && Defaults->GetCapsuleComponent()
			? Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 88.f;
		FVector DesiredGround = Center + Offset;
		FHitResult GroundHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraEnemySkillVFXGround), false);
		QueryParams.AddIgnoredActor(PlayerCharacter);
		if (World->LineTraceSingleByChannel(GroundHit,
			DesiredGround + FVector(0.f, 0.f, 450.f),
			DesiredGround - FVector(0.f, 0.f, 700.f), ECC_Visibility, QueryParams))
		{
			DesiredGround = GroundHit.ImpactPoint;
		}
		const FVector SpawnLocation = DesiredGround + FVector(0.f, 0.f, HalfHeight + 2.f);
		const FVector FaceDirection = (Center - SpawnLocation).GetSafeNormal2D();
		const FTransform SpawnTransform(
			FaceDirection.IsNearlyZero() ? FRotator::ZeroRotator : FaceDirection.Rotation(),
			SpawnLocation);
		AAuraEnemy* Enemy = World->SpawnActorDeferred<AAuraEnemy>(EnemyClass,
			SpawnTransform, this, PlayerCharacter,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Enemy) return nullptr;
		Enemy->ConfigurePreplacedGuard();
		if (bElite) Enemy->ConfigureEliteVariant(FVector(1.18f));
		UGameplayStatics::FinishSpawningActor(Enemy, SpawnTransform);
		Enemy->SetEncounterDormant(true);
		Enemy->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Enemy->GetCharacterMovement()->StopMovementImmediately();
		Enemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AIController = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AIController->StopMovement();
			AIController->SetActorTickEnabled(false);
		}
		Enemy->SetLifeSpan(8.f);
		return Enemy;
	};

	const TSharedRef<FEnemySkillVFXSmokeState> State =
		MakeShared<FEnemySkillVFXSmokeState>();
	State->Goblin = SpawnProbe(GoblinClass, SafeForward * 230.f - Right * 310.f, true);
	State->Shroom = SpawnProbe(ShroomClass, SafeForward * 230.f + Right * 310.f, true);
	State->Shaman = SpawnProbe(ShamanClass, -SafeForward * 230.f - Right * 310.f, false);
	State->Boss = SpawnProbe(BossClass, -SafeForward * 300.f + Right * 360.f, false);
	State->bSpawned = State->Goblin.IsValid() && State->Shroom.IsValid()
		&& State->Shaman.IsValid() && State->Boss.IsValid();
	State->bAssetsConfigured = State->bSpawned
		&& State->Goblin->AreEnemySkillEffectsConfigured()
		&& State->Shroom->AreEnemySkillEffectsConfigured()
		&& State->Shaman->AreEnemySkillEffectsConfigured()
		&& State->Boss->AreEnemySkillEffectsConfigured();
	const AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	State->bTexturesConfigured = AuraHUD && AuraHUD->AreCombatCueTexturesConfigured();
	if (!State->bSpawned || !State->bAssetsConfigured || !State->bTexturesConfigured)
	{
		UE_LOG(LogAuraEnemySkillVFXSmoke, Error,
			TEXT("AURA_ENEMY_SKILL_VFX_RESULT Passed=false Spawned=%s Assets=%s Textures=%s"),
			State->bSpawned ? TEXT("true") : TEXT("false"),
			State->bAssetsConfigured ? TEXT("true") : TEXT("false"),
			State->bTexturesConfigured ? TEXT("true") : TEXT("false"));
		return;
	}

	const TFunction<void()> PlayGallery = [State, Center]()
	{
		AAuraEnemy* Goblin = State->Goblin.Get();
		AAuraEnemy* Shroom = State->Shroom.Get();
		AAuraEnemy* Shaman = State->Shaman.Get();
		AAuraEnemy* Boss = State->Boss.Get();
		if (!Goblin || !Shroom || !Shaman || !Boss) return;

		const FVector GoblinFeet = GetEnemyFeetLocation(Goblin);
		const FVector ShroomFeet = GetEnemyFeetLocation(Shroom);
		const FVector BossFeet = GetEnemyFeetLocation(Boss);
		Goblin->BeginMeleeAction(EAuraMeleeAction::GoblinLunge,
			GoblinFeet + Goblin->GetActorForwardVector() * 280.f, 75.f, 0.82f);
		Goblin->PlayMeleeTextureCue(EAuraMeleeAction::GoblinLunge,
			Goblin->GetActorLocation() + FVector(0.f, 0.f, 42.f),
			Goblin->GetActorForwardVector(), 0.45f);

		Shroom->BeginMeleeAction(EAuraMeleeAction::ShroomSporeBurst,
			ShroomFeet, 520.f, 1.1f);
		Shroom->ActivateEliteSporeField(ShroomFeet, 520.f, 0.4f, 7.f);
		Shroom->PlayMeleeTextureCue(EAuraMeleeAction::ShroomBash,
			ShroomFeet + Shroom->GetActorForwardVector() * 55.f,
			Shroom->GetActorForwardVector(), 0.4f);
		Shroom->PlayMeleeTextureCue(EAuraMeleeAction::ShroomSporeBurst,
			ShroomFeet, Shroom->GetActorForwardVector(), 0.5f);

		Shaman->BeginRangedCast(Center, false, true);
		Shaman->PlayShamanGroundBurstEffect(Center,
			UAuraCombatBalanceData::Get().Shaman.GroundBurstRadius);

		Boss->BeginBossAction(EAuraBossAction::InfernalBarrage,
			BossFeet + Boss->GetActorForwardVector() * 900.f, 900.f);
		Boss->PlayBossProjectileReleaseEffect(
			Boss->GetActorLocation() + FVector(0.f, 0.f, 145.f)
				+ Boss->GetActorForwardVector() * 115.f,
			Boss->GetActorForwardVector(), EAuraBossAction::InfernalBarrage);
		Boss->PlayBossImpactEffect(BossFeet + FVector(100.f, 0.f, 0.f),
			300.f, EAuraBossAction::DemonicWard);
	};
	PlayGallery();

	FTimerHandle ObserveTimer;
	GetWorldTimerManager().SetTimer(ObserveTimer, [World, State]()
	{
		const int32 ActiveCount = CollectActiveEnemySkillVFX(World, &State->ObservedAssets);
		UE_LOG(LogAuraEnemySkillVFXSmoke, Display,
			TEXT("AURA_ENEMY_SKILL_VFX_OBSERVED Unique=%d Components=%d Expected=%d SpawnedCues=%d"),
			State->ObservedAssets.Num(), ActiveCount, GetExpectedEnemySkillVFXAssets().Num(),
			GetSpawnedCueCount(*State));
	}, 0.03f, false);

	FTimerHandle ReplayTimer;
	GetWorldTimerManager().SetTimer(ReplayTimer, [PlayGallery]()
	{
		PlayGallery();
	}, 2.35f, false);

	const bool bCaptureRequested =
		FParse::Param(FCommandLine::Get(), TEXT("AuraEnemySkillVFXCapture"));
	if (bCaptureRequested)
	{
		const FString ScreenshotDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
		IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
		const auto QueueCapture = [this, ScreenshotDirectory](
			const TCHAR* Filename, const float Delay)
		{
			FTimerHandle CaptureTimer;
			GetWorldTimerManager().SetTimer(CaptureTimer,
				[ScreenshotDirectory, Filename = FString(Filename)]()
			{
				FScreenshotRequest::RequestScreenshot(
					FPaths::Combine(ScreenshotDirectory, Filename), false, false);
			}, Delay, false);
		};
		QueueCapture(TEXT("AuraEnemySkillVFX_005.png"), 2.40f);
		QueueCapture(TEXT("AuraEnemySkillVFX_033.png"), 2.72f);

		// After the stress gallery is cleared, capture each authored texture cue
		// in isolation so timing and ground perspective remain visually testable.
		FTimerHandle GoblinWindupTimer;
		GetWorldTimerManager().SetTimer(GoblinWindupTimer, [State]()
		{
			if (AAuraEnemy* Goblin = State->Goblin.Get())
			{
				const FVector Feet = GetEnemyFeetLocation(Goblin);
				Goblin->BeginMeleeAction(EAuraMeleeAction::GoblinJab,
					Feet + Goblin->GetActorForwardVector() * 230.f, 230.f, 0.65f);
			}
		}, 3.4f, false);
		QueueCapture(TEXT("AuraEnemyCue_GoblinWindup.png"), 3.57f);

		FTimerHandle GoblinStrikeTimer;
		GetWorldTimerManager().SetTimer(GoblinStrikeTimer, [State]()
		{
			if (AAuraEnemy* Goblin = State->Goblin.Get())
			{
				Goblin->ClearMeleeTelegraph();
				Goblin->PlayMeleeTextureCue(EAuraMeleeAction::GoblinJab,
					Goblin->GetActorLocation() + FVector(0.f, 0.f, 42.f),
					Goblin->GetActorForwardVector(), 0.28f);
			}
		}, 4.02f, false);
		QueueCapture(TEXT("AuraEnemyCue_GoblinStrike.png"), 4.14f);

		FTimerHandle ShroomWindupTimer;
		GetWorldTimerManager().SetTimer(ShroomWindupTimer, [State]()
		{
			if (State->Goblin.IsValid()) State->Goblin->EndMeleeAction();
			if (AAuraEnemy* Shroom = State->Shroom.Get())
			{
				const FVector Feet = GetEnemyFeetLocation(Shroom);
				Shroom->BeginMeleeAction(EAuraMeleeAction::ShroomSporeBurst,
					Feet, UAuraCombatBalanceData::Get().MeleeEnemy.ShroomSporeRadius,
					UAuraCombatBalanceData::Get().MeleeEnemy.ShroomSporeWindup);
			}
		}, 4.34f, false);
		QueueCapture(TEXT("AuraEnemyCue_ShroomFadeIn.png"), 4.53f);
		QueueCapture(TEXT("AuraEnemyCue_ShroomReady.png"), 5.17f);

		FTimerHandle ShroomBurstTimer;
		GetWorldTimerManager().SetTimer(ShroomBurstTimer, [State]()
		{
			if (AAuraEnemy* Shroom = State->Shroom.Get())
			{
				const FVector Feet = GetEnemyFeetLocation(Shroom);
				Shroom->ClearMeleeTelegraph();
				Shroom->PlayMeleeTextureCue(EAuraMeleeAction::ShroomSporeBurst,
					Feet, Shroom->GetActorForwardVector(), 0.26f);
			}
		}, 5.44f, false);
		QueueCapture(TEXT("AuraEnemyCue_ShroomBurst.png"), 5.55f);

		FTimerHandle ShroomCrackTimer;
		GetWorldTimerManager().SetTimer(ShroomCrackTimer, [State]()
		{
			if (AAuraEnemy* Shroom = State->Shroom.Get())
			{
				Shroom->EndMeleeAction();
				Shroom->PlayMeleeTextureCue(EAuraMeleeAction::ShroomBash,
					GetEnemyFeetLocation(Shroom)
						+ Shroom->GetActorForwardVector() * 70.f,
					Shroom->GetActorForwardVector(), 0.38f);
			}
		}, 5.78f, false);
		QueueCapture(TEXT("AuraEnemyCue_ShroomCrack.png"), 5.86f);

		FTimerHandle IsolatedCleanupTimer;
		GetWorldTimerManager().SetTimer(IsolatedCleanupTimer, [State]()
		{
			if (State->Goblin.IsValid()) State->Goblin->EndMeleeAction();
			if (State->Shroom.IsValid()) State->Shroom->EndMeleeAction();
		}, 6.3f, false);
	}

	FTimerHandle EndPresentationTimer;
	GetWorldTimerManager().SetTimer(EndPresentationTimer, [State]()
	{
		if (State->Goblin.IsValid()) State->Goblin->EndMeleeAction();
		if (State->Shroom.IsValid())
		{
			State->Shroom->EndMeleeAction();
			State->Shroom->ClearEliteSporeField();
		}
		if (State->Shaman.IsValid()) State->Shaman->EndRangedCast();
		if (State->Boss.IsValid()) State->Boss->EndBossAction();
	}, 3.25f, false);

	FTimerHandle ResultTimer;
	const bool bVisualObservationRequired =
		!FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
	GetWorldTimerManager().SetTimer(ResultTimer,
		[World, State, bVisualObservationRequired]()
	{
		const int32 RemainingActive = CollectActiveEnemySkillVFX(World, nullptr, true);
		const int32 SpawnedCues = GetSpawnedCueCount(*State);
		const bool bPassed = State->bSpawned && State->bAssetsConfigured
			&& State->bTexturesConfigured
			&& (!bVisualObservationRequired
				|| (SpawnedCues >= 14
					&& State->ObservedAssets.Num() == GetExpectedEnemySkillVFXAssets().Num()))
			&& RemainingActive == 0;
		UE_LOG(LogAuraEnemySkillVFXSmoke, Display,
			TEXT("AURA_ENEMY_SKILL_VFX_RESULT Passed=%s Spawned=%s Assets=%s Textures=%s VisualRequired=%s Observed=%d/%d Cues=%d Remaining=%d"),
			bPassed ? TEXT("true") : TEXT("false"),
			State->bSpawned ? TEXT("true") : TEXT("false"),
			State->bAssetsConfigured ? TEXT("true") : TEXT("false"),
			State->bTexturesConfigured ? TEXT("true") : TEXT("false"),
			bVisualObservationRequired ? TEXT("true") : TEXT("false"),
			State->ObservedAssets.Num(), GetExpectedEnemySkillVFXAssets().Num(),
			SpawnedCues, RemainingActive);
		for (const TWeakObjectPtr<AAuraEnemy>& Enemy :
			{ State->Goblin, State->Shroom, State->Shaman, State->Boss })
		{
			if (Enemy.IsValid()) Enemy->Destroy();
		}
	}, bCaptureRequested ? 6.65f : 5.55f, false);
}
