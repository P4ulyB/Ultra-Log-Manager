#pragma once

#include "Channels/ULMLogCategories.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "ULMChannel.generated.h"

UENUM(BlueprintType)
enum class EULMVerbosity : uint8
{
	Message		UMETA(DisplayName = "Message"),
	Warning		UMETA(DisplayName = "Warning"), 
	Error		UMETA(DisplayName = "Error"),
	Critical	UMETA(DisplayName = "Critical")
};

// Master definition of all ULM channels - SINGLE SOURCE OF TRUTH
// Add new channels here and they will be available everywhere
#define ULM_CHANNEL_LIST(X) \
	X(ULM,			"ULM",			"ULM (All Logs)") \
	X(Gameplay,		"Gameplay",		"ULMGameplay") \
	X(Network,		"Network",		"ULMNetwork") \
	X(Performance,	"Performance",	"ULMPerformance") \
	X(Debug,		"Debug",		"ULMDebug") \
	X(AI,			"AI",			"ULMAI") \
	X(Physics,		"Physics",		"ULMPhysics") \
	X(Audio,		"Audio",		"ULMAudio") \
	X(Animation,	"Animation",	"ULMAnimation") \
	X(UI,			"UI",			"ULMUI") \
	X(Subsystem,	"Subsystem",	"ULMSubsystem") \
	X(Custom,		"Custom",		"Custom (String)")

// Generate enum from master list
// NOTE: UE's UENUM parser requires explicit entries, so we expand the macro manually
UENUM(BlueprintType)
enum class EULMChannel : uint8
{
	ULM			UMETA(DisplayName = "ULM (All Logs)", Hidden),
	Gameplay	UMETA(DisplayName = "ULMGameplay"),
	Network		UMETA(DisplayName = "ULMNetwork"),
	Performance	UMETA(DisplayName = "ULMPerformance"),
	Debug		UMETA(DisplayName = "ULMDebug"),
	AI			UMETA(DisplayName = "ULMAI"),
	Physics		UMETA(DisplayName = "ULMPhysics"),
	Audio		UMETA(DisplayName = "ULMAudio"),
	Animation	UMETA(DisplayName = "ULMAnimation"),
	UI			UMETA(DisplayName = "ULMUI"),
	Subsystem	UMETA(DisplayName = "ULMSubsystem", Hidden),
	Custom		UMETA(DisplayName = "Custom (String)")
};

// Log categories are now declared in ULMLogCategories.h to prevent duplicate definitions

/**
 * Rate limiting configuration using token bucket algorithm
 */
USTRUCT(BlueprintType)
struct ULM_API FULMRateLimit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Rate Limit", meta = (ClampMin = "0.1", ClampMax = "1000.0"))
	float TokensPerSecond = 20.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Rate Limit", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 BurstCapacity = 20;

	FULMRateLimit() = default;
	FULMRateLimit(float InTokensPerSecond, int32 InBurstCapacity = 0)
		: TokensPerSecond(InTokensPerSecond)
		, BurstCapacity(InBurstCapacity > 0 ? InBurstCapacity : static_cast<int32>(InTokensPerSecond))
	{}
};

/**
 * Channel configuration with hierarchical inheritance support
 */
USTRUCT(BlueprintType)
struct ULM_API FULMChannelConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Channel")
	bool bEnabled = true;

	UPROPERTY(BlueprintReadWrite, Category = "Channel")
	EULMVerbosity MinVerbosity = EULMVerbosity::Message;

	UPROPERTY(BlueprintReadWrite, Category = "Channel")
	FLinearColor DisplayColor = FLinearColor::White;

	UPROPERTY(BlueprintReadWrite, Category = "Channel")
	FULMRateLimit RateLimit;

	UPROPERTY(BlueprintReadWrite, Category = "Channel", meta = (ClampMin = "100", ClampMax = "100000"))
	int32 MaxLogEntries = 1000;

	UPROPERTY(BlueprintReadWrite, Category = "Channel")
	bool bInheritFromParent = true;

	FULMChannelConfig() = default;
};

/**
 * Runtime channel state for efficient logging operations
 */
struct FULMChannelState
{
	// Effective settings after inheritance resolution
	bool bEffectiveEnabled = true;
	EULMVerbosity EffectiveMinVerbosity = EULMVerbosity::Message;
	FLinearColor EffectiveColor = FLinearColor::White;
	FULMRateLimit EffectiveRateLimit;
	int32 EffectiveMaxEntries = 1000;

	// Rate limiting state
	float CurrentTokens = 20.0f;
	double LastRefillTime = 0.0;

	// Channel hierarchy
	FString ParentChannel;
	TArray<FString> ChildChannels;

	// Thread synchronization
	mutable FCriticalSection StateLock;

	FULMChannelState() = default;

	bool CanLog(EULMVerbosity Verbosity, double CurrentTime);
	void RefillTokens(double CurrentTime);
	void UpdateEffectiveSettings(const FULMChannelConfig& Config, const FULMChannelState* ParentState);
};

/**
 * Hierarchical channel management system
 * Provides efficient lookup and inheritance of channel settings
 */
class ULM_API FULMChannelRegistry
{
public:
	FULMChannelRegistry();
	~FULMChannelRegistry();

	// Channel registration and management
	void RegisterChannel(const FString& ChannelName, const FULMChannelConfig& Config = FULMChannelConfig());
	void UnregisterChannel(const FString& ChannelName);
	bool IsChannelRegistered(const FString& ChannelName) const;

	// Channel lookup and state access
	const FULMChannelState* GetChannelState(const FString& ChannelName) const;
	bool CanChannelLog(const FString& ChannelName, EULMVerbosity Verbosity) const;

	// Configuration management
	void UpdateChannelConfig(const FString& ChannelName, const FULMChannelConfig& Config);
	FULMChannelConfig GetChannelConfig(const FString& ChannelName) const;

	// Hierarchy management
	TArray<FString> GetAllChannels() const;
	TArray<FString> GetChildChannels(const FString& ParentChannel) const;
	FString GetParentChannel(const FString& ChannelName) const;

	// Bulk operations
	void SetChannelEnabled(const FString& ChannelName, bool bEnabled, bool bRecursive = false);
	void SetChannelVerbosity(const FString& ChannelName, EULMVerbosity MinVerbosity, bool bRecursive = false);

private:
	void ParseChannelHierarchy(const FString& ChannelName, FString& OutParent, FString& OutName) const;
	void UpdateChildChannels(const FString& ParentChannel);
	void RebuildEffectiveSettings(const FString& ChannelName);

	// Channel storage
	TMap<FString, FULMChannelConfig> ChannelConfigs;
	TMap<FString, TUniquePtr<FULMChannelState>> ChannelStates;

	// Thread synchronization
	mutable FRWLock RegistryLock;

	// Default configuration
	FULMChannelConfig DefaultConfig;
};

