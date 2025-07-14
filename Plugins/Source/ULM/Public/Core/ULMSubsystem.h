#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Channels/ULMChannel.h"
#include "FileIO/ULMFileTypes.h"
#include "MemoryManagement/ULMMemoryBudget.h"
#include "FileIO/ULMJSONFormat.h"
#include "MemoryManagement/ULMLogRotation.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "ULMSubsystem.generated.h"

// Forward declarations
class FULMChannelRegistry;
class FULMLogProcessor;
class FULMFileWriter;
class FULMLogRotator;
class FULMRetentionManager;

// Queue operation for the log processor
struct FULMLogQueueEntry
{
	FString Message;
	FString Channel;
	EULMVerbosity Verbosity;
	FDateTime Timestamp;
	int32 ThreadId;
	
	FULMLogQueueEntry() = default;
	
	FULMLogQueueEntry(const FString& InMessage, const FString& InChannel, EULMVerbosity InVerbosity)
		: Message(InMessage)
		, Channel(InChannel)
		, Verbosity(InVerbosity)
		, Timestamp(FDateTime::Now())
		, ThreadId(FPlatformTLS::GetCurrentThreadId())
	{}
};

// Performance diagnostics for queue operations
struct FULMQueueDiagnostics
{
	FThreadSafeCounter EnqueueCount;
	FThreadSafeCounter DequeueCount;
	FThreadSafeCounter DroppedCount;
	FThreadSafeCounter ProcessedCount;
	FThreadSafeCounter64 TotalEnqueueTime;
	FThreadSafeCounter64 TotalDequeueTime;
	
	void Reset()
	{
		EnqueueCount.Reset();
		DequeueCount.Reset();
		DroppedCount.Reset();
		ProcessedCount.Reset();
		TotalEnqueueTime.Reset();
		TotalDequeueTime.Reset();
	}
};

/**
 * Individual log entry with metadata
 * Optimized for performance and memory efficiency
 */
USTRUCT(BlueprintType)
struct ULM_API FULMLogEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Log")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "Log")
	FString Channel;

	UPROPERTY(BlueprintReadOnly, Category = "Log")
	EULMVerbosity Verbosity;

	UPROPERTY(BlueprintReadOnly, Category = "Log")
	FDateTime Timestamp;

	UPROPERTY(BlueprintReadOnly, Category = "Log")
	int32 ThreadId;

	FULMLogEntry()
		: Verbosity(EULMVerbosity::Message)
		, Timestamp(FDateTime::Now())
		, ThreadId(FPlatformTLS::GetCurrentThreadId())
	{}

	FULMLogEntry(const FString& InMessage, const FString& InChannel, EULMVerbosity InVerbosity)
		: Message(InMessage)
		, Channel(InChannel)
		, Verbosity(InVerbosity)
		, Timestamp(FDateTime::Now())
		, ThreadId(FPlatformTLS::GetCurrentThreadId())
	{}
};

/**
 * Ultra Log Manager Engine Subsystem
 * 
 * High-performance logging system with hierarchical channels, rate limiting, and thread safety.
 * Designed for <0.01ms per log call performance target.
 * 
 * Features:
 * - Hierarchical channel system (e.g., "Gameplay.Combat", "Gameplay.Movement")
 * - Token bucket rate limiting per channel
 * - Thread-safe operations with optimized locking
 * - Network-aware logging (server/client context)
 * - Memory-bounded with automatic trimming
 * - Blueprint and C++ API support
 * 
 * USAGE EXAMPLES:
 * 
 * Blueprint:
 * 1. Get Engine Subsystem -> ULM Subsystem
 * 2. Call LogMessage with channel, message, verbosity
 * 3. Use GetLogEntries to retrieve logs
 * 
 * C++ Macros:
 * ULM_LOG(CHANNEL_NAME(Gameplay), EULMVerbosity::Warning, TEXT("Player health: %d"), Health);
 * ULM_LOG_SERVER(CHANNEL_NAME(Network), EULMVerbosity::Error, TEXT("Connection failed"));
 * ULM_LOG_SAMPLED(CHANNEL_NAME(Performance), EULMVerbosity::Message, TEXT("Frame time: %f"), DeltaTime);
 * 
 * Channel Declaration:
 * ULM_DECLARE_LOG_CHANNEL(Gameplay);  // Simple channels
 * ULM_DECLARE_HIERARCHICAL_CHANNEL(Gameplay_Combat, "Gameplay.Combat");  // Hierarchical channels
 */
UCLASS()
class ULM_API UULMSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	// UEngineSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Core logging functionality (C++ only)
	void LogMessage(const FString& Message, const FString& Channel = TEXT("Default"), EULMVerbosity Verbosity = EULMVerbosity::Message);

	// Enhanced Blueprint logging with enum channel selection and screen display
	UFUNCTION(BlueprintCallable, Category = "ULM", CallInEditor, meta = (DisplayName = "Log Message (Enhanced)"))
	void LogMessageEnhanced(const FString& Message, EULMChannel Channel = EULMChannel::Gameplay, EULMVerbosity Verbosity = EULMVerbosity::Message, bool bPrintToScreen = false, float Duration = 2.0f, const FString& CustomChannel = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "ULM", BlueprintPure)
	TArray<FULMLogEntry> GetLogEntries(const FString& Channel = TEXT(""), int32 MaxEntries = 100) const;

	// Hierarchical channel management (C++ only)
	void RegisterChannel(const FString& ChannelName, const FULMChannelConfig& Config = FULMChannelConfig());

	UFUNCTION(BlueprintCallable, Category = "ULM", BlueprintPure)
	bool IsChannelRegistered(const FString& ChannelName) const;

	UFUNCTION(BlueprintCallable, Category = "ULM", BlueprintPure)
	TArray<FString> GetRegisteredChannels() const;

	UFUNCTION(BlueprintCallable, Category = "ULM", BlueprintPure)
	TArray<FString> GetChildChannels(const FString& ParentChannel) const;

	// Channel configuration (C++ only)
	void UpdateChannelConfig(const FString& ChannelName, const FULMChannelConfig& Config);

	UFUNCTION(BlueprintCallable, Category = "ULM", BlueprintPure)
	FULMChannelConfig GetChannelConfig(const FString& ChannelName) const;

	// Channel control operations (C++ only)
	void SetChannelEnabled(const FString& ChannelName, bool bEnabled, bool bRecursive = false);

	void SetChannelVerbosity(const FString& ChannelName, EULMVerbosity MinVerbosity, bool bRecursive = false);

	// Maintenance operations (C++ only)
	void ClearChannel(const FString& ChannelName);

	void ClearAllChannels();

	// Access to channel registry for macro system
	FULMChannelRegistry* GetChannelRegistry() const { return ChannelRegistry.Get(); }

	// Auto-register all channels from master list
	void RegisterAllChannelsFromMasterList();

	// Internal function to store log entries without triggering Output Log (prevents circular calls)
	// Made public for ULMLogging.cpp access
	void StoreLogEntryInternal(const FString& Message, const FString& ChannelName, EULMVerbosity Verbosity);
	
	// Log processor access - needed by FULMLogProcessor
	void ProcessLogEntry(const FULMLogQueueEntry& Entry);
	void UpdateProcessingDiagnostics(int64 DequeueTimeMicros);

	// Performance diagnostics access
	FULMQueueDiagnostics GetQueueDiagnostics() const { return QueueDiagnostics; }
	void ResetQueueDiagnostics() { QueueDiagnostics.Reset(); }
	
	// File I/O diagnostics access
	FULMFileIODiagnostics GetFileIODiagnostics() const;
	void ResetFileIODiagnostics();
	
	// File I/O configuration
	UFUNCTION(BlueprintCallable, Category = "ULM Settings")
	void SetFileLoggingEnabled(bool bEnabled);
	
	UFUNCTION(BlueprintCallable, Category = "ULM Settings")
	bool IsFileLoggingEnabled() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Settings")
	void SetLogFilePath(const FString& NewPath);
	
	UFUNCTION(BlueprintCallable, Category = "ULM Settings")
	FString GetLogFilePath() const;
	
	// Queue health check
	int32 GetQueueSize() const;
	bool IsQueueHealthy() const;
	
	// Memory budget management
	UFUNCTION(BlueprintCallable, Category = "ULM Memory")
	void SetMemoryBudget(int64 BudgetBytes);
	
	UFUNCTION(BlueprintCallable, Category = "ULM Memory", BlueprintPure)
	int64 GetMemoryBudget() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Memory", BlueprintPure)
	FULMMemoryDiagnostics GetMemoryDiagnostics() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Memory")
	void ResetMemoryDiagnostics();
	
	UFUNCTION(BlueprintCallable, Category = "ULM Memory", BlueprintPure)
	bool IsMemoryBudgetHealthy() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Memory", BlueprintPure)
	int64 GetChannelMemoryUsage(const FString& ChannelName) const;

	// JSON format configuration
	UFUNCTION(BlueprintCallable, Category = "ULM Format")
	void SetLogFormat(EULMLogFormat Format);
	
	UFUNCTION(BlueprintCallable, Category = "ULM Format", BlueprintPure)
	EULMLogFormat GetLogFormat() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Format")
	void SetJSONConfig(const FULMJSONConfig& Config);
	
	UFUNCTION(BlueprintCallable, Category = "ULM Format", BlueprintPure)
	FULMJSONConfig GetJSONConfig() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Format", BlueprintPure)
	FULMFormatDiagnostics GetFormatDiagnostics() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Format")
	void ResetFormatDiagnostics();

	// Log rotation and retention configuration
	UFUNCTION(BlueprintCallable, Category = "ULM Rotation")
	void SetRotationConfig(const FULMRotationConfig& Config);
	
	UFUNCTION(BlueprintCallable, Category = "ULM Rotation", BlueprintPure)
	FULMRotationConfig GetRotationConfig() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Rotation", BlueprintPure)
	FULMRotationDiagnostics GetRotationDiagnostics() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Rotation")
	void ResetRotationDiagnostics();
	
	// Thread health monitoring
	UFUNCTION(BlueprintCallable, Category = "ULM Subsystem", BlueprintPure)
	bool AreThreadsHealthy() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Subsystem")
	void LogThreadHealthStatus() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Subsystem")
	void LogMemoryHealthStatus() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Subsystem", BlueprintPure)
	bool IsMemoryHealthy() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Subsystem")
	void LogSystemHealthStatus() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Subsystem", BlueprintPure)
	bool IsSystemHealthy() const;
	
	UFUNCTION(BlueprintCallable, Category = "ULM Rotation")
	void ForceLogRotation(const FString& ChannelName = TEXT(""));
	
	UFUNCTION(BlueprintCallable, Category = "ULM Rotation")
	void ForceRetentionCleanup();
	
	UFUNCTION(BlueprintCallable, Category = "ULM Rotation", BlueprintPure)
	int64 GetLogDiskUsage() const;

	// Settings integration
	UFUNCTION(BlueprintCallable, Category = "ULM Settings")
	void ApplySettings();

	UFUNCTION(BlueprintCallable, Category = "ULM Settings", BlueprintPure)
	bool IsUsingCustomLogDirectory() const;

	UFUNCTION(BlueprintCallable, Category = "ULM Settings", BlueprintPure)
	FString GetEffectiveLogDirectory() const;

private:
	// Channel registry for hierarchical management
	TUniquePtr<FULMChannelRegistry> ChannelRegistry;

	// Lock-free message queue for high-performance logging
	TQueue<FULMLogQueueEntry, EQueueMode::Spsc> LogMessageQueue;
	
	// Consumer thread for processing queued log entries
	FULMLogProcessor* LogProcessor;
	FRunnableThread* ProcessorThread;
	
	// File I/O system for persistent logging
	TQueue<FULMFileWriteEntry, EQueueMode::Spsc> FileWriteQueue;
	FULMFileWriter* FileWriter;
	FRunnableThread* FileWriterThread;
	bool bFileLoggingEnabled;
	
	// Data structures for processed log storage (still needs protection for read access)
	mutable FCriticalSection StorageCriticalSection;
	TMap<FString, TArray<FULMLogEntry>> LogEntries;
	
	// Performance diagnostics
	FULMQueueDiagnostics QueueDiagnostics;
	
	// Memory budget tracking
	FULMMemoryTracker MemoryTracker;
	
	// JSON format support
	EULMLogFormat CurrentLogFormat;
	FULMJSONConfig JSONConfig;
	FULMJSONFormatter JSONFormatter;
	mutable FULMFormatDiagnostics FormatDiagnostics;
	
	// Log rotation and retention management
	TUniquePtr<FULMLogRotator> LogRotator;
	TUniquePtr<FULMRetentionManager> RetentionManager;
	
	// Maximum queue size to prevent memory issues
	static constexpr int32 MAX_QUEUE_SIZE = 10000;
	
	// Internal helpers
	void StoreProcessedLogEntry(const FULMLogEntry& Entry);
	
	// Memory management helpers
	void TrimMemoryBudget();
	void TrimChannelForMemory(const FString& ChannelName, int32 EntriesToRemove);
	
	// File I/O helpers
	FString FormatLogEntryForFile(const FULMLogEntry& Entry) const;
	FString GenerateLogFilePath(const FString& ChannelName) const;
};