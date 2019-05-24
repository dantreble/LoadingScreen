// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SSimpleLoadingScreen.h"

#include "Slate/DeferredCleanupSlateBrush.h"
#include "SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "SSafeZone.h"
#include "SThrobber.h"
#include "SDPIScaler.h"
#include "SlateApplication.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"

#define LOCTEXT_NAMESPACE "LoadingScreen"

/////////////////////////////////////////////////////
// SSimpleLoadingScreen

static float PointSizeToSlateUnits(float PointSize)
{
	//FreeTypeConstants::HorizontalDPI = 96;
	const float SlateFreeTypeHorizontalResolutionDPI = 96.0f;
	const float FreeTypeNativeDPI = 72.0f;
	const float PixelSize = PointSize * (SlateFreeTypeHorizontalResolutionDPI / FreeTypeNativeDPI);

	return PixelSize;
}

void SSimpleLoadingScreen::Construct(const FArguments& InArgs, const FLoadingScreenDescription& InScreenDescription)
{	
	LastToolTipUpdate = -1.0f;

	ScreenDescriptionInfo = InScreenDescription;

	// Only show on construct if UI is true and we're not showing it after movies
	const bool bShowUiOnConstruct = (ScreenDescriptionInfo.bShowUIOverlay && !ScreenDescriptionInfo.bShowUiAfterMovies);
	bShowThrobber = InArgs._bShowThrobber;
	ThrobberType = InArgs._ThrobberType;	

	// Construct the root of this widget
	TSharedRef<SOverlay> Root = SNew(SOverlay);

	// If there's an image defined
	if (ScreenDescriptionInfo.Images.Num() > 0)
	{
		// Construct a random image to use for the loading screen
		const int32 ImageIndex = FMath::RandRange(0, ScreenDescriptionInfo.Images.Num() - 1);
		const FStringAssetReference& ImageAsset = ScreenDescriptionInfo.Images[ImageIndex];
		UObject* ImageObject = ImageAsset.TryLoad();
		if ( UTexture2D* LoadingImage = Cast<UTexture2D>(ImageObject) )
		{
			FVector2D Size = FVector2D(LoadingImage->GetSizeX(), LoadingImage->GetSizeY());
			LoadingScreenBrush = FDeferredCleanupSlateBrush::CreateBrush(LoadingImage, Size);
			//LoadingImage, Size, FName(*ImageAsset.ToString()))
			// Adds a slot to the root then add that image to the widget, renders over the movie if supposed to
			BackgroundImageWidget = SNew(SImage)
				.Visibility(ScreenDescriptionInfo.bShowImagesAfterMovies ? EVisibility::Hidden : EVisibility::SelfHitTestInvisible)
				.Image(LoadingScreenBrush->GetSlateBrush());

			Root->AddSlot(0)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)	
			[
				SNew(SScaleBox)
				.Stretch(ScreenDescriptionInfo.ImageStretch)
				[
					BackgroundImageWidget.ToSharedRef()
				]
			];
		}
	}	

	// Handles creating the throbber widget	
	{
		// Decides which throbber type to show
		switch (ThrobberType)
		{
		case EThrobberLoadingType::TLT_Circular:
		{
			// Old comment information may be of use in the future
				// Convert font size to pixels, pixel_size = point_size * resolution / 72, then half it to get radius
				// (Settings->LoadingFont.Size * 96.0f / 72.0f) / 2.0f

			ThrobberWidget = SNew(SCircularThrobber)
				.Radius(ScreenDescriptionInfo.Throbber.ThrobberRadius)
				.Period(ScreenDescriptionInfo.Throbber.ThrobberPeriod)
				.NumPieces(ScreenDescriptionInfo.Throbber.NumPiecesThrobber)				
				.PieceImage(&ScreenDescriptionInfo.Throbber.ThrobberImage);
			break;
		}
		case EThrobberLoadingType::TLT_Regular:
		default:
		{			
			const int32 AnimationParams = (ScreenDescriptionInfo.Throbber.AnimateVertically ? SThrobber::Vertical : 0) |
				(ScreenDescriptionInfo.Throbber.AnimateHorizontally ? SThrobber::Horizontal : 0) |
				(ScreenDescriptionInfo.Throbber.AnimateOpacity ? SThrobber::Opacity : 0);

			const SThrobber::EAnimation Animation = static_cast<SThrobber::EAnimation>(AnimationParams);

			ThrobberWidget = SNew(SThrobber)
				.Animate(Animation)
				.NumPieces(ScreenDescriptionInfo.Throbber.NumPiecesThrobber)				
				.PieceImage(&ScreenDescriptionInfo.Throbber.ThrobberImage);
			break;
		}
		}
				
		// Handles flipping the widget if needed
		ThrobberWidget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		const float ThrobberScale = (float)((ScreenDescriptionInfo.Throbber.bFlipThrobberAnimation) ? -1 : 1);
		ThrobberWidget->SetRenderTransform(FSlateRenderTransform(FScale2D(ThrobberScale, 1.0f)));
		ThrobberWidget->SetVisibility((bShowThrobber && bShowUiOnConstruct) ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);
	}

	// Handles creating the tip text widget
	if (ScreenDescriptionInfo.LoadingScreenTips.Tips.Num() > 0)
	{					
		CurrentToolTipIndex = FMath::RandRange(0, ScreenDescriptionInfo.LoadingScreenTips.Tips.Num() - 1);
		
		CurrentToolTipWidget = SNew(STextBlock)
			.Visibility((ScreenDescriptionInfo.LoadingScreenTips.SlotText.bShouldShowText && bShowUiOnConstruct) 
				? EVisibility::SelfHitTestInvisible : EVisibility::Hidden)
			.WrapTextAt(ScreenDescriptionInfo.LoadingScreenTips.SlotText.WrapAt)
			.Font(ScreenDescriptionInfo.LoadingScreenTips.SlotText.Font)
			.ColorAndOpacity(ScreenDescriptionInfo.LoadingScreenTips.SlotText.TextColor)
			.Justification(ScreenDescriptionInfo.LoadingScreenTips.SlotText.TextJustification)
			.Text(ScreenDescriptionInfo.LoadingScreenTips.Tips[CurrentToolTipIndex]);
		
		LastToolTipUpdate = FSlateApplication::Get().GetCurrentTime();
	}

	// Construct Description Text
	{
		DescriptionTextWidget = SNew(STextBlock)
			.Text(ScreenDescriptionInfo.LoadingScreenDescription.Text)
			.Font(ScreenDescriptionInfo.LoadingScreenDescription.SlotText.Font)
			.ColorAndOpacity(ScreenDescriptionInfo.LoadingScreenDescription.SlotText.TextColor)
			.Justification(ScreenDescriptionInfo.LoadingScreenDescription.SlotText.TextJustification)
			.WrapTextAt(ScreenDescriptionInfo.LoadingScreenDescription.SlotText.WrapAt)
			.Visibility((ScreenDescriptionInfo.LoadingScreenDescription.SlotText.bShouldShowText && bShowUiOnConstruct)
				? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);
	}

	// Construct Loading text	
	{
		LoadingTextWidget = SNew(STextBlock)
			.Text(ScreenDescriptionInfo.LoadingScreenText.Text)
			.Font(ScreenDescriptionInfo.LoadingScreenText.SlotText.Font)
			.ColorAndOpacity(ScreenDescriptionInfo.LoadingScreenText.SlotText.TextColor)
			.Justification(ScreenDescriptionInfo.LoadingScreenText.SlotText.TextJustification)
			.Visibility((ScreenDescriptionInfo.LoadingScreenText.SlotText.bShouldShowText && bShowUiOnConstruct)
				? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);
	}

	// Adds a slot to the root of this widget to allow for other widgets to be added to it, renders over image/movie
	Root->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SSafeZone)
		[
			SNew(SConstraintCanvas)

			// Adds the circular throbber to the canvas
		+ SConstraintCanvas::Slot()
		.Anchors(ScreenDescriptionInfo.Throbber.ThrobberSlotPosition.Anchors)
		.Offset(FMargin(ScreenDescriptionInfo.Throbber.ThrobberSlotPosition.Offset))
		.Alignment(ScreenDescriptionInfo.Throbber.ThrobberSlotPosition.Alignment)
		.AutoSize(true)
		.ZOrder(1)
		[
			ThrobberWidget.ToSharedRef()
		]

	// Adds the tip text to the canvas
	+ SConstraintCanvas::Slot()
		.Anchors(ScreenDescriptionInfo.LoadingScreenTips.SlotPosition.Anchors)
		.Offset(FMargin(ScreenDescriptionInfo.LoadingScreenTips.SlotPosition.Offset))
		.Alignment(ScreenDescriptionInfo.LoadingScreenTips.SlotPosition.Alignment)
		.AutoSize(true)
		.ZOrder(1)
		[
			CurrentToolTipWidget.ToSharedRef()
		]

	// Adds the description text to the canvas
	+ SConstraintCanvas::Slot()
		.Anchors(ScreenDescriptionInfo.LoadingScreenDescription.SlotPosition.Anchors)
		.Offset(FMargin(ScreenDescriptionInfo.LoadingScreenDescription.SlotPosition.Offset))
		.Alignment(ScreenDescriptionInfo.LoadingScreenDescription.SlotPosition.Alignment)
		.AutoSize(true)
		.ZOrder(2)
		[
			DescriptionTextWidget.ToSharedRef()
		]

	// Adds the loading text to the canvas
	+ SConstraintCanvas::Slot()
		.Anchors(ScreenDescriptionInfo.LoadingScreenText.SlotPosition.Anchors)
		.Offset(FMargin(ScreenDescriptionInfo.LoadingScreenText.SlotPosition.Offset))
		.Alignment(ScreenDescriptionInfo.LoadingScreenText.SlotPosition.Alignment)
		.AutoSize(true)
		.ZOrder(3)
		[
			LoadingTextWidget.ToSharedRef()
		]
		]
	];
	
	ChildSlot
	[
		Root
	];
}

void SSimpleLoadingScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{		
	if ((InCurrentTime - LastToolTipUpdate) > 5.0f)
	{
		LastToolTipUpdate = InCurrentTime;

		// Safety check that it is a textblock and not something else
		if (CurrentToolTipWidget->GetType() == "STextBlock")
		{
			STextBlock* TextBlock = static_cast<STextBlock*>(&CurrentToolTipWidget.ToSharedRef().Get());
			TextBlock->SetText(GetRandomToolTip());
		}		
	}
}

void SSimpleLoadingScreen::HandleMoviesFinishedPlaying()
{	
	// Show the background if we're allowed to
	if (ScreenDescriptionInfo.bShowImagesAfterMovies)
	{
		BackgroundImageWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
	}

	// Show ui elements if we're showing ui and we can show it after movies
	if (ScreenDescriptionInfo.bShowUiAfterMovies && ScreenDescriptionInfo.bShowUIOverlay)
	{
		ThrobberWidget->SetVisibility(ScreenDescriptionInfo.Throbber.bShowThrobber ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);

		LoadingTextWidget->SetVisibility(ScreenDescriptionInfo.LoadingScreenText.SlotText.bShouldShowText ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);

		DescriptionTextWidget->SetVisibility(ScreenDescriptionInfo.LoadingScreenDescription.SlotText.bShouldShowText ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);

		CurrentToolTipWidget->SetVisibility(ScreenDescriptionInfo.LoadingScreenTips.SlotText.bShouldShowText ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);
	}
}

float SSimpleLoadingScreen::GetDPIScale() const
{
	const FVector2D& DrawSize = GetCachedGeometry().ToPaintGeometry().GetLocalSize();
	const FIntPoint Size((int32)DrawSize.X, (int32)DrawSize.Y);
	return GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
}

FText SSimpleLoadingScreen::GetRandomToolTip() 
{	

	// Decides a random tip to show
	{
		const int32 Total = ScreenDescriptionInfo.LoadingScreenTips.Tips.Num();
		int32 RandomTip = FMath::RandRange(0, Total - 1);
		// If there's only one then do nothing
		if (Total == 1)
		{
			RandomTip = 0;
		}
		else
		{
			// Find a random index that is not currently used
			while (RandomTip == CurrentToolTipIndex)
			{
				// Randomize the index
				RandomTip = FMath::RandRange(0, Total - 1);
			}
		}

		// Update current index to be this randomized one
		CurrentToolTipIndex = RandomTip;
	}

	return ScreenDescriptionInfo.LoadingScreenTips.Tips[CurrentToolTipIndex];
}

bool SSimpleLoadingScreen::CanShowToolTip() const
{	
	return (ScreenDescriptionInfo.LoadingScreenTips.Tips.Num() > 0);
}

#undef LOCTEXT_NAMESPACE
