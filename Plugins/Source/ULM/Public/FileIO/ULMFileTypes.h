#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"

/**
 * Structure for file write operations
 */
struct ULM_API FULMFileWriteEntry
{
	FString LogLine;
	FString FilePath;
	double Timestamp;
	
	FULMFileWriteEntry() = default;
	FULMFileWriteEntry(const FString& InLogLine, const FString& InFilePath, double InTimestamp)
		: LogLine(InLogLine), FilePath(InFilePath), Timestamp(InTimestamp) {}
};

/**
 * File I/O diagnostics for monitoring write performance
 */
struct ULM_API FULMFileIODiagnostics
{
	FThreadSafeCounter WriteCount;
	FThreadSafeCounter BatchCount;
	FThreadSafeCounter FailedWrites;
	FThreadSafeCounter TotalBytesWritten;
	FThreadSafeCounter TotalWriteTime;  // In microseconds
	
	void Reset()
	{
		WriteCount.Reset();
		BatchCount.Reset();
		FailedWrites.Reset();
		TotalBytesWritten.Reset();
		TotalWriteTime.Reset();
	}
};