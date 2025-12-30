#include "VulkanSDL2App.h"
#include <iostream>
#include <set>

#include "../shaders/vert_spv.h"
#include "../shaders/frag_spv.h"


#ifdef NDEBUG
static const bool enableValidationLayers = false;
#else
static const bool enableValidationLayers = true;
#endif

static const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

static const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageType, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

PFN_vkCreateDebugUtilsMessengerEXT  pfnVkCreateDebugUtilsMessengerEXT;
PFN_vkDestroyDebugUtilsMessengerEXT pfnVkDestroyDebugUtilsMessengerEXT;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT( VkInstance                                 instance,
                                                               const VkDebugUtilsMessengerCreateInfoEXT * pCreateInfo,
                                                               const VkAllocationCallbacks *              pAllocator,
                                                               VkDebugUtilsMessengerEXT *                 pMessenger )
{
    return pfnVkCreateDebugUtilsMessengerEXT( instance, pCreateInfo, pAllocator, pMessenger );
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT( VkInstance instance, VkDebugUtilsMessengerEXT messenger, VkAllocationCallbacks const * pAllocator )
{
    return pfnVkDestroyDebugUtilsMessengerEXT( instance, messenger, pAllocator );
}


VulkanSDL2App::VulkanSDL2App(std::string title, int width, int height, bool DiscreteGpuFirst) {
    this->title = title;
    this->windowWidth = width;
    this->windowHeight = height;
    this->DiscreteGpuFirst = DiscreteGpuFirst;

    initWindow();
    initVulkan();
}

VulkanSDL2App::~VulkanSDL2App() {
    cleanupSwapChain();

    device.destroyPipeline(graphicsPipeline);

    device.destroyPipelineLayout(graphicsPipelineLayout);

    device.destroyDescriptorPool(graphicsDescriptorPool);

    device.destroyDescriptorSetLayout(graphicsDescriptorSetLayout);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        device.destroySemaphore(imageAvailableSemaphores[i]);
        device.destroySemaphore(renderFinishedSemaphores[i]);
        device.destroyFence(inFlightFences[i]);
    }

    device.destroyRenderPass(renderPass);

    textures = std::vector<Texture>();

    device.destroyBuffer(vertexBuffer);
    device.freeMemory(vertexBufferMemory);

    device.freeCommandBuffers(commandPool, commandBuffers.size(), commandBuffers.data());
    device.destroyCommandPool(commandPool);

    device.destroy();

    if (enableValidationLayers) {
        instance.destroyDebugUtilsMessengerEXT(debugMessenger);
    }
    instance.destroySurfaceKHR(surface);
    instance.destroy();
}

void VulkanSDL2App::run() {
    SDL_Event event;

    audioPlayer->run();
    ffmpegDecoder->run();

    printAppInfos();

    drawThread = std::thread(&VulkanSDL2App::draw, this);
    drawThread.detach();

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                ffmpegDecoder->stop();
                break;
            }
            switch (event.type) {
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                        case SDLK_q:
                            running = false;
                            ffmpegDecoder->stop();
                            break;
                        case SDLK_SPACE:
                        case SDLK_p:
                            togglePause();
                            break;
                        case SDLK_f:
                            toggleFullscreen();
                            break;
                        case SDLK_UP:
                            updateVolume(1);
                            break;
                        case SDLK_DOWN:
                            updateVolume(-1);
                            break;
                        case SDLK_LEFT:
                            ffmpegDecoder->seekTime(-10);
                            break;
                        case SDLK_RIGHT:
                            ffmpegDecoder->seekTime(10);
                            break;
                        case SDLK_d:
                            ffmpegDecoder->seekTime(60);
                            break;
                        case SDLK_a:
                            ffmpegDecoder->seekTime(-60);
                            break;
                        case SDLK_PAGEUP:
                            ffmpegDecoder->seekTime(600);
                            break;
                        case SDLK_PAGEDOWN:
                            ffmpegDecoder->seekTime(-600);
                            break;
                        default:
                            break;
                    }
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        static bool firstClick = true;
                        static auto lastClick = std::chrono::high_resolution_clock::now();
                        if (firstClick) {
                            firstClick = false;
                            break;
                        }
                        if (std::chrono::duration_cast<std::chrono::milliseconds>
                            (std::chrono::high_resolution_clock::now() - lastClick).count() <= 500) {
                            toggleFullscreen();
                        }
                        lastClick = std::chrono::high_resolution_clock::now();
                    }
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        int x = event.button.x;
                        double seekTime = (double)x / windowWidth * ffmpegDecoder->getDuration() - ffmpegDecoder->getRelativeTime();
                        ffmpegDecoder->seekTime(seekTime);
                    }
                    break;
                case SDL_WINDOWEVENT:
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_SIZE_CHANGED:
                            windowWidth = event.window.data1;
                            windowHeight = event.window.data2;
                            if (!frameBufferResized) {
                                frameBufferResized = true;
                            }

                            break;
                        default:
                            break;
                    }
                default:
                    break;
            }
            if (!running) {
                break;
            }
        }
        // Avoid high cpu usage on the main thread
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    while (!drawThreadExited || !ffmpegDecoder->isEnded()) {}
    std::printf("application exiting...\n");
}

void VulkanSDL2App::draw() {
    const double dt = ffmpegDecoder->getDeltaTime();
    while (running) {
        auto time1 = std::chrono::high_resolution_clock::now();
        DrawFrame();
        auto time2 = std::chrono::high_resolution_clock::now();
        long sleepTime = static_cast<long>(dt * 1000) - std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1).count();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime > 0 ? sleepTime : 0));
    }
    graphicsQueue.waitIdle();
    presentQueue.waitIdle();
    std::printf("drawThread exiting...\n");
    drawThreadExited = true;
}

//------------------------------------------------------

void VulkanSDL2App::initWindow() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        auto error = "Initialize SDL failed: " + std::string(SDL_GetError());
        throw std::runtime_error(error);
    }

    audioPlayer = new SDLAudioPlayer();
    auto spec = audioPlayer->getAudioSpec();
    ffmpegDecoder = new FFmpegDecoder(this->title, spec);
    audioPlayer->setFFmpegDecoder(ffmpegDecoder);

    if (SDL_GetNumVideoDisplays() < 0) {
        auto error = "No available displays: " + std::string(SDL_GetError());
        throw std::runtime_error(error);
    }

    SDL_DisplayMode displayMode;
    SDL_GetDesktopDisplayMode(0, &displayMode);

    // display width and height
    int displayWidth = displayMode.w;
    int displayHeight = displayMode.h;
    double aspectRatioDisplay = (double) displayWidth / (double) displayHeight;

    // media width and height
    auto mediaSize = ffmpegDecoder->getVideoSize();
    mediaWidth = mediaSize[0];
    mediaHeight = mediaSize[1];
    double aspectRatioMedia = (double) mediaWidth / (double) mediaHeight;

    // get final window size
    if (mediaWidth <= displayWidth && mediaHeight <= displayHeight) {
        windowWidth = mediaWidth;
        windowHeight = mediaHeight;
    } else if (aspectRatioMedia > aspectRatioDisplay) {
        windowWidth = displayWidth;
        windowHeight = (double) windowWidth / aspectRatioMedia;
    } else {
        windowHeight = displayHeight;
        windowWidth =  (double) windowHeight * aspectRatioMedia;
    }


    window = SDL_CreateWindow(
        title.data(),
        0, 0, windowWidth, windowHeight,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if (window == nullptr) {
        auto error = "Create window failed: " + std::string(SDL_GetError());
        throw std::runtime_error(error);
    }
}

void VulkanSDL2App::printAppInfos() {
    double duration = ffmpegDecoder->getDuration();
    bool isVideo = ffmpegDecoder->isVideo();
    double fps = ffmpegDecoder->getFps();
    auto resolution = ffmpegDecoder->getVideoSize();
    std::printf("media path:        %s\n", title.c_str());
    if (isVideo) {
        std::printf("resolution:        %d x %d \n", resolution[0], resolution[1]);
        std::printf("fps:               %lf\n", fps);
    } else {
        std::printf("cover size:        %d x %d \n", resolution[0], resolution[1]);
    }
    std::printf("media duration:    %02lld:%02lld:%02lld\n",
        static_cast<long long>(duration) / 3600,
        (static_cast<long long>(duration) - static_cast<long long>(duration) / 3600) / 60,
        static_cast<long long>(duration - static_cast<double>(static_cast<long long>(duration) - static_cast<long long>(duration) % 60))
        );
    std::printf("Selected GPU:      %s\n", physicalDeviceName.c_str());
    std::printf("Audio device:      %s\n", audioPlayer->getDeviceName().c_str());
    std::printf("\nWhile playing:\n"
        "q, ESC             quit\n"
        "f                  toggle full screen\n"
        "p, SPC             pause\n"
        "down/up            decrease and increase volume respectively\n"
        "left/right         seek backward/forward 10 seconds\n"
        "a/d                seek backward/forward 1 minute\n"
        "page down/page up  seek backward/forward 10 minutes\n"
        "right mouse click  seek to percentage in file corresponding to fraction of width\n"
        "left double-click  toggle full screen\n\n"
        );
}

void VulkanSDL2App::toggleFullscreen() {
    SDL_SetWindowFullscreen(window, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    isFullscreen = !isFullscreen;
}

void VulkanSDL2App::togglePause() {
    ffmpegDecoder->pause();
}

void VulkanSDL2App::updateVolume(int sign) {
    audioPlayer->updateVolume(sign);
}

void VulkanSDL2App::initVulkan() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createFrameBuffers();
    createGraphicsDescriptorSetLayout();
    createGraphicsPipeline();
    createCommandPool();
    createCommandBuffers();
    createVertexBuffer();
    createDescriptorPool();
    createDescriptorSets();
    initTextureResource();
    createSyncObjects();
}

void VulkanSDL2App::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    vk::ApplicationInfo appInfo = {
        "Vulkan SDL2",
        vk::makeApiVersion(1, 0, 0, 0),
        "No Engine",
        vk::makeApiVersion(1, 0, 0, 0),
        VK_API_VERSION_1_0
    };

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;
    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();


    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
            {}, vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            debugCallback
    };
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        createInfo.pNext = (vk::DebugUtilsMessengerCreateInfoEXT*)& debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    instance = vk::createInstance(createInfo);

    if (enableValidationLayers) {
        pfnVkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>( instance.getProcAddr( "vkCreateDebugUtilsMessengerEXT" ) );
        if ( !pfnVkCreateDebugUtilsMessengerEXT ) {
            throw std::runtime_error("GetInstanceProcAddr: Unable to find pfnVkCreateDebugUtilsMessengerEXT function.");
        }

        pfnVkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( instance.getProcAddr( "vkDestroyDebugUtilsMessengerEXT" ) );
        if ( !pfnVkDestroyDebugUtilsMessengerEXT ) {
            throw std::runtime_error("GetInstanceProcAddr: Unable to find pfnVkDestroyDebugUtilsMessengerEXT function.");
        }

        debugMessenger = instance.createDebugUtilsMessengerEXT(debugCreateInfo);
    }
}

void VulkanSDL2App::createSurface() {
    VkSurfaceKHR t_surface;
    if (SDL_Vulkan_CreateSurface(window, instance, &t_surface) != SDL_TRUE) {
        throw std::runtime_error("Failed to create Vulkan surface!");
    }
    surface = t_surface;
}

void VulkanSDL2App::pickPhysicalDevice() {
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("There is no physical devices!");
    }
    std::vector<vk::PhysicalDevice> suitableDevices;
    for (const auto& dev : devices) {
        if (isDeviceSuitable(dev)) {
            suitableDevices.push_back(dev);
        }
    }
    if (suitableDevices.empty()) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }

    physicalDevice = suitableDevices[0];
    if (DiscreteGpuFirst) {
        for (const auto& dev : suitableDevices) {
            if (dev.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                physicalDevice = dev;
                break;
            }
        }
    } else {
        for (const auto& dev : suitableDevices) {
            if (dev.getProperties().deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
                physicalDevice = dev;
                break;
            }
        }
    }
    physicalDeviceName = std::string(physicalDevice.getProperties().deviceName);
}

void VulkanSDL2App::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsAndComputeFamily.value(), indices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        queueCreateInfos.push_back(queueCreateInfo);
    }

    vk::PhysicalDeviceFeatures deviceFeatures;

    vk::DeviceCreateInfo createInfo;
    createInfo.setQueueCreateInfos(queueCreateInfos);
    createInfo.setPEnabledFeatures(&deviceFeatures);
    createInfo.setPEnabledExtensionNames(deviceExtensions);
    if (enableValidationLayers) {
        createInfo.setPEnabledLayerNames(validationLayers);
    } else {
        createInfo.setPEnabledLayerNames(nullptr);
    }

    device = physicalDevice.createDevice(createInfo);

    graphicsQueue = device.getQueue(indices.graphicsAndComputeFamily.value(), 0);
    computeQueue = device.getQueue(indices.graphicsAndComputeFamily.value(), 0);
    presentQueue = device.getQueue(indices.presentFamily.value(), 0);
}

void VulkanSDL2App::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    vk::SwapchainKHR oldSwapChain = swapChain;

    vk::SwapchainCreateInfoKHR createInfo = {};
    if (oldSwapChain!=vk::SwapchainKHR{}) {
        createInfo.oldSwapchain = oldSwapChain;
    }

    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;

    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;

    createInfo.imageExtent = extent;

    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsAndComputeFamily.value(), indices.presentFamily.value()};
    if (indices.graphicsAndComputeFamily.value() != indices.presentFamily.value()) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    vk::SwapchainKHR newSwapChain = device.createSwapchainKHR(createInfo);
    presentQueue.waitIdle();
    device.waitIdle();
    cleanupSwapChain();
    swapChain = newSwapChain;
    swapChainImages = device.getSwapchainImagesKHR(swapChain);

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;

    MAX_FRAMES_IN_FLIGHT = swapChainImages.size();
}

void VulkanSDL2App::createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); ++i) {
        vk::ImageViewCreateInfo createInfo = {
            vk::ImageViewCreateFlags(),
            swapChainImages[i],
            vk::ImageViewType::e2D,
            swapChainImageFormat,
            {
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity
            },
            {
                vk::ImageAspectFlagBits::eColor,
                0, 1, 0, 1
            }
        };

        swapChainImageViews[i] = device.createImageView(createInfo);
    }
}

void VulkanSDL2App::createRenderPass() {
    vk::AttachmentDescription colorAttachment = {
        vk::AttachmentDescriptionFlags(),
        swapChainImageFormat,
        vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::ePresentSrcKHR
    };

    vk::AttachmentReference colorAttachmentRef ={
        0, vk::ImageLayout::eColorAttachmentOptimal
    };


    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    vk::SubpassDependency dependency = {
        vk::SubpassExternal,
        0u,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::AccessFlagBits::eNone
    };


    vk::RenderPassCreateInfo renderPassInfo = {
        vk::RenderPassCreateFlags(),
        1, &colorAttachment,
        1, &subpass,
        1, &dependency
    };
    renderPass = device.createRenderPass(renderPassInfo);
}

void VulkanSDL2App::createFrameBuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); ++i) {
        vk::ImageView attachments[] = {
            swapChainImageViews[i]
        };
        vk::FramebufferCreateInfo framebufferInfo = {
            vk::FramebufferCreateFlags(),
            renderPass,
            1,
            attachments,
            swapChainExtent.width,
            swapChainExtent.height,
            1
        };
        swapChainFramebuffers[i] = device.createFramebuffer(framebufferInfo);
    }
}

void VulkanSDL2App::createGraphicsDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding samplerLayoutBinding;
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    std::array<vk::DescriptorSetLayoutBinding, 1> bindings = {
        samplerLayoutBinding
    };

    vk::DescriptorSetLayoutCreateInfo createInfo(
        vk::DescriptorSetLayoutCreateFlags(),
        bindings.size(),
        bindings.data()
    );

    graphicsDescriptorSetLayout = device.createDescriptorSetLayout(createInfo);
}

void VulkanSDL2App::createGraphicsPipeline() {
    vk::ShaderModule vertShaderModule = createShaderModule(vert_spv, vert_spv_len);
    vk::ShaderModule fragShaderModule = createShaderModule(frag_spv, frag_spv_len);

    // shaderStageCreateInfo
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo = {
        {}, vk::ShaderStageFlagBits::eVertex,
        vertShaderModule, "main", nullptr, nullptr
    };
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo = {
        {}, vk::ShaderStageFlagBits::eFragment,
        fragShaderModule, "main", nullptr, nullptr
    };
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vertShaderStageInfo, fragShaderStageInfo
    };

    // about input data
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {
        {}, 1, &bindingDescription,
        static_cast<uint32_t>(attributeDescriptions.size()),
        attributeDescriptions.data()
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {
        {}, vk::PrimitiveTopology::eTriangleFan,
        vk::False, nullptr,
    };

    // viewport state
    vk::PipelineViewportStateCreateInfo viewportState = {
        {}, 1, {},
        1, {}, nullptr
    };

    // Rasterization State
    vk::PipelineRasterizationStateCreateInfo rasterizer = {
        {}, vk::False, vk::False,
        vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise,vk::False,
        0.0f, 0.0f, 0.0f,
        1.0f, nullptr
    };

    // multisample State
    vk::PipelineMultisampleStateCreateInfo multisampling = {
        {}, vk::SampleCountFlagBits::e1,
        vk::False, 0.2f,
        nullptr, vk::False, vk::False
    };

    // color blend State
    vk::PipelineColorBlendAttachmentState colorBlendAttachment = {
        vk::False,
        vk::BlendFactor::eOne, vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        vk::BlendFactor::eOne, vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
        | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };
    vk::PipelineColorBlendStateCreateInfo colorBlending = {
        {}, vk::False, vk::LogicOp::eCopy,
        1, &colorBlendAttachment,
        {0.0f, 0.0f, 0.0f, 0.0f}
    };

    // dynamic states
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamicState = {
        {}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data(), nullptr
    };

    // pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {
        {}, 1, &graphicsDescriptorSetLayout,
        0, nullptr, nullptr
    };
    graphicsPipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

    // create pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo = {
        {},
        2, shaderStages.data(),
        &vertexInputInfo, &inputAssembly,
        {},
        &viewportState, &rasterizer,
        &multisampling, {},
        &colorBlending, &dynamicState,
        graphicsPipelineLayout,
        renderPass, 0,
        {}, -1
    };

    auto res = device.createGraphicsPipeline({}, pipelineInfo);
    if (res.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    graphicsPipeline = res.value;

    device.destroyShaderModule(vertShaderModule);
    device.destroyShaderModule(fragShaderModule);
}

void VulkanSDL2App::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    vk::CommandPoolCreateInfo poolInfo = {
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndices.graphicsAndComputeFamily.value(),
        nullptr
    };
    commandPool = device.createCommandPool(poolInfo);
}

void VulkanSDL2App::createCommandBuffers() {
    commandBuffers = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo(
            commandPool,
            vk::CommandBufferLevel::ePrimary,
            static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
        )
    );
}

void VulkanSDL2App::createVertexBuffer() {
    std::vector<Vertex> vertices = {
        {{-1, 1}, {0, 1}},
        {{1, 1}, {1, 1}},
        {{1, -1}, {1, 0}},
        {{-1, -1}, {0, 0}},
    };

    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent,
        stagingBuffer, stagingBufferMemory
    );

    void *data = device.mapMemory(stagingBufferMemory, 0, bufferSize);
    memcpy(data, vertices.data(), (size_t) bufferSize);
    device.unmapMemory(stagingBufferMemory);

    createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vertexBuffer, vertexBufferMemory
    );

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    device.destroyBuffer(stagingBuffer);
    device.freeMemory(stagingBufferMemory);
}

void VulkanSDL2App::createDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 1> poolSizes;
    poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    graphicsDescriptorPool = device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo(
            {}, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
        )
    );
}

void VulkanSDL2App::createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, graphicsDescriptorSetLayout);
    graphicsDescriptorSets = device.allocateDescriptorSets(
        vk::DescriptorSetAllocateInfo(
            graphicsDescriptorPool, static_cast<uint32_t>(layouts.size()), layouts.data()
        )
    );
}

void VulkanSDL2App::initTextureResource() {
    const auto texture = Texture(device);
    textures.assign(MAX_FRAMES_IN_FLIGHT, texture);
}

void VulkanSDL2App::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo = {};
    vk::FenceCreateInfo fenceInfo = {
        vk::FenceCreateFlagBits::eSignaled
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        imageAvailableSemaphores[i] = device.createSemaphore(semaphoreInfo);
        renderFinishedSemaphores[i] = device.createSemaphore(semaphoreInfo);
        inFlightFences[i] = device.createFence(fenceInfo);
    }
}

void VulkanSDL2App::DrawFrame() {
    if (device.waitForFences(1, &inFlightFences[currentFrame],
        vk::True, UINT64_MAX) != vk::Result::eSuccess) {
        throw std::runtime_error("waitForFences error!");
    }

    uint32_t imageIndex;
    try {
        auto res = device.acquireNextImageKHR(swapChain, UINT64_MAX,
        imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE,
        &imageIndex);
        if (res == vk::Result::eErrorOutOfDateKHR) {
            reCreateSwapChain();
            return;
        }
    } catch (const vk::OutOfDateKHRError&) {
        reCreateSwapChain();
        return;
    }

    updateTexture(imageIndex);

    if (device.resetFences(1, &inFlightFences[currentFrame]) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to reset fence!");
    }

    commandBuffers[currentFrame].reset(vk::CommandBufferResetFlags(0));

    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    vk::Semaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

    vk::SubmitInfo submitInfo = {
        1, waitSemaphores, waitStages,
        1, &commandBuffers[currentFrame],
        1, signalSemaphores
    };

    if (graphicsQueue.submit(1, &submitInfo, inFlightFences[currentFrame]) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to submit draw command!");
    }

    vk::PresentInfoKHR presentInfo = {
        1, signalSemaphores,
        1, &swapChain, &imageIndex, nullptr
    };

    try {
        auto res = presentQueue.presentKHR(presentInfo);
        if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR || frameBufferResized) {
            reCreateSwapChain();
            return;
        }
    } catch (const vk::OutOfDateKHRError&) {
        reCreateSwapChain();
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanSDL2App::cleanupSwapChain() {
    for (auto framebuffer : swapChainFramebuffers) {
        device.destroyFramebuffer(framebuffer);
    }

    for (auto imageView : swapChainImageViews) {
        device.destroyImageView(imageView);
    }

    device.destroySwapchainKHR(swapChain);
}

void VulkanSDL2App::reCreateSwapChain() {
    createSwapChain();
    createImageViews();
    createFrameBuffers();

    frameBufferResized = false;
}

void VulkanSDL2App::updateTexture(uint32_t imageIndex) {
    if (textures[imageIndex].useful) {
        textures[imageIndex].destroy();
    }

    auto frame = ffmpegDecoder->getVideoFrame();

    int textureWidth = frame->data->width;
    int textureHeight = frame->data->height;
    vk::DeviceSize imageSize = textureWidth * textureHeight * 4;

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingMemory;
    createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        stagingBuffer, stagingMemory
    );

    void *data = device.mapMemory(stagingMemory, 0, imageSize);
    memcpy(data, frame->data->data[0], static_cast<size_t>(imageSize));
    device.unmapMemory(stagingMemory);


    createImage(static_cast<uint32_t>(textureWidth), static_cast<uint32_t>(textureHeight), 1, vk::SampleCountFlagBits::e1,
        vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
        | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal,
        textures[imageIndex].image, textures[imageIndex].memory
    );

    transitionImageLayout(textures[imageIndex].image, vk::Format::eR8G8B8A8Srgb,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1
    );

    copyBufferToImage(
        stagingBuffer, textures[imageIndex].image,
        static_cast<uint32_t>(textureWidth), static_cast<uint32_t>(textureHeight)
    );

    device.destroyBuffer(stagingBuffer);
    device.freeMemory(stagingMemory);

    transitionImageLayout(textures[imageIndex].image, vk::Format::eR8G8B8A8Srgb,
        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1
    );

    // create image view
    textures[imageIndex].imageView = device.createImageView(
        vk::ImageViewCreateInfo(
            {}, textures[imageIndex].image,
            vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Srgb, {},
            vk::ImageSubresourceRange(
                vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
            )
        )
    );

    // create sampler
    textures[imageIndex].sampler = device.createSampler(
        vk::SamplerCreateInfo(
            {}, vk::Filter::eLinear, vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eRepeat,
            vk::SamplerAddressMode::eRepeat,
            vk::SamplerAddressMode::eRepeat,
            0.0f, vk::False, 0,
            vk::False, vk::CompareOp::eAlways,
            0.0f, 0.0f,
            vk::BorderColor::eIntOpaqueBlack, vk::False
        )
    );

    // update descriptor set
    vk::DescriptorImageInfo imageInfo = {
        textures[imageIndex].sampler, textures[imageIndex].imageView, vk::ImageLayout::eShaderReadOnlyOptimal
    };

    std::array<vk::WriteDescriptorSet, 1> descriptorWrites = {
        vk::WriteDescriptorSet(
            graphicsDescriptorSets[imageIndex], 0, 0,
            1, vk::DescriptorType::eCombinedImageSampler,
            &imageInfo
        )
    };

    textures[imageIndex].useful = true;

    device.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void VulkanSDL2App::recordCommandBuffer(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
    // begin command buffer
    commandBuffer.begin(vk::CommandBufferBeginInfo());

    vk::RenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = vk::Offset2D(0, 0);
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<vk::ClearValue, 1> clearValues = {};
    clearValues[0].color = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    // begin render pass
    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // bind pipeline
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

    vk::Buffer vertexBuffers[] = {vertexBuffer};
    vk::DeviceSize offsets[] = {0};
    //bind vertex buffer
    commandBuffer.bindVertexBuffers(0, vertexBuffers, offsets);

    // viewport
    float viewX = 0.0, viewY = 0.0;
    int viewportWidth, viewportHeight;


    int extentWidth = swapChainExtent.width, extentHeight= swapChainExtent.height;

    float aspectRatioWindow = static_cast<float>(extentWidth) / static_cast<float>(extentHeight);
    float aspectRatioMedia = static_cast<float>(mediaWidth) / static_cast<float>(mediaHeight);

    if (aspectRatioMedia > aspectRatioWindow) {
        viewportWidth = extentWidth;
        viewportHeight = extentWidth / aspectRatioMedia;
        viewX = 0.0f;
        viewY = static_cast<float>(extentHeight - viewportHeight) / 2.0f;
    } else {
        viewportHeight = extentHeight;
        viewportWidth = extentHeight * aspectRatioMedia;
        viewX = static_cast<float>(extentWidth - viewportWidth) / 2.0f;
        viewY = 0.0f;
    }
    vk::Viewport viewport = {
        viewX, viewY,
        static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
        0.0f, 1.0f
    };
    commandBuffer.setViewport(0, 1, &viewport);

    vk::Rect2D scissor = {
        vk::Offset2D(0, 0),
        swapChainExtent
    };
    commandBuffer.setScissor(0, 1, &scissor);

    // bind descriptor set
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, graphicsPipelineLayout,
        0, 1, &graphicsDescriptorSets[imageIndex],
        0, nullptr
    );

    commandBuffer.draw(4, 1, 0, 0);

    // end render pass
    commandBuffer.endRenderPass();

    // end command buffer
    commandBuffer.end();
}

//--------------------------------
bool VulkanSDL2App::checkValidationLayerSupport() {
    auto availableLayers =  vk::enumerateInstanceLayerProperties();

    for (const char* layerName : validationLayers) {
        bool found = false;
        for (const auto& layerProperties : availableLayers) {
            if (!strcmp(layerProperties.layerName, layerName)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

std::vector<const char *> VulkanSDL2App::getRequiredExtensions() {
    std::vector<const char*> extensions;
    unsigned int glfwExtensionCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &glfwExtensionCount, nullptr);
    extensions.resize(glfwExtensionCount);
    SDL_Vulkan_GetInstanceExtensions(window, &glfwExtensionCount, extensions.data());

    if (enableValidationLayers) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }
    if (enableValidationLayers) {
        std::printf("\nInstance enabled extensions:\n");
        for (const auto& extension : extensions) {
            std::printf("%s\n", extension);
        }
    }
    return extensions;
}

bool VulkanSDL2App::isDeviceSuitable(vk::PhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
        return false;
    }

    bool supportSurface = true;
    vk::Bool32 graphicsSupported = VK_FALSE, presentSupported = VK_FALSE;
    if (device.getSurfaceSupportKHR(indices.graphicsAndComputeFamily.value(), surface, &graphicsSupported)
        != vk::Result::eSuccess) {
        return false;
    }
    if (device.getSurfaceSupportKHR(indices.presentFamily.value(), surface, &presentSupported)
        != vk::Result::eSuccess) {
        return false;
    }

    supportSurface = (graphicsSupported == vk::True) && (presentSupported == vk::True);

    return supportSurface;
}

VulkanSDL2App::QueueFamilyIndices VulkanSDL2App::findQueueFamilies(vk::PhysicalDevice device) {
    QueueFamilyIndices indices;
    auto queueFamiliesProperties = device.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamiliesProperties.size(); i++) {
        if (queueFamiliesProperties[i].queueFlags & vk::QueueFlagBits::eGraphics
            && queueFamiliesProperties[i].queueFlags & vk::QueueFlagBits::eCompute) {
            indices.graphicsAndComputeFamily = i;
        }

        if (device.getSurfaceSupportKHR(i, surface)) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}

VulkanSDL2App::SwapChainSupportDetails VulkanSDL2App::querySwapChainSupport(vk::PhysicalDevice device) {
    SwapChainSupportDetails details;
    details.capabilities = device.getSurfaceCapabilitiesKHR(surface);
    details.formats = device.getSurfaceFormatsKHR(surface);
    details.presentModes = device.getSurfacePresentModesKHR(surface);

    return details;
}

vk::SurfaceFormatKHR VulkanSDL2App::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

vk::PresentModeKHR VulkanSDL2App::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
            return availablePresentMode;
        }
    }

    return availablePresentModes[0];
}

vk::Extent2D VulkanSDL2App::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }
    int width, height;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);

    vk::Extent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

vk::ShaderModule VulkanSDL2App::createShaderModule(const unsigned char *code, unsigned int size) {
    vk::ShaderModuleCreateInfo info(
        vk::ShaderModuleCreateFlags(),
        size,
        reinterpret_cast<const uint32_t *>(code)
    );

    return device.createShaderModule(info);
}

void VulkanSDL2App::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties,
    vk::Buffer &buffer, vk::DeviceMemory &bufferMemory) {
    buffer = device.createBuffer(vk::BufferCreateInfo(vk::BufferCreateFlags(),
        size, usage, vk::SharingMode::eExclusive
    ));

    auto memRequirements = device.getBufferMemoryRequirements(buffer);

    bufferMemory = device.allocateMemory(
        vk::MemoryAllocateInfo(memRequirements.size, findMemoryType(memRequirements.memoryTypeBits, properties))
    );

    device.bindBufferMemory(buffer, bufferMemory, 0);
}

void VulkanSDL2App::copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
    auto commandBuffers = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo(
            commandPool,
            vk::CommandBufferLevel::ePrimary,
            1
        )
    );

    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    auto res =commandBuffers[0].begin(&beginInfo);

    commandBuffers[0].copyBuffer(
        srcBuffer, dstBuffer, vk::BufferCopy(0, 0, size)
    );

    commandBuffers[0].end();

    computeQueue.submit(
        vk::SubmitInfo(0, nullptr, nullptr, 1, commandBuffers.data(), 0, nullptr)
    );

    computeQueue.waitIdle();

    device.freeCommandBuffers(commandPool, commandBuffers);
}

void VulkanSDL2App::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
    vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties,
    vk::Image &image, vk::DeviceMemory &imageMemory) {
    image = device.createImage(
        vk::ImageCreateInfo(
            {}, vk::ImageType::e2D,
            format, {width, height, 1}, mipLevels, 1,
            numSamples, tiling, usage,vk::SharingMode::eExclusive,
            0, nullptr,
            vk::ImageLayout::eUndefined
        )
    );

    vk::MemoryRequirements memoryRequirements = device.getImageMemoryRequirements(image);

    imageMemory = device.allocateMemory(
        vk::MemoryAllocateInfo(
            memoryRequirements.size, findMemoryType(memoryRequirements.memoryTypeBits, properties)
        )
    );

    device.bindImageMemory(image, imageMemory, 0);
}

void VulkanSDL2App::transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout, uint32_t mipLevels) {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;

    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;

    barrier.image = image;

    barrier.subresourceRange = vk::ImageSubresourceRange(
        vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1
    );

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = vk::AccessFlags(0);
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw std::runtime_error("Unsupported layout transition!");
    }

    commandBuffer.pipelineBarrier(sourceStage, destinationStage, vk::DependencyFlags(0),
        0, nullptr, 0, nullptr,
        1, &barrier
    );

    endSingleTimeCommands(commandBuffer);
}

void VulkanSDL2App::copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height) {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

    vk::BufferImageCopy region = {
        0, 0, 0,
        vk::ImageSubresourceLayers(
            vk::ImageAspectFlagBits::eColor, 0, 0, 1
        ),
        vk::Offset3D(0, 0, 0),
        vk::Extent3D(width, height, 1)
    };

    commandBuffer.copyBufferToImage(
        buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region
    );

    endSingleTimeCommands(commandBuffer);
}

vk::CommandBuffer VulkanSDL2App::beginSingleTimeCommands() {
    auto commandBuffers = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1)
    );

    commandBuffers[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    return commandBuffers[0];
}

void VulkanSDL2App::endSingleTimeCommands(vk::CommandBuffer commandBuffer) {
    commandBuffer.end();
    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    auto res = graphicsQueue.submit(1, &submitInfo, nullptr);
    if (res != vk::Result::eSuccess) {
        std::printf("endSingleTimeCommands error\n");
    }
    graphicsQueue.waitIdle();

    device.freeCommandBuffers(commandPool, commandBuffer);
}

uint32_t VulkanSDL2App::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

