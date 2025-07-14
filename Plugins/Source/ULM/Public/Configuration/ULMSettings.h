#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Channels/ULMChannel.h"
#include "FileIO/ULMJSONFormat.h"
#include "MemoryManagement/ULMLogRotation.h"
#include "ULMSettings.generated.h"

/**
 * Performance tier presets for different deployment scenarios
 */
UENUM(BlueprintType)
enum class EULMPerformanceTier : uint8
{
	/** Maximum performance - minimal logging, high memory budget */
	Production		UMETA(DisplayName = "Production"),
	
	/** Balanced performance - moderate logging, medium memory budget */
	Development		UMETA(DisplayName = "Development"),
	
	/** Full debugging - extensive logging, low memory budget for testing */
	Debug			UMETA(DisplayName = "Debug"),
	
	/** Custom settings - user-defined configuration */
	Custom			UMETA(DisplayName = "Custom")
};

UCLASS(config=Engine, defaultconfig)
class ULM_API UULMSettings : public UObject
{
	GENERATED_BODY()

public:
	UULMSettings();


	/** Performance tier preset - automatically configures optimal settings */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Performance Tier"))
	EULMPerformanceTier PerformanceTier;

	/** Enable ULM logging system (master switch) */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Enable ULM Logging"))
	bool bEnabled;

	/** Enable file logging (disable for pure in-memory logging) */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Enable File Logging"))
	bool bFileLoggingEnabled;

	/** Custom log file directory (empty = use default ProjectLogDir/ULM) */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Custom Log Directory"))
	FDirectoryPath CustomLogDirectory;


	// === Queue Configuration ===
	UPROPERTY(config, EditAnywhere, Category = "Queue", meta = (DisplayName = "Max Queue Size", ClampMin = "1000", ClampMax = "100000"))
	int32 MaxQueueSize;

	UPROPERTY(config, EditAnywhere, Category = "Queue", meta = (DisplayName = "Queue Health Threshold", ClampMin = "0.1", ClampMax = "1.0"))
	float QueueHealthThreshold;

	UPROPERTY(config, EditAnywhere, Category = "Queue", meta = (DisplayName = "Batch Processing Size", ClampMin = "1", ClampMax = "1000"))
	int32 BatchProcessingSize;

	// === Memory Management ===
	UPROPERTY(config, EditAnywhere, Category = "Memory", meta = (DisplayName = "Memory Budget (MB)", ClampMin = "10", ClampMax = "1000"))
	int32 MemoryBudgetMB;

	UPROPERTY(config, EditAnywhere, Category = "Memory", meta = (DisplayName = "Memory Trim Threshold", ClampMin = "0.1", ClampMax = "1.0"))
	float MemoryTrimThreshold;

	UPROPERTY(config, EditAnywhere, Category = "Memory", meta = (DisplayName = "Emergency Trim Percentage", ClampMin = "0.1", ClampMax = "0.9"))
	float EmergencyTrimPercentage;

	// === JSON Configuration ===
	UPROPERTY(config, EditAnywhere, Category = "JSON Format", meta = (DisplayName = "JSON Configuration"))
	FULMJSONConfig JSONConfig;

	// === Log Rotation ===
	UPROPERTY(config, EditAnywhere, Category = "Log Rotation", meta = (DisplayName = "Rotation Configuration"))
	FULMRotationConfig RotationConfig;

	// === Channel Defaults ===
	UPROPERTY(config, EditAnywhere, Category = "Channels", meta = (DisplayName = "Default Channel Settings"))
	FULMChannelConfig DefaultChannelConfig;

	UPROPERTY(config, EditAnywhere, Category = "Channels", meta = (DisplayName = "Enable Channel Auto-Registration"))
	bool bAutoRegisterChannels;

	// === Advanced Performance ===
	UPROPERTY(config, EditAnywhere, Category = "Advanced", meta = (DisplayName = "File Writer Flush Interval (seconds)", ClampMin = "0.1", ClampMax = "60.0"))
	float FileWriterFlushInterval;

	UPROPERTY(config, EditAnywhere, Category = "Advanced", meta = (DisplayName = "Thread Sleep Time (ms)", ClampMin = "1", ClampMax = "1000"))
	int32 ThreadSleepTimeMs;

	UPROPERTY(config, EditAnywhere, Category = "Advanced", meta = (DisplayName = "Enable System Health Monitoring"))
	bool bEnableSystemHealthMonitoring;



	/** Get singleton instance */
	UFUNCTION(BlueprintCallable, Category = "ULM Settings", BlueprintPure)
	static const UULMSettings* Get();

	/** Get mutable instance for runtime changes */
	static UULMSettings* GetMutable();

private:
	/** Internal function to apply performance tier settings */
	void ApplyPerformanceTierInternal();

	/** Check if current settings match a specific tier */
	bool DoesCurrentConfigMatchTier(EULMPerformanceTier Tier) const;

	/** Switch to Custom tier when settings are manually modified */
	void SwitchToCustomTierIfNeeded(const FString& ChangedPropertyName);


	/** Apply production performance settings */
	void ApplyProductionTier();

	/** Apply development performance settings */
	void ApplyDevelopmentTier();

	/** Apply debug performance settings */
	void ApplyDebugTier();

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif
};

