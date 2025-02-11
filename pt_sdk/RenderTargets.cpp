/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define USE_DENOISING_NRD 1     // <- this define should come from elsewhere

#include "PathTracer/Config.hlsli"

#include "RenderTargets.h"

#include <donut/engine/FramebufferFactory.h>

#include <donut/core/math/math.h>
using namespace donut::math;
#include "PathTracer/PathTracerShared.h"

#include "PathTracer/StablePlanes.hlsli"

using namespace dm;
using namespace donut;
using namespace donut::math;

void RenderTargets::Init(
        nvrhi::IDevice* device,
        donut::math::int2 renderSize, 
        donut::math::int2 displaySize,
        bool enableMotionVectors,
        bool useReverseProjection,
        int backbufferCount)
{
    m_UseReverseProjection = useReverseProjection;
    m_BackbufferCount = backbufferCount;
    m_Device = device;
    m_RenderSize = renderSize;
    m_DisplaySize = displaySize;

    nvrhi::TextureDesc desc;
    desc.width = renderSize.x;
    desc.height = renderSize.y;

    desc.isVirtual = false; //device->queryFeatureSupport(nvrhi::Feature::VirtualResources); <- codepath not up to date, needs refactoring

    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.isRenderTarget = true;
    desc.useClearValue = true;
    desc.clearValue = nvrhi::Color(0.f);
    desc.sampleCount = m_SampleCount;
    desc.dimension = m_SampleCount > 1 ? nvrhi::TextureDimension::Texture2DMS : nvrhi::TextureDimension::Texture2D;
    desc.keepInitialState = true;
    desc.isTypeless = false;
    desc.isUAV = false;
    desc.mipLevels = 1;

#if 0 // classic hardware depth buffer
    const nvrhi::Format depthFormats[] = {
        nvrhi::Format::D24S8,
        nvrhi::Format::D32S8,
        nvrhi::Format::D32,
        nvrhi::Format::D16 };
    
    const nvrhi::FormatSupport depthFeatures =
        nvrhi::FormatSupport::Texture |
        nvrhi::FormatSupport::DepthStencil |
        nvrhi::FormatSupport::ShaderLoad;
    
    desc.format = nvrhi::utils::ChooseFormat(device, depthFeatures, depthFormats, std::size(depthFormats));
    desc.isTypeless = true;
    desc.initialState = nvrhi::ResourceStates::DepthWrite;
    desc.clearValue = useReverseProjection ? nvrhi::Color(0.f) : nvrhi::Color(1.f);
    desc.debugName = "GBufferDepth";
    Depth = device->createTexture(desc);
#else // same value but not actually used as hardware depth buffer, but exported from path tracer instead
    desc.format = nvrhi::Format::R32_FLOAT;
    desc.isTypeless = false;
    desc.isUAV = true;
    desc.isRenderTarget = true;
    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.debugName = "Depth";
    desc.clearValue = useReverseProjection ? nvrhi::Color(0.f) : nvrhi::Color(1.f);
    desc.useClearValue = true;
    Depth = device->createTexture(desc);
#endif

    desc.isTypeless = false;
    desc.isRenderTarget = true;
    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.clearValue = nvrhi::Color(0.f);
    desc.isUAV = true;
    desc.format = nvrhi::Format::RGBA16_FLOAT;	// allow for .z component too
    desc.debugName = "ScreenMotionVectors";
    ScreenMotionVectors = device->createTexture(desc);
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = "DenoiserMotionVectors";
    DenoiserMotionVectors = device->createTexture(desc);

    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = "StableRadianceBuffer";
    StableRadiance = device->createTexture(desc);
    desc.format = nvrhi::Format::RGBA32_UINT;
    desc.debugName = "PingStablePlanesHeader";
    StablePlanesHeader = device->createTexture(desc);
    desc.debugName = "PongStablePlanesHeader";
    PrevStablePlanesHeader = device->createTexture(desc);

    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = "DenoiserDiffRadianceHitDist";
    DenoiserDiffRadianceHitDist = device->createTexture(desc);
    desc.debugName = "DenoiserOutDiffRadianceHitDist";
    for (int i = 0; i < cStablePlaneCount; ++i)
        DenoiserOutDiffRadianceHitDist[i] = device->createTexture(desc);

    desc.debugName = "DenoiserSpecRadianceHitDist";
    DenoiserSpecRadianceHitDist = device->createTexture(desc);
    desc.debugName = "DenoiserOutSpecRadianceHitDist";
    for (int i = 0; i < cStablePlaneCount; ++i)
        DenoiserOutSpecRadianceHitDist[i] = device->createTexture(desc);

    //desc.debugName = "DenoiserPackedBSDFEstimate";
    //desc.format = nvrhi::Format::RGBA32_UINT;
    //DenoiserPackedBSDFEstimate = device->createTexture(desc);
    desc.format = nvrhi::Format::RGBA32_FLOAT;
    desc.debugName = "RtxdiOutDirectionValid";
    RtxdiOutDirectionValidSample = device->createTexture(desc);
    desc.debugName = "RtxdiOutLiDistance";
    RtxdiOutLiDist = device->createTexture(desc);
    desc.debugName = "DebugVizOutput";
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f);   // avoid the debug layer warnings... not actually cleared except for debug purposes
    DebugVizOutput = device->createTexture(desc);
#if ENABLE_DEBUG_VIZUALISATION
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.debugName = "DenoiserOutValidation";
    DenoiserOutValidation = device->createTexture(desc);
#endif

    desc.format = nvrhi::Format::R32_FLOAT;
    desc.debugName = "DenoiserViewspaceZ";
    DenoiserViewspaceZ = device->createTexture(desc);

    desc.format = nvrhi::Format::R10G10B10A2_UNORM;
    desc.debugName = "DenoiserNormalRoughness";
    DenoiserNormalRoughness = device->createTexture(desc);

    desc.format = nvrhi::Format::RGBA32_FLOAT;
    desc.debugName = "SecondarySurfacePositionNormal";
    SecondarySurfacePositionNormal = device->createTexture(desc);

    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = "SecondarySurfaceRadiance";
    SecondarySurfaceRadiance = device->createTexture(desc);
    
    desc.isUAV = false;
    desc.isRenderTarget = true;
    desc.useClearValue = true;
    desc.clearValue = nvrhi::Color(1.f);
    desc.sampleCount = m_SampleCount;
    desc.dimension = m_SampleCount > 1 ? nvrhi::TextureDimension::Texture2DMS : nvrhi::TextureDimension::Texture2D;
    desc.keepInitialState = true;

    desc.useClearValue = false;
    desc.clearValue = nvrhi::Color(0.f);
    desc.isTypeless = false;
    desc.isUAV = true;
    desc.isRenderTarget = true;
    desc.format = nvrhi::Format::RGBA32_FLOAT;
    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.debugName = "AccumulatedRadiance";
    AccumulatedRadiance = device->createTexture(desc);

    desc.useClearValue = true;
    desc.format = nvrhi::Format::RGBA16_FLOAT;  // keep in float for now in case we need 
    desc.debugName = "OutputColor";
    desc.clearValue = nvrhi::Color(1.0f, 1.0f, 0.0f, 0.0f);   // avoid the debug layer warnings... not actually cleared except for debug purposes
    desc.isUAV = true;
    OutputColor = device->createTexture(desc);

    desc.format = nvrhi::Format::R8_UNORM;
    desc.isUAV = true;
    desc.debugName = "DenoiserDisocclusionThresholdMix";
    DenoiserDisocclusionThresholdMix = device->createTexture(desc);
    desc.debugName = "CombinedHistoryClampRelax";
    CombinedHistoryClampRelax = device->createTexture(desc);

    // !!! NOTE !!! setting desc.width/desc.height to display size (was render size!)
    desc.width = displaySize.x;
    desc.height = displaySize.y;

    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = "ProcessedOutputColor";
    ProcessedOutputColor = device->createTexture(desc);
    desc.format = nvrhi::Format::RGBA16_SNORM;
    desc.debugName = "TemporalFeedback1";
    TemporalFeedback1 = device->createTexture(desc);
    desc.debugName = "TemporalFeedback2";
    TemporalFeedback2 = device->createTexture(desc);

    desc.format = nvrhi::Format::SRGBA8_UNORM;
    desc.isUAV = false;
    desc.debugName = "LdrColor";
    LdrColor = device->createTexture(desc);

    desc.debugName = "PreUIColor";
    PreUIColor = device->createTexture(desc);

    if (desc.isVirtual)
    {
        uint64_t heapSize = 0;
        nvrhi::ITexture* const textures[] = {
            //HdrColor,
            //ResolvedColor,
            //TemporalFeedback1,
            //TemporalFeedback2,
            LdrColor,
            OutputColor,
            PreUIColor,
            //AmbientOcclusion
        };

        for (auto texture : textures)
        {
            nvrhi::MemoryRequirements memReq = device->getTextureMemoryRequirements(texture);
            heapSize = nvrhi::align(heapSize, memReq.alignment);
            heapSize += memReq.size;
        }

        nvrhi::HeapDesc heapDesc;
        heapDesc.type = nvrhi::HeapType::DeviceLocal;
        heapDesc.capacity = heapSize;
        heapDesc.debugName = "RenderTargetHeap";

        Heap = device->createHeap(heapDesc);

        uint64_t offset = 0;
        for (auto texture : textures)
        {
            nvrhi::MemoryRequirements memReq = device->getTextureMemoryRequirements(texture);
            offset = nvrhi::align(offset, memReq.alignment);

            device->bindTextureMemory(texture, Heap, offset);

            offset += memReq.size;
        }
    }

    LdrFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
    LdrFramebuffer->RenderTargets = { LdrColor };

    { // Stable planes
        nvrhi::BufferDesc bufferDesc;
        bufferDesc.isVertexBuffer = false;
        bufferDesc.isConstantBuffer = false;
        bufferDesc.isVolatile = false;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.cpuAccess = nvrhi::CpuAccessMode::None;
        bufferDesc.keepInitialState = true;
        bufferDesc.initialState = nvrhi::ResourceStates::Common;

        bufferDesc.structStride = sizeof(StablePlane);
        bufferDesc.byteSize = sizeof(StablePlane) * StablePlanesComputeStorageElementCount(m_RenderSize.x, m_RenderSize.y);
        bufferDesc.debugName = "PingStablePlanesBuffer";
        StablePlanesBuffer = device->createBuffer(bufferDesc);
        bufferDesc.debugName = "PongStablePlanesBuffer";
        PrevStablePlanesBuffer = device->createBuffer(bufferDesc);

#if 0 // useful test for custom addressing
        for (int i = 0; i < 100; i++)
        {
            uint2 randsiz = {(uint)(std::rand()%2000), (uint)(std::rand()%2000)};
            uint3 randp = {(uint)std::rand()%randsiz.x, (uint)std::rand()%randsiz.y, (uint)std::rand()%3};
            uint addr = StablePlanesPixelToAddress(randp.xy(), randp.z, randsiz.x, randsiz.y );
            uint3 randpt = StablePlanesAddressToPixel(addr, randsiz.x, randsiz.y);
            assert( randp.x == randpt.x && randp.y == randpt.y && randp.z == randpt.z );
        }
#endif
    }
}

[[nodiscard]] bool RenderTargets::IsUpdateRequired(donut::math::int2 renderSize, donut::math::int2 displaySize, donut::math::uint sampleCount) const
{
    if (any(m_RenderSize != renderSize) || any(m_DisplaySize != displaySize) || m_SampleCount != sampleCount) return true;
    return false;
}

void RenderTargets::Clear(nvrhi::ICommandList* commandList) 
{
    const nvrhi::FormatInfo& depthFormatInfo = nvrhi::getFormatInfo(Depth->getDesc().format);

    float depthClearValue = m_UseReverseProjection ? 0.f : 1.f;
    commandList->clearTextureFloat(Depth, nvrhi::AllSubresources, nvrhi::Color(depthClearValue));

    commandList->clearTextureFloat(CombinedHistoryClampRelax, nvrhi::AllSubresources, nvrhi::Color(0));
}
