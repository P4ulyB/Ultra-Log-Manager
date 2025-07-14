#include "Channels/ULMChannel.h"
#include "HAL/PlatformFilemanager.h"

bool FULMChannelState::CanLog(EULMVerbosity Verbosity, double CurrentTime)
{
	if (!bEffectiveEnabled || Verbosity < EffectiveMinVerbosity)
	{
		return false;
	}

	FScopeLock Lock(&StateLock);
	
	RefillTokens(CurrentTime);
	
	if (CurrentTokens >= 1.0f)
	{
		CurrentTokens -= 1.0f;
		return true;
	}
	
	return false;
}

void FULMChannelState::RefillTokens(double CurrentTime)
{
	if (LastRefillTime == 0.0)
	{
		LastRefillTime = CurrentTime;
		CurrentTokens = static_cast<float>(EffectiveRateLimit.BurstCapacity);
		return;
	}

	const double DeltaTime = CurrentTime - LastRefillTime;
	if (DeltaTime > 0.0)
	{
		const float TokensToAdd = static_cast<float>(DeltaTime * EffectiveRateLimit.TokensPerSecond);
		CurrentTokens = FMath::Min(CurrentTokens + TokensToAdd, static_cast<float>(EffectiveRateLimit.BurstCapacity));
		LastRefillTime = CurrentTime;
	}
}

void FULMChannelState::UpdateEffectiveSettings(const FULMChannelConfig& Config, const FULMChannelState* ParentState)
{
	FScopeLock Lock(&StateLock);

	if (Config.bInheritFromParent && ParentState)
	{
		bEffectiveEnabled = ParentState->bEffectiveEnabled && Config.bEnabled;
		EffectiveMinVerbosity = FMath::Max(ParentState->EffectiveMinVerbosity, Config.MinVerbosity);
		EffectiveColor = Config.DisplayColor != FLinearColor::White ? Config.DisplayColor : ParentState->EffectiveColor;
		EffectiveRateLimit = Config.RateLimit.TokensPerSecond > 0 ? Config.RateLimit : ParentState->EffectiveRateLimit;
		EffectiveMaxEntries = Config.MaxLogEntries;
	}
	else
	{
		bEffectiveEnabled = Config.bEnabled;
		EffectiveMinVerbosity = Config.MinVerbosity;
		EffectiveColor = Config.DisplayColor;
		EffectiveRateLimit = Config.RateLimit;
		EffectiveMaxEntries = Config.MaxLogEntries;
	}
}

FULMChannelRegistry::FULMChannelRegistry()
{
	RegisterChannel(TEXT("Default"));
}

FULMChannelRegistry::~FULMChannelRegistry()
{
	FWriteScopeLock WriteLock(RegistryLock);
	ChannelStates.Empty();
	ChannelConfigs.Empty();
}

void FULMChannelRegistry::RegisterChannel(const FString& ChannelName, const FULMChannelConfig& Config)
{
	if (ChannelName.IsEmpty())
	{
		return;
	}

	FWriteScopeLock WriteLock(RegistryLock);

	FString ParentName, LocalName;
	ParseChannelHierarchy(ChannelName, ParentName, LocalName);

	if (!ParentName.IsEmpty() && !ChannelConfigs.Contains(ParentName))
	{
		RegisterChannel(ParentName, DefaultConfig);
	}

	ChannelConfigs.Emplace(ChannelName, Config);

	TUniquePtr<FULMChannelState> NewState = MakeUnique<FULMChannelState>();
	NewState->ParentChannel = ParentName;

	const FULMChannelState* ParentState = nullptr;
	if (!ParentName.IsEmpty())
	{
		if (const TUniquePtr<FULMChannelState>* ParentPtr = ChannelStates.Find(ParentName))
		{
			ParentState = ParentPtr->Get();
		}
	}
	NewState->UpdateEffectiveSettings(Config, ParentState);

	if (!ParentName.IsEmpty())
	{
		if (TUniquePtr<FULMChannelState>* ParentPtr = ChannelStates.Find(ParentName))
		{
			if (FULMChannelState* Parent = ParentPtr->Get())
			{
				Parent->ChildChannels.AddUnique(ChannelName);
			}
		}
	}

	ChannelStates.Emplace(ChannelName, MoveTemp(NewState));

	UpdateChildChannels(ChannelName);
}

void FULMChannelRegistry::UnregisterChannel(const FString& ChannelName)
{
	if (ChannelName == TEXT("Default"))
	{
		return;
	}

	FWriteScopeLock WriteLock(RegistryLock);

	if (TUniquePtr<FULMChannelState>* StatePtr = ChannelStates.Find(ChannelName))
	{
		FULMChannelState* State = StatePtr->Get();
		if (State)
		{
			if (!State->ParentChannel.IsEmpty())
			{
				if (TUniquePtr<FULMChannelState>* ParentPtr = ChannelStates.Find(State->ParentChannel))
				{
					if (FULMChannelState* ParentState = ParentPtr->Get())
					{
						ParentState->ChildChannels.Remove(ChannelName);
					}
				}
			}

			for (const FString& ChildChannel : State->ChildChannels)
			{
				if (TUniquePtr<FULMChannelState>* ChildPtr = ChannelStates.Find(ChildChannel))
				{
					if (FULMChannelState* ChildState = ChildPtr->Get())
					{
						ChildState->ParentChannel = State->ParentChannel;
						if (!State->ParentChannel.IsEmpty())
						{
							if (TUniquePtr<FULMChannelState>* ParentPtr = ChannelStates.Find(State->ParentChannel))
							{
								if (FULMChannelState* ParentState = ParentPtr->Get())
								{
									ParentState->ChildChannels.AddUnique(ChildChannel);
								}
							}
						}
					}
				}
			}
		}
	}

	ChannelConfigs.Remove(ChannelName);
	ChannelStates.Remove(ChannelName);
}

bool FULMChannelRegistry::IsChannelRegistered(const FString& ChannelName) const
{
	FReadScopeLock ReadLock(RegistryLock);
	return ChannelStates.Contains(ChannelName);
}


const FULMChannelState* FULMChannelRegistry::GetChannelState(const FString& ChannelName) const
{
	FReadScopeLock ReadLock(RegistryLock);
	if (const TUniquePtr<FULMChannelState>* StatePtr = ChannelStates.Find(ChannelName))
	{
		return StatePtr->Get();
	}
	return nullptr;
}

bool FULMChannelRegistry::CanChannelLog(const FString& ChannelName, EULMVerbosity Verbosity) const
{
	const FULMChannelState* State = GetChannelState(ChannelName);
	if (!State)
	{
		return false;
	}

	const double CurrentTime = FPlatformTime::Seconds();
	return const_cast<FULMChannelState*>(State)->CanLog(Verbosity, CurrentTime);
}

void FULMChannelRegistry::UpdateChannelConfig(const FString& ChannelName, const FULMChannelConfig& Config)
{
	FWriteScopeLock WriteLock(RegistryLock);

	if (ChannelConfigs.Contains(ChannelName))
	{
		ChannelConfigs[ChannelName] = Config;
		RebuildEffectiveSettings(ChannelName);
	}
}

FULMChannelConfig FULMChannelRegistry::GetChannelConfig(const FString& ChannelName) const
{
	FReadScopeLock ReadLock(RegistryLock);
	
	if (const FULMChannelConfig* Config = ChannelConfigs.Find(ChannelName))
	{
		return *Config;
	}
	
	return DefaultConfig;
}

TArray<FString> FULMChannelRegistry::GetAllChannels() const
{
	FReadScopeLock ReadLock(RegistryLock);
	
	TArray<FString> Result;
	Result.Reserve(ChannelStates.Num());
	ChannelStates.GenerateKeyArray(Result);
	
	return Result;
}

TArray<FString> FULMChannelRegistry::GetChildChannels(const FString& ParentChannel) const
{
	FReadScopeLock ReadLock(RegistryLock);
	
	if (const TUniquePtr<FULMChannelState>* StatePtr = ChannelStates.Find(ParentChannel))
	{
		if (const FULMChannelState* State = StatePtr->Get())
		{
			return State->ChildChannels;
		}
	}
	
	return TArray<FString>();
}

FString FULMChannelRegistry::GetParentChannel(const FString& ChannelName) const
{
	FReadScopeLock ReadLock(RegistryLock);
	
	if (const TUniquePtr<FULMChannelState>* StatePtr = ChannelStates.Find(ChannelName))
	{
		if (const FULMChannelState* State = StatePtr->Get())
		{
			return State->ParentChannel;
		}
	}
	
	return FString();
}

void FULMChannelRegistry::SetChannelEnabled(const FString& ChannelName, bool bEnabled, bool bRecursive)
{
	FWriteScopeLock WriteLock(RegistryLock);

	if (FULMChannelConfig* Config = ChannelConfigs.Find(ChannelName))
	{
		Config->bEnabled = bEnabled;
		RebuildEffectiveSettings(ChannelName);

		if (bRecursive)
		{
			if (const TUniquePtr<FULMChannelState>* StatePtr = ChannelStates.Find(ChannelName))
			{
				if (const FULMChannelState* State = StatePtr->Get())
				{
					for (const FString& ChildChannel : State->ChildChannels)
					{
						SetChannelEnabled(ChildChannel, bEnabled, true);
					}
				}
			}
		}
	}
}

void FULMChannelRegistry::SetChannelVerbosity(const FString& ChannelName, EULMVerbosity MinVerbosity, bool bRecursive)
{
	FWriteScopeLock WriteLock(RegistryLock);

	if (FULMChannelConfig* Config = ChannelConfigs.Find(ChannelName))
	{
		Config->MinVerbosity = MinVerbosity;
		RebuildEffectiveSettings(ChannelName);

		if (bRecursive)
		{
			if (const TUniquePtr<FULMChannelState>* StatePtr = ChannelStates.Find(ChannelName))
			{
				if (const FULMChannelState* State = StatePtr->Get())
				{
					for (const FString& ChildChannel : State->ChildChannels)
					{
						SetChannelVerbosity(ChildChannel, MinVerbosity, true);
					}
				}
			}
		}
	}
}

void FULMChannelRegistry::ParseChannelHierarchy(const FString& ChannelName, FString& OutParent, FString& OutName) const
{
	int32 LastDotIndex;
	if (ChannelName.FindLastChar(TEXT('.'), LastDotIndex))
	{
		OutParent = ChannelName.Left(LastDotIndex);
		OutName = ChannelName.Mid(LastDotIndex + 1);
	}
	else
	{
		OutParent.Empty();
		OutName = ChannelName;
	}
}

void FULMChannelRegistry::UpdateChildChannels(const FString& ParentChannel)
{
	if (const TUniquePtr<FULMChannelState>* ParentPtr = ChannelStates.Find(ParentChannel))
	{
		const FULMChannelState* ParentState = ParentPtr->Get();
		if (ParentState)
	{
		const FULMChannelConfig* ParentConfig = ChannelConfigs.Find(ParentChannel);
		if (!ParentConfig)
		{
			return;
		}

		for (const FString& ChildChannel : ParentState->ChildChannels)
		{
			RebuildEffectiveSettings(ChildChannel);
		}
		}
	}
}

void FULMChannelRegistry::RebuildEffectiveSettings(const FString& ChannelName)
{
	TUniquePtr<FULMChannelState>* StatePtr = ChannelStates.Find(ChannelName);
	const FULMChannelConfig* Config = ChannelConfigs.Find(ChannelName);
	
	if (!StatePtr || !StatePtr->Get() || !Config)
	{
		return;
	}

	FULMChannelState* State = StatePtr->Get();
	const FULMChannelState* ParentState = nullptr;
	if (!State->ParentChannel.IsEmpty())
	{
		if (const TUniquePtr<FULMChannelState>* ParentPtr = ChannelStates.Find(State->ParentChannel))
		{
			ParentState = ParentPtr->Get();
		}
	}

	State->UpdateEffectiveSettings(*Config, ParentState);

	for (const FString& ChildChannel : State->ChildChannels)
	{
		RebuildEffectiveSettings(ChildChannel);
	}
}