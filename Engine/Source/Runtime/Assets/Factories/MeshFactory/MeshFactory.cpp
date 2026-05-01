#include "pch.h"
#include "MeshFactory.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "assets/assettypes/mesh/skeleton/skeleton.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Assets/Factories/TextureFactory/TextureFactory.h"
#include "Core/Object/Package/Package.h"
#include "Core/Utils/Defer.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "TaskSystem/TaskSystem.h"
#include "Tools/Import/ImportHelpers.h"
#include "Tools/UI/ImGui/ImGuiX.h"


namespace Lumina
{
    namespace
    {
        using namespace Import::Mesh;

        // Re-parses the source file with neutral options for preview. User
        // transforms (Scale, FlipUVs, FlipNormals) and the heavy optimize/
        // shadow/meshlet passes are deferred to commit time so toggling
        // options in the dialog never triggers another re-parse.
        bool PreviewParse(const FFixedString& RawPath, FMeshImportData& Out)
        {
            FMeshImportOptions PreviewOptions;
            PreviewOptions.bOptimize         = false;
            PreviewOptions.bMergeMeshes      = false;
            PreviewOptions.bFlipNormals      = false;
            PreviewOptions.bFlipUVs          = false;
            PreviewOptions.Scale             = 1.0f;
            PreviewOptions.bSkipFinalization = true;

            const FName Ext = VFS::Extension(RawPath);
            TExpected<FMeshImportData, FString> Result;
            if (Ext == ".obj")
            {
                Result = OBJ::ImportOBJ(PreviewOptions, RawPath);
            }
            else if (Ext == ".gltf" || Ext == ".glb")
            {
                Result = GLTF::ImportGLTF(PreviewOptions, RawPath);
            }
            else if (Ext == ".fbx")
            {
                Result = FBX::ImportFBX(PreviewOptions, RawPath);
            }

            if (!Result)
            {
                LOG_ERROR("Encountered problem importing source file: {0}", Result.Error());
                return false;
            }

            Out = Move(Result.Value());
            return true;
        }

        constexpr float kLabelColumnWidth = 180.0f;

        bool BeginPropertyTable(const char* Id)
        {
            if (!ImGui::BeginTable(Id, 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit))
            {
                return false;
            }
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, kLabelColumnWidth);
            ImGui::TableSetupColumn("##editor", ImGuiTableColumnFlags_WidthStretch);
            return true;
        }

        void PropertyLabel(const char* Label, const char* Tooltip)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Label);
            if (Tooltip && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", Tooltip);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
        }

        void CheckboxRow(const char* Label, const char* Tooltip, bool& Value)
        {
            PropertyLabel(Label, Tooltip);
            ImGui::PushID(Label);
            ImGui::Checkbox("##v", &Value);
            ImGui::PopID();
        }

        void DragFloatRow(const char* Label, const char* Tooltip, float& Value, float Min, float Max, const char* Fmt)
        {
            PropertyLabel(Label, Tooltip);
            ImGui::PushID(Label);
            ImGui::DragFloat("##v", &Value, 0.001f, Min, Max, Fmt);
            ImGui::PopID();
        }

        void DrawOptionsSection(FMeshImportOptions& Options)
        {
            if (!ImGui::CollapsingHeader("Import Options", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            if (BeginPropertyTable("OptionsTable"))
            {
                CheckboxRow("Import Meshes",     "Import skeletal and static meshes from the source file.", Options.bImportMeshes);
                CheckboxRow("Import Skeletons",  "Import skeleton hierarchies and bone data.",              Options.bImportSkeleton);
                CheckboxRow("Import Animations", "Import skeletal and morph target animations.",            Options.bImportAnimations);
                CheckboxRow("Import Materials",  "Import material definitions and create material assets.", Options.bImportMaterials);
                if (!Options.bImportMaterials)
                {
                    CheckboxRow("Import Textures", "Import texture files referenced by the source.", Options.bImportTextures);
                }

                DragFloatRow("Scale", "Uniform scale factor applied to all imported geometry.",
                             Options.Scale, 0.001f, 100.0f, "%.3f");
                CheckboxRow("Flip UVs",     "Flip UV coordinates vertically (1 - V).",                   Options.bFlipUVs);
                CheckboxRow("Flip Normals", "Invert mesh normals (useful for inside-out geometry).",     Options.bFlipNormals);

                CheckboxRow("Optimize Mesh",
                            "Optimize vertex cache locality and reduce overdraw for better runtime performance.",
                            Options.bOptimize);
                CheckboxRow("Merge Meshes",
                            "Combine every mesh in the source file into a single asset. Primitives "
                            "that share a source material are folded onto the same material slot.",
                            Options.bMergeMeshes);

                ImGui::EndTable();
            }
            ImGui::Spacing();
        }

        void DrawMeshStats(const FMeshImportData& Data)
        {
            if (Data.Resources.empty())
            {
                return;
            }

            FFixedString Header(FFixedString::CtorSprintf(), "Meshes (%zu)###MeshStats", Data.Resources.size());
            if (!ImGui::CollapsingHeader(Header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            constexpr ImGuiTableFlags Flags =
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

            const float Height = eastl::min<float>(180.0f, (Data.Resources.size() + 1) * ImGui::GetTextLineHeightWithSpacing() + 8.0f);
            if (ImGui::BeginTable("MeshStatsTable", 6, Flags, ImVec2(0, Height)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Verts",    ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Indices",  ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Surfaces", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Overdraw", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("V-Fetch",  ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < Data.Resources.size(); ++i)
                {
                    const FMeshResource& R = *Data.Resources[i];
                    const auto& Overdraw = Data.MeshStatistics.OverdrawStatics[i];
                    const auto& Fetch    = Data.MeshStatistics.VertexFetchStatics[i];

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(R.Name.c_str());
                    ImGui::TableNextColumn(); ImGuiX::Text("{0}", ImGuiX::FormatSize(R.GetNumVertices()));
                    ImGui::TableNextColumn(); ImGuiX::Text("{0}", ImGuiX::FormatSize(R.Indices.size()));
                    ImGui::TableNextColumn(); ImGuiX::Text("{0}", R.GeometrySurfaces.size());

                    ImGui::TableNextColumn();
                    if (Overdraw.overdraw > 2.0f) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.45f, 1));
                    ImGuiX::Text("{:.2f}", Overdraw.overdraw);
                    if (Overdraw.overdraw > 2.0f) ImGui::PopStyleColor();

                    ImGui::TableNextColumn();
                    if (Fetch.overfetch > 2.0f) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.45f, 1));
                    ImGuiX::Text("{:.2f}", Fetch.overfetch);
                    if (Fetch.overfetch > 2.0f) ImGui::PopStyleColor();
                }

                ImGui::EndTable();
            }
            ImGui::Spacing();
        }

        void DrawTexturesPreview(const FMeshImportData& Data)
        {
            if (Data.Textures.empty())
            {
                return;
            }

            FFixedString Header(FFixedString::CtorSprintf(), "Textures (%zu)###Textures", Data.Textures.size());
            if (!ImGui::CollapsingHeader(Header.c_str()))
            {
                return;
            }

            TVector<FMeshImportImage> Images;
            Images.assign(Data.Textures.begin(), Data.Textures.end());

            constexpr float ThumbSize = 64.0f;
            constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner;
            if (ImGui::BeginTable("TextureTable", 2, Flags))
            {
                ImGui::TableSetupColumn("##thumb", ImGuiTableColumnFlags_WidthFixed, ThumbSize + 8);
                ImGui::TableSetupColumn("Path",    ImGuiTableColumnFlags_WidthStretch);

                ImGuiListClipper Clipper;
                Clipper.Begin((int)Images.size(), ThumbSize + 8);
                while (Clipper.Step())
                {
                    for (int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i)
                    {
                        const FMeshImportImage& Img = Images[i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (Img.DisplayImage)
                        {
                            ImGui::Image(ImGuiX::ToImTextureRef(Img.DisplayImage), ImVec2(ThumbSize, ThumbSize));
                        }
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGuiX::TextWrapped("{0}", Img.RelativePath);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
        }

        void DrawSkeletonsPreview(FMeshImportData& Data)
        {
            if (Data.Skeletons.empty())
            {
                return;
            }

            FFixedString Header(FFixedString::CtorSprintf(), "Skeletons (%zu)###Skeletons", Data.Skeletons.size());
            if (!ImGui::CollapsingHeader(Header.c_str()))
            {
                return;
            }

            for (TUniquePtr<FSkeletonResource>& Skeleton : Data.Skeletons)
            {
                ImGui::PushID(Skeleton.get());

                bool bImport = Skeleton->bShouldImport;
                if (ImGui::Checkbox("##import", &bImport))
                {
                    Skeleton->bShouldImport = bImport;
                }
                ImGui::SameLine();
                if (ImGui::TreeNodeEx(Skeleton->Name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    auto DrawBone = [&](int32 BoneIdx, auto& Self) -> void
                    {
                        const FSkeletonResource::FBoneInfo& Bone = Skeleton->Bones[BoneIdx];
                        TVector<int32> Children = Skeleton->GetChildBones(BoneIdx);
                        const ImGuiTreeNodeFlags Flags = Children.empty()
                            ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                            : ImGuiTreeNodeFlags_None;
                        if (ImGui::TreeNodeEx(Bone.Name.c_str(), Flags))
                        {
                            for (int32 ChildIdx : Children)
                            {
                                Self(ChildIdx, Self);
                            }
                            if (!Children.empty())
                            {
                                ImGui::TreePop();
                            }
                        }
                    };
                    for (int32 RootIdx : Skeleton->GetRootBones())
                    {
                        DrawBone(RootIdx, DrawBone);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
        }

        void DrawAnimationsPreview(const FMeshImportData& Data)
        {
            if (Data.Animations.empty())
            {
                return;
            }

            FFixedString Header(FFixedString::CtorSprintf(), "Animations (%zu)###Animations", Data.Animations.size());
            if (!ImGui::CollapsingHeader(Header.c_str()))
            {
                return;
            }

            for (size_t i = 0; i < Data.Animations.size(); ++i)
            {
                const FAnimationResource& Anim = *Data.Animations[i];
                ImGui::PushID((int)i);
                if (ImGui::TreeNodeEx(Anim.Name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    ImGui::TextDisabled("Duration: %.2fs   Channels: %zu", Anim.Duration, Anim.Channels.size());
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
        }

        bool DrawDialogButtons()
        {
            ImGui::Separator();
            ImGui::Spacing();

            constexpr float ButtonWidth = 110.0f;
            const float Spacing = ImGui::GetStyle().ItemSpacing.x;
            const float Total = ButtonWidth * 2 + Spacing;
            const float Avail = ImGui::GetContentRegionAvail().x;
            if (Avail > Total)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Avail - Total);
            }

            const bool bImport = ImGui::Button("Import", ImVec2(ButtonWidth, 0));
            ImGui::SameLine();
            return bImport;
        }
    }

    bool CMeshFactory::DrawImportDialogue(const FFixedString& RawPath, const FFixedString& DestinationPath, TUniquePtr<Import::FImportSettings>& ImportSettings, bool& bShouldClose)
    {
        using namespace Import::Mesh;

        static FMeshImportOptions Options;

        if (ImGui::IsWindowAppearing())
        {
            ImportSettings = MakeUnique<FMeshImportData>();
            if (!PreviewParse(RawPath, static_cast<FMeshImportData&>(*ImportSettings)))
            {
                bShouldClose = true;
                return false;
            }
        }

        FMeshImportData* ImportedData = static_cast<FMeshImportData*>(ImportSettings.get());

        ImGui::TextDisabled("Source");
        ImGui::SameLine();
        ImGui::TextUnformatted(VFS::FileName(RawPath).data());
        ImGui::Spacing();

        DrawOptionsSection(Options);
        DrawMeshStats(*ImportedData);
        DrawTexturesPreview(*ImportedData);
        DrawSkeletonsPreview(*ImportedData);
        DrawAnimationsPreview(*ImportedData);

        const bool bImport = DrawDialogButtons();
        if (bImport)
        {
            // Hand the user's chosen options off to TryImport via the
            // settings payload so commit-time finalization can apply them.
            ImportedData->CommitOptions = Options;
            bShouldClose = true;
            return true;
        }
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0)))
        {
            bShouldClose = true;
        }
        return false;
    }
    
    void CMeshFactory::TryImport(const FFixedString& RawPath, const FFixedString& DestinationPath, const Import::FImportSettings* Settings)
    {
        using namespace Import::Mesh;

        // The settings payload holds the raw preview parse plus the user's
        // final options (CommitOptions). Finalize in place: apply user
        // transforms, optionally merge, then run the heavy optimize/shadow/
        // meshlet/stats passes once with the final geometry.
        FMeshImportData& ImportData = const_cast<FMeshImportData&>(Settings->As<FMeshImportData>());
        const FMeshImportOptions& Options = ImportData.CommitOptions;
        FinalizeMeshImportData(ImportData, Options);

        FFixedString DestinationDir;
        FFixedString BaseName;
        const size_t LastSlashPos = DestinationPath.find_last_of('/');
        if (LastSlashPos == FFixedString::npos)
        {
            BaseName = DestinationPath;
        }
        else
        {
            DestinationDir = DestinationPath.substr(0, LastSlashPos + 1);
            BaseName       = DestinationPath.substr(LastSlashPos + 1, FFixedString::npos);
        }
        // Strip the source-file extension (.fbx/.gltf/...) from BaseName so
        // the resulting asset paths don't end up with the wrong extension.
        const size_t DotPos = BaseName.find_last_of('.');
        if (DotPos != FFixedString::npos)
        {
            BaseName = BaseName.substr(0, DotPos);
        }

        auto BuildPath = [&](FStringView Suffix) -> FFixedString
        {
            FFixedString Path = DestinationDir;
            Path.append(BaseName);
            if (!Suffix.empty())
            {
                Path.append("_");
                Path.append_convert(Suffix.data(), Suffix.length());
            }
            return Path;
        };

        // Avoid clobbering existing assets when the user is reimporting next
        // to a previous import, or when sub-asset names happen to collide.
        auto EnsureUniquePath = [](FFixedString Path) -> FFixedString
        {
            if (FindObject<CPackage>(Path) == nullptr)
            {
                return Path;
            }
            for (uint32 N = 1; N < 10000; ++N)
            {
                FFixedString Candidate = Path;
                Candidate.append("_");
                Candidate.append_convert(eastl::to_string(N));
                if (FindObject<CPackage>(Candidate) == nullptr)
                {
                    return Candidate;
                }
            }
            return Path;
        };
        
        TVector<CObject*> CreatedObjects;
        CreatedObjects.reserve(ImportData.Skeletons.size() + ImportData.Resources.size() + ImportData.Animations.size());

        TObjectPtr<CSkeleton> PrimarySkeleton;
        const bool bMultipleSkeletons = ImportData.Skeletons.size() > 1;

        for (size_t i = 0; Options.bImportSkeleton && i < ImportData.Skeletons.size(); ++i)
        {
            TUniquePtr<FSkeletonResource>& SkeletonRes = const_cast<TUniquePtr<FSkeletonResource>&>(ImportData.Skeletons[i]);
            if (!SkeletonRes || !SkeletonRes->bShouldImport)
            {
                continue;
            }

            // Multiple skeletons in one source file is rare but legal (e.g.
            // an FBX with several rigs); disambiguate by internal name.
            FFixedString SkeletonPath = bMultipleSkeletons
                ? BuildPath(SkeletonRes->Name.ToString())
                : BuildPath("Skeleton");
            SkeletonPath = EnsureUniquePath(SkeletonPath);

            CSkeleton* NewSkeleton = CreateNewOf<CSkeleton>(SkeletonPath);
            NewSkeleton->SetFlag(OF_NeedsPostLoad);
            NewSkeleton->SkeletonResource = Move(SkeletonRes);

            if (!PrimarySkeleton)
            {
                PrimarySkeleton = NewSkeleton;
            }
            CreatedObjects.push_back(NewSkeleton);
        }

        const bool bMultipleMeshes = ImportData.Resources.size() > 1;
        for (size_t i = 0; Options.bImportMeshes && i < ImportData.Resources.size(); ++i)
        {
            TUniquePtr<FMeshResource>& MeshResource = const_cast<TUniquePtr<FMeshResource>&>(ImportData.Resources[i]);
            if (!MeshResource)
            {
                continue;
            }


            FFixedString MeshPath = bMultipleMeshes
                ? BuildPath(MeshResource->Name.ToString())
                : BuildPath({});
            MeshPath = EnsureUniquePath(MeshPath);

            CMesh* NewMesh = nullptr;
            if (!MeshResource->bSkinnedMesh)
            {
                NewMesh = CreateNewOf<CStaticMesh>(MeshPath);
            }
            else
            {
                CSkeletalMesh* NewSkeletalMesh = CreateNewOf<CSkeletalMesh>(MeshPath);
                if (PrimarySkeleton)
                {
                    NewSkeletalMesh->Skeleton = PrimarySkeleton;
                    if (!PrimarySkeleton->PreviewMesh)
                    {
                        PrimarySkeleton->PreviewMesh = NewSkeletalMesh;
                    }
                }
                NewMesh = NewSkeletalMesh;
            }

            NewMesh->SetFlag(OF_NeedsPostLoad);


            // Size the material slot array from the highest referenced
            // surface index, not the surface count: merge-mode imports
            // dedup primitives onto shared slots, and any importer that
            // references a sparse source index needs every slot up to it.
            size_t MaterialSlotCount = 0;
            bool   bAnyExplicitMaterial = false;
            for (const FGeometrySurface& Surface : MeshResource->GeometrySurfaces)
            {
                if (Surface.MaterialIndex >= 0)
                {
                    bAnyExplicitMaterial = true;
                    MaterialSlotCount = eastl::max(MaterialSlotCount, (size_t)Surface.MaterialIndex + 1);
                }
            }
            if (!bAnyExplicitMaterial)
            {
                MaterialSlotCount = MeshResource->GeometrySurfaces.size();
            }
            NewMesh->Materials.clear();
            NewMesh->Materials.resize(MaterialSlotCount);

            NewMesh->MeshResources = Move(MeshResource);
            CreatedObjects.push_back(NewMesh);
        }

        const bool bMultipleAnims = ImportData.Animations.size() > 1;
        for (size_t i = 0; Options.bImportAnimations && i < ImportData.Animations.size(); ++i)
        {
            TUniquePtr<FAnimationResource>& Clip = const_cast<TUniquePtr<FAnimationResource>&>(ImportData.Animations[i]);
            if (!Clip)
            {
                continue;
            }

            FFixedString AnimPath = bMultipleAnims
                ? BuildPath(Clip->Name.ToString())
                : BuildPath("Animation");
            AnimPath = EnsureUniquePath(AnimPath);

            CAnimation* NewAnimation = CreateNewOf<CAnimation>(AnimPath);
            NewAnimation->SetFlag(OF_NeedsPostLoad);
            NewAnimation->AnimationResource = Move(Clip);
            NewAnimation->Skeleton = PrimarySkeleton;

            CreatedObjects.push_back(NewAnimation);
        }
        
        if (Options.bImportTextures && !ImportData.Textures.empty())
        {
            TVector<FMeshImportImage> Images(ImportData.Textures.begin(), ImportData.Textures.end());
            CTextureFactory* TextureFactory = CTextureFactory::StaticClass()->GetDefaultObject<CTextureFactory>();

            for (const FMeshImportImage& Texture : Images)
            {
                if (Texture.IsBytes())
                {
                    FFixedString QualifiedPath = Paths::Combine(Paths::Parent(DestinationPath), Texture.RelativePath);
                    if (!FindObject<CPackage>(QualifiedPath))
                    {
                        CPackage::AddPackageExt(QualifiedPath);
                        TextureFactory->Import({}, QualifiedPath, &Texture);
                    }
                }
                else
                {
                    FStringView ParentPath = VFS::Parent(RawPath, true);
                    FFixedString TexturePath;
                    TexturePath.append_convert(ParentPath.data(), ParentPath.length()).append("/").append_convert(Texture.RelativePath);
                    FStringView TextureFileName = VFS::FileName(TexturePath, true);

                    FFixedString QualifiedPath = DestinationDir;
                    QualifiedPath.append_convert(TextureFileName.data(), TextureFileName.length());

                    if (!FindObject<CPackage>(QualifiedPath))
                    {
                        CPackage::AddPackageExt(QualifiedPath);
                        // Pass the mesh-import metadata so IntendedColorSpace
                        // (set by GLTFImport from material slots) survives
                        // the trip to the texture factory. Without it, the
                        // file-path branch was forced to fall back to the
                        // filename heuristic, which misses glTF-style names
                        // like "Default_metalRoughness.jpg".
                        TextureFactory->Import(TexturePath, QualifiedPath, &Texture);
                    }
                }
            }
        }
        
        for (CObject* Obj : CreatedObjects)
        {
            CPackage* Package = Obj->GetPackage();
            if (CPackage::SavePackage(Package, Package->GetPackagePath()))
            {
                FAssetRegistry::Get().AssetCreated(Obj);
            }
            else
            {
                LOG_ERROR("MeshFactory: failed to save {}; asset will not be registered", Package->GetPackagePath());
            }
        }
        
        for (auto It = CreatedObjects.rbegin(); It != CreatedObjects.rend(); ++It)
        {
            (*It)->ForceDestroyNow();
        }
    }
}
