#include "Core/ULMSubsystem.h"
#include "Channels/ULMChannel.h"
#include "Logging/ULMLogging.h"
#include "Logging/ULMLogProcessor.h"
#include "FileIO/ULMFileWriter.h"
#include "MemoryManagement/ULMLogRotation.h"
#include "Configuration/ULMSettings.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/DateTime.h"

extern ULM_API std::atomic<FULMChannelRegistry*> GULMChannelRegistry;
extern ULM_API std::atomic<UULMSubsystem*> GULMSubsystem;
static bool IsChannelInMasterList(const FString& ChannelName)
{
#define ULM_CHECK_CHANNEL(EnumName, ChannelStr, DisplayStr) \
	if (ChannelName == TEXT(ChannelStr)) return true;
	
	ULM_CHANNEL_LIST(ULM_CHECK_CHANNEL)
#undef ULM_CHECK_CHANNEL
	
	return false;
}

void UULMSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	const UULMSettings* Settings = UULMSettings::Get();
	if (!Settings)
	{
#if !UE_BUILD_SHIPPING
		UE_LOG(LogTemp, Error, TEXT("ULM: Failed to load ULMSettings - using defaults"));
#endif
	}
	
	LogProcessor = nullptr;
	ProcessorThread = nullptr;
	FileWriter = nullptr;
	FileWriterThread = nullptr;
	
	if (Settings)
	{
		bFileLoggingEnabled = Settings->bFileLoggingEnabled;
		CurrentLogFormat = EULMLogFormat::JSON;
		JSONConfig = Settings->JSONConfig;
		
		MemoryTracker.SetMemoryBudget(Settings->MemoryBudgetMB * 1024 * 1024);
		
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("ULM Settings loaded: Performance Tier=%d, Memory Budget=%dMB, File Logging=%s"),
			(int32)Settings->PerformanceTier, Settings->MemoryBudgetMB, 
			Settings->bFileLoggingEnabled ? TEXT("Enabled") : TEXT("Disabled"));
	}
	else
	{
		bFileLoggingEnabled = true;
		CurrentLogFormat = EULMLogFormat::JSON;
		JSONConfig = FULMJSONConfig();
		MemoryTracker.SetMemoryBudget(50 * 1024 * 1024);
	}
	
	// Initialize core infrastructure first (no logging yet - system not ready)
	ChannelRegistry = MakeUnique<FULMChannelRegistry>();
	
	// Thread-safe global state initialization using memory_order_release
	GULMChannelRegistry.store(ChannelRegistry.Get(), std::memory_order_release);
	GULMSubsystem.store(this, std::memory_order_release);
	
	// Now logging system is ready - explicitly register Subsystem channel first
	RegisterChannel(TEXT("Subsystem"), FULMChannelConfig());
	
	// Log initialization steps
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Channel registry and global subsystem references established"));
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Creating log processor thread..."));
	TUniquePtr<FULMLogProcessor> ProcessorPtr = MakeUnique<FULMLogProcessor>(this, LogMessageQueue);
	ProcessorThread = FRunnableThread::Create(ProcessorPtr.Get(), TEXT("ULMLogProcessor"), 0, TPri_Normal);
	
	if (ProcessorThread)
	{
		LogProcessor = ProcessorPtr.Release(); // Transfer ownership to raw pointer for thread management
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("Log processor thread created successfully - ID: %d"), 
			ProcessorThread->GetThreadID());
	}
	else
	{
		// ProcessorPtr will automatically clean up the allocated processor
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Critical, 
			TEXT("CRITICAL: Failed to create log processor thread - message processing will be disabled"));
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Creating file writer thread..."));
	TUniquePtr<FULMFileWriter> FileWriterPtr = MakeUnique<FULMFileWriter>(this, FileWriteQueue);
	FileWriterThread = FRunnableThread::Create(FileWriterPtr.Get(), TEXT("ULMFileWriter"), 0, TPri_Normal);
	
	if (FileWriterThread)
	{
		FileWriter = FileWriterPtr.Release(); // Transfer ownership to raw pointer for thread management
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("File writer thread created successfully - ID: %d"), 
			FileWriterThread->GetThreadID());
	}
	else
	{
		// FileWriterPtr will automatically clean up the allocated file writer
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Critical, 
			TEXT("CRITICAL: Failed to create file writer thread - file logging will be disabled"));
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Initializing log rotation and retention managers..."));
	LogRotator = MakeUnique<FULMLogRotator>(this);
	RetentionManager = MakeUnique<FULMRetentionManager>(this);
	
	FULMRotationConfig RotationConfig;
	if (Settings)
	{
		RotationConfig = Settings->RotationConfig;
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("Applied rotation config: MaxSize=%lldMB, Retention=%d days"),
			RotationConfig.MaxFileSizeBytes / (1024 * 1024), RotationConfig.RetentionDays);
	}
	else
	{
		RotationConfig = FULMRotationConfig();
	}
	
	LogRotator->SetRotationConfig(RotationConfig);
	RetentionManager->SetRetentionConfig(RotationConfig);
	
	RetentionManager->SchedulePeriodicCleanup();
	if (RotationConfig.bAutoCleanupOnStartup)
	{
		FString BaseLogPath = FPaths::ProjectLogDir() / TEXT("ULM");
		if (Settings && !Settings->CustomLogDirectory.Path.IsEmpty())
		{
			FString CustomPath = Settings->CustomLogDirectory.Path;
			if (FPaths::DirectoryExists(CustomPath) || IFileManager::Get().MakeDirectory(*CustomPath, true))
			{
				BaseLogPath = CustomPath;
			}
		}
		RetentionManager->PerformCleanup(BaseLogPath);
	}
	
	// Reset diagnostics
	QueueDiagnostics.Reset();
	
	// Auto-register all channels from master list if enabled in settings
	if (!Settings || Settings->bAutoRegisterChannels)
	{
		RegisterAllChannelsFromMasterList();
	}
	else
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("Auto-registration disabled in settings - channels must be registered manually"));
	}
	
	// Final initialization summary
	int32 ActiveThreads = 0;
	if (ProcessorThread) ActiveThreads++;
	if (FileWriterThread) ActiveThreads++;
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
		TEXT("ULM initialization complete - %d threads active, %d channels registered"), 
		ActiveThreads, ChannelRegistry->GetAllChannels().Num());
	
	// Force subsystem channel to appear in UE Output Log dropdown
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULMSubsystem Channel Registered"));
}

void UULMSubsystem::Deinitialize()
{
	ULM_LOG_CRITICAL_SYSTEM(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM shutdown initiated - stopping worker threads..."));
	
	// Stop the processor thread
	if (LogProcessor)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Requesting log processor thread shutdown..."));
		LogProcessor->RequestStop();
	}
	
	// Stop the file writer thread
	if (FileWriter)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Requesting file writer thread shutdown..."));
		FileWriter->RequestStop();
	}
	
	// Wait for threads to finish
	if (ProcessorThread)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Waiting for log processor thread completion..."));
		double StartTime = FPlatformTime::Seconds();
		ProcessorThread->WaitForCompletion();
		double EndTime = FPlatformTime::Seconds();
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("Log processor thread shutdown completed in %.2f ms"), 
			(EndTime - StartTime) * 1000.0);
		delete ProcessorThread;
		ProcessorThread = nullptr;
	}
	
	if (FileWriterThread)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Waiting for file writer thread completion..."));
		double StartTime = FPlatformTime::Seconds();
		FileWriterThread->WaitForCompletion();
		double EndTime = FPlatformTime::Seconds();
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("File writer thread shutdown completed in %.2f ms"), 
			(EndTime - StartTime) * 1000.0);
		delete FileWriterThread;
		FileWriterThread = nullptr;
	}
	
	// Clean up processor
	if (LogProcessor)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Cleaning up log processor instance..."));
		delete LogProcessor;
		LogProcessor = nullptr;
	}
	
	// Clean up file writer
	if (FileWriter)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Cleaning up file writer instance..."));
		delete FileWriter;
		FileWriter = nullptr;
	}
	
	// Clear global pointers with thread-safe operations
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Clearing global subsystem references..."));
	GULMChannelRegistry.store(nullptr, std::memory_order_release);
	GULMSubsystem.store(nullptr, std::memory_order_release);
	
	// Thread-safe cleanup of stored data
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Performing final memory cleanup and data purge..."));
	FScopeLock Lock(&StorageCriticalSection);
	
	// Free memory explicitly
	for (auto& Entry : LogEntries)
	{
		Entry.Value.Empty();
	}
	LogEntries.Empty();
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("All log data and memory pools released"));
	
	// Clean up channel registry
	ChannelRegistry.Reset();
	
	ULM_LOG_CRITICAL_SYSTEM(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM shutdown complete - all threads terminated, resources cleaned up"));
	
	ULM_LOG_CRITICAL_SYSTEM(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM Subsystem fully deinitialized - all systems stopped"));
	
	Super::Deinitialize();
}

// Hierarchical channel management
void UULMSubsystem::RegisterChannel(const FString& ChannelName, const FULMChannelConfig& Config)
{
	// Only allow channels that are in the master list
	if (!IsChannelInMasterList(ChannelName))
	{
		UE_LOG(LogTemp, Warning, TEXT("ULM: Cannot register channel '%s'. Only master list channels are allowed."), *ChannelName);
		return;
	}
	
	if (ChannelRegistry)
	{
		ChannelRegistry->RegisterChannel(ChannelName, Config);
		
		// Create log storage for this channel
		FScopeLock Lock(&StorageCriticalSection);
		if (!LogEntries.Contains(ChannelName))
		{
			TArray<FULMLogEntry> NewEntries;
			NewEntries.Reserve(FMath::Min(Config.MaxLogEntries, 100));
			LogEntries.Emplace(ChannelName, MoveTemp(NewEntries));
		}
	}
}

bool UULMSubsystem::IsChannelRegistered(const FString& ChannelName) const
{
	return ChannelRegistry ? ChannelRegistry->IsChannelRegistered(ChannelName) : false;
}

TArray<FString> UULMSubsystem::GetChildChannels(const FString& ParentChannel) const
{
	return ChannelRegistry ? ChannelRegistry->GetChildChannels(ParentChannel) : TArray<FString>();
}

void UULMSubsystem::UpdateChannelConfig(const FString& ChannelName, const FULMChannelConfig& Config)
{
	if (ChannelRegistry)
	{
		ChannelRegistry->UpdateChannelConfig(ChannelName, Config);
	}
}

FULMChannelConfig UULMSubsystem::GetChannelConfig(const FString& ChannelName) const
{
	return ChannelRegistry ? ChannelRegistry->GetChannelConfig(ChannelName) : FULMChannelConfig();
}

void UULMSubsystem::SetChannelEnabled(const FString& ChannelName, bool bEnabled, bool bRecursive)
{
	if (ChannelRegistry)
	{
		ChannelRegistry->SetChannelEnabled(ChannelName, bEnabled, bRecursive);
	}
}

void UULMSubsystem::SetChannelVerbosity(const FString& ChannelName, EULMVerbosity MinVerbosity, bool bRecursive)
{
	if (ChannelRegistry)
	{
		ChannelRegistry->SetChannelVerbosity(ChannelName, MinVerbosity, bRecursive);
	}
}

void UULMSubsystem::LogMessage(const FString& Message, const FString& Channel, EULMVerbosity Verbosity)
{
	if (Message.IsEmpty())
	{
		return;
	}

	const FString& ChannelName = Channel.IsEmpty() ? TEXT("Default") : Channel;
	
	// Call ULMLogMessage for full logging (includes Output Log)
	ULMLogMessage(ChannelName, Verbosity, Message, nullptr, nullptr, 0);
}

void UULMSubsystem::StoreLogEntryInternal(const FString& Message, const FString& ChannelName, EULMVerbosity Verbosity)
{
	// Fast path: check if channel can log (includes rate limiting)
	if (ChannelRegistry && !ChannelRegistry->CanChannelLog(ChannelName, Verbosity))
	{
		return;
	}

	// Check queue size to prevent memory issues
	if (GetQueueSize() >= MAX_QUEUE_SIZE)
	{
		QueueDiagnostics.DroppedCount.Increment();
		return;
	}

	// Record enqueue time for diagnostics
	double StartTime = FPlatformTime::Seconds();
	
	// Enqueue the message (lock-free operation)
	FULMLogQueueEntry QueueEntry(Message, ChannelName, Verbosity);
	if (LogMessageQueue.Enqueue(QueueEntry))
	{
		QueueDiagnostics.EnqueueCount.Increment();
		
		// Wake up processor thread
		if (LogProcessor)
		{
			LogProcessor->WakeUp();
		}
	}
	else
	{
		QueueDiagnostics.DroppedCount.Increment();
	}
	
	// Update diagnostics
	double EndTime = FPlatformTime::Seconds();
	int64 EnqueueTimeMicros = (int64)((EndTime - StartTime) * 1000000.0);
	QueueDiagnostics.TotalEnqueueTime.Add(EnqueueTimeMicros);
}

TArray<FULMLogEntry> UULMSubsystem::GetLogEntries(const FString& Channel, int32 MaxEntries) const
{
	FScopeLock Lock(&StorageCriticalSection);
	
	TArray<FULMLogEntry> Result;
	Result.Reserve(FMath::Max(MaxEntries, 100));
	
	if (Channel.IsEmpty())
	{
		// Aggregate all channels efficiently
		for (const auto& ChannelPair : LogEntries)
		{
			Result.Append(ChannelPair.Value);
		}
		
		// Sort by timestamp - stable sort for consistent ordering
		Result.StableSort([](const FULMLogEntry& A, const FULMLogEntry& B)
		{
			return A.Timestamp < B.Timestamp;
		});
	}
	else
	{
		// Single channel lookup
		if (const TArray<FULMLogEntry>* ChannelEntries = LogEntries.Find(Channel))
		{
			Result = *ChannelEntries;
		}
	}
	
	// Trim to requested count from end (most recent)
	if (MaxEntries > 0 && Result.Num() > MaxEntries)
	{
		const int32 StartIndex = Result.Num() - MaxEntries;
		Result.RemoveAt(0, StartIndex, EAllowShrinking::No);
	}
	
	return Result;
}

void UULMSubsystem::ClearChannel(const FString& ChannelName)
{
	FScopeLock Lock(&StorageCriticalSection);
	
	if (TArray<FULMLogEntry>* ChannelEntries = LogEntries.Find(ChannelName))
	{
		ChannelEntries->Empty();
	}
}

void UULMSubsystem::ClearAllChannels()
{
	FScopeLock Lock(&StorageCriticalSection);
	
	for (auto& ChannelPair : LogEntries)
	{
		ChannelPair.Value.Empty();
	}
}

TArray<FString> UULMSubsystem::GetRegisteredChannels() const
{
	return ChannelRegistry ? ChannelRegistry->GetAllChannels() : TArray<FString>();
}

// Helper function to convert enum to string - generated from master list
static FString GetChannelStringFromEnum(EULMChannel Channel)
{
	switch (Channel)
	{
#define ULM_ENUM_TO_STRING(EnumName, ChannelStr, DisplayStr) \
		case EULMChannel::EnumName: return TEXT(ChannelStr);
		
		ULM_CHANNEL_LIST(ULM_ENUM_TO_STRING)
#undef ULM_ENUM_TO_STRING
		
		default: return TEXT("ULM");
	}
}

void UULMSubsystem::LogMessageEnhanced(const FString& Message, EULMChannel Channel, EULMVerbosity Verbosity, bool bPrintToScreen, float Duration, const FString& CustomChannel)
{
	// Block access to protected channels
	if (Channel == EULMChannel::ULM || Channel == EULMChannel::Subsystem)
	{
		// Users cannot log to ULM or ULMSubsystem channels - these are system-reserved
		UE_LOG(LogTemp, Warning, TEXT("Access denied: ULM and ULMSubsystem channels are reserved for system use"));
		return;
	}
	
	// Determine the actual channel name
	FString ChannelName;
	if (Channel == EULMChannel::Custom && !CustomChannel.IsEmpty())
	{
		ChannelName = CustomChannel;
	}
	else
	{
		ChannelName = GetChannelStringFromEnum(Channel);
	}
	
	// Call the main ULM logging function - this handles both Output Log and internal storage
	ULMLogMessage(ChannelName, Verbosity, Message, nullptr, nullptr, 0);
	
	// Print to screen if requested
	if (bPrintToScreen)
	{
		// Use appropriate color based on verbosity with more distinct colors
		FLinearColor ScreenColor;
		switch (Verbosity)
		{
			case EULMVerbosity::Message:  ScreenColor = FLinearColor(0.0f, 1.0f, 0.0f, 1.0f); break; // Green
			case EULMVerbosity::Warning:  ScreenColor = FLinearColor(1.0f, 0.85f, 0.0f, 1.0f); break; // Bright yellow
			case EULMVerbosity::Error:    ScreenColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f); break; // Red
			case EULMVerbosity::Critical: ScreenColor = FLinearColor(0.5f, 0.0f, 0.5f, 1.0f); break; // Purple for critical
			default: ScreenColor = FLinearColor::White; break;
		}
		
		// Format message with channel prefix for screen display
		FString ScreenMessage = FString::Printf(TEXT("[%s] %s"), *ChannelName, *Message);
		
		// Print to screen (similar to Blueprint Print String)
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, Duration, ScreenColor.ToFColor(true), ScreenMessage);
		}
	}
}

void UULMSubsystem::RegisterAllChannelsFromMasterList()
{
	// Get default channel configuration from settings
	const UULMSettings* Settings = UULMSettings::Get();
	FULMChannelConfig DefaultConfig;
	
	if (Settings)
	{
		DefaultConfig = Settings->DefaultChannelConfig;
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("Using settings default channel config: Enabled=%s, MinVerbosity=%d, MaxEntries=%d"),
			DefaultConfig.bEnabled ? TEXT("true") : TEXT("false"), 
			(int32)DefaultConfig.MinVerbosity, DefaultConfig.MaxLogEntries);
	}
	else
	{
		// Fallback defaults
		DefaultConfig.bEnabled = true;
		DefaultConfig.MinVerbosity = EULMVerbosity::Message;
		DefaultConfig.MaxLogEntries = 1000;
	}

	// Count registered channels for verification
	int32 RegisteredCount = 0;

#define ULM_REGISTER_CHANNEL(EnumName, ChannelStr, DisplayStr) \
	if (FCString::Strcmp(TEXT(ChannelStr), TEXT("Custom")) != 0) \
	{ \
		RegisterChannel(TEXT(ChannelStr), DefaultConfig); \
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM%s Channel Registered"), TEXT(ChannelStr)); \
		RegisteredCount++; \
	}

	ULM_CHANNEL_LIST(ULM_REGISTER_CHANNEL)
#undef ULM_REGISTER_CHANNEL

	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM initialization complete: %d channels registered and available in Output Log"), RegisteredCount);
	
	// Log all available log categories for debugging
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Available ULM log categories in Output Log: ULM, ULMGameplay, ULMNetwork, ULMPerformance, ULMDebug, ULMAI, ULMPhysics, ULMAudio, ULMAnimation, ULMUI, ULMSubsystem"));
	
	// IMPORTANT: Log to each channel to confirm registration and ensure they appear in Output Log dropdown
	// Unreal Engine only shows log categories that have been used at least once
	ULMLogMessage(TEXT("ULM"), EULMVerbosity::Message, TEXT("ULM Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Gameplay"), EULMVerbosity::Message, TEXT("ULMGameplay Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Network"), EULMVerbosity::Message, TEXT("ULMNetwork Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Performance"), EULMVerbosity::Message, TEXT("ULMPerformance Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Debug"), EULMVerbosity::Message, TEXT("ULMDebug Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("AI"), EULMVerbosity::Message, TEXT("ULMAI Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Physics"), EULMVerbosity::Message, TEXT("ULMPhysics Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Audio"), EULMVerbosity::Message, TEXT("ULMAudio Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Animation"), EULMVerbosity::Message, TEXT("ULMAnimation Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("UI"), EULMVerbosity::Message, TEXT("ULMUI Channel Registered"), nullptr, nullptr, 0);
	ULMLogMessage(TEXT("Subsystem"), EULMVerbosity::Message, TEXT("ULMSubsystem Channel Registered"), nullptr, nullptr, 0);
}

void UULMSubsystem::ProcessLogEntry(const FULMLogQueueEntry& QueueEntry)
{
	// Convert queue entry to log entry
	FULMLogEntry LogEntry(QueueEntry.Message, QueueEntry.Channel, QueueEntry.Verbosity);
	LogEntry.Timestamp = QueueEntry.Timestamp;
	LogEntry.ThreadId = QueueEntry.ThreadId;
	
	// Store the processed entry
	StoreProcessedLogEntry(LogEntry);
}

void UULMSubsystem::StoreProcessedLogEntry(const FULMLogEntry& Entry)
{
	// Only allow channels that are in the master list (check outside lock)
	if (!IsChannelInMasterList(Entry.Channel))
	{
		// For non-master-list channels, log a warning and drop the message
		UE_LOG(LogTemp, Warning, TEXT("ULM: Dropping log for unregistered channel '%s'. Only master list channels are allowed."), *Entry.Channel);
		return;
	}
	
	// Calculate memory footprint before adding (outside lock)
	SIZE_T EntryMemorySize = MemoryTracker.CalculateLogEntrySize(Entry);
	
	// Check if adding this entry would exceed memory budget (outside lock)
	if (MemoryTracker.WouldExceedBudget(EntryMemorySize))
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, 
			TEXT("Memory budget would be exceeded by new log entry - triggering emergency trimming"));
		
		// Trigger memory budget trimming (this will acquire its own lock)
		TrimMemoryBudget();
		
		// Check again after trimming
		if (MemoryTracker.WouldExceedBudget(EntryMemorySize))
		{
			ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Critical, 
				TEXT("CRITICAL: Memory budget still exceeded after emergency trimming - dropping log entry (system in crisis mode)"));
			return;
		}
		else
		{
			ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
				TEXT("Memory budget trimming successful - log entry can now be processed"));
		}
	}
	
	// Now acquire lock for actual storage operations
	FScopeLock Lock(&StorageCriticalSection);
	
	// Auto-register channel if needed (only for master list channels)
	if (!LogEntries.Contains(Entry.Channel))
	{
		TArray<FULMLogEntry> NewEntries;
		NewEntries.Reserve(100);
		LogEntries.Emplace(Entry.Channel, MoveTemp(NewEntries));
		
		// Register in channel registry too
		if (ChannelRegistry && !ChannelRegistry->IsChannelRegistered(Entry.Channel))
		{
			ChannelRegistry->RegisterChannel(Entry.Channel);
		}
	}
	
	// Add entry
	TArray<FULMLogEntry>& ChannelEntries = LogEntries[Entry.Channel];
	ChannelEntries.Add(Entry);
	
	// Track memory usage
	MemoryTracker.AddMemoryUsage(Entry.Channel, EntryMemorySize);
	
	// Queue for file writing if enabled (exclude master ULM channel to avoid redundancy)
	if (bFileLoggingEnabled && FileWriter && Entry.Channel != TEXT("ULM"))
	{
		FString LogLine = FormatLogEntryForFile(Entry);
		FString FilePath = GenerateLogFilePath(Entry.Channel);
		FULMFileWriteEntry FileEntry(LogLine, FilePath, Entry.Timestamp.ToUnixTimestamp());
		
		// Enqueue for asynchronous file writing
		if (!FileWriteQueue.Enqueue(FileEntry))
		{
			// File write queue is full - this is a diagnostic issue
			UE_LOG(LogTemp, Warning, TEXT("ULM: File write queue full, dropping file write for channel '%s'"), *Entry.Channel);
		}
		else
		{
			// Wake up file writer thread
			FileWriter->WakeUp();
		}
	}
	
	// Trim based on channel settings
	if (ChannelRegistry)
	{
		const FULMChannelConfig Config = ChannelRegistry->GetChannelConfig(Entry.Channel);
		if (ChannelEntries.Num() > Config.MaxLogEntries)
		{
			const int32 ElementsToRemove = ChannelEntries.Num() - Config.MaxLogEntries;
			TrimChannelForMemory(Entry.Channel, ElementsToRemove);
		}
	}
}

int32 UULMSubsystem::GetQueueSize() const
{
	// TQueue doesn't have a direct size method, so we estimate based on counters
	int32 Enqueued = QueueDiagnostics.EnqueueCount.GetValue();
	int32 Dequeued = QueueDiagnostics.DequeueCount.GetValue();
	return FMath::Max(0, Enqueued - Dequeued);
}

bool UULMSubsystem::IsQueueHealthy() const
{
	int32 CurrentSize = GetQueueSize();
	return CurrentSize < (MAX_QUEUE_SIZE * 0.8f); // Consider unhealthy if 80% full
}

void UULMSubsystem::UpdateProcessingDiagnostics(int64 DequeueTimeMicros)
{
	QueueDiagnostics.DequeueCount.Increment();
	QueueDiagnostics.ProcessedCount.Increment();
	QueueDiagnostics.TotalDequeueTime.Add(DequeueTimeMicros);
}

// File I/O helper methods
FString UULMSubsystem::FormatLogEntryForFile(const FULMLogEntry& Entry) const
{
	// Track format operation timing
	double StartTime = FPlatformTime::Seconds();
	
	// JSON-only logging: Always format as JSON
	FString Result = JSONFormatter.FormatAsJSON(Entry, JSONConfig);
	
	// Update format diagnostics
	double EndTime = FPlatformTime::Seconds();
	double FormatTime = (EndTime - StartTime) * 1000000.0; // Convert to microseconds
	
	FormatDiagnostics.TotalFormatOperations++;
	FormatDiagnostics.TotalFormatTimeMicros += FormatTime;
	FormatDiagnostics.AverageFormatTimeMicros = FormatDiagnostics.TotalFormatTimeMicros / FormatDiagnostics.TotalFormatOperations;
	
	if (FormatTime > FormatDiagnostics.MaxFormatTimeMicros)
	{
		FormatDiagnostics.MaxFormatTimeMicros = FormatTime;
	}
	
	return Result;
}


FString UULMSubsystem::GenerateLogFilePath(const FString& ChannelName) const
{
	// Use log rotator to generate appropriate file path with rotation support
	FString BaseLogPath = FPaths::ProjectLogDir() / TEXT("ULM");
	
	if (LogRotator)
	{
		return LogRotator->GetActiveFilePath(ChannelName, BaseLogPath);
	}
	
	// Fallback to simple filename generation if rotator not available
	FDateTime Now = FDateTime::Now();
	FString DateString = Now.ToString(TEXT("%Y%m%d"));
	FString Filename = FString::Printf(TEXT("ULM_%s_%s_001.json"), *ChannelName, *DateString);
	
	return BaseLogPath / Filename;
}

// File I/O configuration methods
void UULMSubsystem::SetFileLoggingEnabled(bool bEnabled)
{
	bFileLoggingEnabled = bEnabled;
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM: File logging %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

bool UULMSubsystem::IsFileLoggingEnabled() const
{
	return bFileLoggingEnabled;
}

void UULMSubsystem::SetLogFilePath(const FString& NewPath)
{
	if (FileWriter)
	{
		FileWriter->SetBaseLogPath(NewPath);
	}
}

FString UULMSubsystem::GetLogFilePath() const
{
	return FPaths::ProjectLogDir() / TEXT("ULM");
}

FULMFileIODiagnostics UULMSubsystem::GetFileIODiagnostics() const
{
	if (FileWriter)
	{
		return FileWriter->GetDiagnostics();
	}
	return FULMFileIODiagnostics();
}

void UULMSubsystem::ResetFileIODiagnostics()
{
	if (FileWriter)
	{
		FileWriter->ResetDiagnostics();
	}
}

// Memory budget management methods
void UULMSubsystem::SetMemoryBudget(int64 BudgetBytes)
{
	MemoryTracker.SetMemoryBudget(static_cast<SIZE_T>(BudgetBytes));
}

int64 UULMSubsystem::GetMemoryBudget() const
{
	return static_cast<int64>(MemoryTracker.GetMemoryBudget());
}

FULMMemoryDiagnostics UULMSubsystem::GetMemoryDiagnostics() const
{
	return MemoryTracker.ToBlueprint();
}

void UULMSubsystem::ResetMemoryDiagnostics()
{
	MemoryTracker.Reset();
}

bool UULMSubsystem::IsMemoryBudgetHealthy() const
{
	SIZE_T CurrentUsage = MemoryTracker.GetTotalMemoryUsage();
	SIZE_T Budget = MemoryTracker.GetMemoryBudget();
	
	// Consider healthy if under 80% of budget
	return CurrentUsage < (Budget * 0.8);
}

int64 UULMSubsystem::GetChannelMemoryUsage(const FString& ChannelName) const
{
	return static_cast<int64>(MemoryTracker.GetChannelMemoryUsage(ChannelName));
}

void UULMSubsystem::TrimMemoryBudget()
{
	// Add debug log before acquiring lock
	SIZE_T CurrentUsage = MemoryTracker.GetTotalMemoryUsage();
	SIZE_T Budget = MemoryTracker.GetMemoryBudget();
	
	ULM_WARNING(CHANNEL_PERFORMANCE, TEXT("TrimMemoryBudget called: %lld/%lld bytes"), CurrentUsage, Budget);
	
	FScopeLock Lock(&StorageCriticalSection);
	
	// Re-check after acquiring lock
	CurrentUsage = MemoryTracker.GetTotalMemoryUsage();
	Budget = MemoryTracker.GetMemoryBudget();
	
	// Check if we're actually within a reasonable buffer (not just technically under)
	SIZE_T ReasonableBuffer = Budget * 0.02; // 2% buffer 
	if (CurrentUsage <= (Budget - ReasonableBuffer))
	{
		// Log to both channels for different purposes
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("Memory budget health check: System healthy (%lld/%lld bytes, %.1f%% utilized)"), 
			CurrentUsage, Budget, (static_cast<float>(CurrentUsage) / static_cast<float>(Budget)) * 100.0f);
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
			TEXT("Memory utilization: %lld bytes (%.1f%% of budget)"), 
			CurrentUsage, (static_cast<float>(CurrentUsage) / static_cast<float>(Budget)) * 100.0f);
		return; // Safely within budget
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, 
		TEXT("Memory budget approaching limit (%lld/%lld bytes, %.1f%%) - initiating preventive trimming"), 
		CurrentUsage, Budget, (static_cast<float>(CurrentUsage) / static_cast<float>(Budget)) * 100.0f);
	
	// Calculate how much we need to reduce - be more aggressive if severely over budget
	float OverageRatio = static_cast<float>(CurrentUsage) / static_cast<float>(Budget);
	float TargetPercent = 0.75f; // Default to 75%
	
	if (OverageRatio > 1.2f) // If over 120% of budget, trim to 50%
	{
		TargetPercent = 0.5f;
	}
	else if (OverageRatio > 1.1f) // If over 110% of budget, trim to 60%
	{
		TargetPercent = 0.6f;
	}
	
	SIZE_T TargetReduction = CurrentUsage - (Budget * TargetPercent);
	SIZE_T TotalReduced = 0;
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, 
		TEXT("Memory budget exceeded (%lld/%lld bytes, %.1f%%) - initiating emergency trimming to %.0f%% of budget"), 
		CurrentUsage, Budget, OverageRatio * 100.0f, TargetPercent * 100.0f);
	
	// Build list of channels sorted by memory usage (largest first)
	TArray<TPair<FString, SIZE_T>> ChannelsByUsage;
	for (const auto& LogEntryPair : LogEntries)
	{
		SIZE_T ChannelUsage = MemoryTracker.GetChannelMemoryUsage(LogEntryPair.Key);
		ChannelsByUsage.Emplace(LogEntryPair.Key, ChannelUsage);
	}
	
	// Sort by usage (largest first)
	ChannelsByUsage.Sort([](const TPair<FString, SIZE_T>& A, const TPair<FString, SIZE_T>& B) {
		return A.Value > B.Value;
	});
	
	// Trim from largest channels first
	for (const auto& ChannelPair : ChannelsByUsage)
	{
		if (TotalReduced >= TargetReduction)
		{
			break;
		}
		
		const FString& ChannelName = ChannelPair.Key;
		TArray<FULMLogEntry>* ChannelEntries = LogEntries.Find(ChannelName);
		
		if (ChannelEntries && ChannelEntries->Num() > 0)
		{
			// Calculate removal percentage based on how much we still need to reduce
			float RemainingReduction = static_cast<float>(TargetReduction - TotalReduced);
			float ChannelSize = static_cast<float>(ChannelPair.Value);
			float RemovalPercent = 0.25f; // Default 25%
			
			// Be more aggressive if we need significant reduction
			if (RemainingReduction > ChannelSize * 0.5f)
			{
				RemovalPercent = 0.75f; // Remove 75% if we need major reduction
			}
			else if (RemainingReduction > ChannelSize * 0.25f)
			{
				RemovalPercent = 0.5f; // Remove 50% if we need moderate reduction
			}
			
			int32 EntriesToRemove = FMath::Max(1, static_cast<int32>(ChannelEntries->Num() * RemovalPercent));
			EntriesToRemove = FMath::Min(EntriesToRemove, ChannelEntries->Num());
			
			SIZE_T MemoryBefore = MemoryTracker.GetChannelMemoryUsage(ChannelName);
			TrimChannelForMemory(ChannelName, EntriesToRemove);
			SIZE_T MemoryAfter = MemoryTracker.GetChannelMemoryUsage(ChannelName);
			
			SIZE_T ReducedThisChannel = MemoryBefore - MemoryAfter;
			TotalReduced += ReducedThisChannel;
			
			ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
				TEXT("Trimmed %d entries (%.0f%%) from channel '%s', reduced memory by %lld bytes"), 
				EntriesToRemove, RemovalPercent * 100.0f, *ChannelName, ReducedThisChannel);
		}
	}
	
	MemoryTracker.TrimmingEventsCounter.Increment();
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
		TEXT("Memory budget trimming completed - freed %lld bytes, system health restored"), TotalReduced);
	
	// Add performance metrics separately
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Memory trimming performance: freed %lld bytes from %d channels"), TotalReduced, ChannelsByUsage.Num());
}

void UULMSubsystem::TrimChannelForMemory(const FString& ChannelName, int32 EntriesToRemove)
{
	TArray<FULMLogEntry>* ChannelEntries = LogEntries.Find(ChannelName);
	if (!ChannelEntries || EntriesToRemove <= 0 || ChannelEntries->Num() == 0)
	{
		return;
	}
	
	// Ensure we don't remove more entries than exist
	EntriesToRemove = FMath::Min(EntriesToRemove, ChannelEntries->Num());
	
	// Calculate memory to be removed
	SIZE_T MemoryToRemove = 0;
	for (int32 i = 0; i < EntriesToRemove; ++i)
	{
		MemoryToRemove += MemoryTracker.CalculateLogEntrySize((*ChannelEntries)[i]);
	}
	
	// Remove oldest entries (from the beginning of the array)
	ChannelEntries->RemoveAt(0, EntriesToRemove, EAllowShrinking::No);
	
	// Update memory tracking
	MemoryTracker.RemoveMemoryUsage(ChannelName, MemoryToRemove);
}


// JSON format configuration methods
void UULMSubsystem::SetLogFormat(EULMLogFormat Format)
{
	// JSON-only logging: Always enforce JSON format
	CurrentLogFormat = EULMLogFormat::JSON;
	
	if (Format != EULMLogFormat::JSON)
	{
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Warning, 
			TEXT("Log format request ignored - JSON-only logging enforced"));
	}
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Log format: JSON (JSON-only logging active)"));
	
	// Update diagnostics
	FormatDiagnostics.CurrentFormat = TEXT("JSON");
}

EULMLogFormat UULMSubsystem::GetLogFormat() const
{
	return CurrentLogFormat;
}

void UULMSubsystem::SetJSONConfig(const FULMJSONConfig& Config)
{
	JSONConfig = Config;
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("JSON config updated - SessionId: %s, BuildVersion: %s, Compact: %s"), 
		JSONConfig.bIncludeSessionId ? TEXT("Yes") : TEXT("No"),
		JSONConfig.bIncludeBuildVersion ? TEXT("Yes") : TEXT("No"),
		JSONConfig.bCompactFormat ? TEXT("Yes") : TEXT("No"));
}

FULMJSONConfig UULMSubsystem::GetJSONConfig() const
{
	return JSONConfig;
}

FULMFormatDiagnostics UULMSubsystem::GetFormatDiagnostics() const
{
	return FormatDiagnostics;
}

void UULMSubsystem::ResetFormatDiagnostics()
{
	FormatDiagnostics = FULMFormatDiagnostics();
	FormatDiagnostics.CurrentFormat = TEXT("JSON");  // JSON-only logging
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("Format diagnostics reset - JSON-only logging"));
}

// Log rotation and retention configuration methods
void UULMSubsystem::SetRotationConfig(const FULMRotationConfig& Config)
{
	if (LogRotator)
	{
		LogRotator->SetRotationConfig(Config);
	}
	
	if (RetentionManager)
	{
		RetentionManager->SetRetentionConfig(Config);
	}
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Rotation config updated - Max size: %lld MB, Retention: %d days"), 
		Config.MaxFileSizeBytes / (1024 * 1024), Config.RetentionDays);
}

FULMRotationConfig UULMSubsystem::GetRotationConfig() const
{
	if (LogRotator)
	{
		return LogRotator->GetRotationConfig();
	}
	
	return FULMRotationConfig();
}

FULMRotationDiagnostics UULMSubsystem::GetRotationDiagnostics() const
{
	FULMRotationDiagnostics Combined;
	
	if (LogRotator)
	{
		Combined = LogRotator->GetDiagnostics();
	}
	
	if (RetentionManager)
	{
		FULMRotationDiagnostics RetentionDiagnostics = RetentionManager->GetCleanupDiagnostics();
		Combined.FilesDeleted = RetentionDiagnostics.FilesDeleted;
		Combined.BytesFreed = RetentionDiagnostics.BytesFreed;
		Combined.LastCleanupTime = RetentionDiagnostics.LastCleanupTime;
	}
	
	return Combined;
}

void UULMSubsystem::ResetRotationDiagnostics()
{
	if (LogRotator)
	{
		LogRotator->ResetDiagnostics();
	}
	
	if (RetentionManager)
	{
		RetentionManager->ResetCleanupDiagnostics();
	}
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("Rotation diagnostics reset"));
}

void UULMSubsystem::ForceLogRotation(const FString& ChannelName)
{
	if (!LogRotator)
	{
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Warning, TEXT("Force rotation failed - Log rotator not initialized"));
		return;
	}
	
	FString BaseLogPath = FPaths::ProjectLogDir() / TEXT("ULM");
	
	if (ChannelName.IsEmpty())
	{
		// Rotate all active channels
		TArray<FString> RegisteredChannels = GetRegisteredChannels();
		for (const FString& Channel : RegisteredChannels)
		{
			FString CurrentFilePath = LogRotator->GetActiveFilePath(Channel, BaseLogPath);
			if (IFileManager::Get().FileExists(*CurrentFilePath))
			{
				FString NewFilePath = LogRotator->RotateFile(Channel, CurrentFilePath);
				ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
					TEXT("Force rotated channel: %s -> %s"), *Channel, *FPaths::GetCleanFilename(NewFilePath));
			}
		}
	}
	else
	{
		// Rotate specific channel
		FString CurrentFilePath = LogRotator->GetActiveFilePath(ChannelName, BaseLogPath);
		if (IFileManager::Get().FileExists(*CurrentFilePath))
		{
			FString NewFilePath = LogRotator->RotateFile(ChannelName, CurrentFilePath);
			ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
				TEXT("Force rotated channel: %s -> %s"), *ChannelName, *FPaths::GetCleanFilename(NewFilePath));
		}
		else
		{
			ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Warning, 
				TEXT("Force rotation failed - Channel not found: %s"), *ChannelName);
		}
	}
}

void UULMSubsystem::ForceRetentionCleanup()
{
	if (!RetentionManager)
	{
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Warning, TEXT("Force cleanup failed - Retention manager not initialized"));
		return;
	}
	
	FString BaseLogPath = FPaths::ProjectLogDir() / TEXT("ULM");
	RetentionManager->PerformCleanup(BaseLogPath);
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("Force retention cleanup completed"));
}

int64 UULMSubsystem::GetLogDiskUsage() const
{
	if (!RetentionManager)
	{
		return 0;
	}
	
	FString BaseLogPath = GetEffectiveLogDirectory();
	return RetentionManager->CalculateDiskUsage(BaseLogPath);
}

void UULMSubsystem::ApplySettings()
{
	const UULMSettings* Settings = UULMSettings::Get();
	if (!Settings)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Error, TEXT("ApplySettings failed - Settings not available"));
		return;
	}
	
	// Apply file logging setting
	bFileLoggingEnabled = Settings->bFileLoggingEnabled;
	
	// Apply JSON configuration
	JSONConfig = Settings->JSONConfig;
	
	// Apply memory budget
	MemoryTracker.SetMemoryBudget(Settings->MemoryBudgetMB * 1024 * 1024);
	
	// Apply rotation configuration
	if (LogRotator && RetentionManager)
	{
		LogRotator->SetRotationConfig(Settings->RotationConfig);
		RetentionManager->SetRetentionConfig(Settings->RotationConfig);
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
		TEXT("Settings applied: Memory=%dMB, FileLogging=%s, Tier=%d"),
		Settings->MemoryBudgetMB, Settings->bFileLoggingEnabled ? TEXT("On") : TEXT("Off"), 
		(int32)Settings->PerformanceTier);
}

bool UULMSubsystem::IsUsingCustomLogDirectory() const
{
	const UULMSettings* Settings = UULMSettings::Get();
	return Settings && !Settings->CustomLogDirectory.Path.IsEmpty();
}

FString UULMSubsystem::GetEffectiveLogDirectory() const
{
	const UULMSettings* Settings = UULMSettings::Get();
	if (Settings && !Settings->CustomLogDirectory.Path.IsEmpty())
	{
		// Validate the custom directory exists or can be created
		FString CustomPath = Settings->CustomLogDirectory.Path;
		if (FPaths::DirectoryExists(CustomPath) || IFileManager::Get().MakeDirectory(*CustomPath, true))
		{
			return CustomPath;
		}
		// If invalid, fall back to default
	}
	return FPaths::ProjectLogDir() / TEXT("ULM");
}

// Thread health monitoring functions
bool UULMSubsystem::AreThreadsHealthy() const
{
	// Check if both threads are created and presumably running
	bool bProcessorHealthy = (ProcessorThread != nullptr && LogProcessor != nullptr);
	bool bFileWriterHealthy = (FileWriterThread != nullptr && FileWriter != nullptr);
	
	return bProcessorHealthy && bFileWriterHealthy;
}

void UULMSubsystem::LogThreadHealthStatus() const
{
	int32 HealthyThreads = 0;
	int32 TotalThreads = 0;
	
	if (ProcessorThread && LogProcessor)
	{
		HealthyThreads++;
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("Log processor thread: HEALTHY (ID: %d)"), 
			ProcessorThread->GetThreadID());
	}
	else
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, 
			TEXT("Log processor thread: UNHEALTHY (missing thread or processor)"));
	}
	TotalThreads++;
	
	if (FileWriterThread && FileWriter)
	{
		HealthyThreads++;
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
			TEXT("File writer thread: HEALTHY (ID: %d)"), 
			FileWriterThread->GetThreadID());
	}
	else
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, 
			TEXT("File writer thread: UNHEALTHY (missing thread or writer)"));
	}
	TotalThreads++;
	
	// Queue health status
	FULMQueueDiagnostics QueueDiag = GetQueueDiagnostics();
	bool bQueueHealthy = QueueDiag.DroppedCount.GetValue() == 0 && QueueDiag.EnqueueCount.GetValue() >= QueueDiag.DequeueCount.GetValue();
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
		TEXT("ULM Thread Health Summary: %d/%d threads healthy, Queue: %s"), 
		HealthyThreads, TotalThreads, bQueueHealthy ? TEXT("HEALTHY") : TEXT("DEGRADED"));
	
	if (HealthyThreads < TotalThreads)
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, 
			TEXT("ULM subsystem is operating in degraded mode - some functionality may be impaired"));
	}
}

void UULMSubsystem::LogMemoryHealthStatus() const
{
	FULMMemoryDiagnostics MemoryDiag = GetMemoryDiagnostics();
	int64 Budget = MemoryDiag.MemoryBudget;
	int64 CurrentUsage = MemoryDiag.TotalMemoryUsed;
	float UtilizationPercent = Budget > 0 ? (static_cast<float>(CurrentUsage) / static_cast<float>(Budget)) * 100.0f : 0.0f;
	
	// Determine health status
	FString HealthStatus;
	EULMVerbosity LogVerbosity = EULMVerbosity::Message;
	
	if (UtilizationPercent < 75.0f)
	{
		HealthStatus = TEXT("HEALTHY");
		LogVerbosity = EULMVerbosity::Message;
	}
	else if (UtilizationPercent < 90.0f)
	{
		HealthStatus = TEXT("CAUTION");
		LogVerbosity = EULMVerbosity::Warning;
	}
	else if (UtilizationPercent < 100.0f)
	{
		HealthStatus = TEXT("CRITICAL");
		LogVerbosity = EULMVerbosity::Error;
	}
	else
	{
		HealthStatus = TEXT("CRISIS");
		LogVerbosity = EULMVerbosity::Critical;
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, LogVerbosity, 
		TEXT("Memory Health Status: %s - %lld/%lld bytes (%.1f%% utilized)"), 
		*HealthStatus, CurrentUsage, Budget, UtilizationPercent);
	
	// Additional diagnostic information
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
		TEXT("Memory Details: Trimming Events: %d, Entries: %d, Avg Entry Size: %.1f bytes"), 
		MemoryDiag.TrimmingEvents, MemoryDiag.TotalLogEntries, 
		MemoryDiag.TotalLogEntries > 0 ? (static_cast<float>(CurrentUsage) / static_cast<float>(MemoryDiag.TotalLogEntries)) : 0.0f);
	
	// Performance metrics (separate channel)
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Memory Performance: Utilization %.1f%%, Efficiency: %.1f bytes/entry"), 
		UtilizationPercent, 
		MemoryDiag.TotalLogEntries > 0 ? (static_cast<float>(CurrentUsage) / static_cast<float>(MemoryDiag.TotalLogEntries)) : 0.0f);
}

bool UULMSubsystem::IsMemoryHealthy() const
{
	FULMMemoryDiagnostics MemoryDiag = GetMemoryDiagnostics();
	int64 Budget = MemoryDiag.MemoryBudget;
	int64 CurrentUsage = MemoryDiag.TotalMemoryUsed;
	
	if (Budget <= 0) return false; // Invalid budget
	
	float UtilizationPercent = (static_cast<float>(CurrentUsage) / static_cast<float>(Budget)) * 100.0f;
	
	// Consider healthy if under 90% utilization
	return UtilizationPercent < 90.0f;
}

void UULMSubsystem::LogSystemHealthStatus() const
{
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("=== ULM System Health Report ==="));
	
	// Overall system health
	bool bThreadsHealthy = AreThreadsHealthy();
	bool bMemoryHealthy = IsMemoryHealthy();
	bool bSystemHealthy = bThreadsHealthy && bMemoryHealthy;
	
	EULMVerbosity OverallVerbosity = bSystemHealthy ? EULMVerbosity::Message : EULMVerbosity::Warning;
	ULM_LOG(CHANNEL_SUBSYSTEM, OverallVerbosity, 
		TEXT("Overall System Health: %s (Threads: %s, Memory: %s)"), 
		bSystemHealthy ? TEXT("HEALTHY") : TEXT("DEGRADED"),
		bThreadsHealthy ? TEXT("OK") : TEXT("ISSUES"),
		bMemoryHealthy ? TEXT("OK") : TEXT("ISSUES"));
	
	// Detailed subsystem reports
	LogThreadHealthStatus();
	LogMemoryHealthStatus();
	
	// Queue health
	FULMQueueDiagnostics QueueDiag = GetQueueDiagnostics();
	bool bQueueHealthy = QueueDiag.DroppedCount.GetValue() == 0;
	EULMVerbosity QueueVerbosity = bQueueHealthy ? EULMVerbosity::Message : EULMVerbosity::Warning;
	ULM_LOG(CHANNEL_SUBSYSTEM, QueueVerbosity, 
		TEXT("Queue Health: %s - Processed: %d, Dropped: %d"), 
		bQueueHealthy ? TEXT("HEALTHY") : TEXT("DEGRADED"),
		QueueDiag.ProcessedCount.GetValue(), QueueDiag.DroppedCount.GetValue());
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("=== End Health Report ==="));
	
	// Log to performance channel for monitoring
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("System Health Summary: %s"), bSystemHealthy ? TEXT("Healthy") : TEXT("Degraded"));
}

bool UULMSubsystem::IsSystemHealthy() const
{
	return AreThreadsHealthy() && IsMemoryHealthy();
}

