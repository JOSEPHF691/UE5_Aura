// Copyright yosep

#include "Level/AuraRitualSite.h"

#include "Character/AuraCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/AuraWorldTextFont.h"
#include "Game/AuraGameState.h"
#include "Level/AuraDungeonGate.h"
#include "Level/AuraEncounterVolume.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Player/AuraPlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraRitual, Log, All);

AAuraRitualSite::AAuraRitualSite()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	Pedestal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pedestal"));
	Pedestal->SetupAttachment(SceneRoot);
	Pedestal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RuneCore = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RuneCore"));
	RuneCore->SetupAttachment(SceneRoot);
	RuneCore->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("InteractionLabel"));
	AuraWorldTextFont::Apply(InteractionLabel);
	InteractionLabel->SetupAttachment(SceneRoot);
	InteractionLabel->SetHorizontalAlignment(EHTA_Center);
	InteractionLabel->SetWorldSize(23.f);
	RitualLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RitualLight"));
	RitualLight->SetupAttachment(SceneRoot);
	RitualLight->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	RitualLight->SetIntensity(1200.f);
	RitualLight->SetAttenuationRadius(330.f);
	RitualLight->SetCastShadows(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PillarAsset(
		TEXT("/Game/Assets/Dungeon/Beacon/SM_Pillar.SM_Pillar"));
	if (PillarAsset.Succeeded()) Pedestal->SetStaticMesh(PillarAsset.Object);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeaconAsset(
		TEXT("/Game/Assets/Dungeon/Beacon/SM_Beacon.SM_Beacon"));
	if (BeaconAsset.Succeeded()) RuneCore->SetStaticMesh(BeaconAsset.Object);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> CueAsset(
		TEXT("/Game/Assets/Effects/Combat/NS_GroundSummon.NS_GroundSummon"));
	if (CueAsset.Succeeded()) ActivationEffect = CueAsset.Object;
}

void AAuraRitualSite::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!Pedestal || !RuneCore || !InteractionLabel || !RitualLight) return;
	const bool bAltar = IsBossAltar();
	Pedestal->SetRelativeScale3D(bAltar ? FVector(1.25f) : FVector(0.9f));
	RuneCore->SetRelativeLocation(FVector(0.f, 0.f, bAltar ? 98.f : 76.f));
	RuneCore->SetRelativeScale3D(bAltar ? FVector(0.85f) : FVector(0.55f));
	InteractionLabel->SetRelativeLocation(FVector(0.f, 0.f, bAltar ? 188.f : 160.f));
	if (GetNetMode() != NM_DedicatedServer) RefreshVisuals();
}

void AAuraRitualSite::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && IsBossAltar())
	{
		// Gate BeginPlay may run after this actor; synchronize once all actors are ready.
		GetWorldTimerManager().SetTimerForNextTick(this, &AAuraRitualSite::RefreshBossGate);
	}
	if (GetNetMode() != NM_DedicatedServer) RefreshVisuals();
}

void AAuraRitualSite::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) return;
	const AAuraGameState* State = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!State) return;
	const bool bActive = IsBossAltar() ? State->IsBossSummoned()
		: State->HasActivatedSeal(SealId);
	const bool bReady = IsReadyForInteraction();
	if (bCachedActive != bActive || bCachedReady != bReady) RefreshVisuals();
}

bool AAuraRitualSite::IsReadyForInteraction() const
{
	const AAuraGameState* State = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!State || State->IsRunCompleted()) return false;
	if (IsBossAltar())
	{
		return BossEncounter && State->AreAllSealsActivated() && !State->IsBossSummoned();
	}
	if (!AAuraGameState::RequiredSealIds().Contains(SealId) || !PrerequisiteEncounter
		|| State->HasActivatedSeal(SealId)) return false;
	const EAuraEncounterState RoomState = PrerequisiteEncounter->GetEncounterState();
	return RoomState == EAuraEncounterState::RewardAvailable
		|| RoomState == EAuraEncounterState::Completed
		|| State->IsRewardEncounterCleared(PrerequisiteEncounter->GetEncounterId());
}

bool AAuraRitualSite::CanInteract(const AAuraCharacter* Character,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!IsValid(Character) || Character->IsDead())
	{
		OutFailureReason = TEXT("角色必须存活");
		return false;
	}
	if (FVector::DistSquared2D(Character->GetActorLocation(), GetActorLocation())
		> FMath::Square(InteractionRadius)
		|| FMath::Abs(Character->GetActorLocation().Z - GetActorLocation().Z) > 230.f)
	{
		OutFailureReason = TEXT("请靠近仪式地点");
		return false;
	}
	return true;
}

bool AAuraRitualSite::TryInteract(AAuraCharacter* Character,
	FString& OutFailureReason)
{
	if (!HasAuthority() || !CanInteract(Character, OutFailureReason)) return false;
	AAuraGameState* State = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (!State)
	{
		OutFailureReason = TEXT("队伍任务状态不可用");
		return false;
	}
	if (!IsBossAltar())
	{
		if (!AAuraGameState::RequiredSealIds().Contains(SealId) || !PrerequisiteEncounter)
		{
			OutFailureReason = TEXT("封印配置不完整");
			return false;
		}
		if (State->HasActivatedSeal(SealId))
		{
			OutFailureReason = TEXT("这处封印已激活");
			return false;
		}
		const EAuraEncounterState RoomState = PrerequisiteEncounter->GetEncounterState();
		if (RoomState != EAuraEncounterState::RewardAvailable
			&& RoomState != EAuraEncounterState::Completed
			&& !State->IsRewardEncounterCleared(PrerequisiteEncounter->GetEncounterId()))
		{
			OutFailureReason = TEXT("请先击败该路线的守卫");
			return false;
		}
		if (!State->TryActivateSeal(SealId))
		{
			OutFailureReason = TEXT("封印激活失败");
			return false;
		}
	}
	else
	{
		if (!State->AreAllSealsActivated())
		{
			OutFailureReason = FString::Printf(TEXT("请先激活任意 %d 处封印（%d/%d）"),
				AAuraGameState::SealsNeededForBoss, State->GetActivatedSealCount(),
				AAuraGameState::SealsNeededForBoss);
			return false;
		}
		if (State->IsBossSummoned() || State->IsRunCompleted() || !BossEncounter)
		{
			OutFailureReason = TEXT("首领仪式尚未开放");
			return false;
		}
		if (!BossEncounter->ActivateFromRitual(Character) || !State->TryMarkBossSummoned())
		{
			OutFailureReason = TEXT("首领仪式启动失败");
			return false;
		}
	}
	MulticastPlayRitualCue();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get()))
		{
			Controller->ShowCoopMessage(IsBossAltar()
				? TEXT("逆向仪式已启动：首领显形") : TEXT("路线封印已激活"),
				IsBossAltar() ? FLinearColor(1.f, 0.18f, 0.08f)
					: FLinearColor(0.35f, 0.85f, 1.f), 2.5f);
		}
	}
	UE_LOG(LogAuraRitual, Display, TEXT("M14 ritual activated. Site=%s Seal=%s Boss=%s"),
		*GetName(), *SealId.ToString(), IsBossAltar() ? TEXT("true") : TEXT("false"));
	return true;
}

void AAuraRitualSite::RefreshBossGate()
{
	if (!HasAuthority() || !IsBossAltar() || !BossGate) return;
	const AAuraGameState* State = GetWorld()->GetGameState<AAuraGameState>();
	if (!State || State->IsBossSummoned() || State->IsRunCompleted()) return;
	BossGate->SetGateOpen(State->AreAllSealsActivated(), true);
}

void AAuraRitualSite::RefreshVisuals()
{
	if (GetNetMode() == NM_DedicatedServer || !InteractionLabel || !RitualLight) return;
	const AAuraGameState* State = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	bCachedActive = State && (IsBossAltar() ? State->IsBossSummoned()
		: State->HasActivatedSeal(SealId));
	bCachedReady = IsReadyForInteraction();
	const FString Label = bCachedActive ? (IsBossAltar() ? TEXT("仪式已完成")
		: TEXT("封印已激活")) : (IsBossAltar()
			? (bCachedReady ? TEXT("[F] 完成仪式") : TEXT("需要三处封印"))
			: (bCachedReady ? TEXT("[F] 激活路线封印") : TEXT("请先击败守卫")));
	InteractionLabel->SetText(FText::FromString(Label));
	const FLinearColor Color = bCachedActive ? FLinearColor(0.2f, 0.75f, 1.f)
		: (bCachedReady ? (IsBossAltar() ? FLinearColor(1.f, 0.18f, 0.06f)
			: FLinearColor(0.3f, 0.6f, 1.f)) : FLinearColor(0.35f, 0.32f, 0.46f));
	RitualLight->SetLightColor(Color);
	InteractionLabel->SetTextRenderColor(Color.ToFColor(true));
	RitualLight->SetIntensity(bCachedReady || bCachedActive ? 1700.f : 650.f);
}

void AAuraRitualSite::MulticastPlayRitualCue_Implementation()
{
	if (GetNetMode() != NM_DedicatedServer && ActivationEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ActivationEffect,
			GetActorLocation() + FVector(0.f, 0.f, 15.f), GetActorRotation(),
			IsBossAltar() ? FVector(1.5f) : FVector(0.7f));
	}
	RefreshVisuals();
}
