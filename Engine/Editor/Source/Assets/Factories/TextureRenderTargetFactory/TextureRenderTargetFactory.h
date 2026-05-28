#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/Textures/TextureRenderTarget.h"
#include "TextureRenderTargetFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CTextureRenderTargetFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Render Target"; }
        FStringView GetDefaultAssetCreationName() override { return "NewRenderTarget"; }
        FString GetAssetDescription() const override { return "A writable Texture2D you can paint or render into (e.g. a blood map)."; }
        CClass* GetAssetClass() const override { return CTextureRenderTarget::StaticClass(); }
        FString GetCategory() const override { return "Texture"; }
    };
}
