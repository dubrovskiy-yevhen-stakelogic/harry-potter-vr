#include "quest_planar_mirror.h"
#include <algorithm>

namespace hpvr::quest {
bool PlanarMirrorTarget::Create(VkPhysicalDevice physical,VkDevice device,unsigned width,unsigned height,VkFormat color,VkFormat depth,unsigned layers){
    Destroy();device_=device;
    layers=std::max(1U,layers);framebuffers_.resize(layers);layer_views_.resize(layers);
    const float scale=std::min(.5F,1024.0F/float(std::max(width,height)));
    width_=std::max(1U,unsigned(float(width)*scale));height_=std::max(1U,unsigned(float(height)*scale));
    const auto make=[&](){
        VkPhysicalDeviceMemoryProperties types{};vkGetPhysicalDeviceMemoryProperties(physical,&types);
        for(unsigned i=0;i<2;++i){
            VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image.imageType=VK_IMAGE_TYPE_2D;image.format=i?depth:color;image.extent={width_,height_,1};
            image.mipLevels=1;image.arrayLayers=i?1:layers;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
            image.usage=i?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            if(vkCreateImage(device,&image,nullptr,&images_[i])!=VK_SUCCESS)return false;
            VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,images_[i],&req);
            unsigned type=types.memoryTypeCount;
            for(unsigned j=0;j<types.memoryTypeCount;++j)if((req.memoryTypeBits&(1U<<j))&&(types.memoryTypes[j].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=j;break;}
            if(type==types.memoryTypeCount)return false;
            VkMemoryAllocateInfo alloc{};alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
            if(vkAllocateMemory(device,&alloc,nullptr,&memory_[i])!=VK_SUCCESS||vkBindImageMemory(device,images_[i],memory_[i],0)!=VK_SUCCESS)return false;
            VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=images_[i];view.viewType=i?VK_IMAGE_VIEW_TYPE_2D:VK_IMAGE_VIEW_TYPE_2D_ARRAY;view.format=image.format;
            view.subresourceRange={i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,i?1:layers};
            if(vkCreateImageView(device,&view,nullptr,&views_[i])!=VK_SUCCESS)return false;
            if(!i)for(unsigned layer=0;layer<layers;++layer){
                view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.subresourceRange.baseArrayLayer=layer;view.subresourceRange.layerCount=1;
                if(vkCreateImageView(device,&view,nullptr,&layer_views_[layer])!=VK_SUCCESS)return false;
            }
        }
        std::array<VkAttachmentDescription,2> attachments{};
        for(unsigned i=0;i<2;++i){
            auto& a=attachments[i];a.format=i?depth:color;a.samples=VK_SAMPLE_COUNT_1_BIT;
            a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=i?VK_ATTACHMENT_STORE_OP_DONT_CARE:VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
            a.finalLayout=i?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkAttachmentReference c{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},d{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&c;sub.pDepthStencilAttachment=&d;
        std::array<VkSubpassDependency,2> dependencies{};
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;
        dependencies[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependencies[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=2;pass.pAttachments=attachments.data();pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=2;pass.pDependencies=dependencies.data();
        if(vkCreateRenderPass(device,&pass,nullptr,&pass_)!=VK_SUCCESS)return false;
        for(unsigned layer=0;layer<layers;++layer){
            const std::array attachments{layer_views_[layer],views_[1]};
            VkFramebufferCreateInfo frame{};frame.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;frame.renderPass=pass_;frame.attachmentCount=2;frame.pAttachments=attachments.data();frame.width=width_;frame.height=height_;frame.layers=1;
            if(vkCreateFramebuffer(device,&frame,nullptr,&framebuffers_[layer])!=VK_SUCCESS)return false;
        }
        VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        return vkCreateSampler(device,&sampler,nullptr,&sampler_)==VK_SUCCESS;
    };
    if(!make()){Destroy();return false;}return true;
}
void PlanarMirrorTarget::Bind(VkDescriptorSet set) const {
    VkDescriptorImageInfo image{sampler_,views_[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.dstBinding=6;
    write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image;
    vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
}
void PlanarMirrorTarget::Begin(VkCommandBuffer command,unsigned layer) const {
    std::array<VkClearValue,2> clear{};clear[0].color={{.02F,.02F,.025F,1}};clear[1].depthStencil={1,0};
    VkRenderPassBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=pass_;begin.framebuffer=framebuffers_.at(layer);
    begin.renderArea.extent={width_,height_};begin.clearValueCount=2;begin.pClearValues=clear.data();
    vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
}
void PlanarMirrorTarget::Destroy(){
    if(!device_)return;
    for(auto frame:framebuffers_)if(frame)vkDestroyFramebuffer(device_,frame,nullptr);
    for(auto view:layer_views_)if(view)vkDestroyImageView(device_,view,nullptr);
    if(pass_)vkDestroyRenderPass(device_,pass_,nullptr);
    if(sampler_)vkDestroySampler(device_,sampler_,nullptr);
    for(unsigned i=0;i<2;++i){if(views_[i])vkDestroyImageView(device_,views_[i],nullptr);if(images_[i])vkDestroyImage(device_,images_[i],nullptr);if(memory_[i])vkFreeMemory(device_,memory_[i],nullptr);}
    device_=VK_NULL_HANDLE;framebuffers_.clear();layer_views_.clear();pass_=VK_NULL_HANDLE;sampler_=VK_NULL_HANDLE;views_={};images_={};memory_={};
}
}
