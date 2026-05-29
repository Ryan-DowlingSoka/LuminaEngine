#include "pch.h"
#include "PluginLoadingPhase.h"

namespace Lumina
{
    FStringView LexToString(EPluginLoadingPhase Phase)
    {
        switch (Phase)
        {
            case EPluginLoadingPhase::Earliest:        return "Earliest";
            case EPluginLoadingPhase::Core:            return "Core";
            case EPluginLoadingPhase::PreEngineInit:   return "PreEngineInit";
            case EPluginLoadingPhase::EngineInit:      return "EngineInit";
            case EPluginLoadingPhase::PostEngineInit:  return "PostEngineInit";
            case EPluginLoadingPhase::PostProjectLoad: return "PostProjectLoad";
            case EPluginLoadingPhase::EditorInit:      return "EditorInit";
            default:                                   return "Unknown";
        }
    }

    EPluginLoadingPhase ParsePluginLoadingPhase(FStringView Str, EPluginLoadingPhase Default)
    {
        if (Str == "Earliest")        return EPluginLoadingPhase::Earliest;
        if (Str == "Core")            return EPluginLoadingPhase::Core;
        if (Str == "PreEngineInit")   return EPluginLoadingPhase::PreEngineInit;
        if (Str == "EngineInit")      return EPluginLoadingPhase::EngineInit;
        if (Str == "PostEngineInit")  return EPluginLoadingPhase::PostEngineInit;
        if (Str == "PostProjectLoad") return EPluginLoadingPhase::PostProjectLoad;
        if (Str == "EditorInit")      return EPluginLoadingPhase::EditorInit;
        return Default;
    }
}
