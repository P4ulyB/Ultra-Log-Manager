#include "Logging/ULMLogProcessor.h"
#include "Core/ULMSubsystem.h"
#include "Logging/ULMLogging.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/Event.h"
#include "Misc/DateTime.h"

FULMLogProcessor::FULMLogProcessor(UULMSubsystem* InSubsystem, TQueue<FULMLogQueueEntry, EQueueMode::Spsc>& InQueue)
	: Subsystem(InSubsystem)
	, MessageQueue(InQueue)
	, WakeUpEvent(nullptr)
	, bStopRequested(false)
{
	WakeUpEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FULMLogProcessor::~FULMLogProcessor()
{
	if (WakeUpEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeUpEvent);
		WakeUpEvent = nullptr;
	}
}

bool FULMLogProcessor::Init()
{
	bStopRequested = false;
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Log processor thread initialization complete"));
	return true;
}

uint32 FULMLogProcessor::Run()
{
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Log processor thread started - entering main processing loop"));
	
	int32 TotalProcessedEntries = 0;
	double StartTime = FPlatformTime::Seconds();
	
	while (!bStopRequested)
	{
		ProcessBatch();
		
		if (MessageQueue.IsEmpty())
		{
			WakeUpEvent->Wait(SLEEP_TIME_MS);
		}
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Log processor thread shutdown requested - processing remaining entries..."));
	
	ProcessBatch();
	
	double EndTime = FPlatformTime::Seconds();
	double RuntimeSeconds = EndTime - StartTime;
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
		TEXT("Log processor thread exiting - runtime: %.2f seconds"), 
		RuntimeSeconds);
	
	return 0;
}

void FULMLogProcessor::Stop()
{
	RequestStop();
}


void FULMLogProcessor::RequestStop()
{
	bStopRequested = true;
	WakeUp();
}

void FULMLogProcessor::WakeUp()
{
	if (WakeUpEvent)
	{
		WakeUpEvent->Trigger();
	}
}

void FULMLogProcessor::ProcessBatch()
{
	if (!Subsystem)
	{
		return;
	}
	
	FULMLogQueueEntry Entry;
	int32 ProcessedCount = 0;
	
	// Process entries in batches for better performance
	while (ProcessedCount < BATCH_SIZE && MessageQueue.Dequeue(Entry))
	{
		// Record dequeue time for diagnostics
		double StartTime = FPlatformTime::Seconds();
		
		// Process the entry
		Subsystem->ProcessLogEntry(Entry);
		
		// Update diagnostics through subsystem
		double EndTime = FPlatformTime::Seconds();
		int64 DequeueTimeMicros = (int64)((EndTime - StartTime) * 1000000.0);
		Subsystem->UpdateProcessingDiagnostics(DequeueTimeMicros);
		
		ProcessedCount++;
	}
}