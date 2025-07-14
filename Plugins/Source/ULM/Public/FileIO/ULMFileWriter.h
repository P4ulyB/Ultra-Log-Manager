#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "Channels/ULMChannel.h"
#include "FileIO/ULMFileTypes.h"

// Forward declaration
struct FULMLogEntry;

class UULMSubsystem;

/**
 * Asynchronous file writer with batch processing
 * Handles persistent log file writing with high performance
 */
class ULM_API FULMFileWriter : public FRunnable
{
public:
	FULMFileWriter(UULMSubsystem* InOwner, TQueue<FULMFileWriteEntry, EQueueMode::Spsc>& InWriteQueue);
	virtual ~FULMFileWriter();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

	// Control methods
	void RequestStop();
	void WakeUp();
	
	// Configuration
	void SetBatchSize(int32 NewBatchSize);
	void SetFlushInterval(float NewFlushIntervalSeconds);
	void SetBaseLogPath(const FString& NewBasePath);
	
	// Diagnostics
	FULMFileIODiagnostics GetDiagnostics() const { return Diagnostics; }
	void ResetDiagnostics() { Diagnostics.Reset(); }

private:
	// Thread management
	TAtomic<bool> bStopRequested;
	FEvent* WakeUpEvent;
	
	// Owner reference
	UULMSubsystem* Owner;
	
	// File write queue
	TQueue<FULMFileWriteEntry, EQueueMode::Spsc>& WriteQueue;
	
	// Batch processing
	static constexpr int32 DEFAULT_BATCH_SIZE = 64;
	static constexpr int32 MAX_BATCH_SIZE = 512;
	int32 BatchSize;
	
	// Timing configuration
	float FlushIntervalSeconds;
	double LastFlushTime;
	
	// File management
	FString BaseLogPath;
	TMap<FString, TSharedPtr<FArchive>> OpenFiles;
	mutable FCriticalSection FileMapLock;
	
	// Diagnostics
	FULMFileIODiagnostics Diagnostics;
	
	// Core processing methods
	void ProcessWriteQueue();
	void ProcessBatch(TArray<FULMFileWriteEntry>& Batch);
	void WriteToFile(const FString& FilePath, const FString& Content);
	void FlushAllFiles();
	void CloseAllFiles();
	
	// File management helpers
	TSharedPtr<FArchive> GetOrCreateFile(const FString& FilePath);
	
	// Utility methods
	void UpdateWriteTimeDiagnostics(double StartTime, double EndTime);
	bool ShouldFlush() const;
};