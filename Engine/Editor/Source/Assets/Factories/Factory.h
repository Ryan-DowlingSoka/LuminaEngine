#pragma once

#include "Containers/Function.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/Object.h"
#include "Core/Object/Cast.h"
#include "Memory/SmartPtr.h"
#include "Factory.generated.h"

namespace Lumina
{
    namespace Import
    {
        struct FImportSettings;
    }

    class CFactory;
    
    REFLECT()
    class EDITOR_API CFactoryRegistry : public CObject
    {
        GENERATED_BODY()
    public:

        static CFactoryRegistry& Get();
        
        void RegistryFactory(CFactory* Factory);

        const TVector<CFactory*>& GetFactories() const { return Factories; }

        TVector<CFactory*> Factories;
    };
    
    REFLECT()
    class EDITOR_API CFactory : public CObject
    {
        GENERATED_BODY()

    public:

        void PostCreateCDO() override;

        template<Concept::IsACObject T>
        T* TryCreateNew(FStringView Path)
        {
            return Cast<T>(TryCreateNew(Path));
        }
        
        CObject* TryCreateNew(FStringView Path);
        static CObject* CreateNewOf(CClass* Class, FStringView Path);
        
        template<Concept::IsACObject T>
        static T* CreateNewOf(FStringView Path)
        {
            return Cast<T>(CreateNewOf(T::StaticClass(), Path));
        }
            
        
        virtual FString GetAssetName() const { return ""; }
        virtual FString GetAssetDescription() const { return ""; }
        virtual CClass* GetAssetClass() const { return nullptr; }

        // Groups this asset type in the content browser's "New Asset" menu. Keep the set of
        // category strings small and shared across related factories.
        virtual FString GetCategory() const { return "Miscellaneous"; }
        virtual FStringView GetDefaultAssetCreationName() { return "New_Asset"; }
        
        virtual CObject* CreateNew(const FName& Name, CPackage* Package) { return nullptr; }

        void Import(const FFixedString& ImportFile, const FFixedString& DestinationPath, const Import::FImportSettings* Settings);
        
        virtual bool CanImport() { return false; }
        virtual void TryImport(const FFixedString& ImportFilePath, const FFixedString& DestinationPath, const Import::FImportSettings* Settings) { }
        
        virtual bool IsExtensionSupported(FStringView Ext) { return false; }
        
        static bool ShowCreationDialogue(CFactory* Factory, FStringView Path);

        virtual bool HasImportDialogue() const { return false; }
        virtual bool HasCreationDialogue() const { return false; }

        /**
         * Asynchronously builds the import settings that drive DrawImportDialogue (e.g. parsing
         * the source file off-thread). OnReady runs on the main thread once the settings are
         * ready; it receives null if preparation failed. Default is synchronous.
         */
        using FImportPrepareCallback = TMoveOnlyFunction<void(TUniquePtr<Import::FImportSettings>)>;
        virtual void PrepareImportAsync(const FFixedString& RawPath, const FFixedString& DestinationPath, FImportPrepareCallback OnReady);

        virtual bool DrawImportDialogue(const FFixedString& RawPath, const FFixedString& DestinationPath, TUniquePtr<Import::FImportSettings>& ImportSettings, bool& bShouldClose) { return true; }
        
    protected:
        
        virtual bool DrawCreationDialogue(FStringView Path, bool& bShouldClose) { return true; }
        
    };
}
