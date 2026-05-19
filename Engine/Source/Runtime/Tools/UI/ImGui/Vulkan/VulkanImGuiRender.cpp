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


	const char* GetRHIResourceTypeName(ERHIResourceType type)
	{
		switch (type)
		{
		case RRT_SamplerState:        return "SamplerState";
		case RTT_InputLayout:         return "InputLayout";
		case RRT_BindingLayout:       return "BindingLayout";
		case RRT_BindingSet:          return "BindingSet";
		case RRT_DepthStencilState:   return "DepthStencilState";
		case RRT_BlendState:          return "BlendState";
		case RRT_VertexShader:        return "VertexShader";
		case RRT_PixelShader:         return "PixelShader";
		case RRT_ComputeShader:       return "ComputeShader";
		case RRT_ShaderLibrary:       return "ShaderLibrary";
		case RRT_GraphicsPipeline:    return "GraphicsPipeline";
		case RRT_ComputePipeline:     return "ComputePipeline";
		case RRT_UniformBufferLayout: return "UniformBufferLayout";
		case RRT_UniformBuffer:       return "UniformBuffer";
		case RRT_Buffer:              return "Buffer";
		case RRT_Image:               return "Image";
		case RRT_GPUFence:            return "GPUFence";
		case RRT_Viewport:            return "Viewport";
		case RRT_StagingBuffer:       return "StagingBuffer";
		case RRT_CommandList:         return "CommandList";
		case RRT_DescriptorTable:     return "DescriptorTable";
		default:                      return "Unknown";
		}
	}
	
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
	
	FString VkFormatToString(VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
		case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
		case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
		case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
		case VK_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
		case VK_FORMAT_R32G32B32_SFLOAT: return "VK_FORMAT_R32G32B32_SFLOAT";
		case VK_FORMAT_R32G32_SFLOAT: return "VK_FORMAT_R32G32_SFLOAT";
		case VK_FORMAT_R32_SFLOAT: return "VK_FORMAT_R32_SFLOAT";
		default: return "Unknown Format";
		}
	}
	
	FString VkColorSpaceToString(VkColorSpaceKHR colorSpace)
	{
		switch (colorSpace)
		{
		case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
		case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT: return "VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT";
		case VK_COLOR_SPACE_BT2020_LINEAR_EXT: return "VK_COLOR_SPACE_BT2020_LINEAR_EXT";
		default: return "Unknown ColorSpace";
		}
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
        // Must be >= FRAMES_IN_FLIGHT: ImGui's vtx/idx ring is sized by ImageCount, and
        // frame N+ImageCount overwrites buffer 0 while frame N may still be GPU-in-flight.
        InitInfo.ImageCount						= FRAMES_IN_FLIGHT;
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

	void FVulkanImGuiRender::DrawRenderDebugContents(const FUpdateContext&)
	{
		VkPhysicalDevice physicalDevice = VulkanRenderContext->GetDevice()->GetPhysicalDevice();
		VkPhysicalDeviceFeatures Features;

		vkGetPhysicalDeviceFeatures(physicalDevice, &Features);
		VkPhysicalDeviceProperties props{};

		vkGetPhysicalDeviceProperties(physicalDevice, &props);
		VkPhysicalDeviceMemoryProperties memProps{};

		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
		VmaAllocator Allocator = VulkanRenderContext->GetDevice()->GetAllocator().GetVMA();

		if (ImGui::BeginTabBar("##RendererInfoTabs"))
		{
			if (ImGui::BeginTabItem("Overview"))
			{
				DrawOverviewTab(props, memProps, Allocator);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Memory"))
			{
				DrawMemoryTab(memProps, Allocator);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Resources"))
			{
				DrawResourcesTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Device Info"))
			{
				DrawDeviceInfoTab(props, Features);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
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

    void FVulkanImGuiRender::DrawOverviewTab(const VkPhysicalDeviceProperties& props, const VkPhysicalDeviceMemoryProperties& memProps, VmaAllocator Allocator)
    {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.16f, 0.18f, 1.0f));
		ImGui::BeginChild("DeviceSummary", ImVec2(0, 120), true);
		ImGui::PopStyleColor();
		
		ImGui::Text("GPU: %s", props.deviceName);
		ImGui::SameLine(ImGui::GetWindowWidth() - 180);
		const char* deviceType = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete GPU" :
								 props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "Integrated GPU" : "Other";
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", deviceType);
		
		ImGui::Separator();
		ImGui::Columns(3, nullptr, false);
		
		ImGui::Text("API Version");
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%d.%d.%d", 
			VK_VERSION_MAJOR(props.apiVersion), 
			VK_VERSION_MINOR(props.apiVersion), 
			VK_VERSION_PATCH(props.apiVersion));
		
		ImGui::NextColumn();
		ImGui::Text("Driver Version");
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "0x%X", props.driverVersion);
		
		ImGui::NextColumn();
		ImGui::Text("Vendor/Device ID");
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "0x%04X / 0x%04X", props.vendorID, props.deviceID);
		
		ImGui::Columns(1);
		ImGui::EndChild();
	
		// Memory & Resource Stats
		ImGui::Columns(2, nullptr, false);
	
		// Left: Memory Overview
		ImGui::BeginChild("MemOverview", ImVec2(0, 400), true);
		ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Memory Overview");
		ImGui::Separator();
	
		VmaTotalStatistics stats{};
		vmaCalculateStatistics(Allocator, &stats);
	
		float totalMemoryMB = 0.0f;
		for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
		{
			totalMemoryMB += memProps.memoryHeaps[i].size / (1024.0f * 1024.0f);
		}
	
		float usedMemoryMB = stats.total.statistics.allocationBytes / (1024.0f * 1024.0f);
		float usagePercent = (usedMemoryMB / totalMemoryMB) * 100.0f;
	
		ImGui::Text("Total VRAM: %.0f MB", totalMemoryMB);
		ImGui::Text("Used: %.2f MB (%.1f%%)", usedMemoryMB, usagePercent);
		ImGui::Text("Allocations: %u", stats.total.statistics.allocationCount);
		ImGui::Text("Memory Blocks: %u", stats.total.statistics.blockCount);
	
		ImGui::Spacing();
		
		// Memory usage bar
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
		if (usagePercent > 80.0f)
		{
			ImGui::PopStyleColor(), ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
		}
		else if (usagePercent > 60.0f)
		{
			ImGui::PopStyleColor(), ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
		}

		ImGui::ProgressBar(usagePercent / 100.0f, ImVec2(-1, 30), "");
		ImGui::PopStyleColor();
	
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	
		// Per-heap breakdown
		ImGui::Text("Memory Heaps:");
		for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
		{
			const VkMemoryHeap& heap = memProps.memoryHeaps[i];
			const VmaDetailedStatistics& heapStats = stats.memoryHeap[i];
			
			if (heapStats.statistics.blockCount == 0)
			{
				continue;
			}

			float heapSizeMB = heap.size / (1024.0f * 1024.0f);
			float heapUsedMB = heapStats.statistics.allocationBytes / (1024.0f * 1024.0f);
			float heapPercent = (heapUsedMB / heapSizeMB) * 100.0f;
	
			ImGui::Text("  Heap %u: %.0f MB %s", i, heapSizeMB, 
				heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? "(Device)" : "(Host)");
			ImGui::SameLine(ImGui::GetWindowWidth() - 100);
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%.1f%%", heapPercent);
			
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
			ImGui::ProgressBar(heapPercent / 100.0f, ImVec2(-1, 0));
			ImGui::PopStyleColor();
		}
	
		ImGui::EndChild();
	
		// Right: Resource Overview
		ImGui::NextColumn();
		ImGui::BeginChild("ResourceOverview", ImVec2(0, 400), true);
		ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Resource Allocations");
		ImGui::Separator();
	
		THashMap<ERHIResourceType, TVector<IRHIResource*>> ResourceMap;
		ResourceMap.reserve(RRT_Num);

		FRHIResourceList::ForEach([&](IRHIResource* Resource)
		{
			ResourceMap[Resource->GetResourceType()].push_back(Resource);
		});
		
		ImGui::Text("Total Resources: %u", IRHIResource::GetNumberRHIResources());
		ImGui::Spacing();
	
		// Resource pie chart data
		static ImVector<float> resourceCounts;
		static ImVector<const char*> resourceLabels;
		resourceCounts.clear();
		resourceLabels.clear();
	
		for (int type = (int)RRT_None + 1; type < (int)RRT_Num; ++type)
		{
			auto it = ResourceMap.find((ERHIResourceType)type);
			if (it != ResourceMap.end() && !it->second.empty())
			{
				resourceCounts.push_back((float)it->second.size());
				resourceLabels.push_back(GetRHIResourceTypeName((ERHIResourceType)type));
			}
		}
	
		if (ImPlot::BeginPlot("##ResourcePie", ImVec2(-1, -1), ImPlotFlags_Equal))
		{
			ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
			ImPlot::SetupAxesLimits(-1, 1, -1, 1);
			ImPlot::PlotPieChart(resourceLabels.Data, resourceCounts.Data, resourceCounts.size(), 0.5, 0.5, 0.4, "%.0f", 90);
			ImPlot::EndPlot();
		}
	
		ImGui::EndChild();
		ImGui::Columns(1);
	
		// Swapchain Info
		ImGui::BeginChild("SwapchainInfo", ImVec2(0, 0), true);
		ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Swapchain Configuration");
		ImGui::Separator();
	
		if (FVulkanSwapchain* Swapchain = VulkanRenderContext->GetSwapchain())
		{
			const VkSurfaceFormatKHR& surfaceFormat = Swapchain->GetSurfaceFormat();
			const glm::uvec2& extent = Swapchain->GetSwapchainExtent();
			VkPresentModeKHR presentMode = Swapchain->GetPresentMode();
	
			ImGui::Columns(4, nullptr, false);
			
			ImGui::Text("Resolution");
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%dx%d", extent.x, extent.y);
			
			ImGui::NextColumn();
			ImGui::Text("Present Mode");
			const char* modeStr = presentMode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO (VSync)" :
								  presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "Mailbox" :
								  presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "Immediate" : "Unknown";
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", modeStr);
			
			ImGui::NextColumn();
			ImGui::Text("Format");
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", VkFormatToString(surfaceFormat.format).c_str());
			
			ImGui::NextColumn();
			ImGui::Text("Images");
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%u (%u in flight)", 
				Swapchain->GetImageCount(), Swapchain->GetNumFramesInFlight());
			
			ImGui::Columns(1);
		}
		else
		{
			ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1.0f), "No swapchain available");
		}
	
		ImGui::EndChild();
    }

    void FVulkanImGuiRender::DrawMemoryTab(const VkPhysicalDeviceMemoryProperties& memProps, VmaAllocator Allocator)
    {
		VmaTotalStatistics stats{};
		vmaCalculateStatistics(Allocator, &stats);
		
		VmaBudget Budget = {};
		VulkanRenderContext->GetDevice()->GetAllocator().GetMemoryBudget(&Budget);
		
		// Update history
		static float timeCounter = 0.0f;
		timeCounter += ImGui::GetIO().DeltaTime;
		
		if (VRAMHistory.empty() || timeCounter > 0.1f) // Sample every 100ms
		{
			VRAMHistory.push_back(Budget.usage / (1024.0f * 1024.0f));
			timeCounter = 0.0f;
			
			// Keep last 500 samples (50 seconds at 100ms intervals)
			if (VRAMHistory.size() > 500)
			{
				VRAMHistory.erase(VRAMHistory.begin());
			}
		}
		
		// VRAM Usage Over Time
		ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "VRAM Usage Timeline");
		if (ImPlot::BeginPlot("##VRAMTimeline", ImVec2(-1, 300)))
		{
			ImPlot::SetupAxes("Time (samples)", "VRAM (MB)");
			ImPlot::SetupAxisLimits(ImAxis_X1, 0, VRAMHistory.size(), ImGuiCond_Always);
			//ImPlot::SetupAxisLimits(ImAxis_Y1, 0, ImPlot::AxisL(ImAxis_Y1).Max, ImGuiCond_Once);
			
			ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
			ImPlot::PushStyleColor(ImPlotCol_Fill, ImVec4(0.3f, 0.8f, 0.4f, 0.3f));
			ImPlot::PlotShaded("Used VRAM", VRAMHistory.data(), VRAMHistory.size(), 0.0);
			ImPlot::PlotLine("Used VRAM", VRAMHistory.data(), VRAMHistory.size());
			ImPlot::PopStyleColor(2);
			
			ImPlot::EndPlot();
		}
		
		ImGui::Spacing();
		
		// Per-Heap Detailed Statistics
		ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Memory Heap Details");
		if (ImGui::BeginTable("VmaHeapDetails", 10, 
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | 
			ImGuiTableFlags_ScrollY, ImVec2(0, 300)))
		{
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableSetupColumn("Heap", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("Blocks", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("Allocs", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("Used (MB)", ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableSetupColumn("Unused (MB)", ImGuiTableColumnFlags_WidthFixed, 90);
			ImGui::TableSetupColumn("Min (KB)", ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableSetupColumn("Max (MB)", ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableSetupColumn("Ranges", ImGuiTableColumnFlags_WidthFixed, 70);
			ImGui::TableSetupColumn("Min Empty (KB)", ImGuiTableColumnFlags_WidthFixed, 100);
			ImGui::TableSetupColumn("Max Empty (MB)", ImGuiTableColumnFlags_WidthFixed, 100);
			ImGui::TableHeadersRow();
		
			for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i)
			{
				const VmaDetailedStatistics& heap = stats.memoryHeap[i];
				if (heap.statistics.blockCount == 0 && heap.statistics.allocationCount == 0)
				{
					continue;
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); 
				ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Heap %u", i);
				
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", heap.statistics.blockCount);
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", heap.statistics.allocationCount);
				ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", heap.statistics.allocationBytes / (1024.0f * 1024.0f));
				ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", (heap.statistics.blockBytes - heap.statistics.allocationBytes) / (1024.0f * 1024.0f));
				ImGui::TableSetColumnIndex(5); ImGui::Text(heap.allocationSizeMin == VK_WHOLE_SIZE ? "-" : "%.2f", heap.allocationSizeMin / 1024.0f);
				ImGui::TableSetColumnIndex(6); ImGui::Text("%.2f", heap.allocationSizeMax / (1024.0f * 1024.0f));
				ImGui::TableSetColumnIndex(7); ImGui::Text("%u", heap.unusedRangeCount);
				ImGui::TableSetColumnIndex(8); ImGui::Text(heap.unusedRangeSizeMin == VK_WHOLE_SIZE ? "-" : "%.2f", heap.unusedRangeSizeMin / 1024.0f);
				ImGui::TableSetColumnIndex(9); ImGui::Text("%.2f", heap.unusedRangeSizeMax / (1024.0f * 1024.0f));
			}
			ImGui::EndTable();
		}
		
		ImGui::Spacing();
		
		// Memory Type Breakdown with Bar Charts
		ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Memory Type Allocation");
		
		for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
		{
			const VkMemoryHeap& heap = memProps.memoryHeaps[i];
			const VmaDetailedStatistics& heapStats = stats.memoryHeap[i];
			
			if (heapStats.statistics.blockCount == 0)
			{
				continue;
			}

			float heapSizeMB = heap.size / (1024.0f * 1024.0f);
			float heapUsedMB = heapStats.statistics.allocationBytes / (1024.0f * 1024.0f);
			
			ImGui::Text("Heap %u: %s (%.0f MB)", i, 
				heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? "Device Local" : "Host Visible",
				heapSizeMB);
		
			float values[2] = { heapUsedMB, heapSizeMB - heapUsedMB };
			const char* labels[2] = { "Used", "Free" };
			
			if (ImPlot::BeginPlot(std::format("##HeapBar{}", i).c_str(), ImVec2(-1, 80), ImPlotFlags_NoLegend))
			{
				ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_AutoFit);
				ImPlot::SetupAxisLimits(ImAxis_Y1, 0, heapSizeMB, ImGuiCond_Always);
				ImPlot::PlotBarGroups(labels, values, 2, 1, 0.8, 0, ImPlotBarGroupsFlags_Stacked);
				ImPlot::EndPlot();
			}
		}
    }

    void FVulkanImGuiRender::DrawResourcesTab()
    {
		ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "RHI Resource Tracking");
		ImGui::Separator();
		
		THashMap<ERHIResourceType, TVector<IRHIResource*>> ResourceMap;
		ResourceMap.reserve(RRT_Num);
		
		FRHIResourceList::ForEach([&](IRHIResource* Resource)
		{
			ResourceMap[Resource->GetResourceType()].push_back(Resource);
		});
		
		// Resource count history
		static ImVector<ImVector<float>> resourceHistory;
		static ImVector<ERHIResourceType> trackedTypes;
		static float historyTime = 0.0f;
		historyTime += ImGui::GetIO().DeltaTime;
		
		if (historyTime > 0.2f) // Sample every 200ms
		{
			if (trackedTypes.empty())
			{
				for (int type = (int)RRT_None + 1; type < (int)RRT_Num; ++type)
				{
					trackedTypes.push_back((ERHIResourceType)type);
					resourceHistory.push_back(ImVector<float>());
				}
			}
		
			for (size_t i = 0; i < trackedTypes.size(); ++i)
			{
				auto it = ResourceMap.find(trackedTypes[i]);
				float count = (it != ResourceMap.end()) ? (float)it->second.size() : 0.0f;
				resourceHistory[i].push_back(count);
				
				if (resourceHistory[i].size() > 300) // Keep 60 seconds
				{
					resourceHistory[i].erase(resourceHistory[i].begin());
				}
			}
			historyTime = 0.0f;
		}
		
		// Resource Timeline
		if (ImPlot::BeginPlot("Resource Allocation Timeline", ImVec2(-1, 350)))
		{
			ImPlot::SetupAxes("Time (samples)", "Count", ImPlotAxisFlags_AutoFit);
			ImPlot::SetupAxisLimits(ImAxis_X1, 0, resourceHistory.size(), ImGuiCond_Always);
			ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Outside);
			
			for (size_t i = 0; i < trackedTypes.size(); ++i)
			{
				if (!resourceHistory[i].empty())
				{
					ImPlot::PlotLine(GetRHIResourceTypeName(trackedTypes[i]), resourceHistory[i].Data, resourceHistory[i].size());
				}
			}
			
			ImPlot::EndPlot();
		}
		
		ImGui::Spacing();
		
		// Detailed Resource Table
		if (ImGui::BeginTable("ResourceTable", 2, 
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Resource Type", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 100);
			ImGui::TableHeadersRow();
		
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Total Resources");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%u", IRHIResource::GetNumberRHIResources());
		
			for (int type = (int)RRT_None + 1; type < (int)RRT_Num; ++type)
			{
				auto it = ResourceMap.find((ERHIResourceType)type);
				if (it == ResourceMap.end() || it->second.empty())
				{
					continue;
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", GetRHIResourceTypeName((ERHIResourceType)type));
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%zu", it->second.size());
			}
		
			ImGui::EndTable();
		}
    }

    void FVulkanImGuiRender::DrawDeviceInfoTab(const VkPhysicalDeviceProperties& props, const VkPhysicalDeviceFeatures& Features)
    {
		if (ImGui::BeginTabBar("DeviceInfoTabs"))
		{
			if (ImGui::BeginTabItem("Properties"))
			{
				DrawDeviceProperties(props);
				ImGui::EndTabItem();
			}
		
			if (ImGui::BeginTabItem("Features"))
			{
				DrawDeviceFeatures(Features);
				ImGui::EndTabItem();
			}
		
			if (ImGui::BeginTabItem("Limits"))
			{
				DrawDeviceLimits(props);
				ImGui::EndTabItem();
			}
		
			ImGui::EndTabBar();
		}
    }

    void FVulkanImGuiRender::DrawDeviceProperties(const VkPhysicalDeviceProperties& props)
    {
		if (ImGui::BeginTable("DeviceProps", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			auto Row = [](const char* name, const std::string& value)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
			};

			Row("Device Name", props.deviceName);
			Row("Device Type", props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete GPU" :
							   props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "Integrated GPU" : "Other");
			Row("Vendor ID", std::format("0x{:04X}", props.vendorID));
			Row("Device ID", std::format("0x{:04X}", props.deviceID));
			Row("API Version", std::format("{}.{}.{}", VK_VERSION_MAJOR(props.apiVersion), 
				VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion)));
			Row("Driver Version", std::format("0x{:X}", props.driverVersion));

			ImGui::EndTable();
		}
    }

    void FVulkanImGuiRender::DrawDeviceFeatures(const VkPhysicalDeviceFeatures& Features)
    {
		static char searchBuffer[256] = "";
		ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
		
		if (ImGui::BeginTable("Features", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Supported", ImGuiTableColumnFlags_WidthFixed, 100);
			ImGui::TableHeadersRow();
		
			auto Feature = [&](const char* name, VkBool32 value)
			{
				if (searchBuffer[0] != '\0' && !strstr(name, searchBuffer))
				{
					return;
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextColored(value ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
					value ? "✓ Yes" : "✗ No");
			};
		
			Feature("robustBufferAccess", Features.robustBufferAccess);
			Feature("fullDrawIndexUint32", Features.fullDrawIndexUint32);
			Feature("imageCubeArray", Features.imageCubeArray);
			Feature("independentBlend", Features.independentBlend);
			Feature("geometryShader", Features.geometryShader);
			Feature("tessellationShader", Features.tessellationShader);
			Feature("sampleRateShading", Features.sampleRateShading);
			Feature("dualSrcBlend", Features.dualSrcBlend);
			Feature("logicOp", Features.logicOp);
			Feature("multiDrawIndirect", Features.multiDrawIndirect);
			Feature("drawIndirectFirstInstance", Features.drawIndirectFirstInstance);
			Feature("depthClamp", Features.depthClamp);
			Feature("depthBiasClamp", Features.depthBiasClamp);
			Feature("fillModeNonSolid", Features.fillModeNonSolid);
			Feature("depthBounds", Features.depthBounds);
			Feature("wideLines", Features.wideLines);
			Feature("largePoints", Features.largePoints);
			Feature("alphaToOne", Features.alphaToOne);
			Feature("multiViewport", Features.multiViewport);
			Feature("samplerAnisotropy", Features.samplerAnisotropy);
			Feature("textureCompressionETC2", Features.textureCompressionETC2);
			Feature("textureCompressionASTC_LDR", Features.textureCompressionASTC_LDR);
			Feature("textureCompressionBC", Features.textureCompressionBC);
			Feature("occlusionQueryPrecise", Features.occlusionQueryPrecise);
			Feature("pipelineStatisticsQuery", Features.pipelineStatisticsQuery);
			Feature("vertexPipelineStoresAndAtomics", Features.vertexPipelineStoresAndAtomics);
			Feature("fragmentStoresAndAtomics", Features.fragmentStoresAndAtomics);
			Feature("shaderTessellationAndGeometryPointSize", Features.shaderTessellationAndGeometryPointSize);
			Feature("shaderImageGatherExtended", Features.shaderImageGatherExtended);
			Feature("shaderStorageImageExtendedFormats", Features.shaderStorageImageExtendedFormats);
			Feature("shaderStorageImageMultisample", Features.shaderStorageImageMultisample);
			Feature("shaderStorageImageReadWithoutFormat", Features.shaderStorageImageReadWithoutFormat);
			Feature("shaderStorageImageWriteWithoutFormat", Features.shaderStorageImageWriteWithoutFormat);
			Feature("shaderUniformBufferArrayDynamicIndexing", Features.shaderUniformBufferArrayDynamicIndexing);
			Feature("shaderSampledImageArrayDynamicIndexing", Features.shaderSampledImageArrayDynamicIndexing);
			Feature("shaderStorageBufferArrayDynamicIndexing", Features.shaderStorageBufferArrayDynamicIndexing);
			Feature("shaderStorageImageArrayDynamicIndexing", Features.shaderStorageImageArrayDynamicIndexing);
			Feature("shaderClipDistance", Features.shaderClipDistance);
			Feature("shaderCullDistance", Features.shaderCullDistance);
			Feature("shaderFloat64", Features.shaderFloat64);
			Feature("shaderInt64", Features.shaderInt64);
			Feature("shaderInt16", Features.shaderInt16);
			Feature("shaderResourceResidency", Features.shaderResourceResidency);
			Feature("shaderResourceMinLod", Features.shaderResourceMinLod);
			Feature("sparseBinding", Features.sparseBinding);
			Feature("sparseResidencyBuffer", Features.sparseResidencyBuffer);
			Feature("sparseResidencyImage2D", Features.sparseResidencyImage2D);
			Feature("sparseResidencyImage3D", Features.sparseResidencyImage3D);
			Feature("sparseResidency2Samples", Features.sparseResidency2Samples);
			Feature("sparseResidency4Samples", Features.sparseResidency4Samples);
			Feature("sparseResidency8Samples", Features.sparseResidency8Samples);
			Feature("sparseResidency16Samples", Features.sparseResidency16Samples);
			Feature("sparseResidencyAliased", Features.sparseResidencyAliased);
			Feature("variableMultisampleRate", Features.variableMultisampleRate);
			Feature("inheritedQueries", Features.inheritedQueries);
		
			ImGui::EndTable();
		}
    }

	void FVulkanImGuiRender::DrawDeviceLimits(const VkPhysicalDeviceProperties& props)
	{
		// Create category tabs for better organization
		if (ImGui::BeginTabBar("LimitsTabs"))
		{
			if (ImGui::BeginTabItem("General"))
			{
				DrawGeneralLimits(props.limits);
				ImGui::EndTabItem();
			}
		
			if (ImGui::BeginTabItem("Buffers & Images"))
			{
				DrawBufferImageLimits(props.limits);
				ImGui::EndTabItem();
			}
		
			if (ImGui::BeginTabItem("Compute"))
			{
				DrawComputeLimits(props.limits);
				ImGui::EndTabItem();
			}
		
			if (ImGui::BeginTabItem("Descriptors"))
			{
				DrawDescriptorLimits(props.limits);
				ImGui::EndTabItem();
			}
		
			if (ImGui::BeginTabItem("Rendering"))
			{
				DrawRenderingLimits(props.limits);
				ImGui::EndTabItem();
			}
		
			ImGui::EndTabBar();
		}
	}

	void FVulkanImGuiRender::DrawGeneralLimits(const VkPhysicalDeviceLimits& limits)
	{
		if (ImGui::BeginTable("GeneralLimits", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			auto Row = [](const char* name, const std::string& value)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
			};

			Row("Max Memory Allocation Count", std::format("{:L}", limits.maxMemoryAllocationCount));
			Row("Max Sampler Allocation Count", std::format("{:L}", limits.maxSamplerAllocationCount));
			Row("Buffer-Image Granularity", std::format("{} bytes", limits.bufferImageGranularity));
			Row("Non-Coherent Atom Size", std::format("{} bytes", limits.nonCoherentAtomSize));
			Row("Min Memory Map Alignment", std::format("{} bytes", limits.minMemoryMapAlignment));
			Row("Sparse Address Space Size", std::format("{} GB", limits.sparseAddressSpaceSize / (1024ULL * 1024 * 1024)));
			Row("Max Bound Descriptor Sets", std::format("{}", limits.maxBoundDescriptorSets));
			Row("Max Per-Stage Resources", std::format("{}", limits.maxPerStageResources));
			Row("Timestamp Period", std::format("{:.3f} ns", limits.timestampPeriod));
			Row("Timestamp Compute & Graphics", limits.timestampComputeAndGraphics ? "Yes" : "No");
			Row("Discrete Queue Priorities", std::format("{}", limits.discreteQueuePriorities));

			ImGui::EndTable();
		}
	}

	void FVulkanImGuiRender::DrawBufferImageLimits(const VkPhysicalDeviceLimits& limits)
	{
		if (ImGui::BeginTable("BufferImageLimits", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			auto Row = [](const char* name, const std::string& value)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
			};

			Row("Max Image Dimension 1D", std::format("{:L} px", limits.maxImageDimension1D));
			Row("Max Image Dimension 2D", std::format("{:L} px", limits.maxImageDimension2D));
			Row("Max Image Dimension 3D", std::format("{:L} px", limits.maxImageDimension3D));
			Row("Max Image Dimension Cube", std::format("{:L} px", limits.maxImageDimensionCube));
			Row("Max Image Array Layers", std::format("{:L}", limits.maxImageArrayLayers));
			Row("Max Uniform Buffer Range", std::format("{:L} bytes ({} MB)", limits.maxUniformBufferRange, limits.maxUniformBufferRange / (1024 * 1024)));
			Row("Max Storage Buffer Range", std::format("{:L} bytes ({} MB)", limits.maxStorageBufferRange, limits.maxStorageBufferRange / (1024 * 1024)));
			Row("Max Push Constants Size", std::format("{} bytes", limits.maxPushConstantsSize));
			Row("Min Texel Buffer Offset Align", std::format("{} bytes", limits.minTexelBufferOffsetAlignment));
			Row("Min Uniform Buffer Offset Align", std::format("{} bytes", limits.minUniformBufferOffsetAlignment));
			Row("Min Storage Buffer Offset Align", std::format("{} bytes", limits.minStorageBufferOffsetAlignment));
			Row("Optimal Buffer Copy Offset Align", std::format("{} bytes", limits.optimalBufferCopyOffsetAlignment));
			Row("Optimal Buffer Copy Row Pitch Align", std::format("{} bytes", limits.optimalBufferCopyRowPitchAlignment));

			ImGui::EndTable();
		}
	}

	void FVulkanImGuiRender::DrawComputeLimits(const VkPhysicalDeviceLimits& limits)
	{
		if (ImGui::BeginTable("ComputeLimits", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			auto Row = [](const char* name, const std::string& value)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
			};

			Row("Max Compute Shared Memory Size", std::format("{} bytes ({} KB)", limits.maxComputeSharedMemorySize, limits.maxComputeSharedMemorySize / 1024));
			Row("Max Compute Work Group Invocations", std::format("{:L}", limits.maxComputeWorkGroupInvocations));
			Row("Max Compute Work Group Count", std::format("{:L} × {:L} × {:L}", 
				limits.maxComputeWorkGroupCount[0], 
				limits.maxComputeWorkGroupCount[1], 
				limits.maxComputeWorkGroupCount[2]));
			Row("Max Compute Work Group Size", std::format("{} × {} × {}", 
				limits.maxComputeWorkGroupSize[0], 
				limits.maxComputeWorkGroupSize[1], 
				limits.maxComputeWorkGroupSize[2]));

			ImGui::EndTable();
		}
	}

	void FVulkanImGuiRender::DrawDescriptorLimits(const VkPhysicalDeviceLimits& limits)
	{
		if (ImGui::BeginTable("DescriptorLimits", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
		
			auto Row = [](const char* name, const std::string& value)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
			};
		
			Row("Max Per-Stage Descriptor Samplers", std::format("{:L}", limits.maxPerStageDescriptorSamplers));
			Row("Max Per-Stage Descriptor Uniform Buffers", std::format("{:L}", limits.maxPerStageDescriptorUniformBuffers));
			Row("Max Per-Stage Descriptor Storage Buffers", std::format("{:L}", limits.maxPerStageDescriptorStorageBuffers));
			Row("Max Per-Stage Descriptor Sampled Images", std::format("{:L}", limits.maxPerStageDescriptorSampledImages));
			Row("Max Per-Stage Descriptor Storage Images", std::format("{:L}", limits.maxPerStageDescriptorStorageImages));
			Row("Max Per-Stage Descriptor Input Attachments", std::format("{:L}", limits.maxPerStageDescriptorInputAttachments));
			Row("Max Descriptor Set Samplers", std::format("{:L}", limits.maxDescriptorSetSamplers));
			Row("Max Descriptor Set Uniform Buffers", std::format("{:L}", limits.maxDescriptorSetUniformBuffers));
			Row("Max Descriptor Set Uniform Buffers Dynamic", std::format("{:L}", limits.maxDescriptorSetUniformBuffersDynamic));
			Row("Max Descriptor Set Storage Buffers", std::format("{:L}", limits.maxDescriptorSetStorageBuffers));
			Row("Max Descriptor Set Storage Buffers Dynamic", std::format("{:L}", limits.maxDescriptorSetStorageBuffersDynamic));
			Row("Max Descriptor Set Sampled Images", std::format("{:L}", limits.maxDescriptorSetSampledImages));
			Row("Max Descriptor Set Storage Images", std::format("{:L}", limits.maxDescriptorSetStorageImages));
			Row("Max Descriptor Set Input Attachments", std::format("{:L}", limits.maxDescriptorSetInputAttachments));
		
			ImGui::EndTable();
		}
	}

	void FVulkanImGuiRender::DrawRenderingLimits(const VkPhysicalDeviceLimits& limits)
	{
		if (ImGui::BeginTable("RenderingLimits", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
		
			auto Row = [](const char* name, const std::string& value)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
			};
		
			Row("Max Framebuffer Width", std::format("{:L} px", limits.maxFramebufferWidth));
			Row("Max Framebuffer Height", std::format("{:L} px", limits.maxFramebufferHeight));
			Row("Max Framebuffer Layers", std::format("{:L}", limits.maxFramebufferLayers));
			Row("Max Color Attachments", std::format("{}", limits.maxColorAttachments));
			Row("Framebuffer Color Sample Counts", std::format("0x{:X}", limits.framebufferColorSampleCounts));
			Row("Framebuffer Depth Sample Counts", std::format("0x{:X}", limits.framebufferDepthSampleCounts));
			Row("Framebuffer Stencil Sample Counts", std::format("0x{:X}", limits.framebufferStencilSampleCounts));
			Row("Framebuffer No Attachments Sample Counts", std::format("0x{:X}", limits.framebufferNoAttachmentsSampleCounts));
			Row("Max Viewports", std::format("{}", limits.maxViewports));
			Row("Max Viewport Dimensions", std::format("{:L} × {:L}", limits.maxViewportDimensions[0], limits.maxViewportDimensions[1]));
			Row("Viewport Bounds Range", std::format("[{:.2f}, {:.2f}]", limits.viewportBoundsRange[0], limits.viewportBoundsRange[1]));
			Row("Viewport Subpixel Precision Bits", std::format("{}", limits.viewportSubPixelBits));
			Row("Subpixel Precision Bits", std::format("{}", limits.subPixelPrecisionBits));
			Row("Subpixel Interpolation Offset Bits", std::format("{}", limits.subPixelInterpolationOffsetBits));
			Row("Line Width Range", std::format("[{:.2f}, {:.2f}]", limits.lineWidthRange[0], limits.lineWidthRange[1]));
			Row("Line Width Granularity", std::format("{:.4f}", limits.lineWidthGranularity));
			Row("Point Size Range", std::format("[{:.2f}, {:.2f}]", limits.pointSizeRange[0], limits.pointSizeRange[1]));
			Row("Point Size Granularity", std::format("{:.4f}", limits.pointSizeGranularity));
			Row("Max Vertex Input Attributes", std::format("{}", limits.maxVertexInputAttributes));
			Row("Max Vertex Input Bindings", std::format("{}", limits.maxVertexInputBindings));
			Row("Max Vertex Input Attribute Offset", std::format("{} bytes", limits.maxVertexInputAttributeOffset));
			Row("Max Vertex Input Binding Stride", std::format("{} bytes", limits.maxVertexInputBindingStride));
			Row("Max Vertex Output Components", std::format("{}", limits.maxVertexOutputComponents));
			Row("Max Fragment Input Components", std::format("{}", limits.maxFragmentInputComponents));
			Row("Max Fragment Output Attachments", std::format("{}", limits.maxFragmentOutputAttachments));
			Row("Max Fragment Dual Src Attachments", std::format("{}", limits.maxFragmentDualSrcAttachments));
			Row("Max Fragment Combined Output Resources", std::format("{}", limits.maxFragmentCombinedOutputResources));
			Row("Max Draw Indexed Index Value", std::format("{:L}", limits.maxDrawIndexedIndexValue));
			Row("Max Draw Indirect Count", std::format("{:L}", limits.maxDrawIndirectCount));
		
			ImGui::EndTable();
		}
	}
}
