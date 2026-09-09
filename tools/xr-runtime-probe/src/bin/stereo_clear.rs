use std::error::Error;
use std::ffi::{CStr, CString};
use std::path::PathBuf;
use std::time::{Duration, Instant};

use ash::{
    Entry as VulkanEntry,
    vk::{self, Handle},
};
use openxr as xr;

const VIEW_TYPE: xr::ViewConfigurationType = xr::ViewConfigurationType::PRIMARY_STEREO;
const VIEW_COUNT: u32 = 2;
const DEFAULT_FRAME_COUNT: u32 = 300;
const SESSION_READY_TIMEOUT: Duration = Duration::from_secs(120);

struct Options {
    loader: Option<PathBuf>,
    frame_count: u32,
}

struct ImageResources {
    left_framebuffer: vk::Framebuffer,
    right_framebuffer: vk::Framebuffer,
}

enum FrameOutcome {
    Skipped,
    Rendered(xr::ViewStateFlags),
}

struct VulkanCore {
    instance: ash::Instance,
    device: Option<ash::Device>,
}

impl VulkanCore {
    fn new(instance: ash::Instance) -> Self {
        Self {
            instance,
            device: None,
        }
    }

    fn device(&self) -> &ash::Device {
        self.device
            .as_ref()
            .expect("Vulkan device must exist before use")
    }
}

impl Drop for VulkanCore {
    fn drop(&mut self) {
        unsafe {
            if let Some(device) = self.device.take() {
                let _ = device.device_wait_idle();
                device.destroy_device(None);
            }
            self.instance.destroy_instance(None);
        }
    }
}

struct VulkanDrawResources {
    device: ash::Device,
    render_pass: Option<vk::RenderPass>,
    image_views: Vec<vk::ImageView>,
    framebuffers: Vec<vk::Framebuffer>,
    command_pool: Option<vk::CommandPool>,
    fence: Option<vk::Fence>,
}

impl VulkanDrawResources {
    fn new(device: ash::Device) -> Self {
        Self {
            device,
            render_pass: None,
            image_views: Vec::new(),
            framebuffers: Vec::new(),
            command_pool: None,
            fence: None,
        }
    }
}

impl Drop for VulkanDrawResources {
    fn drop(&mut self) {
        unsafe {
            let _ = self.device.device_wait_idle();
            if let Some(fence) = self.fence.take() {
                self.device.destroy_fence(fence, None);
            }
            if let Some(command_pool) = self.command_pool.take() {
                self.device.destroy_command_pool(command_pool, None);
            }
            for framebuffer in self.framebuffers.drain(..) {
                self.device.destroy_framebuffer(framebuffer, None);
            }
            for image_view in self.image_views.drain(..) {
                self.device.destroy_image_view(image_view, None);
            }
            if let Some(render_pass) = self.render_pass.take() {
                self.device.destroy_render_pass(render_pass, None);
            }
        }
    }
}

fn parse_options() -> Result<Options, Box<dyn Error>> {
    let mut options = Options {
        loader: std::env::var_os("HPVR_OPENXR_LOADER").map(PathBuf::from),
        frame_count: DEFAULT_FRAME_COUNT,
    };
    let mut arguments = std::env::args_os().skip(1);
    while let Some(argument) = arguments.next() {
        match argument.to_string_lossy().as_ref() {
            "--loader" => {
                options.loader = Some(
                    arguments
                        .next()
                        .ok_or("--loader requires a path to openxr_loader.dll")?
                        .into(),
                );
            }
            "--frames" => {
                let value = arguments
                    .next()
                    .ok_or("--frames requires a positive integer")?;
                options.frame_count = value.to_string_lossy().parse()?;
                if options.frame_count == 0 || options.frame_count > 10_000 {
                    return Err("--frames must be in the range 1..=10000".into());
                }
            }
            unknown => {
                return Err(format!(
                    "unknown argument '{unknown}'; expected --loader <path> or --frames <count>"
                )
                .into());
            }
        }
    }
    Ok(options)
}

fn choose_color_format(formats: &[u32]) -> Option<vk::Format> {
    [
        vk::Format::R8G8B8A8_SRGB,
        vk::Format::B8G8R8A8_SRGB,
        vk::Format::R8G8B8A8_UNORM,
        vk::Format::B8G8R8A8_UNORM,
    ]
    .into_iter()
    .find(|format| formats.contains(&(format.as_raw() as u32)))
}

fn extension_names(raw: &str) -> Result<Vec<CString>, Box<dyn Error>> {
    raw.split_whitespace()
        .map(|name| CString::new(name).map_err(Into::into))
        .collect()
}

fn main() {
    if let Err(error) = run() {
        eprintln!("hpvr-xr-stereo-clear: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), Box<dyn Error>> {
    let options = parse_options()?;
    println!("HPVR OpenXR Vulkan stereo-clear probe");
    let entry = unsafe {
        match options.loader.as_deref() {
            Some(path) => {
                println!("OpenXR loader: {}", path.display());
                xr::Entry::load_from(path)?
            }
            None => {
                println!("OpenXR loader: platform default search");
                xr::Entry::load()?
            }
        }
    };

    let available_extensions = entry.enumerate_extensions()?;
    if !available_extensions.khr_vulkan_enable {
        return Err("active runtime does not expose XR_KHR_vulkan_enable".into());
    }

    let mut enabled_extensions = xr::ExtensionSet::default();
    enabled_extensions.khr_vulkan_enable = true;
    let xr_instance = entry.create_instance(
        &xr::ApplicationInfo {
            application_name: "Harry Potter VR stereo-clear probe",
            application_version: 1,
            engine_name: "HPVR",
            engine_version: 1,
            api_version: xr::Version::new(1, 0, 0),
        },
        &enabled_extensions,
        &[],
    )?;
    let runtime = xr_instance.properties()?;
    println!(
        "runtime: {} {}",
        runtime.runtime_name, runtime.runtime_version
    );

    let system = xr_instance
        .system(xr::FormFactor::HEAD_MOUNTED_DISPLAY)
        .map_err(|error| format!("no active HMD system: {error}"))?;
    let view_configuration = xr_instance.enumerate_view_configuration_views(system, VIEW_TYPE)?;
    if view_configuration.len() != VIEW_COUNT as usize {
        return Err(format!(
            "PRIMARY_STEREO reported {} views instead of {VIEW_COUNT}",
            view_configuration.len()
        )
        .into());
    }
    if view_configuration[0].recommended_image_rect_width
        != view_configuration[1].recommended_image_rect_width
        || view_configuration[0].recommended_image_rect_height
            != view_configuration[1].recommended_image_rect_height
    {
        return Err("the two recommended eye extents differ; array swapchain is unsafe".into());
    }

    let requirements = xr_instance.graphics_requirements::<xr::Vulkan>(system)?;
    let target_vulkan_xr = xr::Version::new(1, 1, 0);
    if target_vulkan_xr < requirements.min_api_version_supported
        || target_vulkan_xr > requirements.max_api_version_supported
    {
        return Err(format!(
            "Vulkan 1.1 is outside runtime range {}..={}",
            requirements.min_api_version_supported, requirements.max_api_version_supported
        )
        .into());
    }
    println!(
        "Vulkan API range: {}..={}",
        requirements.min_api_version_supported, requirements.max_api_version_supported
    );

    let environment_blend_mode = *xr_instance
        .enumerate_environment_blend_modes(system, VIEW_TYPE)?
        .first()
        .ok_or("runtime reported no environment blend mode")?;

    unsafe {
        let vk_entry = VulkanEntry::load()?;
        let vk_application_info = vk::ApplicationInfo::default().api_version(vk::API_VERSION_1_1);
        let instance_extensions =
            extension_names(&xr_instance.vulkan_legacy_instance_extensions(system)?)?;
        let instance_extension_pointers = instance_extensions
            .iter()
            .map(|name| name.as_ptr())
            .collect::<Vec<_>>();
        println!(
            "runtime Vulkan instance extensions: {}",
            instance_extensions
                .iter()
                .map(|name| name.to_string_lossy())
                .collect::<Vec<_>>()
                .join(" ")
        );
        let vk_instance_create_info = vk::InstanceCreateInfo::default()
            .application_info(&vk_application_info)
            .enabled_extension_names(&instance_extension_pointers);
        let mut vulkan_core =
            VulkanCore::new(vk_entry.create_instance(&vk_instance_create_info, None)?);

        let physical_device = vk::PhysicalDevice::from_raw(
            xr_instance
                .vulkan_graphics_device(system, vulkan_core.instance.handle().as_raw() as _)?
                as _,
        );
        let physical_device_properties = vulkan_core
            .instance
            .get_physical_device_properties(physical_device);
        let gpu_name =
            CStr::from_ptr(physical_device_properties.device_name.as_ptr()).to_string_lossy();
        println!(
            "OpenXR-selected GPU: {gpu_name}; Vulkan {}.{}.{}",
            vk::api_version_major(physical_device_properties.api_version),
            vk::api_version_minor(physical_device_properties.api_version),
            vk::api_version_patch(physical_device_properties.api_version)
        );

        let queue_families = vulkan_core
            .instance
            .get_physical_device_queue_family_properties(physical_device);
        let queue_family_index = queue_families
            .iter()
            .enumerate()
            .find_map(|(index, properties)| {
                (properties.queue_count > 0
                    && properties
                        .queue_flags
                        .contains(vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE))
                .then_some(index as u32)
            })
            .or_else(|| {
                queue_families
                    .iter()
                    .enumerate()
                    .find_map(|(index, properties)| {
                        (properties.queue_count > 0
                            && properties.queue_flags.contains(vk::QueueFlags::GRAPHICS))
                        .then_some(index as u32)
                    })
            })
            .ok_or("OpenXR-selected GPU has no graphics queue")?;
        println!("graphics queue family: {queue_family_index}");

        let device_extensions =
            extension_names(&xr_instance.vulkan_legacy_device_extensions(system)?)?;
        let device_extension_pointers = device_extensions
            .iter()
            .map(|name| name.as_ptr())
            .collect::<Vec<_>>();
        println!(
            "runtime Vulkan device extensions: {}",
            device_extensions
                .iter()
                .map(|name| name.to_string_lossy())
                .collect::<Vec<_>>()
                .join(" ")
        );
        let queue_priorities = [1.0_f32];
        let queue_create_infos = [vk::DeviceQueueCreateInfo::default()
            .queue_family_index(queue_family_index)
            .queue_priorities(&queue_priorities)];
        let vk_device_create_info = vk::DeviceCreateInfo::default()
            .queue_create_infos(&queue_create_infos)
            .enabled_extension_names(&device_extension_pointers);
        vulkan_core.device = Some(vulkan_core.instance.create_device(
            physical_device,
            &vk_device_create_info,
            None,
        )?);
        let vk_device = vulkan_core.device().clone();
        let queue = vk_device.get_device_queue(queue_family_index, 0);

        let (session, mut frame_waiter, mut frame_stream) = xr_instance
            .create_session::<xr::Vulkan>(
                system,
                &xr::vulkan::SessionCreateInfo {
                    instance: vulkan_core.instance.handle().as_raw() as _,
                    physical_device: physical_device.as_raw() as _,
                    device: vk_device.handle().as_raw() as _,
                    queue_family_index,
                    queue_index: 0,
                },
            )?;
        let reference_space =
            session.create_reference_space(xr::ReferenceSpaceType::LOCAL, xr::Posef::IDENTITY)?;

        let swapchain_formats = session.enumerate_swapchain_formats()?;
        let color_format = choose_color_format(&swapchain_formats)
            .ok_or("runtime exposes none of the supported RGBA/BGRA color formats")?;
        let width = view_configuration[0].recommended_image_rect_width;
        let height = view_configuration[0].recommended_image_rect_height;
        println!(
            "stereo swapchain: {}x{}x2, Vulkan format {}, sample_count=1",
            width,
            height,
            color_format.as_raw()
        );

        let mut swapchain = session.create_swapchain(&xr::SwapchainCreateInfo {
            create_flags: xr::SwapchainCreateFlags::EMPTY,
            usage_flags: xr::SwapchainUsageFlags::COLOR_ATTACHMENT,
            format: color_format.as_raw() as _,
            sample_count: 1,
            width,
            height,
            face_count: 1,
            array_size: VIEW_COUNT,
            mip_count: 1,
        })?;
        let swapchain_images = swapchain.enumerate_images()?;

        let color_attachment = vk::AttachmentDescription {
            format: color_format,
            samples: vk::SampleCountFlags::TYPE_1,
            load_op: vk::AttachmentLoadOp::CLEAR,
            store_op: vk::AttachmentStoreOp::STORE,
            stencil_load_op: vk::AttachmentLoadOp::DONT_CARE,
            stencil_store_op: vk::AttachmentStoreOp::DONT_CARE,
            initial_layout: vk::ImageLayout::UNDEFINED,
            final_layout: vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            ..Default::default()
        };
        let color_reference = vk::AttachmentReference {
            attachment: 0,
            layout: vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        };
        let color_references = [color_reference];
        let subpass = vk::SubpassDescription::default()
            .pipeline_bind_point(vk::PipelineBindPoint::GRAPHICS)
            .color_attachments(&color_references);
        let attachments = [color_attachment];
        let subpasses = [subpass];
        let dependency = vk::SubpassDependency {
            src_subpass: vk::SUBPASS_EXTERNAL,
            dst_subpass: 0,
            src_stage_mask: vk::PipelineStageFlags::COLOR_ATTACHMENT_OUTPUT,
            dst_stage_mask: vk::PipelineStageFlags::COLOR_ATTACHMENT_OUTPUT,
            dst_access_mask: vk::AccessFlags::COLOR_ATTACHMENT_WRITE,
            ..Default::default()
        };
        let dependencies = [dependency];
        let mut draw_resources = VulkanDrawResources::new(vk_device.clone());
        let render_pass = vk_device.create_render_pass(
            &vk::RenderPassCreateInfo::default()
                .attachments(&attachments)
                .subpasses(&subpasses)
                .dependencies(&dependencies),
            None,
        )?;
        draw_resources.render_pass = Some(render_pass);

        let mut image_resources = Vec::with_capacity(swapchain_images.len());
        for image in swapchain_images {
            let image = vk::Image::from_raw(image);
            let mut create_eye_target =
                |array_layer: u32| -> Result<(vk::ImageView, vk::Framebuffer), vk::Result> {
                    let view = vk_device.create_image_view(
                        &vk::ImageViewCreateInfo::default()
                            .image(image)
                            .view_type(vk::ImageViewType::TYPE_2D)
                            .format(color_format)
                            .subresource_range(vk::ImageSubresourceRange {
                                aspect_mask: vk::ImageAspectFlags::COLOR,
                                base_mip_level: 0,
                                level_count: 1,
                                base_array_layer: array_layer,
                                layer_count: 1,
                            }),
                        None,
                    )?;
                    draw_resources.image_views.push(view);
                    let framebuffer_attachments = [view];
                    let framebuffer = vk_device.create_framebuffer(
                        &vk::FramebufferCreateInfo::default()
                            .render_pass(render_pass)
                            .attachments(&framebuffer_attachments)
                            .width(width)
                            .height(height)
                            .layers(1),
                        None,
                    )?;
                    draw_resources.framebuffers.push(framebuffer);
                    Ok((view, framebuffer))
                };
            let (_, left_framebuffer) = create_eye_target(0)?;
            let (_, right_framebuffer) = create_eye_target(1)?;
            image_resources.push(ImageResources {
                left_framebuffer,
                right_framebuffer,
            });
        }

        let command_pool = vk_device.create_command_pool(
            &vk::CommandPoolCreateInfo::default()
                .queue_family_index(queue_family_index)
                .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER),
            None,
        )?;
        draw_resources.command_pool = Some(command_pool);
        let command_buffer = vk_device.allocate_command_buffers(
            &vk::CommandBufferAllocateInfo::default()
                .command_pool(command_pool)
                .level(vk::CommandBufferLevel::PRIMARY)
                .command_buffer_count(1),
        )?[0];
        let fence = vk_device.create_fence(
            &vk::FenceCreateInfo::default().flags(vk::FenceCreateFlags::SIGNALED),
            None,
        )?;
        draw_resources.fence = Some(fence);

        let left_clear = vk::ClearValue {
            color: vk::ClearColorValue {
                float32: [0.55, 0.02, 0.02, 1.0],
            },
        };
        let right_clear = vk::ClearValue {
            color: vk::ClearColorValue {
                float32: [0.02, 0.05, 0.65, 1.0],
            },
        };
        let render_area = vk::Rect2D {
            offset: vk::Offset2D::default(),
            extent: vk::Extent2D { width, height },
        };

        let mut event_buffer = xr::EventDataBuffer::new();
        let mut session_running = false;
        let mut exit_requested = false;
        let mut rendered_frames = 0_u32;
        let mut wait_started = Instant::now();
        let mut render_wait_started = Instant::now();

        'session_loop: loop {
            while let Some(event) = xr_instance.poll_event(&mut event_buffer)? {
                use xr::Event;
                match event {
                    Event::SessionStateChanged(event) => {
                        println!("session state: {:?}", event.state());
                        match event.state() {
                            xr::SessionState::READY => {
                                session.begin(VIEW_TYPE)?;
                                session_running = true;
                                render_wait_started = Instant::now();
                            }
                            xr::SessionState::STOPPING => {
                                session.end()?;
                                session_running = false;
                                wait_started = Instant::now();
                                if exit_requested {
                                    break 'session_loop;
                                }
                            }
                            xr::SessionState::EXITING | xr::SessionState::LOSS_PENDING => {
                                break 'session_loop;
                            }
                            _ => {}
                        }
                    }
                    Event::InstanceLossPending(_) => break 'session_loop,
                    Event::EventsLost(event) => {
                        println!("OpenXR events lost: {}", event.lost_event_count());
                    }
                    _ => {}
                }
            }

            if !session_running {
                if wait_started.elapsed() > SESSION_READY_TIMEOUT {
                    return Err("session did not reach READY within 120 seconds".into());
                }
                std::thread::sleep(Duration::from_millis(10));
                continue;
            }

            let frame_state = frame_waiter.wait()?;
            frame_stream.begin()?;
            let mut frame_ended = false;
            let mut image_acquired = false;
            let mut image_waited = false;
            let mut gpu_may_be_in_flight = false;
            let frame_result = (|| -> Result<FrameOutcome, Box<dyn Error>> {
                if !frame_state.should_render {
                    frame_stream.end(
                        frame_state.predicted_display_time,
                        environment_blend_mode,
                        &[],
                    )?;
                    frame_ended = true;
                    return Ok(FrameOutcome::Skipped);
                }

                let (view_state, views) = session.locate_views(
                    VIEW_TYPE,
                    frame_state.predicted_display_time,
                    &reference_space,
                )?;
                if views.len() != VIEW_COUNT as usize {
                    return Err("locate_views did not return exactly two eyes".into());
                }
                let required_view_state =
                    xr::ViewStateFlags::ORIENTATION_VALID | xr::ViewStateFlags::POSITION_VALID;
                if !view_state.contains(required_view_state) {
                    println!("skipping frame with invalid view state: {view_state:?}");
                    frame_stream.end(
                        frame_state.predicted_display_time,
                        environment_blend_mode,
                        &[],
                    )?;
                    frame_ended = true;
                    return Ok(FrameOutcome::Skipped);
                }

                let image_index = swapchain.acquire_image()?;
                image_acquired = true;
                swapchain.wait_image(xr::Duration::INFINITE)?;
                image_waited = true;
                vk_device.wait_for_fences(&[fence], true, u64::MAX)?;
                vk_device.reset_fences(&[fence])?;
                vk_device
                    .reset_command_buffer(command_buffer, vk::CommandBufferResetFlags::empty())?;
                vk_device.begin_command_buffer(
                    command_buffer,
                    &vk::CommandBufferBeginInfo::default()
                        .flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT),
                )?;

                let targets = &image_resources[image_index as usize];
                for (framebuffer, clear) in [
                    (targets.left_framebuffer, left_clear),
                    (targets.right_framebuffer, right_clear),
                ] {
                    let clear_values = [clear];
                    vk_device.cmd_begin_render_pass(
                        command_buffer,
                        &vk::RenderPassBeginInfo::default()
                            .render_pass(render_pass)
                            .framebuffer(framebuffer)
                            .render_area(render_area)
                            .clear_values(&clear_values),
                        vk::SubpassContents::INLINE,
                    );
                    vk_device.cmd_end_render_pass(command_buffer);
                }
                vk_device.end_command_buffer(command_buffer)?;
                let command_buffers = [command_buffer];
                let submit_infos = [vk::SubmitInfo::default().command_buffers(&command_buffers)];
                gpu_may_be_in_flight = true;
                vk_device.queue_submit(queue, &submit_infos, fence)?;
                vk_device.wait_for_fences(&[fence], true, u64::MAX)?;
                gpu_may_be_in_flight = false;
                swapchain.release_image()?;
                image_acquired = false;
                image_waited = false;

                let image_rect = xr::Rect2Di {
                    offset: xr::Offset2Di { x: 0, y: 0 },
                    extent: xr::Extent2Di {
                        width: width as i32,
                        height: height as i32,
                    },
                };
                let projection_views = [
                    xr::CompositionLayerProjectionView::new()
                        .pose(views[0].pose)
                        .fov(views[0].fov)
                        .sub_image(
                            xr::SwapchainSubImage::new()
                                .swapchain(&swapchain)
                                .image_array_index(0)
                                .image_rect(image_rect),
                        ),
                    xr::CompositionLayerProjectionView::new()
                        .pose(views[1].pose)
                        .fov(views[1].fov)
                        .sub_image(
                            xr::SwapchainSubImage::new()
                                .swapchain(&swapchain)
                                .image_array_index(1)
                                .image_rect(image_rect),
                        ),
                ];
                let projection_layer = xr::CompositionLayerProjection::new()
                    .space(&reference_space)
                    .views(&projection_views);
                frame_stream.end(
                    frame_state.predicted_display_time,
                    environment_blend_mode,
                    &[&projection_layer],
                )?;
                frame_ended = true;
                Ok(FrameOutcome::Rendered(view_state))
            })();

            let view_state = match frame_result {
                Ok(FrameOutcome::Rendered(view_state)) => {
                    render_wait_started = Instant::now();
                    view_state
                }
                Ok(FrameOutcome::Skipped) => {
                    if render_wait_started.elapsed() > SESSION_READY_TIMEOUT {
                        return Err("session produced no renderable frame for 120 seconds".into());
                    }
                    continue;
                }
                Err(error) => {
                    let gpu_idle = !gpu_may_be_in_flight || vk_device.device_wait_idle().is_ok();
                    if image_acquired && image_waited && gpu_idle {
                        let _ = swapchain.release_image();
                    }
                    if !frame_ended {
                        let _ = frame_stream.end(
                            frame_state.predicted_display_time,
                            environment_blend_mode,
                            &[],
                        );
                    }
                    return Err(error);
                }
            };

            rendered_frames += 1;
            if rendered_frames == 1 {
                println!(
                    "first stereo frame submitted; view_state={view_state:?}; left=red right=blue"
                );
            }
            if rendered_frames >= options.frame_count && !exit_requested {
                println!("submitted {rendered_frames} stereo frames; requesting clean exit");
                session.request_exit()?;
                exit_requested = true;
            }
        }

        drop(image_resources);
        drop(draw_resources);
        drop(swapchain);
        drop(reference_space);
        drop(frame_waiter);
        drop(frame_stream);
        drop(session);
        drop(vk_device);
        drop(vulkan_core);
    }

    println!("stereo-clear probe exited cleanly");
    Ok(())
}
