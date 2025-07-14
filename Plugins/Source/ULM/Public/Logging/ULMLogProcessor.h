#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/Event.h"
#include "Containers/Queue.h"

// Forward declarations
class UULMSubsystem;
struct FULMLogQueueEntry;

/**
 * Background thread processor for ULM log entries
 * Consumes entries from the lock-free queue and processes them
 */
class FULMLogProcessor : public FRunnable
{
public:
	FULMLogProcessor(UULMSubsystem* InSubsystem, TQueue<FULMLogQueueEntry, EQueueMode::Spsc>& InQueue);
	virtual ~FULMLogProcessor();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

	// Control methods
	void RequestStop();
	void WakeUp();

private:
	UULMSubsystem* Subsystem;
	TQueue<FULMLogQueueEntry, EQueueMode::Spsc>& MessageQueue;
	FEvent* WakeUpEvent;
	bool bStopRequested;
	
	// Processing batch size for efficiency
	static constexpr int32 BATCH_SIZE = 64;
	
	// Sleep time when queue is empty (milliseconds)
	static constexpr int32 SLEEP_TIME_MS = 1;
	
	void ProcessBatch();
};