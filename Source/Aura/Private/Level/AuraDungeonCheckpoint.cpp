// Copyright yosep

#include "Level/AuraDungeonCheckpoint.h"

#include "Character/AuraCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "EngineUtils.h"
#include "Game/AuraGameState.h"
#include "Game/AuraRunSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Pickup/AuraPickup.h"
#include "Player/AuraPlayerController.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraCheckpoint, Log, All);

AAuraDungeonCheckpoint::AAuraDungeonCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CheckpointMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CheckpointMesh"));
	CheckpointMesh->SetupAttachment(SceneRoot);
	CheckpointMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ActivationSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ActivationSphere"));
	ActivationSphere->SetupAttachment(SceneRoot);
	ActivationSphere->SetRelativeLocation(FVector(0.f, 0.f, 92.f));
	ActivationSphere->InitSphereRadius(175.f);
	ActivationSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ActivationSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	ActivationSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ActivationSphere->SetGenerateOverlapEvents(true);
	ActivationSphere->OnComponentBeginOverlap.AddDynamic(
		this, &AAuraDungeonCheckpoint::OnCheckpointOverlap);

	CheckpointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CheckpointLight"));
	CheckpointLight->SetupAttachment(SceneRoot);
	CheckpointLight->SetRelativeLocation(FVector(0.f, 0.f, 130.f));
	CheckpointLight->SetAttenuationRadius(520.f);
	CheckpointLight->SetCastShadows(false);

	CheckpointLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("CheckpointLabel"));
	AuraWorldTextFont::Apply(CheckpointLabel);
	CheckpointLabel->SetupAttachment(SceneRoot);
	CheckpointLabel->SetRelativeLocation(FVector(0.f, 0.f, 188.f));
	CheckpointLabel->SetHorizontalAlignment(EHTA_Center);
	CheckpointLabel->SetWorldSize(25.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CheckpointMeshAsset(
		TEXT("/Game/Assets/Dungeon/Checkpoint/SM_Checkpoint.SM_Checkpoint"));
	if (CheckpointMeshAsset.Succeeded())
	{
		CheckpointMesh->SetStaticMesh(CheckpointMeshAsset.Object);
	}
}

void AAuraDungeonCheckpoint::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisuals();
	UE_LOG(LogAuraCheckpoint, Display, TEXT("Dungeon checkpoint ready. Name=%s Id=%s"),
		*GetName(), *CheckpointId.ToString());
}

void AAuraDungeonCheckpoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshVisuals();
}

void AAuraDungeonCheckpoint::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraDungeonCheckpoint, bActivated);
}

void AAuraDungeonCheckpoint::ActivateCheckpoint(AAuraCharacter* AuraCharacter)
{
	if (!HasAuthority() || bActivated || !AuraCharacter || AuraCharacter->IsDead()) return;

	bActivated = true;
	const FVector RespawnLocation = GetActorLocation()
		+ GetActorRotation().RotateVector(RespawnOffset);
	const FTransform RespawnTransform(GetActorRotation(), RespawnLocation, FVector::OneVector);
	if (AAuraGameState* AuraGameState = GetWorld()->GetGameState<AAuraGameState>())
	{
		AuraGameState->SetActiveCheckpoint(CheckpointId, RespawnTransform);
	}
	if (GetNetMode() == NM_Standalone)
	{
		if (UAuraRunSubsystem* RunSubsystem =
			GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			RunSubsystem->SaveCheckpoint(GetWorld(), CheckpointId, RespawnTransform,
				AuraCharacter->GetCharacterLevel(), AuraCharacter->GetTotalExperience(),
				AuraCharacter->GetRunUpgradeState());
			if (const AAuraPlayerState* State = AuraCharacter->GetPlayerState<AAuraPlayerState>())
			{
				RunSubsystem->UpdateSavedGrowth(State->GetGrowthState());
			}
		}
	}

	for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
	{
		AAuraCharacter* TeamMember = *It;
		if (!TeamMember || TeamMember->IsDead()) continue;
		TeamMember->RestoreVitalsAtCheckpoint();
		if (AAuraPlayerController* AuraController =
			Cast<AAuraPlayerController>(TeamMember->GetController()))
		{
			AuraController->ShowCoopMessage(
				TEXT("已到达检查点 · 全队生命和法力已恢复"),
				FLinearColor(0.22f, 0.95f, 0.75f), 2.f,
				TEXT("继续前往内殿"));
		}
	}
	TArray<AActor*> Pickups;
	UGameplayStatics::GetAllActorsOfClass(this, AAuraPickup::StaticClass(), Pickups);
	for (AActor* PickupActor : Pickups)
	{
		if (AAuraPickup* Pickup = Cast<AAuraPickup>(PickupActor))
		{
			Pickup->ResetForCheckpoint();
		}
	}

	RefreshVisuals();
	ForceNetUpdate();
	UE_LOG(LogAuraCheckpoint, Display,
		TEXT("Dungeon checkpoint activated. Id=%s Respawn=%s ResetPickups=%d"),
		*CheckpointId.ToString(), *RespawnLocation.ToCompactString(), Pickups.Num());
}

void AAuraDungeonCheckpoint::OnCheckpointOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	ActivateCheckpoint(Cast<AAuraCharacter>(OtherActor));
}

void AAuraDungeonCheckpoint::OnRep_Activated()
{
	RefreshVisuals();
}

void AAuraDungeonCheckpoint::RefreshVisuals()
{
	if (CheckpointLight)
	{
		CheckpointLight->SetLightColor(bActivated
			? FLinearColor(0.05f, 1.f, 0.55f) : FLinearColor(0.12f, 0.42f, 1.f));
		CheckpointLight->SetIntensity(bActivated ? 3200.f : 1700.f);
	}
	if (CheckpointLabel)
	{
		CheckpointLabel->SetText(FText::FromString(
			bActivated ? TEXT("检查点已激活") : TEXT("检查点")));
		CheckpointLabel->SetTextRenderColor(
			bActivated ? FColor(70, 255, 160) : FColor(95, 165, 255));
	}
}
