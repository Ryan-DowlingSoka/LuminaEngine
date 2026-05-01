#pragma once
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Containers/Function.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "ThumbnailManager.generated.h"

namespace Lumina
{
    class CClass;
    class CPackage;
    class FThumbnailScene;
    struct FPackageThumbnail;
}

namespace Lumina
{
    REFLECT()
    class CThumbnailManager : public CObject
    {
        GENERATED_BODY()
    public:

        // Populates the world with whatever the thumbnail for InAsset should
        // show (mesh + lights for a CStaticMesh, sphere with material applied
        // for a CMaterial, etc). Called against a freshly-spun-up
        // FThumbnailScene; the scene's camera is already created and active.
        using FThumbnailRendererFn = TFunction<void(FThumbnailScene&, CObject* /*Asset*/)>;

        CThumbnailManager();

        void Initialize();

        static CThumbnailManager& Get();

        void AsyncLoadThumbnailsForPackage(const FName& Package);
        FPackageThumbnail* GetThumbnailForPackage(const FName& Package);

        void OnPackageDestroyed(FName Package);

        // Register a setup callback for an asset class. The callback is matched
        // by walking up the asset's class hierarchy, so deriving from an
        // already-registered class inherits the renderer.
        void RegisterThumbnailRenderer(CClass* AssetClass, FThumbnailRendererFn Renderer);

        // Generate a fresh thumbnail for Asset and write it into Package's
        // thumbnail slot. Returns false if no renderer is registered for this
        // asset class — caller can fall back to its viewport-grab path.
        bool GenerateThumbnail(CObject* Asset, CPackage* Package);
        
        /** Unit cube mesh used to render thumbnail previews for cube-shaped assets. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> CubeMesh;

        /** Unit sphere mesh used to render thumbnail previews for sphere-shaped assets. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> SphereMesh;

        /** Unit plane mesh used to render thumbnail previews for flat assets. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> PlaneMesh;

        /** Unit cylinder mesh, also used as a primitive when spawning entities in the editor. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> CylinderMesh;

        /** Unit cone mesh, also used as a primitive when spawning entities in the editor. */
        PROPERTY(NotSerialized)
        TObjectPtr<CStaticMesh> ConeMesh;


        FSharedMutex ThumbnailLock;
        THashMap<FName, FPackageThumbnail*> Thumbnails;

    private:

        THashMap<CClass*, FThumbnailRendererFn> ThumbnailRenderers;

    };
}
