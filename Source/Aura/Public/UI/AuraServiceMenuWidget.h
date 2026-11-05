// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "AuraServiceMenuWidget.generated.h"

class AAuraPlayerController;

/** A real Slate hit target for one action in the Canvas-painted NPC panel. */
UCLASS()
class AURA_API UAuraServiceActionButton : public UButton
{
	GENERATED_BODY()

public:
	void InitializeAction(AAuraPlayerController* InController, bool bInMageMenu, int32 InAction);

private:
	UFUNCTION()
	void HandleClicked();

	TWeakObjectPtr<AAuraPlayerController> Controller;
	bool bMageMenu = false;
	int32 Action = INDEX_NONE;
};

/** Transparent UMG buttons aligned over the existing Chinese HUD artwork. */
UCLASS()
class AURA_API UAuraServiceMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeMenu(AAuraPlayerController* InController, bool bInMageMenu);
	bool IsMageMenu() const { return bMageMenu; }
	/** Exercises the bound UButton callback in automated game-mode smoke tests. */
	bool ClickActionForSmoke(int32 Action);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void RefreshActionLayout();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAuraServiceActionButton>> ActionButtons;

	TWeakObjectPtr<AAuraPlayerController> Controller;
	bool bMageMenu = false;
};
