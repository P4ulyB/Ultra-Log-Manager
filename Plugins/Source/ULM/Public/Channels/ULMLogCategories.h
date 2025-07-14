#pragma once

#include "CoreMinimal.h"

/**
 * ULM (Ultra Log Manager) - Centralized Log Category Declarations
 * 
 * This header contains DECLARE_LOG_CATEGORY_EXTERN declarations for all ULM log categories.
 * The corresponding DEFINE_LOG_CATEGORY statements are in ULMLogCategories.cpp.
 * 
 * WARNING: NEVER add DEFINE_LOG_CATEGORY statements anywhere else in the codebase.
 * This violates the One-Definition Rule and causes linker errors in multi-module builds.
 */

// ULM Master Category - Catch-all channel showing all ULM logs
DECLARE_LOG_CATEGORY_EXTERN(ULM, Log, All);

// ULM Gameplay - Gameplay-related logs
DECLARE_LOG_CATEGORY_EXTERN(ULMGameplay, Log, All);

// ULM Network - Network communication logs
DECLARE_LOG_CATEGORY_EXTERN(ULMNetwork, Log, All);

// ULM Performance - Performance monitoring logs
DECLARE_LOG_CATEGORY_EXTERN(ULMPerformance, Log, All);

// ULM Debug - Debug information logs
DECLARE_LOG_CATEGORY_EXTERN(ULMDebug, Log, All);

// ULM AI - AI system logs
DECLARE_LOG_CATEGORY_EXTERN(ULMAI, Log, All);

// ULM Physics - Physics system logs
DECLARE_LOG_CATEGORY_EXTERN(ULMPhysics, Log, All);

// ULM Audio - Audio system logs
DECLARE_LOG_CATEGORY_EXTERN(ULMAudio, Log, All);

// ULM Animation - Animation system logs
DECLARE_LOG_CATEGORY_EXTERN(ULMAnimation, Log, All);

// ULM UI - User interface logs
DECLARE_LOG_CATEGORY_EXTERN(ULMUI, Log, All);

// ULM Subsystem - System initialization logs (isolated)
DECLARE_LOG_CATEGORY_EXTERN(ULMSubsystem, Log, All);