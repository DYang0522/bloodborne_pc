// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include <bit>
#include <iterator>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <functional>

#include "bbport_copy.h"
#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "bbport_toggles.h"
#include "common/assert.h"
#include "common/debug.h"
#include "common/thread.h"
#include "imgui/renderer/texture_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_render_pass.h"
#include "bbport_threads.h"

namespace Vulkan {

std::mutex Scheduler::submit_mutex;

namespace {
const std::array DescriptorPoolSizes{
    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 2048},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 2048},
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 2048},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 2048},
    vk::DescriptorPoolSize{vk::DescriptorType::eInputAttachment, 1024},
};
} // namespace

void Scheduler::SetExecutionQueue(vk::Queue queue) {
    if (execution_queue == queue) {
        return;
    }
    // The timeline and command-pool retirement order span this scheduler's submissions.
    // Drain the old queue before a provider switch can signal a later value on another queue.
    Finish();
    execution_queue = queue;
}

Scheduler::Scheduler(const Instance& instance, bool threaded_recording, vk::Queue queue)
    : instance{instance}, execution_queue{queue ? queue : instance.GetGraphicsQueue()},
      work_semaphore{instance}, command_pool{instance, &work_semaphore} {
    // bbport: BB_VK_RECORD_THREAD=0 records on the calling thread.
    const char* env = std::getenv("BB_VK_RECORD_THREAD");
    if (threaded_recording && !(env && env[0] == '0')) {
        record_chunk = AcquireChunk();
        recorder_thread = std::jthread(std::bind_front(&Scheduler::RecorderThread, this));
        recording_threaded = true;
    }
#if TRACY_GPU_ENABLED
    profiler_scope = reinterpret_cast<tracy::VkCtxScope*>(std::malloc(sizeof(tracy::VkCtxScope)));
#endif
    AllocateWorkerCommandBuffers();
    priority_pending_ops_thread =
        std::jthread(std::bind_front(&Scheduler::PriorityPendingOpsThread, this));
}

Scheduler::~Scheduler() {
    if (recorder_thread.joinable()) {
        SyncRecording();
        recorder_thread.request_stop();
        recorder_cv.notify_all();
        recorder_thread.join();
    }
#if TRACY_GPU_ENABLED
    std::free(profiler_scope);
#endif
}

void Scheduler::BeginRendering(const RenderState& new_state) {
    if (is_rendering && render_state == new_state) {
        return;
    }
    EndRendering();

    auto target_it = std::find_if(render_targets.begin(), render_targets.end(),
                                  [&](const LegacyRenderTarget& target) {
                                      return target.state == new_state;
                                  });
    if (target_it == render_targets.end()) {
        LegacyRenderPassKey key{};
        key.num_color_attachments = new_state.num_color_attachments;
        std::array<vk::Format, 8> color_formats{};
        std::array<vk::SampleCountFlagBits, 8> color_samples{};
        std::array<vk::ImageLayout, 8> color_layouts{};
        std::vector<vk::ImageView> views;
        for (u32 i = 0; i < new_state.num_color_attachments; ++i) {
            const auto& attachment = new_state.color_attachments[i];
            color_formats[i] = attachment.image_view
                                   ? static_cast<vk::Format>(attachment.format)
                                   : vk::Format::eUndefined;
            color_samples[i] = attachment.samples
                                   ? static_cast<vk::SampleCountFlagBits>(attachment.samples)
                                   : vk::SampleCountFlagBits::e1;
            color_layouts[i] = attachment.image_view
                                   ? attachment.image_layout
                                   : vk::ImageLayout::eUndefined;
            key.color_formats[i] = static_cast<u32>(color_formats[i]);
            key.color_samples[i] = static_cast<u32>(color_samples[i]);
            key.color_layouts[i] = static_cast<u32>(color_layouts[i]);
            if (attachment.image_view && color_formats[i] != vk::Format::eUndefined) {
                views.push_back(attachment.image_view);
            }
        }

        const auto& depth = new_state.depth_stencil_attachment;
        const bool has_depth_attachment =
            depth.image_view && (depth.has_depth || depth.has_stencil) &&
            depth.format != static_cast<u32>(vk::Format::eUndefined);
        const auto depth_format = has_depth_attachment
                                      ? static_cast<vk::Format>(depth.format)
                                      : vk::Format::eUndefined;
        const auto depth_samples = depth.samples
                                       ? static_cast<vk::SampleCountFlagBits>(depth.samples)
                                       : vk::SampleCountFlagBits::e1;
        const auto depth_layout = has_depth_attachment
                                      ? depth.image_layout
                                      : vk::ImageLayout::eDepthStencilAttachmentOptimal;
        vk::ImageAspectFlags depth_aspects{};
        if (has_depth_attachment) {
            views.push_back(depth.image_view);
            if (depth.has_depth) depth_aspects |= vk::ImageAspectFlagBits::eDepth;
            if (depth.has_stencil) depth_aspects |= vk::ImageAspectFlagBits::eStencil;
        }
        key.depth_format = static_cast<u32>(depth_format);
        key.depth_samples = static_cast<u32>(depth_samples);
        key.depth_layout = static_cast<u32>(depth_layout);
        key.depth_aspects = static_cast<u32>(depth_aspects);

        auto pass_it = std::find_if(render_pass_cache.begin(), render_pass_cache.end(),
                                    [&](const CachedLegacyRenderPass& cached) {
                                        return cached.key == key;
                                    });
        if (pass_it == render_pass_cache.end()) {
            const vk::AttachmentSampleCountInfoAMD mixed_samples{
                .colorAttachmentCount = new_state.num_color_attachments,
                .pColorAttachmentSamples = color_samples.data(),
                .depthStencilAttachmentSamples = depth_samples,
            };
            auto render_pass = CreateLegacyRenderPass(
                instance.GetDevice(),
                std::span<const vk::Format>{color_formats.data(), new_state.num_color_attachments},
                std::span<const vk::SampleCountFlagBits>{color_samples.data(),
                                                          new_state.num_color_attachments},
                depth_format, depth_samples,
                std::span<const vk::ImageLayout>{color_layouts.data(),
                                                  new_state.num_color_attachments},
                depth_layout, depth_aspects,
                instance.IsMixedDepthSamplesSupported() ? &mixed_samples : nullptr);
            render_pass_cache.push_back({key, std::move(render_pass)});
            pass_it = std::prev(render_pass_cache.end());
        }
        const auto render_pass = *pass_it->render_pass;
        auto framebuffer = CreateLegacyFramebuffer(instance.GetDevice(), render_pass, views,
                                                    new_state.width, new_state.height,
                                                    new_state.num_layers);
        render_targets.push_back(LegacyRenderTarget{new_state, render_pass,
                                                    std::move(framebuffer)});
        target_it = std::prev(render_targets.end());
    }

    const vk::RenderPassBeginInfo begin_info{
        .renderPass = target_it->render_pass,
        .framebuffer = *target_it->framebuffer,
        .renderArea = {{0, 0}, {new_state.width, new_state.height}},
    };
    const vk::ClearRect clear_rect{
        .rect = {{0, 0}, {new_state.width, new_state.height}},
        .baseArrayLayer = 0,
        .layerCount = new_state.num_layers,
    };
    std::vector<vk::ClearAttachment> clears;
    for (u32 i = 0; i < new_state.num_color_attachments; ++i) {
        const auto& attachment = new_state.color_attachments[i];
        if (attachment.image_view && attachment.is_clear) {
            clears.push_back({
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .colorAttachment = i,
                .clearValue = vk::ClearValue{
                    .color = vk::ClearColorValue{.uint32 = attachment.clear_value}},
            });
        }
    }
    const auto& depth = new_state.depth_stencil_attachment;
    if (depth.image_view) {
        vk::ImageAspectFlags aspects{};
        if (depth.depth_clear) aspects |= vk::ImageAspectFlagBits::eDepth;
        if (depth.stencil_clear) aspects |= vk::ImageAspectFlagBits::eStencil;
        if (aspects) {
            clears.push_back({
                .aspectMask = aspects,
                .clearValue = vk::ClearValue{
                    .depthStencil = vk::ClearDepthStencilValue{
                        .depth = std::bit_cast<float>(depth.clear_value[0]),
                        .stencil = depth.clear_value[1]}},
            });
        }
    }
    is_rendering = true;
    render_state = new_state;
    Record([begin_info, clear_rect, clears = std::move(clears)](vk::CommandBuffer cmdbuf) {
        cmdbuf.beginRenderPass(begin_info, vk::SubpassContents::eInline);
        if (!clears.empty()) {
            cmdbuf.clearAttachments(clears, clear_rect);
        }
    });
}

void Scheduler::EndRendering() {
    if (!is_rendering) {
        return;
    }
    is_rendering = false;
    Record([](vk::CommandBuffer cmdbuf) { cmdbuf.endRenderPass(); });
}

vk::DescriptorSet Scheduler::AllocateDescriptorSet(
    vk::DescriptorSetLayout layout, std::span<const vk::WriteDescriptorSet> writes) {
    std::scoped_lock lock{descriptor_heap_mutex};
    if (!descriptor_heap) {
        descriptor_heap = std::make_unique<DescriptorHeap>(instance, &work_semaphore,
                                                            DescriptorPoolSizes, 2048);
    }
    const auto set = descriptor_heap->Commit(layout);
    if (!writes.empty()) {
        std::vector<vk::WriteDescriptorSet> updated(writes.begin(), writes.end());
        for (auto& write : updated) {
            write.dstSet = set;
        }
        instance.GetDevice().updateDescriptorSets(updated, {});
    }
    return set;
}

void Scheduler::TraceDirectRecording(void* caller) {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_RECORDER_TRACE");
        return env && env[0] == '1';
    }();
    if (!enabled) {
        return;
    }
    static std::mutex mutex;
    static std::unordered_map<void*, u64> callers;
    static u64 calls;
    std::scoped_lock lk{mutex};
    ++callers[caller];
    if (++calls % 2000) {
        return;
    }
    std::vector<std::pair<u64, void*>> top;
    for (const auto& [address, count] : callers) {
        top.emplace_back(count, address);
    }
    std::ranges::sort(top, std::greater{});
    for (size_t i = 0; i < std::min<size_t>(top.size(), 8); ++i) {
#ifdef _WIN32
        struct { const char* dli_fname; void* dli_fbase; } info{nullptr, nullptr};
#else
        Dl_info info{};
        dladdr(top[i].second, &info);
#endif
        std::printf("Recorder sync caller: %llu x %s+0x%lx\n",
                    static_cast<unsigned long long>(top[i].first),
                    info.dli_fname ? info.dli_fname : "?",
                    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(top[i].second) -
                                               reinterpret_cast<uintptr_t>(info.dli_fbase)));
    }
    callers.clear();
}

std::unique_ptr<RecordChunk> Scheduler::AcquireChunk() {
    std::scoped_lock lk{recorder_mutex};
    if (free_chunks.empty()) {
        return std::make_unique<RecordChunk>();
    }
    auto chunk = std::move(free_chunks.back());
    free_chunks.pop_back();
    return chunk;
}

void Scheduler::SignalAfterHostCopies(std::function<void()> signal) {
    if (!IsRecordingDeferred()) {
        WaitHostCopies();
        signal();
        return;
    }
    BbCopy::FlushBatch();
    deferred_signals_issued.fetch_add(1, std::memory_order_relaxed);
    Record([signal = std::move(signal), done = deferred_signals_done](vk::CommandBuffer) mutable {
        BbCopy::AfterCopies([signal = std::move(signal), done = std::move(done)] {
            signal();
            done->fetch_add(1, std::memory_order_release);
        });
    });
    KickRecording(true);
}

void Scheduler::WaitDeferredSignals() {
    const u64 issued = deferred_signals_issued.load(std::memory_order_relaxed);
    if (deferred_signals_done->load(std::memory_order_acquire) >= issued ||
        BbToggle::Disabled(BbToggle::OrderedGuestWrites)) {
        return;
    }
    BbStats::WaitTimer timer{BbStats::host_copies_wait_ns};
    KickRecording(true);
    while (deferred_signals_done->load(std::memory_order_acquire) < issued) {
        // Helps the copy threads the signals wait for.
        BbCopy::WaitAsync();
        std::this_thread::yield();
    }
}

void Scheduler::WaitHostCopies() {
    if (host_copies_done.load(std::memory_order_acquire) < host_copies_issued) {
        BbStats::WaitTimer timer{BbStats::host_copies_wait_ns};
        BbStats::host_copy_waits.fetch_add(1, std::memory_order_relaxed);
        KickRecording(true);
        while (host_copies_done.load(std::memory_order_acquire) < host_copies_issued) {
            std::this_thread::yield();
        }
    }
    BbStats::WaitTimer timer{BbStats::copy_threads_wait_ns};
    BbCopy::WaitAsync();
}

void Scheduler::KickRecording(bool force) {
    if (!recording_threaded) {
        return;
    }
    // Callers kick where nobody holds the raw command buffer: deferral resumes.
    direct_mode = false;
    // Batches of tens of KiB keep the queue handoff cheap relative to the work it carries.
    if (!force && full_chunks.empty() && record_chunk->Size() < 32 * 1024) {
        return;
    }
    if (full_chunks.empty() && record_chunk->Empty()) {
        return;
    }
    // bbport: the replacement is taken before the current chunk is handed over, so record_chunk
    // is never null (see RetireChunk). AcquireChunk() takes recorder_mutex itself.
    std::unique_ptr<RecordChunk> next = record_chunk->Empty() ? nullptr : AcquireChunk();
    bool wake;
    {
        std::scoped_lock lk{recorder_mutex};
        for (auto& chunk : full_chunks) {
            recorder_queue.push_back(std::move(chunk));
        }
        if (next) {
            recorder_queue.push_back(std::exchange(record_chunk, std::move(next)));
        }
        wake = recorder_sleeping;
        queued_chunks.store(recorder_queue.size(), std::memory_order_release);
    }
    full_chunks.clear();
    // A busy recorder picks the new chunks up by itself: waking it is a syscall per draw.
    if (wake) {
        recorder_cv.notify_one();
    }
    if (!record_chunk) {
        record_chunk = AcquireChunk();
    }
}

void Scheduler::SyncRecording() {
    if (!recording_threaded) {
        return;
    }
    KickRecording(true);
    BbStats::WaitTimer timer{BbStats::sync_recording_ns};
    std::unique_lock lk{recorder_mutex};
    recorder_idle_cv.wait(lk, [this] { return recorder_queue.empty() && !recorder_busy; });
}

void Scheduler::RecorderThread(std::stop_token stoken) {
    Common::SetCurrentThreadName("bb:VkRecorder");
    while (true) {
        // Spin briefly before sleeping: the next chunk usually follows within microseconds,
        // and a sleeping recorder costs the GPU thread a wake-up syscall per kick. With few
        // hardware threads (Steam Deck: 8) the spin would take time from guest threads.
        static const auto spin_time = std::chrono::microseconds(
            BbThreads::Available() >= 12 ? 200 : 20);
        const auto spin_until = std::chrono::steady_clock::now() + spin_time;
        for (u32 spins = 1; queued_chunks.load(std::memory_order_acquire) == 0; ++spins) {
            __builtin_ia32_pause();
            // The clock is read every 256 pauses, not per iteration.
            if (!(spins & 255) &&
                (stoken.stop_requested() || std::chrono::steady_clock::now() >= spin_until)) {
                break;
            }
        }
        std::unique_ptr<RecordChunk> chunk;
        {
            std::unique_lock lk{recorder_mutex};
            recorder_sleeping = true;
            recorder_cv.wait(lk, stoken, [this] { return !recorder_queue.empty(); });
            recorder_sleeping = false;
            if (recorder_queue.empty()) {
                return; // stop requested
            }
            chunk = std::move(recorder_queue.front());
            recorder_queue.pop_front();
            queued_chunks.store(recorder_queue.size(), std::memory_order_release);
            recorder_busy = true;
        }
        // current_cmdbuf only changes after SyncRecording(), which waits for this thread.
        chunk->Execute(current_cmdbuf);
        {
            std::scoped_lock lk{recorder_mutex};
            free_chunks.push_back(std::move(chunk));
            recorder_busy = false;
            if (recorder_queue.empty()) {
                recorder_idle_cv.notify_all();
            }
        }
    }
}

void Scheduler::Flush(SubmitInfo& info) {
    // When flushing, we only send data to the driver; no waiting is necessary.
    SubmitExecution(info);
}

void Scheduler::Flush() {
    SubmitInfo info{};
    Flush(info);
}

void Scheduler::Finish() {
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitInfo info{};
    SubmitExecution(info);
    Wait(presubmit_tick);
}

void Scheduler::WaitSubmitted() {
    const u64 tick = CurrentTick();
    if (tick > 1) {
        Wait(tick - 1);
    }
}

void Scheduler::Wait(u64 tick) {
    if (tick >= work_semaphore.CurrentTick()) {
        // Make sure we are not waiting for the current tick without signalling
        SubmitInfo info{};
        Flush(info);
    }
    BbStats::WaitTimer timer{BbStats::tick_wait_ns};
    work_semaphore.Wait(tick);
    PopPendingOperations();
}

void Scheduler::PopPendingOperations() {
    {
        std::scoped_lock lk{render_targets_mutex};
        if (!pending_render_targets.empty()) {
            work_semaphore.Refresh();
            while (!pending_render_targets.empty() &&
                   work_semaphore.IsFree(pending_render_targets.front().tick)) {
                pending_render_targets.pop_front();
            }
        }
    }
    if (num_pending_ops.load(std::memory_order_acquire) == 0) {
        return; // every draw comes here
    }
    std::unique_lock lk(pending_ops_mutex);
    // bbport: this runs on every draw and dispatch. Querying the timeline semaphore is an
    // ioctl, so it is skipped when nothing waits and done once per 32 calls (~0.3 ms; reading
    // the clock per draw instead was itself a hot spot).
    if (pending_ops.empty()) {
        return;
    }
    if (!work_semaphore.IsFree(pending_ops.front().gpu_tick)) {
        if ((++pending_polls & 31) != 0 && !BbToggle::Disabled(BbToggle::PendingPollLimit)) {
            return;
        }
        work_semaphore.Refresh();
    }
    while (!pending_ops.empty() && work_semaphore.IsFree(pending_ops.front().gpu_tick)) {
        pending_ops.front().callback();
        pending_ops.pop();
        num_pending_ops.fetch_sub(1, std::memory_order_release);
    }
}

void Scheduler::AllocateWorkerCommandBuffers() {
    const vk::CommandBufferBeginInfo begin_info = {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };

    current_cmdbuf = command_pool.Commit();
    Check(current_cmdbuf.begin(begin_info));

    // Invalidate dynamic state so it gets applied to the new command buffer.
    dynamic_state.Invalidate();

#if TRACY_GPU_ENABLED
    auto* profiler_ctx = instance.GetProfilerContext();
    if (profiler_ctx) {
        static const auto scope_loc =
            GPU_SCOPE_LOCATION("Guest Frame", MarkersPalette::GpuMarkerColor);
        new (profiler_scope) tracy::VkCtxScope{profiler_ctx, &scope_loc, current_cmdbuf, true};
    }
#endif
}

void Scheduler::SubmitExecution(SubmitInfo& info) {
    std::scoped_lock lk{submit_mutex};
    const u64 signal_value = work_semaphore.NextTick();

#if TRACY_GPU_ENABLED
    auto* profiler_ctx = instance.GetProfilerContext();
    if (profiler_ctx) {
        profiler_scope->~VkCtxScope();
        TracyVkCollect(profiler_ctx, current_cmdbuf);
    }
#endif

    if (on_submit) {
        on_submit(info);
    }

    EndRendering();
    if (auto* profiler = GpuProfiler::Get(); profiler && profiler->Records(this)) {
        // Until the next submission's first timestamp: mostly the GPU waiting for it.
        profiler->Mark(0x5B317ull, [] { return std::string{"(between submissions: GPU idle)"}; });
    }
    SyncRecording();
    // Guest memory copies into staging read by this submission (copy threads).
    WaitHostCopies();
    Check(current_cmdbuf.end());

    const vk::Semaphore timeline = work_semaphore.Handle();
    info.AddSignal(timeline, signal_value);

    // 1.8.2's masks (the second wait is the presenter's image), one for every wait the
    // submission can carry, and the external provider's wait after them.
    std::array<vk::PipelineStageFlags, std::tuple_size_v<decltype(info.wait_semas)>>
        wait_stage_masks;
    wait_stage_masks.fill(vk::PipelineStageFlagBits::eAllCommands);
    wait_stage_masks[1] = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    if (pending_external_wait) {
        ASSERT(info.num_wait_semas < info.wait_semas.size());
        wait_stage_masks[info.num_wait_semas] = vk::PipelineStageFlagBits::eAllCommands;
        info.AddWait(pending_external_wait, pending_external_value);
        pending_external_wait = nullptr;
    }

    const vk::TimelineSemaphoreSubmitInfo timeline_si = {
        .waitSemaphoreValueCount = info.num_wait_semas,
        .pWaitSemaphoreValues = info.wait_ticks.data(),
        .signalSemaphoreValueCount = info.num_signal_semas,
        .pSignalSemaphoreValues = info.signal_ticks.data(),
    };

    const vk::SubmitInfo submit_info = {
        .pNext = &timeline_si,
        .waitSemaphoreCount = info.num_wait_semas,
        .pWaitSemaphores = info.wait_semas.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1U,
        .pCommandBuffers = &current_cmdbuf,
        .signalSemaphoreCount = info.num_signal_semas,
        .pSignalSemaphores = info.signal_semas.data(),
    };

    ImGui::Core::TextureManager::Submit();
    auto submit_result = execution_queue.submit(submit_info, info.fence);
    if (submit_result == vk::Result::eErrorDeviceLost) {
        instance.ReportDeviceLost("submit");
    }
    ASSERT_MSG(submit_result != vk::Result::eErrorDeviceLost, "Device lost during submit");

    if (!render_targets.empty()) {
        std::scoped_lock lk{render_targets_mutex};
        pending_render_targets.push_back({signal_value, std::move(render_targets)});
        render_targets.clear();
    }
    work_semaphore.Refresh();
    AllocateWorkerCommandBuffers();

    // Apply pending operations
    PopPendingOperations();
}

void Scheduler::PriorityPendingOpsThread(std::stop_token stoken) {
    Common::SetCurrentThreadName("shadPS4:GpuSchedPriorityPendingOpsRunner");

    while (!stoken.stop_requested()) {
        PendingOp op;
        {
            std::unique_lock lk(priority_pending_ops_mutex);
            priority_pending_ops_cv.wait(lk, stoken,
                                         [this] { return !priority_pending_ops.empty(); });
            if (stoken.stop_requested()) {
                break;
            }

            op = std::move(priority_pending_ops.front());
            priority_pending_ops.pop();
        }

        work_semaphore.Wait(op.gpu_tick);
        if (stoken.stop_requested()) {
            break;
        }

        op.callback();
    }
}

void DynamicState::Commit(const Instance& instance, const vk::CommandBuffer& cmdbuf) {
    // A null command buffer only updates the dirty flags, exactly as recording would.
    CommitWith(instance.IsDepthBoundsSupported(), instance.IsDynamicColorWriteMaskSupported(),
               instance.IsAttachmentFeedbackLoopLayoutSupported(), [&](auto&& command) {
                   if (cmdbuf) {
                       command(cmdbuf);
                   }
               });
}

} // namespace Vulkan
