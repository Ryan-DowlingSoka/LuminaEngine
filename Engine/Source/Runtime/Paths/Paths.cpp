#include "pch.h"
#include "Paths.h"
#include <filesystem>
#include "Core/Assertions/Assert.h"

namespace Lumina::Paths
{
    static THashMap<FName, FString> CachedDirectories;

    namespace
    {
        const char* EngineResourceDirectoryName     = "EngineResourceDirectory";
        const char* EngineFontDirectoryName         = "EngineFontDirectory";
        const char* EngineContentDirectoryName      = "EngineContentDirectory";
        const char* EngineShadersDirectoryName      = "EngineShadersDirectory";
        const char* EngineConfigDirectoryName       = "EngineConfigDirectory";
        const char* EngineInstallDirectoryName      = "EngineInstallDirectory";
        const char* EngineDirectoryName             = "EngineDirectory";

    }

    void InitializePaths()
    {
        FString LuminaDir = std::getenv("LUMINA_DIR");
        Normalize(LuminaDir);
            
        CachedDirectories[EngineInstallDirectoryName]   = LuminaDir;
        CachedDirectories[EngineDirectoryName]          = LuminaDir + "/Engine";
        CachedDirectories[EngineConfigDirectoryName]    = GetEngineDirectory() + "/Config";
        CachedDirectories[EngineResourceDirectoryName]  = GetEngineDirectory() + "/Resources";
        CachedDirectories[EngineFontDirectoryName]      = GetEngineResourceDirectory() + "/Fonts";
        CachedDirectories[EngineContentDirectoryName]   = GetEngineResourceDirectory() + "/Content";
        CachedDirectories[EngineShadersDirectoryName]   = GetEngineResourceDirectory() + "/Shaders";
        
    }

    FString GetEngineDirectory()
    {
        return CachedDirectories[EngineDirectoryName];
    }

    FString GetExtension(const FString& InPath)
    {
        size_t Dot = InPath.find_last_of('.');
        if (Dot != FString::npos && Dot + 1 < InPath.length())
        {
            return InPath.substr(Dot);
        }

        return {};
    }

    bool IsDirectory(const FString& Path)
    {
        return std::filesystem::is_directory(Path.c_str());
    }

    bool Exists(FStringView Filename)
    {
        return std::filesystem::exists(Filename.data());
    }

    FString GetVirtualPathPrefix(const FString& VirtualPath)
    {
        size_t Pos = VirtualPath.find("://");
        if (Pos != FString::npos)
        {
            return VirtualPath.substr(0, Pos + 3);
        }

        return VirtualPath;
    }

    bool CreateDirectories(FStringView Path)
    {
        return std::filesystem::create_directories(Path.data());
    }

    bool IsUnderDirectory(const FString& ParentDirectory, const FString& Directory)
    {
        if (Directory.length() < ParentDirectory.length())
        {
            return false;
        }

        if (FString::comparei(Directory.data(), Directory.data() + ParentDirectory.length(), ParentDirectory.data(), ParentDirectory.data() + ParentDirectory.length()) != 0)
        {
            return false;
        }

        if (Directory.length() > ParentDirectory.length())
        {
            char nextChar = Directory[ParentDirectory.length()];
            if (nextChar != '/' && nextChar != '\\')
            {
                return false;
            }
        }

        return true;
    }
    
    FString MakeRelativeTo(const FString& Path, const FString& BasePath)
    {
        FString NormalizedPath = Path;
        FString NormalizedBase = BasePath;
    
        eastl::replace(NormalizedPath.begin(), NormalizedPath.end(), '\\', '/');
        eastl::replace(NormalizedBase.begin(), NormalizedBase.end(), '\\', '/');
    
        if (!NormalizedBase.empty() && NormalizedBase.back() != '/')
        {
            NormalizedBase.push_back('/');
        }
    
        if (NormalizedPath.find(NormalizedBase) != 0)
        {
            return Path;
        }
    
        return NormalizedPath.substr(NormalizedBase.size());
    }

    void ReplaceFilename(FString& Path, const FString& NewFilename)
    {
        size_t LastSlash = Path.find_last_of("/\\");

        if (LastSlash != FString::npos)
        {
            Path = Path.substr(0, LastSlash + 1) + NewFilename;
            return;
        }

        Path = NewFilename;
    }

    const FString& GetEngineResourceDirectory()
    {
        return CachedDirectories[EngineResourceDirectoryName];
    }

    const FString& GetEngineFontDirectory()
    {
        return CachedDirectories[EngineFontDirectoryName];
    }

    const FString& GetEngineContentDirectory()
    {
        return CachedDirectories[EngineContentDirectoryName];
    }

    const FString& GetEngineConfigDirectory()
    {
        return CachedDirectories[EngineConfigDirectoryName];
    }

    const FString& GetEngineShadersDirectory()
    {
        return CachedDirectories[EngineShadersDirectoryName];
    }

    FString Parent(FStringView Path, bool bRemoveTrailingSlash)
    {
        auto data = Path.data();
        auto len = Path.size();

        size_t i = len;
        while (i > 0)
        {
            char c = data[i - 1];
            if (c == '/' || c == '\\')
            {
                break;
            }
            --i;
        }

        if (i == 0)
        {
            return FString();
        }

        if (bRemoveTrailingSlash && i > 1)
        {
            size_t j = i - 1;
            while (j > 0)
            {
                char c = data[j - 1];
                if (c != '/' && c != '\\')
                {
                    break;
                }
                --j;
            }
            i = j;
        }

        return FString(data, i);
    }

    const FString& GetEngineInstallDirectory()
    {
        return CachedDirectories[EngineInstallDirectoryName];
    }

    void Normalize(FString& Path)
    {
        eastl::replace(Path.begin(), Path.end(), '\\', '/');
    }

    void Normalize(FFixedString& Path)
    {
        eastl::replace(Path.begin(), Path.end(), '\\', '/');
    }

    FFixedString Normalize(FStringView Path)
    {
        FFixedString RetVal = { Path.begin(), Path.end() };
        eastl::replace(RetVal.begin(), RetVal.end(), '\\', '/');
        return RetVal;
    }

    bool PathsEqual(FStringView A, FStringView B)
    {
        size_t lenA = A.size();
        size_t lenB = B.size();
        if (lenA != lenB)
        {
            return false;
        }

        for (size_t i = 0; i < lenA; ++i)
        {
            char a = A[i];
            char b = B[i];

            if ((a == '/' || a == '\\') && (b == '/' || b == '\\'))
            {
                continue;
            }


            if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b)))
            {
                return false;
            }
        }

        return true;
    }
    
    bool SetEnvVariable(const FString& name, const FString& value)
    {
#ifdef _WIN32
        if (_putenv_s(name.c_str(), value.c_str()) == 0)
        {
            LOG_TRACE("Environment variable {} set to {}", name, value);
            return true;
        }
        LOG_WARN("Failed to set environment variable {}", name);
        return false;
#else
        if (setenv(name.c_str(), value.c_str(), 1) == 0)
        {
            LOG_TRACE("Environment variable {} set to {}", name, value);
            return true;
        }
        LOG_WARN("Failed to set environment variable {}", name);
        return false;
#endif
    }
}
