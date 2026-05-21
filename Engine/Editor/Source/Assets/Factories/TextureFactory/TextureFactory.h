#pragma once
#include "Assets/Factories/Factory.h"
#include "Containers/String.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "TextureFactory.generated.h"


namespace Lumina
{
    REFLECT()
    class CTextureFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;
        CClass* GetAssetClass() const override { return CTexture::StaticClass(); }
        FString GetAssetName() const override { return "Texture"; }
        FStringView GetDefaultAssetCreationName() override { return "NewTexture"; }

        bool IsExtensionSupported(FStringView Ext) override
        {
            return Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".hdr";
        }
        bool CanImport() override { return true; }

        void TryImport(const FFixedString& RawPath, const FFixedString& DestinationPath, const Import::FImportSettings* Settings) override;

        /** Re-runs Basis compression on Texture->SourcePath; false if path is missing or asset is mesh-embedded. */
        static EDITOR_API bool Recook(CTexture* Texture);

    private:

    };
}
