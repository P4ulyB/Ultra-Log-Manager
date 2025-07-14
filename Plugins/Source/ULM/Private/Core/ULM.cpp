#include "Core/ULM.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FULMModule"

void FULMModule::StartupModule()
{
#if !UE_BUILD_SHIPPING
	// Add basic diagnostics to verify module is loading (development builds only)
	UE_LOG(LogTemp, Warning, TEXT("[ULM MODULE] FULMModule::StartupModule() called"));
#endif
}

void FULMModule::ShutdownModule()
{
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Warning, TEXT("[ULM MODULE] FULMModule::ShutdownModule() called"));
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FULMModule, ULM)