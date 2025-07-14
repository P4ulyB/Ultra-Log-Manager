
#include "Logging/ULMLogging.h"
#include "Channels/ULMLogCategories.h"
#include "Core/ULMSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

static TMap<FString, FLogCategoryBase*> DynamicLogCategories;

// Channel-to-category mapping table to replace repetitive if/else chains
static TMap<FString, FLogCategoryBase*> ChannelCategoryMap;
static bool bChannelCategoryMapInitialized = false;

static void InitializeChannelCategoryMap()
{
	if (bChannelCategoryMapInitialized)
	{
		return;
	}
	
	ChannelCategoryMap.Add(TEXT("ULM"), &ULM);
	ChannelCategoryMap.Add(TEXT("Default"), &ULM);
	ChannelCategoryMap.Add(TEXT("Gameplay"), &ULMGameplay);
	ChannelCategoryMap.Add(TEXT("Network"), &ULMNetwork);
	ChannelCategoryMap.Add(TEXT("Performance"), &ULMPerformance);
	ChannelCategoryMap.Add(TEXT("Debug"), &ULMDebug);
	ChannelCategoryMap.Add(TEXT("AI"), &ULMAI);
	ChannelCategoryMap.Add(TEXT("Physics"), &ULMPhysics);
	ChannelCategoryMap.Add(TEXT("Audio"), &ULMAudio);
	ChannelCategoryMap.Add(TEXT("Animation"), &ULMAnimation);
	ChannelCategoryMap.Add(TEXT("UI"), &ULMUI);
	ChannelCategoryMap.Add(TEXT("Subsystem"), &ULMSubsystem);
	
	bChannelCategoryMapInitialized = true;
}
static ELogVerbosity::Type GetUEVerbosity(EULMVerbosity Verbosity)
{
	switch (Verbosity)
	{
		case EULMVerbosity::Message:
			return ELogVerbosity::Log;
		case EULMVerbosity::Warning:
			return ELogVerbosity::Warning;
		case EULMVerbosity::Error:
			return ELogVerbosity::Error;
		case EULMVerbosity::Critical:
			return ELogVerbosity::Error;
		default:
			return ELogVerbosity::Log;
	}
}

static void LogToUECategory(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const char* FileName = nullptr, int32 LineNumber = 0)
{
#if UE_BUILD_SHIPPING
	// In shipping builds, UE disables logging categories (they become FNoLoggingCategory)
	// So we fall back to LogTemp with channel prefixes
	ELogVerbosity::Type UEVerbosity = GetUEVerbosity(Verbosity);
	FString PrefixedMessage = FString::Printf(TEXT("[ULM %s] %s"), *ChannelName, *Message);
	UE_LOG(LogTemp, Log, TEXT("%s"), *PrefixedMessage);
#else
	// Full logging for development builds
	ELogVerbosity::Type UEVerbosity = GetUEVerbosity(Verbosity);
	
	FString FormattedMessage;
	if (Verbosity == EULMVerbosity::Critical)
	{
		FormattedMessage = FString::Printf(TEXT("CRITICAL: %s"), *Message);
	}
	else
	{
		FormattedMessage = Message;
	}
	
	if (FileName && LineNumber > 0)
	{
		FString CleanFileName = FPaths::GetCleanFilename(FString(FileName));
		FormattedMessage += FString::Printf(TEXT(" [%s:%d]"), *CleanFileName, LineNumber);
	}
	
	const char* LogFileName = FileName ? FileName : __FILE__;
	int32 LogLineNumber = LineNumber > 0 ? LineNumber : __LINE__;
	
	// Initialize channel mapping on first use
	InitializeChannelCategoryMap();
	
	// Look up the appropriate log category for this channel
	FLogCategoryBase** FoundCategory = ChannelCategoryMap.Find(ChannelName);
	FLogCategoryBase* CategoryToUse = FoundCategory ? *FoundCategory : &ULM; // Default to ULM category
	
	FMsg::Logf(LogFileName, LogLineNumber, CategoryToUse->GetCategoryName(), UEVerbosity, TEXT("%s"), *FormattedMessage);
#endif
}


void ULMLogCriticalSystem(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const char* FileName, int32 LineNumber)
{
	// Critical system logging - always logs to UE console, bypasses all checks
	// Used for initialization/shutdown when ULM system might not be fully ready
	LogToUECategory(ChannelName, Verbosity, Message, FileName, LineNumber);
	
	// If subsystem is available, also store internally for JSON output
	UULMSubsystem* Subsystem = GULMSubsystem.load(std::memory_order_acquire);
	if (Subsystem)
	{
		Subsystem->StoreLogEntryInternal(Message, ChannelName, Verbosity);
	}
}

void ULMLogMessage(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const UObject* WorldContext, const char* FileName, int32 LineNumber)
{
	// Thread-safe access to global state
	FULMChannelRegistry* Registry = GULMChannelRegistry.load(std::memory_order_acquire);
	UULMSubsystem* Subsystem = GULMSubsystem.load(std::memory_order_acquire);
	
	if (!Registry || !Subsystem)
	{
		UE_LOG(ULM, Warning, TEXT("ULM not initialized, falling back: [%s] %s"), *ChannelName, *Message);
		return;
	}

	if (!Registry->CanChannelLog(ChannelName, Verbosity))
	{
		return;
	}
	
	if (ChannelName == TEXT("ULM") || ChannelName == TEXT("Default"))
	{
		LogToUECategory(ChannelName, Verbosity, Message, FileName, LineNumber);
	}
	else if (ChannelName == TEXT("Subsystem"))
	{
		// SUBSYSTEM channel is isolated - only logs to its own channel, NOT to ULM master
		LogToUECategory(ChannelName, Verbosity, Message, FileName, LineNumber);
	}
	else
	{
		LogToUECategory(ChannelName, Verbosity, Message, FileName, LineNumber);
		
		const FString PrefixedMessage = FString::Printf(TEXT("[%s] %s"), *ChannelName, *Message);
		LogToUECategory(TEXT("ULM"), Verbosity, PrefixedMessage, FileName, LineNumber);
	}

	Subsystem->StoreLogEntryInternal(Message, ChannelName, Verbosity);
}

void ULMLogMessageServer(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const UObject* WorldContext, const char* FileName, int32 LineNumber)
{
	// Only log if we have authority (server or standalone)
	if (ULMInternal::HasNetworkAuthority(WorldContext))
	{
		ULMLogMessage(ChannelName, Verbosity, Message, WorldContext, FileName, LineNumber);
	}
}

void ULMLogMessageClient(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const UObject* WorldContext, const char* FileName, int32 LineNumber)
{
	// Only log if we're a client
	if (!ULMInternal::HasNetworkAuthority(WorldContext))
	{
		ULMLogMessage(ChannelName, Verbosity, Message, WorldContext, FileName, LineNumber);
	}
}

void ULMLogMessageSampled(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, uint32 SampleRate, const UObject* WorldContext, const char* FileName, int32 LineNumber)
{
	// Apply sampling before channel checks for maximum performance
	if (ULMInternal::ShouldSample(ChannelName, SampleRate))
	{
		ULMLogMessage(ChannelName, Verbosity, Message, WorldContext, FileName, LineNumber);
	}
}

void ULMLogDirectToFile(const FString& ChannelName, EULMVerbosity Verbosity, const FString& Message, const char* FileName, int32 LineNumber)
{
#if UE_BUILD_SHIPPING
	// Direct file writing for shipping builds - bypasses UE logging completely
	static bool bInitialized = false;
	static FString LogDirectory;
	
	if (!bInitialized)
	{
		LogDirectory = FPaths::ProjectLogDir() / TEXT("ULM");
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*LogDirectory))
		{
			PlatformFile.CreateDirectoryTree(*LogDirectory);
		}
		bInitialized = true;
	}
	
	// Create simple JSON log entry
	FDateTime CurrentTime = FDateTime::Now(); // System local time
	// Format as local time without timezone offset (since we're using system local time)
	FString Timestamp = FString::Printf(TEXT("%04d-%02d-%02dT%02d:%02d:%02d.%06d"),
		CurrentTime.GetYear(), CurrentTime.GetMonth(), CurrentTime.GetDay(),
		CurrentTime.GetHour(), CurrentTime.GetMinute(), CurrentTime.GetSecond(),
		CurrentTime.GetMillisecond() * 1000);
	
	FString VerbosityStr;
	switch (Verbosity)
	{
		case EULMVerbosity::Message: VerbosityStr = TEXT("Message"); break;
		case EULMVerbosity::Warning: VerbosityStr = TEXT("Warning"); break;
		case EULMVerbosity::Error: VerbosityStr = TEXT("Error"); break;
		case EULMVerbosity::Critical: VerbosityStr = TEXT("Critical"); break;
		default: VerbosityStr = TEXT("Message"); break;
	}
	
	// Simple JSON format
	FString JsonEntry = FString::Printf(TEXT("{\"timestamp\":\"%s\",\"channel\":\"%s\",\"verbosity\":\"%s\",\"message\":\"%s\"}"),
		*Timestamp, *ChannelName, *VerbosityStr, *Message.ReplaceCharWithEscapedChar());
	
	// Write to file
	FString LogFileName = FString::Printf(TEXT("ULM_%s_%04d%02d%02d.json"), 
		*ChannelName, CurrentTime.GetYear(), CurrentTime.GetMonth(), CurrentTime.GetDay());
	FString LogFilePath = LogDirectory / LogFileName;
	
	// Append to file
	FFileHelper::SaveStringToFile(JsonEntry + TEXT("\n"), *LogFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
#endif
}

// Structured logging implementation
FULMStructuredLog::FULMStructuredLog(const FString& InChannelName, EULMVerbosity InVerbosity, const FString& InFileName, int32 InLineNumber, const FString& InFunctionName)
	: ChannelName(InChannelName)
	, Verbosity(InVerbosity)
	, FileName(InFileName)
	, LineNumber(InLineNumber)
	, FunctionName(InFunctionName)
{
	Fields.Reserve(8); // Reserve space for common number of fields
}

FULMStructuredLog::~FULMStructuredLog()
{
	if (!bCommitted)
	{
		Commit();
	}
}

FULMStructuredLog& FULMStructuredLog::Add(const FString& Key, const FString& Value)
{
	Fields.Emplace(Key, Value);
	return *this;
}

FULMStructuredLog& FULMStructuredLog::Add(const FString& Key, int32 Value)
{
	Fields.Emplace(Key, FString::FromInt(Value));
	return *this;
}

FULMStructuredLog& FULMStructuredLog::Add(const FString& Key, float Value)
{
	Fields.Emplace(Key, FString::SanitizeFloat(Value));
	return *this;
}

FULMStructuredLog& FULMStructuredLog::Add(const FString& Key, bool Value)
{
	Fields.Emplace(Key, Value ? TEXT("true") : TEXT("false"));
	return *this;
}

FULMStructuredLog& FULMStructuredLog::Add(const FString& Key, const FVector& Value)
{
	Fields.Emplace(Key, Value.ToString());
	return *this;
}

FULMStructuredLog& FULMStructuredLog::Add(const FString& Key, const FRotator& Value)
{
	Fields.Emplace(Key, Value.ToString());
	return *this;
}

void FULMStructuredLog::Commit()
{
	if (bCommitted)
	{
		return;
	}

	// Build structured message
	FString StructuredMessage;
	StructuredMessage.Reserve(256); // Reserve reasonable space

	for (const auto& Field : Fields)
	{
		if (!StructuredMessage.IsEmpty())
		{
			StructuredMessage += TEXT(", ");
		}
		StructuredMessage += FString::Printf(TEXT("%s=%s"), *Field.Key, *Field.Value);
	}

	// Add function information to structured message (file/line will be passed to FMsg::Logf for Rider clickability)
	FString FinalMessage;
	if (!FunctionName.IsEmpty())
	{
		FinalMessage = FString::Printf(TEXT("%s: %s"), *FunctionName, *StructuredMessage);
	}
	else
	{
		FinalMessage = StructuredMessage;
	}
	
	// Log the structured message
	// Convert FString file path to char* for the API
	const char* FileNameCStr = FileName.IsEmpty() ? __FILE__ : TCHAR_TO_ANSI(*FileName);
	int32 LogLineNumber = LineNumber > 0 ? LineNumber : __LINE__;
	ULMLogMessage(ChannelName, Verbosity, FinalMessage, nullptr, FileNameCStr, LogLineNumber);
	
	bCommitted = true;
}