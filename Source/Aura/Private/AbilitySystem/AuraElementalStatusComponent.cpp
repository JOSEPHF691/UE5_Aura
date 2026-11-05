// Copyright yosep
#include "AbilitySystem/AuraElementalStatusComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "Player/AuraPlayerController.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

UAuraElementalStatusComponent::UAuraElementalStatusComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal.SM_HealthCrystal"));
	if (Mesh.Succeeded()) CrystalMeshAsset = Mesh.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Fire(TEXT("/Game/Assets/Effects/Fire/NS_Fire.NS_Fire"));
	if (Fire.Succeeded()) IgniteEffect = Fire.Object;
}
void UAuraElementalStatusComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(false);
}
void UAuraElementalStatusComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (CrystalVisual) CrystalVisual->DestroyComponent();
	if (IgniteVisual) IgniteVisual->DestroyComponent();
	Super::EndPlay(Reason);
}
void UAuraElementalStatusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAuraElementalStatusComponent, State);
}
AAuraEnemy* UAuraElementalStatusComponent::Enemy() const { return Cast<AAuraEnemy>(GetOwner()); }
float UAuraElementalStatusComponent::Now() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}
float UAuraElementalStatusComponent::GetSlowFraction() const { return State.SlowUntil > Now() ? State.SlowFraction : 0.f; }
bool UAuraElementalStatusComponent::CanAcceptHardControl() const
{
	return Enemy() && !Enemy()->IsDead() && !Enemy()->IsBossTransitioning()
		&& !Enemy()->IsStunned() && !HasCrystal() && Now() >= State.ControlProtectedUntil;
}
void UAuraElementalStatusComponent::NotifyHardControl(const float Duration)
{
	if (!GetOwner()->HasAuthority()) return;
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	State.ControlProtectedUntil = FMath::Max(State.ControlProtectedUntil, Now() + Duration
		+ (Enemy()->IsEliteVariant() ? B.EliteControlProtection : B.ControlProtection));
	Publish();
}
void UAuraElementalStatusComponent::ApplySlow()
{
	if (!GetOwner()->HasAuthority() || !Enemy() || Enemy()->IsDead() || Enemy()->IsDamageImmune()) return;
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	State.SlowFraction = Enemy()->IsBossEnemy() ? B.BossSlow : Enemy()->IsEliteVariant() ? B.EliteSlow : B.NormalSlow;
	State.SlowUntil = Now() + B.SlowDuration;
	Publish();
}
bool UAuraElementalStatusComponent::ApplyCrystal(AAuraCharacter* Source, const float BaseDamage)
{
	if (!GetOwner()->HasAuthority() || !Source || !Enemy() || Enemy()->IsDead() || HasCrystal()) return false;
	const bool bBossWindow = Enemy()->IsBossEnemy() && Enemy()->IsPoiseBroken() && Enemy()->GetPoiseBreakRemaining() > 0.f;
	if (Enemy()->IsBossEnemy() ? !bBossWindow : !CanAcceptHardControl()) return false;
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	const float Duration = bBossWindow ? FMath::Min(Enemy()->GetPoiseBreakRemaining(), B.EliteCrystalDuration)
		: Enemy()->IsEliteVariant() ? B.EliteCrystalDuration : B.CrystalDuration;
	UAbilitySystemComponent* ASC = Enemy()->GetAbilitySystemComponent();
	FGameplayEffectSpecHandle Spec = Source->GetAbilitySystemComponent()->MakeOutgoingSpec(
		UAuraShockStunEffect::StaticClass(), 1.f, Source->GetAbilitySystemComponent()->MakeEffectContext());
	if (!Spec.IsValid() || !ASC) return false;
	Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")), Duration);
	if (!bBossWindow) ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	if (!Enemy()->IsStunned()) return false;
	CrystalSource = Source;
	CrystalBurstDamage = BaseDamage * B.CrystalBurstScale;
	State.CrystalUntil = Now() + Duration;
	State.CrystalMaxHealth = FMath::Max(1.f, UAuraCombatBalanceData::Get().FireBolt.Damage
		* Source->GetFireBoltDamageMultiplier() * B.CrystalHealthInFirebolts);
	State.CrystalHealth = State.CrystalMaxHealth;
	if (!bBossWindow) NotifyHardControl(Duration);
	Publish();
	return true;
}
void UAuraElementalStatusComponent::ReleaseCrystal(const bool bBroken)
{
	if (!HasCrystal() && State.CrystalUntil <= 0.f) return;
	State.CrystalHealth = 0.f;
	State.CrystalUntil = 0.f;
	if (bBroken && Enemy() && !Enemy()->IsDead())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")));
		if (!Enemy()->IsBossEnemy()) Enemy()->GetAbilitySystemComponent()->RemoveActiveEffectsWithGrantedTags(Tags);
	}
	Publish();
	if (!bBroken || !CrystalSource.IsValid()) return;
	const FVector Center = GetOwner()->GetActorLocation();
	const float Damage = CrystalBurstDamage;
	const TWeakObjectPtr<AAuraCharacter> Source = CrystalSource;
	GetWorld()->GetTimerManager().SetTimerForNextTick([Source, Center, Damage]()
	{
		if (!Source.IsValid() || Source->IsDead()) return;
		for (AAuraEnemy* Target : GatherTargets(Source->GetWorld(), Center,
			UAuraCombatBalanceData::Get().Elemental.CrystalBurstRadius)) DealDamage(Source.Get(), Target, Damage, true);
		Source->MulticastEmberBurst(Center);
		UE_LOG(LogTemp, Display, TEXT("AURA_CRYSTAL_BROKEN Damage=%.1f"), Damage);
	});
}
void UAuraElementalStatusComponent::OnDirectDamage(const float Damage)
{
	if (!GetOwner()->HasAuthority() || !HasCrystal() || Damage <= 0.f) return;
	State.CrystalHealth = FMath::Max(0.f, State.CrystalHealth - Damage);
	if (State.CrystalHealth <= 0.f) ReleaseCrystal(true);
	else Publish();
}
void UAuraElementalStatusComponent::OnFireHit(AAuraCharacter* Source, const int32 CastId, const float Damage)
{
	if (!GetOwner()->HasAuthority() || !Source || !Enemy() || Enemy()->IsDead() || CastId <= 0) return;
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	FSourceState& Entry = Sources.FindOrAdd(Source);
	if (Entry.FireCasts.Contains(CastId)) return;
	if (Entry.FireCasts.Num() >= 64) Entry.FireCasts.Reset();
	Entry.FireCasts.Add(CastId);
	Entry.EmberUntil = Now() + B.EmberDuration + (Source->HasLegendaryComboEffect(TEXT("Item.Power.EmberEcho")) ? 1.5f : 0.f);
	State.bHasEmber = true;
	const float Chance = FMath::Clamp(B.IgniteBaseChance + FMath::Max(0, Source->GetSkillRank(EAuraPlayerSkill::FireBolt) - 1)
		* B.IgniteChancePerRank, 0.f, 1.f);
	if (FMath::FRand() < Chance)
	{
		if (State.IgniteUntil <= Now()) NextIgniteAt = Now() + 1.f;
		const float NewDamage = Damage * B.IgniteDamageScalePerSecond;
		if (NewDamage >= IgniteDamage || !IgniteSource.IsValid()) IgniteSource = Source;
		IgniteDamage = FMath::Max(IgniteDamage, NewDamage);
		State.IgniteUntil = Now() + B.IgniteDuration;
	}
	Publish();
}
void UAuraElementalStatusComponent::OnLightningHit(AAuraCharacter* Source, const int32 CastId, const float Damage, const bool bApplyDirectDamage)
{
	if (!GetOwner()->HasAuthority() || !Source || !Enemy() || Enemy()->IsDead() || Enemy()->IsDamageImmune() || CastId <= 0) return;
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	FSourceState& Entry = Sources.FindOrAdd(Source);
	if (Entry.ShockCasts.Contains(CastId))
	{
		if (bApplyDirectDamage) DealDamage(Source, Enemy(), Damage);
		return;
	}
	if (Entry.ShockCasts.Num() >= 64) Entry.ShockCasts.Reset();
	Entry.ShockCasts.Add(CastId);
	// Snapshot and consume before direct damage: a lethal hit clears enemy-owned state.
	const bool bExplode = Entry.EmberUntil > Now();
	Entry.EmberUntil = 0.f;
	const FVector Center = Enemy()->GetActorLocation();
	if (bApplyDirectDamage) DealDamage(Source, Enemy(), Damage);
	// Never access Entry here: the death callback may have reset Sources.
	if (!Enemy()->IsDead())
	{
		Enemy()->ApplyPoiseDamage(UAuraCombatBalanceData::Get().Poise.ShockDamage);
		if (State.ParalysisUntil <= Now()) NextParalysisAt = Now() + B.ParalysisInterval;
		State.ParalysisUntil = FMath::Max(State.ParalysisUntil, Now() + B.ParalysisDuration + Source->GetShockStunDurationBonus()
			+ (Source->HasLegendaryComboEffect(TEXT("Item.Power.FractureSurge")) ? 1.f : 0.f));
		ParalysisSource = Source;
	}
	Publish();
	if (!bExplode) return;
	for (AAuraEnemy* Target : GatherTargets(GetWorld(), Center, 220.f)) DealDamage(Source, Target, Damage * B.EmberBurstScale, true);
	Source->MulticastEmberBurst(Center);
	UE_LOG(LogTemp, Display, TEXT("AURA_EMBER_IGNITED CastId=%d"), CastId);
}
void UAuraElementalStatusComponent::ClearAll()
{
	if (!GetOwner()->HasAuthority()) return;
	State = FAuraElementalStatus();
	Sources.Reset();
	IgniteDamage = 0.f;
	Publish();
}
void UAuraElementalStatusComponent::TickComponent(float DT, ELevelTick Type, FActorComponentTickFunction* Fn)
{
	Super::TickComponent(DT, Type, Fn);
	if (!Enemy() || Enemy()->IsDead()) return;
	const float Time = Now();
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	bool bChanged = false;
	if (State.SlowUntil > 0.f && Time >= State.SlowUntil) { State.SlowUntil = 0.f; State.SlowFraction = 0.f; bChanged = true; }
	if (State.CrystalUntil > 0.f && (Time >= State.CrystalUntil || !Enemy()->IsStunned())) ReleaseCrystal(false);
	if (State.IgniteUntil > 0.f && Time >= NextIgniteAt && NextIgniteAt <= State.IgniteUntil + 0.01f)
	{
		NextIgniteAt += 1.f;
		if (IgniteSource.IsValid()) DealDamage(IgniteSource.Get(), Enemy(), IgniteDamage, true);
	}
	if (State.IgniteUntil > 0.f && Time > State.IgniteUntil + 0.1f) { State.IgniteUntil = 0.f; IgniteDamage = 0.f; bChanged = true; }
	if (State.ParalysisUntil > Time && Time >= NextParalysisAt)
	{
		NextParalysisAt = Time + B.ParalysisInterval;
		if (Enemy()->IsBossEnemy()) Enemy()->ApplyPoiseDamage(8.f);
		else if (CanAcceptHardControl() && ParalysisSource.IsValid())
		{
			UAbilitySystemComponent* ASC = ParalysisSource->GetAbilitySystemComponent();
			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAuraShockStunEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
			if (Spec.IsValid())
			{
				const float Duration = Enemy()->IsEliteVariant() ? B.ElitePulseStun : B.NormalPulseStun;
				Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")), Duration);
				Enemy()->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
				NotifyHardControl(Duration);
			}
		}
	}
	if (State.ParalysisUntil > 0.f && Time >= State.ParalysisUntil) { State.ParalysisUntil = 0.f; bChanged = true; }
	if (State.ControlProtectedUntil > 0.f && Time >= State.ControlProtectedUntil) { State.ControlProtectedUntil=0.f; bChanged=true; }
	bool bEmber = false;
	for (auto It = Sources.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) { It.RemoveCurrent(); continue; }
		bEmber |= It.Value().EmberUntil > Time;
	}
	if (State.bHasEmber != bEmber) { State.bHasEmber = bEmber; bChanged = true; }
	if (bChanged) Publish();
}
void UAuraElementalStatusComponent::Publish()
{
	if (GetOwner()->HasAuthority())
		SetComponentTickEnabled(State.SlowUntil > Now() || State.IgniteUntil > 0.f || State.ParalysisUntil > Now()
			|| HasCrystal() || State.bHasEmber || State.ControlProtectedUntil > Now());
	GetOwner()->ForceNetUpdate();
	OnRep_State();
}
void UAuraElementalStatusComponent::OnRep_State()
{
	if (!Enemy()) return;
	Enemy()->RefreshElementalMovement();
	if (GetNetMode() == NM_DedicatedServer) return;
	if (State.IgniteUntil > Now() && !IgniteVisual && IgniteEffect)
	{
		IgniteVisual = UNiagaraFunctionLibrary::SpawnSystemAttached(IgniteEffect,GetOwner()->GetRootComponent(),NAME_None,
			FVector(0,0,-40),FRotator::ZeroRotator,EAttachLocation::KeepRelativeOffset,false);
		if (IgniteVisual) IgniteVisual->SetRelativeScale3D(FVector(0.45f));
	}
	if ((State.IgniteUntil <= Now() || Enemy()->IsDead()) && IgniteVisual)
	{ IgniteVisual->DestroyComponent(); IgniteVisual = nullptr; }
	if (HasCrystal() && !CrystalVisual && CrystalMeshAsset)
	{
		CrystalVisual = NewObject<UStaticMeshComponent>(GetOwner());
		CrystalVisual->SetupAttachment(GetOwner()->GetRootComponent());
		CrystalVisual->SetStaticMesh(CrystalMeshAsset);
		CrystalVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const float Height = Enemy()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.2f;
		CrystalVisual->SetRelativeScale3D(FVector(Height / (CrystalMeshAsset->GetBounds().BoxExtent.Z * 2.f)));
		CrystalVisual->RegisterComponent();
	}
	if (!HasCrystal() && CrystalVisual) { CrystalVisual->DestroyComponent(); CrystalVisual = nullptr; }
}
bool UAuraElementalStatusComponent::DealDamage(AAuraCharacter* Source, AAuraEnemy* Target, const float Damage, const bool bSecondary)
{
	if (!Source || !Source->HasAuthority() || Source->IsDead() || !IsValid(Target)
		|| Target->IsDead() || Target->IsDamageImmune() || Damage <= 0.f) return false;
	UAbilitySystemComponent* ASC = Source->GetAbilitySystemComponent();
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Source);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -Damage);
	if (bSecondary) Spec.Data->AddDynamicAssetTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementalSecondary")));
	Target->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(Source->GetController()))
		if (AAuraHUD* HUD = Cast<AAuraHUD>(Controller->GetHUD())) HUD->ShowDamageNumber(Target, Damage);
	return true;
}
TArray<AAuraEnemy*> UAuraElementalStatusComponent::GatherTargets(UWorld* World, const FVector& Center, const float Radius)
{
	TArray<AAuraEnemy*> Result;
	if (!World) return Result;
	TArray<FOverlapResult> Hits;
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn);
	World->OverlapMultiByObjectType(Hits, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius));
	for (const FOverlapResult& Hit : Hits)
	{
		AAuraEnemy* Target = Cast<AAuraEnemy>(Hit.GetActor());
		if (!Target || Target->IsDead() || Target->IsDamageImmune() || Result.Contains(Target)
			|| FMath::Abs(Target->GetActorLocation().Z - Center.Z) > 220.f) continue;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ElementalLOS), false, Target);
		FHitResult Wall;
		FCollisionObjectQueryParams StaticObjects; StaticObjects.AddObjectTypesToQuery(ECC_WorldStatic);
		if (!World->LineTraceSingleByObjectType(Wall, Center + FVector(0.f,0.f,35.f),
			Target->GetActorLocation(), StaticObjects, Query)) Result.Add(Target);
	}
	return Result;
}
