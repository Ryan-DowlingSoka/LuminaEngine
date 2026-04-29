#include "TextureEditorTool.h"

#include "Assets/AssetTypes/Textures/Texture.h"
#include "Assets/Factories/TextureFactory/TextureFactory.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "Core/Object/Package/Thumbnail/PackageThumbnail.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    const char* TexturePreviewName           = "TexturePreview";
    const char* TexturePropertiesName        = "TextureProperties";

    void FTextureEditorTool::OnInitialize()
    {
        CreateToolWindow(TexturePreviewName, [&](bool bFocused)
        {
            CTexture* Texture = Cast<CTexture>(Asset.Get());
            if (!Texture || Texture->TextureResource == nullptr)
            {
                return;
            }

            ImTextureID TextureID = GRenderManager->GetImGuiRenderer()->GetOrCreateImTexture(Texture->TextureResource->RHIImage, FTextureSubresourceSet(CurrentMipLevel, 1, 0, 1));

            const FRHIImageDesc& ImageDesc = Texture->TextureResource->ImageDescription;
            ImVec2 WindowSize = ImGui::GetContentRegionAvail();
            ImVec2 WindowPos = ImGui::GetCursorScreenPos();

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
            {
                float Wheel = ImGui::GetIO().MouseWheel;
                if (Wheel != 0.0f)
                {
                    float ZoomSpeed = ImGui::GetIO().KeyCtrl ? 0.025f : 0.25f;
                    ZoomFactor += Wheel * ZoomSpeed;
                    ZoomFactor = ImClamp(ZoomFactor, 0.1f, 20.0f);
                }

                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                {
                    ImVec2 MouseDelta = ImGui::GetIO().MouseDelta;
                    PanOffset.x += MouseDelta.x;
                    PanOffset.y += MouseDelta.y;
                }

                // Reset view on double-click
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    ZoomFactor = 1.0f;
                    PanOffset = ImVec2(0, 0);
                }
            }

            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            const int CheckerSize = 16;
            const ImU32 CheckerColor1 = IM_COL32(50, 50, 55, 255);
            const ImU32 CheckerColor2 = IM_COL32(40, 40, 45, 255);

            for (int y = 0; y < (int)WindowSize.y; y += CheckerSize)
            {
                for (int x = 0; x < (int)WindowSize.x; x += CheckerSize)
                {
                    bool isEven = ((x / CheckerSize) + (y / CheckerSize)) % 2 == 0;
                    ImU32 color = isEven ? CheckerColor1 : CheckerColor2;
                    DrawList->AddRectFilled(ImVec2(WindowPos.x + x, WindowPos.y + y), ImVec2(WindowPos.x + x + CheckerSize, WindowPos.y + y + CheckerSize), color);
                }
            }

            ImVec2 TextureSize = ImVec2((float)ImageDesc.Extent.x, (float)ImageDesc.Extent.y);
            ImVec2 ScaledSize = ImVec2(TextureSize.x * ZoomFactor, TextureSize.y * ZoomFactor);
            ImVec2 CenterPos = ImVec2(WindowPos.x + (WindowSize.x - ScaledSize.x) * 0.5f + PanOffset.x,WindowPos.y + (WindowSize.y - ScaledSize.y) * 0.5f + PanOffset.y);

            // Draw border around texture
            DrawList->AddRect(
                ImVec2(CenterPos.x - 1, CenterPos.y - 1),
                ImVec2(CenterPos.x + ScaledSize.x + 1, CenterPos.y + ScaledSize.y + 1),
                IM_COL32(100, 100, 120, 255),
                0.0f,
                0,
                2.0f
            );

            // ImGui's draw shader doesn't tone-map; HDR values >1 clip
            // to white in the LDR swap chain. Apply the exposure stops
            // as a per-channel tint multiplier so the user can dim the
            // preview to recover detail in bright regions. For LDR
            // formats the slider stays at 0 stops (mul = 1) so the
            // preview is unchanged.
            const float ExposureMul = std::pow(2.0f, ExposureStops);
            const int TintByte = std::clamp((int)std::round(ExposureMul * 255.0f), 0, 255);
            DrawList->AddImage(
                TextureID,
                CenterPos,
                ImVec2(CenterPos.x + ScaledSize.x, CenterPos.y + ScaledSize.y),
                ImVec2(0, 0),
                ImVec2(1, 1),
                IM_COL32(TintByte, TintByte, TintByte, 255)
            );

            ImVec2 MousePos = ImGui::GetMousePos();
            bool isOverTexture = MousePos.x >= CenterPos.x && MousePos.x <= CenterPos.x + ScaledSize.x &&
                MousePos.y >= CenterPos.y && MousePos.y <= CenterPos.y + ScaledSize.y;

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && isOverTexture)
            {
                float NormX = (MousePos.x - CenterPos.x) / ScaledSize.x;
                float NormY = (MousePos.y - CenterPos.y) / ScaledSize.y;
                int PixelX = (int)(NormX * TextureSize.x);
                int PixelY = (int)(NormY * TextureSize.y);

                // Draw crosshair
                const ImU32 CrosshairColor = IM_COL32(255, 255, 255, 180);
                DrawList->AddLine(ImVec2(CenterPos.x, MousePos.y), ImVec2(CenterPos.x + ScaledSize.x, MousePos.y), CrosshairColor);
                DrawList->AddLine(ImVec2(MousePos.x, CenterPos.y), ImVec2(MousePos.x, CenterPos.y + ScaledSize.y), CrosshairColor);

                // Pixel info tooltip
                ImGui::BeginTooltip();
                ImGui::Text("Pixel: [%d, %d]", PixelX, PixelY);
                ImGui::Text("UV: [%.3f, %.3f]", NormX, NormY);
                ImGui::EndTooltip();
            }

            ImGui::SetCursorScreenPos(ImVec2(WindowPos.x + 10, WindowPos.y + 10));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 25, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));

            if (ImGui::BeginChild("##Toolbar", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                // Mip level selector
                if (ImageDesc.NumMips > 1)
                {
                    ImGui::Text("Mip Level:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    ImGui::SliderInt("##MipLevel", &CurrentMipLevel, 0, ImageDesc.NumMips - 1);
                }

                // HDR preview exposure: only meaningful for float-format
                // textures (Environment-cooked HDRIs). Hidden for LDR
                // textures so the toolbar doesn't gain unused controls.
                const EFormat Fmt = ImageDesc.Format;
                const bool bIsHDRPreview =
                    Fmt == EFormat::RGBA16_FLOAT || Fmt == EFormat::RGBA32_FLOAT ||
                    Fmt == EFormat::R11G11B10_FLOAT;
                if (bIsHDRPreview)
                {
                    if (ImageDesc.NumMips > 1)
                    {
                        ImGui::SameLine(0, 20);
                    }
                    ImGui::Text("Exposure:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140);
                    ImGui::SliderFloat("##Exposure", &ExposureStops, -8.0f, 4.0f, "%+.1f stops");
                    ImGuiX::TextTooltip("ImGui can't tone-map; HDR values >1 clip to white. Dial down to see detail in bright sky regions.");
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(WindowPos.x + 10, WindowPos.y + WindowSize.y - 35));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 25, 230));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));

            if (ImGui::BeginChild("##InfoBar", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                ImGui::Text("%.0f%% | %dx%d | Pan: [%.0f, %.0f]",
                    ZoomFactor * 100.0f,
                    (int)TextureSize.x,
                    (int)TextureSize.y,
                    PanOffset.x,
                    PanOffset.y
                );
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "RMB/MMB: Pan | Scroll: Zoom | Ctrl+Scroll: Fine Zoom | Double-Click: Reset");
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        });
    
        CreateToolWindow(TexturePropertiesName, [&](bool bFocused)
        {
            CTexture* Texture = Cast<CTexture>(Asset.Get());
            if (!Texture)
            {
                return;
            }
        
            const FRHIImageDesc& ImageDesc = Texture->TextureResource->ImageDescription;
        
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", Texture->GetName().c_str());
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
        
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Texture Information");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
        
            if (ImGui::BeginTable("##TextureInfo", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
            
                auto PropertyRow = [](const char* label, const FString& value, const ImVec4* color = nullptr)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(label);
                    ImGui::TableSetColumnIndex(1);
                    if (color)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, *color);
                    }
                    ImGui::TextUnformatted(value.c_str());
                    if (color)
                    {
                        ImGui::PopStyleColor();
                    }
                };
            
                // Type
                FString dimensionStr;
                switch (ImageDesc.Dimension)
                {
                    case EImageDimension::Texture2D: dimensionStr = "2D Texture"; break;
                    case EImageDimension::Texture3D: dimensionStr = "3D Volume"; break;
                    case EImageDimension::TextureCube: dimensionStr = "Cubemap"; break;
                    default: dimensionStr = "Unknown"; break;
                }
                
                ImVec4 dimensionColor(0.5f, 0.9f, 0.5f, 1.0f);
                PropertyRow("Type", dimensionStr, &dimensionColor);
                PropertyRow("Resolution", eastl::to_string(ImageDesc.Extent.x) + " x " + eastl::to_string(ImageDesc.Extent.y));
                
                if (ImageDesc.Depth > 1)
                {
                    PropertyRow("Depth", eastl::to_string(ImageDesc.Depth));
                }
                
                if (ImageDesc.ArraySize > 1)
                {
                    ImVec4 arrayColor(0.9f, 0.7f, 0.4f, 1.0f);
                    PropertyRow("Array Size", eastl::to_string(ImageDesc.ArraySize), &arrayColor);
                }
            
                // Format
                const FFormatInfo& FormatInfo = RHI::Format::Info(ImageDesc.Format);
                PropertyRow("Pixel Format", FString(FormatInfo.Name));
                
                // Memory
                size_t MemorySizeBytes = Texture->TextureResource->CalcTotalSizeBytes();
                size_t MemorySizeKB = MemorySizeBytes / 1024;
                size_t MemorySizeMB = MemorySizeKB / 1024;
                
                FString memoryStr;
                ImVec4 memoryColor(0.7f, 1.0f, 0.7f, 1.0f);
                
                if (MemorySizeMB > 0)
                {
                    memoryStr = eastl::to_string(MemorySizeMB) + " MB";
                    if (MemorySizeMB > 10)  memoryColor = ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
                    if (MemorySizeMB > 50)  memoryColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                }
                else
                {
                    memoryStr = eastl::to_string(MemorySizeKB) + " KB";
                }
                
                PropertyRow("Memory Size", memoryStr, &memoryColor);
            
                ImGui::EndTable();
            }
        
            ImGui::Spacing();
            ImGui::Spacing();
        
            // === Mipmap Chain ===
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Mipmap Chain");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
        
            if (ImageDesc.NumMips > 1)
            {
                // Summary stats
                uint64 totalMipMemory = 0;
                uint64 baseMipMemory = 0;
                
                if (!Texture->TextureResource->Mips.empty())
                {
                    baseMipMemory = Texture->TextureResource->Mips[0].Pixels.size();
                    for (const auto& Mip : Texture->TextureResource->Mips)
                    {
                        totalMipMemory += Mip.Pixels.size();
                    }
                }
                
                float mipOverhead = baseMipMemory > 0 ? ((float)(totalMipMemory - baseMipMemory) / (float)baseMipMemory * 100.0f) : 0.0f;
                
                ImGui::Text("Total Mip Levels: ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "%u", ImageDesc.NumMips);
                
                ImGui::Text("Memory Overhead: ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1.0f), "%.1f%%", mipOverhead);
                ImGui::SameLine();
                ImGui::TextDisabled("(~33%% is typical)");
        
                ImGui::Spacing();
        
                // Mip level table
                if (ImGui::BeginTable("##MipLevels", 5, 
                    ImGuiTableFlags_Borders | 
                    ImGuiTableFlags_RowBg | 
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_SizingFixedFit,
                    ImVec2(0.0f, eastl::min(300.0f, ImGui::GetContentRegionAvail().y * 0.5f))))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                    ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Texels", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("% of Total", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableHeadersRow();
        
                    for (uint32 i = 0; i < ImageDesc.NumMips; ++i)
                    {
                        ImGui::TableNextRow();
                        
                        uint32 mipWidth = eastl::max<uint32>(1u, ImageDesc.Extent.x >> i);
                        uint32 mipHeight = eastl::max<uint32>(1u, ImageDesc.Extent.y >> i);
                        uint32 mipTexels = mipWidth * mipHeight;
                        
                        // Calculate expected memory size
                        uint32 bytesPerBlock = RHI::Format::BytesPerBlock(ImageDesc.Format);
                        uint64 expectedSize = (uint64)mipWidth * mipHeight * bytesPerBlock;
                        
                        // Get actual size if available
                        uint64 actualSize = expectedSize;
                        if (i < Texture->TextureResource->Mips.size())
                        {
                            actualSize = Texture->TextureResource->Mips[i].Pixels.size();
                        }
                        
                        float percentOfTotal = totalMipMemory > 0 ? ((float)actualSize / (float)totalMipMemory * 100.0f) : 0.0f;
        
                        // Level number
                        ImGui::TableSetColumnIndex(0);
                        if (i == 0)
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "%u", i);
                        }
                        else
                        {
                            ImGui::Text("%u", i);
                        }
        
                        // Resolution
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%ux%u", mipWidth, mipHeight);
        
                        // Texel count
                        ImGui::TableSetColumnIndex(2);
                        if (mipTexels >= 1000000)
                        {
                            ImGui::Text("%.2fM", mipTexels / 1000000.0f);
                        }
                        else if (mipTexels >= 1000)
                        {
                            ImGui::Text("%.1fK", mipTexels / 1000.0f);
                        }
                        else
                        {
                            ImGui::Text("%u", mipTexels);
                        }
        
                        // Size
                        ImGui::TableSetColumnIndex(3);
                        if (actualSize >= 1024 * 1024)
                        {
                            ImGui::Text("%.2f MB", actualSize / (1024.0f * 1024.0f));
                        }
                        else if (actualSize >= 1024)
                        {
                            ImGui::Text("%.1f KB", actualSize / 1024.0f);
                        }
                        else
                        {
                            ImGui::Text("%llu B", actualSize);
                        }
        
                        // Percentage
                        ImGui::TableSetColumnIndex(4);
                        if (i == 0)
                        {
                            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1.0f), "%.1f%%", percentOfTotal);
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%.1f%%", percentOfTotal);
                        }
                    }
        
                    ImGui::EndTable();
                }
            }
            else
            {
                ImGui::TextDisabled("No mipmaps generated (single mip level only)");
            }
        
            ImGui::Spacing();
            ImGui::Spacing();
        
            // === Image Capabilities ===
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Image Capabilities");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
        
            struct FlagInfo
            {
                EImageCreateFlags Flag;
                const char* Name;
                const char* Description;
                ImVec4 Color;
            };
        
            FlagInfo flags[] = 
            {
                { EImageCreateFlags::ShaderResource, "Shader Resource", "Can be sampled in shaders", ImVec4(0.5f, 0.8f, 1.0f, 1.0f) },
                { EImageCreateFlags::RenderTarget, "Render Target", "Can be rendered to", ImVec4(1.0f, 0.6f, 0.3f, 1.0f) },
                { EImageCreateFlags::DepthStencil, "Depth/Stencil", "Used for depth testing", ImVec4(0.6f, 0.4f, 0.9f, 1.0f) },
                { EImageCreateFlags::Storage, "Storage", "Supports storage operations", ImVec4(0.9f, 0.9f, 0.3f, 1.0f) },
                { EImageCreateFlags::UnorderedAccess, "Unordered Access", "UAV/RWTexture support", ImVec4(1.0f, 0.4f, 0.6f, 1.0f) },
                { EImageCreateFlags::InputAttachment, "Input Attachment", "Can be used as input", ImVec4(0.4f, 0.9f, 0.6f, 1.0f) },
                { EImageCreateFlags::CubeCompatible, "Cube Compatible", "Cubemap compatible", ImVec4(0.7f, 0.5f, 1.0f, 1.0f) },
                { EImageCreateFlags::Aliasable, "Aliasable", "Memory can be aliased", ImVec4(0.8f, 0.8f, 0.8f, 1.0f) }
            };
        
            bool anyFlagSet = false;
            for (const auto& flagInfo : flags)
            {
                if (ImageDesc.Flags.IsFlagSet(flagInfo.Flag))
                {
                    anyFlagSet = true;
                    ImGui::PushStyleColor(ImGuiCol_Text, flagInfo.Color);
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::Text("%s", flagInfo.Name);
                    ImGui::PopStyleColor();
                    
                    ImGui::SameLine(200);
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "- %s", flagInfo.Description);
                }
            }
        
            if (!anyFlagSet)
            {
                ImGui::TextDisabled("No special flags set");
            }
        
            ImGui::Spacing();
            ImGui::Spacing();
        
            // === Statistics ===
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Statistics");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
        
            float aspectRatio = (float)ImageDesc.Extent.x / (float)ImageDesc.Extent.y;
            uint32 baseTexels = ImageDesc.Extent.x * ImageDesc.Extent.y * ImageDesc.Depth * ImageDesc.ArraySize;
            
            if (ImGui::BeginTable("##Stats", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);
                
                auto StatRow = [](const char* label, const FString& value)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(value.c_str());
                };
                
                char aspectStr[32];
                snprintf(aspectStr, sizeof(aspectStr), "%.3f:1", aspectRatio);
                StatRow("Aspect Ratio:", aspectStr);
                
                StatRow("Base Mip Texels:", eastl::to_string(baseTexels));
                
                if (ImageDesc.NumMips > 1)
                {
                    uint64 mipChainTexels = 0;
                    for (uint32 i = 0; i < ImageDesc.NumMips; ++i)
                    {
                        uint32 mipWidth = eastl::max<uint32>(1u, ImageDesc.Extent.x >> i);
                        uint32 mipHeight = eastl::max<uint32>(1u, ImageDesc.Extent.y >> i);
                        mipChainTexels += mipWidth * mipHeight;
                    }
                    StatRow("Total Mip Texels:", eastl::to_string(mipChainTexels));
                    
                    float texelIncrease = ((float)mipChainTexels / (float)baseTexels - 1.0f) * 100.0f;
                    char texelIncStr[32];
                    snprintf(texelIncStr, sizeof(texelIncStr), "+%.1f%%", texelIncrease);
                    StatRow("Texel Increase:", texelIncStr);
                }
        
                ImGui::EndTable();
            }
        
            ImGui::Spacing();
            ImGui::Spacing();
            
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Compression");
            ImGuiX::Font::PopFont();

            ImGui::Spacing();

            // Color-space combo + recook. Changing ColorSpace alone doesn't
            // re-encode the bits (the BC stream is baked at import time);
            // the user has to click Recook for the new format/encoder mode
            // to take effect. The active stored format is shown so the user
            // can tell at a glance whether the asset is up-to-date.
            {
                static const char* ColorSpaceLabels[] =
                {
                    "Auto (re-classify on next recook)",
                    "Linear (data, non-color)",
                    "sRGB (color: albedo / emissive / UI)",
                    "Normal Map (BC5, XY + reconstructed Z)",
                    "Packed Data (ORM / MRA / etc.)",
                    "HDR Environment (RGBA16F equirect, no compression)",
                };
                int CurrentIndex = (int)Texture->ColorSpace;
                ImGui::TextUnformatted("Color Space");
                ImGui::SameLine(150);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##ColorSpace", &CurrentIndex, ColorSpaceLabels, IM_ARRAYSIZE(ColorSpaceLabels)))
                {
                    Texture->ColorSpace = (ETextureColorSpace)CurrentIndex;
                    Asset->GetPackage()->MarkDirty();
                }

                ImGui::Spacing();
                if (Texture->SourcePath.empty())
                {
                    ImGui::BeginDisabled();
                    ImGui::Button("Recook (no source path)##Texture", ImVec2(-1, 0));
                    ImGui::EndDisabled();
                    ImGuiX::TextTooltip("This asset wasn't imported from a standalone file (likely embedded in a mesh import), so there's nothing on disk to re-cook from.");
                }
                else
                {
                    if (ImGui::Button("Recook##Texture", ImVec2(-1, 0)))
                    {
                        if (CTextureFactory::Recook(Texture))
                        {
                            ImGuiX::Notifications::NotifySuccess("Recooked '{0}' as {1}",
                                Texture->GetName().c_str(), ColorSpaceLabels[(int)Texture->ColorSpace]);
                        }
                        else
                        {
                            ImGuiX::Notifications::NotifyError("Recook failed for '{0}' -- check log",
                                Texture->GetName().c_str());
                        }
                    }
                    ImGuiX::TextTooltip("Re-run Basis Universal compression with the current Color Space, picking the right BC format and encoder mode. Source: %s", Texture->SourcePath.c_str());
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();

            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Actions");
            ImGuiX::Font::PopFont();

            ImGui::Spacing();
            ImGui::TextDisabled("Transform Tools");
            ImGui::Spacing();
            
            if (ImGui::Button("Flip Vertical##Texture", ImVec2(150, 0)))
            {
                for (FTextureResource::FMip& Mip : Texture->GetTextureResource().Mips)
                {
                    uint8* Pixels           = Mip.Pixels.data();
                    uint32 Height           = Mip.Height;
                    uint32 RowSize          = Mip.RowPitch;
                    
                    TVector<uint8> TempRow(RowSize);

                    for (uint32 Y = 0; Y < Height / 2; Y++)
                    {
                        uint8* Top    = Pixels + Y * RowSize;
                        uint8* Bottom = Pixels + (Height - 1 - Y) * RowSize;
                    
                        memcpy(TempRow.data(), Top, RowSize);
                        memcpy(Top, Bottom, RowSize);
                        memcpy(Bottom, TempRow.data(), RowSize);
                    }
                }
                
                Asset->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            ImGuiX::TextTooltip("Flip the texture along the vertical axis.");
            
            ImGui::SameLine();
            
            if (ImGui::Button("Flip Horizontal##Texture", ImVec2(150, 0)))
            {
                for (FTextureResource::FMip& Mip : Texture->GetTextureResource().Mips)
                {
                    uint8* Pixels        = Mip.Pixels.data();
                    uint32 Width         = Mip.Width;
                    uint32 Height        = Mip.Height;
                    uint32 BytesPerPixel = Mip.RowPitch / Mip.Width;

                    for (uint32 Y = 0; Y < Height; Y++)
                    {
                        for (uint32 X = 0; X < Width / 2; X++)
                        {
                            uint8* Left  = Pixels + (Y * Width + X) * BytesPerPixel;
                            uint8* Right = Pixels + (Y * Width + (Width - 1 - X)) * BytesPerPixel;

                            for (uint32 C = 0; C < BytesPerPixel; C++)
                            {
                                std::swap(Left[C], Right[C]);
                            }
                        }
                    }
                }
                
                Asset->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            
            ImGuiX::TextTooltip("Flip the texture along the horizontal axis.");

            if (ImGui::Button("Export to File...", ImVec2(-1, 0)))
            {
                // TODO: Implement export
            }
            ImGuiX::TextTooltip("Export the texture to disk.");
            
            if (ImGui::Button("Analyze Color Distribution", ImVec2(-1, 0)))
            {
                // TODO: Implement analysis
            }
            ImGuiX::TextTooltip("Analyze the color distribution across all pixels.");
        });
    }

    void FTextureEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FTextureEditorTool::OnAssetLoadFinished()
    {
    }

    void FTextureEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::DrawToolMenu(UpdateContext);
    }

    void FTextureEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGuiID leftDockID = 0, rightDockID = 0, bottomDockID = 0;

        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &rightDockID, &leftDockID);

        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Down, 0.3f, &bottomDockID, &InDockspaceID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(TexturePreviewName).c_str(), leftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(TexturePropertiesName).c_str(), rightDockID);
    }
}
