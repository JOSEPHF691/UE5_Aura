// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Level/AuraEncounterVolume.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraBossWardSmoke, Log, All);

void AAuraPlayerController::AuraTestBossWard()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!HasAuthority() || !IsLocalController() || !GetWorld() || !AuraPawn
		|| !AuraPawn->GetAbilitySystemComponent())
	{
		UE_LOG(LogAuraBossWardSmoke, Error,
			TEXT("AURA_BOSS_WARD Passed=false Setup=missing authoritative player"));
		return;
	}
	UClass* BossClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon.BP_AuraBossDemon_C"));
	if (!BossClass)
	{
		UE_LOG(LogAuraBossWardSmoke, Error,
			TEXT("AURA_BOSS_WARD Passed=false Setup=missing boss class"));
		return;
	}
	const bool bCapture = FParse::Param(FCommandLine::Get(), TEXT("AuraBossWardCapture"));
	FVector TestBossLocation = AuraPawn->GetActorLocation() + FVector(0.f, 650.f, 200.f);
	float CaptureFloorZ = 0.f;
	if (bCapture)
	{
		// Use the existing boss arena for visual QA; the starting stairwell has
		// walls between the test pawn and any nearby synthetic boss spawn.
		for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
		{
			if (It->IsFinalEncounter())
			{
				CaptureFloorZ = It->GetActorLocation().Z;
				// The authored encounter is at the south threshold; its actual boss
				// spawn sits roughly 900 uu deeper into the room.
				TestBossLocation = It->GetActorLocation() + FVector(0.f, 900.f, 200.f);
				break;
			}
		}
	}
	const FTransform Transform(FRotator::ZeroRotator, TestBossLocation);
	AAuraEnemy* Boss = GetWorld()->SpawnActorDeferred<AAuraEnemy>(BossClass,
		Transform, this, AuraPawn, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Boss)
	{
		UE_LOG(LogAuraBossWardSmoke, Error,
			TEXT("AURA_BOSS_WARD Passed=false Setup=spawn failed"));
		return;
	}
	Boss->ConfigurePreplacedGuard();
	UGameplayStatics::FinishSpawningActor(Boss, Transform);
	if (bCapture)
	{
		Boss->SetActorScale3D(FVector(2.5f));
		const float CapsuleHalfHeight = Boss->GetCapsuleComponent()
			? Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 200.f;
		FVector GroundedLocation = Boss->GetActorLocation();
		GroundedLocation.Z = CaptureFloorZ + CapsuleHalfHeight;
		Boss->SetActorLocation(GroundedLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	UAbilitySystemComponent* BossASC = Boss->GetAbilitySystemComponent();
	if (!BossASC)
	{
		UE_LOG(LogAuraBossWardSmoke, Error,
			TEXT("AURA_BOSS_WARD Passed=false Setup=missing boss ASC"));
		return;
	}

	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	const float InitialHealth = BossASC->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	const float Armor = BossASC->GetNumericAttribute(UAuraAttributeSet::GetArmorAttribute());
	const bool bStats = FMath::IsNearlyEqual(InitialHealth, Balance.MaxHealth, 0.1f)
		&& FMath::IsNearlyEqual(Armor, Balance.Armor, 0.1f);
	Boss->ActivateBossWard();
	const bool bPhaseGuard = !Boss->HasBossWard();

	const TWeakObjectPtr<AAuraCharacter> WeakPlayer(AuraPawn);
	const TWeakObjectPtr<AAuraEnemy> WeakBoss(Boss);
	const auto Hit = [WeakPlayer, WeakBoss](const float RawDamage)
	{
		AAuraCharacter* Source = WeakPlayer.Get();
		AAuraEnemy* Target = WeakBoss.Get();
		UAbilitySystemComponent* SourceASC = Source
			? Source->GetAbilitySystemComponent() : nullptr;
		UAbilitySystemComponent* TargetASC = Target
			? Target->GetAbilitySystemComponent() : nullptr;
		if (!SourceASC || !TargetASC) return false;
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddSourceObject(Source);
		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
		if (!Spec.IsValid()) return false;
		Spec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -RawDamage);
		TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		return true;
	};
	const bool bTransitionHit = Hit(10000.f);
	const bool bTransition = Boss->IsBossTransitioning()
		&& FMath::IsNearlyEqual(BossASC->GetNumericAttribute(
			UAuraAttributeSet::GetHealthAttribute()),
			InitialHealth * Balance.PhaseTwoHealthThreshold, 0.1f);

	FTimerHandle WardCheckTimer;
	GetWorldTimerManager().SetTimer(WardCheckTimer,
		[this, WeakBoss, Hit, bStats, bPhaseGuard, bTransitionHit, bTransition, Balance]()
	{
		AAuraEnemy* CurrentBoss = WeakBoss.Get();
		UAbilitySystemComponent* ASC = CurrentBoss
			? CurrentBoss->GetAbilitySystemComponent() : nullptr;
		if (!CurrentBoss || !ASC)
		{
			UE_LOG(LogAuraBossWardSmoke, Error,
				TEXT("AURA_BOSS_WARD Passed=false Setup=boss lost"));
			return;
		}
		const bool bPhaseTwo = CurrentBoss->GetBossPhase() == EAuraBossPhase::PhaseTwo;
		const float HealthBefore = ASC->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
		CurrentBoss->ActivateBossWard();
		const float Capacity = CurrentBoss->GetBossWardCapacity();
		const bool bActivated = bPhaseTwo && CurrentBoss->HasBossWard()
			&& FMath::IsNearlyEqual(Capacity,
				Balance.MaxHealth * Balance.WardHealthFraction, 0.2f);
		const float FirstHit = UAuraAttributeSet::CalculateMitigatedDamage(40.f, Balance.Armor);
		const bool bHitOne = Hit(40.f);
		const bool bAbsorbed = bHitOne
			&& FMath::IsNearlyEqual(CurrentBoss->GetBossWardHealth(), Capacity - FirstHit, 0.2f)
			&& FMath::IsNearlyEqual(ASC->GetNumericAttribute(
				UAuraAttributeSet::GetHealthAttribute()), HealthBefore, 0.2f);
		const float SecondHit = UAuraAttributeSet::CalculateMitigatedDamage(100.f, Balance.Armor);
		const float ExpectedOverflow = SecondHit - (Capacity - FirstHit);
		const bool bHitTwo = Hit(100.f);
		const bool bOverflow = bHitTwo && !CurrentBoss->HasBossWard()
			&& FMath::IsNearlyEqual(ASC->GetNumericAttribute(
				UAuraAttributeSet::GetHealthAttribute()), HealthBefore - ExpectedOverflow, 0.2f);
		CurrentBoss->ActivateBossWard();
		const bool bRecastForExpiry = CurrentBoss->HasBossWard();
		if (bRecastForExpiry
			&& FParse::Param(FCommandLine::Get(), TEXT("AuraBossWardCapture")))
		{
			const FVector Focus = CurrentBoss->GetMesh()->Bounds.Origin
				+ FVector(0.f, 0.f, 30.f);
			// The arena extends north of its encounter center. Shoot from inside it,
			// not through the south/west perimeter wall.
			const FVector CameraLocation = Focus + FVector(260.f, -460.f, 320.f);
			ACameraActor* DetailCamera = GetWorld()->SpawnActor<ACameraActor>(
				CameraLocation, (Focus - CameraLocation).Rotation());
			if (DetailCamera)
			{
				DetailCamera->GetCameraComponent()->SetFieldOfView(55.f);
				SetViewTarget(DetailCamera);
			}
			UStaticMeshComponent* WardShell = nullptr;
			TArray<UStaticMeshComponent*> StaticMeshes;
			CurrentBoss->GetComponents<UStaticMeshComponent>(StaticMeshes);
			for (UStaticMeshComponent* Component : StaticMeshes)
			{
				if (Component && Component->GetFName() == FName(TEXT("BossWardShell")))
				{
					WardShell = Component;
					break;
				}
			}
			UE_LOG(LogAuraBossWardSmoke, Display,
				TEXT("Boss ward capture geometry. Actor=%s Capsule=%s MeshCenter=%s WardCenter=%s WardExtent=%s Camera=%s Focus=%s"),
				*CurrentBoss->GetActorLocation().ToCompactString(),
				*CurrentBoss->GetCapsuleComponent()->GetComponentLocation().ToCompactString(),
				*CurrentBoss->GetMesh()->Bounds.Origin.ToCompactString(),
				WardShell ? *WardShell->GetComponentLocation().ToCompactString() : TEXT("none"),
				WardShell ? *WardShell->Bounds.BoxExtent.ToCompactString() : TEXT("none"),
				DetailCamera ? *DetailCamera->GetActorLocation().ToCompactString() : TEXT("none"),
				*Focus.ToCompactString());
			const FString ScreenshotDirectory = FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
			IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
			FTimerHandle CaptureTimer;
			GetWorldTimerManager().SetTimer(CaptureTimer, [ScreenshotDirectory]()
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(
					ScreenshotDirectory, TEXT("AuraBossWard.png")), false, false);
			}, 0.7f, false);
		}
		UE_LOG(LogAuraBossWardSmoke, Display,
			TEXT("Boss ward smoke intermediate. Stats=%s PhaseGuard=%s Transition=%s PhaseTwo=%s Activated=%s Absorbed=%s Overflow=%s Recast=%s"),
			bStats ? TEXT("true") : TEXT("false"),
			bPhaseGuard ? TEXT("true") : TEXT("false"),
			(bTransitionHit && bTransition) ? TEXT("true") : TEXT("false"),
			bPhaseTwo ? TEXT("true") : TEXT("false"),
			bActivated ? TEXT("true") : TEXT("false"),
			bAbsorbed ? TEXT("true") : TEXT("false"),
			bOverflow ? TEXT("true") : TEXT("false"),
			bRecastForExpiry ? TEXT("true") : TEXT("false"));

		FTimerHandle ExpiryCheckTimer;
		GetWorldTimerManager().SetTimer(ExpiryCheckTimer,
			[WeakBoss, Hit, bStats, bPhaseGuard, bTransitionHit, bTransition,
				bPhaseTwo, bActivated, bAbsorbed, bOverflow, bRecastForExpiry]()
		{
			AAuraEnemy* ExpiredBoss = WeakBoss.Get();
			UAbilitySystemComponent* ExpiredASC = ExpiredBoss
				? ExpiredBoss->GetAbilitySystemComponent() : nullptr;
			const bool bExpired = ExpiredBoss && ExpiredASC && !ExpiredBoss->HasBossWard();
			bool bUnshieldedDamage = false;
			bool bDeathCleanup = false;
			if (ExpiredBoss && ExpiredASC)
			{
				const float Before = ExpiredASC->GetNumericAttribute(
					UAuraAttributeSet::GetHealthAttribute());
				bUnshieldedDamage = Hit(10.f)
					&& ExpiredASC->GetNumericAttribute(
						UAuraAttributeSet::GetHealthAttribute()) < Before;
				ExpiredBoss->ActivateBossWard();
				bDeathCleanup = Hit(10000.f) && ExpiredBoss->IsDead()
					&& !ExpiredBoss->HasBossWard();
			}
			const bool bPassed = bStats && bPhaseGuard && bTransitionHit && bTransition
				&& bPhaseTwo && bActivated && bAbsorbed && bOverflow
				&& bRecastForExpiry && bExpired && bUnshieldedDamage && bDeathCleanup;
			UE_LOG(LogAuraBossWardSmoke, Display,
				TEXT("AURA_BOSS_WARD Passed=%s Expired=%s UnshieldedDamage=%s DeathCleanup=%s"),
				bPassed ? TEXT("true") : TEXT("false"),
				bExpired ? TEXT("true") : TEXT("false"),
				bUnshieldedDamage ? TEXT("true") : TEXT("false"),
				bDeathCleanup ? TEXT("true") : TEXT("false"));
		}, Balance.WardDuration + 0.25f, false);
	}, Balance.PhaseTransitionDuration + 0.25f, false);
}
