#pragma once
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "ThumbnailManager.generated.h"

namespace Lumina
{
    struct FPackageThumbnail;
}

namespace Lumina
{
    REFLECT()
    class CThumbnailManager : public CObject
    {
        GENERATED_BODY()
    public:

        CThumbnailManager();

        void Initialize();
        
        static CThumbnailManager& Get();

        void AsyncLoadThumbnailsForPackage(const FName& Package);
        FPackageThumbnail* GetThumbnailForPackage(const FName& Package);
        
        void OnPackageDestroyed(FName Package);
        
        /** Unit cube mesh used to render thumbnail previews for cube-shaped assets. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> CubeMesh;

        /** Unit sphere mesh used to render thumbnail previews for sphere-shaped assets. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> SphereMesh;

        /** Unit plane mesh used to render thumbnail previews for flat assets. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> PlaneMesh;
        
        
        FSharedMutex ThumbnailLock;
        THashMap<FName, FPackageThumbnail*> Thumbnails;
        
    };
}
