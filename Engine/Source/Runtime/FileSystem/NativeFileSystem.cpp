#include "pch.h"
#include "NativeFileSystem.h"
#include <filesystem>
#include <fstream>

#include "FileInfo.h"
#include "Paths/Paths.h"
#include "Platform/Process/PlatformProcess.h"


namespace Lumina::VFS
{
    FNativeFileSystem::FNativeFileSystem(const FFixedString& InAliasPath, FStringView InBasePath)
        : AliasPath(Paths::Normalize(InAliasPath))
        , BasePath(Paths::Normalize(InBasePath))
    {
    }

    FFixedString FNativeFileSystem::ResolveVirtualPath(FStringView Path) const
    {
        if (!Path.starts_with(AliasPath))
        {
            return {};
        }
    
        FStringView RelativePath = Path.substr(AliasPath.length());
    
        FFixedString FullPath = BasePath;
        FullPath.append(RelativePath.begin(), RelativePath.end());
        
        return FullPath;
    }

    bool FNativeFileSystem::ReadFile(TVector<uint8>& Result, FStringView Path)
    {
        FFixedString FullPath = ResolveVirtualPath(Path);
        
        Result.clear();

        std::ifstream File(FullPath.data(), std::ios::binary | std::ios::ate);
        if (!File)
        {
            return false;
        }

        const std::streamsize Size = File.tellg();
        if (Size < 0)
        {
            return false;
        }

        if (Size == 0)
        {
            return true;
        }

        File.seekg(0, std::ios::beg);

        Result.resize(static_cast<size_t>(Size));

        if (!File.read(reinterpret_cast<char*>(Result.data()), Size))
        {
            Result.clear();
            return false;
        }

        return true;
    }


    bool FNativeFileSystem::ReadFile(FString& OutString, FStringView Path)
    {
        FFixedString FullPath = ResolveVirtualPath(Path);

        std::ifstream File(FullPath.data(), std::ios::binary);
        if (!File)
        {
            return false;
        }

        File.seekg(0, std::ios::end);
        std::streamsize Size = File.tellg();
        File.seekg(0, std::ios::beg);

        if (Size < 0)
        {
            return false;
        }

        OutString.resize(static_cast<size_t>(Size));

        if (!File.read(OutString.data(), Size))
        {
            return false;
        }

        return true;
    }

    bool FNativeFileSystem::WriteFile(FStringView Path, FStringView Data)
    {
        FFixedString FullPath = ResolveVirtualPath(Path);
        std::ofstream File(FullPath.data(), std::ios::binary | std::ios::trunc);
        if (!File)
        {
            return false;
        }

        File.write(Data.data(), static_cast<std::streamsize>(Data.size()));
        return File.good();
    }

    bool FNativeFileSystem::WriteFile(FStringView Path, TSpan<const uint8> Data)
    {
        FFixedString FullPath = ResolveVirtualPath(Path);
        std::ofstream OutFile(FullPath.data(), std::ios::binary | std::ios::trunc);
        if (!OutFile)
        {
            return false;
        }

        if (!OutFile.write(reinterpret_cast<const char*>(Data.data()), static_cast<int64>(Data.size())))
        {
            return false;
        }

        return true;
    }

    bool FNativeFileSystem::Exists(FStringView Path) const
    {
        return std::filesystem::exists(ResolveVirtualPath(Path).c_str());
    }

    bool FNativeFileSystem::IsDirectory(FStringView Path) const
    {
        return std::filesystem::is_directory(ResolveVirtualPath(Path).c_str());
    }

    size_t FNativeFileSystem::Size(FStringView Path) const
    {
        return std::filesystem::file_size(ResolveVirtualPath(Path).c_str());
    }

    bool FNativeFileSystem::CreateDir(FStringView Path)
    {
        return std::filesystem::create_directories(ResolveVirtualPath(Path).c_str());
    }

    bool FNativeFileSystem::Remove(FStringView Path)
    {
        return std::filesystem::remove(ResolveVirtualPath(Path).c_str());
    }

    bool FNativeFileSystem::RemoveAll(FStringView Path)
    {
        return std::filesystem::remove_all(ResolveVirtualPath(Path).c_str());
    }

    bool FNativeFileSystem::Rename(FStringView Old, FStringView New)
    {
        FFixedString OldResolvedPath = ResolveVirtualPath(Old);
        FFixedString NewResolvedPath = ResolveVirtualPath(New);
        
        std::error_code EC;
        std::filesystem::rename(OldResolvedPath.c_str(), NewResolvedPath.c_str(), EC);
        
        if (EC)
        {
            LOG_ERROR("File System Error! - Failed to rename: {0}", EC.message());
            return false;
        }
        
        return true;
    }

    bool FNativeFileSystem::IsEmpty(FStringView Path) const
    {
        return std::filesystem::is_empty(ResolveVirtualPath(Path).c_str());
    }

    void FNativeFileSystem::PlatformOpen(FStringView Path) const
    {
        Platform::LaunchURL(StringUtils::ToWideString(ResolveVirtualPath(Path)).c_str());
    }

    void FNativeFileSystem::DirectoryIterator(FStringView Path, const TFunction<void(const FFileInfo&)>& Callback) const
    {
        FFixedString ResolvedPath = ResolveVirtualPath(Path);
        
        for (auto& Itr : std::filesystem::directory_iterator(ResolvedPath.c_str()))
        {
            std::string FilePath        = Itr.path().generic_string();
            std::string RelativeStr     = FilePath.substr(BasePath.size());
            FFixedString VirtualPath    { RelativeStr.data(), RelativeStr.size() };
            
            VirtualPath.insert(0, AliasPath);

            auto FileTime           = std::filesystem::last_write_time(Itr);
            auto SysTime            = std::chrono::clock_cast<std::chrono::system_clock>(FileTime);
            int64 LastModifyTime    = std::chrono::duration_cast<std::chrono::nanoseconds>(SysTime.time_since_epoch()).count();
            bool bHidden            = Itr.path().filename().generic_string().starts_with(".");
            

            EFileFlags Flags = EFileFlags::None;
            
            if (Itr.path().extension() == ".lua" || Itr.path().extension() == ".luau")
            {
                Flags |= EFileFlags::LuaFile;
            }
            
            if (Itr.path().extension() == ".lasset")
            {
                Flags |= EFileFlags::LAssetFile;
            }
            
            if (bHidden)
            {
                Flags |= EFileFlags::Hidden;
            }
            
            if (Itr.is_directory())
            {
                Flags |= EFileFlags::Directory;
            }
            
            if (Itr.is_symlink())
            {
                Flags |= EFileFlags::Symlink;
            }
            
            auto Perms = Itr.status().permissions();
            if ((Perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none)
            {
                Flags |= EFileFlags::ReadOnly;
            }
            
            FFileInfo FileInfo
            {
                .Name               = Itr.path().filename().generic_string().c_str(),
                .VirtualPath        = FFixedString{VirtualPath.data(),VirtualPath.size()},
                .PathSource         = FFixedString{FilePath.data(), FilePath.size()},
                .LastModifyTime     = LastModifyTime,
                .Flags              = Flags
            };
            
            Callback(Move(FileInfo));
        }
    }

    void FNativeFileSystem::RecursiveDirectoryIterator(FStringView Path, const TFunction<void(const FFileInfo&)>& Callback) const
    {
        FFixedString BaseResolvedPath = ResolveVirtualPath(Path);
        
        for (auto& Itr : std::filesystem::recursive_directory_iterator(BaseResolvedPath.c_str()))
        {
            std::string FilePath        = Itr.path().generic_string();
            std::string RelativeStr     = FilePath.substr(BasePath.size());
            FFixedString VirtualPath    { RelativeStr.data(), RelativeStr.size() };
            
            VirtualPath.insert(0, AliasPath);

            auto FileTime           = std::filesystem::last_write_time(Itr);
            auto SysTime            = std::chrono::clock_cast<std::chrono::system_clock>(FileTime);
            int64 LastModifyTime    = std::chrono::duration_cast<std::chrono::nanoseconds>(SysTime.time_since_epoch()).count();
            bool bHidden            = Itr.path().filename().generic_string().starts_with(".");
            

            EFileFlags Flags = EFileFlags::None;
            
            if (Itr.path().extension() == ".lua" || Itr.path().extension() == ".luau")
            {
                Flags |= EFileFlags::LuaFile;
            }
            
            if (Itr.path().extension() == ".lasset")
            {
                Flags |= EFileFlags::LAssetFile;
            }
            
            if (bHidden)
            {
                Flags |= EFileFlags::Hidden;
            }
            
            if (Itr.is_directory())
            {
                Flags |= EFileFlags::Directory;
            }
            
            if (Itr.is_symlink())
            {
                Flags |= EFileFlags::Symlink;
            }
            
            auto Perms = Itr.status().permissions();
            if ((Perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none)
            {
                Flags |= EFileFlags::ReadOnly;
            }
            
            FFileInfo FileInfo
            {
                .Name               = Itr.path().filename().generic_string().c_str(),
                .VirtualPath        = FFixedString{VirtualPath.data(),VirtualPath.size()},
                .PathSource         = FFixedString{FilePath.data(), FilePath.size()},
                .LastModifyTime     = LastModifyTime,
                .Flags              = Flags
            };
            
            Callback(Move(FileInfo));
        }
    }
}
