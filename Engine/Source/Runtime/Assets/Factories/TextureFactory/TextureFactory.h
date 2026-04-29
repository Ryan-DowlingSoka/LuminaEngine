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

        /** Re-runs the Basis Universal compression on an existing texture using
         *  its currently-set ColorSpace, replacing its compressed bits and
         *  RHI image in-place. Reads from `Texture->SourcePath`; returns false
         *  if the source file is missing or the asset wasn't imported from a
         *  file (e.g. mesh-embedded). Marks the package dirty on success. */
        static RUNTIME_API bool Recook(CTexture* Texture);

    private:

    };
}
