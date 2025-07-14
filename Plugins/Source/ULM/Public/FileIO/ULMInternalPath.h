#pragma once

#include "CoreMinimal.h"

namespace ULMInternal
{
    inline FString ToMinimalRiderPath(const char* InAbsFile)
    {
        // 1. Normalise slashes
        FString Path(ANSI_TO_TCHAR(InAbsFile));
        Path.ReplaceInline(TEXT("\\"), TEXT("/"));

        // 2. Slice from the first occurrence of "/Source/", "/Plugins/", or "/Engine/Source/"
        auto CropFrom = [&](const TCHAR* Token)
        {
            int32 Idx = Path.Find(Token, ESearchCase::IgnoreCase, ESearchDir::FromStart);
            if (Idx != INDEX_NONE)
            {
                Path = Path.RightChop(Idx);   // keep token itself
            }
        };
        CropFrom(TEXT("/Source/"));
        CropFrom(TEXT("/Plugins/"));
        CropFrom(TEXT("/Engine/Source/"));

        // 3. Remove any spaces (Rider regex forbids them)
        Path.ReplaceInline(TEXT(" "), TEXT(""));

        return Path; // e.g.  "Source/UltraLogManager/ULMTestSuite.cpp"
    }
}