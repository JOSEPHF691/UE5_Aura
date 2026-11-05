// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AbilitySystem/Abilities/AuraFireBoltAbility.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraM24Smoke, Log, All);

void AAuraPlayerController::AuraTestTargetingPolish()
{
	AAuraCharacter* TestCharacter = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = TestCharacter
		? TestCharacter->GetAbilitySystemComponent() : nullptr;
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	if (!HasAuthority() || !IsLocalController() || GetNetMode() != NM_Standalone
		|| !TestCharacter || !AbilitySystem || !Run || Run->IsPersistentSaveEnabled())
	{
		UE_LOG(LogAuraM24Smoke, Error,
			TEXT("AURA_M24_TARGETING Passed=false Setup=false"));
		return;
	}

	const bool bInitiallyLocked = TestCharacter->GetSkillRank(EAuraPlayerSkill::ArcaneShards) == 0
		&& TestCharacter->GetSkillRank(EAuraPlayerSkill::Shock) == 0;
	// A half-body miss should lock, but a click beyond that margin must not.
	// Mouse-down snapshots remain authoritative if viewport capture moves the cursor.
	bool bDirectFireTarget = false;
	bool bHalfBodyAssistLocks = false;
	bool bBeyondMarginMiss = false;
	bool bClickSnapshot = false;
	bool bMissSnapshot = false;
	bool bHoverOutline = false;
	bool bNearHoverOutline = false;
	bool bHoverCleared = false;
	bool bVisibilityHitbox = false;
	if (UClass* GoblinClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear.BP_AuraGoblinSpear_C")))
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator,
			TestCharacter->GetActorLocation() + FVector(0.f, 0.f, 120.f));
		AAuraEnemy* TestEnemy = GetWorld()->SpawnActorDeferred<AAuraEnemy>(
			GoblinClass, SpawnTransform, this, TestCharacter,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (TestEnemy)
		{
			TestEnemy->ConfigurePreplacedGuard();
			UGameplayStatics::FinishSpawningActor(TestEnemy, SpawnTransform);
			bVisibilityHitbox = TestEnemy->GetCapsuleComponent()
				&& TestEnemy->GetCapsuleComponent()->GetCollisionResponseToChannel(
					ECC_Visibility) == ECR_Block;
			bUseDebugAimOverride = true;
			DebugAimOverride = TestEnemy->GetActorLocation();
			DebugAimActorOverride = TestEnemy;
			bDirectFireTarget = GetFireBoltCursorTarget() == TestEnemy;
			UpdateEnemyHoverHighlight();
			bHoverOutline = EnemyHoverOutlineMaterial && TestEnemy->GetMesh()
				&& TestEnemy->GetMesh()->GetOverlayMaterial() == EnemyHoverOutlineMaterial;
			const float PlayerRadius = TestCharacter->GetCapsuleComponent()
				->GetScaledCapsuleRadius();
			const float EnemyRadius = TestEnemy->GetCapsuleComponent()
				->GetScaledCapsuleRadius();
			FVector GroundPoint = TestEnemy->GetActorLocation();
			GroundPoint.Z -= TestEnemy->GetCapsuleComponent()
				->GetScaledCapsuleHalfHeight();
			DebugAimActorOverride.Reset();
			DebugAimOverride = GroundPoint
				+ FVector(EnemyRadius + PlayerRadius * 0.5f, 0.f, 0.f);
			bHalfBodyAssistLocks = GetFireBoltCursorTarget() == TestEnemy;
			UpdateEnemyHoverHighlight();
			bNearHoverOutline = EnemyHoverOutlineMaterial && TestEnemy->GetMesh()
				&& TestEnemy->GetMesh()->GetOverlayMaterial() == EnemyHoverOutlineMaterial;
			PendingFireBoltClickTarget = GetFireBoltCursorTarget();
			PendingFireBoltClickExpiry = GetWorld()->GetTimeSeconds() + 0.2f;
			bHasPendingFireBoltClick = true;
			DebugAimOverride = GroundPoint
				+ FVector(EnemyRadius + PlayerRadius + 60.f, 0.f, 0.f);
			bBeyondMarginMiss = GetFireBoltCursorTarget() == nullptr;
			bClickSnapshot = ConsumeFireBoltClickTarget() == TestEnemy;
			UpdateEnemyHoverHighlight();
			bHoverCleared = TestEnemy->GetMesh()
				&& TestEnemy->GetMesh()->GetOverlayMaterial() != EnemyHoverOutlineMaterial;
			PendingFireBoltClickTarget.Reset();
			PendingFireBoltClickExpiry = GetWorld()->GetTimeSeconds() + 0.2f;
			bHasPendingFireBoltClick = true;
			DebugAimActorOverride = TestEnemy;
			bMissSnapshot = ConsumeFireBoltClickTarget() == nullptr;
			ClearEnemyHoverHighlight();
			TestEnemy->Destroy();
		}
	}
	bUseDebugAimOverride = false;
	DebugAimActorOverride.Reset();
	PendingFireBoltClickTarget.Reset();
	bHasPendingFireBoltClick = false;
	ToggleArcaneShardsTargeting();
	const bool bQLocked = !bIsArcaneShardsTargeting;
	ToggleShockTargeting();
	const bool bELocked = !bIsShockTargeting;

	const bool bUnlocked = TestCharacter->SetSkillRank(EAuraPlayerSkill::ArcaneShards, 1)
		&& TestCharacter->SetSkillRank(EAuraPlayerSkill::Shock, 1);
	const float MaxMana = AbilitySystem->GetNumericAttribute(
		UAuraAttributeSet::GetMaxManaAttribute());
	AbilitySystem->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), MaxMana);
	bUseDebugGroundTargetOverride = true;
	const FVector PawnLocation = TestCharacter->GetActorLocation();
	DebugGroundTargetOverride = PawnLocation + FVector(250.f, 0.f, 0.f);
	ToggleArcaneShardsTargeting();
	const bool bQAimStarted = bIsArcaneShardsTargeting && bArcaneShardsPreviewValid;
	bForceInvalidGroundTargetForSmoke = true;
	UpdateArcaneShardsTargeting();
	ConfirmArcaneShardsTargeting();
	const bool bQRetried = bIsArcaneShardsTargeting && !bHasCommittedArcaneShardsTarget;
	bForceInvalidGroundTargetForSmoke = false;
	UpdateArcaneShardsTargeting();
	const FVector QPreviewLocation = ArcaneShardsPreviewLocation;
	DebugGroundTargetOverride = PawnLocation + FVector(0.f, 250.f, 0.f);
	ConfirmArcaneShardsTargeting();
	const bool bQConfirmed = !bIsArcaneShardsTargeting
		&& FVector::DistSquared2D(CommittedArcaneShardsTarget, QPreviewLocation) < 1.f;

	DebugGroundTargetOverride = PawnLocation + FVector(0.f, 250.f, 0.f);
	ToggleShockTargeting();
	const bool bEAimStarted = bIsShockTargeting && bShockPreviewValid;
	bForceInvalidGroundTargetForSmoke = true;
	UpdateShockTargeting();
	ConfirmShockTargeting();
	const bool bERetried = bIsShockTargeting && !bHasCommittedShockDirection;
	bForceInvalidGroundTargetForSmoke = false;
	UpdateShockTargeting();
	const FVector PreviewDirection = ShockPreviewDirection.GetSafeNormal2D();
	// A viewport capture can move the raw cursor during LMB. The cast must use
	// the last previewed direction, not a fresh sample from this shifted target.
	DebugGroundTargetOverride = PawnLocation + FVector(250.f, 0.f, 0.f);
	ConfirmShockTargeting();
	const bool bEDirectionPreserved = !bIsShockTargeting
		&& FVector::DotProduct(CommittedShockDirection, PreviewDirection) > 0.999f;

	bForceInvalidGroundTargetForSmoke = false;
	bUseDebugGroundTargetOverride = false;
	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	if (bIsShockTargeting) CancelShockTargeting();
	const bool bPassed = bInitiallyLocked && bQLocked && bELocked && bUnlocked
		&& bQAimStarted && bQRetried && bQConfirmed && bEAimStarted
		&& bERetried && bEDirectionPreserved && bDirectFireTarget
		&& bHalfBodyAssistLocks && bBeyondMarginMiss
		&& bClickSnapshot && bMissSnapshot && bHoverOutline
		&& bNearHoverOutline && bHoverCleared && bVisibilityHitbox;
	UE_LOG(LogAuraM24Smoke, Display,
		TEXT("AURA_M24_TARGETING Passed=%s Locked=%d QLock=%d ELock=%d Unlock=%d QAim=%d QRetry=%d QConfirm=%d EAim=%d ERetry=%d EDirection=%d FireDirect=%d HalfBodyAssist=%d BeyondMarginMiss=%d ClickSnapshot=%d MissSnapshot=%d RedOutline=%d NearOutline=%d OutlineCleared=%d VisibilityHitbox=%d"),
		bPassed ? TEXT("true") : TEXT("false"), bInitiallyLocked, bQLocked, bELocked,
		bUnlocked, bQAimStarted, bQRetried, bQConfirmed, bEAimStarted,
		bERetried, bEDirectionPreserved, bDirectFireTarget,
		bHalfBodyAssistLocks, bBeyondMarginMiss, bClickSnapshot, bMissSnapshot,
		bHoverOutline, bNearHoverOutline, bHoverCleared, bVisibilityHitbox);
}

void AAuraPlayerController::AuraTestFireBoltTurn()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* AbilitySystem = AuraPawn
		? AuraPawn->GetAbilitySystemComponent() : nullptr;
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	if (!HasAuthority() || !IsLocalController() || !GetWorld() || !AuraPawn
		|| !AbilitySystem || !Run || Run->IsPersistentSaveEnabled())
	{
		UE_LOG(LogAuraM24Smoke, Error,
			TEXT("AURA_FIREBOLT_TURN Passed=false Setup=false"));
		return;
	}
	const FTransform OriginalTransform = AuraPawn->GetActorTransform();
	UCharacterMovementComponent* Movement = AuraPawn->GetCharacterMovement();
	const EMovementMode OriginalMode = Movement->MovementMode;
	const float OriginalGravity = Movement->GravityScale;
	AuraPawn->TeleportTo(OriginalTransform.GetLocation() + FVector(0.f, 0.f, 2200.f),
		FRotator::ZeroRotator, false, true);
	AuraPawn->SetActorRotation(FRotator::ZeroRotator);
	Movement->GravityScale = 0.f;
	Movement->DisableMovement();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* Target = GetWorld()->SpawnActor<AAuraEnemy>(AAuraEnemy::StaticClass(),
		AuraPawn->GetActorLocation() + FVector(-420.f, 0.f, 0.f),
		FRotator::ZeroRotator, Spawn);
	if (!Target)
	{
		UE_LOG(LogAuraM24Smoke, Error,
			TEXT("AURA_FIREBOLT_TURN Passed=false Spawn=false"));
		AuraPawn->TeleportTo(OriginalTransform.GetLocation(),
			OriginalTransform.Rotator(), false, true);
		Movement->GravityScale = OriginalGravity;
		Movement->SetMovementMode(OriginalMode);
		return;
	}
	Target->GetCharacterMovement()->DisableMovement();
	bUseDebugAimOverride = true;
	DebugAimOverride = Target->GetActorLocation();
	DebugAimActorOverride = Target;
	bHasPendingFireBoltClick = false;
	const bool bActivated = AbilitySystem->TryActivateAbilityByClass(
		UAuraFireBoltAbility::StaticClass());
	const bool bTurnStarted = AuraPawn->IsFireBoltTurning();
	const bool bNoInstantSnap = FMath::Abs(AuraPawn->GetActorRotation().Yaw) < 1.f;
	bUseDebugAimOverride = false;
	DebugAimActorOverride.Reset();
	const TSharedRef<bool> bProgressed = MakeShared<bool>(false);
	const TWeakObjectPtr<AAuraCharacter> WeakPawn(AuraPawn);
	const TWeakObjectPtr<AAuraEnemy> WeakTarget(Target);
	FTimerHandle ProgressTimer;
	GetWorldTimerManager().SetTimer(ProgressTimer,
		[WeakPawn, bProgressed]()
	{
		if (const AAuraCharacter* CurrentPawn = WeakPawn.Get())
		{
			const float Turned = FMath::Abs(FMath::FindDeltaAngleDegrees(
				0.f, CurrentPawn->GetActorRotation().Yaw));
			const float Remaining = FMath::Abs(FMath::FindDeltaAngleDegrees(
				CurrentPawn->GetActorRotation().Yaw, 180.f));
			*bProgressed = Turned > 25.f && Remaining > 15.f;
		}
	}, 0.12f, false);
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, WeakPawn, WeakTarget, bProgressed, bActivated, bTurnStarted,
			bNoInstantSnap, OriginalTransform, OriginalMode, OriginalGravity]()
	{
		AAuraCharacter* CurrentPawn = WeakPawn.Get();
		AAuraEnemy* CurrentTarget = WeakTarget.Get();
		AAuraFireBoltProjectile* Projectile = nullptr;
		for (TActorIterator<AAuraFireBoltProjectile> It(GetWorld()); It; ++It)
		{
			if (CurrentPawn && It->GetOwner() == CurrentPawn)
			{
				Projectile = *It;
				break;
			}
		}
		const float YawError = CurrentPawn ? FMath::Abs(
			FMath::FindDeltaAngleDegrees(CurrentPawn->GetActorRotation().Yaw, 180.f))
			: 180.f;
		const FVector ToTarget = CurrentTarget && Projectile
			? (CurrentTarget->GetActorLocation() - Projectile->GetActorLocation()).GetSafeNormal2D()
			: FVector::ZeroVector;
		const float ShotDot = Projectile ? FVector::DotProduct(
			Projectile->GetActorForwardVector().GetSafeNormal2D(), ToTarget) : 0.f;
		const bool bTurnEnded = CurrentPawn && !CurrentPawn->IsFireBoltTurning()
			&& CurrentPawn->GetCharacterMovement()->bOrientRotationToMovement;
		const bool bPassed = bActivated && bTurnStarted && bNoInstantSnap
			&& *bProgressed && YawError < 5.f && bTurnEnded && ShotDot > 0.95f;
		UE_LOG(LogAuraM24Smoke, Display,
			TEXT("AURA_FIREBOLT_TURN Passed=%s Activated=%d Started=%d NoSnap=%d Progressed=%d YawError=%.1f TurnEnded=%d ShotDot=%.3f"),
			bPassed ? TEXT("true") : TEXT("false"), bActivated, bTurnStarted,
			bNoInstantSnap, *bProgressed, YawError, bTurnEnded, ShotDot);
		if (CurrentTarget) CurrentTarget->Destroy();
		if (CurrentPawn)
		{
			CurrentPawn->EndFireBoltTurn();
			CurrentPawn->TeleportTo(OriginalTransform.GetLocation(),
				OriginalTransform.Rotator(), false, true);
			CurrentPawn->GetCharacterMovement()->GravityScale = OriginalGravity;
			CurrentPawn->GetCharacterMovement()->SetMovementMode(OriginalMode);
		}
	}, 0.43f, false);
}
