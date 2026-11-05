// Copyright yosep

#include "UI/AuraServiceMenuWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Level/AuraCampVendor.h"
#include "Player/AuraPlayerController.h"

void UAuraServiceActionButton::InitializeAction(
	AAuraPlayerController* InController, const bool bInMageMenu, const int32 InAction)
{
	Controller = InController;
	bMageMenu = bInMageMenu;
	Action = InAction;
	InitIsFocusable(false);
	SetBackgroundColor(FLinearColor::Transparent);
	OnClicked.AddUniqueDynamic(this, &UAuraServiceActionButton::HandleClicked);
}

void UAuraServiceActionButton::HandleClicked()
{
	if (AAuraPlayerController* Owner = Controller.Get())
	{
		if (bMageMenu) Owner->HandleArchmageMenuAction(Action);
		else Owner->HandleCampMenuAction(Action);
	}
}

void UAuraServiceMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;
	for (int32 Action = 1; Action <= 16; ++Action)
	{
		UAuraServiceActionButton* Button = WidgetTree->ConstructWidget<UAuraServiceActionButton>(
			UAuraServiceActionButton::StaticClass());
		Root->AddChildToCanvas(Button);
		Button->SetVisibility(ESlateVisibility::Collapsed);
		ActionButtons.Add(Button);
	}
}

void UAuraServiceMenuWidget::InitializeMenu(
	AAuraPlayerController* InController, const bool bInMageMenu)
{
	Controller = InController;
	bMageMenu = bInMageMenu;
	for (int32 Index = 0; Index < ActionButtons.Num(); ++Index)
	{
		ActionButtons[Index]->InitializeAction(InController, bInMageMenu, Index + 1);
	}
	RefreshActionLayout();
}

bool UAuraServiceMenuWidget::ClickActionForSmoke(const int32 Action)
{
	RefreshActionLayout();
	UAuraServiceActionButton* Button = ActionButtons.IsValidIndex(Action - 1)
		? ActionButtons[Action - 1] : nullptr;
	if (!Button || !Button->IsVisible()) return false;
	Button->OnClicked.Broadcast();
	return true;
}

void UAuraServiceMenuWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshActionLayout();
}

void UAuraServiceMenuWidget::RefreshActionLayout()
{
	AAuraPlayerController* Owner = Controller.Get();
	if (!Owner || (bMageMenu ? !Owner->IsArchmageMenuOpen() : !Owner->IsCampMenuOpen())) return;
	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this);
	const float Dpi = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
	const float Scale = FMath::Clamp(FMath::Min(Viewport.X / 1280.f,
		Viewport.Y / 720.f), 0.75f, 2.f);
	const FVector2D PanelSize = bMageMenu ? FVector2D(570.f, 430.f) : FVector2D(860.f, 580.f);
	const FVector2D PanelOrigin = (Viewport - PanelSize * Scale) * 0.5f;
	const auto Place = [&](const int32 Action, const bool bVisible,
		const float X, const float Y, const float Width, const float Height)
	{
		UAuraServiceActionButton* Button = ActionButtons.IsValidIndex(Action - 1)
			? ActionButtons[Action - 1] : nullptr;
		if (!Button) return;
		Button->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (!bVisible) return;
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Button->Slot))
		{
			Slot->SetPosition((PanelOrigin + FVector2D(X, Y) * Scale) / Dpi);
			Slot->SetSize(FVector2D(Width, Height) * Scale / Dpi);
		}
	};
	for (int32 Action = 1; Action <= ActionButtons.Num(); ++Action)
	{
		ActionButtons[Action - 1]->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (bMageMenu)
	{
		const int32 Pending = Owner->GetArchmagePendingAction();
		Place(9, true, 505.f, 16.f, 48.f, 33.f);
		if (Pending == 0)
		{
			const int32 Actions[] = {2, 3, 4, 5};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Actions); ++Index)
				Place(Actions[Index], true, 16.f, 122.f + Index * 46.f, 538.f, 38.f);
		}
		else
		{
			if (Pending == 5 || (Pending >= 11 && Pending <= 13))
			{
				for (int32 Action = 2; Action <= (Pending >= 11 ? 3 : 4); ++Action)
					Place(Action, true, 16.f, 167.f + (Action - 2) * 43.f,
						Pending == 5
							&& Owner->GetMageDraftRank(static_cast<EAuraPlayerSkill>(Action - 2)) >= 3
							? 200.f : 538.f, 37.f);
				if (Pending == 5)
				{
					Place(8, true, 430.f, 295.f, 118.f, 38.f);
					const bool bPathVisible = Owner->GetMageDraftRank(EAuraPlayerSkill::FireBolt) >= 3;
					Place(11, bPathVisible, 221.f, 167.f, 156.f, 37.f);
					Place(12, bPathVisible, 380.f, 167.f, 174.f, 37.f);
					for (int32 I = 1; I < 3; ++I)
					{
						const bool bVisible = Owner->GetMageDraftRank(static_cast<EAuraPlayerSkill>(I)) >= 3;
						Place(11 + I*2, bVisible, 221.f, 167.f + I*43.f, 156.f, 37.f);
						Place(12 + I*2, bVisible, 380.f, 167.f + I*43.f, 174.f, 37.f);
					}
				}
			}
			Place(6, true, 22.f, 349.f, 242.f, 41.f);
			Place(7, true, 306.f, 349.f, 242.f, 41.f);
		}
		return;
	}
	Place(15, true, 808.f, 16.f, 38.f, 33.f);
	const AAuraCampVendor* Vendor = Owner->GetActiveCampVendor();
	if (Vendor && Vendor->GetServiceType() == EAuraCampService::Merchant)
	{
		Place(11, true, 540.f, 16.f, 128.f, 33.f);
		Place(12, true, 676.f, 16.f, 128.f, 33.f);
	}
	else if (Vendor && Vendor->GetServiceType() == EAuraCampService::Blacksmith)
	{
		Place(11, true, 540.f, 16.f, 80.f, 33.f);
		Place(12, true, 628.f, 16.f, 80.f, 33.f);
		Place(16, true, 716.f, 16.f, 80.f, 33.f);
	}
	const int32 Remaining = Owner->BuildCampServiceRows().Num() - Owner->GetCampPage() * 8;
	for (int32 Action = 1; Action <= FMath::Clamp(Remaining, 0, 8); ++Action)
		Place(Action, true, 18.f, 100.f + (Action - 1) * 45.f, 504.f, 38.f);
	Place(13, true, 24.f, 468.f, 106.f, 38.f);
	Place(14, true, 414.f, 468.f, 106.f, 38.f);
	if (Owner->IsCampConfirming())
	{
		Place(9, true, 24.f, 514.f, 236.f, 41.f);
		Place(10, true, 280.f, 514.f, 240.f, 41.f);
	}
}
