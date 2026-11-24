// SPDX-License-Identifier: GPL-2.0-or-later
#include "deko3d_raster.hpp"
#include "raster_scene.hpp"
#include <switch.h>
#include <deko3d.hpp>
#include <cstdio>
#include <thread>

namespace whj2 {
namespace {
using namespace gpu;
#include "deko_raster.inc"

constexpr unsigned ImageCount = 3;
constexpr uint32_t Mapped = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;

class DekoScene final : public Scene {
 dk::UniqueDevice device;
 dk::UniqueQueue queue;
 dk::UniqueMemBlock imageMemory, code, commandMemory, uniform, output, staging, history;
 dk::UniqueMemBlock texels, drawBuffer, tileBuffer, imageDescriptors;
 dk::UniqueCmdBuf cmd;
 dk::Shader shader;
 std::array<dk::Image,ImageCount> images;
 dk::UniqueSwapchain swapchain;
 std::array<SwapDamage::Mask,ImageCount> damage;
 dk::Fence done;
 bool inFlight=false, interlaced=false, historyValid=false, canvasValid=false;
 Clock::time_point lastRefresh{};

 dk::UniqueMemBlock allocate(size_t bytes,uint32_t flags){
  if(!bytes || bytes>UINT32_MAX-(DK_MEMBLOCK_ALIGNMENT-1))
   throw std::runtime_error("deko3d memory block size");
  auto size=uint32_t((bytes+DK_MEMBLOCK_ALIGNMENT-1)&~size_t(DK_MEMBLOCK_ALIGNMENT-1));
  auto block=dk::MemBlockMaker{device,size}.setFlags(flags).create();
  if(!block)throw std::runtime_error("deko3d memory allocation");
  return dk::UniqueMemBlock{std::move(block)};
 }
 void grow(dk::UniqueMemBlock& block,size_t required,size_t minimum=DK_MEMBLOCK_ALIGNMENT,bool preserve=false){
  size_t old=block?block.getSize():0;
  if(old>=required)return;
  auto next=allocate(std::max({required,minimum,old*2}),Mapped);
  if(preserve&&old)std::memcpy(next.getCpuAddr(),block.getCpuAddr(),old);
  block=std::move(next);
 }
 void initialize() override {
  device=dk::DeviceMaker{}.create();
  if(!device)throw std::runtime_error("deko3d device creation");
  queue=dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics|DkQueueFlags_Compute)
         .setPerWarpScratchMemorySize(std::max<unsigned>(rasterScratchPerWarp,DK_PER_WARP_SCRATCH_MEM_ALIGNMENT)).create();
  if(!queue)throw std::runtime_error("deko3d queue creation");
  code=allocate(sizeof(rasterDksh)+DK_SHADER_CODE_UNUSABLE_SIZE,Mapped|DkMemBlockFlags_Code);
  std::memcpy(code.getCpuAddr(),rasterDksh,sizeof(rasterDksh));
  dk::ShaderMaker{code,0}.initialize(shader);
  if(!shader.isValid()||shader.getStage()!=DkStage_Compute)throw std::runtime_error("deko3d compute shader");
  cmd=dk::CmdBufMaker{device}.create();
  if(!cmd)throw std::runtime_error("deko3d command buffer creation");
  commandMemory=allocate(128*1024,Mapped);
  uniform=allocate(DK_UNIFORM_BUF_ALIGNMENT,Mapped);
  output=allocate(2*CanvasBytes,DkMemBlockFlags_GpuCached);

  dk::ImageLayout layout;
  dk::ImageLayoutMaker{device}.setType(DkImageType_2D).setFormat(DkImageFormat_BGRA8_Unorm)
       .setDimensions(Width,Height).setFlags(DkImageFlags_UsagePresent|DkImageFlags_UsageLoadStore).initialize(layout);
  size_t alignment=layout.getAlignment();
  size_t stride=(layout.getSize()+alignment-1)&~(alignment-1);
  imageMemory=allocate(stride*ImageCount,DkMemBlockFlags_Image|DkMemBlockFlags_GpuCached);
  imageDescriptors=allocate(ImageCount*sizeof(DkImageDescriptor),Mapped);
  auto* descriptors=static_cast<uint8_t*>(imageDescriptors.getCpuAddr());
  std::array<const DkImage*,ImageCount> handles{};
  for(unsigned i=0;i<ImageCount;++i){
   images[i].initialize(layout,imageMemory,uint32_t(stride*i));
   // The packed QLIE texel already has the native BGRA byte order. An R32
   // storage view writes those same four bytes without normalized conversion.
   dk::ImageDescriptor descriptor;
   descriptor.initialize(dk::ImageView{images[i]}.setFormat(DkImageFormat_R32_Uint),true);
   std::memcpy(descriptors+i*sizeof(descriptor),&descriptor,sizeof(descriptor));
   handles[i]=&images[i];
   damage[i].set();
  }
  swapchain=dk::SwapchainMaker{device,nwindowGetDefault(),handles}.create();
  if(!swapchain)throw std::runtime_error("deko3d NWindow swapchain");
  swapchain.setSwapInterval(1);
  std::fprintf(stderr,"DEKO3D native display=%ux%u images=%u scene=tiled-compute\n",Width,Height,ImageCount);
 }
 void waitGpu() override {
  if(!inFlight)return;
  auto start=Clock::now();
  auto result=done.wait(5'000'000'000LL);
  if(result!=DkResult_Success||queue.isInErrorState())throw std::runtime_error("deko3d frame fence "+std::to_string(result));
  inFlight=false;
  priorWaitMs+=milliseconds(Clock::now()-start);
 }
 void uploadTexture(Span span,const qlie::Bytes& bytes) override {
  grow(texels,span.offset+span.size,16*1024*1024,true);
  std::memcpy(static_cast<uint8_t*>(texels.getCpuAddr())+span.offset,bytes.data(),span.size);
 }
 void uploadCanvas(const qlie::Bytes& pixels) override {
  grow(staging,CanvasBytes);
  std::memcpy(staging.getCpuAddr(),pixels.data(),pixels.size());
  uploadBase=true;
 }
 void setInterlaced(bool enabled) override {
  if(interlaced==enabled)return;
  waitForPrevious();
  if(enabled&&!history)history=allocate(CanvasBytes,DkMemBlockFlags_GpuCached);
  interlaced=enabled;
  historyValid=false;
  nativeDisplayCurrent=false;
  for(auto& pending:damage)pending.set();
 }
 SwapDamage::Mask planFrame(){
  if(!recording)throw std::runtime_error("deko3d frame not begun");
  planTiles();
  waitForPrevious();
  auto changed=changedTiles(interlaced);
  for(auto& pending:damage)pending|=changed;
  return changed;
 }
 void recordTiles(int presentSlot){
  const size_t drawBytes=current->commands.size()*sizeof(DrawData);
  const size_t headerWords=workTiles.size()*4;
  grow(drawBuffer,std::max(sizeof(DrawData),drawBytes));
  grow(tileBuffer,(headerWords+workIndices.size())*sizeof(uint32_t));
  grow(texels,sizeof(uint32_t));
  if(drawBytes)std::memcpy(drawBuffer.getCpuAddr(),current->commands.data(),drawBytes);
  writeTileList(static_cast<uint32_t*>(tileBuffer.getCpuAddr()));
  Push parameters;
  parameters.meta[0]=Width;
  parameters.meta[1]=Height;
  parameters.meta[2]=presentSlot>=0;
  parameters.meta[3]=current->cleared;
  parameters.colors[0]=current->color;
  std::memcpy(uniform.getCpuAddr(),&parameters,sizeof(parameters));
  const std::array<DkBufExtents,4> buffers{{
   {texels.getGpuAddr(),texels.getSize()},{output.getGpuAddr(),output.getSize()},
   {drawBuffer.getGpuAddr(),drawBuffer.getSize()},{tileBuffer.getGpuAddr(),tileBuffer.getSize()}
  }};
  const DkShader* program=&shader;
  cmd.bindShaders(DkStageFlag_Compute,program);
  cmd.bindUniformBuffer(DkStage_Compute,0,uniform.getGpuAddr(),DK_UNIFORM_BUF_ALIGNMENT);
  cmd.bindStorageBuffers(DkStage_Compute,0,buffers);
  cmd.bindImageDescriptorSet(imageDescriptors.getGpuAddr(),ImageCount);
  const auto handle=dkMakeImageHandle(presentSlot>=0?unsigned(presentSlot):0u);
  cmd.bindImages(DkStage_Compute,0,handle);
  cmd.dispatchCompute(uint32_t(workTiles.size()),1,1);
  ++dispatches;
  cmd.barrier(DkBarrier_Primitives,0);
 }
 void recordCanvas(int presentSlot){
  cmd.clear();
  cmd.addMemory(commandMemory,0,commandMemory.getSize());
  if(interlaced&&!historyValid&&canvasValid){
   cmd.copyBuffer(output.getGpuAddr(),history.getGpuAddr(),CanvasBytes);
   cmd.barrier(DkBarrier_Full,0);
   historyValid=true;
  }
  if(uploadBase){
   cmd.copyBuffer(staging.getGpuAddr(),output.getGpuAddr(),CanvasBytes);
   cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Shader);
  }
  if(!workTiles.empty())recordTiles(presentSlot);
  if(interlaced){
   if(!historyValid)cmd.copyBuffer(output.getGpuAddr(),history.getGpuAddr(),CanvasBytes);
   else for(unsigned y=(frames+1)&1;y<Height;y+=2){
    size_t offset=size_t(y)*Width*4;
    cmd.copyBuffer(output.getGpuAddr()+offset,history.getGpuAddr()+offset,Width*4);
   }
   historyValid=true;
  }
  canvasValid=true;
 }
 DkGpuAddr displayAddress(){return interlaced?history.getGpuAddr():output.getGpuAddr();}
 void submit(bool flushForCpu){
  cmd.signalFence(done,flushForCpu);
  queue.submitCommands(cmd.finishList());
  inFlight=true;
 }
 void finishFrame(){
  cpuBuildMs=milliseconds(Clock::now()-cpuFrameStart)-priorWaitMs;
  recording=false;
  completeFrame();
 }
 std::optional<uint64_t> presentNative(bool refreshUnchanged) override {
  const auto changed=planFrame();
  if(!interlaced&&!refreshUnchanged&&nativeDisplayCurrent&&current->cleared&&workTiles.empty()){
   std::this_thread::sleep_until(lastRefresh+std::chrono::nanoseconds(16666667));
   lastRefresh=Clock::now();
   reusedDisplay=true;
   finishFrame();
   return std::nullopt;
  }
  const int slot=queue.acquireImage(swapchain);
  if(slot<0||unsigned(slot)>=ImageCount)throw std::runtime_error("deko3d swapchain acquisition");
  // Only write an acquired image if this dispatch paints every tile that
  // image has missed. Its other tiles already hold the correct final pixels.
  const bool direct=!interlaced&&current->cleared&&changed.any()&&(damage[slot]&~changed).none();
  recordCanvas(direct?slot:-1);
  unsigned copiedTiles=0;
  if(!direct){
   for(const auto& rect:SwapDamage::regions(damage[slot])){
    DkImageRect dst{rect.x*Tile,rect.y*Tile,0,rect.width*Tile,rect.height*Tile,1};
    size_t offset=(size_t(dst.y)*Width+dst.x)*4;
    // DkCopyBuf rowLength is a byte pitch, including for partial rectangles.
    cmd.copyBufferToImage({displayAddress()+offset,Width*sizeof(uint32_t),0},dk::ImageView{images[slot]},dst);
    copiedTiles+=rect.width*rect.height;
   }
  }
  submit(false);
  queue.presentImage(swapchain,slot);
  if(frames==0||(frames+1)%120==0)
   std::fprintf(stderr,"DEKO3D PRESENT frame=%llu direct_tiles=%zu copy_tiles=%u\n",
                (unsigned long long)(frames+1),direct?workTiles.size():size_t(0),copiedTiles);
  damage[slot].reset();
  nativeDisplayCurrent=true;
  lastRefresh=Clock::now();
  finishFrame();
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(lastRefresh.time_since_epoch()).count());
 }
 void readCanvas(qlie::Bytes& pixels) override {
  planFrame();
  grow(staging,CanvasBytes);
  recordCanvas(-1);
  cmd.copyBuffer(displayAddress(),staging.getGpuAddr(),CanvasBytes);
  submit(true);
  queue.flush();
  waitForPrevious();
  std::memcpy(pixels.data(),staging.getCpuAddr(),CanvasBytes);
  nativeDisplayCurrent=false;
  finishFrame();
 }
public:
 DekoScene():Scene("DEKO3D"){}
 ~DekoScene() override {if(queue)queue.waitIdle();}
};
}
Deko3dRaster::Deko3dRaster():GpuRaster(std::make_unique<DekoScene>()){}
}
