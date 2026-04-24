#pragma once

#include <meshoptimizer.h>
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Templates/Optional.h"
#include "Core/Utils/Expected.h"
#include "Memory/SmartPtr.h"
#include "Platform/Platform.h"
#include "Renderer/Format.h"
#include "Renderer/RenderResource.h"

namespace Lumina
{
    struct FAnimationResource;
    struct FMeshResource;
    struct FSkeletonResource;
    class IRenderContext;
    struct FVertex;
}

namespace Lumina::Import
{
    struct FImportSettings
    {
        virtual ~FImportSettings() = default;
        
        template<typename T>
        requires(eastl::is_base_of_v<FImportSettings, T> && !eastl::is_pointer_v<T>)
        const T& As() const
        {
            return *static_cast<const T*>(this);
        }
    };
    
    
    namespace Textures
    {
        struct FTextureImportResult
        {
            TVector<uint8>  Pixels;
            glm::uvec2      Dimensions;
            EFormat         Format;
        };
        
        /** Gets an image's raw pixel data */
        RUNTIME_API TOptional<FTextureImportResult> ImportTexture(FStringView RawFilePath, bool bFlipVertical = true, glm::uvec2 Size = {});
        RUNTIME_API TOptional<FTextureImportResult> ImportTexture(TSpan<const uint8> ImageData, bool bFlipVertical = true, glm::uvec2 Size = {});
    
        /** Creates a raw RHI Image */
        NODISCARD RUNTIME_API FRHIImageRef CreateTextureFromImport(FStringView RawFilePath, bool bFlipVerticalOnLoad = true, glm::uvec2 Size = {});
    }

    
    
    
    namespace Mesh
    {
        struct FMeshImportOptions
        {
            bool bOptimize          = true;
            bool bImportMaterials   = true;
            bool bImportTextures    = true;
            bool bImportMeshes      = true;
            bool bImportAnimations  = true;
            bool bImportSkeleton    = true;
            bool bFlipNormals       = false;
            bool bFlipUVs           = false;
            bool bMergeMeshes       = false;
            float Scale             = 1.0f;
        };

        struct FMeshImportImage : FImportSettings
        {
            FFixedString    RelativePath;
            FRHIImageRef    DisplayImage;
            TVector<uint8>  Bytes;
            
            NODISCARD bool IsBytes() const { return !Bytes.empty(); }

            bool operator==(const FMeshImportImage& Other) const
            {
                return Other.RelativePath == RelativePath && Other.Bytes == Bytes;
            }
        };

        struct FMeshImportImageHasher
        {
            size_t operator()(const FMeshImportImage& Asset) const noexcept
            {
                size_t Seed = 0;
                Hash::HashCombine(Seed, Asset.RelativePath);
                Hash::HashCombine(Seed, Asset.Bytes.data());
                return Seed;
            }
        };
    
        struct FMeshImportImageEqual
        {
            bool operator()(const FMeshImportImage& A, const FMeshImportImage& B) const noexcept
            {
                return A.RelativePath == B.RelativePath && A.Bytes == B.Bytes;
            }
        };

        using FMeshImportTextureMap = THashSet<FMeshImportImage, FMeshImportImageHasher, FMeshImportImageEqual>;
        
        struct FMeshStatistics : INonCopyable
        {
            TVector<meshopt_OverdrawStatistics>         OverdrawStatics;
            TVector<meshopt_VertexFetchStatistics>      VertexFetchStatics;
        };

        struct FMeshImportData : FImportSettings
        {
            FMeshStatistics                             MeshStatistics;
            FMeshImportTextureMap                       Textures;
            TVector<TUniquePtr<FMeshResource>>          Resources;
            TVector<TUniquePtr<FAnimationResource>>     Animations;
            TVector<TUniquePtr<FSkeletonResource>>      Skeletons;
        };
        
        void OptimizeNewlyImportedMesh(FMeshResource& MeshResource);
        void GenerateShadowBuffers(FMeshResource& MeshResource);
        void GenerateMeshlets(FMeshResource& MeshResource);
        void AnalyzeMeshStatistics(FMeshResource& MeshResource, FMeshStatistics& OutMeshStats);
        
        namespace OBJ
        {
            NODISCARD RUNTIME_API TExpected<FMeshImportData, FString> ImportOBJ(const FMeshImportOptions& ImportOptions, FStringView FilePath);
        }
        
        namespace FBX
        {
            NODISCARD RUNTIME_API TExpected<FMeshImportData, FString> ImportFBX(const FMeshImportOptions& ImportOptions, FStringView FilePath);
        }

        
        namespace GLTF
        {
            NODISCARD RUNTIME_API TExpected<FMeshImportData, FString> ImportGLTF(const FMeshImportOptions& ImportOptions, FStringView FilePath);
        }
    }
    
}
