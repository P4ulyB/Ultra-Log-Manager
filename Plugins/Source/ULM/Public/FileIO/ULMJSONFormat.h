#pragma once

#include "CoreMinimal.h"
#include "Channels/ULMChannel.h"
#include "HAL/PlatformFilemanager.h"
#include "ULMJSONFormat.generated.h"

/**
 * Log format types supported by ULM
 */
UENUM(BlueprintType)
enum class EULMLogFormat : uint8
{
	/** Human-readable text format: [Timestamp] [Channel] [Level] [ThreadID] Message */
	Text		UMETA(DisplayName = "Text Format"),
	
	/** Structured JSON format with Brisbane (UTC+10) timestamps and extended metadata */
	JSON		UMETA(DisplayName = "JSON Format"),
	
	/** Both text and JSON formats (dual output) */
	Both		UMETA(DisplayName = "Both Formats")
};

/**
 * JSON log format configuration
 */
USTRUCT(BlueprintType)
struct ULM_API FULMJSONConfig
{
	GENERATED_BODY()

	/** Include session ID in JSON logs */
	UPROPERTY(BlueprintReadWrite, Category = "JSON Config")
	bool bIncludeSessionId;

	/** Include build version in JSON logs */
	UPROPERTY(BlueprintReadWrite, Category = "JSON Config")
	bool bIncludeBuildVersion;

	/** Include file and line information */
	UPROPERTY(BlueprintReadWrite, Category = "JSON Config")
	bool bIncludeSourceLocation;

	/** Use compact JSON format (no pretty printing) */
	UPROPERTY(BlueprintReadWrite, Category = "JSON Config")
	bool bCompactFormat;

	/** Custom fields to include in JSON logs */
	UPROPERTY(BlueprintReadWrite, Category = "JSON Config")
	TMap<FString, FString> CustomFields;

	FULMJSONConfig()
		: bIncludeSessionId(true)
		, bIncludeBuildVersion(true)
		, bIncludeSourceLocation(false)
		, bCompactFormat(true)
	{}
};

/**
 * JSON log formatter for ULM system
 * Provides structured JSON output with Brisbane (UTC+10) timestamps and configurable metadata
 */
class ULM_API FULMJSONFormatter
{
public:
	FULMJSONFormatter();
	
	/**
	 * Format a log entry as JSON string
	 */
	FString FormatAsJSON(const struct FULMLogEntry& Entry, const FULMJSONConfig& Config = FULMJSONConfig()) const;
	
	
	/**
	 * Get current local timestamp in ISO 8601 format with microseconds
	 */
	static FString GetLocalTimestamp();
	
	/**
	 * Get local timestamp from FDateTime in ISO 8601 format with microseconds
	 */
	static FString GetLocalTimestamp(const FDateTime& DateTime);
	
	/**
	 * Convert verbosity to JSON log level string
	 */
	static FString VerbosityToJSONLevel(EULMVerbosity Verbosity);
	
	
	/**
	 * Escape JSON string for safe embedding
	 */
	static FString EscapeJSONString(const FString& Input);
	
	/**
	 * Generate session ID for this logging session
	 */
	static FString GenerateSessionId();

private:
	/** Static session ID for this process instance */
	static FString SessionId;
	
	/** Static build version string */
	static FString BuildVersion;
	
	/** Initialize static data */
	static void InitializeStaticData();
};

/**
 * Log format diagnostics for Blueprint access
 */
USTRUCT(BlueprintType)
struct ULM_API FULMFormatDiagnostics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	int32 JSONLogsWritten;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	int32 TextLogsWritten;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	int32 FormatErrors;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	float AvgJSONFormatTimeMicros;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	float AvgTextFormatTimeMicros;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	FString CurrentFormat;

	// Additional diagnostics members
	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	int32 TotalFormatOperations;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	double TotalFormatTimeMicros;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	double AverageFormatTimeMicros;

	UPROPERTY(BlueprintReadOnly, Category = "Format Diagnostics")
	double MaxFormatTimeMicros;

	FULMFormatDiagnostics()
		: JSONLogsWritten(0)
		, TextLogsWritten(0)
		, FormatErrors(0)
		, AvgJSONFormatTimeMicros(0.0f)
		, AvgTextFormatTimeMicros(0.0f)
		, CurrentFormat(TEXT("Text"))
		, TotalFormatOperations(0)
		, TotalFormatTimeMicros(0.0)
		, AverageFormatTimeMicros(0.0)
		, MaxFormatTimeMicros(0.0)
	{}
};