#pragma once

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
    class RUNTIME_API CFactoryRegistry : public CObject
    {
        GENERATED_BODY()
    public:

        static CFactoryRegistry& Get();
        
        void RegistryFactory(CFactory* Factory);

        const TVector<CFactory*>& GetFactories() const { return Factories; }

        // CDOs are always rooted.
        TVector<CFactory*> Factories;
    };
    
    REFLECT()
    class RUNTIME_API CFactory : public CObject
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
        virtual FStringView GetDefaultAssetCreationName() { return "New_Asset"; }
        
        virtual CObject* CreateNew(const FName& Name, CPackage* Package) { return nullptr; }

        void Import(const FFixedString& ImportFile, const FFixedString& DestinationPath, const Import::FImportSettings* Settings);
        
        virtual bool CanImport() { return false; }
        virtual void TryImport(const FFixedString& ImportFilePath, const FFixedString& DestinationPath, const Import::FImportSettings* Settings) { }
        
        virtual bool IsExtensionSupported(FStringView Ext) { return false; }
        
        static bool ShowCreationDialogue(CFactory* Factory, FStringView Path);

        virtual bool HasImportDialogue() const { return false; }
        virtual bool HasCreationDialogue() const { return false; }
        virtual bool DrawImportDialogue(const FFixedString& RawPath, const FFixedString& DestinationPath, TUniquePtr<Import::FImportSettings>& ImportSettings, bool& bShouldClose) { return true; }
        
    protected:
        
        virtual bool DrawCreationDialogue(FStringView Path, bool& bShouldClose) { return true; }
        
    };
}
