// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FCoverSystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

DECLARE_LOG_CATEGORY_EXTERN(LogCoverSystem, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(VLogLogCoverSystem, Log, All);

// Logging

#define COVER_LOG(Verbosity, Format, ...) \
{ \
UE_LOG(LogCoverSystem, Verbosity, Format, ##__VA_ARGS__); \
}

#define COVER_VLOG(Actor, Verbosity, Format, ...) \
{ \
UE_LOG(LogCoverSystem, Verbosity, Format, ##__VA_ARGS__); \
UE_VLOG(Actor, VLogLogCoverSystem, Verbosity, Format, ##__VA_ARGS__); \
}

#define SCREEN_LOG_COLOR(Text, Key, Color, ...) \
if(GEngine) \
{ \
	uint64 InnerKey = -1; \
	if (Key != NAME_None) \
	{ \
		InnerKey = GetTypeHash(Key); \
	} \
	GEngine->AddOnScreenDebugMessage(InnerKey, 2.0f, Color, FString::Printf(Text, ##__VA_ARGS__)); \
} \

#define SCREEN_LOG(Text, Key, ...) SCREEN_LOG_COLOR(Text, Key, FColor::Yellow, ##__VA_ARGS__)

//Current Class Name + Function Name where this is called!
#define CUR_CLASS_FUNC (FString(__FUNCTION__))

//Current Line Number in the code where this is called!
#define CUR_LINE (FString::FromInt(__LINE__))

//Line Number and Current Class Func where this is called!
#define CUR_LINE_CLASS_FUNC ("(" + CUR_LINE + ")" + "[" + CUR_CLASS_FUNC + "]")