#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <array>
#include <vulkan/vulkan.h>
#include "base/VulkanBase.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

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

    std::array<UniformBuffer, MAX_CONCURRENT_FRAMES> uniformBuffers;

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
    std::array<VkFence, MAX_CONCURRENT_FRAMES> waitFences;

    uint32_t currentFrame = 0;

    VulkanExample() : VulkanBase(ENABLE_VALIDATION)
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
            vkDestroyFence(device, waitFences[i], nullptr);
            vkDestroySemaphore(device, presentCompleteSemaphores[i], nullptr);
            vkDestroySemaphore(device, renderCompleteSemaphores[i], nullptr);
            vkDestroyBuffer(device, uniformBuffers[i].buffer, nullptr);
            vkFreeMemory(device, uniformBuffers[i].memory, nullptr);
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

    uint32_t  getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties)
    {
        // Iterate over all memory types available for the device used in this example
        for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++)
        {
            if ((typeBits & 1) == 1)
            {
                if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                {
                    return i;
                }
            }
            typeBits >>= 1;
        }

        throw "Could not find a suitable memory type!";
    }

    void createVertexBuffer()
    {
        std::vector<Vertex> vertexBuffer{
            { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
            { { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
            { {  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
        };

        uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size() * sizeof(Vertex));

        std::vector<uint32_t> indexBuffer{ 0, 1, 2 };
		indices.count = static_cast<uint32_t>(indexBuffer.size());
		uint32_t indexBufferSize = indices.count * sizeof(uint32_t);

        VkMemoryAllocateInfo memAlloc {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        struct StagingBuffer {
            VkDeviceMemory memory;
            VkBuffer buffer;
        };

        struct {
            StagingBuffer vertices;
            StagingBuffer indices;
        } stagingBuffers;

        void* data;

        VkBufferCreateInfo vertexBufferInfoCI {};
        vertexBufferInfoCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfoCI.size = vertexBufferSize;
        vertexBufferInfoCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; 
        VK_CHECK_RESULT(vkCreateBuffer(device, &vertexBufferInfoCI, nullptr, &stagingBuffers.vertices.buffer));
        vkGetBufferMemoryRequirements(device, stagingBuffers.vertices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.vertices.memory));
        VK_CHECK_RESULT(vkMapMemory(device, stagingBuffers.vertices.memory, 0, memAlloc.allocationSize, 0, &data));
		memcpy(data, vertexBuffer.data(), vertexBufferSize);
		vkUnmapMemory(device, stagingBuffers.vertices.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0));

        vertexBufferInfoCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VK_CHECK_RESULT(vkCreateBuffer(device, &vertexBufferInfoCI, nullptr, &vertices.buffer));
        vkGetBufferMemoryRequirements(device, vertices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &vertices.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, vertices.buffer, vertices.memory, 0));

        // Index buffer
		VkBufferCreateInfo indexbufferCI{};
		indexbufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		indexbufferCI.size = indexBufferSize;
		indexbufferCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		// Copy index data to a buffer visible to the host (staging buffer)
		VK_CHECK_RESULT(vkCreateBuffer(device, &indexbufferCI, nullptr, &stagingBuffers.indices.buffer));
		vkGetBufferMemoryRequirements(device, stagingBuffers.indices.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.indices.memory));
		VK_CHECK_RESULT(vkMapMemory(device, stagingBuffers.indices.memory, 0, indexBufferSize, 0, &data));
		memcpy(data, indexBuffer.data(), indexBufferSize);
		vkUnmapMemory(device, stagingBuffers.indices.memory);
		VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffers.indices.buffer, stagingBuffers.indices.memory, 0));

        	// Create destination buffer with device only visibility
		indexbufferCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VK_CHECK_RESULT(vkCreateBuffer(device, &indexbufferCI, nullptr, &indices.buffer));
		vkGetBufferMemoryRequirements(device, indices.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &indices.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, indices.buffer, indices.memory, 0));

        VkCommandBuffer copyCmd;

        VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = commandPool;
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = 1;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &copyCmd));

        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
        VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

        VkBufferCopy copyRegion{};

        copyRegion.size = vertexBufferSize;
        vkCmdCopyBuffer(copyCmd, stagingBuffers.vertices.buffer, vertices.buffer, 1, &copyRegion);

        copyRegion.size = indexBufferSize;
        vkCmdCopyBuffer(copyCmd, stagingBuffers.indices.buffer, indices.buffer, 1, &copyRegion);
        VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCmd;

        VkFenceCreateInfo fenceCI{};
        fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCI.flags = 0;
        VkFence fence;
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &fence));
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
        VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &copyCmd);

        vkDestroyBuffer(device, stagingBuffers.vertices.buffer, nullptr);
        vkFreeMemory(device, stagingBuffers.vertices.memory, nullptr);
        vkDestroyBuffer(device, stagingBuffers.indices.buffer, nullptr);
        vkFreeMemory(device, stagingBuffers.indices.memory,  nullptr);
        
    }

    void createUniformBuffers()
    {
        VkMemoryRequirements memReqs;

        VkBufferCreateInfo bufferInfo{};
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.allocationSize = 0;
        allocInfo.memoryTypeIndex = 0;

        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(ShaderData);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
            VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBuffers[i].buffer));
            vkGetBufferMemoryRequirements(device, uniformBuffers[i].buffer, &memReqs);
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &(uniformBuffers[i].memory)));
            VK_CHECK_RESULT(vkBindBufferMemory(device, uniformBuffers[i].buffer, uniformBuffers[i].memory, 0));
            VK_CHECK_RESULT(vkMapMemory(device, uniformBuffers[i].memory, 0 , sizeof(ShaderData), 0, (void**)&uniformBuffers[i].mapped));

        }
    }

    void createDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        layoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
        descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorLayoutCI.pNext = nullptr;
        descriptorLayoutCI.bindingCount = 1;
        descriptorLayoutCI.pBindings = &layoutBinding;
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayout));

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.pNext = nullptr;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
        
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
    }

    void createDescriptorPool()
    {
        VkDescriptorPoolSize descriptorTypeCounts[1];
        descriptorTypeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorTypeCounts[0].descriptorCount = MAX_CONCURRENT_FRAMES;

		VkDescriptorPoolCreateInfo descriptorPoolCI{};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.pNext = nullptr;
        descriptorPoolCI.poolSizeCount = 1;
        descriptorPoolCI.pPoolSizes = descriptorTypeCounts;

        descriptorPoolCI.maxSets = MAX_CONCURRENT_FRAMES;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

    }

    void createDescriptorSets()
    {
        for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++)
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &uniformBuffers[i].descriptorSet));

            VkWriteDescriptorSet writeDescriptorSet{};

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers[i].buffer;
            bufferInfo.range = sizeof(ShaderData);

            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.dstSet = uniformBuffers[i].descriptorSet;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSet.pBufferInfo = &bufferInfo;
            writeDescriptorSet.dstBinding = 0;
            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

        }
    }

    VkShaderModule loadSPIRVShader(std::string filename)
	{
		size_t shaderSize;
		char* shaderCode{ nullptr };

#if defined(__ANDROID__)
		// Load shader from compressed asset
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		shaderSize = AAsset_getLength(asset);
		assert(shaderSize > 0);

		shaderCode = new char[shaderSize];
		AAsset_read(asset, shaderCode, shaderSize);
		AAsset_close(asset);
#else
		std::ifstream is(filename, std::ios::binary | std::ios::in | std::ios::ate);

		if (is.is_open())
		{
			shaderSize = is.tellg();
			is.seekg(0, std::ios::beg);
			// Copy file contents into a buffer
			shaderCode = new char[shaderSize];
			is.read(shaderCode, shaderSize);
			is.close();
			assert(shaderSize > 0);
		}
#endif
		if (shaderCode)
		{
			// Create a new shader module that will be used for pipeline creation
			VkShaderModuleCreateInfo shaderModuleCI{};
			shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			shaderModuleCI.codeSize = shaderSize;
			shaderModuleCI.pCode = (uint32_t*)shaderCode;

			VkShaderModule shaderModule;
			VK_CHECK_RESULT(vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule));

			delete[] shaderCode;

			return shaderModule;
		}
		else
		{
			std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
			return VK_NULL_HANDLE;
		}
	}

    void createPipelines() 
    {
        VkGraphicsPipelineCreateInfo pipelineCI{};
        pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.layout = pipelineLayout;
        pipelineCI.renderPass = renderPass;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
        inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
        rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_LINE;
        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationStateCI.depthClampEnable = VK_FALSE;
        rasterizationStateCI.rasterizerDiscardEnable = VK_FALSE;
        rasterizationStateCI.depthBiasEnable = VK_FALSE;
        rasterizationStateCI.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.colorWriteMask = 0Xf;
        blendAttachmentState.blendEnable = VK_FALSE;
        
        VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
        colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendStateCI.attachmentCount = 1;
        colorBlendStateCI.pAttachments = &blendAttachmentState;

        VkPipelineViewportStateCreateInfo viewportStateCI{};
        viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCI.viewportCount = 1;
        viewportStateCI.scissorCount = 1;

        std::vector<VkDynamicState> dynamicStateEnable;
        dynamicStateEnable.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        dynamicStateEnable.push_back(VK_DYNAMIC_STATE_SCISSOR);
        VkPipelineDynamicStateCreateInfo dynamicStateCI{};
        dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCI.pDynamicStates = dynamicStateEnable.data();
        dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnable.size());

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
        depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilStateCI.depthTestEnable = VK_TRUE;
        depthStencilStateCI.depthWriteEnable = VK_TRUE;
        depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilStateCI.depthBoundsTestEnable = VK_FALSE;
        depthStencilStateCI.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilStateCI.back.passOp = VK_STENCIL_OP_KEEP;
        depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilStateCI.stencilTestEnable = VK_FALSE;
        depthStencilStateCI.front = depthStencilStateCI.back;

        VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
        multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampleStateCI.pSampleMask = nullptr;

        VkVertexInputBindingDescription vertexInputBinding{};
        vertexInputBinding.binding = 0;
        vertexInputBinding.stride = sizeof(Vertex);
        vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> vertexInputAttributs;
        
        vertexInputAttributs[0].binding = 0;
        vertexInputAttributs[0].location = 0;
        vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexInputAttributs[0].offset = offsetof(Vertex, position);

        vertexInputAttributs[1].binding = 0;
        vertexInputAttributs[1].location = 1;
        vertexInputAttributs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexInputAttributs[1].offset = offsetof(Vertex, color);

        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
        vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputStateCI.vertexBindingDescriptionCount = 1;
        vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputStateCI.vertexAttributeDescriptionCount = 2;
        vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributs.data();

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = loadSPIRVShader(getShadersPath() + "triangle/triangle.vert.spv");
        shaderStages[0].pName = "main";
        assert(shaderStages[0].module != VK_NULL_HANDLE);

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = loadSPIRVShader(getShadersPath() + "triangle/triangle.frag.spv");
        shaderStages[1].pName = "main";
        assert(shaderStages[1].module != VK_NULL_HANDLE);

        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();
        
        pipelineCI.pVertexInputState = &vertexInputStateCI;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

        vkDestroyShaderModule(device, shaderStages[0].module, nullptr); 
        vkDestroyShaderModule(device, shaderStages[1].module, nullptr); 


    }
    void setupFrameBuffer()
    {
        // Create a frame buffer for every image in the swapchain
        frameBuffers.resize(swapChain.imageCount);
        for (size_t i = 0; i < frameBuffers.size(); i++)
        {
            std::array<VkImageView, 2> attachments;
            // Color attachment is the view of the swapchain image
            attachments[0] = swapChain.buffers[i].view;
            // Depth/Stencil attachment is the same for all frame buffers due to how depth works with current GPUs
            attachments[1] = depthStencil.view;

            VkFramebufferCreateInfo frameBufferCI{};
            frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            // All frame buffers use the same renderpass setup
            frameBufferCI.renderPass = renderPass;
            frameBufferCI.attachmentCount = static_cast<uint32_t>(attachments.size());
            frameBufferCI.pAttachments = attachments.data();
            frameBufferCI.width = width;
            frameBufferCI.height = height;
            frameBufferCI.layers = 1;
            // Create the framebuffer
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCI, nullptr, &frameBuffers[i]));
        }
    }

    void setupRenderPass()
    {
        // This example will use a single render pass with one subpass

        // Descriptors for the attachments used by this renderpass
        std::array<VkAttachmentDescription, 2> attachments{};

        // Color attachment
        attachments[0].format = swapChain.colorFormat;                                  // Use the color format selected by the swapchain
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;                                 // We don't use multi sampling in this example
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;                            // Clear this attachment at the start of the render pass
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;                          // Keep its contents after the render pass is finished (for displaying it)
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;                 // We don't use stencil, so don't care for load
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;               // Same for store
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;                       // Layout at render pass start. Initial doesn't matter, so we use undefined
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;                   // Layout to which the attachment is transitioned when the render pass is finished
                                                                                        // As we want to present the color buffer to the swapchain, we transition to PRESENT_KHR
        // Depth attachment
        attachments[1].format = depthFormat;                                           // A proper depth format is selected in the example base
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;                           // Clear depth at start of first subpass
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;                     // We don't need depth after render pass has finished (DONT_CARE may result in better performance)
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;                // No stencil
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;              // No Stencil
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;                      // Layout at render pass start. Initial doesn't matter, so we use undefined
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Transition to depth/stencil attachment

        // Setup attachment references
        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;                                    // Attachment 0 is color
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Attachment layout used as color during the subpass

        VkAttachmentReference depthReference{};
        depthReference.attachment = 1;                                            // Attachment 1 is color
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Attachment used as depth/stencil used during the subpass

        // Setup a single subpass reference
        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;                            // Subpass uses one color attachment
        subpassDescription.pColorAttachments = &colorReference;                 // Reference to the color attachment in slot 0
        subpassDescription.pDepthStencilAttachment = &depthReference;           // Reference to the depth attachment in slot 1
        subpassDescription.inputAttachmentCount = 0;                            // Input attachments can be used to sample from contents of a previous subpass
        subpassDescription.pInputAttachments = nullptr;                         // (Input attachments not used by this example)
        subpassDescription.preserveAttachmentCount = 0;                         // Preserved attachments can be used to loop (and preserve) attachments through subpasses
        subpassDescription.pPreserveAttachments = nullptr;                      // (Preserve attachments not used by this example)
        subpassDescription.pResolveAttachments = nullptr;                       // Resolve attachments are resolved at the end of a sub pass and can be used for e.g. multi sampling

        // Setup subpass dependencies
        // These will add the implicit attachment layout transitions specified by the attachment descriptions
        // The actual usage layout is preserved through the layout specified in the attachment reference
        // Each subpass dependency will introduce a memory and execution dependency between the source and dest subpass described by
        // srcStageMask, dstStageMask, srcAccessMask, dstAccessMask (and dependencyFlags is set)
        // Note: VK_SUBPASS_EXTERNAL is a special constant that refers to all commands executed outside of the actual renderpass)
        std::array<VkSubpassDependency, 2> dependencies;

        // Does the transition from final to initial layout for the depth an color attachments
        // Depth attachment
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies[0].dependencyFlags = 0;
        // Color attachment
        dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].dstSubpass = 0;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = 0;
        dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dependencies[1].dependencyFlags = 0;

        // Create the actual renderpass
        VkRenderPassCreateInfo renderPassCI{};
        renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());  // Number of attachments used by this render pass
        renderPassCI.pAttachments = attachments.data();                            // Descriptions of the attachments used by the render pass
        renderPassCI.subpassCount = 1;                                             // We only use one subpass in this example
        renderPassCI.pSubpasses = &subpassDescription;                             // Description of that subpass
        renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size()); // Number of subpass dependencies
        renderPassCI.pDependencies = dependencies.data();                          // Subpass dependencies used by the render pass
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderPass));
    }
   
    void prepare()
    {
        VulkanBase::prepare();
        createSynchronizationPrimitives();
        createCommandBuffers();
        createVertexBuffer();
    	createUniformBuffers();
        createDescriptorSetLayout();
        createDescriptorPool();
		createDescriptorSets();
        createPipelines();
        prepared = true;

    }

    virtual void render()
    {
        if (!prepared)
        {
            return;
        }
        vkWaitForFences(device, 1, &waitFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain.swapChain, UINT64_MAX, presentCompleteSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            /* windowResize();*/
            return;
        }
        else if (result != VK_SUCCESS && (result != VK_SUBOPTIMAL_KHR)) {
            throw "Could not acquire the next swap chian image";
        }

        ShaderData shaderData{};
        shaderData.projectionMatrix = camera.matrices.perspective;
        shaderData.viewMatrix = camera.matrices.view;
        shaderData.modelMatrix = glm::mat4(1.0f);

        memcpy(uniformBuffers[currentBuffer].mapped, &shaderData, sizeof(shaderData));
        VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[currentFrame]));

        vkResetCommandBuffer(commandBuffers[currentBuffer], 0);

        VkCommandBufferBeginInfo cmdBufInfo{};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        VkClearValue clearValues[2];
        clearValues[0].color = { {0.0f, 0.0f, 0.2f, 1.0f} };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext = nullptr;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;
        renderPassBeginInfo.framebuffer = frameBuffers[imageIndex];
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers[currentBuffer], &cmdBufInfo));

        vkCmdBeginRenderPass(commandBuffers[currentBuffer], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.height = (float)height;
        viewport.width = (float)width;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffers[currentBuffer], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent.width = width;
        scissor.extent.height = height;
        scissor.offset.x = 0;
        scissor.offset.y = 0;

        vkCmdSetScissor(commandBuffers[currentBuffer], 0, 1, &scissor);
        vkCmdBindDescriptorSets(commandBuffers[currentBuffer], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &uniformBuffers[currentBuffer].descriptorSet, 0, nullptr);
        vkCmdBindPipeline(commandBuffers[currentBuffer], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize offsets[1]{ 0 };
        vkCmdBindVertexBuffers(commandBuffers[currentBuffer], 0, 1, &vertices.buffer, offsets);
        vkCmdBindIndexBuffer(commandBuffers[currentBuffer], indices.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(commandBuffers[currentBuffer], indices.count, 1, 0, 0, 1);
        vkCmdEndRenderPass(commandBuffers[currentBuffer]);

        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers[currentBuffer]));

        VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pWaitDstStageMask = &waitStageMask;               // Pointer to the list of pipeline stages that the semaphore waits will occur at
        submitInfo.waitSemaphoreCount = 1;                           // One wait semaphore
        submitInfo.signalSemaphoreCount = 1;                         // One signal semaphore
        submitInfo.pCommandBuffers = &commandBuffers[currentBuffer]; // Command buffers(s) to execute in this batch (submission)
        submitInfo.commandBufferCount = 1;

        submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
        submitInfo.pSignalSemaphores = &renderCompleteSemaphores[currentFrame];

        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[currentFrame]));

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderCompleteSemaphores[currentFrame];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain.swapChain;
        presentInfo.pImageIndices = &imageIndex;
        result = vkQueuePresentKHR(queue, &presentInfo);

        if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
            //windowResize();
        }
        else if (result != VK_SUCCESS) {
            throw "Could not present the image to the swap chain!";
        }

    }

};