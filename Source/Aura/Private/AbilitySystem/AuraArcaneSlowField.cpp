// Copyright yosep

#include "AbilitySystem/AuraArcaneSlowField.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/SceneComponent.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AAuraArcaneSlowField::AAuraArcaneSlowField()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AAuraArcaneSlowField::Configure(AAuraCharacter* InCaster, const float InRadius,
	const float InDuration)
{
	if (!HasAuthority()) return;
	Caster = InCaster;
	Radius = FMath::Max(1.f, InRadius);
	SetLifeSpan(FMath::Max(0.1f, InDuration));
	ForceNetUpdate();
}

void AAuraArcaneSlowField::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraArcaneSlowField, Radius);
}

void AAuraArcaneSlowField::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;
	PulseSlow();
	GetWorldTimerManager().SetTimer(PulseTimer, this,
		&AAuraArcaneSlowField::PulseSlow, 0.2f, true);
}

void AAuraArcaneSlowField::PulseSlow()
{
	if (!HasAuthority() || !Caster || Caster->IsDead()) return;
	UAbilitySystemComponent* SourceASC = Caster->GetAbilitySystemComponent();
	if (!SourceASC) return;
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AuraArcaneSlowField), false, Caster);
	GetWorld()->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity,
		Objects, FCollisionShape::MakeSphere(Radius), Params);
	TSet<AAuraEnemy*> Affected;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAuraEnemy* Enemy = Cast<AAuraEnemy>(Overlap.GetActor());
		if (!Enemy || Enemy->IsDead() || Enemy->IsDamageImmune() || Affected.Contains(Enemy)) continue;
		UAbilitySystemComponent* TargetASC = Enemy->GetAbilitySystemComponent();
		if (!TargetASC) continue;
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddSourceObject(this);
		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UAuraArcaneSlowEffect::StaticClass(), Caster->GetCharacterLevel(), Context);
		if (!Spec.IsValid()) continue;
		Spec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")), 0.38f);
		TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		Affected.Add(Enemy);
	}
}
