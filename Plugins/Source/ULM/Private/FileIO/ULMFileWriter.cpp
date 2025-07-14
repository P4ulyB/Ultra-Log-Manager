#include "FileIO/ULMFileWriter.h"
#include "Core/ULMSubsystem.h"
#include "Logging/ULMLogging.h"
#include "Channels/ULMChannel.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"

FULMFileWriter::FULMFileWriter(UULMSubsystem* InOwner, TQueue<FULMFileWriteEntry, EQueueMode::Spsc>& InWriteQueue)
	: bStopRequested(false)
	, WakeUpEvent(nullptr)
	, Owner(InOwner)
	, WriteQueue(InWriteQueue)
	, BatchSize(DEFAULT_BATCH_SIZE)
	, FlushIntervalSeconds(5.0f)
	, LastFlushTime(0.0)
	, BaseLogPath(FPaths::ProjectLogDir() / TEXT("ULM"))
{
	WakeUpEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FULMFileWriter::~FULMFileWriter()
{
	if (WakeUpEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeUpEvent);
		WakeUpEvent = nullptr;
	}
}

bool FULMFileWriter::Init()
{
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULMFileWriter: Initializing asynchronous file writer with batch size %d"), BatchSize);
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*BaseLogPath))
	{
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Creating log directory: %s"), *BaseLogPath);
		if (!PlatformFile.CreateDirectoryTree(*BaseLogPath))
		{
			ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Error, TEXT("CRITICAL: Failed to create log directory: %s"), *BaseLogPath);
			return false;
		}
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("Log directory created successfully"));
	}
	
	LastFlushTime = FPlatformTime::Seconds();
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("File writer thread initialization complete"));
	return true;
}

uint32 FULMFileWriter::Run()
{
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("File writer thread started - entering main processing loop"));
	
	double StartTime = FPlatformTime::Seconds();
	
	while (!bStopRequested.Load())
	{
		ProcessWriteQueue();
		
		if (ShouldFlush())
		{
			FlushAllFiles();
			LastFlushTime = FPlatformTime::Seconds();
		}
		
		if (WriteQueue.IsEmpty())
		{
			WakeUpEvent->Wait(FTimespan::FromMilliseconds(100));
		}
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("File writer thread shutdown requested - flushing and closing files..."));
	
	ProcessWriteQueue();
	FlushAllFiles();
	CloseAllFiles();
	
	double EndTime = FPlatformTime::Seconds();
	double RuntimeSeconds = EndTime - StartTime;
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, 
		TEXT("File writer thread exiting - runtime: %.2f seconds, files managed: %d"), 
		RuntimeSeconds, OpenFiles.Num());
	
	return 0;
}

void FULMFileWriter::Stop()
{
	RequestStop();
}


void FULMFileWriter::RequestStop()
{
	bStopRequested.Store(true);
	WakeUp();
}

void FULMFileWriter::WakeUp()
{
	if (WakeUpEvent)
	{
		WakeUpEvent->Trigger();
	}
}

void FULMFileWriter::SetBatchSize(int32 NewBatchSize)
{
	BatchSize = FMath::Clamp(NewBatchSize, 1, MAX_BATCH_SIZE);
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULMFileWriter: Batch size set to %d"), BatchSize);
}

void FULMFileWriter::SetFlushInterval(float NewFlushIntervalSeconds)
{
	FlushIntervalSeconds = FMath::Max(0.1f, NewFlushIntervalSeconds);
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULMFileWriter: Flush interval set to %.2f seconds"), FlushIntervalSeconds);
}

void FULMFileWriter::SetBaseLogPath(const FString& NewBasePath)
{
	BaseLogPath = NewBasePath;
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*BaseLogPath))
	{
		PlatformFile.CreateDirectoryTree(*BaseLogPath);
	}
	
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULMFileWriter: Base log path set to %s"), *BaseLogPath);
}

void FULMFileWriter::ProcessWriteQueue()
{
	if (WriteQueue.IsEmpty())
	{
		return;
	}
	
	TArray<FULMFileWriteEntry> Batch;
	Batch.Reserve(BatchSize);
	
	FULMFileWriteEntry Entry;
	while (Batch.Num() < BatchSize && WriteQueue.Dequeue(Entry))
	{
		Batch.Add(Entry);
	}
	
	if (Batch.Num() > 0)
	{
		ProcessBatch(Batch);
	}
}

void FULMFileWriter::ProcessBatch(TArray<FULMFileWriteEntry>& Batch)
{
	double StartTime = FPlatformTime::Seconds();
	
	TMap<FString, TArray<FString>> FileGroups;
	
	for (const FULMFileWriteEntry& Entry : Batch)
	{
		if (!FileGroups.Contains(Entry.FilePath))
		{
			FileGroups.Add(Entry.FilePath, TArray<FString>());
		}
		FileGroups[Entry.FilePath].Add(Entry.LogLine);
	}
	
	for (const auto& FileGroup : FileGroups)
	{
		const FString& FilePath = FileGroup.Key;
		const TArray<FString>& Lines = FileGroup.Value;
		
		FString CombinedContent;
		for (const FString& Line : Lines)
		{
			CombinedContent += Line + TEXT("\n");
		}
		
		WriteToFile(FilePath, CombinedContent);
	}
	
	double EndTime = FPlatformTime::Seconds();
	UpdateWriteTimeDiagnostics(StartTime, EndTime);
	Diagnostics.BatchCount.Increment();
}

void FULMFileWriter::WriteToFile(const FString& FilePath, const FString& Content)
{
	FScopeLock Lock(&FileMapLock);
	
	TSharedPtr<FArchive> FileArchive = GetOrCreateFile(FilePath);
	if (!FileArchive.IsValid())
	{
		Diagnostics.FailedWrites.Increment();
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Warning, TEXT("ULMFileWriter: Failed to open file for writing: %s"), *FilePath);
		return;
	}
	
	FTCHARToUTF8 UTF8Content(*Content);
	const int32 BytesToWrite = UTF8Content.Length();
	
	FileArchive->Serialize(const_cast<char*>(UTF8Content.Get()), BytesToWrite);
	
	Diagnostics.WriteCount.Increment();
	Diagnostics.TotalBytesWritten.Add(BytesToWrite);
	
	if (FileArchive->IsError())
	{
		Diagnostics.FailedWrites.Increment();
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Error, TEXT("ULMFileWriter: Error writing to file: %s"), *FilePath);
	}
}

void FULMFileWriter::FlushAllFiles()
{
	FScopeLock Lock(&FileMapLock);
	
	for (auto& FilePair : OpenFiles)
	{
		if (FilePair.Value.IsValid())
		{
			FilePair.Value->Flush();
		}
	}
}

void FULMFileWriter::CloseAllFiles()
{
	FScopeLock Lock(&FileMapLock);
	
	for (auto& FilePair : OpenFiles)
	{
		if (FilePair.Value.IsValid())
		{
			FilePair.Value->Close();
		}
	}
	
	OpenFiles.Empty();
	ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULMFileWriter: Closed all open files"));
}

TSharedPtr<FArchive> FULMFileWriter::GetOrCreateFile(const FString& FilePath)
{
	if (TSharedPtr<FArchive>* ExistingFile = OpenFiles.Find(FilePath))
	{
		if (ExistingFile->IsValid())
		{
			return *ExistingFile;
		}
	}
	
	TSharedPtr<FArchive> NewArchive = MakeShareable(IFileManager::Get().CreateFileWriter(*FilePath, FILEWRITE_Append));
	
	if (NewArchive.IsValid())
	{
		OpenFiles.Add(FilePath, NewArchive);
		ULM_LOG(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULMFileWriter: Opened file for writing: %s"), *FilePath);
	}
	
	return NewArchive;
}



void FULMFileWriter::UpdateWriteTimeDiagnostics(double StartTime, double EndTime)
{
	int64 WriteTimeMicros = (int64)((EndTime - StartTime) * 1000000.0);
	Diagnostics.TotalWriteTime.Add(WriteTimeMicros);
}

bool FULMFileWriter::ShouldFlush() const
{
	double CurrentTime = FPlatformTime::Seconds();
	return (CurrentTime - LastFlushTime) >= FlushIntervalSeconds;
}