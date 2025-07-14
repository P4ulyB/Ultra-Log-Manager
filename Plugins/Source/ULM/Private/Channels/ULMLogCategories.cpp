#include "Channels/ULMLogCategories.h"
#include <atomic>

// Forward declarations for atomic global pointers
class FULMChannelRegistry;
class UULMSubsystem;

/**
 * ULM (Ultra Log Manager) - Centralized Log Category Definitions
 * 
 * This file contains the SINGLE authoritative DEFINE_LOG_CATEGORY statement for each ULM log category.
 * 
 * CRITICAL: This is the ONLY file in the entire codebase that should contain DEFINE_LOG_CATEGORY 
 * statements for ULM categories. Adding them elsewhere violates the One-Definition Rule and 
 * causes linker errors in multi-module builds.
 * 
 * See Docs/LoggingConventions.md for the prevention rule that enforces this constraint.
 */

DEFINE_LOG_CATEGORY(ULM);
DEFINE_LOG_CATEGORY(ULMGameplay);
DEFINE_LOG_CATEGORY(ULMNetwork);
DEFINE_LOG_CATEGORY(ULMPerformance);
DEFINE_LOG_CATEGORY(ULMDebug);
DEFINE_LOG_CATEGORY(ULMAI);
DEFINE_LOG_CATEGORY(ULMPhysics);
DEFINE_LOG_CATEGORY(ULMAudio);
DEFINE_LOG_CATEGORY(ULMAnimation);
DEFINE_LOG_CATEGORY(ULMUI);
DEFINE_LOG_CATEGORY(ULMSubsystem);

// Thread-safe global state using atomic pointers
ULM_API std::atomic<FULMChannelRegistry*> GULMChannelRegistry(nullptr);
ULM_API std::atomic<UULMSubsystem*> GULMSubsystem(nullptr);