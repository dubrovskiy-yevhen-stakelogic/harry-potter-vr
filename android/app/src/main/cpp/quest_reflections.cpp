#include "quest_reflections.h"
#include <cstring>
#include <android/log.h>
namespace hpvr::quest {
namespace {
bool MemoryType(VkPhysicalDevice physical,unsigned bits,VkMemoryPropertyFlags flags,unsigned& type){
    VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(physical,&p);
    for(unsigned i=0;i<p.memoryTypeCount;++i)if((bits&(1U<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags){type=i;return true;}
    return false;
}
VkImageMemoryBarrier Barrier(VkImage image,VkImageAspectFlags aspect,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst){
    VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.image=image;
    b.subresourceRange={aspect,0,1,0,1};b.oldLayout=from;b.newLayout=to;b.srcAccessMask=src;b.dstAccessMask=dst;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;return b;
}
}
bool ReflectionHistory::Create(VkPhysicalDevice physical,VkDevice device,VkQueue queue,unsigned family,
                               unsigned width,unsigned height,VkFormat color,VkFormat depth){
    Destroy();physical_=physical;device_=device;
    // Choose independently: Adreno 740 supports color blits but no depth blit
    // destination. Keep full-resolution depth and halve color on this device.
    const auto divisor=[&](VkFormat format){
        VkFormatProperties props{};vkGetPhysicalDeviceFormatProperties(physical,format,&props);
        const auto bits=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
        return (props.optimalTilingFeatures&bits)==bits?2U:1U;
    };
    divisor_=divisor(color);depth_divisor_=divisor(depth);
    width_=ReflectionExtent(width,divisor_);height_=ReflectionExtent(height,divisor_);
    depth_width_=ReflectionExtent(width,depth_divisor_);depth_height_=ReflectionExtent(height,depth_divisor_);
    const auto make=[&](){
        for(unsigned i=0;i<2;++i){
            VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.imageType=VK_IMAGE_TYPE_2D;info.format=i?depth:color;
            info.extent={i?depth_width_:width_,i?depth_height_:height_,1};
            info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
            info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            if(vkCreateImage(device,&info,nullptr,&images_[i])!=VK_SUCCESS)return false;
            VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,images_[i],&req);unsigned type=0;
            if(!MemoryType(physical,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,type))return false;
            VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=req.size;a.memoryTypeIndex=type;
            if(vkAllocateMemory(device,&a,nullptr,&memories_[i])!=VK_SUCCESS||vkBindImageMemory(device,images_[i],memories_[i],0)!=VK_SUCCESS)return false;
            VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.image=images_[i];v.viewType=VK_IMAGE_VIEW_TYPE_2D;
            v.format=info.format;v.subresourceRange={i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            if(vkCreateImageView(device,&v,nullptr,&views_[i])!=VK_SUCCESS)return false;
        }
        VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if(vkCreateSampler(device,&sampler,nullptr,&sampler_)!=VK_SUCCESS)return false;
        VkBufferCreateInfo b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;b.size=sizeof(Uniform);b.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if(vkCreateBuffer(device,&b,nullptr,&buffer_)!=VK_SUCCESS)return false;
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer_,&req);unsigned type=0;
        if(!MemoryType(physical,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,type))return false;
        VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=req.size;a.memoryTypeIndex=type;
        if(vkAllocateMemory(device,&a,nullptr,&buffer_memory_)!=VK_SUCCESS||vkBindBufferMemory(device,buffer_,buffer_memory_,0)!=VK_SUCCESS||
           vkMapMemory(device,buffer_memory_,0,sizeof(Uniform),0,&mapped_)!=VK_SUCCESS)return false;
        data_={};std::memcpy(mapped_,&data_,sizeof(data_));return true;
    };
    if(!make()){Destroy();return false;}
    // Descriptors must name a valid layout even on the first (history-disabled) frame.
    VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo p{};p.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;p.queueFamilyIndex=family;
    bool ok=vkCreateCommandPool(device,&p,nullptr,&pool)==VK_SUCCESS;
    if(ok){VkCommandBufferAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;a.commandPool=pool;
        a.commandBufferCount=1;a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ok=vkAllocateCommandBuffers(device,&a,&command)==VK_SUCCESS;}
    if(ok){VkCommandBufferBeginInfo b{};b.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;ok=vkBeginCommandBuffer(command,&b)==VK_SUCCESS;}
    if(ok){
        const std::array barriers{Barrier(images_[0],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,VK_ACCESS_SHADER_READ_BIT),
            Barrier(images_[1],VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,VK_ACCESS_SHADER_READ_BIT)};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,2,barriers.data());
        ok=vkEndCommandBuffer(command)==VK_SUCCESS;
    }
    if(ok){VkSubmitInfo s{};s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;s.commandBufferCount=1;s.pCommandBuffers=&command;
        ok=vkQueueSubmit(queue,1,&s,VK_NULL_HANDLE)==VK_SUCCESS&&vkQueueWaitIdle(queue)==VK_SUCCESS;}
    if(pool)vkDestroyCommandPool(device,pool,nullptr);
    if(!ok){Destroy();return false;}
    __android_log_print(ANDROID_LOG_INFO,"HPVR.Quest","[hpvr.quest.ssr] status=READY history=LEFT_SHARED passes=0 max_extent=%ux%u color_divisor=%u depth_divisor=%u capture=WORLD_BEFORE_OVERLAY trace=PROJECTED_16",width_,height_,divisor_,depth_divisor_);
    return true;
}
void ReflectionHistory::Bind(VkDescriptorSet set){
    const std::array<VkDescriptorImageInfo,2> images{{{sampler_,views_[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler_,views_[1],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
    VkDescriptorBufferInfo buffer{buffer_,0,sizeof(Uniform)};std::array<VkWriteDescriptorSet,3> writes{};
    for(unsigned i=0;i<3;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=i+2;w.descriptorCount=1;
        w.descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        if(i<2)w.pImageInfo=&images[i];else w.pBufferInfo=&buffer;}
    vkUpdateDescriptorSets(device_,3,writes.data(),0,nullptr);
}
void ReflectionHistory::Prepare(bool enabled,int strength,const Matrix4& current,unsigned width,unsigned height){
    if(!mapped_)return;
    enabled_=enabled&&strength>0;
    Matrix4 inv{};bool stable=InvertReflectionMatrix(current,inv)&&std::abs(inv[11])>1e-6F;
    if(stable){float distance=0;for(int i=0;i<3;++i){const float d=inv[8+i]/inv[11]-data_.eye[i];distance+=d*d;}stable=distance<1.0F;}
    if(stable&&valid_){
        std::array<float,3> before{},after{};float dot=0,aa=0,bb=0;
        for(int i=0;i<3;++i){
            before[i]=(data_.inverse[8+i]*.5F+data_.inverse[12+i])/(data_.inverse[11]*.5F+data_.inverse[15])-data_.eye[i];
            after[i]=(inv[8+i]*.5F+inv[12+i])/(inv[11]*.5F+inv[15])-inv[8+i]/inv[11];
            dot+=before[i]*after[i];aa+=before[i]*before[i];bb+=after[i]*after[i];
        }
        stable=std::isfinite(dot)&&aa>0&&bb>0&&dot/std::sqrt(aa*bb)>.86F;
    }
    if(!enabled_||last_width_!=width||last_height_!=height||!stable)valid_=false;
    data_.params={valid_?float(strength)*.01F:0.0F,float(ReflectionExtent(last_width_,divisor_))/float(width_),float(ReflectionExtent(last_height_,divisor_))/float(height_),12.0F};
    data_.depth_uv={float(ReflectionExtent(last_width_,depth_divisor_))/float(depth_width_),float(ReflectionExtent(last_height_,depth_divisor_))/float(depth_height_),0,0};
    std::memcpy(mapped_,&data_,sizeof(data_));
}
void ReflectionHistory::Capture(VkCommandBuffer command,VkImage color,VkImage depth,const Matrix4& matrix,unsigned width,unsigned height){
    if(!enabled_)return;
    const std::array<VkImage,2> sources{color,depth};
    for(unsigned i=0;i<2;++i){
        const auto aspect=i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
        const auto layout=i?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const auto access=i?VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        std::array barriers{Barrier(sources[i],aspect,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,access,VK_ACCESS_TRANSFER_READ_BIT),
            Barrier(images_[i],aspect,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT)};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers.data());
        const auto divisor=i?depth_divisor_:divisor_;
        if(divisor==2){
            VkImageBlit blit{};blit.srcSubresource={aspect,0,0,1};blit.dstSubresource={aspect,0,0,1};
            blit.srcOffsets[1]={int(width),int(height),1};
            blit.dstOffsets[1]={int(ReflectionExtent(width,divisor)),int(ReflectionExtent(height,divisor)),1};
            vkCmdBlitImage(command,sources[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images_[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        }else{
            VkImageCopy copy{};copy.srcSubresource={aspect,0,0,1};copy.dstSubresource={aspect,0,0,1};copy.extent={width,height,1};
            vkCmdCopyImage(command,sources[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images_[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        }
        barriers={Barrier(sources[i],aspect,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layout,VK_ACCESS_TRANSFER_READ_BIT,access),
            Barrier(images_[i],aspect,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,0,0,nullptr,0,nullptr,2,barriers.data());
    }
    data_.projection=matrix;valid_=InvertReflectionMatrix(matrix,data_.inverse)&&std::abs(data_.inverse[11])>1e-6F;
    if(valid_)for(int i=0;i<3;++i)data_.eye[i]=data_.inverse[8+i]/data_.inverse[11];
    last_width_=width;last_height_=height;
    // UBO upload is deliberately next frame, after both eyes finished sampling.
}
void ReflectionHistory::Destroy(){
    if(!device_)return;
    if(mapped_)vkUnmapMemory(device_,buffer_memory_);mapped_=nullptr;
    if(buffer_)vkDestroyBuffer(device_,buffer_,nullptr);if(buffer_memory_)vkFreeMemory(device_,buffer_memory_,nullptr);
    if(sampler_)vkDestroySampler(device_,sampler_,nullptr);
    for(unsigned i=0;i<2;++i){if(views_[i])vkDestroyImageView(device_,views_[i],nullptr);if(images_[i])vkDestroyImage(device_,images_[i],nullptr);if(memories_[i])vkFreeMemory(device_,memories_[i],nullptr);}
    images_={};views_={};memories_={};buffer_=VK_NULL_HANDLE;buffer_memory_=VK_NULL_HANDLE;sampler_=VK_NULL_HANDLE;device_=VK_NULL_HANDLE;valid_=enabled_=false;
}
}
