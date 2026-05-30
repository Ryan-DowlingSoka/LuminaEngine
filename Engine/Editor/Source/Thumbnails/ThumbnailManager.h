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

        // Populates a freshly-spun-up FThumbnailScene with what the asset's thumbnail should show; camera is already created and active.
        using FThumbnailRendererFn = TFunction<void(FThumbnailScene&, CObject* /*Asset*/)>;

        CThumbnailManager();

        void Initialize();

        static CThumbnailManager& Get();

        void AsyncLoadThumbnailsForPackage(const FName& Package);
        FPackageThumbnail* GetThumbnailForPackage(const FName& Package);

        void OnPackageDestroyed(FName Package);

        // Register a setup callback for an asset class; matched by walking up the class hierarchy, so subclasses inherit the renderer.
        void RegisterThumbnailRenderer(CClass* AssetClass, FThumbnailRendererFn Renderer);

        // Generate a fresh thumbnail for Asset into Package's slot; false if no renderer is registered (caller can fall back to viewport-grab).
        bool GenerateThumbnail(CObject* Asset, CPackage* Package);

        FSharedMutex ThumbnailLock;
        THashMap<FName, FPackageThumbnail*> Thumbnails;

    private:

        THashMap<CClass*, FThumbnailRendererFn> ThumbnailRenderers;

    };
}
