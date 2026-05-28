#include "pch.h"

#include "VulkanSwapchain.h"

#ifdef _WIN32
	#include <Windows.h>
#endif
#include "Core/Windows/GLFWInclude.h"
#include "VkBootstrap.h"
#include "VulkanDevice.h"
#include "VulkanMacros.h"
#include "VulkanRenderContext.h"
#include "Core/Profiler/Profile.h"
#include "Core/Windows/Window.h"
#include "Renderer/RenderTypes.h"
#include "Core/Engine/Engine.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"

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

    void FVulkanSwapchain::CreateSwapchain(VkInstance Instance, FVulkanRenderContext* InContext, GLFWwindow* Window, FUIntVector2 Extent, bool bFromResize, bool bPrimary)
    {
    	LUMINA_PROFILE_SCOPE();

    	Context = InContext;
    	DesiredExtent = Extent;   // requested target; the actual (clamped) extent is read back from vkb after build
    	VkDevice Device = Context->GetDevice()->GetDevice();

    	if (bFromResize == false)
    	{
    		WindowHandle = Window;
    		bIsPrimarySwapchain = bPrimary;
			VK_CHECK(glfwCreateWindowSurface(Instance, Window, VK_ALLOC_CALLBACK, &Surface));
    	}

    	VkPhysicalDevice PhysicalDevice = Context->GetDevice()->GetPhysicalDevice();
        vkb::SwapchainBuilder SwapchainBuilder(PhysicalDevice, Device, Surface);

    	Format						= VK_FORMAT_B8G8R8A8_UNORM;
    	SurfaceFormat				= {};
        SurfaceFormat.format		= VK_FORMAT_B8G8R8A8_UNORM;
    	SurfaceFormat.colorSpace	= VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    	
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
    	
    	ASSERT(vkbSwapchain.has_value() && vkbSwapchain->get_images().has_value(), "Failed to create swapchain! Error: {} - Extent: {}x{}", vkbSwapchain.error().message(), Extent.x, Extent.y);
    	
    	if (bFromResize)
    	{
			vkDestroySwapchainKHR(Device, Swapchain, VK_ALLOC_CALLBACK);
    		Swapchain = VK_NULL_HANDLE;
		}

        Swapchain = vkbSwapchain->swapchain;

    	// Use actual (clamped) extent; mismatching renderArea > imageView triggers VUID-06080.
    	const VkExtent2D ActualExtent = vkbSwapchain->extent;
    	SwapchainExtent = FUIntVector2(ActualExtent.width, ActualExtent.height);

        std::vector<VkImage> RawImages = vkbSwapchain->get_images().value();

    	SwapchainImages.clear();
    	SwapchainImages.reserve(SWAPCHAIN_IMAGES);

	    for (int i = 0; i < RawImages.size(); ++i)
	    {
	    	VkImage RawImage = RawImages[i];
	    	
        	FRHIImageDesc ImageDescription;
        	ImageDescription.Extent = SwapchainExtent;   // actual (clamped) extent, not the request
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

    void FVulkanSwapchain::RecreateSwapchain(const FUIntVector2& Extent)
    {
    	LUMINA_PROFILE_SCOPE();

    	Context->WaitIdle();
    	CreateSwapchain(Context->GetVulkanInstance(), Context, WindowHandle, Extent, true);

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

    void FVulkanSwapchain::WaitForFramePace()
    {
        LUMINA_PROFILE_SCOPE();

        while (FramesInFlight.size() >= FRAMES_IN_FLIGHT)
        {
            FRHIEventQueryRef Query = FramesInFlight.front();
            FramesInFlight.pop_front();

            Context->WaitEventQuery(Query);

            QueryPool.push_back(Query);
        }
    }

    bool FVulkanSwapchain::AcquireNextImage()
    {
    	LUMINA_PROFILE_SCOPE();

    	VkResult    Result    = VK_RESULT_MAX_ENUM;
    	VkSemaphore Semaphore = VK_NULL_HANDLE;

    	// Only a successful acquire publishes a handle; a failed one must not leave a stale wait.
    	CurrentAcquireSemaphore = VK_NULL_HANDLE;

	    constexpr int MaxAttempts = 3;
	    for (int Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	    {
	    	if (bNeedsResize)
	    	{
	    		FUIntVector2 NewExtent = bIsPrimarySwapchain ? Windowing::GetPrimaryWindowHandle()->GetExtent() : DesiredExtent;

	    		if (NewExtent.x > 0 && NewExtent.y > 0)
	    		{
	    			RecreateSwapchain(NewExtent);
	    			if (bIsPrimarySwapchain && GRenderManager != nullptr)
	    			{
	    				GRenderManager->SwapchainResized(NewExtent);
	    			}
	    		}
	    		else
	    		{
	    			bNeedsResize = false;   // bad extent; don't spin recreating
	    		}
	    	}
	    	
	    	Semaphore = AcquireSemaphores[AcquireSemaphoreIndex];

	    	Result = vkAcquireNextImageKHR(Context->GetDevice()->GetDevice(), Swapchain, UINT64_MAX, Semaphore, nullptr, &CurrentImageIndex);
	    	
	    	if (Result == VK_ERROR_OUT_OF_DATE_KHR)
	    	{
	    		bNeedsResize = true;
	    		continue;
	    	}
	    	break;
	    }

    	if (Result == VK_SUBOPTIMAL_KHR)
    	{
    		bNeedsResize = true;
    	}

    	if (Result == VK_SUCCESS || Result == VK_SUBOPTIMAL_KHR)
    	{
    		CurrentAcquireSemaphore = Semaphore;
    		AcquireSemaphoreIndex = (AcquireSemaphoreIndex + 1) % AcquireSemaphores.size();
    		return true;
    	}
    	
    	return false;
    }

    bool FVulkanSwapchain::Present()
    {
	    LUMINA_PROFILE_SCOPE();

    	FQueue* Queue = Context->GetQueue(ECommandQueue::Graphics);
    	
    	VkSemaphore Semaphore = PresentSemaphores[CurrentImageIndex];

    	VkPresentInfoKHR PresentInfo	= {};
    	PresentInfo.sType				= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    	PresentInfo.pSwapchains			= &Swapchain;
    	PresentInfo.swapchainCount		= 1;
    	PresentInfo.pWaitSemaphores		= &Semaphore;
    	PresentInfo.waitSemaphoreCount	= 1;
    	PresentInfo.pImageIndices		= &CurrentImageIndex;

    	VkResult Result;
	    {
	    	LUMINA_PROFILE_SECTION_COLORED("vkQueuePresentKHR", tracy::Color::Aquamarine3);
		    Result = Queue->Present(PresentInfo);
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
    	FramesInFlight.push_back(Query);
    	return true;
    }
}
