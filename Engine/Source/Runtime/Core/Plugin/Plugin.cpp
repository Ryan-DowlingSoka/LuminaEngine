#include "pch.h"
#include "Plugin.h"
#include "Paths/Paths.h"

namespace Lumina
{
    FPlugin::FPlugin(FPluginDescriptor InDescriptor, FString InPluginDirectory, FString InDescriptorPath)
        : Descriptor(Move(InDescriptor))
        , PluginDirectory(Move(InPluginDirectory))
        , DescriptorPath(Move(InDescriptorPath))
        , bEnabled(Descriptor.bEnabledByDefault)
    {
    }

    FString FPlugin::GetMountAlias() const
    {
        FString Result = "/";
        Result.append(Descriptor.Name.c_str(), Descriptor.Name.size());
        return Result;
    }

    FString FPlugin::GetContentDirectory() const
    {
        FString Result = PluginDirectory;
        Result += "/Content";
        return Result;
    }

    FString FPlugin::ResolveModuleBinaryPath(FStringView ModuleName) const
    {
        // Same layout the engine uses (see Engine.cpp::Engine_LoadGameDLL):
        //   <plugin>/Binaries/<Platform>/<Module>-<Cfg>.dll
        //
        // LUMINA_PLATFORM_NAME ("Windows64") includes the architecture
        // suffix because the binary folder is keyed by full platform.
        // LUMINA_SYSTEM_NAME ("Windows") drops the arch and is used to
        // match against .lplugin SupportedPlatforms strings; the two are
        // intentionally distinct, don't unify them.
        // LUMINA_SHAREDLIB_EXT_NAME already includes the leading dot
        // (".dll" / ".so" / ".dylib") so no separator is added below.
        FString Result = PluginDirectory;
        Result += "/Binaries/";
        Result += LUMINA_PLATFORM_NAME;
        Result += "/";
        Result.append(ModuleName.data(), ModuleName.size());
        Result += "-";
        Result += LUMINA_CONFIGURATION_NAME;
        Result += LUMINA_SHAREDLIB_EXT_NAME;
        return Result;
    }
}
