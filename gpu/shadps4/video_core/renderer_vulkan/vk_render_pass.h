// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <span>
#include <vector>

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

// Vulkan 1.2 render-pass path used when VK_KHR_dynamic_rendering is not exposed.
inline vk::UniqueRenderPass CreateLegacyRenderPass(
    vk::Device device, std::span<const vk::Format> color_formats,
    std::span<const vk::SampleCountFlagBits> color_samples, vk::Format depth_format,
    vk::SampleCountFlagBits depth_samples, std::span<const vk::ImageLayout> color_layouts = {},
    vk::ImageLayout depth_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    vk::ImageAspectFlags depth_aspects =
        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
    const void* next = nullptr) {
    ASSERT(color_formats.size() == color_samples.size());
    ASSERT(color_layouts.empty() || color_layouts.size() == color_formats.size());

    std::vector<vk::AttachmentDescription2> attachments;
    std::vector<vk::AttachmentReference2> color_refs;
    attachments.reserve(color_formats.size() + (depth_format != vk::Format::eUndefined));
    color_refs.reserve(color_formats.size());

    for (u32 i = 0; i < color_formats.size(); ++i) {
        if (color_formats[i] == vk::Format::eUndefined) {
            color_refs.push_back({
                .attachment = VK_ATTACHMENT_UNUSED,
                .layout = color_layouts.empty() ? vk::ImageLayout::eColorAttachmentOptimal
                                                : color_layouts[i],
                .aspectMask = vk::ImageAspectFlagBits::eColor,
            });
            continue;
        }
        const u32 index = static_cast<u32>(attachments.size());
        attachments.push_back({
            .format = color_formats[i],
            .samples = color_samples[i],
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = color_layouts.empty() ? vk::ImageLayout::eColorAttachmentOptimal
                                                   : color_layouts[i],
            .finalLayout = color_layouts.empty() ? vk::ImageLayout::eColorAttachmentOptimal
                                                 : color_layouts[i],
        });
        color_refs.push_back({
            .attachment = index,
            .layout = color_layouts.empty() ? vk::ImageLayout::eColorAttachmentOptimal
                                            : color_layouts[i],
            .aspectMask = vk::ImageAspectFlagBits::eColor,
        });
    }

    vk::AttachmentReference2 depth_ref{};
    if (depth_format != vk::Format::eUndefined) {
        const u32 index = static_cast<u32>(attachments.size());
        attachments.push_back({
            .format = depth_format,
            .samples = depth_samples,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eLoad,
            .stencilStoreOp = vk::AttachmentStoreOp::eStore,
            .initialLayout = depth_layout,
            .finalLayout = depth_layout,
        });
        depth_ref = {
            .attachment = index,
            .layout = depth_layout,
            .aspectMask = depth_aspects,
        };
    }

    const vk::SubpassDescription2 subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = static_cast<u32>(color_refs.size()),
        .pColorAttachments = color_refs.data(),
        .pDepthStencilAttachment =
            depth_format != vk::Format::eUndefined ? &depth_ref : nullptr,
    };
    const vk::RenderPassCreateInfo2 create_info{
        .pNext = next,
        .attachmentCount = static_cast<u32>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    auto [result, render_pass] = device.createRenderPass2Unique(create_info);
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create legacy render pass: {}",
               vk::to_string(result));
    return std::move(render_pass);
}

inline vk::UniqueFramebuffer CreateLegacyFramebuffer(vk::Device device, vk::RenderPass render_pass,
                                                       std::span<const vk::ImageView> views,
                                                       u32 width, u32 height, u32 layers) {
    const vk::FramebufferCreateInfo create_info{
        .renderPass = render_pass,
        .attachmentCount = static_cast<u32>(views.size()),
        .pAttachments = views.data(),
        .width = width,
        .height = height,
        .layers = layers,
    };
    auto [result, framebuffer] = device.createFramebufferUnique(create_info);
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create legacy framebuffer: {}",
               vk::to_string(result));
    return std::move(framebuffer);
}

} // namespace Vulkan
