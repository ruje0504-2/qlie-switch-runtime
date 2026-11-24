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
#include <limits>
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
struct DrawData {Push image;float maskSource[4]{},maskRect[4]{};uint32_t maskMeta[4]{};};
static_assert(sizeof(DrawData)==160,"Shader draw layout");
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
 struct Span {size_t offset=0,size=0;};
 struct Texture {Span storage;unsigned width=0,height=0;uint64_t lastUsed=0,checkedBatch=0;};
 static constexpr unsigned Tile=16,Width=1280,Height=720,Columns=Width/Tile,Rows=Height/Tile,TileCount=Columns*Rows;
 Buffer output,texels,parameters,tileBuffer;VkDescriptorSet rasterSet=VK_NULL_HANDLE;
 std::map<const Image*,Texture>textures;std::vector<Span>retired;std::map<size_t,size_t>freeSpans;size_t textureEnd=0,maxBufferBytes=0;
 std::vector<DrawData>drawCommands;std::array<std::vector<uint32_t>,TileCount>tileDraws;
 Image transitionLut;double lutAmount=-1;
 uint64_t frames=0,batch=0;unsigned draws=0,dispatches=0,textureChecks=0;size_t uploadBytes=0;bool recording=false,inBatch=false,clearOutput=false;uint32_t clearColor=0;
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
  maxBufferBytes=properties.limits.maxStorageBufferRange;
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
  VkDescriptorSetLayoutBinding bindings[4]{};for(unsigned i=0;i<4;i++){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT|(i==1?VK_SHADER_STAGE_FRAGMENT_BIT:0);}
  VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=4;si.pBindings=bindings;checked(CreateDescriptorSetLayout(device,&si,nullptr,&setLayout),"descriptor layout");VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=2;pi.poolSizeCount=1;pi.pPoolSizes=&poolSize;checked(CreateDescriptorPool(device,&pi,nullptr,&descriptorPool),"descriptor pool");
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Push)};VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&setLayout;li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;checked(CreatePipelineLayout(device,&li,nullptr,&layout),"pipeline layout");VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof(rasterSpv);sm.pCode=rasterSpv;VkShaderModule shader;checked(CreateShaderModule(device,&sm,nullptr,&shader),"shader");
  VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=layout;cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=shader;cp.stage.pName="main";auto result=CreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline);DestroyShaderModule(device,shader,nullptr);checked(result,"compute pipeline");
  VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=family;checked(CreateCommandPool(device,&ci,nullptr,&commandPool),"command pool");VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=commandPool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;checked(AllocateCommandBuffers(device,&ai,&command),"command buffer");VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checked(CreateFence(device,&fi,nullptr,&fence),"fence");output=buffer(1280*720*4);
  VkDescriptorSetAllocateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};descriptors.descriptorPool=descriptorPool;descriptors.descriptorSetCount=1;descriptors.pSetLayouts=&setLayout;checked(AllocateDescriptorSets(device,&descriptors,&rasterSet),"raster descriptor");
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
  if(!recording)throw std::runtime_error("Vulkan frame not begun");recordRaster();uint32_t index=0;auto acquire=AcquireNextImageKHR(device,swapchain,5000000000ull,acquired,VK_NULL_HANDLE,&index);if(acquire!=VK_SUBOPTIMAL_KHR)checked(acquire,"acquire image");
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=renderPass;begin.framebuffer=swapFrames[index];begin.renderArea={{0,0},extent};CmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,displayPipeline);CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&displaySet,0,nullptr);Push push;push.meta[0]=extent.width;push.meta[1]=extent.height;CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);CmdDraw(command,3,1,0,0);CmdEndRenderPass(command);checked(EndCommandBuffer(command),"end display");checked(ResetFences(device,1,&fence),"reset display fence");VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&wait;submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&finished[index];checked(QueueSubmit(queue,1,&submit,fence),"submit display");VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.waitSemaphoreCount=1;info.pWaitSemaphores=&finished[index];info.swapchainCount=1;info.pSwapchains=&swapchain;info.pImageIndices=&index;auto result=QueuePresentKHR(queue,&info);if(result!=VK_SUBOPTIMAL_KHR)checked(result,"present");checked(WaitForFences(device,1,&fence,VK_TRUE,5000000000ull),"display completion");recording=false;completeFrame();
 }
 void destroyPresentation(){if(displayPipeline)DestroyPipeline(device,displayPipeline,nullptr);for(auto fb:swapFrames)if(fb)DestroyFramebuffer(device,fb,nullptr);for(auto view:swapViews)if(view)DestroyImageView(device,view,nullptr);for(auto sem:finished)if(sem)DestroySemaphore(device,sem,nullptr);if(acquired)DestroySemaphore(device,acquired,nullptr);if(renderPass)DestroyRenderPass(device,renderPass,nullptr);if(swapchain)DestroySwapchainKHR(device,swapchain,nullptr);}
#endif
 void freeSpan(Span span){
  auto next=freeSpans.lower_bound(span.offset);
  if(next!=freeSpans.begin()){auto previous=std::prev(next);if(previous->first+previous->second==span.offset){span.offset=previous->first;span.size+=previous->second;freeSpans.erase(previous);}}
  if(next!=freeSpans.end()&&span.offset+span.size==next->first){span.size+=next->second;freeSpans.erase(next);}
  freeSpans.emplace(span.offset,span.size);
 }
 void completeFrame(){
  ++frames;
  for(auto span:retired)freeSpan(span);retired.clear();
  for(auto it=textures.begin();it!=textures.end();){if(it->second.lastUsed!=frames){freeSpan(it->second.storage);it=textures.erase(it);}else ++it;}
  if(frames==1||frames%600==0)std::cerr<<"VULKAN FRAME "<<frames<<" draws="<<draws<<" dispatches="<<dispatches<<" textures="<<textures.size()<<" texture_checks="<<textureChecks<<" upload_bytes="<<uploadBytes<<"\n";
 }

 ~Impl(){if(device&&DeviceWaitIdle){DeviceWaitIdle(device);
#ifdef __SWITCH__
 destroyPresentation();
#endif
 release(texels);release(parameters);release(tileBuffer);release(output);if(fence)DestroyFence(device,fence,nullptr);if(commandPool)DestroyCommandPool(device,commandPool,nullptr);if(pipeline)DestroyPipeline(device,pipeline,nullptr);if(layout)DestroyPipelineLayout(device,layout,nullptr);if(descriptorPool)DestroyDescriptorPool(device,descriptorPool,nullptr);if(setLayout)DestroyDescriptorSetLayout(device,setLayout,nullptr);DestroyDevice(device,nullptr);}
#ifdef __SWITCH__
 if(surface&&DestroySurfaceKHR)DestroySurfaceKHR(instance,surface,nullptr);
#endif
 if(instance&&DestroyInstance)DestroyInstance(instance,nullptr);
#ifndef __SWITCH__
  if(library)dlclose(library);
#endif
 }
 void ensure(Buffer&b,size_t required,size_t minimum=4096){
  if(required<=b.size)return;
  if(required>maxBufferBytes)throw std::runtime_error("Vulkan storage buffer capacity exceeded");
  size_t capacity=std::max(minimum,size_t(b.size));while(capacity<required)capacity=std::min(maxBufferBytes,capacity*2);
  capacity=std::min(capacity,maxBufferBytes);Buffer next=buffer(capacity);
  if(b.data)std::memcpy(next.data,b.data,size_t(b.size));release(b);b=next;
 }
 Span allocateSpan(size_t size){
  for(auto it=freeSpans.begin();it!=freeSpans.end();++it)if(it->second>=size){Span span{it->first,size};size_t rest=it->second-size;freeSpans.erase(it);if(rest)freeSpans.emplace(span.offset+size,rest);return span;}
  ensure(texels,textureEnd+size,16*1024*1024);Span span{textureEnd,size};textureEnd+=size;return span;
 }
 uint32_t texture(const Image&i){
  if(i.pixels.size()!=size_t(i.width)*i.height*4||i.pixels.empty())throw std::runtime_error("Vulkan texture dimensions");
  auto at=textures.find(&i);
  if(at!=textures.end()){
   auto&t=at->second;bool same=inBatch&&t.checkedBatch==batch;
   if(!same){++textureChecks;same=t.width==i.width&&t.height==i.height&&std::memcmp(static_cast<const uint8_t*>(texels.data)+t.storage.offset,i.pixels.data(),i.pixels.size())==0;}
   if(!same){if(t.lastUsed==frames+1)retired.push_back(t.storage);else freeSpan(t.storage);textures.erase(at);at=textures.end();}
  }
  if(at==textures.end()){
   Texture t;t.width=i.width;t.height=i.height;t.storage=allocateSpan(i.pixels.size());
   std::memcpy(static_cast<uint8_t*>(texels.data)+t.storage.offset,i.pixels.data(),i.pixels.size());uploadBytes+=i.pixels.size();at=textures.emplace(&i,t).first;
  }
  at->second.lastUsed=frames+1;at->second.checkedBatch=batch;return uint32_t(at->second.storage.offset/4);
 }
 void barrier(VkPipelineStageFlags source,VkPipelineStageFlags dest,VkAccessFlags from,VkAccessFlags to){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=from;b.dstAccessMask=to;CmdPipelineBarrier(command,source,dest,0,1,&b,0,nullptr,0,nullptr);}
 void append(Push p){
  if(!recording)throw std::runtime_error("Vulkan frame not begun");
  p.rect[0]=std::max(0,p.rect[0]);p.rect[1]=std::max(0,p.rect[1]);p.rect[2]=std::min(int(Width),p.rect[2]);p.rect[3]=std::min(int(Height),p.rect[3]);if(p.rect[2]<=p.rect[0]||p.rect[3]<=p.rect[1])return;
  uint32_t index=uint32_t(drawCommands.size());drawCommands.push_back({p,{},{},{}});
  for(unsigned y=unsigned(p.rect[1])/Tile;y<=unsigned(p.rect[3]-1)/Tile;y++)for(unsigned x=unsigned(p.rect[0])/Tile;x<=unsigned(p.rect[2]-1)/Tile;x++)tileDraws[y*Columns+x].push_back(index);
  ++draws;
 }
 void dispatch(const Image&i,Push&p,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){
  if(!recording)throw std::runtime_error("Vulkan frame not begun");if(!i.width||!i.height||alpha<=0)return;if(blend>8)throw std::runtime_error("Vulkan unsupported blend");
  if(p.rect[2]<=0||p.rect[3]<=0||p.rect[0]>=int(Width)||p.rect[1]>=int(Height))return;
  p.meta[0]=i.width;p.meta[1]=i.height;p.meta[2]|=(blend<<8)|filter;p.meta[3]=unsigned(std::clamp(alpha,0.f,255.f));p.colors[0]=add;p.colors[1]=mul;p.colors[2]=texture(i);append(p);
 }
 void beginFrame(bool clear,uint32_t color){
  if(recording)throw std::runtime_error("Vulkan frame already begun");
  checked(ResetCommandPool(device,commandPool,0),"reset commands");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checked(BeginCommandBuffer(command,&bi),"begin commands");
  drawCommands.clear();for(auto&tile:tileDraws)tile.clear();draws=dispatches=textureChecks=0;uploadBytes=0;clearOutput=clear;clearColor=color;recording=true;inBatch=false;
 }
 void recordRaster(){
  size_t words=TileCount*2;for(auto&tile:tileDraws)words+=tile.size();
  ensure(parameters,std::max(sizeof(DrawData),drawCommands.size()*sizeof(DrawData)));ensure(tileBuffer,words*sizeof(uint32_t));ensure(texels,4);
  if(!drawCommands.empty())std::memcpy(parameters.data,drawCommands.data(),drawCommands.size()*sizeof(DrawData));
  auto*indices=static_cast<uint32_t*>(tileBuffer.data);uint32_t cursor=TileCount*2;
  for(unsigned tile=0;tile<TileCount;tile++){indices[tile*2]=cursor;indices[tile*2+1]=uint32_t(tileDraws[tile].size());std::copy(tileDraws[tile].begin(),tileDraws[tile].end(),indices+cursor);cursor+=uint32_t(tileDraws[tile].size());}
  flush(parameters);flush(tileBuffer);if(uploadBytes)flush(texels);
  VkDescriptorBufferInfo buffers[4]={{texels.buffer,0,texels.size},{output.buffer,0,output.size},{parameters.buffer,0,parameters.size},{tileBuffer.buffer,0,tileBuffer.size}};
  VkWriteDescriptorSet writes[4]{};for(unsigned n=0;n<4;n++){writes[n].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[n].dstSet=rasterSet;writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=&buffers[n];}UpdateDescriptorSets(device,4,writes,0,nullptr);
  barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&rasterSet,0,nullptr);
  Push push;push.meta[0]=Width;push.meta[1]=Height;push.meta[2]=Columns;push.meta[3]=clearOutput;push.colors[0]=clearColor;
  CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);CmdDispatch(command,Columns,Rows,1);++dispatches;
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
void VulkanRaster::begin(const qlie::Bytes&pixels){auto&s=*impl;if(pixels.size()!=s.output.size||s.recording)throw std::runtime_error("Vulkan begin frame");std::memcpy(s.output.data,pixels.data(),pixels.size());s.flush(s.output);s.beginFrame(false,0);}
void VulkanRaster::begin(uint32_t color){impl->beginFrame(true,color);}
void VulkanRaster::beginBatch(){++impl->batch;impl->inBatch=true;}
void VulkanRaster::endBatch(){impl->inBatch=false;}
void VulkanRaster::maskedImage(const Image&source,const Image&mask,const AnimationSample::Mask&settings,const Affine2D&m,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){
 auto&s=*impl;size_t before=s.drawCommands.size();image(source,m,alpha,add,mul,blend,filter);if(s.drawCommands.size()==before)return;
 auto&draw=s.drawCommands.back();draw.image.meta[2]|=262144;
 float width=settings.width?settings.width:source.width,height=settings.height?settings.height:source.height;
 float maskWidth=settings.maskWidth?settings.maskWidth:mask.width,maskHeight=settings.maskHeight?settings.maskHeight:mask.height;
 draw.maskSource[0]=settings.sourceX-width*settings.alignX;draw.maskSource[1]=settings.sourceY-height*settings.alignY;draw.maskSource[2]=width;draw.maskSource[3]=height;
 draw.maskRect[0]=settings.x-maskWidth*settings.alignX;draw.maskRect[1]=settings.y-maskHeight*settings.alignY;draw.maskRect[2]=maskWidth;draw.maskRect[3]=maskHeight;
 draw.maskMeta[0]=mask.width;draw.maskMeta[1]=mask.height;draw.maskMeta[2]=s.texture(mask);draw.maskMeta[3]=unsigned(settings.amount)|(unsigned(settings.mode)<<8);
}
void VulkanRaster::font(const Image&i,int x,int y){
 if(!i.width||!i.height)return;
 Push p;p.p[1][0]=float(x);p.p[1][1]=float(y);p.rect[0]=x;p.rect[1]=y;p.rect[2]=x+int(i.width);p.rect[3]=y+int(i.height);p.meta[0]=i.width;p.meta[1]=i.height;p.meta[2]=1u<<20;p.colors[2]=impl->texture(i);impl->append(p);
}
void VulkanRaster::dim(unsigned numerator,unsigned denominator){
 if(!denominator||numerator>denominator)throw std::runtime_error("Vulkan dim ratio");
 Push p;p.rect[2]=1280;p.rect[3]=720;p.meta[0]=numerator;p.meta[1]=denominator;p.meta[2]=2u<<20;impl->append(p);
}
void VulkanRaster::transition(const Image&previous,const Image*rule,double amount,unsigned mode){
 auto&s=*impl;if(previous.width!=1280||previous.height!=720)throw std::runtime_error("Vulkan transition dimensions");
 Push p;p.rect[2]=1280;p.rect[3]=720;p.meta[2]=3u<<20;p.colors[2]=s.texture(previous);
 if(rule){p.meta[0]=rule->width;p.meta[1]=rule->height;p.meta[2]|=131072;p.meta[3]=uint8_t(255*(1-amount));p.colors[1]=mode;p.colors[3]=s.texture(*rule);}
 else{
  // CPU's double expression is quantized once for the 256x256 possible byte
  // pairs. The GPU reuses this exact table for all four channels and pixels.
  if(s.lutAmount!=amount){s.transitionLut={256,256,qlie::Bytes(256*256*4)};for(unsigned old=0;old<256;old++)for(unsigned current=0;current<256;current++)s.transitionLut.pixels[(old*256+current)*4]=uint8_t(old*(1-amount)+current*amount);s.lutAmount=amount;}
  p.colors[3]=s.texture(s.transitionLut);
 }
 s.append(p);
}

void VulkanRaster::end(qlie::Bytes&pixels){auto&s=*impl;if(!s.recording)throw std::runtime_error("Vulkan frame not begun");s.recordRaster();s.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);checked(s.EndCommandBuffer(s.command),"end commands");checked(s.ResetFences(s.device,1,&s.fence),"reset fence");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&s.command;checked(s.QueueSubmit(s.queue,1,&submit,s.fence),"submit");checked(s.WaitForFences(s.device,1,&s.fence,VK_TRUE,5000000000ull),"wait");s.flush(s.output,true);std::memcpy(pixels.data(),s.output.data,pixels.size());s.recording=false;s.completeFrame();}
void VulkanRaster::image(const Image&i,const Affine2D&m,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){float det=m.a*m.d-m.b*m.c;if(std::abs(det)<1e-12f)return;Push p;p.p[0][0]=m.a;p.p[0][1]=m.b;p.p[0][2]=m.c;p.p[0][3]=m.d;p.p[1][0]=m.x;p.p[1][1]=m.y;p.p[1][2]=det;float xmin=m.x,xmax=m.x,ymin=m.y,ymax=m.y;for(unsigned n=0;n<4;n++){float u=n&1?i.width:0,v=n&2?i.height:0;float x=m.a*u+m.b*v+m.x,y=m.c*u+m.d*v+m.y;xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);}p.rect[0]=int(std::floor(xmin));p.rect[1]=int(std::floor(ymin));p.rect[2]=int(std::ceil(xmax));p.rect[3]=int(std::ceil(ymax));impl->dispatch(i,p,alpha,add,mul,blend,filter);}
void VulkanRaster::triangle(const Image&i,const std::array<TextureVertex,3>&vertices,const Affine2D&post,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){auto v=vertices;for(auto&p:v){if(p.depth<10)return;float ratio=1000/p.depth;float x=640+(p.x-640)*ratio,y=360+(p.y-360)*ratio;p.x=post.a*x+post.b*y+post.x;p.y=post.c*x+post.d*y+post.y;}auto edge=[](const TextureVertex&a,const TextureVertex&b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};float area=edge(v[0],v[1],v[2].x,v[2].y);if(std::abs(area)<1e-6f)return;if(area<0){std::swap(v[1],v[2]);area=-area;}Push p;for(unsigned n=0;n<3;n++){p.p[n][0]=v[n].x;p.p[n][1]=v[n].y;p.p[n][2]=v[n].u;p.p[n][3]=v[n].v;p.p[3][n]=v[n].depth;}p.p[3][3]=area;p.meta[2]=65536;p.rect[0]=int(std::floor(std::min({v[0].x,v[1].x,v[2].x})));p.rect[1]=int(std::floor(std::min({v[0].y,v[1].y,v[2].y})));p.rect[2]=int(std::ceil(std::max({v[0].x,v[1].x,v[2].x})));p.rect[3]=int(std::ceil(std::max({v[0].y,v[1].y,v[2].y})));impl->dispatch(i,p,alpha,add,mul,blend,filter);}
}
