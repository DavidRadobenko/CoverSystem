// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoverSystem.h"

#define LOCTEXT_NAMESPACE "FCoverSystemModule"

void FCoverSystemModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FCoverSystemModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCoverSystemModule, CoverSystem)

DEFINE_LOG_CATEGORY(LogCoverSystem);
DEFINE_LOG_CATEGORY(VLogLogCoverSystem);