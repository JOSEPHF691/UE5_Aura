#pragma once

#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "UObject/ConstructorHelpers.h"

namespace AuraWorldTextFont
{
	inline void Apply(UTextRenderComponent* Label)
	{
		// Dedicated servers strip font data and never draw world-space labels.
		if (!Label || IsRunningDedicatedServer()) return;
		static ConstructorHelpers::FObjectFinder<UFont> ChineseFont(
			TEXT("/Game/Aura/UI/Fonts/F_AuraWorldChinese.F_AuraWorldChinese"));
		if (Label && ChineseFont.Succeeded())
		{
			Label->SetFont(ChineseFont.Object);
		}
	}
}
