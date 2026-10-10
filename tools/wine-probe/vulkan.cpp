/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Vulkan on a Win32 surface, which Wine's display driver backs with its own;
// exclusive sets the display mode first, as a Vulkan game without
// VK_EXT_full_screen_exclusive does. vulkan-1.dll is loaded when asked for,
// and the frame is a render pass that clears to red with a clear of its right
// half to blue, so that no shader is needed.

#include "probe.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <vector>

#define UPSCALE_VULKAN_FUNCTIONS(X)              \
    X(vkEnumeratePhysicalDevices)                \
    X(vkGetPhysicalDeviceProperties)             \
    X(vkGetPhysicalDeviceQueueFamilyProperties)  \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)      \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)      \
    X(vkCreateWin32SurfaceKHR)                   \
    X(vkCreateDevice)                            \
    X(vkGetDeviceQueue)                          \
    X(vkCreateSwapchainKHR)                      \
    X(vkDestroySwapchainKHR)                     \
    X(vkGetSwapchainImagesKHR)                   \
    X(vkCreateImageView)                         \
    X(vkDestroyImageView)                        \
    X(vkCreateRenderPass)                        \
    X(vkCreateFramebuffer)                       \
    X(vkDestroyFramebuffer)                      \
    X(vkCreateCommandPool)                       \
    X(vkAllocateCommandBuffers)                  \
    X(vkResetCommandBuffer)                      \
    X(vkBeginCommandBuffer)                      \
    X(vkEndCommandBuffer)                        \
    X(vkCmdBeginRenderPass)                      \
    X(vkCmdEndRenderPass)                        \
    X(vkCmdClearAttachments)                     \
    X(vkCreateSemaphore)                         \
    X(vkCreateFence)                             \
    X(vkWaitForFences)                           \
    X(vkResetFences)                             \
    X(vkAcquireNextImageKHR)                     \
    X(vkQueueSubmit)                             \
    X(vkQueuePresentKHR)                         \
    X(vkDeviceWaitIdle)

namespace
{

#define UPSCALE_DECLARE(name) PFN_##name name = nullptr;
UPSCALE_VULKAN_FUNCTIONS(UPSCALE_DECLARE)
#undef UPSCALE_DECLARE

class Vulkan : public Renderer
{
public:
    bool start(HWND window, int width, int height, bool exclusive, std::string &error) override
    {
        if (exclusive && !setDisplayMode(width, height, error)) {
            return false;
        }
        if (!load(error) || !connect(window, error)) {
            return false;
        }
        return chain(width, height, error);
    }

    void frame(int width, int height) override
    {
        VkSurfaceCapabilitiesKHR capabilities = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &capabilities);
        const VkExtent2D wanted = capabilities.currentExtent.width != UINT32_MAX ? capabilities.currentExtent
                                                                                 : VkExtent2D{uint32_t(width), uint32_t(height)};
        std::string error;
        if ((wanted.width != m_extent.width || wanted.height != m_extent.height) && !chain(int(wanted.width), int(wanted.height), error)) {
            return;
        }
        vkWaitForFences(m_device, 1, &m_done, VK_TRUE, UINT64_MAX);
        uint32_t index = 0;
        const VkResult acquired = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, m_acquired, VK_NULL_HANDLE, &index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            chain(width, height, error);
            return;
        }
        vkResetFences(m_device, 1, &m_done);
        record(index);
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &m_acquired;
        submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &m_commands;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &m_drawn;
        vkQueueSubmit(m_queue, 1, &submit, m_done);
        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &m_drawn;
        present.swapchainCount = 1;
        present.pSwapchains = &m_swapChain;
        present.pImageIndices = &index;
        vkQueuePresentKHR(m_queue, &present);
    }

    std::string device() const override
    {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(m_physical, &properties);
        return properties.deviceName;
    }

private:
    bool load(std::string &error)
    {
        const HMODULE library = LoadLibraryW(L"vulkan-1.dll");
        const auto address = library ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void *>(GetProcAddress(library, "vkGetInstanceProcAddr")))
                                     : nullptr;
        const auto create = address ? reinterpret_cast<PFN_vkCreateInstance>(address(nullptr, "vkCreateInstance")) : nullptr;
        if (!create) {
            error = "no vulkan-1.dll";
            return false;
        }
        const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        VkApplicationInfo application = {};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "Upscale probe";
        application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instance = {};
        instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance.pApplicationInfo = &application;
        instance.enabledExtensionCount = 2;
        instance.ppEnabledExtensionNames = extensions;
        const VkResult result = create(&instance, nullptr, &m_instance);
        if (result != VK_SUCCESS) {
            error = "vkCreateInstance answered " + std::to_string(result);
            return false;
        }
#define UPSCALE_LOAD(name) name = reinterpret_cast<PFN_##name>(address(m_instance, #name));
        UPSCALE_VULKAN_FUNCTIONS(UPSCALE_LOAD)
#undef UPSCALE_LOAD
        return true;
    }

    // The surface, a device with a queue that draws and presents to it, and
    // what a frame is recorded with.
    bool connect(HWND window, std::string &error)
    {
        VkWin32SurfaceCreateInfoKHR surface = {};
        surface.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface.hinstance = GetModuleHandleW(nullptr);
        surface.hwnd = window;
        if (vkCreateWin32SurfaceKHR(m_instance, &surface, nullptr, &m_surface) != VK_SUCCESS) {
            error = "no Win32 surface";
            return false;
        }
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
        for (VkPhysicalDevice candidate : devices) {
            uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> properties(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, properties.data());
            for (uint32_t family = 0; family < families && !m_physical; ++family) {
                VkBool32 presents = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, m_surface, &presents);
                if ((properties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presents) {
                    m_physical = candidate;
                    m_family = family;
                }
            }
        }
        if (!m_physical) {
            error = "no device presents to the surface";
            return false;
        }
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queue = {};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = m_family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        const char *extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo device = {};
        device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device.queueCreateInfoCount = 1;
        device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = 1;
        device.ppEnabledExtensionNames = &extension;
        if (vkCreateDevice(m_physical, &device, nullptr, &m_device) != VK_SUCCESS) {
            error = "vkCreateDevice failed";
            return false;
        }
        vkGetDeviceQueue(m_device, m_family, 0, &m_queue);
        VkCommandPoolCreateInfo pool = {};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = m_family;
        vkCreateCommandPool(m_device, &pool, nullptr, &m_pool);
        VkCommandBufferAllocateInfo buffer = {};
        buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        buffer.commandPool = m_pool;
        buffer.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        buffer.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_device, &buffer, &m_commands);
        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(m_device, &semaphore, nullptr, &m_acquired);
        vkCreateSemaphore(m_device, &semaphore, nullptr, &m_drawn);
        VkFenceCreateInfo fence = {};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(m_device, &fence, nullptr, &m_done);
        return true;
    }

    // The swap chain at @p width x @p height, made again whenever the surface
    // changes size, with a view and a framebuffer per image.
    bool chain(int width, int height, std::string &error)
    {
        vkDeviceWaitIdle(m_device);
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &count, formats.data());
        if (formats.empty()) {
            error = "the surface offers no format";
            return false;
        }
        VkSurfaceCapabilitiesKHR capabilities = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &capabilities);
        const bool opaque = capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        VkSwapchainCreateInfoKHR swapChain = {};
        swapChain.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChain.surface = m_surface;
        swapChain.minImageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount && swapChain.minImageCount > capabilities.maxImageCount) {
            swapChain.minImageCount = capabilities.maxImageCount;
        }
        swapChain.imageFormat = formats.front().format;
        swapChain.imageColorSpace = formats.front().colorSpace;
        swapChain.imageExtent = {uint32_t(width), uint32_t(height)};
        swapChain.imageArrayLayers = 1;
        swapChain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChain.preTransform = capabilities.currentTransform;
        swapChain.compositeAlpha = opaque ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                          : VkCompositeAlphaFlagBitsKHR(capabilities.supportedCompositeAlpha & -capabilities.supportedCompositeAlpha);
        swapChain.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapChain.clipped = VK_TRUE;
        swapChain.oldSwapchain = m_swapChain;
        VkSwapchainKHR made = VK_NULL_HANDLE;
        const VkResult result = vkCreateSwapchainKHR(m_device, &swapChain, nullptr, &made);
        for (VkFramebuffer framebuffer : m_framebuffers) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
        for (VkImageView view : m_views) {
            vkDestroyImageView(m_device, view, nullptr);
        }
        m_framebuffers.clear();
        m_views.clear();
        if (m_swapChain) {
            vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        }
        m_swapChain = made;
        if (result != VK_SUCCESS) {
            error = "vkCreateSwapchainKHR answered " + std::to_string(result);
            return false;
        }
        if (!m_pass) {
            pass(swapChain.imageFormat);
        }
        m_extent = swapChain.imageExtent;
        vkGetSwapchainImagesKHR(m_device, m_swapChain, &count, nullptr);
        std::vector<VkImage> images(count);
        vkGetSwapchainImagesKHR(m_device, m_swapChain, &count, images.data());
        for (VkImage image : images) {
            VkImageViewCreateInfo view = {};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = swapChain.imageFormat;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            m_views.emplace_back();
            vkCreateImageView(m_device, &view, nullptr, &m_views.back());
            VkFramebufferCreateInfo framebuffer = {};
            framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer.renderPass = m_pass;
            framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &m_views.back();
            framebuffer.width = m_extent.width;
            framebuffer.height = m_extent.height;
            framebuffer.layers = 1;
            m_framebuffers.emplace_back();
            vkCreateFramebuffer(m_device, &framebuffer, nullptr, &m_framebuffers.back());
        }
        return true;
    }

    void pass(VkFormat format)
    {
        VkAttachmentDescription attachment = {};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        const VkAttachmentReference colour = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colour;
        VkRenderPassCreateInfo pass = {};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass.attachmentCount = 1;
        pass.pAttachments = &attachment;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        vkCreateRenderPass(m_device, &pass, nullptr, &m_pass);
    }

    void record(uint32_t index)
    {
        vkResetCommandBuffer(m_commands, 0);
        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(m_commands, &begin);
        VkClearValue red = {};
        red.color = {{1.0F, 0.0F, 0.0F, 1.0F}};
        VkRenderPassBeginInfo pass = {};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = m_pass;
        pass.framebuffer = m_framebuffers[index];
        pass.renderArea.extent = m_extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &red;
        vkCmdBeginRenderPass(m_commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
        VkClearAttachment blue = {VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
        blue.clearValue.color = {{0.0F, 0.0F, 1.0F, 1.0F}};
        const int half = leftHalf(int(m_extent.width));
        const VkClearRect second = {{{half, 0}, {m_extent.width - uint32_t(half), m_extent.height}}, 0, 1};
        vkCmdClearAttachments(m_commands, 1, &blue, 1, &second);
        vkCmdEndRenderPass(m_commands);
        vkEndCommandBuffer(m_commands);
    }

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    uint32_t m_family = 0;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
    VkSemaphore m_acquired = VK_NULL_HANDLE;
    VkSemaphore m_drawn = VK_NULL_HANDLE;
    VkFence m_done = VK_NULL_HANDLE;
    VkRenderPass m_pass = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkExtent2D m_extent = {};
    std::vector<VkImageView> m_views;
    std::vector<VkFramebuffer> m_framebuffers;
};

}

std::unique_ptr<Renderer> makeVulkan()
{
    return std::make_unique<Vulkan>();
}
