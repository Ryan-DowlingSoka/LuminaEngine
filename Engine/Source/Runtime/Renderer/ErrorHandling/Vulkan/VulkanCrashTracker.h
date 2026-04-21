#pragma once

#include <volk/volk.h>
#include "VkBootstrap.h"
#if WITH_AFTERMATH
// Aftermath's decoding header exposes its SPIR-V helpers only when VULKAN_H_ is defined.
// volk includes vulkan_core.h directly (not vulkan.h), so VULKAN_H_ is not set - declare it
// manually so GFSDK_Aftermath_GetShaderHashSpirv and friends are visible.
#ifndef VULKAN_H_
#define VULKAN_H_ 1
#endif
#include <NvidiaAftermath/GFSDK_Aftermath_GpuCrashDumpDecoding.h>
#endif
#include "Containers/Name.h"
#include "Renderer/ErrorHandling/CrashTracker.h"

namespace Lumina::RHI
{
    class FVulkanCrashTracker : public ICrashTracker
    {
    public:

        FVulkanCrashTracker();
        ~FVulkanCrashTracker() override = default;
        LE_NO_COPYMOVE(FVulkanCrashTracker);

        void Initialize(RHIDevice InDevice, RHIPhysicalDevice InPhysicalDevice) override;
        void Shutdown() override;

        void OnDeviceLost() override;

        // Call after device creation to enable Aftermath features
        void EnableDeviceFeatures(vkb::DeviceBuilder& Builder);

        void GPUCrashDumpCallback(const void* GPUCrashDump, uint32 CrashDumpSize) override;
        void OnShaderDebugInfo(const void* ShaderDebugInfo, const uint32 ShaderDebugInfoSize) override;

        void RegisterShader(const TVector<uint32>& SPRIV, const FString& Name) override;
        void SetMarker(RHICommandBuffer cmdBuffer, const char* markerName) override;
        void BeginMarker(RHICommandBuffer cmdBuffer, const char* markerName) override;
        void EndMarker(RHICommandBuffer cmdBuffer) override;
        void PollCrashDumps() override;

        const FString& GetCrashDumpDirectory() const { return CrashDumpDirectory; }

        #if WITH_AFTERMATH
        // Decoder lookup handlers (called from static callback shims)
        void OnShaderDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugInfoIdentifier& Identifier, PFN_GFSDK_Aftermath_SetData SetShaderDebugInfo) const;
        void OnShaderLookup(const GFSDK_Aftermath_ShaderBinaryHash& ShaderHash, PFN_GFSDK_Aftermath_SetData SetShaderBinary) const;
        void OnShaderSourceDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugName& DebugName, PFN_GFSDK_Aftermath_SetData SetShaderBinary) const;
        #endif
        
    private:

        struct FRegisteredShader
        {
            TVector<uint8> Binary;
            FString        DebugName;
            FString        FriendlyName;
        };

        mutable FSharedMutex ShaderRegistryMutex;
        // Keyed by GFSDK_Aftermath_ShaderBinaryHash::hash (uint64)
        THashMap<uint64, FRegisteredShader> RegisteredShaders;
        // Debug name (from GFSDK_Aftermath_GetShaderDebugNameSpirv) -> shader hash
        THashMap<FString, uint64> DebugNameToHash;

        mutable FSharedMutex ShaderDebugInfoMutex;
        // Keyed by combined hash of the identifier id[0]/id[1] pair
        THashMap<uint64, TVector<uint8>> ShaderDebugInfos;

        // Marker strings must remain live for the full life of the command buffer;
        // Aftermath dereferences these pointers when decoding a crash dump.
        mutable FSharedMutex MarkerMutex;
        TList<FString> MarkerStorage;

        const void* StoreMarker(const char* MarkerName);

        VkDevice Device = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;

        FString CrashDumpDirectory;

        bool bInitialized = false;
    };

}
