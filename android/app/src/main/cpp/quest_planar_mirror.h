#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <vector>

namespace hpvr::quest {
// Reused sequentially: left capture/composite, then right capture/composite.
class PlanarMirrorTarget {
public:
    bool Create(VkPhysicalDevice physical,VkDevice device,unsigned width,unsigned height,VkFormat color,VkFormat depth,unsigned layers=1);
    void Destroy();
    void Bind(VkDescriptorSet set) const;
    void Begin(VkCommandBuffer command,unsigned layer) const;
    VkRenderPass Pass() const {return pass_;}
    unsigned Width() const {return width_;}
    unsigned Height() const {return height_;}
private:
    VkDevice device_=VK_NULL_HANDLE;
    VkRenderPass pass_=VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkImageView> layer_views_;
    VkSampler sampler_=VK_NULL_HANDLE;
    std::array<VkImage,2> images_{};
    std::array<VkImageView,2> views_{};
    std::array<VkDeviceMemory,2> memory_{};
    unsigned width_=0,height_=0;
};
}
