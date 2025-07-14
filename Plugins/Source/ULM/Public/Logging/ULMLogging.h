#pragma once

#include "Channels/ULMLogCategories.h"
#include "CoreMinimal.h"
#include "Channels/ULMChannel.h"
#include "Engine/World.h"
#include "HAL/PlatformFilemanager.h"
#include "UObject/Object.h"
#include "Misc/Paths.h"
#include "FileIO/ULMInternalPath.h"
#include <atomic>

// Forward declarations for performance
class UULMSubsystem;
class FULMChannelRegistry;

// Thread-safe global state access
extern ULM_API std::atomic<FULMChannelRegistry*> GULMChannelRegistry;
extern ULM_API std::atomic<UULMSubsystem*> GULMSubsystem;

/**
 * High-performance logging macros for ULM system
 * Designed for <0.01ms per call performance target
 */

/**
 * Fast channel state lookup with caching
 */
namespace ULMInternal
{
	// Thread-safe channel state cache using values instead of pointers
	struct FCachedChannelState
	{
		bool bEnabled;
		EULMVerbosity MinVerbosity;
		int32 MaxLogEntries;
		bool bIsValid;
		
		FCachedChannelState() : bEnabled(false), MinVerbosity(EULMVerbosity::Message), MaxLogEntries(1000), bIsValid(false) {}
		FCachedChannelState(const FULMChannelState& State) 
			: bEnabled(State.bEffectiveEnabled), MinVerbosity(State.EffectiveMinVerbosity), MaxLogEntries(State.EffectiveMaxEntries), bIsValid(true) {}
	};
	
	thread_local static TMap<FString, FCachedChannelState> ChannelStateCache;
	thread_local static double LastCacheFlush = 0.0;
	thread_local static FULMChannelRegistry* LastRegistryPtr = nullptr;
	
	inline bool GetCachedChannelState(const FString& ChannelName, FCachedChannelState& OutState)
	{
		const double CurrentTime = FPlatformTime::Seconds();
		FULMChannelRegistry* Registry = GULMChannelRegistry.load(std::memory_order_acquire);
		
		// Invalidate cache if registry changed or time expired
		if (Registry != LastRegistryPtr || CurrentTime - LastCacheFlush > 5.0)
		{
			ChannelStateCache.Empty();
			LastCacheFlush = CurrentTime;
			LastRegistryPtr = Registry;
		}
		
		if (!Registry)
		{
			return false;
		}
		
		if (FCachedChannelState* CachedState = ChannelStateCache.Find(ChannelName))
		{
			OutState = *CachedState;
			return CachedState->bIsValid;
		}
		
		// Get fresh state and cache the VALUES, not the pointer
		const FULMChannelState* State = Registry->GetChannelState(ChannelName);
		if (State)
		{
			FCachedChannelState NewCachedState(*State);
			ChannelStateCache.Emplace(ChannelName, NewCachedState);
			OutState = NewCachedState;
			return true;
		}
		
		// Cache invalid state to avoid repeated lookups
		ChannelStateCache.Emplace(ChannelName, FCachedChannelState());
		return false;
	}
	
	// Parameter validation helpers
	inline bool IsValidChannel(const FString& ChannelName)
	{
		return !ChannelName.IsEmpty() && ChannelName.Len() <= 64; // Reasonable channel name limit
	}
	
	inline bool IsValidVerbosity(EULMVerbosity Verbosity)
	{
		return Verbosity >= EULMVerbosity::Message && Verbosity <= EULMVerbosity::Critical;
	}
	
	inline bool IsValidFormat(const TCHAR* Format)
	{
		return Format != nullptr && FCString::Strlen(Format) > 0 && FCString::Strlen(Format) <= 8192; // Reasonable format string limit
	}
	
	// Fast authority check for network context
	inline bool HasNetworkAuthority(const UObject* WorldContext)
	{
		if (!WorldContext)
		{
			return true; // Default to logging if no context
		}
		
		if (const UWorld* World = WorldContext->GetWorld())
		{
			return World->GetNetMode() != NM_Client;
		}
		
		return true;
	}
	
	// Extract function name from full function signature
	inline FString ExtractFunctionName(const char* FullFunction)
	{
		FString FullFunctionStr(ANSI_TO_TCHAR(FullFunction));
		
		// Find the last occurrence of '::' to get just the function name
		int32 LastScopeIndex = FullFunctionStr.Find(TEXT("::"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastScopeIndex != INDEX_NONE)
		{
			return FullFunctionStr.RightChop(LastScopeIndex + 2);
		}
		
		// If no scope operator found, return the original string
		return FullFunctionStr;
	}
	
	// Extract class name from full function signature
	inline FString ExtractClassName(const char* FullFunction)
	{
		FString FullFunctionStr(ANSI_TO_TCHAR(FullFunction));
		
		// Find the last occurrence of '::' to get the class name
		int32 LastScopeIndex = FullFunctionStr.Find(TEXT("::"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastScopeIndex != INDEX_NONE)
		{
			// Find the previous '::' or beginning of string
			int32 PrevScopeIndex = FullFunctionStr.Find(TEXT("::"), ESearchCase::IgnoreCase, ESearchDir::FromEnd, LastScopeIndex - 1);
			if (PrevScopeIndex != INDEX_NONE)
			{
				return FullFunctionStr.Mid(PrevScopeIndex + 2, LastScopeIndex - PrevScopeIndex - 2);
			}
			else
			{
				return FullFunctionStr.Left(LastScopeIndex);
			}
		}
		
		// If no scope operator found, return "Global"
		return TEXT("Global");
	}
	
	// Get class name from UObject context
	inline FString GetObjectClassName(const UObject* Object)
	{
		if (Object && Object->GetClass())
		{
			return Object->GetClass()->GetName();
		}
		return TEXT("Unknown");
	}
	
	// Sampling state for rate-limited logging
	struct FSamplingState
	{
		double LastLogTime = 0.0;
		uint32 MessageCount = 0;
		uint32 SampleRate = 100; // Log every 100th message by default
	};
	
	thread_local static TMap<FString, FSamplingState> SamplingStates;
	
	inline bool ShouldSample(const FString& ChannelName, uint32 CustomSampleRate = 0)
	{
		FSamplingState& State = SamplingStates.FindOrAdd(ChannelName);
		
		if (CustomSampleRate > 0)
		{
			State.SampleRate = CustomSampleRate;
		}
		
		State.MessageCount++;
		
		if (State.MessageCount >= State.SampleRate)
		{
			State.MessageCount = 0;
			State.LastLogTime = FPlatformTime::Seconds();
			return true;
		}
		
		return false;
	}
}

/**
 * Core logging function - optimized for performance
 */
ULM_API void ULMLogMessage(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const UObject* WorldContext = nullptr, const char* FileName = nullptr, int32 LineNumber = 0);

/**
 * Critical system logging function - bypasses initialization checks for early system logs
 */
ULM_API void ULMLogCriticalSystem(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const char* FileName = nullptr, int32 LineNumber = 0);

/**
 * Network-aware logging functions
 */
ULM_API void ULMLogMessageServer(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const UObject* WorldContext, const char* FileName = nullptr, int32 LineNumber = 0);
ULM_API void ULMLogMessageClient(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const UObject* WorldContext, const char* FileName = nullptr, int32 LineNumber = 0);

/**
 * Sampled logging for high-frequency messages
 */
ULM_API void ULMLogMessageSampled(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, uint32 SampleRate = 100, const UObject* WorldContext = nullptr, const char* FileName = nullptr, int32 LineNumber = 0);


// Core logging macros with compile-time optimizations
// Critical system logging - bypasses channel state checks for early initialization logging
#define ULM_LOG_CRITICAL_SYSTEM(Channel, Verbosity, Format, ...) \
	do { \
		const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
		ULMLogCriticalSystem(Channel, Verbosity, Msg, __FILE__, __LINE__); \
	} while(0)

#define ULM_LOG(Channel, Verbosity, Format, ...) \
	do { \
		if (ULMInternal::IsValidChannel(Channel) && ULMInternal::IsValidVerbosity(Verbosity) && ULMInternal::IsValidFormat(Format)) \
		{ \
			ULMInternal::FCachedChannelState CachedState; \
			if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
			{ \
				const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
				ULMLogMessage(Channel, Verbosity, Msg, nullptr, __FILE__, __LINE__); \
			} \
		} \
	} while(0)

#define ULM_LOG_SERVER(Channel, Verbosity, Format, ...) \
	do { \
		if (ULMInternal::IsValidChannel(Channel) && ULMInternal::IsValidVerbosity(Verbosity) && ULMInternal::IsValidFormat(Format)) \
		{ \
			ULMInternal::FCachedChannelState CachedState; \
			if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
			{ \
				const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
				ULMLogMessageServer(Channel, Verbosity, Msg, Cast<UObject>(this), __FILE__, __LINE__); \
			} \
		} \
	} while(0)

#define ULM_LOG_CLIENT(Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			ULMLogMessageClient(Channel, Verbosity, Msg, Cast<UObject>(this), __FILE__, __LINE__); \
		} \
	} while(0)

#define ULM_LOG_SAMPLED(Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity && ULMInternal::ShouldSample(Channel)) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			ULMLogMessage(Channel, Verbosity, Msg, nullptr, __FILE__, __LINE__); \
		} \
	} while(0)

#define ULM_LOG_SAMPLED_RATE(Channel, Verbosity, Rate, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity && ULMInternal::ShouldSample(Channel, Rate)) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			ULMLogMessage(Channel, Verbosity, Msg, nullptr, __FILE__, __LINE__); \
		} \
	} while(0)

// Context-aware macros that automatically provide world context
#define ULM_LOG_OBJECT(Object, Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			ULMLogMessage(Channel, Verbosity, Msg, Object, __FILE__, __LINE__); \
		} \
	} while(0)

// Conditional logging macros for common scenarios
#define ULM_LOG_IF(Condition, Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if ((Condition) && ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			ULMLogMessage(Channel, Verbosity, Msg, nullptr, __FILE__, __LINE__); \
		} \
	} while(0)

// Convenience macros for common verbosity levels
#define ULM_MESSAGE(Channel, Format, ...) ULM_LOG(Channel, EULMVerbosity::Message, Format, ##__VA_ARGS__)
#define ULM_WARNING(Channel, Format, ...) ULM_LOG(Channel, EULMVerbosity::Warning, Format, ##__VA_ARGS__)
#define ULM_ERROR(Channel, Format, ...) ULM_LOG(Channel, EULMVerbosity::Error, Format, ##__VA_ARGS__)
#define ULM_CRITICAL(Channel, Format, ...) ULM_LOG(Channel, EULMVerbosity::Critical, Format, ##__VA_ARGS__)

// Enhanced logging macros with improved Rider formatting and class name extraction
// Compact macro with Rider-compatible format: includes function name in the message
#define ULM_LOG_COMPACT(Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
		{ \
			const FString FunctionName = ULMInternal::ExtractFunctionName(__FUNCTION__); \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			const FString LogMsg = FString::Printf(TEXT("%s: %s"), *FunctionName, *Msg); \
			ULMLogMessage(Channel, Verbosity, LogMsg, nullptr, __FILE__, __LINE__); \
		} \
	} while(0)

// Enhanced macro with class/function context (Rider will make file paths clickable via __FILE__/__LINE__ params)
#define ULM_LOG_ENHANCED(Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			const FString ClassName = ULMInternal::ExtractClassName(__FUNCTION__); \
			const FString FunctionName = ULMInternal::ExtractFunctionName(__FUNCTION__); \
			const FString LogMsg = FString::Printf(TEXT("[%s::%s] %s"), \
				*ClassName, *FunctionName, *Msg); \
			ULMLogMessage(Channel, Verbosity, LogMsg, nullptr, __FILE__, __LINE__); \
		} \
	} while(0)

// Class-aware macro for use within UObject-derived classes
#define ULM_LOG_CLASS(Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			const FString ClassName = ULMInternal::GetObjectClassName(this); \
			const FString FunctionName = ULMInternal::ExtractFunctionName(__FUNCTION__); \
			const FString LogMsg = FString::Printf(TEXT("[%s::%s] %s"), \
				*ClassName, *FunctionName, *Msg); \
			ULMLogMessage(Channel, Verbosity, LogMsg, Cast<UObject>(this), __FILE__, __LINE__); \
		} \
	} while(0)

// Object-enhanced macro with explicit object parameter
#define ULM_LOG_OBJECT_ENHANCED(Object, Channel, Verbosity, Format, ...) \
	do { \
		ULMInternal::FCachedChannelState CachedState; \
		if (ULMInternal::GetCachedChannelState(Channel, CachedState) && CachedState.bEnabled && Verbosity >= CachedState.MinVerbosity) \
		{ \
			const FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
			const FString ClassName = ULMInternal::GetObjectClassName(Object); \
			const FString FunctionName = ULMInternal::ExtractFunctionName(__FUNCTION__); \
			const FString LogMsg = FString::Printf(TEXT("[%s::%s] %s"), \
				*ClassName, *FunctionName, *Msg); \
			ULMLogMessage(Channel, Verbosity, LogMsg, Object, __FILE__, __LINE__); \
		} \
	} while(0)

// Enhanced convenience macros for common verbosity levels
#define ULM_MESSAGE_ENHANCED(Channel, Format, ...) ULM_LOG_ENHANCED(Channel, EULMVerbosity::Message, Format, ##__VA_ARGS__)
#define ULM_WARNING_ENHANCED(Channel, Format, ...) ULM_LOG_ENHANCED(Channel, EULMVerbosity::Warning, Format, ##__VA_ARGS__)
#define ULM_ERROR_ENHANCED(Channel, Format, ...) ULM_LOG_ENHANCED(Channel, EULMVerbosity::Error, Format, ##__VA_ARGS__)
#define ULM_CRITICAL_ENHANCED(Channel, Format, ...) ULM_LOG_ENHANCED(Channel, EULMVerbosity::Critical, Format, ##__VA_ARGS__)

// Enhanced class-aware convenience macros
#define ULM_MESSAGE_CLASS(Channel, Format, ...) ULM_LOG_CLASS(Channel, EULMVerbosity::Message, Format, ##__VA_ARGS__)
#define ULM_WARNING_CLASS(Channel, Format, ...) ULM_LOG_CLASS(Channel, EULMVerbosity::Warning, Format, ##__VA_ARGS__)
#define ULM_ERROR_CLASS(Channel, Format, ...) ULM_LOG_CLASS(Channel, EULMVerbosity::Error, Format, ##__VA_ARGS__)
#define ULM_CRITICAL_CLASS(Channel, Format, ...) ULM_LOG_CLASS(Channel, EULMVerbosity::Critical, Format, ##__VA_ARGS__)

// Compact convenience macros with function name included
#define ULM_MESSAGE_COMPACT(Channel, Format, ...) ULM_LOG_COMPACT(Channel, EULMVerbosity::Message, Format, ##__VA_ARGS__)
#define ULM_WARNING_COMPACT(Channel, Format, ...) ULM_LOG_COMPACT(Channel, EULMVerbosity::Warning, Format, ##__VA_ARGS__)
#define ULM_ERROR_COMPACT(Channel, Format, ...) ULM_LOG_COMPACT(Channel, EULMVerbosity::Error, Format, ##__VA_ARGS__)
#define ULM_CRITICAL_COMPACT(Channel, Format, ...) ULM_LOG_COMPACT(Channel, EULMVerbosity::Critical, Format, ##__VA_ARGS__)

/**
 * Structured logging support for complex data
 */
class ULM_API FULMStructuredLog
{
public:
	FULMStructuredLog(const FString& InChannelName, EULMVerbosity InVerbosity, const FString& InFileName = TEXT(""), int32 InLineNumber = 0, const FString& InFunctionName = TEXT(""));
	~FULMStructuredLog();

	// Fluent interface for building structured logs
	FULMStructuredLog& Add(const FString& Key, const FString& Value);
	FULMStructuredLog& Add(const FString& Key, int32 Value);
	FULMStructuredLog& Add(const FString& Key, float Value);
	FULMStructuredLog& Add(const FString& Key, bool Value);
	FULMStructuredLog& Add(const FString& Key, const FVector& Value);
	FULMStructuredLog& Add(const FString& Key, const FRotator& Value);

	// Commit the structured log
	void Commit();

private:
	FString ChannelName;
	EULMVerbosity Verbosity;
	FString FileName;
	int32 LineNumber;
	FString FunctionName;
	TMap<FString, FString> Fields;
	bool bCommitted = false;
};

// Structured logging macro
#define ULM_LOG_STRUCTURED(Channel, Verbosity) \
	FULMStructuredLog(Channel, Verbosity, *FPaths::GetCleanFilename(__FILE__), __LINE__, *ULMInternal::ExtractFunctionName(__FUNCTION__))

// Channel name constants - inline to avoid linker issues
static const FString ULMChannelGameplay(TEXT("Gameplay"));
static const FString ULMChannelNetwork(TEXT("Network"));
static const FString ULMChannelPerformance(TEXT("Performance"));
static const FString ULMChannelDebug(TEXT("Debug"));
static const FString ULMChannelAI(TEXT("AI"));
static const FString ULMChannelPhysics(TEXT("Physics"));
static const FString ULMChannelAudio(TEXT("Audio"));
static const FString ULMChannelAnimation(TEXT("Animation"));
static const FString ULMChannelUI(TEXT("UI"));
static const FString ULMChannelSubsystem(TEXT("Subsystem"));

// Channel name convenience macros
#define CHANNEL_GAMEPLAY ULMChannelGameplay
#define CHANNEL_NETWORK ULMChannelNetwork
#define CHANNEL_PERFORMANCE ULMChannelPerformance
#define CHANNEL_DEBUG ULMChannelDebug
#define CHANNEL_AI ULMChannelAI
#define CHANNEL_PHYSICS ULMChannelPhysics
#define CHANNEL_AUDIO ULMChannelAudio
#define CHANNEL_ANIMATION ULMChannelAnimation
#define CHANNEL_UI ULMChannelUI
#define CHANNEL_SUBSYSTEM ULMChannelSubsystem

