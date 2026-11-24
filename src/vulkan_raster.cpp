// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef __SWITCH__
#include "diagnostics.h"
#include <switch.h>
#include "switch_threads.hpp"
#include <thread>
#include <condition_variable>
#include <mutex>
#define VK_USE_PLATFORM_VI_NN
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "vulkan_raster.hpp"
#include "raster_scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <limits>
#include <chrono>
#ifndef __SWITCH__
#include <dlfcn.h>
#else
extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance,const char*);
#endif
namespace whj2 {
using namespace gpu;
namespace {
#include "raster_spv.inc"
#ifdef __SWITCH__
#include "scene_vert_spv.inc"
#include "scene_frag_spv.inc"
#include "scene_init_comp_spv.inc"
#include "present_vert_spv.inc"
#include "present_frag_spv.inc"
#endif
void checked(VkResult r,const char*what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string("Vulkan ")+what+": "+std::to_string(r));}

}
struct VulkanRaster::Impl final : Scene {
 Impl():Scene("VULKAN"){}
#define INSTANCE_FUNCTIONS(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceProperties2) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) X(DestroyBuffer) X(FreeMemory) X(CreateDescriptorSetLayout) X(CreateDescriptorPool) X(AllocateDescriptorSets) X(FreeDescriptorSets) X(UpdateDescriptorSets) X(DestroyDescriptorPool) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(CreateShaderModule) X(CreateComputePipelines) X(DestroyShaderModule) X(DestroyPipeline) X(DestroyPipelineLayout) X(CreateCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool) X(BeginCommandBuffer) X(EndCommandBuffer) X(DestroyCommandPool) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) X(CmdCopyBuffer) X(CmdFillBuffer) X(CmdPipelineBarrier) X(CreateFence) X(ResetFences) X(WaitForFences) X(DestroyFence) X(QueueSubmit) X(DeviceWaitIdle)
#define DECLARE(N) PFN_vk##N N=nullptr;
 INSTANCE_FUNCTIONS(DECLARE) DEVICE_FUNCTIONS(DECLARE)
#ifdef __SWITCH__
#define PRESENT_INSTANCE(X) X(GetPhysicalDeviceFeatures2) X(EnumerateDeviceExtensionProperties) X(GetPhysicalDeviceFormatProperties) X(CreateViSurfaceNN) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR)
#define PRESENT_DEVICE(X) X(CmdCopyBufferToImage) X(CreateBufferView) X(DestroyBufferView) X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) X(CreateSemaphore) X(DestroySemaphore) X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateGraphicsPipelines) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdDraw) X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults)
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
 Buffer output,staging,texels,parameters,tileBuffer,scanlineHistory;VkDescriptorSet rasterSet=VK_NULL_HANDLE;
 bool interlaced=false,scanlineValid=false,canvasValid=false;unsigned scanlinePhase=0;
 bool descriptorsChanged=true,texelsDirty=false;
 size_t maxBufferBytes=0;
#ifdef __SWITCH__
 bool canOrderPixels=false,copyPresent=false;
 std::vector<SwapDamage::Mask> swapDamage;
 VkRenderPass scenePass=VK_NULL_HANDLE;VkFramebuffer sceneFrame=VK_NULL_HANDLE;
 VkPipeline scenePipeline=VK_NULL_HANDLE,sceneInit=VK_NULL_HANDLE;
 VkBufferView texelView=VK_NULL_HANDLE;uint32_t maxViewElements=0,viewElements=0;
 std::array<std::array<uint32_t,2>,TileCount> ranges{};
 std::vector<std::array<uint32_t,3>> instances;
 std::vector<std::array<unsigned,4>> neededBounds;
 std::vector<std::array<uint32_t,4>> initTiles;
 Clock::time_point lastRefresh{};
 std::thread presenter;
 std::mutex presentMutex;
 std::condition_variable presentSignal;
 bool presentQueued=false,stopPresenter=false,inFlight=false;
 std::exception_ptr presentError;
 std::optional<uint64_t> completedMs;
 VkQueryPool timeQueries=VK_NULL_HANDLE;
 unsigned timestampBits=0;
 double timestampNs=0;
 uint64_t timingFrame=0;bool timingHadRaster=false;
#endif
 void release(Buffer&b){if(b.data)UnmapMemory(device,b.memory);if(b.buffer)DestroyBuffer(device,b.buffer,nullptr);if(b.memory)FreeMemory(device,b.memory,nullptr);b={};}
 Buffer buffer(size_t size,VkBufferUsageFlags extra=0,bool host=true){
  Buffer b;b.size=size;VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|extra;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  checked(CreateBuffer(device,&info,nullptr,&b.buffer),"create buffer");
  try{
   VkMemoryRequirements req;GetBufferMemoryRequirements(device,b.buffer,&req);uint32_t type=UINT32_MAX;int best=-1;
   for(unsigned i=0;i<memory.memoryTypeCount;++i){
    auto flags=memory.memoryTypes[i].propertyFlags;
    if(!(req.memoryTypeBits&(1u<<i))||(host&&!(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)))continue;
    int score=(flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT?2:0)+(host&&(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?1:0);
    if(score>best){type=i;best=score;}
   }
   if(type==UINT32_MAX)throw std::runtime_error("No compatible Vulkan buffer memory");
   b.coherent=(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
   VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;
   checked(AllocateMemory(device,&allocation,nullptr,&b.memory),"allocate buffer");checked(BindBufferMemory(device,b.buffer,b.memory,0),"bind buffer");
   if(host)checked(MapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.data),"map buffer");
   return b;
  }catch(...){release(b);throw;}
 }
 void ensureStaging(){if(!staging.buffer)staging=buffer(CanvasBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);}
 void flush(const Buffer&b,bool invalidate=false){if(b.coherent)return;VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=b.memory;range.size=VK_WHOLE_SIZE;checked(invalidate?InvalidateMappedMemoryRanges(device,1,&range):FlushMappedMemoryRanges(device,1,&range),invalidate?"invalidate":"flush");}
 void initialize() override {
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
  uint32_t families=0;
  GetPhysicalDeviceQueueFamilyProperties(physical,&families,nullptr);
  std::vector<VkQueueFamilyProperties>queues(families);
  GetPhysicalDeviceQueueFamilyProperties(physical,&families,queues.data());
  uint32_t family=UINT32_MAX;
  for(unsigned i=0;i<families;++i){
   if(!(queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT))continue;
#ifdef __SWITCH__
   VkBool32 supported=VK_FALSE;
   checked(GetPhysicalDeviceSurfaceSupportKHR(physical,i,surface,&supported),"surface support");
   if(!supported||!(queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT))continue;
#endif
   family=i;
   break;
  }
  if(family==UINT32_MAX)throw std::runtime_error("Vulkan has no compute queue");
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
#ifdef __SWITCH__
  const char*deviceExtensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME,VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME};
  uint32_t extensionCount=0;
  checked(EnumerateDeviceExtensionProperties(physical,nullptr,&extensionCount,nullptr),"device extensions");
  std::vector<VkExtensionProperties> available(extensionCount);
  checked(EnumerateDeviceExtensionProperties(physical,nullptr,&extensionCount,available.data()),"device extensions");
  bool hasInterlock=std::any_of(available.begin(),available.end(),[](const auto&e){return std::strcmp(e.extensionName,VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME)==0;});
  VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
  VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  if(hasInterlock)supported.pNext=&interlock;
  GetPhysicalDeviceFeatures2(physical,&supported);
  VkFormatProperties r32{};GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R32_UINT,&r32);
  canOrderPixels=hasInterlock&&interlock.fragmentShaderPixelInterlock&&supported.features.fragmentStoresAndAtomics&&supported.features.shaderClipDistance&&
                 (r32.bufferFeatures&VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT);
  VkPhysicalDeviceFeatures enabled{};
  if(canOrderPixels){
   enabled.fragmentStoresAndAtomics=VK_TRUE;enabled.shaderClipDistance=VK_TRUE;interlock.fragmentShaderSampleInterlock=VK_FALSE;interlock.fragmentShaderShadingRateInterlock=VK_FALSE;
   di.pEnabledFeatures=&enabled;di.pNext=&interlock;
  }
  maxViewElements=properties.limits.maxTexelBufferElements;
  di.enabledExtensionCount=canOrderPixels?2:1;di.ppEnabledExtensionNames=deviceExtensions;
  VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&driver;GetPhysicalDeviceProperties2(physical,&props);QLIE_LOG_LINE(<<"VULKAN driver="<<driver.driverName<<" info="<<driver.driverInfo<<" id="<<driver.driverID<<"\n");if(driver.driverID!=VK_DRIVER_ID_MESA_NVK)throw std::runtime_error("Switch requires Mesa NVK");
#endif
  checked(CreateDevice(physical,&di,nullptr,&device),"create device");
#define LOAD_DEVICE(N) N=reinterpret_cast<PFN_vk##N>(GetDeviceProcAddr(device,"vk" #N));if(!N)throw std::runtime_error("Missing vk" #N);
  DEVICE_FUNCTIONS(LOAD_DEVICE)
#ifdef __SWITCH__
  PRESENT_DEVICE(LOAD_DEVICE)
#endif
#undef LOAD_DEVICE
  GetDeviceQueue(device,family,0,&queue);QLIE_LOG_LINE(<<"VULKAN device="<<properties.deviceName<<" api="<<VK_VERSION_MAJOR(properties.apiVersion)<<'.'<<VK_VERSION_MINOR(properties.apiVersion)<<" compute-family="<<family<<"\n");
  VkDescriptorSetLayoutBinding bindings[5]{};
  unsigned bindingCount=4,poolTypes=1;
  for(unsigned i=0;i<4;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT|(i==1?VK_SHADER_STAGE_FRAGMENT_BIT:0);}
#ifdef __SWITCH__
  if(canOrderPixels){
   for(unsigned i=0;i<4;++i)bindings[i].stageFlags|=VK_SHADER_STAGE_FRAGMENT_BIT;
   bindings[2].stageFlags|=VK_SHADER_STAGE_VERTEX_BIT;bindings[3].stageFlags|=VK_SHADER_STAGE_VERTEX_BIT;
   bindings[4]={4,VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
   bindingCount=5;poolTypes=2;
  }
#endif
  VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=bindingCount;si.pBindings=bindings;
  checked(CreateDescriptorSetLayout(device,&si,nullptr,&setLayout),"descriptor layout");
  VkDescriptorPoolSize poolSizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8},{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,2}};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=2;pi.poolSizeCount=poolTypes;pi.pPoolSizes=poolSizes;
  checked(CreateDescriptorPool(device,&pi,nullptr,&descriptorPool),"descriptor pool");
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Push)};VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&setLayout;li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;checked(CreatePipelineLayout(device,&li,nullptr,&layout),"pipeline layout");VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof(rasterSpv);sm.pCode=rasterSpv;VkShaderModule shader;checked(CreateShaderModule(device,&sm,nullptr,&shader),"shader");
  VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=layout;cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=shader;cp.stage.pName="main";auto result=CreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline);DestroyShaderModule(device,shader,nullptr);checked(result,"compute pipeline");
  VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=family;checked(CreateCommandPool(device,&ci,nullptr,&commandPool),"command pool");VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=commandPool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;checked(AllocateCommandBuffers(device,&ai,&command),"command buffer");VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checked(CreateFence(device,&fi,nullptr,&fence),"fence");
#ifdef __SWITCH__
  output=buffer(2*CanvasBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false);
#else
  output=buffer(2*CanvasBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
#endif
  VkDescriptorSetAllocateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};descriptors.descriptorPool=descriptorPool;descriptors.descriptorSetCount=1;descriptors.pSetLayouts=&setLayout;checked(AllocateDescriptorSets(device,&descriptors,&rasterSet),"raster descriptor");
#ifdef __SWITCH__
  createPresentation(physical);
  if(canOrderPixels){
   try{createScenePipeline();}catch(const std::exception&e){destroyScenePipeline();QLIE_LOGF("VULKAN ordered geometry unavailable: %s\n",e.what());}
  }
  QLIE_LOGF("VULKAN scene_backend=%s pixel_interlock=%d\n",scenePipeline?"ordered-geometry":"tiled-compute",int(canOrderPixels));
  if constexpr (qlie::diagnostics){
   timestampBits=queues[family].timestampValidBits;
   timestampNs=properties.limits.timestampPeriod;
   if(timestampBits&&properties.limits.timestampComputeAndGraphics&&timestampNs>0){
    VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queries.queryType=VK_QUERY_TYPE_TIMESTAMP;queries.queryCount=4;
    auto status=CreateQueryPool(device,&queries,nullptr,&timeQueries);
    if(status!=VK_SUCCESS){timeQueries=VK_NULL_HANDLE;QLIE_LOG_LINE(<<"VULKAN timestamps unavailable result="<<status<<"\n");}
   }
  }
  presenter=std::thread([this]{presentationLoop();});
#endif
 }

#ifdef __SWITCH__
 // Native surface, per-swapchain-image finished semaphores and acquire/submit/
 // present ownership follow biko3-switch-runtime's 0BSD renderer at bd70f2c.
 void createPresentation(VkPhysicalDevice physical){
  VkSurfaceCapabilitiesKHR caps;checked(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"surface capabilities");uint32_t count=0;checked(GetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"surface formats");std::vector<VkSurfaceFormatKHR>formats(count);checked(GetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"surface formats");auto chosen=std::find_if(formats.begin(),formats.end(),[](auto f){return f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM;});if(chosen==formats.end())throw std::runtime_error("No UNORM display format");if(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT){
   auto bgra=std::find_if(formats.begin(),formats.end(),[](auto f){return f.format==VK_FORMAT_B8G8R8A8_UNORM;});if(bgra!=formats.end())chosen=bgra;
  }
  format=chosen->format;
  extent=caps.currentExtent;if(extent.width==UINT32_MAX)extent={std::clamp(1280u,caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(720u,caps.minImageExtent.height,caps.maxImageExtent.height)};
  if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)||!(caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))throw std::runtime_error("Unsupported display surface");
  count=std::max(3u,caps.minImageCount);if(caps.maxImageCount)count=std::min(count,caps.maxImageCount);VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=surface;ci.minImageCount=count;ci.imageFormat=format;ci.imageColorSpace=chosen->colorSpace;ci.imageExtent=extent;ci.imageArrayLayers=1;copyPresent=format==VK_FORMAT_B8G8R8A8_UNORM&&extent.width==Width&&extent.height==Height&&(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT);ci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|(copyPresent?VK_IMAGE_USAGE_TRANSFER_DST_BIT:0);ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;ci.preTransform=caps.currentTransform;ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;ci.presentMode=VK_PRESENT_MODE_FIFO_KHR;ci.clipped=copyPresent?VK_FALSE:VK_TRUE;auto created=CreateSwapchainKHR(device,&ci,nullptr,&swapchain);
  if(copyPresent&&(created==VK_ERROR_FORMAT_NOT_SUPPORTED||created==VK_ERROR_INITIALIZATION_FAILED)){
   QLIE_LOGF("VULKAN transfer swapchain result=%d, retrying color-only\n",int(created));copyPresent=false;ci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ci.clipped=VK_TRUE;
   created=CreateSwapchainKHR(device,&ci,nullptr,&swapchain);
  }
  checked(created,"swapchain");checked(GetSwapchainImagesKHR(device,swapchain,&count,nullptr),"swapchain images");swapImages.resize(count);checked(GetSwapchainImagesKHR(device,swapchain,&count,swapImages.data()),"swapchain images");swapViews.resize(count);swapFrames.resize(count);finished.resize(count);
  if(copyPresent){swapDamage.resize(count);for(auto&pending:swapDamage)pending.set();}
  VkAttachmentDescription attachment{};attachment.format=format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;VkSubpassDependency dependency{};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=1;rp.pDependencies=&dependency;checked(CreateRenderPass(device,&rp,nullptr,&renderPass),"display pass");VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};checked(CreateSemaphore(device,&semaphore,nullptr,&acquired),"acquire semaphore");
  for(unsigned i=0;i<count;i++){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=swapImages[i];vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(CreateImageView(device,&vi,nullptr,&swapViews[i]),"display view");VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=renderPass;fb.attachmentCount=1;fb.pAttachments=&swapViews[i];fb.width=extent.width;fb.height=extent.height;fb.layers=1;checked(CreateFramebuffer(device,&fb,nullptr,&swapFrames[i]),"display frame");checked(CreateSemaphore(device,&semaphore,nullptr,&finished[i]),"present semaphore");}
  graphicsPipeline(renderPass,extent,1,presentVertexSpv,sizeof(presentVertexSpv),presentFragmentSpv,sizeof(presentFragmentSpv),displayPipeline);
  VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=descriptorPool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&setLayout;checked(AllocateDescriptorSets(device,&allocation,&displaySet),"display descriptor");VkDescriptorBufferInfo bi{output.buffer,0,CanvasBytes};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=displaySet;write.dstBinding=1;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bi;UpdateDescriptorSets(device,1,&write,0,nullptr);QLIE_LOG_LINE(<<"VULKAN native display="<<extent.width<<'x'<<extent.height<<" images="<<count<<" copy_present="<<copyPresent<<"\n");
 }
 VkShaderModule module(const uint32_t*code,size_t bytes){
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=bytes;info.pCode=code;
  VkShaderModule result=VK_NULL_HANDLE;checked(CreateShaderModule(device,&info,nullptr,&result),"shader module");return result;
 }
 void graphicsPipeline(VkRenderPass pass,VkExtent2D size,unsigned attachments,const uint32_t*vs,size_t vsBytes,const uint32_t*fs,size_t fsBytes,VkPipeline&out){
  VkShaderModule vert=module(vs,vsBytes),frag=VK_NULL_HANDLE;
  try{frag=module(fs,fsBytes);}catch(...){DestroyShaderModule(device,vert,nullptr);throw;}
  VkPipelineShaderStageCreateInfo stages[2]{};
  for(auto&stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
  stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vert;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=frag;
  VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};input.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0,0,float(size.width),float(size.height),0,1};VkRect2D scissor{{0,0},size};
  VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};view.viewportCount=view.scissorCount=1;view.pViewports=&viewport;view.pScissors=&scissor;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
  VkPipelineColorBlendStateCreateInfo color{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};color.attachmentCount=attachments;color.pAttachments=attachments?&blend:nullptr;
  VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.stageCount=2;info.pStages=stages;info.pVertexInputState=&vertex;info.pInputAssemblyState=&input;
  info.pViewportState=&view;info.pRasterizationState=&raster;info.pMultisampleState=&samples;info.pColorBlendState=&color;info.layout=layout;info.renderPass=pass;
  auto result=CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&out);
  DestroyShaderModule(device,frag,nullptr);DestroyShaderModule(device,vert,nullptr);checked(result,"graphics pipeline");
 }
 void createScenePipeline(){
  VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.subpassCount=1;pass.pSubpasses=&sub;
  checked(CreateRenderPass(device,&pass,nullptr,&scenePass),"geometry pass");
  VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=scenePass;fb.width=Width;fb.height=Height;fb.layers=1;
  checked(CreateFramebuffer(device,&fb,nullptr,&sceneFrame),"geometry framebuffer");
  auto shader=module(sceneInitSpv,sizeof(sceneInitSpv));VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cp.layout=layout;cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=shader;cp.stage.pName="main";
  auto result=CreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&sceneInit);DestroyShaderModule(device,shader,nullptr);checked(result,"geometry tile init");
  graphicsPipeline(scenePass,{Width,Height},0,sceneVertexSpv,sizeof(sceneVertexSpv),sceneFragmentSpv,sizeof(sceneFragmentSpv),scenePipeline);
 }
 void destroyScenePipeline(){
  if(scenePipeline)DestroyPipeline(device,scenePipeline,nullptr);if(sceneInit)DestroyPipeline(device,sceneInit,nullptr);
  if(sceneFrame)DestroyFramebuffer(device,sceneFrame,nullptr);if(scenePass)DestroyRenderPass(device,scenePass,nullptr);
  scenePipeline=sceneInit=VK_NULL_HANDLE;scenePass=VK_NULL_HANDLE;sceneFrame=VK_NULL_HANDLE;
 }
 uint64_t presentRecorded(){
  const auto start=qlie::diagnostics?Clock::now():Clock::time_point{};
  uint32_t index=0;
  auto result=AcquireNextImageKHR(device,swapchain,5000000000ull,acquired,VK_NULL_HANDLE,&index);
  if(result!=VK_SUBOPTIMAL_KHR)checked(result,"acquire image");
  const auto imageReady=qlie::diagnostics?Clock::now():Clock::time_point{};
  if(qlie::diagnostics&&timingFrame&&timeQueries){
   CmdResetQueryPool(command,timeQueries,2,2);
   // The previous scene work must finish before measuring display conversion.
   CmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timeQueries,2);
  }
  unsigned copiedTiles=0,copyRegions=0;
  if(copyPresent){
   auto regions=SwapDamage::regions(swapDamage[index]);
   copyRegions=unsigned(regions.size());
   if(!regions.empty()){
    std::vector<VkBufferImageCopy> copies;
    copies.reserve(regions.size());
    for(auto r:regions){
     VkBufferImageCopy copy{};
     copy.bufferOffset=VkDeviceSize(r.y*Tile*Width+r.x*Tile)*4;
     copy.bufferRowLength=Width;copy.bufferImageHeight=Height;
     copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
     copy.imageOffset={int32_t(r.x*Tile),int32_t(r.y*Tile),0};
     copy.imageExtent={r.width*Tile,r.height*Tile,1};
     copies.push_back(copy);copiedTiles+=r.width*r.height;
    }
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkImageMemoryBarrier swap{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};swap.image=swapImages[index];swap.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    swap.srcQueueFamilyIndex=swap.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    swap.oldLayout=copiedTiles==TileCount?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    swap.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;swap.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&swap);
    CmdCopyBufferToImage(command,interlaced?scanlineHistory.buffer:output.buffer,swap.image,swap.newLayout,uint32_t(copies.size()),copies.data());
    swap.oldLayout=swap.newLayout;swap.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;swap.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;swap.dstAccessMask=0;
    CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&swap);
   }
  }else{
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin.renderPass=renderPass;begin.framebuffer=swapFrames[index];begin.renderArea={{0,0},extent};
  CmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
  CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,displayPipeline);
  CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&displaySet,0,nullptr);
  Push push;push.meta[0]=extent.width;push.meta[1]=extent.height;
  CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
  CmdDraw(command,3,1,0,0);CmdEndRenderPass(command);
  }
  if(qlie::diagnostics&&timingFrame&&timeQueries)CmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timeQueries,3);
  checked(EndCommandBuffer(command),"end display");
  checked(ResetFences(device,1,&fence),"reset display fence");
  VkPipelineStageFlags wait=copyPresent?(copyRegions?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT):VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&wait;
  submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&finished[index];
  checked(QueueSubmit(queue,1,&submit,fence),"submit display");
  VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  info.waitSemaphoreCount=1;info.pWaitSemaphores=&finished[index];info.swapchainCount=1;info.pSwapchains=&swapchain;info.pImageIndices=&index;
  result=QueuePresentKHR(queue,&info);
  if(result!=VK_SUBOPTIMAL_KHR)checked(result,"present");
  if(copyPresent)swapDamage[index].reset();
  const auto presented=Clock::now();
  checked(WaitForFences(device,1,&fence,VK_TRUE,5000000000ull),"display completion");
  if(qlie::diagnostics&&timingFrame){
   auto gpuMs=[&](unsigned first){
    uint64_t ticks[2];
    if(!timeQueries||GetQueryPoolResults(device,timeQueries,first,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)!=VK_SUCCESS)return -1.0;
    uint64_t mask=timestampBits==64?UINT64_MAX:(uint64_t(1)<<timestampBits)-1;
    return double((ticks[1]-ticks[0])&mask)*timestampNs/1e6;
   };
   QLIE_LOGF("VULKAN TIMING frame=%llu raster_gpu_ms=%.3f present_gpu_ms=%.3f acquire_ms=%.3f submission_to_fence_ms=%.3f copy_regions=%u copy_tiles=%u\n",
                (unsigned long long)timingFrame,timingHadRaster?gpuMs(0):0.0,gpuMs(2),milliseconds(imageReady-start),milliseconds(Clock::now()-imageReady),copyRegions,copiedTiles);
  }
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(presented.time_since_epoch()).count());
 }
 void presentationLoop(){
  logSwitchThread("vulkan-present",bindSwitchThread(1));
  std::unique_lock<std::mutex> lock(presentMutex);
  for(;;){
   presentSignal.wait(lock,[this]{return presentQueued||stopPresenter;});
   if(!presentQueued)return;
   lock.unlock();
   std::optional<uint64_t> stamp;
   std::exception_ptr error;
   try{stamp=presentRecorded();}catch(...){error=std::current_exception();}
   lock.lock();
   completedMs=stamp;presentError=error;presentQueued=false;
   presentSignal.notify_all();
  }
 }
 // Called by the producer only after the preceding presenter has finished.
 // Readbacks also change the retained canvas, even though no image is presented.
 void markSwapDamage(){
  if(!copyPresent)return;
  auto changed=changedTiles(interlaced);
  for(auto&pending:swapDamage)pending|=changed;
 }
 std::optional<uint64_t> presentNative(bool refreshUnchanged) override {
  current->geometry=scenePipeline&&current->cleared;
  if(!recording)throw std::runtime_error("Vulkan frame not begun");
  planTiles();
  waitForPrevious();
  markSwapDamage();
  std::optional<uint64_t> confirmed;
  {
   std::lock_guard<std::mutex> lock(presentMutex);
   confirmed=std::exchange(completedMs,std::nullopt);
  }
  if(!interlaced&&!refreshUnchanged&&nativeDisplayCurrent&&current->cleared&&workTiles.empty()){
   // The compositor still displays the last presented surface. Throttle input
   // and animation sampling when FIFO acquisition no longer paces the loop.
   if constexpr (qlie::diagnostics) cpuBuildMs=milliseconds(Clock::now()-cpuFrameStart)-priorWaitMs;
   const auto next=lastRefresh+std::chrono::nanoseconds(16666667);
   auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(next-Clock::now()).count();
   if(remaining>0)svcSleepThread(remaining);
   if constexpr (qlie::diagnostics) reusedDisplay=true;
  }else{
   beginGpuCommands();
   {
    std::lock_guard<std::mutex> lock(presentMutex);
    presentQueued=true;inFlight=true;
   }
   presentSignal.notify_one();nativeDisplayCurrent=true;
  }
  lastRefresh=Clock::now();
  recording=false;completeFrame();
  return confirmed;
 }
 void destroyPresentation(){destroyScenePipeline();if(texelView)DestroyBufferView(device,texelView,nullptr);if(qlie::diagnostics&&timeQueries)DestroyQueryPool(device,timeQueries,nullptr);if(displayPipeline)DestroyPipeline(device,displayPipeline,nullptr);for(auto fb:swapFrames)if(fb)DestroyFramebuffer(device,fb,nullptr);for(auto view:swapViews)if(view)DestroyImageView(device,view,nullptr);for(auto sem:finished)if(sem)DestroySemaphore(device,sem,nullptr);if(acquired)DestroySemaphore(device,acquired,nullptr);if(renderPass)DestroyRenderPass(device,renderPass,nullptr);if(swapchain)DestroySwapchainKHR(device,swapchain,nullptr);}
#endif
 void waitGpu() override {
#ifdef __SWITCH__
  if(inFlight){
   const auto start=qlie::diagnostics?Clock::now():Clock::time_point{};
   std::unique_lock<std::mutex> lock(presentMutex);
   presentSignal.wait(lock,[this]{return !presentQueued;});
   inFlight=false;
   if constexpr (qlie::diagnostics) priorWaitMs+=milliseconds(Clock::now()-start);
   if(presentError)std::rethrow_exception(presentError);
  }
#endif
 }
 ~Impl(){
#ifdef __SWITCH__
  if(presenter.joinable()){
   {std::lock_guard<std::mutex> lock(presentMutex);stopPresenter=true;}
   presentSignal.notify_one();presenter.join();
   if(presentError){try{std::rethrow_exception(presentError);}catch(const std::exception&e){QLIE_LOGF("Vulkan presenter stopped: %s\n",e.what());}}
  }
#endif
  if(device&&DeviceWaitIdle){DeviceWaitIdle(device);
#ifdef __SWITCH__
 destroyPresentation();
#endif
 release(texels);release(parameters);release(tileBuffer);release(output);release(staging);release(scanlineHistory);if(fence)DestroyFence(device,fence,nullptr);if(commandPool)DestroyCommandPool(device,commandPool,nullptr);if(pipeline)DestroyPipeline(device,pipeline,nullptr);if(layout)DestroyPipelineLayout(device,layout,nullptr);if(descriptorPool)DestroyDescriptorPool(device,descriptorPool,nullptr);if(setLayout)DestroyDescriptorSetLayout(device,setLayout,nullptr);DestroyDevice(device,nullptr);}
#ifdef __SWITCH__
 if(surface&&DestroySurfaceKHR)DestroySurfaceKHR(instance,surface,nullptr);
#endif
 if(instance&&DestroyInstance)DestroyInstance(instance,nullptr);
#ifndef __SWITCH__
  if(library)dlclose(library);
#endif
 }
 void ensure(Buffer&b,size_t required,size_t minimum=4096,bool preserve=false){
  if(required<=b.size)return;
  if(required>maxBufferBytes)throw std::runtime_error("Vulkan storage buffer capacity exceeded");
  size_t capacity=std::max(minimum,size_t(b.size));while(capacity<required)capacity=std::min(maxBufferBytes,capacity*2);
  capacity=std::min(capacity,maxBufferBytes);VkBufferUsageFlags extra=0;
#ifdef __SWITCH__
  if(&b==&texels&&canOrderPixels)extra=VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
#endif
  Buffer next=buffer(capacity,extra);
#ifdef __SWITCH__
  if(&b==&texels&&texelView){DestroyBufferView(device,texelView,nullptr);texelView=VK_NULL_HANDLE;}
#endif
  if(b.data&&preserve)std::memcpy(next.data,b.data,size_t(b.size));
  release(b);b=next;descriptorsChanged=true;
 }
 void uploadTexture(Span span,const qlie::Bytes& bytes) override {
  ensure(texels,span.offset+span.size,16*1024*1024,true);
  std::memcpy(static_cast<uint8_t*>(texels.data)+span.offset,bytes.data(),span.size);
  texelsDirty=true;
 }
 void barrier(VkPipelineStageFlags source,VkPipelineStageFlags dest,VkAccessFlags from,VkAccessFlags to){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=from;b.dstAccessMask=to;CmdPipelineBarrier(command,source,dest,0,1,&b,0,nullptr,0,nullptr);}
 void prepareGpuCommands(){
  // The next command list and its tiles are CPU-owned while the previous
  // frame presents. Reuse command/GPU upload memory only after its fence.
  planTiles();
  waitForPrevious();
#ifdef __SWITCH__
  markSwapDamage();
#endif
  beginGpuCommands();
 }
 void setInterlaced(bool enabled) override {
  if(interlaced==enabled)return;
  waitForPrevious();
  interlaced=enabled;scanlineValid=false;
  if(enabled&&!scanlineHistory.buffer){
#ifdef __SWITCH__
   scanlineHistory=buffer(CanvasBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false);
#else
   scanlineHistory=buffer(CanvasBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
#endif
  }
#ifdef __SWITCH__
  if(displaySet){VkDescriptorBufferInfo bi{enabled?scanlineHistory.buffer:output.buffer,0,CanvasBytes};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=displaySet;write.dstBinding=1;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bi;UpdateDescriptorSets(device,1,&write,0,nullptr);}
  nativeDisplayCurrent=false;for(auto&pending:swapDamage)pending.set();
#endif
 }
 void copyScanlines(bool seed){
  barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
  if(seed){VkBufferCopy copy{0,0,CanvasBytes};CmdCopyBuffer(command,output.buffer,scanlineHistory.buffer,1,&copy);scanlineValid=true;}
  else{
   std::array<VkBufferCopy,Height/2> rows{};unsigned count=0;scanlinePhase=unsigned((frames+1)&1);
   for(unsigned y=scanlinePhase;y<Height;y+=2){auto offset=VkDeviceSize(y)*Width*4;rows[count++]={offset,offset,Width*4};}
   CmdCopyBuffer(command,output.buffer,scanlineHistory.buffer,count,rows.data());
  }
  barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 }
 void beginGpuCommands(){
  checked(ResetCommandPool(device,commandPool,0),"reset commands");
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  checked(BeginCommandBuffer(command,&bi),"begin commands");
  if(interlaced&&!scanlineValid&&canvasValid)copyScanlines(true);
#ifdef __SWITCH__
  if constexpr (qlie::diagnostics){
   timingFrame=(frames==0||(frames+1)%120==0)?frames+1:0;
   timingHadRaster=!workTiles.empty();
  }
#endif
  if(uploadBase){
   VkBufferCopy copy{0,0,CanvasBytes};CmdCopyBuffer(command,staging.buffer,output.buffer,1,&copy);
   barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  }
  if(!workTiles.empty())recordTiles();
  if(interlaced)copyScanlines(!scanlineValid);
  canvasValid=true;
  if constexpr (qlie::diagnostics) cpuBuildMs=milliseconds(Clock::now()-cpuFrameStart)-priorWaitMs;
 }
#ifdef __SWITCH__
 // A raw decoded frame or a neutral, unscaled opaque bitmap can seed the
 // retained canvas in one transfer. Every tile must agree on this first draw.
 uint32_t directBase()const{
  if(workTiles.size()!=TileCount||!workTiles.front()[2])return UINT32_MAX;
  uint32_t id=workIndices[workTiles.front()[1]];
  const auto&p=current->commands[id].image;
  bool bitmap=(p.meta[2]>>20)==0&&(p.meta[2]&524288u)&&!(p.meta[2]&262144u)&&
              ((p.meta[2]>>8)&255)<2&&p.meta[3]==255&&current->sources[id].opaque&&
              p.meta[0]==Width&&p.meta[1]==Height&&p.p[1][0]==0&&p.p[1][1]==0&&p.colors[0]==0&&p.colors[1]==0xffffff;
  if((p.meta[2]>>20)!=4&&!bitmap)return UINT32_MAX;
  for(auto&h:workTiles)if(h[3]||!h[2]||workIndices[h[1]]!=id)return UINT32_MAX;
  return id;
 }
 void prepareInstances(){
  baseCopy=directBase();
  ranges.fill({UINT32_MAX,0});instances.clear();initTiles.clear();
  neededBounds.assign(current->commands.size(),{Width,Height,0,0});
  for(const auto&h:workTiles){
   unsigned x=h[0]&65535u,y=h[0]>>16;
   unsigned tile=(y/Tile)*Columns+x/Tile;
   if((h[3]&0x80000000u)||!current->covered[tile])initTiles.push_back(h);
   if(!h[2])continue;
   unsigned saveAfter=h[3]&0x7fffffffu;
   ranges[(y/Tile)*Columns+x/Tile]={workIndices[h[1]],saveAfter?workIndices[h[1]+saveAfter-1]+1:0};
   for(unsigned i=0;i<h[2];++i){
    auto&r=neededBounds[workIndices[h[1]+i]];
    r[0]=std::min(r[0],x);r[1]=std::min(r[1],y);r[2]=std::max(r[2],x+Tile);r[3]=std::max(r[3],y+Tile);
   }
  }
  for(unsigned i=0;i<neededBounds.size();++i){
   const auto&r=neededBounds[i];if(r[2]&&i!=baseCopy)instances.push_back({i,r[0]|(r[1]<<16),r[2]|(r[3]<<16)});
  }
 }
 void recordScene(){
  bool fullClear=initTiles.size()==TileCount&&std::all_of(initTiles.begin(),initTiles.end(),[](const auto&h){return h[3]==0;});
  if(baseCopy!=UINT32_MAX){
   barrier(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
           VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
   VkBufferCopy copy{VkDeviceSize(current->commands[baseCopy].image.colors[2])*4,0,CanvasBytes};
   CmdCopyBuffer(command,texels.buffer,output.buffer,1,&copy);
   barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  }else if(fullClear){
   CmdFillBuffer(command,output.buffer,0,CanvasBytes,current->color);
   barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  }else if(!initTiles.empty()){
   CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,sceneInit);
   CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&rasterSet,0,nullptr);
   CmdDispatch(command,uint32_t(initTiles.size()),1,1);if constexpr (qlie::diagnostics) ++dispatches;
   barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  }
  if(instances.empty())return;
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=scenePass;begin.framebuffer=sceneFrame;begin.renderArea={{0,0},{Width,Height}};
  CmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
  CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,scenePipeline);
  CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&rasterSet,0,nullptr);
  CmdDraw(command,6,uint32_t(instances.size()),0,0);
  CmdEndRenderPass(command);
 }
#endif
 void recordTiles(){
  const size_t commandBytes=current->commands.size()*sizeof(DrawData),headerWords=workTiles.size()*4;
  size_t words=headerWords+workIndices.size();
  Push push;push.meta[0]=Width;push.meta[1]=Height;push.meta[3]=current->cleared;push.colors[0]=current->color;
#ifdef __SWITCH__
  if(current->geometry){
   prepareInstances();push.meta[2]=uint32_t(instances.size()*3);push.colors[1]=push.meta[2]+uint32_t(TileCount*2);
   words=push.colors[1]+initTiles.size()*4;
  }
#endif
  ensure(parameters,std::max(sizeof(DrawData),commandBytes));ensure(tileBuffer,words*sizeof(uint32_t));ensure(texels,4);
  if(commandBytes)std::memcpy(parameters.data,current->commands.data(),commandBytes);
  auto*mapped=static_cast<uint32_t*>(tileBuffer.data);
#ifdef __SWITCH__
  if(current->geometry){
   if(!instances.empty())std::memcpy(mapped,instances.data(),instances.size()*sizeof(instances.front()));
   std::memcpy(mapped+push.meta[2],ranges.data(),sizeof(ranges));
   if(!initTiles.empty())std::memcpy(mapped+push.colors[1],initTiles.data(),initTiles.size()*sizeof(initTiles.front()));
  }else
#endif
  {
   writeTileList(mapped);
  }
  flush(parameters);flush(tileBuffer);if(texelsDirty){flush(texels);texelsDirty=false;}
  if(descriptorsChanged){
   VkDescriptorBufferInfo buffers[4]={{texels.buffer,0,texels.size},{output.buffer,0,output.size},{parameters.buffer,0,parameters.size},{tileBuffer.buffer,0,tileBuffer.size}};
   VkWriteDescriptorSet writes[5]{};unsigned count=4;
   for(unsigned n=0;n<4;++n){
    writes[n].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[n].dstSet=rasterSet;writes[n].dstBinding=n;
    writes[n].descriptorCount=1;writes[n].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=&buffers[n];
   }
#ifdef __SWITCH__
   if(scenePipeline){
    if(!texelView){
     viewElements=uint32_t(std::min<VkDeviceSize>(texels.size/4,maxViewElements));
     VkBufferViewCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};info.buffer=texels.buffer;info.format=VK_FORMAT_R32_UINT;info.range=VkDeviceSize(viewElements)*4;
     checked(CreateBufferView(device,&info,nullptr,&texelView),"source texel view");
    }
    writes[4].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[4].dstSet=rasterSet;writes[4].dstBinding=4;
    writes[4].descriptorCount=1;writes[4].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;writes[4].pTexelBufferView=&texelView;count=5;
   }
#endif
   UpdateDescriptorSets(device,count,writes,0,nullptr);descriptorsChanged=false;
  }
  VkPipelineStageFlags shaders=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
#ifdef __SWITCH__
  shaders|=VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  push.colors[2]=viewElements;
#endif
  barrier(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|shaders,shaders,VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
#ifdef __SWITCH__
  if(qlie::diagnostics&&timingFrame&&timeQueries){CmdResetQueryPool(command,timeQueries,0,2);CmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timeQueries,0);}
  if(current->geometry)recordScene();else
#endif
  {
   CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
   CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&rasterSet,0,nullptr);
   CmdDispatch(command,uint32_t(workTiles.size()),1,1);if constexpr (qlie::diagnostics) ++dispatches;
  }
#ifdef __SWITCH__
  if(qlie::diagnostics&&timingFrame&&timeQueries)CmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timeQueries,1);
#endif
 }

#ifndef __SWITCH__
 std::optional<uint64_t> presentNative(bool) override {throw std::runtime_error("Native presentation requires Horizon");}
#endif
 void uploadCanvas(const qlie::Bytes& pixels) override {
  auto& s=*this;
 if(s.output.data){std::memcpy(s.output.data,pixels.data(),CanvasBytes);s.flush(s.output);}
 else{s.ensureStaging();std::memcpy(s.staging.data,pixels.data(),CanvasBytes);s.flush(s.staging);s.uploadBase=true;}
 }
 void readCanvas(qlie::Bytes&pixels) override {
 auto&s=*this;
 if(!s.interlaced&&s.readCoveredSolid(pixels))return;
#ifdef __SWITCH__
 s.current->geometry=s.scenePipeline&&s.current->cleared;
#endif
 s.prepareGpuCommands();
 VkPipelineStageFlags writers=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT;
#ifdef __SWITCH__
 writers|=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
#endif
 auto&display=s.interlaced?s.scanlineHistory:s.output;
 if(!display.data){
  s.ensureStaging();s.barrier(writers,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferCopy copy{0,0,CanvasBytes};s.CmdCopyBuffer(s.command,display.buffer,s.staging.buffer,1,&copy);
  s.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
 }else s.barrier(writers,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
 checked(s.EndCommandBuffer(s.command),"end commands");checked(s.ResetFences(s.device,1,&s.fence),"reset fence");
 VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&s.command;
 checked(s.QueueSubmit(s.queue,1,&submit,s.fence),"submit");checked(s.WaitForFences(s.device,1,&s.fence,VK_TRUE,5000000000ull),"wait");
 auto&readback=display.data?display:s.staging;s.flush(readback,true);std::memcpy(pixels.data(),readback.data,CanvasBytes);
 s.recording=false;s.nativeDisplayCurrent=false;s.completeFrame();
}
};
VulkanRaster::VulkanRaster():GpuRaster(std::make_unique<Impl>()){}
}
