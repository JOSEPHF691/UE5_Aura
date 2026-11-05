// Copyright yosep
#include "AbilitySystem/AuraElementalSpell.h"
#include "AbilitySystem/AuraElementalStatusComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/SceneComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AAuraElementalSpell::AAuraElementalSpell()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Q(TEXT("/Game/Assets/Effects/ArcaneShards/NS_ArcaneShards.NS_ArcaneShards"));
	if (Q.Succeeded()) CrystalEffect = Q.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> E(TEXT("/Game/Assets/Effects/Shock/NS_Sparks.NS_Sparks"));
	if (E.Succeeded()) LightningEffect = E.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Beam(TEXT("/Game/Assets/Effects/Shock/NS_ElectricBeam.NS_ElectricBeam"));
	if (Beam.Succeeded()) BeamEffect = Beam.Object;
}
void AAuraElementalSpell::Configure(AAuraCharacter* Source, const EAuraElementalZone InMode,
	const float InDamage, const float InRadius, const int32 InCastId)
{
	Caster = Source; Mode = InMode; Damage = InDamage; Radius = InRadius; CastId = InCastId;
}
void AAuraElementalSpell::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisuals();
	if (!HasAuthority()) return;
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	SetLifeSpan(6.f);
	GetWorldTimerManager().SetTimer(StepTimer, this, &ThisClass::Step,
		Mode == EAuraElementalZone::Echo ? B.EchoInterval : Mode == EAuraElementalZone::Storm ? B.StormInterval : 0.1f, false);
}
void AAuraElementalSpell::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(StepTimer);
	for (UNiagaraComponent* Visual : FieldVisuals) if (Visual) Visual->DestroyComponent();
	Super::EndPlay(Reason);
}
void AAuraElementalSpell::RefreshVisuals()
{
	if (GetNetMode() == NM_DedicatedServer || Mode != EAuraElementalZone::Storm || !BeamEffect || !FieldVisuals.IsEmpty()) return;
	FVector Start = GetActorLocation();
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FHitResult Floor;
	if (GetWorld()->LineTraceSingleByObjectType(Floor, Start + FVector(0,0,100), Start - FVector(0,0,250), Objects))
		Start.Z = Floor.ImpactPoint.Z + 12.f;
	for (const float Angle : {-30.f,0.f,30.f})
	{
		UNiagaraComponent* Visual = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,BeamEffect,Start,FRotator::ZeroRotator,FVector(1),false,false);
		if (!Visual) continue;
		Visual->SetVariableVec3(TEXT("User.Beam Start"),FVector::ZeroVector);
		Visual->SetVariableVec3(TEXT("User.Beam End"),Start + GetActorForwardVector().RotateAngleAxis(Angle,FVector::UpVector)*Radius);
		Visual->Activate(true);
		FieldVisuals.Add(Visual);
	}
}
void AAuraElementalSpell::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraElementalSpell, Caster);
	DOREPLIFETIME(AAuraElementalSpell, Mode);
	DOREPLIFETIME(AAuraElementalSpell, Radius);
	DOREPLIFETIME(AAuraElementalSpell, PendingStrike);
	DOREPLIFETIME(AAuraElementalSpell, bPendingStrike);
}
bool AAuraElementalSpell::GroundPoint(const FVector& Desired, FVector& OutPoint) const
{
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ElementalGround), false, this);
	FHitResult Floor, Wall;
	if (!GetWorld()->LineTraceSingleByObjectType(Floor, Desired + FVector(0.f,0.f,180.f),
		Desired - FVector(0.f,0.f,180.f), Objects, Query) || Floor.ImpactNormal.Z < 0.5f) return false;
	if (FMath::Abs(Floor.ImpactPoint.Z - GetActorLocation().Z) > 130.f) return false;
	OutPoint = Floor.ImpactPoint + FVector(0.f,0.f,5.f);
	return !GetWorld()->LineTraceSingleByObjectType(Wall, GetActorLocation() + FVector(0.f,0.f,45.f),
		OutPoint + FVector(0.f,0.f,45.f), Objects, Query);
}
void AAuraElementalSpell::ApplyQ(AAuraCharacter* Source, const FVector& Center,
	const float InDamage, const float InRadius, const bool bCrystal)
{
	if (!Source || !Source->HasAuthority() || Source->IsDead()) return;
	for (AAuraEnemy* Target : UAuraElementalStatusComponent::GatherTargets(Source->GetWorld(), Center, InRadius))
	{
		if (!UAuraElementalStatusComponent::DealDamage(Source, Target, InDamage)) continue;
		if (Target->IsDead()) continue;
		Target->GetElementalStatus()->ApplySlow();
		const bool bFrozen = bCrystal && Target->GetElementalStatus()->ApplyCrystal(Source, InDamage);
		if (!bFrozen) Target->ApplyPoiseDamage(UAuraCombatBalanceData::Get().Poise.ArcaneShardsDamage);
	}
}
void AAuraElementalSpell::ApplyLightning(AAuraCharacter* Source, AAuraEnemy* Target,
	const float InDamage, const int32 InCastId)
{
	if (Source && Source->HasAuthority() && !Source->IsDead() && IsValid(Target))
		Target->GetElementalStatus()->OnLightningHit(Source, InCastId, InDamage, true);
}
void AAuraElementalSpell::Step()
{
	if (!Caster || Caster->IsDead()) { Destroy(); return; }
	const auto& B = UAuraCombatBalanceData::Get().Elemental;
	float NextDelay = 0.f;
	if (Mode == EAuraElementalZone::Echo)
	{
		const float Scales[] = {0.45f, 0.30f, 0.20f};
		const FVector Directions[] = {FVector(1,0,0), FVector(-1,0,0), FVector(0,1,0), FVector(0,-1,0)};
		for (const FVector& Direction : Directions)
		{
			FVector Point;
			if (!GroundPoint(GetActorLocation() + Direction * Radius * B.EchoSpacingScale * (StepIndex + 1), Point)) continue;
			ApplyQ(Caster, Point, Damage * Scales[StepIndex], Radius * B.EchoRadiusScale, false);
			MulticastCue(Point, Radius * B.EchoRadiusScale, false);
		}
		++StepIndex;
		if (StepIndex < 3) NextDelay = B.EchoInterval;
	}
	else if (Mode == EAuraElementalZone::Storm)
	{
		const float MinDot = FMath::Cos(FMath::DegreesToRadians(B.StormHalfAngle));
		for (AAuraEnemy* Target : UAuraElementalStatusComponent::GatherTargets(GetWorld(), GetActorLocation(), Radius))
		{
			if (FVector::DotProduct(GetActorForwardVector(), (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D()) < MinDot) continue;
			ApplyLightning(Caster, Target, Damage * B.StormDamageScale, CastId);
			MulticastCue(Target->GetActorLocation(), 90.f, true);
		}
		++StepIndex;
		if (StepIndex * B.StormInterval < B.StormDuration) NextDelay = B.StormInterval;
	}
	else
	{
		if (!bPendingStrike)
		{
			TArray<AAuraEnemy*> Candidates = UAuraElementalStatusComponent::GatherTargets(GetWorld(), GetActorLocation(), Radius);
			if (Candidates.IsEmpty()) { Destroy(); return; }
			TArray<AAuraEnemy*> Fresh;
			for (AAuraEnemy* Candidate : Candidates) if (!SelectedTargets.Contains(Candidate)) Fresh.Add(Candidate);
			if (Fresh.IsEmpty()) { SelectedTargets.Reset(); Fresh = Candidates; }
			AAuraEnemy* Target = Fresh[FMath::RandRange(0, Fresh.Num() - 1)];
			SelectedTargets.Add(Target);
			PendingStrike = Target->GetActorLocation();
			bPendingStrike = true;
			NextDelay = B.JudgmentWindup;
			ForceNetUpdate();
		}
		else
		{
			for (AAuraEnemy* Target : UAuraElementalStatusComponent::GatherTargets(GetWorld(), PendingStrike, B.JudgmentImpactRadius))
				ApplyLightning(Caster, Target, Damage * B.JudgmentDamageScale, CastId);
			MulticastCue(PendingStrike, B.JudgmentImpactRadius, true);
			bPendingStrike = false;
			++StepIndex;
			if (StepIndex < 3) NextDelay = FMath::Max(0.05f, B.JudgmentInterval - B.JudgmentWindup);
			ForceNetUpdate();
		}
	}
	if (NextDelay > 0.f) GetWorldTimerManager().SetTimer(StepTimer, this, &ThisClass::Step, NextDelay, false);
	else { SetLifeSpan(0.2f); }
}
void AAuraElementalSpell::MulticastCue_Implementation(const FVector Center, const float CueRadius, const bool bLightning)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (bLightning && BeamEffect && Mode == EAuraElementalZone::Judgment)
	{
		UNiagaraComponent* Beam = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,BeamEffect,Center+FVector(0,0,650),FRotator::ZeroRotator,FVector(1),false,false);
		if (Beam)
		{
			Beam->SetVariableVec3(TEXT("User.Beam Start"),FVector::ZeroVector);
			Beam->SetVariableVec3(TEXT("User.Beam End"),Center-FVector(0,0,70));
			Beam->Activate(true);
			TWeakObjectPtr<UNiagaraComponent> WeakBeam(Beam);
			FTimerHandle Cleanup; GetWorldTimerManager().SetTimer(Cleanup,[WeakBeam]() { if(WeakBeam.IsValid()) WeakBeam->DestroyComponent(); },0.35f,false);
		}
	}
	UNiagaraSystem* Effect = bLightning ? LightningEffect.Get() : CrystalEffect.Get();
	if (!Effect) return;
	UNiagaraComponent* Visual = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effect, Center,
		FRotator::ZeroRotator, FVector(FMath::Max(0.3f, CueRadius / 300.f)));
	if (!Visual) return;
	const TWeakObjectPtr<UNiagaraComponent> WeakVisual(Visual);
	FTimerHandle Cleanup;
	GetWorldTimerManager().SetTimer(Cleanup, [WeakVisual]()
	{ if (WeakVisual.IsValid()) WeakVisual->DestroyComponent(); }, 1.6f, false);
}
