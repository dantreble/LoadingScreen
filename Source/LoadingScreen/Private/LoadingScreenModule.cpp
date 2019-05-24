// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ILoadingScreenModule.h"
#include "LoadingScreenSettings.h"
#include "SSimpleLoadingScreen.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "LoadingScreen"

class FLoadingScreenModule : public ILoadingScreenModule
{
public:
	FLoadingScreenModule();

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool IsGameModule() const override
	{
		return true;
	}

private:
	void HandlePrepareLoadingScreen();

	void HandleMovieClipFinished(const FString& FinishedClip);

	void BeginLoadingScreen(const FLoadingScreenDescription& ScreenDescription);	

	TSharedPtr<class SSimpleLoadingScreen> WidgetLoadingScreen;
};

IMPLEMENT_MODULE(FLoadingScreenModule, LoadingScreen)

FLoadingScreenModule::FLoadingScreenModule()
{

}

void FLoadingScreenModule::StartupModule()
{
	if ( !IsRunningDedicatedServer() && FSlateApplication::IsInitialized() )
	{
		// Load for cooker reference
		const ULoadingScreenSettings* Settings = GetDefault<ULoadingScreenSettings>();
		for ( const FStringAssetReference& Ref : Settings->StartupScreen.Images )
		{
			Ref.TryLoad();
		}
		for ( const FStringAssetReference& Ref : Settings->DefaultScreen.Images )
		{
			Ref.TryLoad();
		}

		if ( IsMoviePlayerEnabled() )
		{			
			// Binds the delegate to auto fire the loading screen code when a level changes and when a movie finishes
			GetMoviePlayer()->OnPrepareLoadingScreen().AddRaw(this, &FLoadingScreenModule::HandlePrepareLoadingScreen);			
		}

		// Prepare the startup screen, the PrepareLoadingScreen callback won't be called
		// if we've already explictly setup the loading screen.
		BeginLoadingScreen(Settings->StartupScreen);
	}
}

void FLoadingScreenModule::ShutdownModule()
{
	if ( !IsRunningDedicatedServer() )
	{
		if (WidgetLoadingScreen.IsValid())
		{
			WidgetLoadingScreen.Reset();
		}				
		GetMoviePlayer()->OnPrepareLoadingScreen().RemoveAll(this);
	}
}

void FLoadingScreenModule::HandlePrepareLoadingScreen()
{
	const ULoadingScreenSettings* Settings = GetDefault<ULoadingScreenSettings>();
	BeginLoadingScreen(Settings->DefaultScreen);
}

void FLoadingScreenModule::HandleMovieClipFinished(const FString & FinishedClip)
{
	// If its not the last movie then try keep waiting
	if (!GetMoviePlayer()->IsLastMovieInPlaylist())
	{
		return;
	}

	// Unbind the delegate so we're not firing this multiple times
	GetMoviePlayer()->OnMovieClipFinished().RemoveAll(this);
	
	// Show the loading screen widget	
	if (WidgetLoadingScreen.IsValid())
	{
		WidgetLoadingScreen->HandleMoviesFinishedPlaying();
	}
}

void FLoadingScreenModule::BeginLoadingScreen(const FLoadingScreenDescription& ScreenDescription)
{
	if (WidgetLoadingScreen.IsValid())
	{
		WidgetLoadingScreen.Reset();
	}

	FLoadingScreenAttributes LoadingScreen;
	LoadingScreen.MinimumLoadingScreenDisplayTime = ScreenDescription.MinimumLoadingScreenDisplayTime;
	LoadingScreen.bAutoCompleteWhenLoadingCompletes = ScreenDescription.bAutoCompleteWhenLoadingCompletes;
	LoadingScreen.bMoviesAreSkippable = ScreenDescription.bMoviesAreSkippable;
	LoadingScreen.bWaitForManualStop = ScreenDescription.bWaitForManualStop;
	LoadingScreen.MoviePaths = ScreenDescription.MoviePaths;
	LoadingScreen.PlaybackType = ScreenDescription.PlaybackType;


	// Create and store widget
	WidgetLoadingScreen = SNew(SSimpleLoadingScreen, ScreenDescription)
		.bShowThrobber(ScreenDescription.Throbber.bShowThrobber)
		.ThrobberType(ScreenDescription.Throbber.ThrobberType)
		;
	LoadingScreen.WidgetLoadingScreen = WidgetLoadingScreen;

	// Incase we have no movie paths, this will force it to show the loading screen anyway
	if (LoadingScreen.MoviePaths.Num() == 0)
	{
		// Forces the movie player to create a movie streamer to actually show the widget and such
		LoadingScreen.MoviePaths.Add("");		
	}
	// If we have movies to show, then setup what happens if we're supposed to show ui otherwise skip this
	else
	{
		// Edgecase
		GetMoviePlayer()->OnMovieClipFinished().RemoveAll(this);
		
		GetMoviePlayer()->OnMovieClipFinished().AddRaw(this, &FLoadingScreenModule::HandleMovieClipFinished);
	}	

	// This happens last after everything has been prepared ahead of time
	GetMoviePlayer()->SetupLoadingScreen(LoadingScreen);
}


#undef LOCTEXT_NAMESPACE
