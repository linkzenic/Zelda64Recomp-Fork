#include <android/log.h>
#include <jni.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <unistd.h>
#include <vector>

namespace {
constexpr const char* kLogTag = "ZeldaVkSmoke";
constexpr SDL_JoystickID kAndroidMotionInstanceId = -0x5A64;

char log_path[512] = "/sdcard/Zelda64/Zelda64Recompiled.log";
int virtual_joystick_id = -1;
SDL_Joystick* virtual_joystick = nullptr;
SDL_JoystickID virtual_joystick_instance_id = -1;

void copy_path(char* destination, size_t destination_size, const char* source) {
    if (source == nullptr || source[0] == '\0' || destination_size == 0) {
        return;
    }

    std::strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

void append_probe_log(const char* line) {
    const int fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
        return;
    }

    write(fd, line, std::strlen(line));
    write(fd, "\n", 1);
    close(fd);
}

void probe_log(int priority, const char* format, ...) {
    char line[1024];

    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    __android_log_print(priority, kLogTag, "%s", line);
    append_probe_log(line);
}

const char* vk_result_name(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

const char* physical_device_type_name(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "INTEGRATED_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "DISCRETE_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "VIRTUAL_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "OTHER";
    }
}

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) {
    const VkCompositeAlphaFlagBitsKHR candidates[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };

    for (VkCompositeAlphaFlagBitsKHR candidate : candidates) {
        if ((supported & candidate) != 0) {
            return candidate;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

bool find_graphics_present_queue(VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t* queue_family) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

    probe_log(ANDROID_LOG_INFO, "queue family count=%u", queue_family_count);
    for (uint32_t i = 0; i < queue_family_count; i++) {
        VkBool32 present_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_supported);
        probe_log(ANDROID_LOG_INFO, "queue family %u flags=0x%X count=%u present=%u",
                  i,
                  queue_families[i].queueFlags,
                  queue_families[i].queueCount,
                  present_supported);

        if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present_supported == VK_TRUE) {
            *queue_family = i;
            return true;
        }
    }

    return false;
}

int run_probe() {
    probe_log(ANDROID_LOG_INFO, "Vulkan smoke probe entered");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        probe_log(ANDROID_LOG_ERROR, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    probe_log(ANDROID_LOG_INFO, "SDL_Init succeeded; video driver=%s",
              SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "(none)");

    SDL_Window* window = SDL_CreateWindow("Zelda64 Recompiled Vulkan Probe",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          1280,
                                          720,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (window == nullptr) {
        probe_log(ANDROID_LOG_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);
    probe_log(ANDROID_LOG_INFO, "SDL Vulkan window created; drawable=%dx%d", drawable_width, drawable_height);

    unsigned int extension_count = 0;
    if (SDL_Vulkan_GetInstanceExtensions(window, &extension_count, nullptr) != SDL_TRUE) {
        probe_log(ANDROID_LOG_ERROR, "SDL_Vulkan_GetInstanceExtensions count failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 3;
    }

    std::vector<const char*> extensions(extension_count);
    if (SDL_Vulkan_GetInstanceExtensions(window, &extension_count, extensions.data()) != SDL_TRUE) {
        probe_log(ANDROID_LOG_ERROR, "SDL_Vulkan_GetInstanceExtensions list failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
    }

    for (unsigned int i = 0; i < extension_count; i++) {
        probe_log(ANDROID_LOG_INFO, "instance extension %u: %s", i, extensions[i]);
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Zelda64 Android Vulkan Probe";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "probe";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = extensions.data();

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
    probe_log(ANDROID_LOG_INFO, "vkCreateInstance result=%s (%d)", vk_result_name(result), result);
    if (result != VK_SUCCESS) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 5;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE) {
        probe_log(ANDROID_LOG_ERROR, "SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        vkDestroyInstance(instance, nullptr);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 6;
    }
    probe_log(ANDROID_LOG_INFO, "SDL_Vulkan_CreateSurface succeeded; continuing to device-only probe");

    uint32_t device_count = 0;
    result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    probe_log(ANDROID_LOG_INFO, "vkEnumeratePhysicalDevices count=%u result=%s (%d)", device_count, vk_result_name(result), result);
    if (result != VK_SUCCESS || device_count == 0) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 7;
    }

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());

    VkPhysicalDevice physical_device = physical_devices[0];
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    probe_log(ANDROID_LOG_INFO, "physical device 0: name=\"%s\" vendor=0x%X device=0x%X type=%s driver=0x%X api=%u.%u.%u",
              properties.deviceName,
              properties.vendorID,
              properties.deviceID,
              physical_device_type_name(properties.deviceType),
              properties.driverVersion,
              VK_VERSION_MAJOR(properties.apiVersion),
              VK_VERSION_MINOR(properties.apiVersion),
              VK_VERSION_PATCH(properties.apiVersion));

    uint32_t queue_family = 0;
    if (!find_graphics_present_queue(physical_device, surface, &queue_family)) {
        probe_log(ANDROID_LOG_ERROR, "No graphics+present queue family found");
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 8;
    }

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;

    VkDevice device = VK_NULL_HANDLE;
    result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
    probe_log(ANDROID_LOG_INFO, "vkCreateDevice result=%s (%d)", vk_result_name(result), result);
    if (result != VK_SUCCESS) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 9;
    }

    probe_log(ANDROID_LOG_INFO, "Vulkan device created; continuing to surface caps/formats/present modes");

    VkSurfaceCapabilitiesKHR capabilities{};
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);
    probe_log(ANDROID_LOG_INFO,
              "surface caps result=%s (%d) currentExtent=%ux%u min=%ux%u max=%ux%u minImages=%u maxImages=%u usage=0x%X transforms=0x%X currentTransform=0x%X composite=0x%X",
              vk_result_name(result),
              result,
              capabilities.currentExtent.width,
              capabilities.currentExtent.height,
              capabilities.minImageExtent.width,
              capabilities.minImageExtent.height,
              capabilities.maxImageExtent.width,
              capabilities.maxImageExtent.height,
              capabilities.minImageCount,
              capabilities.maxImageCount,
              capabilities.supportedUsageFlags,
              capabilities.supportedTransforms,
              capabilities.currentTransform,
              capabilities.supportedCompositeAlpha);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data());
    for (uint32_t i = 0; i < format_count; i++) {
        probe_log(ANDROID_LOG_INFO, "surface format %u: format=%u colorSpace=%u", i, formats[i].format, formats[i].colorSpace);
    }

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes.data());
    for (uint32_t i = 0; i < present_mode_count; i++) {
        probe_log(ANDROID_LOG_INFO, "present mode %u: %u", i, present_modes[i]);
    }

    probe_log(ANDROID_LOG_INFO, "Vulkan surface queries completed; continuing to swapchain creation");

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max()) {
        extent.width = static_cast<uint32_t>(std::max(drawable_width, 0));
        extent.height = static_cast<uint32_t>(std::max(drawable_height, 0));
        extent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, extent.width));
        extent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, extent.height));
    }

    VkSurfaceFormatKHR chosen_format = format_count > 0 ? formats[0] : VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen_format = format;
            break;
        }
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = mode;
            break;
        }
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info{};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = chosen_format.format;
    swapchain_info.imageColorSpace = chosen_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha);
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    result = vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain);
    probe_log(ANDROID_LOG_INFO,
              "vkCreateSwapchainKHR result=%s (%d) extent=%ux%u minImages=%u format=%u colorSpace=%u usage=0x%X preTransform=0x%X compositeAlpha=0x%X presentMode=%u",
              vk_result_name(result),
              result,
              extent.width,
              extent.height,
              image_count,
              chosen_format.format,
              chosen_format.colorSpace,
              swapchain_info.imageUsage,
              swapchain_info.preTransform,
              swapchain_info.compositeAlpha,
              swapchain_info.presentMode);

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    uint32_t swapchain_image_count = 0;
    if (swapchain != VK_NULL_HANDLE) {
        result = vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);
        probe_log(ANDROID_LOG_INFO, "vkGetSwapchainImagesKHR count result=%s (%d) count=%u",
                  vk_result_name(result),
                  result,
                  swapchain_image_count);
    }

    std::vector<VkImage> swapchain_images(swapchain_image_count);
    if (swapchain != VK_NULL_HANDLE && swapchain_image_count > 0) {
        result = vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());
        probe_log(ANDROID_LOG_INFO, "vkGetSwapchainImagesKHR list result=%s (%d)", vk_result_name(result), result);
    }

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore image_available = VK_NULL_HANDLE;
    result = vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available);
    probe_log(ANDROID_LOG_INFO, "vkCreateSemaphore image_available result=%s (%d)", vk_result_name(result), result);
    VkSemaphore render_finished = VK_NULL_HANDLE;
    result = vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished);
    probe_log(ANDROID_LOG_INFO, "vkCreateSemaphore render_finished result=%s (%d)", vk_result_name(result), result);

    std::vector<VkImageView> image_views;
    image_views.reserve(swapchain_image_count);
    for (uint32_t i = 0; i < swapchain_image_count; i++) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = chosen_format.format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VkImageView view = VK_NULL_HANDLE;
        result = vkCreateImageView(device, &view_info, nullptr, &view);
        probe_log(ANDROID_LOG_INFO, "vkCreateImageView %u result=%s (%d)", i, vk_result_name(result), result);
        image_views.push_back(view);
    }

    VkAttachmentDescription color_attachment{};
    color_attachment.format = chosen_format.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    result = vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass);
    probe_log(ANDROID_LOG_INFO, "vkCreateRenderPass result=%s (%d)", vk_result_name(result), result);

    std::vector<VkFramebuffer> framebuffers;
    framebuffers.reserve(image_views.size());
    for (uint32_t i = 0; i < image_views.size(); i++) {
        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &image_views[i];
        framebuffer_info.width = extent.width;
        framebuffer_info.height = extent.height;
        framebuffer_info.layers = 1;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        result = vkCreateFramebuffer(device, &framebuffer_info, nullptr, &framebuffer);
        probe_log(ANDROID_LOG_INFO, "vkCreateFramebuffer %u result=%s (%d)", i, vk_result_name(result), result);
        framebuffers.push_back(framebuffer);
    }

    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queueFamilyIndex = queue_family;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    result = vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool);
    probe_log(ANDROID_LOG_INFO, "vkCreateCommandPool result=%s (%d)", vk_result_name(result), result);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (command_pool != VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo command_buffer_info{};
        command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_info.commandPool = command_pool;
        command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_info.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffer);
        probe_log(ANDROID_LOG_INFO, "vkAllocateCommandBuffers result=%s (%d)", vk_result_name(result), result);
    }

    const uint32_t start = SDL_GetTicks();
    SDL_Event event{};
    while (SDL_GetTicks() - start < 600000) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                break;
            }
        }

        if (swapchain != VK_NULL_HANDLE && image_available != VK_NULL_HANDLE && render_finished != VK_NULL_HANDLE) {
            uint32_t image_index = 0;
            result = vkAcquireNextImageKHR(device, swapchain, 1000000000ULL, image_available, VK_NULL_HANDLE, &image_index);
            if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
                if (command_buffer != VK_NULL_HANDLE) {
                    VkCommandBufferBeginInfo begin_info{};
                    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    VkResult command_result = vkBeginCommandBuffer(command_buffer, &begin_info);
                    if (command_result == VK_SUCCESS) {
                        VkClearValue clear{};
                        clear.color.float32[0] = static_cast<float>((SDL_GetTicks() / 8) % 255) / 255.0f;
                        clear.color.float32[1] = static_cast<float>((SDL_GetTicks() / 13) % 255) / 255.0f;
                        clear.color.float32[2] = 0.35f;
                        clear.color.float32[3] = 1.0f;

                        VkRenderPassBeginInfo render_pass_begin{};
                        render_pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        render_pass_begin.renderPass = render_pass;
                        render_pass_begin.framebuffer = image_index < framebuffers.size() ? framebuffers[image_index] : VK_NULL_HANDLE;
                        render_pass_begin.renderArea.offset = { 0, 0 };
                        render_pass_begin.renderArea.extent = extent;
                        render_pass_begin.clearValueCount = 1;
                        render_pass_begin.pClearValues = &clear;

                        vkCmdBeginRenderPass(command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
                        vkCmdEndRenderPass(command_buffer);
                        command_result = vkEndCommandBuffer(command_buffer);
                    }
                    if (command_result == VK_SUCCESS) {
                        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                        VkSubmitInfo submit_info{};
                        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                        submit_info.waitSemaphoreCount = 1;
                        submit_info.pWaitSemaphores = &image_available;
                        submit_info.pWaitDstStageMask = &wait_stage;
                        submit_info.commandBufferCount = 1;
                        submit_info.pCommandBuffers = &command_buffer;
                        submit_info.signalSemaphoreCount = 1;
                        submit_info.pSignalSemaphores = &render_finished;
                        command_result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
                        if (command_result == VK_SUCCESS) {
                            command_result = vkQueueWaitIdle(queue);
                        }
                    }
                    if (command_result != VK_SUCCESS) {
                        probe_log(ANDROID_LOG_ERROR, "empty command submit failed: %s (%d)", vk_result_name(command_result), command_result);
                        break;
                    }
                }

                VkPresentInfoKHR present_info{};
                present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                present_info.waitSemaphoreCount = 1;
                present_info.pWaitSemaphores = &render_finished;
                present_info.swapchainCount = 1;
                present_info.pSwapchains = &swapchain;
                present_info.pImageIndices = &image_index;
                VkResult present_result = vkQueuePresentKHR(queue, &present_info);
                static uint32_t present_count = 0;
                if ((present_count++ % 60) == 0) {
                    probe_log(ANDROID_LOG_INFO,
                              "acquire/present result acquire=%s (%d) present=%s (%d) image=%u",
                              vk_result_name(result),
                              result,
                              vk_result_name(present_result),
                              present_result,
                              image_index);
                }
                if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
                    probe_log(ANDROID_LOG_ERROR, "vkQueuePresentKHR failed: %s (%d)", vk_result_name(present_result), present_result);
                    break;
                }
            } else {
                probe_log(ANDROID_LOG_ERROR, "vkAcquireNextImageKHR failed: %s (%d)", vk_result_name(result), result);
                break;
            }
        }

        SDL_Delay(16);
    }

    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }
    for (VkFramebuffer framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    if (render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass, nullptr);
    }
    for (VkImageView view : image_views) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    if (render_finished != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, render_finished, nullptr);
    }
    if (image_available != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, image_available, nullptr);
    }
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    probe_log(ANDROID_LOG_INFO, "Vulkan smoke probe completed");
    return result == VK_SUCCESS ? 0 : 10;
}
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetLogPaths(
    JNIEnv* env,
    jclass,
    jstring java_log_path,
    jstring) {
    const char* log_path_chars = java_log_path != nullptr ? env->GetStringUTFChars(java_log_path, nullptr) : nullptr;
    copy_path(log_path, sizeof(log_path), log_path_chars);
    if (log_path_chars != nullptr) {
        env->ReleaseStringUTFChars(java_log_path, log_path_chars);
    }
    probe_log(ANDROID_LOG_INFO, "Native log path ready for Vulkan smoke probe");
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetAndroidSurfaceReady(
    JNIEnv*,
    jclass,
    jboolean ready) {
    probe_log(ANDROID_LOG_VERBOSE, "surface ready=%d", ready ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetAppAudioActive(
    JNIEnv*,
    jclass,
    jboolean active) {
    probe_log(ANDROID_LOG_VERBOSE, "audio active=%d", active ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeOnFileDialogResult(
    JNIEnv*,
    jclass,
    jboolean,
    jstring) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeOnFileDialogMultipleResult(
    JNIEnv*,
    jclass,
    jboolean,
    jobjectArray) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeReloadClockTexturePack(
    JNIEnv*,
    jclass) {
}

extern "C" __attribute__((visibility("default"))) jboolean Java_io_github_zelda64recomp_ZeldaSDLActivity_attachController(
    JNIEnv*,
    jobject) {
    if (virtual_joystick != nullptr) {
        return JNI_TRUE;
    }
    if ((SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) == 0) {
        return JNI_FALSE;
    }

    virtual_joystick_id = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (virtual_joystick_id < 0) {
        return JNI_FALSE;
    }

    virtual_joystick = SDL_JoystickOpen(virtual_joystick_id);
    if (virtual_joystick == nullptr) {
        SDL_JoystickDetachVirtual(virtual_joystick_id);
        virtual_joystick_id = -1;
        virtual_joystick_instance_id = -1;
        return JNI_FALSE;
    }

    virtual_joystick_instance_id = SDL_JoystickInstanceID(virtual_joystick);
    return JNI_TRUE;
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_detachController(
    JNIEnv*,
    jobject) {
    if (virtual_joystick != nullptr) {
        SDL_JoystickClose(virtual_joystick);
        virtual_joystick = nullptr;
    }
    if (virtual_joystick_id >= 0) {
        SDL_JoystickDetachVirtual(virtual_joystick_id);
        virtual_joystick_id = -1;
    }
    virtual_joystick_instance_id = -1;
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_setButton(
    JNIEnv*,
    jobject,
    jint button,
    jboolean value) {
    if (virtual_joystick != nullptr) {
        SDL_JoystickSetVirtualButton(virtual_joystick, button, value ? 1 : 0);
    }
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_setAxis(
    JNIEnv*,
    jobject,
    jint axis,
    jshort value) {
    if (virtual_joystick != nullptr) {
        SDL_JoystickSetVirtualAxis(virtual_joystick, axis, value);
    }
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_submitAndroidMotionSensor(
    JNIEnv*,
    jobject,
    jint sensor_type,
    jfloat x,
    jfloat y,
    jfloat z,
    jlong timestamp_ns) {
    SDL_Event event{};
    event.type = SDL_CONTROLLERSENSORUPDATE;
    event.csensor.timestamp = static_cast<Uint32>(timestamp_ns / 1000000);
    event.csensor.which = virtual_joystick_instance_id >= 0 ? virtual_joystick_instance_id : kAndroidMotionInstanceId;
    event.csensor.sensor = sensor_type == 0 ? SDL_SENSOR_ACCEL : SDL_SENSOR_GYRO;
    event.csensor.data[0] = x;
    event.csensor.data[1] = y;
    event.csensor.data[2] = z;
    event.csensor.timestamp_us = static_cast<Uint64>(timestamp_ns / 1000);
    SDL_PushEvent(&event);
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return run_probe();
}
