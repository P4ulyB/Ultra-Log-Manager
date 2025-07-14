# Ultra-Log-Manager
- ULM (Ultra Log Manager)

ULM is a high-performance JSON logging system for Unreal Engine 5.6+ projects. It provides thread-safe, channel-based logging with asynchronous file writing, memory management, and comprehensive Blueprint integration.

-- What It Is

ULM is an Unreal Engine plugin that supplements the standard UE logging system with:

- 'JSON-only output format' for structured log data
- 'Channel-based organization' with predefined and custom channels
- 'High-performance design' using lock-free queues and background processing
- 'Memory budget management' with automatic trimming and health monitoring
- 'Log rotation and retention' with configurable policies
- 'Thread-safe operations' suitable for multi-threaded game environments
- 'Blueprint integration' with enum-based channel selection
- 'IDE integration' with clickable file paths in JetBrains Rider

-- What It Does

The system captures log messages through C++ macros or Blueprint functions, processes them on background threads, and writes structured JSON data to log files. Each channel creates its own JSON file (e.g., `ULM_Gameplay_20251214_001.json`) with automatic rotation based on file size and retention policies.

All log entries include timestamps, verbosity levels, thread IDs, channel information, and structured JSON formatting for easy parsing and analysis.

---

-- Installation

--- 1. Copy Plugin Files

Copy the `UltraLogManager` plugin folder to your project's `Plugins/` directory:
```
YourProject/
├── Plugins/
│   └── UltraLogManager/
│       ├── UltraLogManager.uplugin
│       └── Source/
└── Source/
```

--- 2. Update Build Dependencies

Add ULM to your module's `Build.cs` file:

```csharp
// YourProject/Source/YourProject/YourProject.Build.cs
public class YourProject : ModuleRules
{
    public YourProject(ReadOnlyTargetRules Target) : base(Target)
    {
        // Add ULM to your dependencies
        PublicDependencyModuleNames.AddRange(new string[] 
        { 
            "Core", 
            "CoreUObject", 
            "Engine",
            "ULM"  // Add this line
        });
    }
}
```

--- 3. Include Headers

In your C++ files, include the ULM header:

```cpp
-include "Logging/ULMLogging.h"
```

--- 4. Enable Plugin

Enable the UltraLogManager plugin in your project through:
- Unreal Editor → Edit → Plugins → Project → Search "UltraLogManager"
- Or add to your `.uproject` file:

```json
{
    "Plugins": [
        {
            "Name": "UltraLogManager",
            "Enabled": true
        }
    ]
}
```

---

-- Available Channels

ULM provides predefined channels for common game systems:

| Channel 		  | Purpose 							          | Log Category 		  |
|---------------|---------------------------------|-------------------|
| `Gameplay` 	  | Gameplay logic and mechanics 		| `ULMGameplay` 	  |
| `Network` 	  | Networking and multiplayer 		  | `ULMNetwork` 		  |
| `Performance` | Performance monitoring 			    | `ULMPerformance` 	|
| `Debug` 		  | Debug information 				      | `ULMDebug` 		    |
| `AI` 			    | AI systems and behavior 			  | `ULMAI` 			    |
| `Physics` 	  | Physics simulation 				      | `ULMPhysics` 		  |
| `Audio` 		  | Audio systems 					        | `ULMAudio` 		    |
| `Animation` 	| Animation systems 				      | `ULMAnimation` 	  |
| `UI` 			    | User interface 					        | `ULMUI` 			    |
| `ULM` 		    | Master channel (shows all logs) | `ULM` 			      |
| `Subsystem` 	| System initialization 			    | `ULMSubsystem` 	  |

---

-- Adding Custom Channels

--- 1. Define Channel in Master List

Edit `Plugins/UltraLogManager/Source/ULM/Public/Channels/ULMChannel.h`:

```cpp
-define ULM_CHANNEL_LIST(X) \
    X(ULM,          "ULM",          "ULM (All Logs)") \
    X(Gameplay,     "Gameplay",     "ULMGameplay") \
    X(Network,      "Network",      "ULMNetwork") \
    /* ... existing channels ... */ \
    X(MyChannel,    "MyChannel",    "ULMMyChannel") \  // Add your channel here
```

--- 2. Add to Blueprint Enum

In the same file, add to the `EULMChannel` enum:

```cpp
UENUM(BlueprintType)
enum class EULMChannel : uint8
{
    Gameplay     UMETA(DisplayName = "Gameplay"),
    Network      UMETA(DisplayName = "Network"),
    /* ... existing channels ... */
    MyChannel    UMETA(DisplayName = "My Channel"),  // Add here
};
```

--- 3. Define Log Category

In `Plugins/UltraLogManager/Source/ULM/Public/Channels/ULMLogCategories.h`:

```cpp
DECLARE_LOG_CATEGORY_EXTERN(ULMMyChannel, Log, All);
```

In `Plugins/UltraLogManager/Source/ULM/Private/Channels/ULMLogCategories.cpp`:

```cpp
DEFINE_LOG_CATEGORY(ULMMyChannel);
```

--- 4. Add to Channel Mapping

In `Plugins/UltraLogManager/Source/ULM/Private/Logging/ULMLogging.cpp`, update the `InitializeChannelCategoryMap()` function:

```cpp
static void InitializeChannelCategoryMap()
{
    // ... existing mappings ...
    ChannelCategoryMap.Add(TEXT("MyChannel"), &ULMMyChannel);
    // ...
}
```

--- 5. Add to Engine Configuration

In your project's `Config/DefaultEngine.ini`:

```ini
[Core.Log]
LogULMMyChannel=Log
```

--- 6. Create Channel Constant

In `Plugins/UltraLogManager/Source/ULM/Public/Logging/ULMLogging.h`:

```cpp
// Channel name constants
static const FString ULMChannelMyChannel(TEXT("MyChannel"));

// Channel name convenience macros  
-define CHANNEL_MYCHANNEL ULMChannelMyChannel
```

---

-- Macro Reference and Examples

--- Core Logging Macros

---- `ULM_LOG(Channel, Verbosity, Format, ...)`
Basic logging with channel and verbosity control.

```cpp
ULM_LOG(CHANNEL_GAMEPLAY, EULMVerbosity::Message, TEXT("Player health: %d"), PlayerHealth);
ULM_LOG(CHANNEL_NETWORK, EULMVerbosity::Warning, TEXT("High ping detected: %f ms"), PingTime);
ULM_LOG(CHANNEL_PERFORMANCE, EULMVerbosity::Error, TEXT("Frame rate dropped below threshold"));
```

---- `ULM_LOG_SERVER(Channel, Verbosity, Format, ...)`
Logs only when running on server or in standalone mode.

```cpp
ULM_LOG_SERVER(CHANNEL_GAMEPLAY, EULMVerbosity::Message, TEXT("Server: Player %s connected"), *PlayerName);
```

---- `ULM_LOG_CLIENT(Channel, Verbosity, Format, ...)`
Logs only when running on client.

```cpp
ULM_LOG_CLIENT(CHANNEL_UI, EULMVerbosity::Message, TEXT("Client: Menu opened"));
```

--- Verbosity-Specific Macros

---- `ULM_MESSAGE(Channel, Format, ...)`
Shorthand for Message verbosity.

```cpp
ULM_MESSAGE(CHANNEL_GAMEPLAY, TEXT("Game started"));
ULM_MESSAGE(CHANNEL_AI, TEXT("AI state changed to: %s"), *NewState);
```

---- `ULM_WARNING(Channel, Format, ...)`
Shorthand for Warning verbosity.

```cpp
ULM_WARNING(CHANNEL_NETWORK, TEXT("Connection unstable"));
ULM_WARNING(CHANNEL_PHYSICS, TEXT("Physics simulation variance detected"));
```

---- `ULM_ERROR(Channel, Format, ...)`
Shorthand for Error verbosity.

```cpp
ULM_ERROR(CHANNEL_GAMEPLAY, TEXT("Critical game state error"));
ULM_ERROR(CHANNEL_AUDIO, TEXT("Failed to load audio asset: %s"), *AssetPath);
```

---- `ULM_CRITICAL(Channel, Format, ...)`
Shorthand for Critical verbosity.

```cpp
ULM_CRITICAL(CHANNEL_PERFORMANCE, TEXT("Memory budget exceeded"));
ULM_CRITICAL(CHANNEL_NETWORK, TEXT("Server connection lost"));
```

--- Enhanced Logging Macros

---- `ULM_LOG_ENHANCED(Channel, Verbosity, Format, ...)`
Includes file name and line number in the log output.

```cpp
ULM_LOG_ENHANCED(CHANNEL_DEBUG, EULMVerbosity::Message, TEXT("Debug checkpoint reached"));
// Output includes: [MyFile.cpp:123] Debug checkpoint reached
```

---- `ULM_LOG_CLASS(Channel, Verbosity, Format, ...)`
Includes class name and function name (use within class methods).

```cpp
void AMyActor::UpdateHealth()
{
    ULM_LOG_CLASS(CHANNEL_GAMEPLAY, EULMVerbosity::Message, TEXT("Health updated"));
    // Output includes: [AMyActor::UpdateHealth] Health updated
}
```

---- Enhanced Verbosity Shortcuts

```cpp
ULM_MESSAGE_ENHANCED(CHANNEL_GAMEPLAY, TEXT("Enhanced message"));
ULM_WARNING_ENHANCED(CHANNEL_NETWORK, TEXT("Enhanced warning"));
ULM_ERROR_ENHANCED(CHANNEL_AUDIO, TEXT("Enhanced error"));
ULM_CRITICAL_ENHANCED(CHANNEL_PERFORMANCE, TEXT("Enhanced critical"));

ULM_MESSAGE_CLASS(CHANNEL_GAMEPLAY, TEXT("Class message"));
ULM_WARNING_CLASS(CHANNEL_NETWORK, TEXT("Class warning"));
ULM_ERROR_CLASS(CHANNEL_AUDIO, TEXT("Class error"));
ULM_CRITICAL_CLASS(CHANNEL_PERFORMANCE, TEXT("Class critical"));
```

--- Conditional and Sampling Macros

---- `ULM_LOG_IF(Condition, Channel, Verbosity, Format, ...)`
Logs only if condition is true.

```cpp
ULM_LOG_IF(bIsDebugMode, CHANNEL_DEBUG, EULMVerbosity::Message, TEXT("Debug mode active"));
ULM_LOG_IF(Health < 25, CHANNEL_GAMEPLAY, EULMVerbosity::Warning, TEXT("Low health warning"));
```

---- `ULM_LOG_SAMPLED(Channel, Verbosity, Format, ...)`
Logs with automatic sampling to reduce spam.

```cpp
// Will automatically sample to prevent log spam
ULM_LOG_SAMPLED(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("Tick performance: %f ms"), DeltaTime);
```

---- `ULM_LOG_SAMPLED_RATE(Channel, Verbosity, Rate, Format, ...)`
Logs with specified sampling rate.

```cpp
// Logs approximately 10% of the time
ULM_LOG_SAMPLED_RATE(CHANNEL_AI, EULMVerbosity::Message, 0.1f, TEXT("AI update tick"));
```

--- Object-Specific Macros

---- `ULM_LOG_OBJECT(Object, Channel, Verbosity, Format, ...)`
Includes object information in the log.

```cpp
ULM_LOG_OBJECT(this, CHANNEL_GAMEPLAY, EULMVerbosity::Message, TEXT("Actor spawned"));
ULM_LOG_OBJECT(PlayerCharacter, CHANNEL_GAMEPLAY, EULMVerbosity::Warning, TEXT("Player taking damage"));
```

---- `ULM_LOG_OBJECT_ENHANCED(Object, Channel, Verbosity, Format, ...)`
Enhanced object logging with additional context.

```cpp
ULM_LOG_OBJECT_ENHANCED(this, CHANNEL_GAMEPLAY, EULMVerbosity::Message, TEXT("Enhanced object log"));
```

--- Compact Logging Macros

---- `ULM_LOG_COMPACT(Channel, Verbosity, Format, ...)`
Minimal overhead logging for performance-critical sections.

```cpp
ULM_LOG_COMPACT(CHANNEL_PERFORMANCE, EULMVerbosity::Message, TEXT("High-frequency event"));
```

---- Compact Verbosity Shortcuts

```cpp
ULM_MESSAGE_COMPACT(CHANNEL_PERFORMANCE, TEXT("Compact message"));
ULM_WARNING_COMPACT(CHANNEL_PERFORMANCE, TEXT("Compact warning"));
ULM_ERROR_COMPACT(CHANNEL_PERFORMANCE, TEXT("Compact error"));
ULM_CRITICAL_COMPACT(CHANNEL_PERFORMANCE, TEXT("Compact critical"));
```

--- System-Level Macros

---- `ULM_LOG_CRITICAL_SYSTEM(Channel, Verbosity, Format, ...)`
Always logs to UE console, bypassing ULM checks (for system initialization/shutdown).

```cpp
ULM_LOG_CRITICAL_SYSTEM(CHANNEL_SUBSYSTEM, EULMVerbosity::Message, TEXT("ULM system starting"));
```

---- `ULM_LOG_STRUCTURED(Channel, Verbosity)`
Structured logging builder (experimental).

```cpp
ULM_LOG_STRUCTURED(CHANNEL_GAMEPLAY, EULMVerbosity::Message)
    .Field("player_id", PlayerId)
    .Field("position", PlayerLocation)
    .Message("Player position updated");
```

-- Blueprint Integration

--- Using Enhanced Logging Function

The primary Blueprint function provides enum-based channel selection:

```cpp
// In Blueprint:
Log Message (Enhanced)
├── Channel: Gameplay (dropdown)
├── Message: "Player spawned"
├── Verbosity: Message
├── Print to Screen: true
└── Custom Channel: (empty)
```

--- Component-Based Logging

Add `SimpleULMComponent` to any actor for convenient Blueprint logging:

```cpp
// Component functions available:
- Log Enhanced
- Log Simple Message  
- Log With Verbosity
- Test Performance Logging
```

--- Retrieving Logs

```cpp
// Get log entries in Blueprint:
Get Log Entries
├── Channel: "Gameplay"
├── Max Entries: 100
└── Returns: Array of ULM Log Entry
```

---

-- Configuration

--- Performance Tiers

ULM provides three performance presets in Project Settings → ULM Settings:

- 'Production': Maximum performance, minimal logging, high memory budget
- 'Development': Balanced performance, moderate logging, medium memory budget  
- 'Debug': Full debugging, extensive logging, low memory budget for testing
- 'Custom': User-defined configuration

--- Memory Management

```ini
[/Script/ULM.ULMSettings]
MemoryBudgetMB=50
MemoryTrimThreshold=0.8
EmergencyTrimPercentage=0.5
```

--- Log Rotation

```ini
[/Script/ULM.ULMSettings]
RotationConfig=(MaxFileSizeBytes=104857600,RetentionDays=7,MaxFilesPerDay=10)
```

--- Queue Configuration

```ini
[/Script/ULM.ULMSettings]
MaxQueueSize=10000
QueueHealthThreshold=0.9
BatchProcessingSize=64
```

---

-- File Output

--- JSON Format

All logs are written in structured JSON format:

```json
{
  "timestamp": "2025-01-15T14:30:25.123456",
  "channel": "Gameplay",
  "verbosity": "Message",
  "thread_id": 12345,
  "message": "Player health: 100",
  "session_id": "abc123",
  "build_version": "1.0.0"
}
```

<img width="498" height="529" alt="image" src="https://github.com/user-attachments/assets/6b66d103-6192-4f8b-97bc-c88441dcb61c" />

--- File Naming

Log files follow the pattern: `ULM_Channel_YYYYMMDD_XXX.json`

Examples:
- `ULM_Gameplay_20251215_001.json`
- `ULM_Network_20251215_002.json`
- `ULM_Performance_20251215_001.json`

--- File Locations

- 'Development': `ProjectLogDir/ULM/`
- 'Packaged': `Saved/Logs/ULM/`
- 'Custom': Configure via `CustomLogDirectory` setting

---

-- IDE Integration

--- JetBrains Rider

ULM optimizes for Rider's UnrealLink plugin:
- Clickable file paths in "Unreal Editor Log" panel
- Direct navigation to source code line numbers
- Enhanced debugging workflow

--- Unreal Editor Output Log

All channels appear as separate categories in the Output Log dropdown:
- Filter by specific channel (e.g., `ULMGameplay`)
- View master channel (`ULM`) for all logs
- Color-coded by verbosity level

---

-- Performance Characteristics

--- High-Performance Design

- 'Lock-free queues': SPSC (Single Producer, Single Consumer) queues
- 'Background processing': Dedicated threads for log processing and file I/O
- 'Batch operations': 64-entry batches for optimal throughput
- 'Memory management': Token bucket rate limiting and automatic trimming
- 'Thread-local caching': Channel state caching for sub-millisecond logging

--- Performance Targets

- 'Logging overhead': <0.01ms per log entry
- 'Queue processing': <1ms for 64-entry batches
- 'File I/O': Asynchronous with 5-second flush intervals
- 'Memory usage': 50MB default budget with automatic management

--- Threading Model

- 'Main Thread': Log entry creation and enqueuing
- 'Log Processor Thread': Queue consumption and memory storage  
- 'File Writer Thread': Asynchronous JSON file writing
- 'Memory Manager': Background trimming and health monitoring

---

-- Limitations

--- Channel Restrictions

- 'Fixed channel list': Channels must be defined in the master list at compile time
- 'No runtime channel creation': Cannot add new channels during gameplay
- 'Master list dependency': All channel additions require plugin modification

--- Performance Constraints

- 'Queue size limit': 10,000 entries maximum to prevent memory exhaustion
- 'Memory budget': Default 50MB limit with automatic trimming when exceeded
- 'File size limits': 100MB per file before rotation
- 'Batch processing delays': Up to 5-second delay for file writing

--- Platform Limitations

- 'Thread requirements': Requires multi-threading support
- 'File system access': Needs write permissions to log directory
- 'Memory overhead': Each channel maintains separate storage
- 'UE5.6+ only': May not be compatible with earlier Unreal Engine versions

--- Blueprint Limitations

- 'Enum synchronization': Blueprint enum must be manually updated when adding channels
- 'No custom channels': Blueprint interface doesn't support arbitrary channel names
- 'Limited formatting': Blueprint functions use basic string formatting

--- IDE Integration

- 'Rider-specific': Clickable paths optimized for JetBrains Rider only
- 'Plugin dependency': Requires RiderLink/UnrealLink plugin for full functionality
- 'File path format': Assumes specific file naming conventions

--- JSON Format Constraints

- 'JSON-only output': No support for plain text or other formats
- 'Unicode escaping': International characters are escaped in JSON
- 'File parsing': Large JSON files may require streaming parsers
- 'Timezone handling': Uses system local time without explicit timezone information

--- Memory Management

- 'Budget enforcement': Aggressive trimming may lose recent log entries
- 'Emergency trimming': May remove up to 50% of stored logs when budget exceeded
- 'No compression': Log entries stored uncompressed in memory
- 'Per-channel budgets': Memory distributed evenly across active channels

---

-- Troubleshooting

--- Common Issues

'Plugin not loading:'
- Verify plugin is enabled in project settings
- Check build dependencies include "ULM"
- Regenerate project files after adding plugin

'No log output:'
- Verify channels are registered correctly
- Check memory budget hasn't been exceeded
- Ensure write permissions to log directory

'Performance issues:'
- Reduce logging frequency in tight loops
- Use sampling macros for high-frequency events
- Increase memory budget if trimming occurs frequently

'Missing log categories in Output Log:'
- Log categories appear only after first use
- Use ULM macros to trigger category creation
- Check log category definitions in ULMLogCategories.cpp

--- Debug Commands

```cpp
// Console commands for diagnostics:
ULM.TestQueue          // Test queue performance
ULM.ShowDiagnostics    // Display real-time metrics
```

--- Health Monitoring

The system provides automatic health monitoring:
- Queue size and drop rate warnings
- Memory usage alerts
- File I/O failure detection
- Thread health status

---

-- Technical Architecture

--- Module Structure

```
ULM/
├── Public/
│   ├── Channels/     - Channel definitions and management
│   ├── Configuration/ - Settings and configuration
│   ├── Core/         - Main subsystem and module
│   ├── FileIO/       - File operations and JSON formatting
│   ├── Logging/      - Logging macros and processors
│   └── MemoryManagement/ - Memory budget and log rotation
└── Private/          - Implementation files
```

--- Thread Architecture

1. 'Main Thread': Creates log entries and enqueues to lock-free queue
2. 'Log Processor Thread': Processes queue entries and stores in memory
3. 'File Writer Thread': Batches and writes JSON to disk asynchronously
4. 'Background Tasks': Memory trimming, file rotation, health monitoring

--- Data Flow

```
[Log Macro] → [Validation] → [Queue] → [Processing] → [Memory Storage]
                                              ↓
[JSON Files] ← [File Writer] ← [Batch Queue] ← [Background Thread]
```

This system provides high-performance, structured logging for Unreal Engine projects with comprehensive C++ and Blueprint integration, suitable for both development and shipping builds.
