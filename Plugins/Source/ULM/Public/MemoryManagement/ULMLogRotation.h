#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Misc/DateTime.h"
#include "ULMLogRotation.generated.h"

// Forward declarations
class UULMSubsystem;

/**
 * Configuration for log rotation and retention policies
 */
USTRUCT(BlueprintType)
struct ULM_API FULMRotationConfig
{
	GENERATED_BODY()

	// Maximum file size in bytes before rotation (default: 100MB)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Log Rotation")
	int64 MaxFileSizeBytes;

	// Number of days to retain log files (default: 7 days)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Log Rotation")
	int32 RetentionDays;

	// Maximum number of files per channel per day (default: 10)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Log Rotation")
	int32 MaxFilesPerDay;

	// Whether to enable automatic cleanup on startup (default: true)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Log Rotation")
	bool bAutoCleanupOnStartup;

	// Whether to enable periodic cleanup (default: true)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Log Rotation")
	bool bPeriodicCleanup;

	// Cleanup interval in hours (default: 24 hours)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Log Rotation")
	float CleanupIntervalHours;

	FULMRotationConfig()
		: MaxFileSizeBytes(104857600)  // 100MB
		, RetentionDays(7)
		, MaxFilesPerDay(10)
		, bAutoCleanupOnStartup(true)
		, bPeriodicCleanup(true)
		, CleanupIntervalHours(24.0f)
	{}
};

/**
 * Diagnostics for log rotation and retention
 */
USTRUCT(BlueprintType)
struct ULM_API FULMRotationDiagnostics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Rotation Diagnostics")
	int32 TotalRotations;

	UPROPERTY(BlueprintReadOnly, Category = "Rotation Diagnostics")
	int32 FilesDeleted;

	UPROPERTY(BlueprintReadOnly, Category = "Rotation Diagnostics")
	int64 BytesFreed;

	UPROPERTY(BlueprintReadOnly, Category = "Rotation Diagnostics")
	FDateTime LastCleanupTime;

	UPROPERTY(BlueprintReadOnly, Category = "Rotation Diagnostics")
	int32 ActiveFiles;

	UPROPERTY(BlueprintReadOnly, Category = "Rotation Diagnostics")
	int64 TotalDiskUsage;

	FULMRotationDiagnostics()
		: TotalRotations(0)
		, FilesDeleted(0)
		, BytesFreed(0)
		, LastCleanupTime(FDateTime::MinValue())
		, ActiveFiles(0)
		, TotalDiskUsage(0)
	{}
};

/**
 * Information about a log file
 */
struct ULM_API FULMLogFileInfo
{
	FString FilePath;
	FString ChannelName;
	FDateTime CreationDate;
	int64 FileSize;
	int32 FileIndex;  // For same-day rotation files
	bool bIsActive;

	FULMLogFileInfo()
		: FileSize(0)
		, FileIndex(1)
		, bIsActive(false)
	{}

	FULMLogFileInfo(const FString& InFilePath, const FString& InChannelName, const FDateTime& InDate, int32 InIndex = 1)
		: FilePath(InFilePath)
		, ChannelName(InChannelName)
		, CreationDate(InDate)
		, FileSize(0)
		, FileIndex(InIndex)
		, bIsActive(false)
	{}
};

/**
 * Manages log file tracking and metadata
 */
class ULM_API FULMLogFileTracker
{
public:
	FULMLogFileTracker();
	~FULMLogFileTracker();

	// File tracking
	void RegisterFile(const FString& ChannelName, const FString& FilePath, const FDateTime& CreationDate, int32 FileIndex = 1);
	void UpdateFileSize(const FString& ChannelName, int64 NewSize);
	FULMLogFileInfo* GetActiveFile(const FString& ChannelName);
	const FULMLogFileInfo* GetActiveFile(const FString& ChannelName) const;
	TArray<FULMLogFileInfo> GetAllFiles(const FString& ChannelName = TEXT("")) const;

	// File operations
	bool ShouldRotateFile(const FString& ChannelName, int64 MaxSize) const;
	FString GenerateRotatedFilePath(const FString& ChannelName, const FString& BaseLogPath) const;
	void SetFileActive(const FString& ChannelName, const FString& FilePath);

	// Cleanup
	void RemoveFile(const FString& FilePath);
	void Clear();
	
	// Public access to parsing function
	bool ParseLogFileName(const FString& FileName, FString& OutChannel, FDateTime& OutDate, int32& OutIndex) const;

private:
	// Map of channel name to array of file info
	TMap<FString, TArray<FULMLogFileInfo>> ChannelFiles;
	mutable FCriticalSection FileTrackingLock;

	void ScanExistingFiles(const FString& BaseLogPath);
};

/**
 * Handles log file rotation based on size and time
 */
class ULM_API FULMLogRotator
{
public:
	FULMLogRotator(UULMSubsystem* InOwner);
	~FULMLogRotator();

	// Configuration
	void SetRotationConfig(const FULMRotationConfig& Config);
	FULMRotationConfig GetRotationConfig() const;

	// Rotation operations
	bool ShouldRotateFile(const FString& ChannelName, const FString& FilePath) const;
	FString RotateFile(const FString& ChannelName, const FString& CurrentFilePath);
	void UpdateFileSize(const FString& ChannelName, int64 NewSize);

	// File management
	FString GetActiveFilePath(const FString& ChannelName, const FString& BaseLogPath) const;
	void RegisterNewFile(const FString& ChannelName, const FString& FilePath);

	// Diagnostics
	FULMRotationDiagnostics GetDiagnostics() const;
	void ResetDiagnostics();

private:
	UULMSubsystem* Owner;
	FULMRotationConfig Config;
	FULMLogFileTracker FileTracker;
	mutable FULMRotationDiagnostics Diagnostics;
	mutable FCriticalSection RotationLock;

	void IncrementRotationCount();
	bool IsValidRotationConfig() const;
};

/**
 * Manages log file retention and cleanup policies
 */
class ULM_API FULMRetentionManager
{
public:
	FULMRetentionManager(UULMSubsystem* InOwner);
	~FULMRetentionManager();

	// Retention operations
	void PerformCleanup(const FString& BaseLogPath);
	void SchedulePeriodicCleanup();
	void SetRetentionConfig(const FULMRotationConfig& Config);

	// File operations
	TArray<FString> GetExpiredFiles(const FString& BaseLogPath) const;
	bool DeleteExpiredFiles(const TArray<FString>& FilesToDelete);
	int64 CalculateDiskUsage(const FString& BaseLogPath) const;

	// Diagnostics
	FULMRotationDiagnostics GetCleanupDiagnostics() const;
	void ResetCleanupDiagnostics();

private:
	UULMSubsystem* Owner;
	FULMRotationConfig Config;
	mutable FULMRotationDiagnostics CleanupDiagnostics;
	mutable FCriticalSection RetentionLock;

	FDateTime LastCleanupTime;
	FTimerHandle CleanupTimerHandle;

	bool IsFileExpired(const FString& FilePath) const;
	bool IsLogFile(const FString& FilePath) const;
	FDateTime GetFileCreationDate(const FString& FilePath) const;
	void UpdateCleanupDiagnostics(int32 FilesDeleted, int64 BytesFreed);
};