#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "ULMMemoryBudget.generated.h"

/**
 * Memory budget diagnostics for Blueprint access
 */
USTRUCT(BlueprintType)
struct ULM_API FULMMemoryDiagnostics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Memory Diagnostics")
	int64 TotalMemoryUsed;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Diagnostics")
	int64 MemoryBudget;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Diagnostics")
	int64 LargestChannelUsage;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Diagnostics")
	FString LargestChannelName;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Diagnostics")
	int32 TotalLogEntries;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Diagnostics")
	int32 TrimmingEvents;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Diagnostics")
	float MemoryUsagePercent;

	FULMMemoryDiagnostics()
		: TotalMemoryUsed(0)
		, MemoryBudget(52428800) // 50MB default
		, LargestChannelUsage(0)
		, LargestChannelName(TEXT("None"))
		, TotalLogEntries(0)
		, TrimmingEvents(0)
		, MemoryUsagePercent(0.0f)
	{}
};

/**
 * Internal memory tracking with thread-safe counters
 */
struct ULM_API FULMMemoryTracker
{
	// Thread-safe counters for real-time tracking
	FThreadSafeCounter64 TotalMemoryUsedCounter;
	FThreadSafeCounter TotalEntriesCounter;
	FThreadSafeCounter TrimmingEventsCounter;
	
	// Configuration
	SIZE_T MemoryBudget;
	
	// Per-channel tracking (protected by critical section)
	mutable FCriticalSection ChannelMemoryLock;
	TMap<FString, SIZE_T> ChannelMemoryUsage;
	
	// Largest channel tracking
	FString LargestChannelName;
	SIZE_T LargestChannelUsage;
	
	FULMMemoryTracker()
		: MemoryBudget(52428800) // 50MB default
		, LargestChannelName(TEXT("None"))
		, LargestChannelUsage(0)
	{}
	
	/**
	 * Calculate memory footprint of a log entry
	 */
	SIZE_T CalculateLogEntrySize(const struct FULMLogEntry& Entry) const;
	
	/**
	 * Add memory usage for a channel
	 */
	void AddMemoryUsage(const FString& ChannelName, SIZE_T MemorySize);
	
	/**
	 * Remove memory usage for a channel
	 */
	void RemoveMemoryUsage(const FString& ChannelName, SIZE_T MemorySize);
	
	/**
	 * Get current memory usage for a specific channel
	 */
	SIZE_T GetChannelMemoryUsage(const FString& ChannelName) const;
	
	/**
	 * Check if adding memory would exceed budget
	 */
	bool WouldExceedBudget(SIZE_T AdditionalMemory) const;
	
	/**
	 * Get current total memory usage
	 */
	SIZE_T GetTotalMemoryUsage() const { return TotalMemoryUsedCounter.GetValue(); }
	
	/**
	 * Convert to Blueprint-friendly diagnostics
	 */
	FULMMemoryDiagnostics ToBlueprint() const;
	
	/**
	 * Reset all tracking data
	 */
	void Reset();
	
	/**
	 * Update largest channel tracking
	 */
	void UpdateLargestChannel();
	
	/**
	 * Set memory budget
	 */
	void SetMemoryBudget(SIZE_T NewBudget);
	
	/**
	 * Get memory budget
	 */
	SIZE_T GetMemoryBudget() const { return MemoryBudget; }
};