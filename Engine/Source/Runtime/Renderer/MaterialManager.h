#pragma once

#include "RenderResource.h"
#include "MaterialTypes.h"
#include "Containers/Array.h"
#include "Containers/SparseArray.h"

namespace Lumina
{
    class CMaterialInterface;
    struct FMaterialUniforms;
}

namespace Lumina::RHI
{
    class FMaterialManager
    {
    public:
        
        FMaterialManager();
        
        void AddMaterial(CMaterialInterface* Material);
        void RemoveMaterial(CMaterialInterface* Material);
        
        void UpdateMaterialUniforms(const FMaterialUniforms* InUniforms, uint32 Index);
        FRHIBuffer* GetMaterialBuffer() const { return MaterialBuffer; }
    
    private:
        
        FSharedMutex                            Mutex;
        TStack<uint32>                          FreeList;
        FRHIBufferRef                           MaterialBuffer;
    };
}
