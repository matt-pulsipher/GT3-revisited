#pragma once

// SDL_GPU on OpenGL ES 3.0: the renderer's fallback for GPUs SDL_GPU has no backend for (Windows
// and Linux PCs without Vulkan, e.g. GeForce 400/500 or Intel HD 2000-4000 laptops; on Windows
// through ANGLE, OpenGL ES on Direct3D 11, when its DLLs sit next to the game). See
// docs/dev/RENDERING.md "OpenGL ES".
//
// The renderer keeps calling SDL_GPU by its usual names; in builds with this backend (GT3R_GLES)
// the macros below route those calls to gpu_gles.cpp, which hands them on to SDL unless the
// OpenGL ES device is the one in use (gles::active()). It implements the subset of SDL_GPU this
// project uses on the OpenGL ES 3.0 context of SDL's GLES2 2D renderer (so 3D views composite
// into the UI as on SDL_GPU). Shaders are GLSL ES 3.00 generated from the same sources
// (shaders/compile_shaders.py), passed as SDL_GPU_SHADERFORMAT_PRIVATE.
//
// Conventions match SDL_GPU's: render targets store the image top row first and the
// clip-space depth range is [0, 1] (the generated vertex shaders flip y and remap z for
// GL; front faces are flipped to match).

#include <SDL3/SDL.h>

#if GT3R_GLES

// Shader properties for SDL_GPU_SHADERFORMAT_PRIVATE (GLSL ES 3.00 source in `code`): the
// uniform block names in slot order and the sampler uniform names in slot order, comma
// separated.
#define GT3R_GL_PROP_SHADER_UNIFORM_BLOCKS "gt3r.gl.uniform_blocks"
#define GT3R_GL_PROP_SHADER_SAMPLERS "gt3r.gl.samplers"

namespace gt3r::render::gles {

// The OpenGL ES device (its shader format: SDL_GPU_SHADERFORMAT_PRIVATE): every routed call
// goes to it until it is destroyed. SDL_CreateGPURenderer then creates the context (debug: with
// GL error checks; angle: on Windows, through ANGLE when its DLLs are next to the game, not the
// graphics driver's own OpenGL ES).
SDL_GPUDevice* create_device(bool debug, bool angle);
// True while the OpenGL ES device is the one in use.
bool active();
// True when the device in use asked for ANGLE (create_device).
bool angle();
// GL_VERSION and GL_RENDERER of the context, e.g. "OpenGL ES 3.0.0 (ANGLE ...); ANGLE (NVIDIA
// GeForce GT 520M Direct3D11 ...)"; empty before SDL_CreateGPURenderer.
const char* description();
// A --probe-3d run (app/gles_probe.h): opens the device on a hidden window as a start with it
// would, through ANGLE (angle) or the graphics driver's own OpenGL ES. False also when SDL turned
// to ANGLE for the driver's (a driver without OpenGL ES 3.0 contexts).
bool probe(bool angle);

} // namespace gt3r::render::gles

void gt3r_gl_DestroyGPUDevice(SDL_GPUDevice* device);
const char* gt3r_gl_GetGPUDeviceDriver(SDL_GPUDevice* device);
SDL_GPUShaderFormat gt3r_gl_GetGPUShaderFormats(SDL_GPUDevice* device);
SDL_Renderer* gt3r_gl_CreateGPURenderer(SDL_GPUDevice* device, SDL_Window* window);
bool gt3r_gl_GPUTextureSupportsFormat(SDL_GPUDevice* device, SDL_GPUTextureFormat format, SDL_GPUTextureType type,
                                      SDL_GPUTextureUsageFlags usage);
bool gt3r_gl_GPUTextureSupportsSampleCount(SDL_GPUDevice* device, SDL_GPUTextureFormat format,
                                           SDL_GPUSampleCount sample_count);
SDL_GPUShader* gt3r_gl_CreateGPUShader(SDL_GPUDevice* device, const SDL_GPUShaderCreateInfo* createinfo);
void gt3r_gl_ReleaseGPUShader(SDL_GPUDevice* device, SDL_GPUShader* shader);
SDL_GPUGraphicsPipeline* gt3r_gl_CreateGPUGraphicsPipeline(SDL_GPUDevice* device,
                                                           const SDL_GPUGraphicsPipelineCreateInfo* createinfo);
void gt3r_gl_ReleaseGPUGraphicsPipeline(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* pipeline);
SDL_GPUSampler* gt3r_gl_CreateGPUSampler(SDL_GPUDevice* device, const SDL_GPUSamplerCreateInfo* createinfo);
void gt3r_gl_ReleaseGPUSampler(SDL_GPUDevice* device, SDL_GPUSampler* sampler);
SDL_GPUTexture* gt3r_gl_CreateGPUTexture(SDL_GPUDevice* device, const SDL_GPUTextureCreateInfo* createinfo);
void gt3r_gl_ReleaseGPUTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture);
SDL_GPUBuffer* gt3r_gl_CreateGPUBuffer(SDL_GPUDevice* device, const SDL_GPUBufferCreateInfo* createinfo);
void gt3r_gl_ReleaseGPUBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer);
SDL_GPUTransferBuffer* gt3r_gl_CreateGPUTransferBuffer(SDL_GPUDevice* device,
                                                       const SDL_GPUTransferBufferCreateInfo* createinfo);
void gt3r_gl_ReleaseGPUTransferBuffer(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer_buffer);
void* gt3r_gl_MapGPUTransferBuffer(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer_buffer, bool cycle);
void gt3r_gl_UnmapGPUTransferBuffer(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer_buffer);
SDL_GPUCommandBuffer* gt3r_gl_AcquireGPUCommandBuffer(SDL_GPUDevice* device);
bool gt3r_gl_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer* command_buffer);
SDL_GPUFence* gt3r_gl_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer* command_buffer);
bool gt3r_gl_WaitForGPUFences(SDL_GPUDevice* device, bool wait_all, SDL_GPUFence* const* fences, Uint32 num_fences);
void gt3r_gl_ReleaseGPUFence(SDL_GPUDevice* device, SDL_GPUFence* fence);
bool gt3r_gl_WaitForGPUIdle(SDL_GPUDevice* device);
SDL_GPUCopyPass* gt3r_gl_BeginGPUCopyPass(SDL_GPUCommandBuffer* command_buffer);
void gt3r_gl_EndGPUCopyPass(SDL_GPUCopyPass* copy_pass);
void gt3r_gl_UploadToGPUBuffer(SDL_GPUCopyPass* copy_pass, const SDL_GPUTransferBufferLocation* source,
                               const SDL_GPUBufferRegion* destination, bool cycle);
void gt3r_gl_UploadToGPUTexture(SDL_GPUCopyPass* copy_pass, const SDL_GPUTextureTransferInfo* source,
                                const SDL_GPUTextureRegion* destination, bool cycle);
void gt3r_gl_DownloadFromGPUTexture(SDL_GPUCopyPass* copy_pass, const SDL_GPUTextureRegion* source,
                                    const SDL_GPUTextureTransferInfo* destination);
void gt3r_gl_CopyGPUTextureToTexture(SDL_GPUCopyPass* copy_pass, const SDL_GPUTextureLocation* source,
                                     const SDL_GPUTextureLocation* destination, Uint32 w, Uint32 h, Uint32 d,
                                     bool cycle);
void gt3r_gl_GenerateMipmapsForGPUTexture(SDL_GPUCommandBuffer* command_buffer, SDL_GPUTexture* texture);
void gt3r_gl_BlitGPUTexture(SDL_GPUCommandBuffer* command_buffer, const SDL_GPUBlitInfo* info);
SDL_GPURenderPass* gt3r_gl_BeginGPURenderPass(SDL_GPUCommandBuffer* command_buffer,
                                              const SDL_GPUColorTargetInfo* color_target_infos,
                                              Uint32 num_color_targets,
                                              const SDL_GPUDepthStencilTargetInfo* depth_stencil_target_info);
void gt3r_gl_EndGPURenderPass(SDL_GPURenderPass* render_pass);
void gt3r_gl_BindGPUGraphicsPipeline(SDL_GPURenderPass* render_pass, SDL_GPUGraphicsPipeline* graphics_pipeline);
void gt3r_gl_BindGPUVertexBuffers(SDL_GPURenderPass* render_pass, Uint32 first_slot,
                                  const SDL_GPUBufferBinding* bindings, Uint32 num_bindings);
void gt3r_gl_BindGPUIndexBuffer(SDL_GPURenderPass* render_pass, const SDL_GPUBufferBinding* binding,
                                SDL_GPUIndexElementSize index_element_size);
void gt3r_gl_BindGPUFragmentSamplers(SDL_GPURenderPass* render_pass, Uint32 first_slot,
                                     const SDL_GPUTextureSamplerBinding* texture_sampler_bindings,
                                     Uint32 num_bindings);
void gt3r_gl_PushGPUVertexUniformData(SDL_GPUCommandBuffer* command_buffer, Uint32 slot_index, const void* data,
                                      Uint32 length);
void gt3r_gl_PushGPUFragmentUniformData(SDL_GPUCommandBuffer* command_buffer, Uint32 slot_index, const void* data,
                                        Uint32 length);
void gt3r_gl_SetGPUViewport(SDL_GPURenderPass* render_pass, const SDL_GPUViewport* viewport);
void gt3r_gl_SetGPUScissor(SDL_GPURenderPass* render_pass, const SDL_Rect* scissor);
void gt3r_gl_DrawGPUIndexedPrimitives(SDL_GPURenderPass* render_pass, Uint32 num_indices, Uint32 num_instances,
                                      Uint32 first_index, Sint32 vertex_offset, Uint32 first_instance);

// The GL name of a texture (for SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER).
unsigned gt3r_gl_texture_name(SDL_GPUTexture* texture);
// Sets the GL attributes of the OpenGL ES 3.0 context (SDL_GL_SetAttribute) and returns the
// window flag it needs (SDL_WINDOW_OPENGL).
SDL_WindowFlags gt3r_gl_window_flags();

#ifndef GT3R_GLES_IMPL // gpu_gles.cpp calls SDL's own
#define SDL_DestroyGPUDevice gt3r_gl_DestroyGPUDevice
#define SDL_GetGPUDeviceDriver gt3r_gl_GetGPUDeviceDriver
#define SDL_GetGPUShaderFormats gt3r_gl_GetGPUShaderFormats
#define SDL_CreateGPURenderer gt3r_gl_CreateGPURenderer
#define SDL_GPUTextureSupportsFormat gt3r_gl_GPUTextureSupportsFormat
#define SDL_GPUTextureSupportsSampleCount gt3r_gl_GPUTextureSupportsSampleCount
#define SDL_CreateGPUShader gt3r_gl_CreateGPUShader
#define SDL_ReleaseGPUShader gt3r_gl_ReleaseGPUShader
#define SDL_CreateGPUGraphicsPipeline gt3r_gl_CreateGPUGraphicsPipeline
#define SDL_ReleaseGPUGraphicsPipeline gt3r_gl_ReleaseGPUGraphicsPipeline
#define SDL_CreateGPUSampler gt3r_gl_CreateGPUSampler
#define SDL_ReleaseGPUSampler gt3r_gl_ReleaseGPUSampler
#define SDL_CreateGPUTexture gt3r_gl_CreateGPUTexture
#define SDL_ReleaseGPUTexture gt3r_gl_ReleaseGPUTexture
#define SDL_CreateGPUBuffer gt3r_gl_CreateGPUBuffer
#define SDL_ReleaseGPUBuffer gt3r_gl_ReleaseGPUBuffer
#define SDL_CreateGPUTransferBuffer gt3r_gl_CreateGPUTransferBuffer
#define SDL_ReleaseGPUTransferBuffer gt3r_gl_ReleaseGPUTransferBuffer
#define SDL_MapGPUTransferBuffer gt3r_gl_MapGPUTransferBuffer
#define SDL_UnmapGPUTransferBuffer gt3r_gl_UnmapGPUTransferBuffer
#define SDL_AcquireGPUCommandBuffer gt3r_gl_AcquireGPUCommandBuffer
#define SDL_SubmitGPUCommandBuffer gt3r_gl_SubmitGPUCommandBuffer
#define SDL_SubmitGPUCommandBufferAndAcquireFence gt3r_gl_SubmitGPUCommandBufferAndAcquireFence
#define SDL_WaitForGPUFences gt3r_gl_WaitForGPUFences
#define SDL_ReleaseGPUFence gt3r_gl_ReleaseGPUFence
#define SDL_WaitForGPUIdle gt3r_gl_WaitForGPUIdle
#define SDL_BeginGPUCopyPass gt3r_gl_BeginGPUCopyPass
#define SDL_EndGPUCopyPass gt3r_gl_EndGPUCopyPass
#define SDL_UploadToGPUBuffer gt3r_gl_UploadToGPUBuffer
#define SDL_UploadToGPUTexture gt3r_gl_UploadToGPUTexture
#define SDL_DownloadFromGPUTexture gt3r_gl_DownloadFromGPUTexture
#define SDL_CopyGPUTextureToTexture gt3r_gl_CopyGPUTextureToTexture
#define SDL_GenerateMipmapsForGPUTexture gt3r_gl_GenerateMipmapsForGPUTexture
#define SDL_BlitGPUTexture gt3r_gl_BlitGPUTexture
#define SDL_BeginGPURenderPass gt3r_gl_BeginGPURenderPass
#define SDL_EndGPURenderPass gt3r_gl_EndGPURenderPass
#define SDL_BindGPUGraphicsPipeline gt3r_gl_BindGPUGraphicsPipeline
#define SDL_BindGPUVertexBuffers gt3r_gl_BindGPUVertexBuffers
#define SDL_BindGPUIndexBuffer gt3r_gl_BindGPUIndexBuffer
#define SDL_BindGPUFragmentSamplers gt3r_gl_BindGPUFragmentSamplers
#define SDL_PushGPUVertexUniformData gt3r_gl_PushGPUVertexUniformData
#define SDL_PushGPUFragmentUniformData gt3r_gl_PushGPUFragmentUniformData
#define SDL_SetGPUViewport gt3r_gl_SetGPUViewport
#define SDL_SetGPUScissor gt3r_gl_SetGPUScissor
#define SDL_DrawGPUIndexedPrimitives gt3r_gl_DrawGPUIndexedPrimitives
#endif

#endif // GT3R_GLES
