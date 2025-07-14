#include "Configuration/ULMSettings.h"
#include "Core/ULMSubsystem.h"
#include "Logging/ULMLogging.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformFileManager.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IMainFrameModule.h"
#endif

UULMSettings::UULMSettings()
{
	PerformanceTier = EULMPerformanceTier::Development;
	ApplyDevelopmentTier();
}


void UULMSettings::ApplyPerformanceTierInternal()
{
	FString TierName = TEXT("Unknown");
	
	switch (PerformanceTier)
	{
		case EULMPerformanceTier::Production:
			TierName = TEXT("Production");
			ApplyProductionTier();
			break;
		case EULMPerformanceTier::Development:
			TierName = TEXT("Development");
			ApplyDevelopmentTier();
			break;
		case EULMPerformanceTier::Debug:
			TierName = TEXT("Debug");
			ApplyDebugTier();
			break;
		case EULMPerformanceTier::Custom:
			TierName = TEXT("Custom");
			return;
	}
	
	SaveConfig();
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("=== ULM SETTINGS: Performance Tier Changed ==="));
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Applied Tier: %s"), *TierName);
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Memory Budget: %d MB"), MemoryBudgetMB);
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("File Logging: %s"), bFileLoggingEnabled ? TEXT("ENABLED") : TEXT("DISABLED"));
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Queue Size: %d"), MaxQueueSize);
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Retention Days: %d"), RotationConfig.RetentionDays);
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Settings saved to DefaultEngine.ini"));
	
	if (UULMSubsystem* ULMSys = GEngine->GetEngineSubsystem<UULMSubsystem>())
	{
		ULMSys->SetMemoryBudget(MemoryBudgetMB * 1024 * 1024);
		ULMSys->SetJSONConfig(JSONConfig);
		ULMSys->SetRotationConfig(RotationConfig);
		ULMSys->SetFileLoggingEnabled(bFileLoggingEnabled);
		
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Runtime system updated with new settings"));
	}
	else
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Runtime system not available - settings will apply on next startup"));
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("=== ULM SETTINGS: Performance Tier Applied ==="));
}

void UULMSettings::ApplyProductionTier()
{
	bEnabled = true;
	bFileLoggingEnabled = true;
	MaxQueueSize = 5000;
	QueueHealthThreshold = 0.6f;
	BatchProcessingSize = 32;
	MemoryBudgetMB = 25;
	MemoryTrimThreshold = 0.7f;
	EmergencyTrimPercentage = 0.5f;
	
	JSONConfig.bIncludeSessionId = true;
	JSONConfig.bIncludeBuildVersion = true;
	JSONConfig.bIncludeSourceLocation = false;
	JSONConfig.bCompactFormat = true;
	
	// Rotation Config for production
	RotationConfig.MaxFileSizeBytes = 50 * 1024 * 1024;  // 50MB files
	RotationConfig.RetentionDays = 3;  // Shorter retention
	RotationConfig.MaxFilesPerDay = 5;  // Fewer files per day
	RotationConfig.bAutoCleanupOnStartup = true;
	RotationConfig.bPeriodicCleanup = true;
	RotationConfig.CleanupIntervalHours = 12.0f;  // More frequent cleanup
	
	// Channel defaults
	DefaultChannelConfig.bEnabled = true;
	DefaultChannelConfig.MinVerbosity = EULMVerbosity::Warning;  // Only warnings and above
	DefaultChannelConfig.MaxLogEntries = 500;  // Smaller per-channel limits
	
	// Advanced settings
	FileWriterFlushInterval = 2.0f;  // Faster flushing
	ThreadSleepTimeMs = 50;  // More responsive
	bEnableSystemHealthMonitoring = true;
	bAutoRegisterChannels = true;
}

void UULMSettings::ApplyDevelopmentTier()
{
	// Development: Balanced performance and debugging
	bEnabled = true;
	bFileLoggingEnabled = true;
	MaxQueueSize = 10000;  // Default queue size
	QueueHealthThreshold = 0.8f;  // Standard health threshold
	BatchProcessingSize = 64;  // Standard batch size
	MemoryBudgetMB = 50;  // Standard memory budget
	MemoryTrimThreshold = 0.8f;  // Standard trim threshold
	EmergencyTrimPercentage = 0.6f;  // Moderate trimming
	
	// JSON Config for development
	JSONConfig.bIncludeSessionId = true;
	JSONConfig.bIncludeBuildVersion = true;
	JSONConfig.bIncludeSourceLocation = true;  // Enable for debugging
	JSONConfig.bCompactFormat = true;
	
	// Rotation Config for development
	RotationConfig.MaxFileSizeBytes = 100 * 1024 * 1024;  // 100MB files
	RotationConfig.RetentionDays = 7;  // Standard retention
	RotationConfig.MaxFilesPerDay = 10;  // Standard file count
	RotationConfig.bAutoCleanupOnStartup = true;
	RotationConfig.bPeriodicCleanup = true;
	RotationConfig.CleanupIntervalHours = 24.0f;  // Daily cleanup
	
	// Channel defaults
	DefaultChannelConfig.bEnabled = true;
	DefaultChannelConfig.MinVerbosity = EULMVerbosity::Message;  // All messages
	DefaultChannelConfig.MaxLogEntries = 1000;  // Standard limits
	
	// Advanced settings
	FileWriterFlushInterval = 5.0f;  // Standard flushing
	ThreadSleepTimeMs = 100;  // Standard sleep
	bEnableSystemHealthMonitoring = true;
	bAutoRegisterChannels = true;
}

void UULMSettings::ApplyDebugTier()
{
	// Debug: Maximum logging, lower performance
	bEnabled = true;
	bFileLoggingEnabled = true;
	MaxQueueSize = 20000;  // Larger queue for extensive logging
	QueueHealthThreshold = 0.9f;  // More lenient health threshold
	BatchProcessingSize = 128;  // Larger batches
	MemoryBudgetMB = 100;  // Higher memory budget
	MemoryTrimThreshold = 0.9f;  // Trim only when very full
	EmergencyTrimPercentage = 0.7f;  // Conservative trimming
	
	// JSON Config for debug
	JSONConfig.bIncludeSessionId = true;
	JSONConfig.bIncludeBuildVersion = true;
	JSONConfig.bIncludeSourceLocation = true;  // Full debugging info
	JSONConfig.bCompactFormat = false;  // Pretty printing for readability
	
	// Rotation Config for debug
	RotationConfig.MaxFileSizeBytes = 200 * 1024 * 1024;  // 200MB files
	RotationConfig.RetentionDays = 14;  // Longer retention
	RotationConfig.MaxFilesPerDay = 20;  // More files per day
	RotationConfig.bAutoCleanupOnStartup = true;
	RotationConfig.bPeriodicCleanup = true;
	RotationConfig.CleanupIntervalHours = 48.0f;  // Less frequent cleanup
	
	// Channel defaults
	DefaultChannelConfig.bEnabled = true;
	DefaultChannelConfig.MinVerbosity = EULMVerbosity::Message;  // All messages
	DefaultChannelConfig.MaxLogEntries = 5000;  // Large per-channel limits
	
	// Advanced settings
	FileWriterFlushInterval = 10.0f;  // Less frequent flushing
	ThreadSleepTimeMs = 200;  // More relaxed sleep
	bEnableSystemHealthMonitoring = true;
	bAutoRegisterChannels = true;
}

bool UULMSettings::DoesCurrentConfigMatchTier(EULMPerformanceTier Tier) const
{
	// Don't create UObject instances - use hardcoded expected values instead
	switch (Tier)
	{
		case EULMPerformanceTier::Production:
		{
			// Production tier expected values (from ApplyProductionTier)
			return (MemoryBudgetMB == 25 &&
					bFileLoggingEnabled == true &&
					MaxQueueSize == 5000 &&
					FMath::IsNearlyEqual(QueueHealthThreshold, 0.6f) &&
					BatchProcessingSize == 32 &&
					FMath::IsNearlyEqual(MemoryTrimThreshold, 0.7f) &&
					RotationConfig.MaxFileSizeBytes == 50 * 1024 * 1024 &&
					RotationConfig.RetentionDays == 3 &&
					DefaultChannelConfig.MinVerbosity == EULMVerbosity::Warning &&
					DefaultChannelConfig.MaxLogEntries == 500);
		}
		case EULMPerformanceTier::Development:
		{
			// Development tier expected values (from ApplyDevelopmentTier)
			return (MemoryBudgetMB == 50 &&
					bFileLoggingEnabled == true &&
					MaxQueueSize == 10000 &&
					FMath::IsNearlyEqual(QueueHealthThreshold, 0.8f) &&
					BatchProcessingSize == 64 &&
					FMath::IsNearlyEqual(MemoryTrimThreshold, 0.8f) &&
					RotationConfig.MaxFileSizeBytes == 100 * 1024 * 1024 &&
					RotationConfig.RetentionDays == 7 &&
					DefaultChannelConfig.MinVerbosity == EULMVerbosity::Message &&
					DefaultChannelConfig.MaxLogEntries == 1000);
		}
		case EULMPerformanceTier::Debug:
		{
			// Debug tier expected values (from ApplyDebugTier)
			return (MemoryBudgetMB == 100 &&
					bFileLoggingEnabled == true &&
					MaxQueueSize == 20000 &&
					FMath::IsNearlyEqual(QueueHealthThreshold, 0.9f) &&
					BatchProcessingSize == 128 &&
					FMath::IsNearlyEqual(MemoryTrimThreshold, 0.9f) &&
					RotationConfig.MaxFileSizeBytes == 200 * 1024 * 1024 &&
					RotationConfig.RetentionDays == 14 &&
					DefaultChannelConfig.MinVerbosity == EULMVerbosity::Message &&
					DefaultChannelConfig.MaxLogEntries == 5000);
		}
		case EULMPerformanceTier::Custom:
		default:
			return true; // Custom tier always matches (no predefined values)
	}
}

void UULMSettings::SwitchToCustomTierIfNeeded(const FString& ChangedPropertyName)
{
	// Don't switch if we're already Custom or if the PerformanceTier itself changed
	if (PerformanceTier == EULMPerformanceTier::Custom || ChangedPropertyName == TEXT("PerformanceTier"))
	{
		return;
	}
	
	// Check if current settings still match the selected tier
	if (!DoesCurrentConfigMatchTier(PerformanceTier))
	{
		EULMPerformanceTier OldTier = PerformanceTier;
		PerformanceTier = EULMPerformanceTier::Custom;
		
		// Log the automatic switch
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("=== ULM SETTINGS: Auto-Switched to Custom Tier ==="));
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Trigger: Property '%s' changed"), *ChangedPropertyName);
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Previous Tier: %d -> Custom"), (int32)OldTier);
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("Settings no longer match predefined tier - switched to Custom"));
		
		// Save the change
		SaveConfig();
		
#if WITH_EDITOR
		// Force UI refresh to show the tier change
		if (HasAnyFlags(RF_ClassDefaultObject))
		{
			Modify();
			
			// Create a property changed event for the PerformanceTier to update the dropdown
			FProperty* PerformanceTierProperty = GetClass()->FindPropertyByName(TEXT("PerformanceTier"));
			if (PerformanceTierProperty)
			{
				FPropertyChangedEvent PropertyChangedEvent(PerformanceTierProperty, EPropertyChangeType::ValueSet);
				FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, PropertyChangedEvent);
			}
		}
#endif
	}
}


const UULMSettings* UULMSettings::Get()
{
	return GetDefault<UULMSettings>();
}

UULMSettings* UULMSettings::GetMutable()
{
	return GetMutableDefault<UULMSettings>();
}

#if WITH_EDITOR
void UULMSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	// Only process if we have a valid property (avoid infinite loops from button refreshes)
	if (PropertyChangedEvent.Property)
	{
		FString PropertyName = PropertyChangedEvent.Property->GetName();
		
		// Log the property change for debugging
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM Settings property changed: %s"), *PropertyName);
		
		// Auto-apply performance tier when changed
		if (PropertyName == TEXT("PerformanceTier"))
		{
			// Apply the performance tier settings automatically
			ApplyPerformanceTierInternal();
		}
		else
		{
			// Check if we need to switch to Custom tier due to manual changes
			SwitchToCustomTierIfNeeded(PropertyName);
			
			// Apply settings to runtime system for other property changes
			if (UULMSubsystem* ULMSys = GEngine->GetEngineSubsystem<UULMSubsystem>())
			{
				ULMSys->ApplySettings();
				ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Runtime system updated due to property change: %s"), *PropertyName);
			}
		}
	}
}
#endif


