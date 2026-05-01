#include "pch.h"

#include "VulkanSwapchain.h"

#ifdef _WIN32
	#include <Windows.h>
#endif
#include <glfw/glfw3.h>
#include "VkBootstrap.h"
#include "VulkanDevice.h"
#include "VulkanMacros.h"
#include "VulkanRenderContext.h"
#include "Core/Profiler/Profile.h"
#include "Core/Windows/Window.h"
#include "Renderer/RenderTypes.h"
#include "Core/Engine/Engine.h"
#include "Renderer/RenderManager.h"

namespace Lumina
{
    FVulkanSwapchain::~FVulkanSwapchain()
    {
    	vkDestroySwapchainKHR(Context->GetDevice()->GetDevice(), Swapchain, VK_ALLOC_CALLBACK);

    	SwapchainImages.clear();

    	for (VkSemaphore Semaphore : AcquireSemaphores)
    	{
    		if (Semaphore)
    		{
    			vkDestroySemaphore(Context->GetDevice()->GetDevice(), Semaphore, VK_ALLOC_CALLBACK);
    		}
    	}
    	
    	for (VkSemaphore Semaphore : PresentSemaphores)
    	{
    		if (Semaphore)
    		{
    			vkDestroySemaphore(Context->GetDevice()->GetDevice(), Semaphore, VK_ALLOC_CALLBACK);    
    		}
    	}

    	AcquireSemaphores.clear();
    	PresentSemaphores.clear();

    	
    	vkDestroySurfaceKHR(Context->GetVulkanInstance(), Surface, VK_ALLOC_CALLBACK);
    	
    }

    void FVulkanSwapchain::CreateSwapchain(VkInstance Instance, FVulkanRenderContext* InContext, FWindow* Window, glm::uvec2 Extent, bool bFromResize)
    {
    	LUMINA_PROFILE_SCOPE();

    	Context = InContext;
    	SwapchainExtent = Extent;
    	VkDevice Device = Context->GetDevice()->GetDevice();
    	
    	if (bFromResize == false)
    	{
			VK_CHECK(glfwCreateWindowSurface(Instance, Window->GetWindow(), VK_ALLOC_CALLBACK, &Surface));
    	}

    	VkPhysicalDevice PhysicalDevice = Context->GetDevice()->GetPhysicalDevice();
        vkb::SwapchainBuilder SwapchainBuilder(PhysicalDevice, Device, Surface);

    	Format						= VK_FORMAT_B8G8R8A8_UNORM;
    	SurfaceFormat				= {};
        SurfaceFormat.format		= VK_FORMAT_B8G8R8A8_UNORM;
    	SurfaceFormat.colorSpace	= VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    	LOG_TRACE("Creating Vulkan Swapchain - Format {} - Width: {} - Height: {}", "VK_FORMAT_B8G8R8A8_UNORM", Extent.x, Extent.y);

        auto vkbSwapchain = SwapchainBuilder
            .set_desired_format(SurfaceFormat)
            .set_desired_present_mode(CurrentPresentMode)
            .set_desired_min_image_count(SWAPCHAIN_IMAGES)
    		.set_old_swapchain(Swapchain)
            .set_image_array_layer_count(1)
    		.set_allocation_callbacks(VK_ALLOC_CALLBACK)
            .set_desired_extent(Extent.x, Extent.y)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            .build();

    	//DEBUG_ASSERT(!vkbSwapchain.has_value() || !vkbSwapchain->get_images().has_value(), "Failed to create swapchain! Error: {} - Extent: {}x{}", vkbSwapchain.error().message(), Extent.x, Extent.y);
    	if (!vkbSwapchain.has_value() || !vkbSwapchain->get_images().has_value())
    	{
    		LOG_CRITICAL("Failed to create swapchain! Error: {} - Extent: {}x{}", vkbSwapchain.error().message(), Extent.x, Extent.y);
    		UNREACHABLE();
    	}
    	
    	if (bFromResize)
    	{
			vkDestroySwapchainKHR(Device, Swapchain, VK_ALLOC_CALLBACK);
    		Swapchain = VK_NULL_HANDLE;
		}

        Swapchain = vkbSwapchain->swapchain;
    	
        std::vector<VkImage> RawImages = vkbSwapchain->get_images().value();

    	SwapchainImages.clear();
    	SwapchainImages.reserve(SWAPCHAIN_IMAGES);

	    for (int i = 0; i < RawImages.size(); ++i)
	    {
	    	VkImage RawImage = RawImages[i];
	    	
        	FRHIImageDesc ImageDescription;
        	ImageDescription.Extent = Extent;
        	ImageDescription.Format = EFormat::BGRA8_UNORM;
        	ImageDescription.InitialState = EResourceStates::Present;
        	ImageDescription.bKeepInitialState = true;
        	ImageDescription.DebugName = "Swapchain Image";

        	TRefCountPtr<FVulkanImage> Image = MakeRefCount<FVulkanImage>(Context->GetDevice(), ImageDescription, RawImage, FVulkanImage::EInternal::ExternallyManaged);

        	FFixedString ObjectName = FFixedString("SwapchainImage: ").append(eastl::to_string(i).c_str());
        	Context->SetObjectName(Image, ObjectName.c_str(), EAPIResourceType::Image);
			SwapchainImages.push_back(Image);
        }

    	if (!bFromResize)
    	{
    		if ((!PresentSemaphores.empty() || !AcquireSemaphores.empty()))
    		{
    			for (VkSemaphore Semaphore : PresentSemaphores)
    			{
    				vkDestroySemaphore(Device, Semaphore, VK_ALLOC_CALLBACK);
    			}
    			for (VkSemaphore Semaphore : AcquireSemaphores)
    			{
    				vkDestroySemaphore(Device, Semaphore, VK_ALLOC_CALLBACK);
    			}

    			PresentSemaphores.clear();
    			AcquireSemaphores.clear();
    		}

    	
    		size_t NumPresentSemaphores = SwapchainImages.size();
    		PresentSemaphores.reserve(NumPresentSemaphores);
    		for (SIZE_T i = 0; i < NumPresentSemaphores; ++i)
    		{
    			VkSemaphoreCreateInfo CreateInfo = {};
    			CreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    			VkSemaphore Semaphore;
    			VK_CHECK(vkCreateSemaphore(Device, &CreateInfo, VK_ALLOC_CALLBACK, &Semaphore));
    			PresentSemaphores.push_back(Semaphore);
    		}

    		SIZE_T NumAcquireSemaphores = std::max<uint32>(FRAMES_IN_FLIGHT, SwapchainImages.size());
    		AcquireSemaphores.reserve(NumAcquireSemaphores);

    		for (SIZE_T i = 0; i < NumAcquireSemaphores; ++i)
    		{
    			VkSemaphoreCreateInfo CreateInfo = {};
    			CreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    			VkSemaphore Semaphore;
    			VK_CHECK(vkCreateSemaphore(Device, &CreateInfo, VK_ALLOC_CALLBACK, &Semaphore));
    			AcquireSemaphores.push_back(Semaphore);
    		}
    	}

    	CurrentImageIndex = 0;
    }

    void FVulkanSwapchain::RecreateSwapchain(const glm::uvec2& Extent)
    {
    	LUMINA_PROFILE_SCOPE();

    	Context->WaitIdle();
    	CreateSwapchain(Context->GetVulkanInstance(), Context, Windowing::GetPrimaryWindowHandle(), Extent, true);
    	
    	bNeedsResize = false;
    }

	void FVulkanSwapchain::SetPresentMode(VkPresentModeKHR NewMode)
    {
    	CurrentPresentMode = NewMode;
    	bNeedsResize = true;
    }
	

	TRefCountPtr<FVulkanImage> FVulkanSwapchain::GetCurrentImage() const
    {
	    return SwapchainImages[CurrentImageIndex];
    }

    bool FVulkanSwapchain::AcquireNextImage()
    {
    	LUMINA_PROFILE_SCOPE();
    	
    	VkSemaphore Semaphore = AcquireSemaphores[AcquireSemaphoreIndex];
    	VkResult Result = VK_RESULT_MAX_ENUM;

	    constexpr int MaxAttempts = 3;
	    for (int Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	    {
	    	Result = vkAcquireNextImageKHR(Context->GetDevice()->GetDevice(), Swapchain, UINT64_MAX, Semaphore, nullptr, &CurrentImageIndex);

	    	if ((Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR || bNeedsResize) && Attempt < MaxAttempts)
	    	{
	    		RecreateSwapchain(Windowing::GetPrimaryWindowHandle()->GetExtent());
	    		GRenderManager->SwapchainResized(Windowing::GetPrimaryWindowHandle()->GetExtent());
	    	}
		    else
		    {
			    break;
		    }
	    }

    	AcquireSemaphoreIndex = (AcquireSemaphoreIndex + 1) % AcquireSemaphores.size();
    	
    	if ((Result == VK_SUCCESS || Result == VK_SUBOPTIMAL_KHR || bNeedsResize))
    	{
    		Context->GetQueue(ECommandQueue::Graphics)->AddWaitSemaphore(Semaphore, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    	}

    	return false;
    }

    bool FVulkanSwapchain::Present()
    {
	    LUMINA_PROFILE_SCOPE();

    	FQueue* Queue = Context->GetQueue(ECommandQueue::Graphics);
    	
    	VkSemaphore Semaphore = PresentSemaphores[CurrentImageIndex];
    	
    	Queue->SignalSemaphore(Semaphore);
    	
    	VkPresentInfoKHR PresentInfo = {};
    	PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    	PresentInfo.pSwapchains = &Swapchain;
    	PresentInfo.swapchainCount = 1;
    	PresentInfo.pWaitSemaphores = &Semaphore;
    	PresentInfo.waitSemaphoreCount = 1;
    	PresentInfo.pImageIndices = &CurrentImageIndex;

    	VkResult Result;
	    {
	    	LUMINA_PROFILE_SECTION_COLORED("vkQueuePresentKHR", tracy::Color::Aquamarine3);
		    Result = vkQueuePresentKHR(Queue->Queue, &PresentInfo);
	    }

    	if (!(Result == VK_SUCCESS || Result == VK_SUBOPTIMAL_KHR || Result == VK_ERROR_OUT_OF_DATE_KHR))
    	{
    		LOG_CRITICAL("Failed to present an image!");
    		return false;
    	}

#ifndef _WIN32

    	if (CurrentPresentMode == VK_PRESENT_MODE_FIFO_KHR)
    	{
    		Queue->WaitIdle();
    	}
#endif


    	while (FramesInFlight.size() >= FRAMES_IN_FLIGHT)
    	{
    		FRHIEventQueryRef Query = FramesInFlight.front();
    		FramesInFlight.pop();
			
    		Context->WaitEventQuery(Query);

    		QueryPool.push_back(Query);
    	}

    	FRHIEventQueryRef Query;
    	if (!QueryPool.empty())
    	{
    		Query = QueryPool.back();
    		QueryPool.pop_back();
    	}
	    else
	    {
		    Query = Context->CreateEventQuery();
	    }

    	Context->ResetEventQuery(Query);
    	Context->SetEventQuery(Query, ECommandQueue::Graphics);
    	FramesInFlight.push(Query);
    	return true;
    }
}
