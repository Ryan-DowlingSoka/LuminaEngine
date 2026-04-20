#include "pch.h"
#include "ParticleSystem.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/Shader.h"

namespace Lumina
{
    void CParticleSystem::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);
    }

    void CParticleSystem::PostLoad()
    {
        if (!ComputeShaderBinaries.empty())
        {
            FShaderHeader Header;
            Header.DebugName    = GetName().ToString() + "_ComputeShader";
            Header.Hash         = Hash::GetHash64(ComputeShaderBinaries.data(), ComputeShaderBinaries.size() * sizeof(uint32));
            Header.Binaries     = ComputeShaderBinaries;
            ComputeShader       = GRenderContext->CreateComputeShader(Header);
        }
    }

    void CParticleSystem::OnDestroy()
    {
        CObject::OnDestroy();
        ComputeShader = nullptr;
    }
}
