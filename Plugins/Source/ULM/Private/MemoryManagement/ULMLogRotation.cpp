#include "MemoryManagement/ULMLogRotation.h"
#include "Core/ULMSubsystem.h"
#include "Logging/ULMLogging.h"
#include "Channels/ULMChannel.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"

FULMLogFileTracker::FULMLogFileTracker()
{
}

//destructor
FULMLogFileTracker::~FULMLogFileTracker()
{
	Clear();
}

void FULMLogFileTracker::RegisterFile(const FString& ChannelName, const FString& FilePath, const FDateTime& CreationDate, int32 FileIndex)
{
	FScopeLock Lock(&FileTrackingLock);
	
	FULMLogFileInfo NewFile(FilePath, ChannelName, CreationDate, FileIndex);
	
	TArray<FULMLogFileInfo>& Files = ChannelFiles.FindOrAdd(ChannelName);
	
	for (FULMLogFileInfo& ExistingFile : Files)
	{
		if (ExistingFile.FilePath == FilePath)
		{
			ExistingFile.CreationDate = CreationDate;
			ExistingFile.FileIndex = FileIndex;
			return;
		}
	}
	
	Files.Add(NewFile);
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Log file registered - Channel: %s, File: %s, Index: %d"), 
		*ChannelName, *FPaths::GetCleanFilename(FilePath), FileIndex);
}

void FULMLogFileTracker::UpdateFileSize(const FString& ChannelName, int64 NewSize)
{
	FScopeLock Lock(&FileTrackingLock);
	
	TArray<FULMLogFileInfo>* Files = ChannelFiles.Find(ChannelName);
	if (!Files)
	{
		return;
	}
	
	for (FULMLogFileInfo& File : *Files)
	{
		if (File.bIsActive)
		{
			File.FileSize = NewSize;
			break;
		}
	}
}

FULMLogFileInfo* FULMLogFileTracker::GetActiveFile(const FString& ChannelName)
{
	// Use const version and cast away constness - avoids code duplication
	return const_cast<FULMLogFileInfo*>(
		static_cast<const FULMLogFileTracker*>(this)->GetActiveFile(ChannelName)
	);
}

const FULMLogFileInfo* FULMLogFileTracker::GetActiveFile(const FString& ChannelName) const
{
	FScopeLock Lock(&FileTrackingLock);
	
	const TArray<FULMLogFileInfo>* Files = ChannelFiles.Find(ChannelName);
	if (!Files)
	{
		return nullptr;
	}
	
	// Find active file
	for (const FULMLogFileInfo& File : *Files)
	{
		if (File.bIsActive)
		{
			return &File;
		}
	}
	
	return nullptr;
}


TArray<FULMLogFileInfo> FULMLogFileTracker::GetAllFiles(const FString& ChannelName) const
{
	FScopeLock Lock(&FileTrackingLock);
	
	TArray<FULMLogFileInfo> AllFiles;
	
	if (ChannelName.IsEmpty())
	{
		// Return all files from all channels
		for (const auto& ChannelPair : ChannelFiles)
		{
			AllFiles.Append(ChannelPair.Value);
		}
	}
	else
	{
		// Return files for specific channel
		const TArray<FULMLogFileInfo>* Files = ChannelFiles.Find(ChannelName);
		if (Files)
		{
			AllFiles = *Files;
		}
	}
	
	return AllFiles;
}

bool FULMLogFileTracker::ShouldRotateFile(const FString& ChannelName, int64 MaxSize) const
{
	FScopeLock Lock(&FileTrackingLock);
	
	const TArray<FULMLogFileInfo>* Files = ChannelFiles.Find(ChannelName);
	if (!Files)
	{
		return false;
	}
	
	// Check if active file exceeds max size
	for (const FULMLogFileInfo& File : *Files)
	{
		if (File.bIsActive && File.FileSize >= MaxSize)
		{
			return true;
		}
	}
	
	return false;
}

FString FULMLogFileTracker::GenerateRotatedFilePath(const FString& ChannelName, const FString& BaseLogPath) const
{
	FScopeLock Lock(&FileTrackingLock);
	
	FDateTime Now = FDateTime::Now();
	FString DateString = Now.ToString(TEXT("%Y%m%d"));
	
	// Find the highest index for today
	int32 NextIndex = 1;
	const TArray<FULMLogFileInfo>* Files = ChannelFiles.Find(ChannelName);
	if (Files)
	{
		for (const FULMLogFileInfo& File : *Files)
		{
			FString FileDateString = File.CreationDate.ToString(TEXT("%Y%m%d"));
			if (FileDateString == DateString && File.FileIndex >= NextIndex)
			{
				NextIndex = File.FileIndex + 1;
			}
		}
	}
	
	// Generate filename: ULM_Channel_YYYYMMDD_XXX.json
	FString Filename = FString::Printf(TEXT("ULM_%s_%s_%03d.json"), 
		*ChannelName, *DateString, NextIndex);
	
	return BaseLogPath / Filename;
}

void FULMLogFileTracker::SetFileActive(const FString& ChannelName, const FString& FilePath)
{
	FScopeLock Lock(&FileTrackingLock);
	
	TArray<FULMLogFileInfo>* Files = ChannelFiles.Find(ChannelName);
	if (!Files)
	{
		return;
	}
	
	// Deactivate all files for this channel
	for (FULMLogFileInfo& File : *Files)
	{
		File.bIsActive = false;
	}
	
	// Activate the specified file
	for (FULMLogFileInfo& File : *Files)
	{
		if (File.FilePath == FilePath)
		{
			File.bIsActive = true;
			break;
		}
	}
}

void FULMLogFileTracker::RemoveFile(const FString& FilePath)
{
	FScopeLock Lock(&FileTrackingLock);
	
	for (auto& ChannelPair : ChannelFiles)
	{
		TArray<FULMLogFileInfo>& Files = ChannelPair.Value;
		Files.RemoveAll([&FilePath](const FULMLogFileInfo& File)
		{
			return File.FilePath == FilePath;
		});
	}
}

void FULMLogFileTracker::Clear()
{
	FScopeLock Lock(&FileTrackingLock);
	ChannelFiles.Empty();
}

bool FULMLogFileTracker::ParseLogFileName(const FString& FileName, FString& OutChannel, FDateTime& OutDate, int32& OutIndex) const
{
	// Expected format: ULM_Channel_YYYYMMDD_XXX.json
	TArray<FString> Parts;
	FileName.ParseIntoArray(Parts, TEXT("_"), true);
	
	if (Parts.Num() < 4)
	{
		return false;
	}
	
	if (Parts[0] != TEXT("ULM"))
	{
		return false;
	}
	
	OutChannel = Parts[1];
	
	// Parse date (YYYYMMDD)
	FString DateString = Parts[2];
	if (DateString.Len() != 8)
	{
		return false;
	}
	
	int32 Year = FCString::Atoi(*DateString.Left(4));
	int32 Month = FCString::Atoi(*DateString.Mid(4, 2));
	int32 Day = FCString::Atoi(*DateString.Right(2));
	
	OutDate = FDateTime(Year, Month, Day);
	
	// Parse index (XXX.json)
	FString IndexPart = Parts[3];
	IndexPart = FPaths::GetBaseFilename(IndexPart);  // Remove .json extension
	OutIndex = FCString::Atoi(*IndexPart);
	
	return true;
}

// FULMLogRotator Implementation

FULMLogRotator::FULMLogRotator(UULMSubsystem* InOwner)
	: Owner(InOwner)
{
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("Log rotator initialized"));
}

FULMLogRotator::~FULMLogRotator()
{
}

void FULMLogRotator::SetRotationConfig(const FULMRotationConfig& NewConfig)
{
	FScopeLock Lock(&RotationLock);
	Config = NewConfig;
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Rotation config updated - Max size: %lld bytes, Retention: %d days"), 
		Config.MaxFileSizeBytes, Config.RetentionDays);
}

FULMRotationConfig FULMLogRotator::GetRotationConfig() const
{
	FScopeLock Lock(&RotationLock);
	return Config;
}

bool FULMLogRotator::ShouldRotateFile(const FString& ChannelName, const FString& FilePath) const
{
	if (!IsValidRotationConfig())
	{
		return false;
	}
	
	// Check file size
	int64 FileSize = IFileManager::Get().FileSize(*FilePath);
	if (FileSize >= Config.MaxFileSizeBytes)
	{
		return true;
	}
	
	// Check with file tracker
	return FileTracker.ShouldRotateFile(ChannelName, Config.MaxFileSizeBytes);
}

FString FULMLogRotator::RotateFile(const FString& ChannelName, const FString& CurrentFilePath)
{
	FScopeLock Lock(&RotationLock);
	
	if (!IsValidRotationConfig())
	{
		return CurrentFilePath;
	}
	
	// Generate new rotated file path
	FString BaseLogPath = FPaths::GetPath(CurrentFilePath);
	FString NewFilePath = FileTracker.GenerateRotatedFilePath(ChannelName, BaseLogPath);
	
	// Mark old file as inactive
	FileTracker.SetFileActive(ChannelName, TEXT(""));
	
	// Register new file
	FDateTime Now = FDateTime::Now();
	// Parse index from filename
	FString FileName = FPaths::GetCleanFilename(NewFilePath);
	FString OutChannel;
	FDateTime OutDate;
	int32 FileIndex;
	if (FileTracker.ParseLogFileName(FileName, OutChannel, OutDate, FileIndex))
	{
		FileTracker.RegisterFile(ChannelName, NewFilePath, Now, FileIndex);
		FileTracker.SetFileActive(ChannelName, NewFilePath);
	}
	
	IncrementRotationCount();
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Log file rotated - Channel: %s, New file: %s"), 
		*ChannelName, *FPaths::GetCleanFilename(NewFilePath));
	
	return NewFilePath;
}

void FULMLogRotator::UpdateFileSize(const FString& ChannelName, int64 NewSize)
{
	FileTracker.UpdateFileSize(ChannelName, NewSize);
}

FString FULMLogRotator::GetActiveFilePath(const FString& ChannelName, const FString& BaseLogPath) const
{
	const FULMLogFileInfo* ActiveFile = FileTracker.GetActiveFile(ChannelName);
	if (ActiveFile)
	{
		return ActiveFile->FilePath;
	}
	
	// No active file found, generate new one
	FDateTime Now = FDateTime::Now();
	FString DateString = Now.ToString(TEXT("%Y%m%d"));
	FString Filename = FString::Printf(TEXT("ULM_%s_%s_001.json"), *ChannelName, *DateString);
	
	return BaseLogPath / Filename;
}

void FULMLogRotator::RegisterNewFile(const FString& ChannelName, const FString& FilePath)
{
	FDateTime Now = FDateTime::Now();
	
	// Parse index from filename
	FString FileName = FPaths::GetCleanFilename(FilePath);
	FString OutChannel;
	FDateTime OutDate;
	int32 FileIndex;
	if (FileTracker.ParseLogFileName(FileName, OutChannel, OutDate, FileIndex))
	{
		FileTracker.RegisterFile(ChannelName, FilePath, Now, FileIndex);
		FileTracker.SetFileActive(ChannelName, FilePath);
	}
}

FULMRotationDiagnostics FULMLogRotator::GetDiagnostics() const
{
	FScopeLock Lock(&RotationLock);
	
	// Update active files count
	TArray<FULMLogFileInfo> AllFiles = FileTracker.GetAllFiles();
	Diagnostics.ActiveFiles = AllFiles.Num();
	
	// Calculate total disk usage
	int64 TotalSize = 0;
	for (const FULMLogFileInfo& File : AllFiles)
	{
		TotalSize += File.FileSize;
	}
	Diagnostics.TotalDiskUsage = TotalSize;
	
	return Diagnostics;
}

void FULMLogRotator::ResetDiagnostics()
{
	FScopeLock Lock(&RotationLock);
	Diagnostics = FULMRotationDiagnostics();
}

void FULMLogRotator::IncrementRotationCount()
{
	Diagnostics.TotalRotations++;
}

bool FULMLogRotator::IsValidRotationConfig() const
{
	return Config.MaxFileSizeBytes > 0 && Config.RetentionDays > 0;
}

// FULMRetentionManager Implementation

FULMRetentionManager::FULMRetentionManager(UULMSubsystem* InOwner)
	: Owner(InOwner)
	, LastCleanupTime(FDateTime::MinValue())
{
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("Retention manager initialized"));
}

FULMRetentionManager::~FULMRetentionManager()
{
	// Clear any scheduled cleanup
	if (CleanupTimerHandle.IsValid())
	{
		if (UWorld* World = (Owner ? Owner->GetWorld() : nullptr))
		{
			World->GetTimerManager().ClearTimer(CleanupTimerHandle);
		}
	}
}

void FULMRetentionManager::PerformCleanup(const FString& BaseLogPath)
{
	FScopeLock Lock(&RetentionLock);
	
	if (Config.RetentionDays <= 0)
	{
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Warning, 
			TEXT("Retention cleanup skipped - Invalid retention policy"));
		return;
	}
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Starting retention cleanup - Policy: %d days"), Config.RetentionDays);
	
	// Find expired files
	TArray<FString> ExpiredFiles = GetExpiredFiles(BaseLogPath);
	
	if (ExpiredFiles.Num() > 0)
	{
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
			TEXT("Found %d expired log files for cleanup"), ExpiredFiles.Num());
		
		// Delete expired files
		bool bSuccess = DeleteExpiredFiles(ExpiredFiles);
		if (bSuccess)
		{
			ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
				TEXT("Retention cleanup completed successfully"));
		}
		else
		{
			ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Warning, 
				TEXT("Retention cleanup completed with some errors"));
		}
	}
	else
	{
		ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
			TEXT("No expired log files found for cleanup"));
	}
	
	LastCleanupTime = FDateTime::Now();
	CleanupDiagnostics.LastCleanupTime = LastCleanupTime;
}

void FULMRetentionManager::SchedulePeriodicCleanup()
{
	if (!Config.bPeriodicCleanup || Config.CleanupIntervalHours <= 0)
	{
		return;
	}
	
	UWorld* World = (Owner ? Owner->GetWorld() : nullptr);
	if (!World)
	{
		return;
	}
	
	// Clear existing timer
	if (CleanupTimerHandle.IsValid())
	{
		World->GetTimerManager().ClearTimer(CleanupTimerHandle);
	}
	
	// Schedule periodic cleanup
	float CleanupInterval = Config.CleanupIntervalHours * 3600.0f;  // Convert to seconds
	
	World->GetTimerManager().SetTimer(CleanupTimerHandle, 
		FTimerDelegate::CreateLambda([this]()
		{
			if (Owner)
			{
				FString BaseLogPath = FPaths::ProjectLogDir() / TEXT("ULM");
				PerformCleanup(BaseLogPath);
			}
		}), 
		CleanupInterval, true);
	
	ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
		TEXT("Periodic cleanup scheduled - Interval: %.1f hours"), Config.CleanupIntervalHours);
}

void FULMRetentionManager::SetRetentionConfig(const FULMRotationConfig& NewConfig)
{
	FScopeLock Lock(&RetentionLock);
	Config = NewConfig;
	
	// Reschedule cleanup if needed
	SchedulePeriodicCleanup();
}

TArray<FString> FULMRetentionManager::GetExpiredFiles(const FString& BaseLogPath) const
{
	TArray<FString> ExpiredFiles;
	
	// Find all log files in the directory
	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *BaseLogPath, TEXT("*.json"));
	
	FDateTime CutoffDate = (FDateTime::Now()) - FTimespan::FromDays(Config.RetentionDays);
	
	for (const FString& LogFile : LogFiles)
	{
		FString FullPath = BaseLogPath / LogFile;
		
		if (IsLogFile(FullPath) && IsFileExpired(FullPath))
		{
			ExpiredFiles.Add(FullPath);
		}
	}
	
	return ExpiredFiles;
}

bool FULMRetentionManager::DeleteExpiredFiles(const TArray<FString>& FilesToDelete)
{
	int32 FilesDeleted = 0;
	int64 BytesFreed = 0;
	bool bAllSuccess = true;
	
	for (const FString& FilePath : FilesToDelete)
	{
		int64 FileSize = IFileManager::Get().FileSize(*FilePath);
		
		if (IFileManager::Get().Delete(*FilePath))
		{
			FilesDeleted++;
			BytesFreed += FileSize;
			
			ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Message, 
				TEXT("Deleted expired log file: %s (%lld bytes)"), 
				*FPaths::GetCleanFilename(FilePath), FileSize);
		}
		else
		{
			bAllSuccess = false;
			ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Warning, 
				TEXT("Failed to delete expired log file: %s"), 
				*FPaths::GetCleanFilename(FilePath));
		}
	}
	
	UpdateCleanupDiagnostics(FilesDeleted, BytesFreed);
	
	return bAllSuccess;
}

int64 FULMRetentionManager::CalculateDiskUsage(const FString& BaseLogPath) const
{
	int64 TotalSize = 0;
	
	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *BaseLogPath, TEXT("*.json"));
	
	for (const FString& LogFile : LogFiles)
	{
		FString FullPath = BaseLogPath / LogFile;
		int64 FileSize = IFileManager::Get().FileSize(*FullPath);
		TotalSize += FileSize;
	}
	
	return TotalSize;
}

FULMRotationDiagnostics FULMRetentionManager::GetCleanupDiagnostics() const
{
	FScopeLock Lock(&RetentionLock);
	return CleanupDiagnostics;
}

void FULMRetentionManager::ResetCleanupDiagnostics()
{
	FScopeLock Lock(&RetentionLock);
	CleanupDiagnostics = FULMRotationDiagnostics();
}

bool FULMRetentionManager::IsFileExpired(const FString& FilePath) const
{
	FDateTime FileDate = GetFileCreationDate(FilePath);
	FDateTime CutoffDate = (FDateTime::Now()) - FTimespan::FromDays(Config.RetentionDays);
	
	return FileDate < CutoffDate;
}

bool FULMRetentionManager::IsLogFile(const FString& FilePath) const
{
	FString FileName = FPaths::GetCleanFilename(FilePath);
	return FileName.StartsWith(TEXT("ULM_")) && FileName.EndsWith(TEXT(".json"));
}

FDateTime FULMRetentionManager::GetFileCreationDate(const FString& FilePath) const
{
	FString FileName = FPaths::GetCleanFilename(FilePath);
	
	// Parse date from filename (ULM_Channel_YYYYMMDD_XXX.json)
	TArray<FString> Parts;
	FileName.ParseIntoArray(Parts, TEXT("_"), true);
	
	if (Parts.Num() >= 3)
	{
		FString DateString = Parts[2];
		if (DateString.Len() == 8)
		{
			int32 Year = FCString::Atoi(*DateString.Left(4));
			int32 Month = FCString::Atoi(*DateString.Mid(4, 2));
			int32 Day = FCString::Atoi(*DateString.Right(2));
			
			return FDateTime(Year, Month, Day);
		}
	}
	
	// Fallback to file modification time
	return IFileManager::Get().GetTimeStamp(*FilePath);
}

void FULMRetentionManager::UpdateCleanupDiagnostics(int32 FilesDeleted, int64 BytesFreed)
{
	CleanupDiagnostics.FilesDeleted += FilesDeleted;
	CleanupDiagnostics.BytesFreed += BytesFreed;
}