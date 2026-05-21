#include "pch.h"
#include "VulkanImGuiRender.h"
#include "imgui.h"
#include "implot.h"
#include "Assets/Factories/TextureFactory/TextureFactory.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "Core/Engine/Engine.h"
#include "Core/Profiler/Profile.h"
#include "Core/Windows/Window.h"
#include "Paths/Paths.h"
#include "Renderer/RHIStaticStates.h"
#include "Renderer/API/Vulkan/VulkanRenderContext.h"
#include "Renderer/API/Vulkan/VulkanSwapchain.h"
#include "Renderer/CommandList.h"
#include "Tools/Import/ImportHelpers.h"

namespace Lumina
{


	static FVulkanImage::ESubresourceViewType GetTextureViewType(EFormat BindingFormat, EFormat TextureFormat)
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
	
    void FVulkanImGuiRender::Initialize()
    {
		IImGuiRenderer::Initialize();
    	LUMINA_PROFILE_SCOPE();

		VulkanRenderContext		= (FVulkanRenderContext*)GRenderContext;
		
        ImGui_ImplGlfw_InitForVulkan(Windowing::GetPrimaryWindowHandle()->GetWindow(), true);

		VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
		
        VkPipelineRenderingCreateInfo RenderPipeline	= {};
        RenderPipeline.sType							= VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        RenderPipeline.pColorAttachmentFormats			= &Format;
        RenderPipeline.colorAttachmentCount				= 1;

    	
        ImGui_ImplVulkan_InitInfo InitInfo		= {};
    	InitInfo.ApiVersion						= VK_API_VERSION_1_3;
		InitInfo.Allocator						= VK_ALLOC_CALLBACK;
        InitInfo.PipelineRenderingCreateInfo	= RenderPipeline;
        InitInfo.Instance						= VulkanRenderContext->GetVulkanInstance();
        InitInfo.PhysicalDevice					= VulkanRenderContext->GetDevice()->GetPhysicalDevice();
        InitInfo.Device							= VulkanRenderContext->GetDevice()->GetDevice();
        InitInfo.Queue							= VulkanRenderContext->GetQueue(ECommandQueue::Graphics)->Queue;
		InitInfo.DescriptorPoolSize				= 1000;
        InitInfo.MinImageCount					= 2;
        // ImageCount drives two ImGui lifetimes: the vtx/idx ring size AND how
        // many frames a texture must be unused before WantDestroy frees its
        // descriptor (backend: UnusedFrames >= ImageCount). UnusedFrames is
        // counted on the game thread, which leads the render thread by up to
        // FRAMES_IN_FLIGHT (snapshot ring depth) and the GPU by that plus the
        // in-flight queue depth. With ImageCount == FRAMES_IN_FLIGHT, an old
        // font atlas (rebuilt on scene switch) gets freed while a submitted
        // command buffer still references it -> VUID-vkFreeDescriptorSets-00309
        // and device loss. Double it so the destroy delay covers the full
        // game-thread-to-GPU-completion gap.
        InitInfo.ImageCount						= FRAMES_IN_FLIGHT * 2;
        InitInfo.UseDynamicRendering			= true;
        InitInfo.MSAASamples					= VK_SAMPLE_COUNT_1_BIT;
		
        bool bInitImGui = ImGui_ImplVulkan_Init(&InitInfo);
		ASSERT(bInitImGui);


		FName SquareTexturePath		= Paths::GetEngineResourceDirectory() + "/Textures/WhiteSquareTexture.png";
		FRHIImageRef RHI			= Import::Textures::CreateTextureFromImport(SquareTexturePath.ToString(), false);
		ImTextureRef ImTex			= ImGuiX::ToImTextureRef(RHI);

		TUniquePtr<FEntry> Entry	= MakeUnique<FEntry>();
		SquareWhiteTexture.first	= SquareTexturePath;
		SquareWhiteTexture.second	= Entry.get();
		Entry->Name					= SquareTexturePath;
		Entry->RHIImage				= RHI;
		Entry->ImTexture			= ImTex;
		Entry->State				= ETextureState::Ready;

		Images.emplace(SquareTexturePath, Move(Entry));
		
    }

    void FVulkanImGuiRender::Deinitialize()
    {
		LOG_INFO("Vulkan ImGui Renderer shutting down with {} images", Images.size());
		FRecursiveScopeLock Lock(Mutex);

		VulkanRenderContext->WaitIdle();

		SquareWhiteTexture = {};
		
		Images.clear();
    	//vkDestroyDescriptorPool(VulkanRenderContext->GetDevice()->GetDevice(), DescriptorPool, VK_ALLOC_CALLBACK);
		DescriptorPool = nullptr;
		
    	ImGui_ImplVulkan_Shutdown();
    	ImGui_ImplGlfw_Shutdown();
		ImPlot::DestroyContext();
    	ClearSnapshots();
    	ImGui::DestroyContext();
    }

    void FVulkanImGuiRender::FillReferencedImagesSnapshot(TVector<FRHIImageRef>& Out)
    {
		FRecursiveScopeLock Lock(Mutex);
		Out.reserve(ReferencedImages.size());
		for (const FRHIImageRef& Image : ReferencedImages)
		{
			Out.push_back(Image);
		}
    }

    void FVulkanImGuiRender::ProcessTextureUpdates_GameThread()
    {
    	LUMINA_PROFILE_SCOPE();

    	// Hold the same Mutex that guards GetOrCreateImTexture (AddTexture) and
    	// the stale sweep (RemoveTexture). UpdateTexture alloc/frees descriptor
    	// sets on the shared backend pool; vkAllocate/Free/UpdateDescriptorSets
    	// require external host synchronization. Without this lock the render
    	// thread's OnEndFrame sweep can mutate the pool concurrently with this
    	// game-thread walk -> corrupted free list -> descriptors alias the wrong
    	// view (corrupt textures) and eventual device lost.
    	FRecursiveScopeLock Lock(Mutex);

    	// Single shared list across all viewports (imgui.cpp sets every
    	// draw_data->Textures to &g.PlatformIO.Textures). Processing it here once
    	// covers the main viewport (rendered later on the render thread) and the
    	// platform windows (rendered just below on this thread). Caller holds the
    	// graphics-queue lock; UpdateTexture submits + waits on that queue.
    	for (ImTextureData* Tex : ImGui::GetPlatformIO().Textures)
    	{
    		if (Tex->Status != ImTextureStatus_OK)
    		{
    			ImGui_ImplVulkan_UpdateTexture(Tex);
    		}
    	}
    }

    void FVulkanImGuiRender::OnStartFrame(const FUpdateContext& UpdateContext)
    {
    	LUMINA_PROFILE_SCOPE();
		
		FRecursiveScopeLock Lock(Mutex);
		SquareWhiteTexture.second->LastUseFrame.exchange(GEngine->GetUpdateContext().GetFrame(), std::memory_order_relaxed);

		ReferencedImages.clear();

		{
			LUMINA_PROFILE_SECTION_COLORED("ImGui_ImplVulkan_NewFrame", tracy::Color::Aquamarine);
			ImGui_ImplVulkan_NewFrame();
		}
		{
			LUMINA_PROFILE_SECTION_COLORED("ImGui_ImplGlfw_NewFrame", tracy::Color::Aquamarine);
			ImGui_ImplGlfw_NewFrame();
		}
		{
			LUMINA_PROFILE_SECTION_COLORED("ImGui::NewFrame", tracy::Color::Aquamarine);
			ImGui::NewFrame();
		}
    }
	
    void FVulkanImGuiRender::OnEndFrame(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot)
    {
		LUMINA_PROFILE_SECTION_COLORED("ImGui Render", tracy::Color::Aquamarine3);

		FRHIImage* EngineViewport = FEngine::GetEngineViewport()->GetRenderTarget();
		CmdList.SetImageState(EngineViewport, AllSubresources, EResourceStates::RenderTarget);
		CmdList.CommitBarriers();

		ImDrawData* DrawData = Snapshot.GetDrawData();
		if (DrawData != nullptr && DrawData->Valid)
		{
			CmdList.DisableAutomaticBarriers();

			FRenderPassDesc::FAttachment Attachment; Attachment
				.SetImage(EngineViewport);

			for (const FRHIImageRef& Image : Snapshot.ReferencedImages)
			{
				if (Image.GetReference() == EngineViewport)
				{
					continue;
				}

				CmdList.SetImageState(Image, AllSubresources, EResourceStates::ShaderResource);
				CmdList.KeepAlive(Image);
			}

			CmdList.CommitBarriers();

			FRenderPassDesc RenderPass; RenderPass
			.AddColorAttachment(Attachment)
			.SetRenderArea(EngineViewport->GetExtent());

			CmdList.BeginRenderPass(RenderPass);
			ImGui_ImplVulkan_RenderDrawData(DrawData, CmdList.GetAPI<VkCommandBuffer>());
			CmdList.EndRenderPass();

			CmdList.EnableAutomaticBarriers();
		}


		// Amortize the stale-entry sweep. With a content browser open, Images
		// can easily hold dozens of thumbnails; walking the whole map every
		// frame burns measurable CPU for a tiny payoff. Once a second is plenty
		// (entries keep FRHIImage refs alive, so this only affects reclamation
		// latency, not correctness).
		constexpr uint64 CleanupIntervalFrames = 60;
		constexpr uint64 UnusedFrameThreshold = 60;

		uint64 CurrentFrame = GEngine->GetUpdateContext().GetFrame();
		if (CurrentFrame - LastCleanupFrame >= CleanupIntervalFrames)
		{
			LastCleanupFrame = CurrentFrame;

			TFixedVector<uint64, 8> ToDelete;

			FRecursiveScopeLock Lock(Mutex);
			for (auto& KVP : Images)
			{
				FEntry* Entry = KVP.second.get();

				if (Entry == SquareWhiteTexture.second)
				{
					continue;
				}

				uint64 LastUse = Entry->LastUseFrame.load(std::memory_order_acquire);
				if (CurrentFrame - LastUse > UnusedFrameThreshold)
				{
					ToDelete.push_back(KVP.first);
				}
			}

			for (uint64 Delete : ToDelete)
			{
				DestroyImTexture(Delete);
			}
		}
    }

	ImTextureID FVulkanImGuiRender::GetOrCreateImTexture(FStringView Path)
	{
		FRecursiveScopeLock Lock(Mutex);

		FName NamePath = Path;
		auto It = Images.find(NamePath);
		
		if (It != Images.end())
		{
			It->second->LastUseFrame.exchange(GEngine->GetUpdateContext().GetFrame(), std::memory_order_relaxed);
			ReferencedImages.push_back(It->second->RHIImage);
			return It->second->ImTexture.GetTexID();
		}

		FRHIImageRef Image = Import::Textures::CreateTextureFromImport(Path, false);
		if (Image == nullptr)
		{
			return SquareWhiteTexture.second->ImTexture.GetTexID();
		}
		
		ReferencedImages.push_back(Image);
		
		const FTextureSubresourceSet Subresource = AllSubresources;
		FVulkanImage::ESubresourceViewType ViewType = GetTextureViewType(EFormat::UNKNOWN, Image->GetDescription().Format);
		VkImageView View = Image.As<FVulkanImage>()->GetSubresourceView(Subresource, Image->GetDescription().Dimension, Image->GetDescription().Format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ViewType).View;
		
		FRHISamplerRef Sampler = TStaticRHISampler<>::GetRHI();
		VkSampler VulkanSampler = Sampler->GetAPI<VkSampler>();

		ImTextureID NewTextureID = (ImTextureID)ImGui_ImplVulkan_AddTexture(VulkanSampler, View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		
		TUniquePtr<FEntry> NewEntry = MakeUnique<FEntry>();
		NewEntry->State				= ETextureState::Ready;
		NewEntry->RHIImage			= Image;
		NewEntry->ImTexture._TexID	= NewTextureID;
		NewEntry->LastUseFrame.exchange(GEngine->GetUpdateContext().GetFrame(), std::memory_order_relaxed);
		
		Images.insert_or_assign(NamePath, Move(NewEntry));
		
		return NewTextureID;
	}


	ImTextureID FVulkanImGuiRender::GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources)
    {
    	if(Image == nullptr)
    	{
    		return 0;
    	}

		// Fast key: (FRHIImage*, subresource). Avoids the per-frame
		// GetSubresourceView call (which takes its own mutex + cache walk) on
		// the warm path. We only resolve the VkImageView on cache miss, or if
		// the image's view for this subresource has changed under us.
		uint64 Key = (uintptr_t)Image;
		Hash::HashCombine(Key, Subresources.BaseMipLevel);
		Hash::HashCombine(Key, Subresources.NumMipLevels);
		Hash::HashCombine(Key, Subresources.BaseArraySlice);
		Hash::HashCombine(Key, Subresources.NumArraySlices);

		const uint64 Frame = GEngine->GetUpdateContext().GetFrame();

		FRecursiveScopeLock Lock(Mutex);

		auto It = Images.find(Key);
		if (It != Images.end())
		{
			FEntry* Entry = It->second.get();
			Entry->LastUseFrame.store(Frame, std::memory_order_relaxed);
			ReferencedImages.push_back(Entry->RHIImage);
			return Entry->ImTexture.GetTexID();
		}

		// Miss: resolve the view and register a descriptor set.
		FVulkanImage::ESubresourceViewType ViewType = GetTextureViewType(EFormat::UNKNOWN, Image->GetDescription().Format);
		VkImageView View = static_cast<FVulkanImage*>(Image)->GetSubresourceView(Subresources, Image->GetDescription().Dimension, Image->GetDescription().Format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ViewType).View;

		FRHISamplerRef Sampler = TStaticRHISampler<>::GetRHI();
		VkSampler VulkanSampler = Sampler->GetAPI<VkSampler>();

		ImTextureID NewTextureID = (ImTextureID)ImGui_ImplVulkan_AddTexture(VulkanSampler, View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		TUniquePtr<FEntry> NewEntry	= MakeUnique<FEntry>();
		NewEntry->State				= ETextureState::Ready;
		NewEntry->RHIImage			= Image;
		NewEntry->ImTexture._TexID	= NewTextureID;
		NewEntry->CachedView		= View;
		NewEntry->LastUseFrame.store(Frame, std::memory_order_relaxed);

		ReferencedImages.push_back(NewEntry->RHIImage);
		Images.insert_or_assign(Key, Move(NewEntry));

		return NewTextureID;
    }

    void FVulkanImGuiRender::DestroyImTexture(uint64 Hash)
    {
		FRecursiveScopeLock Lock(Mutex);

		auto It = Images.find(Hash);
		if (It == Images.end())
		{
			LOG_WARN("ImGuiTexture {} was not found.", Hash);
			return;
		}

		FEntry* EntryToDestroy = It->second.get();
		ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)EntryToDestroy->ImTexture.GetTexID());  // NOLINT(performance-no-int-to-ptr)
		Images.erase(Hash);
    }

}
