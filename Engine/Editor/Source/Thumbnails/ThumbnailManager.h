#pragma once
#include "Containers/Function.h"
#include "Core/Object/Object.h"
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
        // asset class, caller can fall back to its viewport-grab path.
        bool GenerateThumbnail(CObject* Asset, CPackage* Package);

        FSharedMutex ThumbnailLock;
        THashMap<FName, FPackageThumbnail*> Thumbnails;

    private:

        THashMap<CClass*, FThumbnailRendererFn> ThumbnailRenderers;

    };
}
