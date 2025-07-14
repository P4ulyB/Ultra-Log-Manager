#include "FileIO/ULMJSONFormat.h"
#include "Core/ULMSubsystem.h"
#include "Logging/ULMLogging.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "HAL/PlatformFilemanager.h"

// Initialize static members
FString FULMJSONFormatter::SessionId;
FString FULMJSONFormatter::BuildVersion;

FULMJSONFormatter::FULMJSONFormatter()
{
	InitializeStaticData();
}

void FULMJSONFormatter::InitializeStaticData()
{
	static bool bInitialized = false;
	if (!bInitialized)
	{
		SessionId = GenerateSessionId();
		BuildVersion = FApp::GetBuildVersion();
		bInitialized = true;
		
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
			TEXT("JSON formatter initialized - Session: %s, Build: %s"), 
			*SessionId, *BuildVersion);
	}
}

FString FULMJSONFormatter::FormatAsJSON(const FULMLogEntry& Entry, const FULMJSONConfig& Config) const
{
	double StartTime = FPlatformTime::Seconds();
	
	FString JSONLog;
	
	if (Config.bCompactFormat)
	{
		JSONLog = TEXT("{");
		JSONLog += FString::Printf(TEXT("\"timestamp\":\"%s\","), *GetLocalTimestamp(Entry.Timestamp));
		JSONLog += FString::Printf(TEXT("\"channel\":\"%s\","), *Entry.Channel);
		JSONLog += FString::Printf(TEXT("\"level\":\"%s\","), *VerbosityToJSONLevel(Entry.Verbosity));
		JSONLog += FString::Printf(TEXT("\"thread_id\":\"%08X\","), Entry.ThreadId);
		JSONLog += FString::Printf(TEXT("\"message\":\"%s\""), *EscapeJSONString(Entry.Message));
		
		if (Config.bIncludeSessionId)
		{
			JSONLog += FString::Printf(TEXT(",\"session_id\":\"%s\""), *SessionId);
		}
		
		if (Config.bIncludeBuildVersion)
		{
			JSONLog += FString::Printf(TEXT(",\"build_version\":\"%s\""), *BuildVersion);
		}
		
		for (const auto& CustomField : Config.CustomFields)
		{
			JSONLog += FString::Printf(TEXT(",\"%s\":\"%s\""), 
				*EscapeJSONString(CustomField.Key), 
				*EscapeJSONString(CustomField.Value));
		}
		
		JSONLog += TEXT("}");
	}
	else
	{
		JSONLog = TEXT("{\n");
		JSONLog += FString::Printf(TEXT("  \"timestamp\": \"%s\",\n"), *GetLocalTimestamp(Entry.Timestamp));
		JSONLog += FString::Printf(TEXT("  \"channel\": \"%s\",\n"), *Entry.Channel);
		JSONLog += FString::Printf(TEXT("  \"level\": \"%s\",\n"), *VerbosityToJSONLevel(Entry.Verbosity));
		JSONLog += FString::Printf(TEXT("  \"thread_id\": \"%08X\",\n"), Entry.ThreadId);
		JSONLog += FString::Printf(TEXT("  \"message\": \"%s\""), *EscapeJSONString(Entry.Message));
		
		if (Config.bIncludeSessionId)
		{
			JSONLog += FString::Printf(TEXT(",\n  \"session_id\": \"%s\""), *SessionId);
		}
		
		if (Config.bIncludeBuildVersion)
		{
			JSONLog += FString::Printf(TEXT(",\n  \"build_version\": \"%s\""), *BuildVersion);
		}
		
		for (const auto& CustomField : Config.CustomFields)
		{
			JSONLog += FString::Printf(TEXT(",\n  \"%s\": \"%s\""), 
				*EscapeJSONString(CustomField.Key), 
				*EscapeJSONString(CustomField.Value));
		}
		
		JSONLog += TEXT("\n}");
	}
	
	double EndTime = FPlatformTime::Seconds();
	float FormatTimeMicros = (EndTime - StartTime) * 1000000.0f;
	
	/*ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("JSON format completed in %.2f μs"), FormatTimeMicros);*/
	
	return JSONLog;
}


FString FULMJSONFormatter::GetLocalTimestamp()
{
	return GetLocalTimestamp(FDateTime::Now());
}

FString FULMJSONFormatter::GetLocalTimestamp(const FDateTime& DateTime)
{
	// Use system local time instead of hardcoded timezone
	FDateTime LocalTime = FDateTime::Now();
	
	// Get microseconds from the fractional part
	int64 Ticks = LocalTime.GetTicks();
	int32 Microseconds = (Ticks % ETimespan::TicksPerSecond) / ETimespan::TicksPerMicrosecond;
	
	// Format as local time without timezone offset (system local time)
	return FString::Printf(TEXT("%04d-%02d-%02dT%02d:%02d:%02d.%06d"),
		LocalTime.GetYear(),
		LocalTime.GetMonth(),
		LocalTime.GetDay(),
		LocalTime.GetHour(),
		LocalTime.GetMinute(),
		LocalTime.GetSecond(),
		Microseconds
	);
}

FString FULMJSONFormatter::VerbosityToJSONLevel(EULMVerbosity Verbosity)
{
	switch (Verbosity)
	{
		case EULMVerbosity::Message:  return TEXT("INFO");
		case EULMVerbosity::Warning:  return TEXT("WARN");
		case EULMVerbosity::Error:    return TEXT("ERROR");
		case EULMVerbosity::Critical: return TEXT("CRITICAL");
		default: return TEXT("INFO");
	}
}


FString FULMJSONFormatter::EscapeJSONString(const FString& Input)
{
	FString Output = Input;
	
	// Escape JSON special characters
	Output = Output.Replace(TEXT("\\"), TEXT("\\\\"));  // Backslash
	Output = Output.Replace(TEXT("\""), TEXT("\\\""));  // Quote
	Output = Output.Replace(TEXT("\n"), TEXT("\\n"));   // Newline
	Output = Output.Replace(TEXT("\r"), TEXT("\\r"));   // Carriage return
	Output = Output.Replace(TEXT("\t"), TEXT("\\t"));   // Tab
	Output = Output.Replace(TEXT("\b"), TEXT("\\b"));   // Backspace
	Output = Output.Replace(TEXT("\f"), TEXT("\\f"));   // Form feed
	
	return Output;
}

FString FULMJSONFormatter::GenerateSessionId()
{
	// Generate a unique session ID for this logging session
	FGuid SessionGuid = FGuid::NewGuid();
	return SessionGuid.ToString(EGuidFormats::DigitsWithHyphens);
}