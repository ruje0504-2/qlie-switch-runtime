// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef __SWITCH__
#include <switch.h>
#define VK_USE_PLATFORM_VI_NN
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "vulkan_raster.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#ifndef __SWITCH__
#include <dlfcn.h>
#else
extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance,const char*);
#endif
namespace whj2 {
namespace {
#include "raster_spv.inc"
#ifdef __SWITCH__
#include "present_vert_spv.inc"
#include "present_frag_spv.inc"
#endif
void checked(VkResult r,const char*what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string("Vulkan ")+what+": "+std::to_string(r));}
struct Push {float p[4][4]{};int32_t rect[4]{};uint32_t meta[4]{},colors[4]{};};
static_assert(sizeof(Push)==112,"Shader push layout");
}
struct VulkanRaster::Impl {
#define INSTANCE_FUNCTIONS(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceProperties2) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) X(DestroyBuffer) X(FreeMemory) X(CreateDescriptorSetLayout) X(CreateDescriptorPool) X(AllocateDescriptorSets) X(FreeDescriptorSets) X(UpdateDescriptorSets) X(DestroyDescriptorPool) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(CreateShaderModule) X(CreateComputePipelines) X(DestroyShaderModule) X(DestroyPipeline) X(DestroyPipelineLayout) X(CreateCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool) X(BeginCommandBuffer) X(EndCommandBuffer) X(DestroyCommandPool) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) X(CmdPipelineBarrier) X(CreateFence) X(ResetFences) X(WaitForFences) X(DestroyFence) X(QueueSubmit) X(DeviceWaitIdle)
#define DECLARE(N) PFN_vk##N N=nullptr;
 INSTANCE_FUNCTIONS(DECLARE) DEVICE_FUNCTIONS(DECLARE)
#ifdef __SWITCH__
#define PRESENT_INSTANCE(X) X(CreateViSurfaceNN) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR)
#define PRESENT_DEVICE(X) X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) X(CreateSemaphore) X(DestroySemaphore) X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateGraphicsPipelines) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdDraw)
 PRESENT_INSTANCE(DECLARE) PRESENT_DEVICE(DECLARE)
 VkSurfaceKHR surface=VK_NULL_HANDLE;VkSwapchainKHR swapchain=VK_NULL_HANDLE;VkExtent2D extent{};VkFormat format=VK_FORMAT_UNDEFINED;
 VkRenderPass renderPass=VK_NULL_HANDLE;VkPipeline displayPipeline=VK_NULL_HANDLE;VkDescriptorSet displaySet=VK_NULL_HANDLE;VkSemaphore acquired=VK_NULL_HANDLE;
 std::vector<VkImage>swapImages;std::vector<VkImageView>swapViews;std::vector<VkFramebuffer>swapFrames;std::vector<VkSemaphore>finished;
#endif
#undef DECLARE
 VkInstance instance=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkQueue queue=VK_NULL_HANDLE;VkPhysicalDeviceMemoryProperties memory{};
 VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkDescriptorPool descriptorPool=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;VkCommandPool commandPool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
#ifndef __SWITCH__
 void* library=nullptr;
#endif
 struct Buffer {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void*data=nullptr;VkDeviceSize size=0;bool coherent=false;};
 struct Texture {Buffer storage;VkDescriptorSet set=VK_NULL_HANDLE;unsigned width=0,height=0,lastUsed=0;};
 Buffer output;std::map<const Image*,Texture>textures;std::vector<Texture>retired;unsigned draws=0,frames=0;bool recording=false;
 void release(Buffer&b){if(b.data)UnmapMemory(device,b.memory);if(b.buffer)DestroyBuffer(device,b.buffer,nullptr);if(b.memory)FreeMemory(device,b.memory,nullptr);b={};}
 Buffer buffer(size_t size){Buffer b;b.size=size;VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;checked(CreateBuffer(device,&info,nullptr,&b.buffer),"create buffer");
  VkMemoryRequirements req;GetBufferMemoryRequirements(device,b.buffer,&req);uint32_t type=UINT32_MAX;
  for(unsigned i=0;i<memory.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)){type=i;if(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)break;}
  if(type==UINT32_MAX)throw std::runtime_error("Vulkan requires CPU-visible memory");b.coherent=(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;checked(AllocateMemory(device,&allocation,nullptr,&b.memory),"allocate buffer");checked(BindBufferMemory(device,b.buffer,b.memory,0),"bind buffer");checked(MapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.data),"map buffer");return b;
 }
 void flush(const Buffer&b,bool invalidate=false){if(b.coherent)return;VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=b.memory;range.size=VK_WHOLE_SIZE;checked(invalidate?InvalidateMappedMemoryRanges(device,1,&range):FlushMappedMemoryRanges(device,1,&range),invalidate?"invalidate":"flush");}
 void initialize(){
  PFN_vkGetInstanceProcAddr get=nullptr;
#ifdef __SWITCH__
  get=vkGetInstanceProcAddr;
#else
  const char*path=std::getenv("WHJ2_VULKAN_LIBRARY");if(!path)path="/opt/homebrew/lib/libvulkan_kosmickrisp.dylib";
  library=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!library)throw std::runtime_error(std::string("Vulkan library: ")+dlerror());
  get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));if(!get)get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vk_icdGetInstanceProcAddr"));
#endif
  if(!get)throw std::runtime_error("Vulkan entry point unavailable");auto create=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));if(!create)throw std::runtime_error("Vulkan create unavailable");
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="WHJ2";app.apiVersion=VK_API_VERSION_1_2;VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
#ifdef __SWITCH__
  const char*extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_NN_VI_SURFACE_EXTENSION_NAME};info.enabledExtensionCount=2;info.ppEnabledExtensionNames=extensions;
#endif
  checked(create(&info,nullptr,&instance),"create instance");
#define LOAD_INSTANCE(N) N=reinterpret_cast<PFN_vk##N>(get(instance,"vk" #N));if(!N)throw std::runtime_error("Missing vk" #N);
  INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#ifdef __SWITCH__
  PRESENT_INSTANCE(LOAD_INSTANCE)
  VkViSurfaceCreateInfoNN surfaceInfo{VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN};surfaceInfo.window=nwindowGetDefault();checked(CreateViSurfaceNN(instance,&surfaceInfo,nullptr,&surface),"VI surface");
#endif
#undef LOAD_INSTANCE
  uint32_t count=0;checked(EnumeratePhysicalDevices(instance,&count,nullptr),"enumerate devices");if(!count)throw std::runtime_error("Vulkan has no GPU");std::vector<VkPhysicalDevice>devices(count);checked(EnumeratePhysicalDevices(instance,&count,devices.data()),"enumerate devices");
  VkPhysicalDevice physical=devices[0];VkPhysicalDeviceProperties properties{};GetPhysicalDeviceProperties(physical,&properties);GetPhysicalDeviceMemoryProperties(physical,&memory);
  uint32_t families=0;GetPhysicalDeviceQueueFamilyProperties(physical,&families,nullptr);std::vector<VkQueueFamilyProperties>queues(families);GetPhysicalDeviceQueueFamilyProperties(physical,&families,queues.data());uint32_t family=UINT32_MAX;for(unsigned i=0;i<families;i++)if(queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT){
#ifdef __SWITCH__
   VkBool32 supported=VK_FALSE;checked(GetPhysicalDeviceSurfaceSupportKHR(physical,i,surface,&supported),"surface support");if(!supported||!(queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT))continue;
#endif
   family=i;break;}if(family==UINT32_MAX)throw std::runtime_error("Vulkan has no compute queue");
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
#ifdef __SWITCH__
  const char*deviceExtensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};di.enabledExtensionCount=1;di.ppEnabledExtensionNames=deviceExtensions;
  VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&driver;GetPhysicalDeviceProperties2(physical,&props);std::cerr<<"VULKAN driver="<<driver.driverName<<" info="<<driver.driverInfo<<" id="<<driver.driverID<<"\n";if(driver.driverID!=VK_DRIVER_ID_MESA_NVK)throw std::runtime_error("Switch requires Mesa NVK");
#endif
  checked(CreateDevice(physical,&di,nullptr,&device),"create device");
#define LOAD_DEVICE(N) N=reinterpret_cast<PFN_vk##N>(GetDeviceProcAddr(device,"vk" #N));if(!N)throw std::runtime_error("Missing vk" #N);
  DEVICE_FUNCTIONS(LOAD_DEVICE)
#ifdef __SWITCH__
  PRESENT_DEVICE(LOAD_DEVICE)
#endif
#undef LOAD_DEVICE
  GetDeviceQueue(device,family,0,&queue);std::cerr<<"VULKAN device="<<properties.deviceName<<" api="<<VK_VERSION_MAJOR(properties.apiVersion)<<'.'<<VK_VERSION_MINOR(properties.apiVersion)<<" compute-family="<<family<<"\n";
  VkDescriptorSetLayoutBinding bindings[2]{};for(unsigned i=0;i<2;i++){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;}
  VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=2;si.pBindings=bindings;checked(CreateDescriptorSetLayout(device,&si,nullptr,&setLayout),"descriptor layout");VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8192};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;pi.maxSets=4096;pi.poolSizeCount=1;pi.pPoolSizes=&poolSize;checked(CreateDescriptorPool(device,&pi,nullptr,&descriptorPool),"descriptor pool");
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Push)};VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&setLayout;li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;checked(CreatePipelineLayout(device,&li,nullptr,&layout),"pipeline layout");VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof(rasterSpv);sm.pCode=rasterSpv;VkShaderModule shader;checked(CreateShaderModule(device,&sm,nullptr,&shader),"shader");
  VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=layout;cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=shader;cp.stage.pName="main";auto result=CreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline);DestroyShaderModule(device,shader,nullptr);checked(result,"compute pipeline");
  VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=family;checked(CreateCommandPool(device,&ci,nullptr,&commandPool),"command pool");VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=commandPool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;checked(AllocateCommandBuffers(device,&ai,&command),"command buffer");VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checked(CreateFence(device,&fi,nullptr,&fence),"fence");output=buffer(1280*720*4);
#ifdef __SWITCH__
  createPresentation(physical);
#endif
 }

#ifdef __SWITCH__
 // Native surface, per-swapchain-image finished semaphores and acquire/submit/
 // present ownership follow biko3-switch-runtime's 0BSD renderer at bd70f2c.
 void createPresentation(VkPhysicalDevice physical){
  VkSurfaceCapabilitiesKHR caps;checked(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"surface capabilities");uint32_t count=0;checked(GetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"surface formats");std::vector<VkSurfaceFormatKHR>formats(count);checked(GetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"surface formats");auto chosen=std::find_if(formats.begin(),formats.end(),[](auto f){return f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM;});if(chosen==formats.end())throw std::runtime_error("No UNORM display format");format=chosen->format;
  extent=caps.currentExtent;if(extent.width==UINT32_MAX)extent={std::clamp(1280u,caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(720u,caps.minImageExtent.height,caps.maxImageExtent.height)};
  if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)||!(caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))throw std::runtime_error("Unsupported display surface");
  count=std::max(3u,caps.minImageCount);if(caps.maxImageCount)count=std::min(count,caps.maxImageCount);VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=surface;ci.minImageCount=count;ci.imageFormat=format;ci.imageColorSpace=chosen->colorSpace;ci.imageExtent=extent;ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;ci.preTransform=caps.currentTransform;ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;ci.presentMode=VK_PRESENT_MODE_FIFO_KHR;ci.clipped=VK_TRUE;checked(CreateSwapchainKHR(device,&ci,nullptr,&swapchain),"swapchain");checked(GetSwapchainImagesKHR(device,swapchain,&count,nullptr),"swapchain images");swapImages.resize(count);checked(GetSwapchainImagesKHR(device,swapchain,&count,swapImages.data()),"swapchain images");swapViews.resize(count);swapFrames.resize(count);finished.resize(count);
  VkAttachmentDescription attachment{};attachment.format=format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;VkSubpassDependency dependency{};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=1;rp.pDependencies=&dependency;checked(CreateRenderPass(device,&rp,nullptr,&renderPass),"display pass");VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};checked(CreateSemaphore(device,&semaphore,nullptr,&acquired),"acquire semaphore");
  for(unsigned i=0;i<count;i++){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=swapImages[i];vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(CreateImageView(device,&vi,nullptr,&swapViews[i]),"display view");VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=renderPass;fb.attachmentCount=1;fb.pAttachments=&swapViews[i];fb.width=extent.width;fb.height=extent.height;fb.layers=1;checked(CreateFramebuffer(device,&fb,nullptr,&swapFrames[i]),"display frame");checked(CreateSemaphore(device,&semaphore,nullptr,&finished[i]),"present semaphore");}
  auto shader=[&](const uint32_t*code,size_t size){VkShaderModule module;VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=size;info.pCode=code;checked(CreateShaderModule(device,&info,nullptr,&module),"display shader");return module;};VkShaderModule vert=shader(presentVertexSpv,sizeof(presentVertexSpv)),frag=shader(presentFragmentSpv,sizeof(presentFragmentSpv));VkPipelineShaderStageCreateInfo stages[2]{};for(auto&stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vert;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=frag;
  VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};input.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;VkViewport viewport{0,0,float(extent.width),float(extent.height),0,1};VkRect2D scissor{{0,0},extent};VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};view.viewportCount=1;view.pViewports=&viewport;view.scissorCount=1;view.pScissors=&scissor;VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo color{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};color.attachmentCount=1;color.pAttachments=&blend;
  VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipelineInfo.stageCount=2;pipelineInfo.pStages=stages;pipelineInfo.pVertexInputState=&vertex;pipelineInfo.pInputAssemblyState=&input;pipelineInfo.pViewportState=&view;pipelineInfo.pRasterizationState=&raster;pipelineInfo.pMultisampleState=&samples;pipelineInfo.pColorBlendState=&color;pipelineInfo.layout=layout;pipelineInfo.renderPass=renderPass;auto result=CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&displayPipeline);DestroyShaderModule(device,vert,nullptr);DestroyShaderModule(device,frag,nullptr);checked(result,"display pipeline");
  VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=descriptorPool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&setLayout;checked(AllocateDescriptorSets(device,&allocation,&displaySet),"display descriptor");VkDescriptorBufferInfo bi{output.buffer,0,output.size};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=displaySet;write.dstBinding=1;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bi;UpdateDescriptorSets(device,1,&write,0,nullptr);std::cerr<<"VULKAN native display="<<extent.width<<'x'<<extent.height<<" images="<<count<<"\n";
 }
 void presentNative(){
  if(!recording)throw std::runtime_error("Vulkan frame not begun");uint32_t index=0;auto acquire=AcquireNextImageKHR(device,swapchain,5000000000ull,acquired,VK_NULL_HANDLE,&index);if(acquire!=VK_SUBOPTIMAL_KHR)checked(acquire,"acquire image");
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=renderPass;begin.framebuffer=swapFrames[index];begin.renderArea={{0,0},extent};CmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,displayPipeline);CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&displaySet,0,nullptr);Push push;push.meta[0]=extent.width;push.meta[1]=extent.height;CmdPushConstants(command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);CmdDraw(command,3,1,0,0);CmdEndRenderPass(command);checked(EndCommandBuffer(command),"end display");checked(ResetFences(device,1,&fence),"reset display fence");VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&wait;submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&finished[index];checked(QueueSubmit(queue,1,&submit,fence),"submit display");VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.waitSemaphoreCount=1;info.pWaitSemaphores=&finished[index];info.swapchainCount=1;info.pSwapchains=&swapchain;info.pImageIndices=&index;auto result=QueuePresentKHR(queue,&info);if(result!=VK_SUBOPTIMAL_KHR)checked(result,"present");checked(WaitForFences(device,1,&fence,VK_TRUE,5000000000ull),"display completion");recording=false;completeFrame();
 }
 void destroyPresentation(){if(displayPipeline)DestroyPipeline(device,displayPipeline,nullptr);for(auto fb:swapFrames)if(fb)DestroyFramebuffer(device,fb,nullptr);for(auto view:swapViews)if(view)DestroyImageView(device,view,nullptr);for(auto sem:finished)if(sem)DestroySemaphore(device,sem,nullptr);if(acquired)DestroySemaphore(device,acquired,nullptr);if(renderPass)DestroyRenderPass(device,renderPass,nullptr);if(swapchain)DestroySwapchainKHR(device,swapchain,nullptr);}
#endif
 void completeFrame(){++frames;for(auto it=textures.begin();it!=textures.end();){if(it->second.lastUsed!=frames){FreeDescriptorSets(device,descriptorPool,1,&it->second.set);release(it->second.storage);it=textures.erase(it);}else ++it;}if(frames==1||frames%600==0)std::cerr<<"VULKAN FRAME "<<frames<<" dispatches="<<draws<<" textures="<<textures.size()<<"\n";}

 ~Impl(){if(device&&DeviceWaitIdle){DeviceWaitIdle(device);
#ifdef __SWITCH__
 destroyPresentation();
#endif
 for(auto&it:textures)release(it.second.storage);for(auto&t:retired)release(t.storage);release(output);if(fence)DestroyFence(device,fence,nullptr);if(commandPool)DestroyCommandPool(device,commandPool,nullptr);if(pipeline)DestroyPipeline(device,pipeline,nullptr);if(layout)DestroyPipelineLayout(device,layout,nullptr);if(descriptorPool)DestroyDescriptorPool(device,descriptorPool,nullptr);if(setLayout)DestroyDescriptorSetLayout(device,setLayout,nullptr);DestroyDevice(device,nullptr);}
#ifdef __SWITCH__
 if(surface&&DestroySurfaceKHR)DestroySurfaceKHR(instance,surface,nullptr);
#endif
 if(instance&&DestroyInstance)DestroyInstance(instance,nullptr);
#ifndef __SWITCH__
  if(library)dlclose(library);
#endif
 }
 VkDescriptorSet texture(const Image&i){auto at=textures.find(&i);if(at!=textures.end()&&(at->second.width!=i.width||at->second.height!=i.height||std::memcmp(at->second.storage.data,i.pixels.data(),i.pixels.size())!=0)){retired.push_back(at->second);textures.erase(at);at=textures.end();}
  if(at==textures.end()){Texture t;t.width=i.width;t.height=i.height;t.storage=buffer(i.pixels.size());std::memcpy(t.storage.data,i.pixels.data(),i.pixels.size());flush(t.storage);VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=descriptorPool;ai.descriptorSetCount=1;ai.pSetLayouts=&setLayout;checked(AllocateDescriptorSets(device,&ai,&t.set),"allocate descriptor");VkDescriptorBufferInfo bi[2]={{t.storage.buffer,0,t.storage.size},{output.buffer,0,output.size}};VkWriteDescriptorSet writes[2]{};for(unsigned n=0;n<2;n++){writes[n].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[n].dstSet=t.set;writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=&bi[n];}UpdateDescriptorSets(device,2,writes,0,nullptr);at=textures.emplace(&i,t).first;}at->second.lastUsed=frames+1;return at->second.set;
 }
 void barrier(VkPipelineStageFlags source,VkPipelineStageFlags dest,VkAccessFlags from,VkAccessFlags to){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=from;b.dstAccessMask=to;CmdPipelineBarrier(command,source,dest,0,1,&b,0,nullptr,0,nullptr);}
 void dispatch(const Image&i,Push&p,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){if(!recording)throw std::runtime_error("Vulkan frame not begun");if(!i.width||!i.height||alpha<=0)return;if(blend>8)throw std::runtime_error("Vulkan unsupported blend");p.rect[0]=std::max(0,p.rect[0]);p.rect[1]=std::max(0,p.rect[1]);p.rect[2]=std::min(1280,p.rect[2]);p.rect[3]=std::min(720,p.rect[3]);if(p.rect[2]<=p.rect[0]||p.rect[3]<=p.rect[1])return;
  p.meta[0]=i.width;p.meta[1]=i.height;p.meta[2]|=(blend<<8)|filter;p.meta[3]=unsigned(std::clamp(alpha,0.f,255.f));p.colors[0]=add;p.colors[1]=mul;p.colors[2]=1280;p.colors[3]=720;auto set=texture(i);CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);CmdDispatch(command,unsigned(p.rect[2]-p.rect[0]+15)/16,unsigned(p.rect[3]-p.rect[1]+15)/16,1);barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);draws++;
 }
};
VulkanRaster::VulkanRaster():impl(std::make_unique<Impl>()){impl->initialize();}
VulkanRaster::~VulkanRaster()=default;
bool VulkanRaster::nativePresentation()const{
#ifdef __SWITCH__
 return true;
#else
 return false;
#endif
}
void VulkanRaster::present(){
#ifdef __SWITCH__
 impl->presentNative();
#else
 throw std::runtime_error("Native presentation requires Horizon");
#endif
}
void VulkanRaster::presentPixels(const qlie::Bytes&pixels){begin(pixels);present();}
void VulkanRaster::begin(const qlie::Bytes&pixels){auto&s=*impl;if(pixels.size()!=s.output.size||s.recording)throw std::runtime_error("Vulkan begin frame");for(auto&t:s.retired){s.FreeDescriptorSets(s.device,s.descriptorPool,1,&t.set);s.release(t.storage);}s.retired.clear();std::memcpy(s.output.data,pixels.data(),pixels.size());s.flush(s.output);checked(s.ResetCommandPool(s.device,s.commandPool,0),"reset commands");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checked(s.BeginCommandBuffer(s.command,&bi),"begin commands");s.barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);s.CmdBindPipeline(s.command,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipeline);s.recording=true;s.draws=0;}
void VulkanRaster::end(qlie::Bytes&pixels){auto&s=*impl;if(!s.recording)throw std::runtime_error("Vulkan frame not begun");s.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);checked(s.EndCommandBuffer(s.command),"end commands");checked(s.ResetFences(s.device,1,&s.fence),"reset fence");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&s.command;checked(s.QueueSubmit(s.queue,1,&submit,s.fence),"submit");checked(s.WaitForFences(s.device,1,&s.fence,VK_TRUE,5000000000ull),"wait");s.flush(s.output,true);std::memcpy(pixels.data(),s.output.data,pixels.size());s.recording=false;s.completeFrame();}
void VulkanRaster::image(const Image&i,const Affine2D&m,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){float det=m.a*m.d-m.b*m.c;if(std::abs(det)<1e-12f)return;Push p;p.p[0][0]=m.a;p.p[0][1]=m.b;p.p[0][2]=m.c;p.p[0][3]=m.d;p.p[1][0]=m.x;p.p[1][1]=m.y;p.p[1][2]=det;float xmin=m.x,xmax=m.x,ymin=m.y,ymax=m.y;for(unsigned n=0;n<4;n++){float u=n&1?i.width:0,v=n&2?i.height:0;float x=m.a*u+m.b*v+m.x,y=m.c*u+m.d*v+m.y;xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);}p.rect[0]=int(std::floor(xmin));p.rect[1]=int(std::floor(ymin));p.rect[2]=int(std::ceil(xmax));p.rect[3]=int(std::ceil(ymax));impl->dispatch(i,p,alpha,add,mul,blend,filter);}
void VulkanRaster::triangle(const Image&i,const std::array<TextureVertex,3>&vertices,const Affine2D&post,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){auto v=vertices;for(auto&p:v){if(p.depth<10)return;float ratio=1000/p.depth;float x=640+(p.x-640)*ratio,y=360+(p.y-360)*ratio;p.x=post.a*x+post.b*y+post.x;p.y=post.c*x+post.d*y+post.y;}auto edge=[](const TextureVertex&a,const TextureVertex&b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};float area=edge(v[0],v[1],v[2].x,v[2].y);if(std::abs(area)<1e-6f)return;if(area<0){std::swap(v[1],v[2]);area=-area;}Push p;for(unsigned n=0;n<3;n++){p.p[n][0]=v[n].x;p.p[n][1]=v[n].y;p.p[n][2]=v[n].u;p.p[n][3]=v[n].v;p.p[3][n]=v[n].depth;}p.p[3][3]=area;p.meta[2]=65536;p.rect[0]=int(std::floor(std::min({v[0].x,v[1].x,v[2].x})));p.rect[1]=int(std::floor(std::min({v[0].y,v[1].y,v[2].y})));p.rect[2]=int(std::ceil(std::max({v[0].x,v[1].x,v[2].x})));p.rect[3]=int(std::ceil(std::max({v[0].y,v[1].y,v[2].y})));impl->dispatch(i,p,alpha,add,mul,blend,filter);}
}
