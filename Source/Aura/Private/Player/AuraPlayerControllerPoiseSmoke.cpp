// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraPoiseSmoke, Log, All);

void AAuraPlayerController::AuraTestEnemyPoise()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* SourceASC = AuraPawn ? AuraPawn->GetAbilitySystemComponent() : nullptr;
	const FAuraEnemyPoiseBalance Poise = UAuraCombatBalanceData::Get().Poise;
	if (!HasAuthority() || !IsLocalController() || !GetWorld() || !SourceASC
		|| Poise.FireBoltDamage <= 0.f)
	{
		UE_LOG(LogAuraPoiseSmoke, Error, TEXT("AURA_ENEMY_POISE Passed=false Setup=invalid"));
		return;
	}
	UClass* EnemyClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear.BP_AuraGoblinSpear_C"));
	if (!EnemyClass)
	{
		UE_LOG(LogAuraPoiseSmoke, Error, TEXT("AURA_ENEMY_POISE Passed=false Setup=missing goblin"));
		return;
	}

	const auto Spawn = [this, AuraPawn, EnemyClass](const int32 Index, const bool bElite)
	{
		const FTransform Transform(FRotator::ZeroRotator,
			AuraPawn->GetActorLocation() + FVector(250.f + Index * 200.f, 600.f, 80.f));
		AAuraEnemy* Enemy = GetWorld()->SpawnActorDeferred<AAuraEnemy>(
			EnemyClass, Transform, this, AuraPawn,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Enemy) return static_cast<AAuraEnemy*>(nullptr);
		Enemy->ConfigurePreplacedGuard();
		if (bElite) Enemy->ConfigureEliteVariant(FVector(1.18f));
		UGameplayStatics::FinishSpawningActor(Enemy, Transform);
		return Enemy;
	};
	AAuraEnemy* Normal = Spawn(0, false);
	AAuraEnemy* Elite = Spawn(1, true);
	AAuraEnemy* Regen = Spawn(2, false);
	if (!Normal || !Elite || !Regen)
	{
		UE_LOG(LogAuraPoiseSmoke, Error, TEXT("AURA_ENEMY_POISE Passed=false Spawn=invalid"));
		return;
	}

	const auto Hit = [SourceASC, AuraPawn](AAuraEnemy* Enemy, const float GaugeDamage)
	{
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddSourceObject(AuraPawn);
		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
		if (!Spec.IsValid()) return false;
		Spec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -1.f);
		if (GaugeDamage > 0.f)
		{
			Spec.Data->SetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Data.PoiseDamage")), GaugeDamage);
		}
		Enemy->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		return true;
	};

	const bool bInitial = FMath::IsNearlyEqual(Normal->GetPoiseCurrent(), Poise.NormalMax)
		&& FMath::IsNearlyEqual(Elite->GetPoiseCurrent(), Poise.EliteMax);
	const bool bUntagged = Hit(Normal, 0.f)
		&& FMath::IsNearlyEqual(Normal->GetPoiseCurrent(), Poise.NormalMax);
	const int32 NormalHits = FMath::CeilToInt(Poise.NormalMax / Poise.FireBoltDamage);
	bool bNoEarlyStagger = NormalHits > 1;
	for (int32 Index = 1; Index < NormalHits; ++Index)
	{
		bNoEarlyStagger &= Hit(Normal, Poise.FireBoltDamage)
			&& !Normal->IsStunned() && !Normal->IsPoiseBroken()
			&& Normal->GetPoiseCurrent() > 0.f;
	}
	const bool bNormalBreak = Hit(Normal, Poise.FireBoltDamage)
		&& Normal->IsStunned() && Normal->IsPoiseBroken()
		&& FMath::IsNearlyZero(Normal->GetPoiseCurrent());
	const bool bBreakImmune = Hit(Normal, Poise.FireBoltDamage)
		&& Normal->IsPoiseBroken()
		&& FMath::IsNearlyZero(Normal->GetPoiseCurrent());

	const int32 EliteHits = FMath::CeilToInt(Poise.EliteMax / Poise.FireBoltDamage);
	bool bEliteThreshold = EliteHits > NormalHits;
	for (int32 Index = 1; Index < EliteHits; ++Index)
	{
		bEliteThreshold &= Hit(Elite, Poise.FireBoltDamage)
			&& !Elite->IsStunned() && !Elite->IsPoiseBroken();
	}
	bEliteThreshold &= Hit(Elite, Poise.FireBoltDamage)
		&& Elite->IsStunned() && Elite->IsPoiseBroken();
	const bool bRegenStarted = Hit(Regen, Poise.FireBoltDamage)
		&& Regen->GetPoiseCurrent() < Regen->GetPoiseMax();
	const float RegenStart = Regen->GetPoiseCurrent();

	struct FPoiseSmokeResults
	{
		bool bReset = false;
		bool bProtectedAfterReset = false;
	};
	const TSharedRef<FPoiseSmokeResults> Results = MakeShared<FPoiseSmokeResults>();
	const TWeakObjectPtr<AAuraEnemy> WeakNormal(Normal);
	const TWeakObjectPtr<AAuraEnemy> WeakElite(Elite);
	const TWeakObjectPtr<AAuraEnemy> WeakRegen(Regen);
	FTimerHandle ResetCheck;
	GetWorldTimerManager().SetTimer(ResetCheck,
		[Results, WeakNormal, WeakElite, Poise]()
		{
			AAuraEnemy* NormalEnemy = WeakNormal.Get();
			AAuraEnemy* EliteEnemy = WeakElite.Get();
			Results->bReset = NormalEnemy && EliteEnemy
				&& !NormalEnemy->IsPoiseBroken() && !EliteEnemy->IsPoiseBroken()
				&& !NormalEnemy->IsStunned() && !EliteEnemy->IsStunned()
				&& FMath::IsNearlyEqual(NormalEnemy->GetPoiseCurrent(), Poise.NormalMax)
				&& FMath::IsNearlyEqual(EliteEnemy->GetPoiseCurrent(), Poise.EliteMax);
			Results->bProtectedAfterReset = NormalEnemy
				&& !NormalEnemy->ApplyPoiseDamage(Poise.FireBoltDamage)
				&& FMath::IsNearlyEqual(NormalEnemy->GetPoiseCurrent(), Poise.NormalMax);
		}, FMath::Max(Poise.NormalStaggerDuration,
			Poise.EliteStaggerDuration) + 0.2f, false);

	FTimerHandle FinalCheck;
	GetWorldTimerManager().SetTimer(FinalCheck,
		[Results, WeakNormal, WeakElite, WeakRegen, bInitial, bUntagged,
			bNoEarlyStagger, bNormalBreak, bBreakImmune, bEliteThreshold,
			bRegenStarted, RegenStart, Poise]()
		{
			AAuraEnemy* NormalEnemy = WeakNormal.Get();
			AAuraEnemy* RegenEnemy = WeakRegen.Get();
			const bool bRegen = RegenEnemy && bRegenStarted
				&& RegenEnemy->GetPoiseCurrent() > RegenStart;
			const bool bAfterImmunity = NormalEnemy && Results->bReset
				&& NormalEnemy->GetPoiseCurrent() >= Poise.NormalMax - 0.01f
				&& !NormalEnemy->ApplyPoiseDamage(Poise.FireBoltDamage)
				&& NormalEnemy->GetPoiseCurrent() < Poise.NormalMax;
			const bool bPassed = bInitial && bUntagged && bNoEarlyStagger
				&& bNormalBreak && bBreakImmune && bEliteThreshold
				&& Results->bReset && Results->bProtectedAfterReset
				&& bRegen && bAfterImmunity;
			UE_LOG(LogAuraPoiseSmoke, Display,
				TEXT("AURA_ENEMY_POISE Passed=%s Initial=%s Untagged=%s NoEarly=%s Break=%s BreakImmune=%s Elite=%s Reset=%s PostResetImmune=%s Regen=%s AfterImmunity=%s"),
				bPassed ? TEXT("true") : TEXT("false"),
				bInitial ? TEXT("true") : TEXT("false"),
				bUntagged ? TEXT("true") : TEXT("false"),
				bNoEarlyStagger ? TEXT("true") : TEXT("false"),
				bNormalBreak ? TEXT("true") : TEXT("false"),
				bBreakImmune ? TEXT("true") : TEXT("false"),
				bEliteThreshold ? TEXT("true") : TEXT("false"),
				Results->bReset ? TEXT("true") : TEXT("false"),
				Results->bProtectedAfterReset ? TEXT("true") : TEXT("false"),
				bRegen ? TEXT("true") : TEXT("false"),
				bAfterImmunity ? TEXT("true") : TEXT("false"));
			if (NormalEnemy) NormalEnemy->Destroy();
			if (AAuraEnemy* EliteEnemy = WeakElite.Get()) EliteEnemy->Destroy();
			if (RegenEnemy) RegenEnemy->Destroy();
		}, FMath::Max(Poise.RegenDelay + 0.4f,
			Poise.NormalStaggerDuration + Poise.BreakImmunity + 0.4f), false);
}
