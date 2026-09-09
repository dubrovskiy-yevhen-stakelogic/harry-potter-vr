#pragma once
#include "hpvr/quest_reflection_math.h"
#include <vulkan/vulkan.h>
#include <array>
namespace hpvr::quest {
// Original HPVR implementation. One shared left-eye history, sampled by the
// material shader. No second scene draw and no fullscreen reflection pass.
class ReflectionHistory {
public:
    bool Create(VkPhysicalDevice physical,VkDevice device,VkQueue queue,unsigned family,
                unsigned width,unsigned height,VkFormat color,VkFormat depth);
    void Destroy();
    void Bind(VkDescriptorSet set);
    void Prepare(bool enabled,int strength,const Matrix4& current,unsigned width,unsigned height);
    bool Enabled() const {return enabled_;}
    void Capture(VkCommandBuffer command,VkImage color,VkImage depth,const Matrix4& matrix,unsigned width,unsigned height);
private:
    struct Uniform {Matrix4 projection{},inverse{};std::array<float,4> eye{},params{},depth_uv{};} data_;
    VkPhysicalDevice physical_=VK_NULL_HANDLE;VkDevice device_=VK_NULL_HANDLE;
    std::array<VkImage,2> images_{};std::array<VkDeviceMemory,2> memories_{};std::array<VkImageView,2> views_{};
    VkSampler sampler_=VK_NULL_HANDLE;VkBuffer buffer_=VK_NULL_HANDLE;VkDeviceMemory buffer_memory_=VK_NULL_HANDLE;
    void* mapped_=nullptr;unsigned width_=0,height_=0,last_width_=0,last_height_=0,divisor_=1;
    unsigned depth_width_=0,depth_height_=0,depth_divisor_=1;
    bool valid_=false,enabled_=false;
};
}
