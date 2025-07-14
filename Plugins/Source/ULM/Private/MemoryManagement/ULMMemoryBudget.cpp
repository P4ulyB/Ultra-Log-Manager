#include "MemoryManagement/ULMMemoryBudget.h"
#include "Core/ULMSubsystem.h"
#include "Logging/ULMLogging.h"

SIZE_T FULMMemoryTracker::CalculateLogEntrySize(const FULMLogEntry& Entry) const
{
	SIZE_T Size = 0;
	
	Size += sizeof(FULMLogEntry);
	
	Size += Entry.Message.Len() * sizeof(TCHAR);
	Size += Entry.Channel.Len() * sizeof(TCHAR);
	
	Size += sizeof(FString) * 2;
	
	return Size;
}

void FULMMemoryTracker::AddMemoryUsage(const FString& ChannelName, SIZE_T MemorySize)
{
	TotalMemoryUsedCounter.Add(MemorySize);
	TotalEntriesCounter.Increment();
	
	{
		FScopeLock Lock(&ChannelMemoryLock);
		
		SIZE_T* ExistingUsage = ChannelMemoryUsage.Find(ChannelName);
		if (ExistingUsage)
		{
			*ExistingUsage += MemorySize;
		}
		else
		{
			ChannelMemoryUsage.Add(ChannelName, MemorySize);
		}
		
		UpdateLargestChannel();
	}
}

void FULMMemoryTracker::RemoveMemoryUsage(const FString& ChannelName, SIZE_T MemorySize)
{
	TotalMemoryUsedCounter.Subtract(MemorySize);
	TotalEntriesCounter.Decrement();
	
	{
		FScopeLock Lock(&ChannelMemoryLock);
		
		SIZE_T* ExistingUsage = ChannelMemoryUsage.Find(ChannelName);
		if (ExistingUsage)
		{
			*ExistingUsage = (*ExistingUsage > MemorySize) ? (*ExistingUsage - MemorySize) : 0;
			
			if (*ExistingUsage == 0)
			{
				ChannelMemoryUsage.Remove(ChannelName);
			}
		}
		
		UpdateLargestChannel();
	}
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Memory removed - Channel: %s, Size: %lld bytes, Total: %lld bytes"), 
		*ChannelName, MemorySize, GetTotalMemoryUsage());
}

SIZE_T FULMMemoryTracker::GetChannelMemoryUsage(const FString& ChannelName) const
{
	FScopeLock Lock(&ChannelMemoryLock);
	
	const SIZE_T* Usage = ChannelMemoryUsage.Find(ChannelName);
	return Usage ? *Usage : 0;
}

bool FULMMemoryTracker::WouldExceedBudget(SIZE_T AdditionalMemory) const
{
	SIZE_T CurrentUsage = GetTotalMemoryUsage();
	return (CurrentUsage + AdditionalMemory) > MemoryBudget;
}

FULMMemoryDiagnostics FULMMemoryTracker::ToBlueprint() const
{
	FULMMemoryDiagnostics Result;
	
	Result.TotalMemoryUsed = GetTotalMemoryUsage();
	Result.MemoryBudget = MemoryBudget;
	Result.TotalLogEntries = TotalEntriesCounter.GetValue();
	Result.TrimmingEvents = TrimmingEventsCounter.GetValue();
	
	// Calculate usage percentage
	if (MemoryBudget > 0)
	{
		Result.MemoryUsagePercent = (static_cast<float>(Result.TotalMemoryUsed) / static_cast<float>(MemoryBudget)) * 100.0f;
	}
	
	{
		FScopeLock Lock(&ChannelMemoryLock);
		Result.LargestChannelName = LargestChannelName;
		Result.LargestChannelUsage = LargestChannelUsage;
	}
	
	return Result;
}

void FULMMemoryTracker::Reset()
{
	TotalMemoryUsedCounter.Reset();
	TotalEntriesCounter.Reset();
	TrimmingEventsCounter.Reset();
	
	{
		FScopeLock Lock(&ChannelMemoryLock);
		ChannelMemoryUsage.Empty();
		LargestChannelName = TEXT("None");
		LargestChannelUsage = 0;
	}
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("Memory tracker reset"));
}

void FULMMemoryTracker::UpdateLargestChannel()
{
	// Must be called within ChannelMemoryLock
	
	FString NewLargestName = TEXT("None");
	SIZE_T NewLargestUsage = 0;
	
	for (const auto& ChannelPair : ChannelMemoryUsage)
	{
		if (ChannelPair.Value > NewLargestUsage)
		{
			NewLargestUsage = ChannelPair.Value;
			NewLargestName = ChannelPair.Key;
		}
	}
	
	LargestChannelName = NewLargestName;
	LargestChannelUsage = NewLargestUsage;
}

void FULMMemoryTracker::SetMemoryBudget(SIZE_T NewBudget)
{
	MemoryBudget = NewBudget;
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Memory budget set to %lld bytes (%.2f MB)"), 
		NewBudget, static_cast<float>(NewBudget) / (1024.0f * 1024.0f));
}