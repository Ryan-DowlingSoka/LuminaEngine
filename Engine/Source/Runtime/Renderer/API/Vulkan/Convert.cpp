#include "pch.h"
#include "Convert.h"

#include "VulkanCommandList.h"

namespace Lumina::Vk
{
    struct FResourceStateMappingInternal
    {
        EResourceStates         State;
        VkImageLayout           ImageLayout;
        VkPipelineStageFlags2   StageFlags;
        VkAccessFlags2          AccessMask;

        FResourceStateMapping AsResourceStateMapping() const 
        {
            // synchronization2 stages/bits beyond 32-bit range are not safe to cast to legacy types.
            DEBUG_ASSERT((StageFlags & VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT) != VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT);
            DEBUG_ASSERT((AccessMask & VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT) != VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT);
            return FResourceStateMapping(State, static_cast<VkPipelineStageFlags>(StageFlags), static_cast<VkAccessFlags>(AccessMask), ImageLayout);
        }

        FResourceStateMapping2 AsResourceStateMapping2() const
        {
            return FResourceStateMapping2(State, ImageLayout, StageFlags, AccessMask);
        }
    };

    static const FResourceStateMappingInternal GResourceStateMap[] =
    {
        { EResourceStates::Common,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0 },
        { EResourceStates::ConstantBuffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_UNIFORM_READ_BIT },
        { EResourceStates::VertexBuffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT },
        { EResourceStates::IndexBuffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            VK_ACCESS_2_INDEX_READ_BIT},
        { EResourceStates::IndirectArgument,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT},
        { EResourceStates::ShaderResource,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_SHADER_READ_BIT},
        { EResourceStates::UnorderedAccess,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT},
        { EResourceStates::RenderTarget,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT},
        { EResourceStates::DepthWrite,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        { EResourceStates::DepthRead,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT},
        { EResourceStates::StreamOut,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT,
            VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT},
        { EResourceStates::CopyDest,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT},
        { EResourceStates::CopySource,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT },
        { EResourceStates::ResolveDest,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT},
        { EResourceStates::ResolveSource,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT },
        { EResourceStates::Present,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            0},
        { EResourceStates::AccelStructRead,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR},
        { EResourceStates::AccelStructWrite,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR},
        { EResourceStates::AccelStructBuildInput,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR},
        { EResourceStates::AccelStructBuildBlas,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR},
        { EResourceStates::ShadingRateSurface,
            VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
            VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR},
        { EResourceStates::OpacityMicromapWrite,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT,
            VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT},
        { EResourceStates::OpacityMicromapBuildInput,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT,
            VK_ACCESS_2_SHADER_READ_BIT},
    };

    static FResourceStateMappingInternal ConvertResourceStateInternal(EResourceStates state)
    {
        FResourceStateMappingInternal Result = {};

        constexpr uint32 NumStateBits = std::size(GResourceStateMap);

        uint32 StateTmp = uint32(state);
        uint32 BitIndex = 0;

        while (StateTmp != 0 && BitIndex < NumStateBits)
        {
            uint32 Bit = (1 << BitIndex);

            if (StateTmp & Bit)
            {
                const FResourceStateMappingInternal& Mapping = GResourceStateMap[BitIndex];

                DEBUG_ASSERT(uint32(Mapping.State) == Bit);
                DEBUG_ASSERT(Result.ImageLayout == VK_IMAGE_LAYOUT_UNDEFINED || Mapping.ImageLayout == VK_IMAGE_LAYOUT_UNDEFINED || Result.ImageLayout == Mapping.ImageLayout);

                Result.State = EResourceStates(Result.State | Mapping.State);
                Result.AccessMask |= Mapping.AccessMask;
                Result.StageFlags |= Mapping.StageFlags;
                if (Mapping.ImageLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                {
                    Result.ImageLayout = Mapping.ImageLayout;
                }

                StateTmp &= ~Bit;
            }

            BitIndex++;
        }

        DEBUG_ASSERT(Result.State == state);

        return Result;
    }

    FResourceStateMapping2 ConvertResourceState2(EResourceStates State)
    {
        const FResourceStateMappingInternal Mapping = ConvertResourceStateInternal(State);
        return Mapping.AsResourceStateMapping2();
    }

    FVulkanImage::ESubresourceViewType GetTextureViewType(EFormat BindingFormat, EFormat TextureFormat)
    {
        EFormat Format = (BindingFormat == EFormat::UNKNOWN) ? TextureFormat : BindingFormat;

        const FFormatInfo& FormatInfo = RHI::Format::Info(Format);

        if (FormatInfo.bHasDepth)
        {
            return FVulkanImage::ESubresourceViewType::DepthOnly;
        }

        if (FormatInfo.bHasStencil)
        {
            return FVulkanImage::ESubresourceViewType::StencilOnly;
        }
        
        return FVulkanImage::ESubresourceViewType::AllAspects;
    }
    
    FResourceStateMapping ConvertResourceState(EResourceStates State)
    {
        const FResourceStateMappingInternal Mapping = ConvertResourceStateInternal(State);
        return Mapping.AsResourceStateMapping();
    }
}
