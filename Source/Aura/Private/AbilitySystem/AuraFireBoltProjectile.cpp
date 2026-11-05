// Copyright yosep

#include "AbilitySystem/AuraFireBoltProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "AbilitySystem/AuraElementalStatusComponent.h"
#include "Components/SphereComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraProjectile, Log, All);

AAuraFireBoltProjectile::AAuraFireBoltProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicateMovement(true);
	InitialLifeSpan = 4.f;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(18.f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->InitialSpeed = 1200.f;
	ProjectileMovement->MaxSpeed = 1200.f;
	ProjectileMovement->ProjectileGravityScale = 0.f;
	ProjectileMovement->bIsHomingProjectile = false;
	ProjectileMovement->HomingAccelerationMagnitude = 3500.f;
	ProjectileMovement->bRotationFollowsVelocity = true;

	ProjectileEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileEffect"));
	ProjectileEffect->SetupAttachment(CollisionSphere);
	ProjectileEffect->SetAutoActivate(true);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ProjectileEffectAsset(
		TEXT("/Game/Assets/Effects/Projectiles/FireBolt/NS_Fireball.NS_Fireball"));
	if (ProjectileEffectAsset.Succeeded()) ProjectileEffect->SetAsset(ProjectileEffectAsset.Object);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ImpactEffectAsset(
		TEXT("/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion.NS_FireExplosion"));
	if (ImpactEffectAsset.Succeeded()) ImpactEffect = ImpactEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> ImpactSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/FireBolt/sfx_FireBolt_Impact.sfx_FireBolt_Impact"));
	if (ImpactSoundAsset.Succeeded()) ImpactSound = ImpactSoundAsset.Object;
}

void AAuraFireBoltProjectile::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraFireBoltProjectile, HomingTarget);
	DOREPLIFETIME(AAuraFireBoltProjectile, bOrbitHolding);
}

void AAuraFireBoltProjectile::SetHomingTarget(AAuraEnemy* InTarget)
{
	HomingTarget = InTarget;
	RefreshHomingState();
	if (HasAuthority()) SetActorTickEnabled(bOrbitHolding || HomingTarget != nullptr);
	if (HasAuthority()) ForceNetUpdate();
}

void AAuraFireBoltProjectile::ConfigureOrbitalRelease(const int32 InIndex,
	const float InDelay, AAuraEnemy* InTarget, const FVector& InFallbackDirection)
{
	bOrbitHolding = true;
	OrbitIndex = FMath::Clamp(InIndex, 0, 2);
	OrbitReleaseDelay = FMath::Max(0.01f, InDelay);
	FallbackShotDirection = InFallbackDirection.GetSafeNormal();
	if (InTarget) SetHomingTarget(InTarget);
}

void AAuraFireBoltProjectile::RefreshHomingState()
{
	if (!ProjectileMovement) return;
	const bool bCanHome = IsValid(HomingTarget) && !HomingTarget->IsDead();
	ProjectileMovement->HomingTargetComponent = bCanHome
		? HomingTarget->GetRootComponent() : nullptr;
	ProjectileMovement->bIsHomingProjectile = bCanHome;
}

void AAuraFireBoltProjectile::OnRep_HomingTarget()
{
	RefreshHomingState();
}

void AAuraFireBoltProjectile::RefreshOrbitState()
{
	if (bOrbitHolding)
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}
	else
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ProjectileMovement->Activate(true);
		RefreshHomingState();
	}
}

void AAuraFireBoltProjectile::OnRep_OrbitHolding()
{
	RefreshOrbitState();
}

void AAuraFireBoltProjectile::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	if (!bOrbitHolding)
	{
		if (ProjectileMovement->bIsHomingProjectile
			&& (!IsValid(HomingTarget) || HomingTarget->IsDead()))
			SetHomingTarget(nullptr);
		return;
	}
	const AAuraCharacter* Caster = Cast<AAuraCharacter>(GetOwner());
	if (!IsValid(Caster) || Caster->IsDead())
	{
		Destroy();
		return;
	}
	const FVector Offset(35.f, (OrbitIndex - 1) * 90.f, 115.f);
	SetActorLocation(Caster->GetActorLocation()
		+ Caster->GetActorRotation().RotateVector(Offset), false);
}

void AAuraFireBoltProjectile::ReleaseOrbitalShot()
{
	if (!HasAuthority() || !bOrbitHolding) return;
	const AAuraCharacter* Caster = Cast<AAuraCharacter>(GetOwner());
	if (!IsValid(Caster) || Caster->IsDead())
	{
		Destroy();
		return;
	}
	bOrbitHolding = false;
	SetActorTickEnabled(HomingTarget != nullptr);
	if (!IsValid(HomingTarget) || HomingTarget->IsDead()) SetHomingTarget(nullptr);
	const FVector Direction = HomingTarget
		? (HomingTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal()
		: FallbackShotDirection;
	SetActorRotation(Direction.Rotation());
	RefreshOrbitState();
	ProjectileMovement->Velocity = Direction * ProjectileMovement->InitialSpeed;
	ProjectileMovement->UpdateComponentVelocity();
	ForceNetUpdate();
}

void AAuraFireBoltProjectile::BeginPlay()
{
	Super::BeginPlay();
	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AAuraFireBoltProjectile::OnSphereOverlap);
	CollisionSphere->OnComponentHit.AddDynamic(this, &AAuraFireBoltProjectile::OnSphereHit);
	CollisionSphere->IgnoreActorWhenMoving(GetOwner(), true);
	CollisionSphere->IgnoreActorWhenMoving(GetInstigator(), true);
	RefreshHomingState();
	if (HasAuthority() && HomingTarget) SetActorTickEnabled(true);
	if (bOrbitHolding)
	{
		RefreshOrbitState();
		if (HasAuthority())
		{
			SetActorTickEnabled(true);
			Tick(0.f);
			GetWorldTimerManager().SetTimer(OrbitReleaseTimer, this,
				&AAuraFireBoltProjectile::ReleaseOrbitalShot, OrbitReleaseDelay, false);
		}
	}
	UE_LOG(LogAuraProjectile, Display, TEXT("FireBolt spawned at %s toward %s"),
		*GetActorLocation().ToCompactString(), *GetActorForwardVector().ToCompactString());
}

void AAuraFireBoltProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority() || bImpacted || !OtherActor || HitActors.Contains(OtherActor)
		|| OtherActor == GetOwner() || OtherActor == GetInstigator()) return;

	// The same projectile class is used by the player and the Shaman. Ignore
	// friendly pawns without consuming the projectile so enemies cannot damage
	// one another and the player cannot hit another player pawn.
	if (GetOwner() && GetOwner()->IsA<AAuraEnemy>() && !OtherActor->IsA<AAuraCharacter>()) return;
	if (GetOwner() && GetOwner()->IsA<AAuraCharacter>() && !OtherActor->IsA<AAuraEnemy>()) return;
	if (const AAuraEnemy* TargetEnemy = Cast<AAuraEnemy>(OtherActor);
		TargetEnemy && TargetEnemy->IsDamageImmune())
	{
		UE_LOG(LogAuraProjectile, Display,
			TEXT("FireBolt absorbed by invulnerable boss. Target=%s"), *OtherActor->GetName());
		bImpacted = true;
		SpawnImpactEffects();
		Destroy();
		return;
	}

	UAbilitySystemComponent* TargetAbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor);
	UAbilitySystemComponent* SourceAbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (!TargetAbilitySystem || !SourceAbilitySystem) return;

	FGameplayEffectContextHandle EffectContext = SourceAbilitySystem->MakeEffectContext();
	EffectContext.AddSourceObject(this);
	FGameplayEffectSpecHandle DamageSpec = SourceAbilitySystem->MakeOutgoingSpec(
		UAuraDamageGameplayEffect::StaticClass(), 1.f, EffectContext);
	if (DamageSpec.IsValid())
	{
		AAuraCharacter* PlayerCaster = Cast<AAuraCharacter>(GetOwner());
		AAuraEnemy* TargetEnemy = Cast<AAuraEnemy>(OtherActor);
		DamageSpec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -Damage);
		if (PlayerCaster && TargetEnemy)
		{
			DamageSpec.Data->SetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Data.PoiseDamage")),
				UAuraCombatBalanceData::Get().Poise.FireBoltDamage * PoiseDamageScale);
		}
		TargetAbilitySystem->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
		if (PlayerCaster && TargetEnemy && !TargetEnemy->IsDead())
			TargetEnemy->GetElementalStatus()->OnFireHit(PlayerCaster, ComboCastId, Damage);
		UE_LOG(LogAuraProjectile, Display, TEXT("FireBolt hit %s for %.1f damage. Health=%.1f"),
			*OtherActor->GetName(), Damage,
			TargetAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()));

		const APawn* SourcePawn = Cast<APawn>(GetOwner());
		const APlayerController* SourceController = SourcePawn
			? Cast<APlayerController>(SourcePawn->GetController()) : nullptr;
		if (AAuraHUD* AuraHUD = SourceController ? Cast<AAuraHUD>(SourceController->GetHUD()) : nullptr)
		{
			AuraHUD->ShowDamageNumber(OtherActor, Damage);
		}
	}
	HitActors.Add(OtherActor);
	if (DamageSpec.IsValid() && ExtraPiercesRemaining > 0 && OtherActor->IsA<AAuraEnemy>())
	{
		--ExtraPiercesRemaining;
		if (HomingTarget) SetHomingTarget(nullptr);
		CollisionSphere->IgnoreActorWhenMoving(OtherActor, true);
		SpawnImpactEffects();
		UE_LOG(LogAuraProjectile, Display,
			TEXT("FireBolt rank-three pierce. Target=%s Remaining=%d"),
			*OtherActor->GetName(), ExtraPiercesRemaining);
		return;
	}

	bImpacted = true;
	SpawnImpactEffects();
	Destroy();
}

void AAuraFireBoltProjectile::OnSphereHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bImpacted || OtherActor == GetOwner() || OtherActor == GetInstigator()) return;
	bImpacted = true;
	SpawnImpactEffects();
	Destroy();
}

void AAuraFireBoltProjectile::SpawnImpactEffects()
{
	if (ImpactEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactEffect, GetActorLocation(), GetActorRotation());
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, GetActorLocation());
	}
}
