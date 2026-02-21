#include <SDL2/SDL.h>
#include <vulkan/vulkan.hpp>
#include <SDL2/SDL_vulkan.h>
#include <string>
#include <optional>
#include <glm/glm.hpp>
#include "FFmpegDecoder.h"
#include "SDLAudioPlayer.h"

struct Config {

    bool DiscreteGpuFirst = false;

    bool autoReplay = false;
};

class VulkanSDL2App {
public:
    VulkanSDL2App(std::string title, int width, int height, Config config);
    ~VulkanSDL2App();

    void run();

    Config config;

private:
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsAndComputeFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsAndComputeFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
        vk::SurfaceCapabilitiesKHR capabilities;
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> presentModes;
    };

    struct Vertex {
        glm::vec2 pos;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription() {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;

            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions() {
            std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions{};
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = vk::Format::eR32G32Sfloat;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = vk::Format::eR32G32Sfloat;
            attributeDescriptions[1].offset = offsetof(Vertex, texCoord);

            return attributeDescriptions;
        }
    };

    // data
    std::string title;

    // about window size
    std::mutex windowResizeMutex;
    int windowWidth, windowHeight;
    int mediaWidth, mediaHeight;
    bool isFullscreen = false;
    std::atomic<bool> frameBufferResized = false;

    std::atomic<bool> running = true;

    SDL_Window* window;

    FFmpegDecoder* ffmpegDecoder;
    SDLAudioPlayer* audioPlayer;

    // data about vulkan
    std::atomic<bool> drawThreadExited = false;
    std::atomic<bool> drawThreadRunning = false;
    std::thread drawThread;
    void draw();

    vk::Instance instance;
    vk::DebugUtilsMessengerEXT debugMessenger;
    vk::SurfaceKHR surface;

    vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::string physicalDeviceName;
    vk::Device device;

    vk::Queue graphicsQueue;
    vk::Queue computeQueue;
    vk::Queue presentQueue;

    vk::SwapchainKHR swapChain;
    std::vector<vk::Image> swapChainImages;
    vk::Format swapChainImageFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::ImageView> swapChainImageViews;
    std::vector<vk::Framebuffer> swapChainFramebuffers;

    vk::RenderPass renderPass;
    vk::PipelineLayout graphicsPipelineLayout;
    vk::Pipeline graphicsPipeline;

    vk::DescriptorSetLayout graphicsDescriptorSetLayout;
    vk::DescriptorPool graphicsDescriptorPool;
    std::vector<vk::DescriptorSet> graphicsDescriptorSets;

    vk::CommandPool commandPool;
    std::vector<vk::CommandBuffer> commandBuffers;

    vk::Buffer vertexBuffer;
    vk::DeviceMemory vertexBufferMemory;

    struct Texture {
        explicit Texture(const vk::Device device) : device(device) {}

        ~Texture() {
            destroy();
        }

        vk::Device device;

        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView imageView;
        vk::Sampler sampler;

        bool useful = false;

        void destroy() {
            if (useful) {
                device.destroySampler(sampler);
                device.destroyImageView(imageView);
                device.destroyImage(image);
                device.freeMemory(memory);
                useful = false;
            }
        }
    };

    std::vector<Texture> textures;

    uint32_t currentFrame = 0;
    int MAX_FRAMES_IN_FLIGHT;
    std::vector<vk::Semaphore> imageAvailableSemaphores;
    std::vector<vk::Semaphore> renderFinishedSemaphores;
    std::vector<vk::Fence> inFlightFences;


    // functions
    void initWindow();

    void printAppInfos();

    void toggleFullscreen();
    void togglePause();
    void updateVolume(int sign);

    // functions about vulkan
    void destroyVulkan();

    void initVulkan();
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createFrameBuffers();
    void createGraphicsDescriptorSetLayout();
    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffers();
    void createVertexBuffer();
    void createDescriptorPool();
    void createDescriptorSets();
    void initTextureResource();
    void createSyncObjects();

    void DrawFrame(std::shared_ptr<FFmpegDecoder::Frame> frame);

    void cleanupSwapChain();
    void reCreateSwapChain();

    void updateTexture(uint32_t imageIndex, std::shared_ptr<FFmpegDecoder::Frame> frame);
    void recordCommandBuffer(vk::CommandBuffer commandBuffer, uint32_t imageIndex);

    // helper functions
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(vk::PhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device);

    SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice device);
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

    vk::ShaderModule createShaderModule(const unsigned char* code, unsigned int size);

    void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory);

    void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);

    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
                     vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                     vk::MemoryPropertyFlags properties, vk::Image &image, vk::DeviceMemory &imageMemory);

    void transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                               uint32_t mipLevels);

    void copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height);

    vk::CommandBuffer beginSingleTimeCommands();

    void endSingleTimeCommands(vk::CommandBuffer commandBuffer);

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
};
