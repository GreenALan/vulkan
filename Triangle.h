#include <iostream>
#include <vulkan/vulkan.h>
#include "base/VulkanBase.h"
#include <glm/glm.hpp>
#include <glm/gtc/matric_transform.hpp>

#define ENABLE_VALIDATION false 

#define MAX_CONCURRENT_FRAMES 2

class VulkanExample : public VulkanBase
{
public:

    struct Vertex {
		float position[3];
		float color[3];
	};

    // 匿名结构体，并且声明 vertices 为一个结构体变量
    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
    } vertices;

    // Index buffer
    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
        uint32_t count;
    } indices;

    // Uniform buffer block object
	struct UniformBuffer {
		VkDeviceMemory memory;
		VkBuffer buffer;
		// The descriptor set stores the resources bound to the binding points in a shader
		// It connects the binding points of the different shaders with the buffers and images used for those bindings
		VkDescriptorSet descriptorSet;
		// We keep a pointer to the mapped buffer, so we can easily update it's contents via a memcpy
		uint8_t* mapped{ nullptr };
	};

    std::array<UniformBuffer, MAX_CONCURRENT_FRAMES> UniformBuffers;

    struct ShaderData {
		glm::mat4 projectionMatrix;
		glm::mat4 modelMatrix;
		glm::mat4 viewMatrix;
	};

    VkPipelineLayout pipelineLayout;

    VkPipeline pipeline;

    VkDescriptorSetLayout descriptorSetLayout;
    
    std::array<VkSemaphore, MAX_CONCURRENT_FRAMES> presentCompleteSemaphores;
    std::array<VkSemaphore, MAX_CONCURRENT_FRAMES> renderCompleteSemaphores;

    VkCommandPool commandPool;
    std::array<VkCommandBuffer, MAX_CONCURRENT_FRAMES> commandBuffers;
    std::array<VkCommandBuffer, MAX_CONCURRENT_FRAMES> waitFences;

    uint32_t currentFrame = 0;

    VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
    {
        title = "Triangle";
        settings.overlay = false;
        camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
		camera.setRotation(glm::vec3(0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);
    }

    ~VulkanExample()
    {
		vkDestroyPipeline(device, pipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyBuffer(device, vertices.buffer, nullptr);
		vkFreeMemory(device, vertices.memory, nullptr);

        vkDestroyBuffer(device, indices.buffer, nullptr);
		vkFreeMemory(device, indices.memory, nullptr);

        for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++)
        {
            vkDestoryFence(device, waitFences[i], nullptr);
            vkDestorSemaphore(device, presentCompleteSemaphores[i], nullptr);
            vkDestorSemaphore(device, renderCompleteSemaphores[i], nullptr);
            vkDestroyBuffer(device, UniformBuffers[i].buffer, nullptr);
            vkFreeMemory(device, UniformBuffers[i].memory, nullptr);
        }

    }

	void createSynchronizationPrimitives()
    {
        for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
			
			// Semaphores (Used for correct command ordering)
			VkSemaphoreCreateInfo semaphoreCI{};
			semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			// Semaphore used to ensure that image presentation is complete before starting to submit again
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentCompleteSemaphores[i]));
			// Semaphore used to ensure that all commands submitted have been finished before submitting the image to the queue
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &renderCompleteSemaphores[i]));

			// Fences (Used to check draw command buffer completion)
			VkFenceCreateInfo fenceCI{};
			fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			// Create in signaled state so we don't wait on first render of each command buffer
			fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &waitFences[i]));

		}
    }

    void createCommandBuffers()
	{
		// All command buffers are allocated from a command pool
		VkCommandPoolCreateInfo commandPoolCI{};
		commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		commandPoolCI.queueFamilyIndex = swapChain.queueNodeIndex;
		commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));

		// Allocate one command buffer per max. concurrent frame from above pool
		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, MAX_CONCURRENT_FRAMES);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, commandBuffers.data()));
	}

    void createVertexBuffer()
    {
        std::vector<Vertex> vertexBuffer {
            { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ {  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
        }

        uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size()) * sizeof(Vertex);

        std::vector<uint32_t> indexBuffer{ 0, 1, 2 };
		indices.count = static_cast<uint32_t>(indexBuffer.size());
		uint32_t indexBufferSize = indices.count * sizeof(uint32_t);

        VkMemoryAllcoateInfo memAlloc {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        struct StagingBuffer {
            VkDeviceMemory memory;
            VkBuffer buffer;
        }

        struct {
            StagingBuffer vertices;
            StagingBuffer indices;
        } StagingBuffers;

        void* data;

        VkBufferCreateInfo vertexBufferInfoCI {};
        vertexBufferInfoCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfoCI.sType = vertexBufferSize;
        vertexBufferInfoCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; 
        VK_CHECK_RESULT(vkCreateBuffer(device, &vertexBufferInfoCI, nullptr, &stagingBuffers.vertices.buffer));
        vkGetBufferMemoryRequirements(device, stagingBuffers.vertices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.vertices.memory));

        VK_CHECK_RESULT(vkMapMemory(device, stagingBuffers.vertices.memory, 0, memAlloc.allocationSize, 0, &data));
		memcpy(data, vertexBuffer.data(), vertexBufferSize);
		vkUnmapMemory(device, stagingBuffers.vertices.memory);
    }

    void prepare()
    {
        VulkanBase::prepare();
        createSynchronizationPrimitives();
    }


}