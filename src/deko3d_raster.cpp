// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "deko3d_raster.hpp"
#include "deko_tile_batches.hpp"
#include <switch.h>
#include <deko3d.hpp>
#include <cstdio>
#include <thread>
extern "C" {
#include <libavutil/frame.h>
#include "video.h"
}

namespace whj2 {
namespace {
using namespace gpu;
#include "deko_raster.inc"
#include "deko_motion.inc"
#include "deko_restore.inc"
#include "deko_base.inc"
#include "deko_video.inc"
#include "deko_field.inc"

constexpr unsigned ImageCount = 3;
// NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD returns the PMU's device-wide load in tenths of a percent.
constexpr uint32_t GetGpuLoad = _NV_IOR('G',0x15,uint32_t);
constexpr uint32_t Mapped = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;

class DekoScene final : public Scene {
 struct BufferRelease {void operator()(AVBufferRef* p)const{av_buffer_unref(&p);}};
 struct FrameRelease {void operator()(AVFrame* p)const{av_frame_free(&p);}};
 struct VideoMapping {
  std::unique_ptr<AVBufferRef,BufferRelease> pool;
  dk::UniqueMemBlock memory;
  std::array<dk::ImageDescriptor,2> planes;
 };
 struct VideoTexture {
  std::unique_ptr<AVFrame,FrameRelease> frame;
  const VideoMapping* mapping=nullptr;
  Span span;
  uint64_t identity=0,lastUsed=0;
  unsigned height=0;
  bool converted=false;
 };
 dk::UniqueDevice device;
 dk::UniqueQueue queue;
 dk::UniqueMemBlock imageMemory, code, commandMemory, uniform, output, staging, history;
 dk::UniqueMemBlock texels, drawBuffer, tileBuffer, imageDescriptors, repairTiles;
 dk::UniqueCmdBuf cmd;
 dk::Shader shader,motionShader,restoreShader,baseShader,videoShader,fieldShader;
 std::map<uint32_t,VideoMapping> videoMaps;
 std::map<uint64_t,VideoTexture> videoTextures;
 dk::UniqueMemBlock videoDescriptors,videoUniforms;
 DekoTileBatches batches;
 struct alignas(16) CounterReport {uint64_t value,timestamp;};
 static_assert(sizeof(CounterReport)==16);
 dk::UniqueMemBlock reports;
 uint32_t gpuControl=UINT32_MAX;
 uint64_t sampleFrame=0;
 size_t sampleDirect=0;
 unsigned sampleCopied=0;
 size_t sampleTiles=0,sampleMotionTiles=0,sampleBaseTiles=0;
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
 void growTexels(size_t required){
  if(texels&&texels.getSize()>=required)return;
  grow(texels,required,16*1024*1024,true);
  for(auto& entry:videoTextures)entry.second.converted=false;
 }
 void initialize() override {
  device=dk::DeviceMaker{}.create();
  if(!device)throw std::runtime_error("deko3d device creation");
  queue=dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics|DkQueueFlags_Compute)
         .setPerWarpScratchMemorySize(std::max<unsigned>({rasterScratchPerWarp,motionScratchPerWarp,restoreScratchPerWarp,baseScratchPerWarp,videoScratchPerWarp,fieldScratchPerWarp,DK_PER_WARP_SCRATCH_MEM_ALIGNMENT})).create();
  if(!queue)throw std::runtime_error("deko3d queue creation");
  static_assert(sizeof(rasterDksh)%DK_SHADER_CODE_ALIGNMENT==0);
  static_assert(sizeof(motionDksh)%DK_SHADER_CODE_ALIGNMENT==0);
  constexpr size_t restoreOffset=sizeof(rasterDksh)+sizeof(motionDksh);
  static_assert(sizeof(restoreDksh)%DK_SHADER_CODE_ALIGNMENT==0);
  constexpr size_t baseOffset=restoreOffset+sizeof(restoreDksh);
  static_assert(sizeof(baseDksh)%DK_SHADER_CODE_ALIGNMENT==0);
  constexpr size_t videoOffset=baseOffset+sizeof(baseDksh);
  static_assert(sizeof(videoDksh)%DK_SHADER_CODE_ALIGNMENT==0);
  constexpr size_t fieldOffset=videoOffset+sizeof(videoDksh);
  code=allocate(fieldOffset+sizeof(fieldDksh)+DK_SHADER_CODE_UNUSABLE_SIZE,Mapped|DkMemBlockFlags_Code);
  std::memcpy(code.getCpuAddr(),rasterDksh,sizeof(rasterDksh));
  std::memcpy(static_cast<uint8_t*>(code.getCpuAddr())+sizeof(rasterDksh),motionDksh,sizeof(motionDksh));
  std::memcpy(static_cast<uint8_t*>(code.getCpuAddr())+restoreOffset,restoreDksh,sizeof(restoreDksh));
  std::memcpy(static_cast<uint8_t*>(code.getCpuAddr())+baseOffset,baseDksh,sizeof(baseDksh));
  std::memcpy(static_cast<uint8_t*>(code.getCpuAddr())+videoOffset,videoDksh,sizeof(videoDksh));
  std::memcpy(static_cast<uint8_t*>(code.getCpuAddr())+fieldOffset,fieldDksh,sizeof(fieldDksh));
  dk::ShaderMaker{code,fieldOffset}.initialize(fieldShader);
  if(!fieldShader.isValid()||fieldShader.getStage()!=DkStage_Compute)throw std::runtime_error("deko3d field presentation shader");
  dk::ShaderMaker{code,videoOffset}.initialize(videoShader);
  if(!videoShader.isValid()||videoShader.getStage()!=DkStage_Compute)throw std::runtime_error("deko3d NV12 movie shader");
  dk::ShaderMaker{code,baseOffset}.initialize(baseShader);
  if(!baseShader.isValid()||baseShader.getStage()!=DkStage_Compute)throw std::runtime_error("deko3d opaque tile shader");
  dk::ShaderMaker{code,restoreOffset}.initialize(restoreShader);
  if(!restoreShader.isValid()||restoreShader.getStage()!=DkStage_Compute)throw std::runtime_error("deko3d tile restore shader");
  dk::ShaderMaker{code,0}.initialize(shader);
  dk::ShaderMaker{code,sizeof(rasterDksh)}.initialize(motionShader);
  if(!motionShader.isValid()||motionShader.getStage()!=DkStage_Compute)throw std::runtime_error("deko3d motion shader");
  if(!shader.isValid()||shader.getStage()!=DkStage_Compute)throw std::runtime_error("deko3d compute shader");
  cmd=dk::CmdBufMaker{device}.create();
  if(!cmd)throw std::runtime_error("deko3d command buffer creation");
  commandMemory=allocate(128*1024,Mapped);
  uniform=allocate(DK_UNIFORM_BUF_ALIGNMENT,Mapped);
  if constexpr (qlie::diagnostics) reports=allocate(4*sizeof(CounterReport),DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuUncached);
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
  if constexpr (qlie::diagnostics){
   uint32_t fd;
   if(R_SUCCEEDED(nvOpen(&fd,"/dev/nvhost-ctrl-gpu")))gpuControl=fd;
  }
  QLIE_LOGF("DEKO3D native display=%ux%u images=%u scene=tiled-compute\n",Width,Height,ImageCount);
 }
 double gpuLoadPercent()const{
  uint32_t value=0;
  if(gpuControl==UINT32_MAX||R_FAILED(nvIoctl(gpuControl,GetGpuLoad,&value))||value>1000)return -1.0;
  return double(value)/10.0;
 }
 void timestamp(unsigned index,DkCounter counter){
  cmd.reportCounter(counter,reports.getGpuAddr()+index*sizeof(CounterReport));
 }
 double stageMs(unsigned first){
  auto* samples=static_cast<const volatile CounterReport*>(reports.getCpuAddr());
  const uint64_t start=samples[first].timestamp,end=samples[first+1].timestamp;
  return start&&end>=start?double(dkTimestampToNs(end-start))/1e6:-1.0;
 }
 void waitGpu() override {
  if(!inFlight)return;
  const auto start=qlie::diagnostics?Clock::now():Clock::time_point{};
  auto result=done.wait(5'000'000'000LL);
  if(result!=DkResult_Success||queue.isInErrorState())throw std::runtime_error("deko3d frame fence "+std::to_string(result));
  inFlight=false;
  if constexpr (qlie::diagnostics) priorWaitMs+=milliseconds(Clock::now()-start);
  if(qlie::diagnostics&&sampleFrame){
   traceFrame("DEKO3D PRESENT frame=%llu direct_tiles=%zu copy_tiles=%u kernel=%s motion_tiles=%zu raster_gpu_ms=%.3f copy_gpu_ms=%.3f gpu_load_pct=%.1f base_tiles=%zu\n",
                (unsigned long long)sampleFrame,sampleDirect,sampleCopied,
                !sampleTiles?"idle":!sampleMotionTiles?"general":sampleMotionTiles==sampleTiles?"motion":"mixed",sampleMotionTiles,
                sampleTiles?stageMs(0):0.0,sampleCopied?stageMs(2):0.0,gpuLoadPercent(),sampleBaseTiles);
   sampleFrame=0;
  }
 }
 void uploadTexture(Span span,const qlie::Bytes& bytes) override {
  growTexels(span.offset+span.size);
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
 bool supportsVideoSurfaces()const override{return true;}
 std::optional<uint32_t> videoTexture(const KVideoSurface& s,uint64_t& identity) override {
  if(!kvideo_scene_surface_size(s.width,s.height)||s.pitch[0]<s.width||s.pitch[1]<s.width||
     s.pitch[1]%2||!s.frame||!s.frame->hw_frames_ctx||!s.storage||!s.storage_size||
     (reinterpret_cast<uintptr_t>(s.storage)|s.storage_size)%DK_MEMBLOCK_ALIGNMENT||
     s.chroma_offset>=s.storage_size||!s.version)return std::nullopt;
  auto existing=videoTextures.find(s.version);
  if(existing!=videoTextures.end()){
   existing->second.lastUsed=frames+1;
   identity=existing->second.identity;
   return uint32_t(existing->second.span.offset/sizeof(uint32_t));
  }
  auto found=videoMaps.find(s.handle);
  if(found==videoMaps.end()){
   const std::array<uint32_t,2> offsets{{0,s.chroma_offset}};
   const std::array<DkImageFormat,2> formats{{DkImageFormat_R8_Uint,DkImageFormat_RG8_Uint}};
   std::array<dk::ImageLayout,2> layouts;
   for(unsigned i=0;i<layouts.size();++i){
    dk::ImageLayoutMaker maker{device};
    maker.setType(DkImageType_2D).setFormat(formats[i]).setDimensions(s.pitch[i]>>i,s.height>>i)
         .setFlags(DkImageFlags_UsageLoadStore|DkImageFlags_Usage2DEngine|
                   (s.linear?DkImageFlags_PitchLinear:DkImageFlags_UsageVideo));
    if(s.linear)maker.setPitchStride(s.pitch[i]);
    maker.initialize(layouts[i]);
    const auto alignment=layouts[i].getAlignment();
    const auto room=i?s.storage_size-offsets[i]:s.chroma_offset;
    if(!alignment||offsets[i]%alignment||!layouts[i].getSize()||layouts[i].getSize()>room)return std::nullopt;
   }
   VideoMapping map;
   map.pool.reset(av_buffer_ref(s.frame->hw_frames_ctx));
   if(!map.pool)return std::nullopt;
   map.memory=dk::MemBlockMaker{device,s.storage_size}.setFlags(Mapped|DkMemBlockFlags_Image).setStorage(s.storage).create();
   if(!map.memory)return std::nullopt;
   for(unsigned i=0;i<layouts.size();++i){
    dk::Image image;
    image.initialize(layouts[i],map.memory,offsets[i]);
    map.planes[i].initialize(dk::ImageView{image},true);
   }
   if(videoMaps.empty())QLIE_LOGF("DEKO3D VIDEO mapped NVDEC NV12 planes %ux%u\n",s.width,s.height);
   found=videoMaps.emplace(s.handle,std::move(map)).first;
  }
  VideoTexture texture;
  texture.frame.reset(av_frame_clone(s.frame));
  if(!texture.frame)throw std::runtime_error("deko3d video frame reference");
  texture.mapping=&found->second;
  texture.height=s.height;
  texture.span=allocateSpan(size_t(s.width)*s.height*sizeof(uint32_t));
  texture.identity=identity=nextUploadId++;
  texture.lastUsed=frames+1;
  const auto address=uint32_t(texture.span.offset/sizeof(uint32_t));
  videoTextures.emplace(s.version,std::move(texture));
  return address;
 }
 bool beginVideoSurface(const KVideoSurface& s) override {
  if(s.width!=Width||s.height!=Height)return false;
  uint64_t identity=0;
  auto address=videoTexture(s,identity);
  if(!address)return false;
  beginFrame(true,0);
  appendVideo(*address,identity);
  return true;
 }
 void recordVideos(){
  if(videoTextures.empty())return;
  size_t textureBytes=0;
  for(const auto& entry:videoTextures)textureBytes=std::max(textureBytes,entry.second.span.offset+entry.second.span.size);
  growTexels(textureBytes);
  const size_t count=std::count_if(videoTextures.begin(),videoTextures.end(),[](const auto& e){return !e.second.converted;});
  if(!count)return;
  constexpr size_t planeBytes=sizeof(VideoMapping::planes);
  static_assert(planeBytes%DK_IMAGE_DESCRIPTOR_ALIGNMENT==0);
  grow(videoDescriptors,count*planeBytes);
  grow(videoUniforms,count*DK_UNIFORM_BUF_ALIGNMENT);
  // receive_frame waited for NVDEC; invalidate cached reads of reused pool memory.
  cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Image);
  cmd.bindStorageBuffer(DkStage_Compute,0,texels.getGpuAddr(),texels.getSize());
  const DkShader* program=&videoShader;
  cmd.bindShaders(DkStageFlag_Compute,program);
  size_t slot=0;
  for(auto& entry:videoTextures){
   auto& texture=entry.second;
   if(texture.converted)continue;
   const size_t descriptors=slot*planeBytes,parameters=slot*DK_UNIFORM_BUF_ALIGNMENT;
   std::memcpy(static_cast<uint8_t*>(videoDescriptors.getCpuAddr())+descriptors,texture.mapping->planes.data(),planeBytes);
   const uint32_t firstWord=uint32_t(texture.span.offset/sizeof(uint32_t));
   std::memcpy(static_cast<uint8_t*>(videoUniforms.getCpuAddr())+parameters,&firstWord,sizeof(firstWord));
   cmd.bindUniformBuffer(DkStage_Compute,0,videoUniforms.getGpuAddr()+parameters,DK_UNIFORM_BUF_ALIGNMENT);
   cmd.bindImageDescriptorSet(videoDescriptors.getGpuAddr()+descriptors,2);
   const std::array<DkResHandle,2> handles{{dkMakeImageHandle(0),dkMakeImageHandle(1)}};
   cmd.bindImages(DkStage_Compute,0,handles);
   // The video kernel uses 16x8 threads, each converting a 2x2 NV12 block.
   static_assert(Width%32==0);
   cmd.dispatchCompute(Width/32,texture.height/16,1);
   texture.converted=true;
   if constexpr (qlie::diagnostics) ++dispatches;
   ++slot;
  }
  cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Shader);
 }
 SwapDamage::Mask planFrame(){
  if(!recording)throw std::runtime_error("deko3d frame not begun");
  planTiles();
  waitForPrevious();
  // The previous submission can no longer sample these NVDEC allocations.
  for(auto it=videoTextures.begin();it!=videoTextures.end();){
   if(it->second.lastUsed==frames+1){++it;continue;}
   freeSpan(it->second.span);
   it=videoTextures.erase(it);
  }
  for(auto it=videoMaps.begin();it!=videoMaps.end();){
   auto* pool=it->second.pool->data;
   const bool active=std::any_of(videoTextures.begin(),videoTextures.end(),[pool](const auto& e){
    return e.second.mapping->pool->data==pool;
   });
   if(active)++it;else it=videoMaps.erase(it);
  }
  auto changed=changedTiles(interlaced);
  for(auto& pending:damage)pending|=changed;
  return changed;
 }
 void recordTiles(int presentSlot){
  const size_t drawBytes=current->commands.size()*sizeof(DrawData);
  const size_t headerWords=workTiles.size()*4;
  grow(drawBuffer,std::max(sizeof(DrawData),drawBytes));
  grow(tileBuffer,(headerWords+workIndices.size()+2*(DekoTileBatches::AlignmentWords-1))*sizeof(uint32_t));
  grow(texels,sizeof(uint32_t));
  if(drawBytes)std::memcpy(drawBuffer.getCpuAddr(),current->commands.data(),drawBytes);
  batches.write(*this,static_cast<uint32_t*>(tileBuffer.getCpuAddr()));
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
  cmd.bindUniformBuffer(DkStage_Compute,0,uniform.getGpuAddr(),DK_UNIFORM_BUF_ALIGNMENT);
  cmd.bindStorageBuffers(DkStage_Compute,0,buffers);
  cmd.bindImageDescriptorSet(imageDescriptors.getGpuAddr(),ImageCount);
  const auto handle=dkMakeImageHandle(presentSlot>=0?unsigned(presentSlot):0u);
  cmd.bindImages(DkStage_Compute,0,handle);
  if(qlie::diagnostics&&sampleFrame){
   sampleTiles=workTiles.size();sampleBaseTiles=batches.baseTiles;sampleMotionTiles=batches.motionTiles+sampleBaseTiles;
   timestamp(0,DkCounter_TimestampPipelineTop);
  }
  auto rasterBatch=[&](const dk::Shader& program,unsigned count,uint32_t start){
   if(!count)return;
   const auto offset=start*sizeof(uint32_t);
   if(offset)cmd.bindStorageBuffer(DkStage_Compute,3,tileBuffer.getGpuAddr()+offset,tileBuffer.getSize()-offset);
   const DkShader* bound=&program;
   cmd.bindShaders(DkStageFlag_Compute,bound);
   cmd.dispatchCompute(count,1,1);
   if constexpr (qlie::diagnostics) ++dispatches;
  };
  rasterBatch(motionShader,batches.motionTiles,0);
  rasterBatch(baseShader,batches.baseTiles,batches.baseOffsetWords);
  rasterBatch(shader,unsigned(workTiles.size())-batches.motionTiles-batches.baseTiles,batches.generalOffsetWords);
  cmd.barrier(DkBarrier_Primitives,0);
  if(qlie::diagnostics&&sampleFrame)timestamp(1,DkCounter_Timestamp);
 }
 void recordCanvas(int presentSlot,bool fieldOutput){
  cmd.clear();
  cmd.addMemory(commandMemory,0,commandMemory.getSize());
  if(fieldOutput&&!historyValid&&canvasValid){
   cmd.copyBuffer(output.getGpuAddr(),history.getGpuAddr(),CanvasBytes);
   cmd.barrier(DkBarrier_Full,0);
   historyValid=true;
  }
  if(uploadBase){
   cmd.copyBuffer(staging.getGpuAddr(),output.getGpuAddr(),CanvasBytes);
   cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Shader);
  }
  if(!workTiles.empty()){recordVideos();recordTiles(presentSlot);}
  if(fieldOutput){
   if(!historyValid)cmd.copyBuffer(output.getGpuAddr(),history.getGpuAddr(),CanvasBytes);
   else {
    static_assert(Width%16==0&&Width*4%256==0);
    const auto offset=uint32_t(((frames+1)&1)*Width*4);
    const std::array<DkBufExtents,2> buffers{{
     {output.getGpuAddr()+offset,uint32_t(CanvasBytes)-offset},{history.getGpuAddr()+offset,uint32_t(CanvasBytes)-offset}
    }};
    cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Shader);
    cmd.bindStorageBuffers(DkStage_Compute,0,buffers);
    const DkShader* program=&fieldShader;
    cmd.bindShaders(DkStageFlag_Compute,program);
    cmd.dispatchCompute(Width/16,(Height/2+15)/16,1);
    cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Shader);
   }
   historyValid=true;
  }
  canvasValid=true;
 }
 DkGpuAddr displayAddress(){return interlaced?history.getGpuAddr():output.getGpuAddr();}
 void repairDisplay(unsigned slot,const SwapDamage::Mask& stale){
  if(stale.none())return;
  const auto rects=SwapDamage::regions(stale,false);
  if(qlie::diagnostics&&sampleFrame){
   sampleCopied=unsigned(stale.count());
   timestamp(2,DkCounter_TimestampPipelineTop);
  }
  if(rects.size()>64){
   // A single workgroup restores each missing tile. A bounding rectangle
   // here can include both unchanged pixels and tiles just rasterized.
   const size_t count=stale.count();
   grow(repairTiles,count*sizeof(uint32_t));
   auto* origins=static_cast<uint32_t*>(repairTiles.getCpuAddr());
   for(auto rect:rects)for(unsigned y=rect.y;y<rect.y+rect.height;++y)
    for(unsigned x=rect.x;x<rect.x+rect.width;++x)*origins++=(x*Tile)|((y*Tile)<<16);
   const std::array<DkBufExtents,2> buffers{{
    {displayAddress(),CanvasBytes},{repairTiles.getGpuAddr(),uint32_t(count*sizeof(uint32_t))}
   }};
   if(interlaced||uploadBase)cmd.barrier(DkBarrier_Full,DkInvalidateFlags_Shader);
   cmd.bindStorageBuffers(DkStage_Compute,0,buffers);
   cmd.bindImageDescriptorSet(imageDescriptors.getGpuAddr(),ImageCount);
   cmd.bindImages(DkStage_Compute,0,dkMakeImageHandle(slot));
   const DkShader* program=&restoreShader;
   cmd.bindShaders(DkStageFlag_Compute,program);
   cmd.dispatchCompute(uint32_t(count),1,1);
   if constexpr (qlie::diagnostics) ++dispatches;
   cmd.barrier(DkBarrier_Primitives,0);
  }else for(const auto& rect:rects){
   DkImageRect dst{rect.x*Tile,rect.y*Tile,0,rect.width*Tile,rect.height*Tile,1};
   size_t offset=(size_t(dst.y)*Width+dst.x)*4;
   // DkCopyBuf rowLength is a byte pitch, including for partial rectangles.
   cmd.copyBufferToImage({displayAddress()+offset,Width*sizeof(uint32_t),0},dk::ImageView{images[slot]},dst);
  }
  if(qlie::diagnostics&&sampleFrame){
   cmd.barrier(DkBarrier_Full,0);
   timestamp(3,DkCounter_Timestamp);
  }
 }
 void submit(bool flushForCpu){
  cmd.signalFence(done,flushForCpu||(qlie::diagnostics&&sampleFrame!=0));
  queue.submitCommands(cmd.finishList());
  inFlight=true;
 }
 void finishFrame(){
  if constexpr (qlie::diagnostics) cpuBuildMs=milliseconds(Clock::now()-cpuFrameStart)-priorWaitMs;
  recording=false;
  completeFrame();
 }
 std::optional<uint64_t> presentNative(bool refreshUnchanged) override {
  const auto changed=planFrame();
  if(!interlaced&&!refreshUnchanged&&nativeDisplayCurrent&&current->cleared&&workTiles.empty()){
   std::this_thread::sleep_until(lastRefresh+std::chrono::nanoseconds(16666667));
   lastRefresh=Clock::now();
   if constexpr (qlie::diagnostics) reusedDisplay=true;
   if(qlie::diagnostics&&(frames==0||(frames+1)%120==0))
    traceFrame("DEKO3D IDLE frame=%llu gpu_load_pct=%.1f\n",(unsigned long long)(frames+1),gpuLoadPercent());
   finishFrame();
   return std::nullopt;
  }
  const int slot=queue.acquireImage(swapchain);
  if(slot<0||unsigned(slot)>=ImageCount)throw std::runtime_error("deko3d swapchain acquisition");
  // The changed tiles get their final pixels in the raster dispatch. Only
  // older misses in this particular swap image need the retained canvas.
  const bool direct=!interlaced&&current->cleared&&changed.any();
  const auto stale=direct?damage[slot]&~changed:damage[slot];
  if(qlie::diagnostics&&(frames==0||(frames+1)%120==0)){
   sampleFrame=frames+1;sampleDirect=direct?workTiles.size():0;
   sampleTiles=0;sampleMotionTiles=0;sampleBaseTiles=0;sampleCopied=0;
   std::memset(reports.getCpuAddr(),0,4*sizeof(CounterReport));
  }
  recordCanvas(direct?slot:-1,interlaced);
  repairDisplay(unsigned(slot),stale);
  submit(false);
  queue.presentImage(swapchain,slot);
  damage[slot].reset();
  nativeDisplayCurrent=true;
  lastRefresh=Clock::now();
  finishFrame();
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(lastRefresh.time_since_epoch()).count());
 }
 void readCanvas(qlie::Bytes& pixels) override {
  if(readCoveredSolid(pixels)){
   for(auto& entry:videoTextures)if(entry.second.lastUsed==frames+1)entry.second.lastUsed=frames;
   return;
  }
  planFrame();
  grow(staging,CanvasBytes);
  recordCanvas(-1,false);
  cmd.copyBuffer(output.getGpuAddr(),staging.getGpuAddr(),CanvasBytes);
  submit(true);
  queue.flush();
  waitForPrevious();
  std::memcpy(pixels.data(),staging.getCpuAddr(),CanvasBytes);
  nativeDisplayCurrent=false;
  finishFrame();
 }
public:
 DekoScene():Scene("DEKO3D",true){compareAlignedBitmaps=true;}
 ~DekoScene() override {
  if(queue)queue.waitIdle();
  if(qlie::diagnostics&&gpuControl!=UINT32_MAX)nvClose(gpuControl);
 }
};
}
Deko3dRaster::Deko3dRaster():GpuRaster(std::make_unique<DekoScene>()){}
}
