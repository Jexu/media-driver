/*
* Copyright (c) 2021-2022, Intel Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/
//!
//! \file     vp_render_cmd_packet.cpp
//! \brief    render packet which used in by mediapipline.
//! \details  render packet provide the structures and generate the cmd buffer which mediapipline will used.
//!
#include "vp_render_cmd_packet.h"
#include "vp_platform_interface.h"
#include "vp_pipeline_common.h"
#include "vp_render_kernel_obj.h"
#include "hal_oca_interface.h"
#include "vp_pipeline.h"
#include "vp_packet_pipe.h"
#include "vp_user_feature_control.h"
#include "mhw_mi_itf.h"
#include "mhw_mi_cmdpar.h"

namespace vp
{
static inline RENDERHAL_SURFACE_TYPE InitRenderHalSurfType(VPHAL_SURFACE_TYPE vpSurfType)
{
    VP_FUNC_CALL();

    switch (vpSurfType)
    {
    case SURF_IN_BACKGROUND:
        return RENDERHAL_SURF_IN_BACKGROUND;

    case SURF_IN_PRIMARY:
        return RENDERHAL_SURF_IN_PRIMARY;

    case SURF_IN_SUBSTREAM:
        return RENDERHAL_SURF_IN_SUBSTREAM;

    case SURF_IN_REFERENCE:
        return RENDERHAL_SURF_IN_REFERENCE;

    case SURF_OUT_RENDERTARGET:
        return RENDERHAL_SURF_OUT_RENDERTARGET;

    case SURF_NONE:
    default:
        return RENDERHAL_SURF_NONE;
    }
}

VpRenderCmdPacket::VpRenderCmdPacket(MediaTask *task, PVP_MHWINTERFACE hwInterface, PVpAllocator &allocator, VPMediaMemComp *mmc, VpKernelSet *kernelSet) : CmdPacket(task),
                                                                                                                                                            RenderCmdPacket(task, hwInterface->m_osInterface, hwInterface->m_renderHal),
                                                                                                                                                            VpCmdPacket(task, hwInterface, allocator, mmc, VP_PIPELINE_PACKET_RENDER),
                                                                                                                                                            m_firstFrame(true),
                                                                                                                                                            m_kernelSet(kernelSet)
{
    if (m_hwInterface && m_hwInterface->m_userFeatureControl)
    {
        bool computeContextEnabled = m_hwInterface->m_userFeatureControl->IsComputeContextEnabled();
        m_PacketId = computeContextEnabled ? VP_PIPELINE_PACKET_COMPUTE : VP_PIPELINE_PACKET_RENDER;
    }
    else
    {
        VP_RENDER_ASSERTMESSAGE("m_hwInterface or m_hwInterface->m_userFeatureControl is nullptr!");
    }
}

VpRenderCmdPacket::~VpRenderCmdPacket()
{
    for (auto &samplerstate : m_kernelSamplerStateGroup)
    {
        if (samplerstate.second.SamplerType == MHW_SAMPLER_TYPE_AVS)
        {
            MOS_FreeMemAndSetNull(samplerstate.second.Avs.pMhwSamplerAvsTableParam);
        }
    }
    MOS_Delete(m_surfMemCacheCtl);
}

MOS_STATUS VpRenderCmdPacket::Init()
{
    return RenderCmdPacket::Init();
}

MOS_STATUS VpRenderCmdPacket::LoadKernel()
{
    int32_t                     iKrnAllocation  = 0;
    MHW_KERNEL_PARAM            MhwKernelParam  = {};
    RENDERHAL_KERNEL_PARAM      KernelParam     = m_renderData.KernelParam;
    // Load kernel to GSH
    INIT_MHW_KERNEL_PARAM(MhwKernelParam, &m_renderData.KernelEntry);
    UpdateKernelConfigParam(KernelParam);
    iKrnAllocation = m_renderHal->pfnLoadKernel(
        m_renderHal,
        &KernelParam,
        &MhwKernelParam,
        m_kernel->GetCachedEntryForKernelLoad());

    if (iKrnAllocation < 0)
    {
        RENDER_PACKET_ASSERTMESSAGE("kernel load failed");
        return MOS_STATUS_UNKNOWN;
    }

    m_renderData.kernelAllocationID = iKrnAllocation;

    if (m_renderData.iCurbeOffset < 0)
    {
        RENDER_PACKET_ASSERTMESSAGE("Curbe Set Fail, return error");
        return MOS_STATUS_UNKNOWN;
    }
    // Allocate Media ID, link to kernel
    m_renderData.mediaID = m_renderHal->pfnAllocateMediaID(
        m_renderHal,
        iKrnAllocation,
        m_renderData.bindingTable,
        m_renderData.iCurbeOffset,
        (m_renderData.iCurbeLength),
        0,
        nullptr);

    if (m_renderData.mediaID < 0)
    {
        RENDER_PACKET_ASSERTMESSAGE("Allocate Media ID failed, return error");
        return MOS_STATUS_UNKNOWN;
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::Prepare()
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(m_renderHal);
    VP_RENDER_CHK_NULL_RETURN(m_kernelSet);
    VP_RENDER_CHK_NULL_RETURN(m_surfMemCacheCtl);

    if (m_packetResourcesdPrepared)
    {
        VP_RENDER_NORMALMESSAGE("Resource Prepared, skip this time");
        return MOS_STATUS_SUCCESS;
    }

    VP_RENDER_CHK_STATUS_RETURN(m_kernelSet->CreateKernelObjects(
        m_renderKernelParams,
        m_surfSetting.surfGroup,
        m_kernelSamplerStateGroup,
        m_kernelConfigs,
        m_kernelObjs,
        *m_surfMemCacheCtl));

    if (m_submissionMode == MULTI_KERNELS_WITH_MULTI_MEDIA_STATES)
    {
        m_kernelRenderData.clear();
        VP_RENDER_CHK_NULL_RETURN(m_renderHal->pStateHeap);

        m_renderHal->pStateHeap->iCurrentBindingTable = 0;
        m_renderHal->pStateHeap->iCurrentSurfaceState = 0;

        for (auto it = m_kernelObjs.begin(); it != m_kernelObjs.end(); it++)
        {
            m_kernel = it->second;
            VP_RENDER_CHK_NULL_RETURN(m_kernel);

            m_kernel->SetCacheCntl(m_surfMemCacheCtl);

            // reset render Data for current kernel
            MOS_ZeroMemory(&m_renderData, sizeof(KERNEL_PACKET_RENDER_DATA));

            if (m_bindingtableMode == MULTI_KERNELS_WITH_MULTI_BINDINGTABLES)
            {
                isMultiBindingTables = true;
            }
            else
            {
                isMultiBindingTables = false;
            }

            VP_RENDER_CHK_STATUS_RETURN(RenderEngineSetup());

            VP_RENDER_CHK_STATUS_RETURN(KernelStateSetup());

            VP_RENDER_CHK_STATUS_RETURN(SetupSurfaceState());  // once Surface setup done, surface index should be created here

            VP_RENDER_CHK_STATUS_RETURN(SetupCurbeState());  // Set Curbe with updated surface index

            VP_RENDER_CHK_STATUS_RETURN(LoadKernel());

            VP_RENDER_CHK_STATUS_RETURN(SetupSamplerStates());

            VP_RENDER_CHK_STATUS_RETURN(SetupWalkerParams());

            VP_RENDER_CHK_STATUS_RETURN(m_renderHal->pfnSetVfeStateParams(
                m_renderHal,
                MEDIASTATE_DEBUG_COUNTER_FREE_RUNNING,
                m_renderData.KernelParam.Thread_Count,
                m_renderData.iCurbeLength,
                m_renderData.iInlineLength,
                m_renderData.scoreboardParams));

            m_kernelRenderData.insert(std::make_pair(it->first, m_renderData));
        }
    }
    else if (m_submissionMode == MULTI_KERNELS_WITH_ONE_MEDIA_STATE)
    {
        MOS_ZeroMemory(&m_renderData, sizeof(KERNEL_PACKET_RENDER_DATA));
        VP_RENDER_CHK_STATUS_RETURN(RenderEngineSetup());

        m_kernelRenderData.clear();

        // for multi-kernel prepare together
        for (auto it = m_kernelObjs.begin(); it != m_kernelObjs.end(); it++)
        {
            m_kernel = it->second;
            VP_RENDER_CHK_NULL_RETURN(m_kernel);

            if (it != m_kernelObjs.begin())
            {
                // reset render Data for current kernel
                PRENDERHAL_MEDIA_STATE pMediaState = m_renderData.mediaState;
                MOS_ZeroMemory(&m_renderData, sizeof(KERNEL_PACKET_RENDER_DATA));
                m_renderData.mediaState = pMediaState;
                // Assign and Reset binding table
                RENDER_PACKET_CHK_STATUS_RETURN(m_renderHal->pfnAssignBindingTable(
                    m_renderHal,
                    &m_renderData.bindingTable));
            }

            VP_RENDER_CHK_STATUS_RETURN(KernelStateSetup());

            VP_RENDER_CHK_STATUS_RETURN(SetupSurfaceState());  // once Surface setup done, surface index should be created here

            VP_RENDER_CHK_STATUS_RETURN(SetupCurbeState());  // Set Curbe with updated surface index

            VP_RENDER_CHK_STATUS_RETURN(LoadKernel());

            VP_RENDER_CHK_STATUS_RETURN(SetupSamplerStates());

            VP_RENDER_CHK_STATUS_RETURN(SetupWalkerParams());

            m_kernelRenderData.insert(std::make_pair(it->first, m_renderData));
        }

        VP_RENDER_CHK_STATUS_RETURN(m_renderHal->pfnSetVfeStateParams(
            m_renderHal,
            MEDIASTATE_DEBUG_COUNTER_FREE_RUNNING,
            RENDERHAL_USE_MEDIA_THREADS_MAX,
            m_totalCurbeSize,
            m_totoalInlineSize,
            m_renderData.scoreboardParams));
    }
    else
    {
        return MOS_STATUS_INVALID_PARAMETER;
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetupSamplerStates()
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(m_renderHal);
    VP_RENDER_CHK_NULL_RETURN(m_kernel);

    KERNEL_SAMPLER_STATES samplerStates = {};

    // For AdvKernel, SetSamplerStates is called by VpRenderKernelObj::SetKernelConfigs
    if (!m_kernel->IsAdvKernel())
    {
        // Initialize m_kernelSamplerStateGroup.
        VP_RENDER_CHK_STATUS_RETURN(m_kernel->SetSamplerStates(m_kernelSamplerStateGroup));
    }

    for (int samplerIndex = 0, activeSamplerLeft = m_kernelSamplerStateGroup.size(); activeSamplerLeft > 0; ++samplerIndex)
    {
        auto it = m_kernelSamplerStateGroup.find(samplerIndex);
        if (m_kernelSamplerStateGroup.end() != it)
        {
            --activeSamplerLeft;
            samplerStates.push_back(it->second);
        }
        else
        {
            MHW_SAMPLER_STATE_PARAM param = {};
            samplerStates.push_back(param);
        }
    }

    if (!samplerStates.empty())
    {
        if (samplerStates.size() > MHW_RENDER_ENGINE_SAMPLERS_MAX)
        {
            MOS_STATUS_INVALID_PARAMETER;
        }

        VP_RENDER_CHK_STATUS_RETURN(m_renderHal->pfnSetSamplerStates(
            m_renderHal,
            m_renderData.mediaID,
            &samplerStates[0],
            samplerStates.size()));
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::Submit(MOS_COMMAND_BUFFER *commandBuffer, uint8_t packetPhase)
{
    VP_FUNC_CALL();
    if (m_kernelObjs.empty())
    {
        VP_RENDER_ASSERTMESSAGE("No Kernel Object Creation");
        return MOS_STATUS_NULL_POINTER;
    }
    if (m_submissionMode == MULTI_KERNELS_WITH_MULTI_MEDIA_STATES   &&
        m_bindingtableMode == MULTI_KERNELS_WITH_MULTI_BINDINGTABLES)
    {
        VP_RENDER_CHK_STATUS_RETURN(SetupMediaWalker());

        VP_RENDER_CHK_STATUS_RETURN(SubmitWithMultiKernel(commandBuffer, packetPhase));
    }
    else if (m_submissionMode == MULTI_KERNELS_WITH_MULTI_MEDIA_STATES)
    {
        VP_RENDER_CHK_STATUS_RETURN(SetupMediaWalker());

        VP_RENDER_CHK_STATUS_RETURN(RenderCmdPacket::Submit(commandBuffer, packetPhase));
    }
    else if (m_submissionMode == MULTI_KERNELS_WITH_ONE_MEDIA_STATE)
    {
        VP_RENDER_CHK_STATUS_RETURN(SubmitWithMultiKernel(commandBuffer, packetPhase));
    }
    else
    {
        return MOS_STATUS_INVALID_PARAMETER;
    }

    if (!m_surfSetting.dumpLaceSurface &&
        !m_surfSetting.dumpPostSurface)
    {
        VP_RENDER_CHK_STATUS_RETURN(m_kernelSet->DestroyKernelObjects(m_kernelObjs));
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::InitFcMemCacheControlForTarget(PVP_RENDER_CACHE_CNTL settings)
{
    MOS_HW_RESOURCE_DEF                 Usage           = MOS_HW_RESOURCE_DEF_MAX;
    MEMORY_OBJECT_CONTROL_STATE         MemObjCtrl      = {};
    PMOS_INTERFACE                      pOsInterface    = m_osInterface;

    VP_RENDER_CHK_NULL_RETURN(pOsInterface);
    VP_RENDER_CHK_NULL_RETURN(settings);

    VPHAL_SET_SURF_MEMOBJCTL(settings->Composite.TargetSurfMemObjCtl,         MOS_MP_RESOURCE_USAGE_DEFAULT_RCS);

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::InitFcMemCacheControl(PVP_RENDER_CACHE_CNTL settings)
{
    MOS_HW_RESOURCE_DEF                 Usage           = MOS_HW_RESOURCE_DEF_MAX;
    MEMORY_OBJECT_CONTROL_STATE         MemObjCtrl      = {};
    PMOS_INTERFACE                      pOsInterface    = m_osInterface;

    VP_RENDER_CHK_NULL_RETURN(settings);

    if (!settings->bCompositing)
    {
        return MOS_STATUS_SUCCESS;
    }

    settings->Composite.bL3CachingEnabled = true;

    VPHAL_SET_SURF_MEMOBJCTL(settings->Composite.PrimaryInputSurfMemObjCtl,   MOS_MP_RESOURCE_USAGE_SurfaceState_RCS);
    VPHAL_SET_SURF_MEMOBJCTL(settings->Composite.InputSurfMemObjCtl,          MOS_MP_RESOURCE_USAGE_SurfaceState_RCS);

    VP_RENDER_CHK_STATUS_RETURN(InitFcMemCacheControlForTarget(settings));

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::InitSurfMemCacheControl(VP_EXECUTE_CAPS packetCaps)
{
    MOS_HW_RESOURCE_DEF                 Usage           = MOS_HW_RESOURCE_DEF_MAX;
    MEMORY_OBJECT_CONTROL_STATE         MemObjCtrl      = {};
    PMOS_INTERFACE                      pOsInterface    = nullptr;
    PVP_RENDER_CACHE_CNTL               pSettings       = nullptr;

    VP_FUNC_CALL();

    if (nullptr == m_surfMemCacheCtl)
    {
        m_surfMemCacheCtl = MOS_New(VP_RENDER_CACHE_CNTL);
        VP_PUBLIC_CHK_NULL_RETURN(m_surfMemCacheCtl);
    }

    VP_PUBLIC_CHK_NULL_RETURN(m_hwInterface);
    VP_PUBLIC_CHK_NULL_RETURN(m_hwInterface->m_osInterface);

    MOS_ZeroMemory(m_surfMemCacheCtl, sizeof(*m_surfMemCacheCtl));

    pOsInterface    = m_hwInterface->m_osInterface;
    pSettings       = m_surfMemCacheCtl;

    pSettings->bCompositing = packetCaps.bComposite;
    pSettings->bDnDi = true;
    pSettings->bLace = MEDIA_IS_SKU(m_hwInterface->m_skuTable, FtrLace);

    VP_RENDER_CHK_STATUS_RETURN(InitFcMemCacheControl(pSettings));

    if (pSettings->bDnDi)
    {
        pSettings->DnDi.bL3CachingEnabled = true;

        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.CurrentInputSurfMemObjCtl,        MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.PreviousInputSurfMemObjCtl,       MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.STMMInputSurfMemObjCtl,           MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.STMMOutputSurfMemObjCtl,          MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.DnOutSurfMemObjCtl,               MOS_MP_RESOURCE_USAGE_SurfaceState);

        if (packetCaps.bVebox && !packetCaps.bSFC && !packetCaps.bRender)
        {
            // Disable cache for output surface in vebox only condition
            VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.CurrentOutputSurfMemObjCtl,    MOS_MP_RESOURCE_USAGE_DEFAULT);
        }
        else
        {
            VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.CurrentOutputSurfMemObjCtl,    MOS_MP_RESOURCE_USAGE_SurfaceState);
        }

        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.StatisticsOutputSurfMemObjCtl,    MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.AlphaOrVignetteSurfMemObjCtl,     MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.LaceOrAceOrRgbHistogramSurfCtrl,  MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.SkinScoreSurfMemObjCtl,           MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.LaceLookUpTablesSurfMemObjCtl,    MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.Vebox3DLookUpTablesSurfMemObjCtl, MOS_MP_RESOURCE_USAGE_SurfaceState);
    }
    else
    {
        pSettings->DnDi.bL3CachingEnabled = false;

        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.CurrentInputSurfMemObjCtl,        MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.PreviousInputSurfMemObjCtl,       MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.STMMInputSurfMemObjCtl,           MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.STMMOutputSurfMemObjCtl,          MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.DnOutSurfMemObjCtl,               MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.CurrentOutputSurfMemObjCtl,       MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.StatisticsOutputSurfMemObjCtl,    MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.AlphaOrVignetteSurfMemObjCtl,     MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.LaceOrAceOrRgbHistogramSurfCtrl,  MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.SkinScoreSurfMemObjCtl,           MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.LaceLookUpTablesSurfMemObjCtl,    MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->DnDi.Vebox3DLookUpTablesSurfMemObjCtl, MOS_MP_RESOURCE_USAGE_DEFAULT);
    }

    if (pSettings->bLace)
    {
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.FrameHistogramSurfaceMemObjCtl,      MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.AggregatedHistogramSurfaceMemObjCtl, MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.StdStatisticsSurfaceMemObjCtl,       MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.PwlfInSurfaceMemObjCtl,              MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.PwlfOutSurfaceMemObjCtl,             MOS_MP_RESOURCE_USAGE_SurfaceState);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.WeitCoefSurfaceMemObjCtl,            MOS_MP_RESOURCE_USAGE_SurfaceState);
    }
    else
    {
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.FrameHistogramSurfaceMemObjCtl,                       MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.AggregatedHistogramSurfaceMemObjCtl,                  MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.StdStatisticsSurfaceMemObjCtl,                        MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.PwlfInSurfaceMemObjCtl,                               MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.PwlfOutSurfaceMemObjCtl,                              MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.WeitCoefSurfaceMemObjCtl,                             MOS_MP_RESOURCE_USAGE_DEFAULT);
        VPHAL_SET_SURF_MEMOBJCTL(pSettings->Lace.GlobalToneMappingCurveLUTSurfaceMemObjCtl,            MOS_MP_RESOURCE_USAGE_DEFAULT);
    }


    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::PacketInit(
    VP_SURFACE *        inputSurface,
    VP_SURFACE *        outputSurface,
    VP_SURFACE *        previousSurface,
    VP_SURFACE_SETTING &surfSetting,
    VP_EXECUTE_CAPS     packetCaps)
{
    VP_FUNC_CALL();

    // will remodify when normal render path enabled
    VP_UNUSED(inputSurface);
    VP_UNUSED(outputSurface);
    VP_UNUSED(previousSurface);
    VP_RENDER_CHK_NULL_RETURN(m_renderHal);

    m_PacketCaps = packetCaps;

    // Init packet surface params.
    m_surfSetting = surfSetting;

    m_packetResourcesdPrepared = false;
    m_kernelConfigs.clear();
    m_renderKernelParams.clear();

    m_renderHal->eufusionBypass = false;
    m_totoalInlineSize = 0;

    VP_RENDER_CHK_STATUS_RETURN(InitSurfMemCacheControl(packetCaps));

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::KernelStateSetup()
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(m_kernel);

    // Initialize States
    MOS_ZeroMemory(&m_renderData.KernelEntry, sizeof(Kdll_CacheEntry));

    // Store pointer to Kernel Parameter
    VP_RENDER_CHK_STATUS_RETURN(m_kernel->GetKernelSettings(m_renderData.KernelParam));

    // Set Parameters for Kernel Entry
    VP_RENDER_CHK_STATUS_RETURN(m_kernel->GetKernelEntry(m_renderData.KernelEntry));

    // set the Inline Data length
    m_renderData.iInlineLength = (int32_t)m_kernel->GetInlineDataSize();
    m_totoalInlineSize += m_renderData.iInlineLength;

    VP_RENDER_CHK_STATUS_RETURN(m_kernel->GetScoreboardParams(m_renderData.scoreboardParams));

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetupSurfaceState()
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(m_kernel);
    VP_RENDER_CHK_NULL_RETURN(m_renderHal);
    VP_RENDER_CHK_NULL_RETURN(m_renderHal->pOsInterface);

    if (!m_kernel->GetKernelSurfaceConfig().empty())
    {
        for (auto surface = m_kernel->GetKernelSurfaceConfig().begin(); surface != m_kernel->GetKernelSurfaceConfig().end(); surface++)
        {
            KERNEL_SURFACE_STATE_PARAM *kernelSurfaceParam = &surface->second;
            SurfaceType                 type               = surface->first;

            RENDERHAL_SURFACE_NEXT renderHalSurface;
            MOS_ZeroMemory(&renderHalSurface, sizeof(RENDERHAL_SURFACE_NEXT));

            RENDERHAL_SURFACE_STATE_PARAMS renderSurfaceParams;
            MOS_ZeroMemory(&renderSurfaceParams, sizeof(RENDERHAL_SURFACE_STATE_PARAMS));
            if (kernelSurfaceParam->surfaceOverwriteParams.updatedRenderSurfaces)
            {
                renderSurfaceParams = kernelSurfaceParam->surfaceOverwriteParams.renderSurfaceParams;
            }
            else
            {
                renderSurfaceParams.bRenderTarget    = (kernelSurfaceParam->renderTarget == true) ? 1 : 0;
                renderSurfaceParams.Boundary         = RENDERHAL_SS_BOUNDARY_ORIGINAL;  // Add conditional in future for Surfaces out of range
                renderSurfaceParams.bWidth16Align    = false;
                renderSurfaceParams.bWidthInDword_Y  = true;
                renderSurfaceParams.bWidthInDword_UV = true;

                //set mem object control for cache
                renderSurfaceParams.MemObjCtl = (m_renderHal->pOsInterface->pfnCachePolicyGetMemoryObject(
                    MOS_MP_RESOURCE_USAGE_DEFAULT,
                    m_renderHal->pOsInterface->pfnGetGmmClientContext(m_renderHal->pOsInterface))).DwordValue;
            }

            VP_SURFACE *vpSurface = nullptr;

            if (m_surfSetting.surfGroup.find(type) != m_surfSetting.surfGroup.end())
            {
                vpSurface = m_surfSetting.surfGroup.find(type)->second;
            }

            if (vpSurface)
            {
                MOS_STATUS status = m_kernel->InitRenderHalSurface(type, vpSurface, &renderHalSurface);
                if (MOS_STATUS_UNIMPLEMENTED == status)
                {
                    // Prepare surfaces tracked in Resource manager
                    VP_RENDER_CHK_STATUS_RETURN(InitRenderHalSurface(*vpSurface, renderHalSurface));
                    VP_RENDER_CHK_STATUS_RETURN(UpdateRenderSurface(renderHalSurface, *kernelSurfaceParam));
                }
                else
                {
                    VP_RENDER_CHK_STATUS_RETURN(status);
                }
                if (SurfaceTypeFcCscCoeff == type)
                {
                    m_renderHal->bCmfcCoeffUpdate  = true;
                    m_renderHal->pCmfcCoeffSurface = &vpSurface->osSurface->OsResource;
                }
                else
                {
                    m_renderHal->bCmfcCoeffUpdate  = false;
                    m_renderHal->pCmfcCoeffSurface = nullptr;
                }
            }
            else
            {
                // State Heaps are not tracked in resource manager till now
                VP_RENDER_CHK_STATUS_RETURN(InitStateHeapSurface(type, renderHalSurface));
                VP_RENDER_CHK_STATUS_RETURN(UpdateRenderSurface(renderHalSurface, *kernelSurfaceParam));
            }

            uint32_t index = 0;

            if (kernelSurfaceParam->surfaceOverwriteParams.bindedKernel && !kernelSurfaceParam->surfaceOverwriteParams.bufferResource)
            {
                index = SetSurfaceForHwAccess(
                    &renderHalSurface.OsSurface,
                    &renderHalSurface,
                    &renderSurfaceParams,
                    kernelSurfaceParam->surfaceOverwriteParams.bindIndex,
                    renderSurfaceParams.bRenderTarget,
                    kernelSurfaceParam->surfaceEntries,
                    kernelSurfaceParam->sizeOfSurfaceEntries);
            }
            else
            {
                if ((kernelSurfaceParam->surfaceOverwriteParams.updatedSurfaceParams  &&
                     kernelSurfaceParam->surfaceOverwriteParams.bufferResource        &&
                     kernelSurfaceParam->surfaceOverwriteParams.bindedKernel))
                {
                    index = SetBufferForHwAccess(
                        &renderHalSurface.OsSurface,
                        &renderHalSurface,
                        &renderSurfaceParams,
                        kernelSurfaceParam->surfaceOverwriteParams.bindIndex,
                        renderSurfaceParams.bRenderTarget);
                }
                else if ((kernelSurfaceParam->surfaceOverwriteParams.updatedSurfaceParams &&
                     kernelSurfaceParam->surfaceOverwriteParams.bufferResource            &&
                     !kernelSurfaceParam->surfaceOverwriteParams.bindedKernel)            ||
                    (!kernelSurfaceParam->surfaceOverwriteParams.updatedSurfaceParams     &&
                    (renderHalSurface.OsSurface.Type == MOS_GFXRES_BUFFER                 ||
                     renderHalSurface.OsSurface.Type == MOS_GFXRES_INVALID)))
                {
                    index = SetBufferForHwAccess(
                        &renderHalSurface.OsSurface,
                        &renderHalSurface,
                        &renderSurfaceParams,
                        renderSurfaceParams.bRenderTarget);
                }
                else
                {
                    index = SetSurfaceForHwAccess(
                        &renderHalSurface.OsSurface,
                        &renderHalSurface,
                        &renderSurfaceParams,
                        renderSurfaceParams.bRenderTarget);
                }
            }
            VP_RENDER_CHK_STATUS_RETURN(m_kernel->UpdateCurbeBindingIndex(type, index));
        }
        VP_RENDER_CHK_STATUS_RETURN(m_kernel->UpdateCompParams());
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetupCurbeState()
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(m_kernel);

    // set the Curbe Data length
    void *   curbeData   = nullptr;
    uint32_t curbeLength = 0;
    uint32_t curbeLengthAligned = 0;

    VP_RENDER_CHK_STATUS_RETURN(m_kernel->GetCurbeState(curbeData, curbeLength, curbeLengthAligned, m_renderData.KernelParam, m_renderHal->dwCurbeBlockAlign));

    m_renderData.iCurbeOffset = m_renderHal->pfnLoadCurbeData(
        m_renderHal,
        m_renderData.mediaState,
        curbeData,
        curbeLength);

    if (m_renderData.iCurbeOffset < 0)
    {
        RENDER_PACKET_ASSERTMESSAGE("Curbe Set Fail, return error");
        return MOS_STATUS_UNKNOWN;
    }

    m_renderData.iCurbeLength = curbeLengthAligned;
 
    m_totalCurbeSize += m_renderData.iCurbeLength;

    m_kernel->FreeCurbe(curbeData);

    return MOS_STATUS_SUCCESS;
}

VP_SURFACE *VpRenderCmdPacket::GetSurface(SurfaceType type)
{
    VP_FUNC_CALL();

    auto        it   = m_surfSetting.surfGroup.find(type);
    VP_SURFACE *surf = (m_surfSetting.surfGroup.end() != it) ? it->second : nullptr;

    return surf;
}

MOS_STATUS VpRenderCmdPacket::SetupMediaWalker()
{
    VP_FUNC_CALL();

    switch (m_walkerType)
    {
    case WALKER_TYPE_MEDIA:
        MOS_ZeroMemory(&m_mediaWalkerParams, sizeof(MHW_WALKER_PARAMS));
        // Prepare Media Walker Params
        VP_RENDER_CHK_STATUS_RETURN(PrepareMediaWalkerParams(m_renderData.walkerParam, m_mediaWalkerParams));
        break;
    case WALKER_TYPE_COMPUTE:
        // Parepare Compute Walker Param
        MOS_ZeroMemory(&m_gpgpuWalkerParams, sizeof(MHW_GPGPU_WALKER_PARAMS));
        VP_RENDER_CHK_STATUS_RETURN(PrepareComputeWalkerParams(m_renderData.walkerParam, m_gpgpuWalkerParams));
        break;
    case WALKER_TYPE_DISABLED:
    default:
        // using BB for walker setting
        return MOS_STATUS_UNIMPLEMENTED;
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetupWalkerParams()
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(m_kernel);

    VP_RENDER_CHK_STATUS_RETURN(m_kernel->GetWalkerSetting(m_renderData.walkerParam, m_renderData));

    return MOS_STATUS_SUCCESS;
}

void VpRenderCmdPacket::UpdateKernelConfigParam(RENDERHAL_KERNEL_PARAM &kernelParam)
{
    // In VP, 32 alignment with 5 bits right shift has already been done for CURBE_Length.
    // No need update here.
}

void VpRenderCmdPacket::OcaDumpDbgInfo(MOS_COMMAND_BUFFER &cmdBuffer, MOS_CONTEXT &mosContext)
{
    // Add kernel info to log.
    for (auto it = m_kernelObjs.begin(); it != m_kernelObjs.end(); it++)
    {
        auto kernel = it->second;
        if (kernel)
        {
            kernel->OcaDumpKernelInfo(cmdBuffer, mosContext);
        }
        else
        {
            VP_RENDER_ASSERTMESSAGE("nullptr in m_kernelObjs!");
        }
    }
    // Add vphal param to log.
    HalOcaInterface::DumpVphalParam(cmdBuffer, mosContext, m_renderHal->pVphalOcaDumper);
}

MOS_STATUS VpRenderCmdPacket::SetMediaFrameTracking(RENDERHAL_GENERIC_PROLOG_PARAMS &genericPrologParams)
{
    return VpCmdPacket::SetMediaFrameTracking(genericPrologParams);
}

MOS_STATUS VpRenderCmdPacket::InitRenderHalSurface(VP_SURFACE &surface, RENDERHAL_SURFACE &renderSurface)
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(surface.osSurface);
    VP_RENDER_CHK_STATUS_RETURN(RenderCmdPacket::InitRenderHalSurface(*surface.osSurface, &renderSurface));

    renderSurface.rcSrc    = surface.rcSrc;
    renderSurface.rcDst    = surface.rcDst;
    renderSurface.rcMaxSrc = surface.rcMaxSrc;
    renderSurface.SurfType =
        InitRenderHalSurfType(surface.SurfType);

    return MOS_STATUS_SUCCESS;
}
MOS_STATUS VpRenderCmdPacket::InitStateHeapSurface(SurfaceType type, RENDERHAL_SURFACE &renderSurface)
{
    VP_FUNC_CALL();
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;

    MOS_SURFACE mosSurface;

    MOS_ZeroMemory(&mosSurface, sizeof(MOS_SURFACE));

    // Check for Vebox Heap readiness
    const MHW_VEBOX_HEAP *pVeboxHeap = nullptr;
    std::shared_ptr<mhw::vebox::Itf> veboxItf = nullptr;

    VP_RENDER_CHK_NULL_RETURN(m_hwInterface);
    VP_RENDER_CHK_NULL_RETURN(m_hwInterface->m_veboxInterface);
    veboxItf = std::static_pointer_cast<mhw::vebox::Itf>(m_hwInterface->m_veboxInterface->GetNewVeboxInterface());

    if (veboxItf)
    {
        VP_RENDER_CHK_STATUS_RETURN(veboxItf->GetVeboxHeapInfo(
            &pVeboxHeap));
    }
    else
    {
        VP_RENDER_CHK_STATUS_RETURN(m_hwInterface->m_veboxInterface->GetVeboxHeapInfo(
            &pVeboxHeap));
    }

    VP_RENDER_CHK_NULL_RETURN(pVeboxHeap);

    switch (type)
    {
    case SurfaceTypeVeboxStateHeap_Drv:
        mosSurface.OsResource = pVeboxHeap->DriverResource;
        break;
    case SurfaceTypeVeboxStateHeap_Knr:
    case SurfaceTypeVeboxInput:
    case SurfaceTypeLaceAceRGBHistogram:
    case SurfaceTypeLaceLut:
    case SurfaceTypeStatistics:
    case SurfaceTypeSkinScore:
    case SurfaceTypeAggregatedHistogram:
    case SurfaceTypeFrameHistogram:
    case SurfaceTypeStdStatistics:
    case SurfaceTypePwlfIn:
    case SurfaceTypePwlfOut:
    case SurfaceTypeWeitCoef:
    case SurfaceTypGlobalToneMappingCurveLUT:
        mosSurface.OsResource = pVeboxHeap->KernelResource;
        break;
    default:
        eStatus = MOS_STATUS_UNIMPLEMENTED;
        VP_RENDER_ASSERTMESSAGE("Not Inplenmented in driver now, return fail");
        break;
    }

    VP_RENDER_CHK_STATUS_RETURN(RenderCmdPacket::InitRenderHalSurface(mosSurface, &renderSurface));

    return eStatus;
}
MOS_STATUS VpRenderCmdPacket::UpdateRenderSurface(RENDERHAL_SURFACE_NEXT &renderSurface, KERNEL_SURFACE_STATE_PARAM &kernelParams)
{
    VP_FUNC_CALL();
    auto &overwriteParam = kernelParams.surfaceOverwriteParams;
    if (overwriteParam.updatedSurfaceParams)
    {
        if (overwriteParam.width && overwriteParam.height)
        {
            renderSurface.OsSurface.dwWidth  = overwriteParam.width;
            renderSurface.OsSurface.dwHeight = overwriteParam.height;
            renderSurface.OsSurface.dwQPitch = overwriteParam.height;
        }

        renderSurface.OsSurface.dwPitch = overwriteParam.pitch != 0 ? overwriteParam.pitch : renderSurface.OsSurface.dwPitch;

        if (renderSurface.OsSurface.dwPitch < renderSurface.OsSurface.dwWidth)
        {
            VP_RENDER_ASSERTMESSAGE("Invalid Surface where Pitch < Width, return invalid Overwrite Params");
            return MOS_STATUS_INVALID_PARAMETER;
        }

        renderSurface.OsSurface.Format = (overwriteParam.format != 0) ? overwriteParam.format : renderSurface.OsSurface.Format;

        if (0 == renderSurface.OsSurface.dwQPitch)
        {
            renderSurface.OsSurface.dwQPitch = renderSurface.OsSurface.dwHeight;
        }
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetSamplerAvsParams(MHW_SAMPLER_STATE_PARAM &samplerStateParam, PRENDER_SR_PARAMS params)
{
    VP_FUNC_CALL();
    // Set Surface Smapler Status
    samplerStateParam.Avs.bEnableAVS          = true;
    samplerStateParam.Avs.BypassIEF           = 0;
    samplerStateParam.Avs.AvsType             = 0;  // Adaptive
    samplerStateParam.Avs.EightTapAFEnable    = 1;
    samplerStateParam.Avs.GainFactor          = 44;  // should be default
    samplerStateParam.Avs.GlobalNoiseEstm     = 255;
    samplerStateParam.Avs.StrongEdgeThr       = 8;
    samplerStateParam.Avs.WeakEdgeThr         = 1;
    samplerStateParam.Avs.StrongEdgeWght      = 7;
    samplerStateParam.Avs.RegularWght         = 2;
    samplerStateParam.Avs.NonEdgeWght         = 1;
    samplerStateParam.Avs.EightTapAFEnable    = true;
    samplerStateParam.Avs.bEnableSTDE         = 0;
    samplerStateParam.Avs.b8TapAdaptiveEnable = 0;
    samplerStateParam.Avs.bSkinDetailFactor   = 0;
    samplerStateParam.Avs.bHdcDwEnable        = true;
    samplerStateParam.Avs.bWritebackStandard  = true;
    samplerStateParam.Avs.bEnableIEF          = 0;
    samplerStateParam.Avs.wIEFFactor          = 0;

    // IEF params - default value
    samplerStateParam.Avs.wR3xCoefficient  = 6;
    samplerStateParam.Avs.wR3cCoefficient  = 15;
    samplerStateParam.Avs.wR5xCoefficient  = 9;
    samplerStateParam.Avs.wR5cxCoefficient = 8;
    samplerStateParam.Avs.wR5cCoefficient  = 3;

    // AVS_STATE
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->bBypassXAdaptiveFiltering  = 0;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->bBypassYAdaptiveFiltering  = 0;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->byteDefaultSharpnessLevel  = 255;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->byteMaxDerivative4Pixels   = 7;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->byteMaxDerivative8Pixels   = 20;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->byteTransitionArea8Pixels  = 5;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->byteTransitionArea4Pixels  = 4;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->bEnableRGBAdaptive         = false;
    samplerStateParam.Avs.pMhwSamplerAvsTableParam->bAdaptiveFilterAllChannels = true;

    MHW_AVS_PARAMS avs_params = {};
    // Allocate AVS coefficients, One set each for X and Y
    int32_t size = ((NUM_POLYPHASE_Y_ENTRIES * NUM_HW_POLYPHASE_TABLES_G9 * sizeof(float)) + (NUM_POLYPHASE_UV_ENTRIES * NUM_HW_POLYPHASE_TABLES_G9 * sizeof(float))) * 2;

    char *ptr = (char *)MOS_AllocAndZeroMemory(size);
    VPHAL_RENDER_CHK_NULL_RETURN(ptr);

    avs_params.piYCoefsX = (int32_t *)ptr;

    ptr += (NUM_POLYPHASE_Y_ENTRIES * NUM_HW_POLYPHASE_TABLES_G9 * sizeof(float));
    avs_params.piUVCoefsX = (int32_t *)ptr;

    ptr += (NUM_POLYPHASE_UV_ENTRIES * NUM_HW_POLYPHASE_TABLES_G9 * sizeof(float));
    avs_params.piYCoefsY = (int32_t *)ptr;

    ptr += (NUM_POLYPHASE_Y_ENTRIES * NUM_HW_POLYPHASE_TABLES_G9 * sizeof(float));
    avs_params.piUVCoefsY = (int32_t *)ptr;

    SamplerAvsCalcScalingTable(avs_params, (params->chromaLayerParam.kernelFormat) ? Format_YV12 : Format_NV12, false, params->chromaLayerParam.fScaleX, params->chromaLayerParam.fChromaScaleX, CHROMA_SITING_HORZ_LEFT | CHROMA_SITING_VERT_TOP, false);

    SamplerAvsCalcScalingTable(avs_params, (params->chromaLayerParam.kernelFormat) ? Format_YV12 : Format_NV12, true, params->chromaLayerParam.fScaleY, params->chromaLayerParam.fChromaScaleY, CHROMA_SITING_HORZ_LEFT | CHROMA_SITING_VERT_TOP, false);

    avs_params.Format = (params->chromaLayerParam.kernelFormat) ? Format_YV12 : Format_NV12;

    // Assign the coefficient table;
    for (uint32_t i = 0; i < MHW_NUM_HW_POLYPHASE_TABLES; i++)
    {
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[0] = (uint8_t)avs_params.piYCoefsX[i * 8 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[1] = (uint8_t)avs_params.piYCoefsX[i * 8 + 1];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[2] = (uint8_t)avs_params.piYCoefsX[i * 8 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[3] = (uint8_t)avs_params.piYCoefsX[i * 8 + 3];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[4] = (uint8_t)avs_params.piYCoefsX[i * 8 + 4];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[5] = (uint8_t)avs_params.piYCoefsX[i * 8 + 5];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[6] = (uint8_t)avs_params.piYCoefsX[i * 8 + 6];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroXFilterCoefficient[7] = (uint8_t)avs_params.piYCoefsX[i * 8 + 7];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[0] = (uint8_t)avs_params.piYCoefsY[i * 8 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[1] = (uint8_t)avs_params.piYCoefsY[i * 8 + 1];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[2] = (uint8_t)avs_params.piYCoefsY[i * 8 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[3] = (uint8_t)avs_params.piYCoefsY[i * 8 + 3];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[4] = (uint8_t)avs_params.piYCoefsY[i * 8 + 4];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[5] = (uint8_t)avs_params.piYCoefsY[i * 8 + 5];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[6] = (uint8_t)avs_params.piYCoefsY[i * 8 + 6];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .ZeroYFilterCoefficient[7] = (uint8_t)avs_params.piYCoefsY[i * 8 + 7];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneXFilterCoefficient[0] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneXFilterCoefficient[1] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 1];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneXFilterCoefficient[2] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneXFilterCoefficient[3] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 3];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneYFilterCoefficient[0] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneYFilterCoefficient[1] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 1];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneYFilterCoefficient[2] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParam[i]
            .OneYFilterCoefficient[3] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 3];
    }

    // Assign the coefficient table;
    for (uint32_t i = 0; i < MHW_NUM_HW_POLYPHASE_EXTRA_TABLES_G9; i++)
    {
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[0] = (uint8_t)avs_params.piYCoefsX[i * 8 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[1] = (uint8_t)avs_params.piYCoefsX[i * 8 + 1];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[2] = (uint8_t)avs_params.piYCoefsX[i * 8 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[3] = (uint8_t)avs_params.piYCoefsX[i * 8 + 3];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[4] = (uint8_t)avs_params.piYCoefsX[i * 8 + 4];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[5] = (uint8_t)avs_params.piYCoefsX[i * 8 + 5];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[6] = (uint8_t)avs_params.piYCoefsX[i * 8 + 6];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroXFilterCoefficient[7] = (uint8_t)avs_params.piYCoefsX[i * 8 + 7];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[0] = (uint8_t)avs_params.piYCoefsY[i * 8 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[1] = (uint8_t)avs_params.piYCoefsY[i * 8 + 1];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[2] = (uint8_t)avs_params.piYCoefsY[i * 8 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[3] = (uint8_t)avs_params.piYCoefsY[i * 8 + 3];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[4] = (uint8_t)avs_params.piYCoefsY[i * 8 + 4];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[5] = (uint8_t)avs_params.piYCoefsY[i * 8 + 5];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[6] = (uint8_t)avs_params.piYCoefsY[i * 8 + 6];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .ZeroYFilterCoefficient[7] = (uint8_t)avs_params.piYCoefsY[i * 8 + 7];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneXFilterCoefficient[0] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneXFilterCoefficient[1] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 1];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneXFilterCoefficient[2] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneXFilterCoefficient[3] = (uint8_t)avs_params.piUVCoefsX[i * 4 + 3];

        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneYFilterCoefficient[0] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 0];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneYFilterCoefficient[1] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 1];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneYFilterCoefficient[2] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 2];
        samplerStateParam.Avs.pMhwSamplerAvsTableParam->paMhwAvsCoeffParamExtra[i]
            .OneYFilterCoefficient[3] = (uint8_t)avs_params.piUVCoefsY[i * 4 + 3];
    }

    MOS_SafeFreeMemory(avs_params.piYCoefsX);

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SamplerAvsCalcScalingTable(
    MHW_AVS_PARAMS &avsParameters,
    MOS_FORMAT      SrcFormat,
    bool            bVertical,
    float           fLumaScale,
    float           fChromaScale,
    uint32_t        dwChromaSiting,
    bool            b8TapAdaptiveEnable)
{
    VP_FUNC_CALL();
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;
    MHW_PLANE  Plane;
    int32_t    iUvPhaseOffset;
    uint32_t   dwHwPhrase;
    uint32_t   YCoefTableSize;
    uint32_t   UVCoefTableSize;
    float      fLumaScaleParam;
    float      fChromaScaleParam;
    int32_t *  piYCoefsParam;
    int32_t *  piUVCoefsParam;
    float      fHPStrength;

    VP_PUBLIC_CHK_NULL_RETURN(avsParameters.piYCoefsY);
    VP_PUBLIC_CHK_NULL_RETURN(avsParameters.piYCoefsX);
    VP_PUBLIC_CHK_NULL_RETURN(avsParameters.piUVCoefsY);
    VP_PUBLIC_CHK_NULL_RETURN(avsParameters.piUVCoefsX);

    YCoefTableSize  = (NUM_POLYPHASE_Y_ENTRIES * NUM_HW_POLYPHASE_TABLES_G9 * sizeof(float));
    UVCoefTableSize = (NUM_POLYPHASE_UV_ENTRIES * NUM_HW_POLYPHASE_TABLES_G9 * sizeof(float));
    dwHwPhrase      = NUM_HW_POLYPHASE_TABLES_G9;

    fHPStrength    = 0.0F;
    piYCoefsParam  = bVertical ? avsParameters.piYCoefsY : avsParameters.piYCoefsX;
    piUVCoefsParam = bVertical ? avsParameters.piUVCoefsY : avsParameters.piUVCoefsX;

    // Recalculate Horizontal or Vertical scaling table
    if (SrcFormat != avsParameters.Format)  //|| fLumaScale != fLumaScaleParam || fChromaScale != fChromaScaleParam
    {
        MOS_ZeroMemory(piYCoefsParam, YCoefTableSize);
        MOS_ZeroMemory(piUVCoefsParam, UVCoefTableSize);

        // 4-tap filtering for RGformat G-channel if 8tap adaptive filter is not enabled.
        Plane = (IS_RGB32_FORMAT(SrcFormat) && !b8TapAdaptiveEnable) ? MHW_U_PLANE : MHW_Y_PLANE;

        // For 1x scaling in horizontal direction, use special coefficients for filtering
        // we don't do this when bForcePolyPhaseCoefs flag is set
        if (fLumaScale == 1.0F && !avsParameters.bForcePolyPhaseCoefs)
        {
            VPHAL_RENDER_CHK_STATUS_RETURN(SetNearestModeTable(
                piYCoefsParam,
                Plane,
                true));
            // If the 8-tap adaptive is enabled for all channel, then UV/RB use the same coefficient as Y/G
            // So, coefficient for UV/RB channels caculation can be passed
            if (!b8TapAdaptiveEnable)
            {
                if (fChromaScale == 1.0F)
                {
                    VPHAL_RENDER_CHK_STATUS_RETURN(SetNearestModeTable(
                        piUVCoefsParam,
                        MHW_U_PLANE,
                        true));
                }
                else
                {
                    if (dwChromaSiting & (bVertical ? MHW_CHROMA_SITING_VERT_TOP : MHW_CHROMA_SITING_HORZ_LEFT))
                    {
                        // No Chroma Siting
                        VPHAL_RENDER_CHK_STATUS_RETURN(CalcPolyphaseTablesUV(
                            piUVCoefsParam,
                            2.0F,
                            fChromaScale));
                    }
                    else
                    {
                        // Chroma siting offset needs to be added
                        if (dwChromaSiting & (bVertical ? MHW_CHROMA_SITING_VERT_CENTER : MHW_CHROMA_SITING_HORZ_CENTER))
                        {
                            iUvPhaseOffset = MOS_UF_ROUND(0.5F * 16.0F);  // U0.4
                        }
                        else  //if (ChromaSiting & (bVertical ? MHW_CHROMA_SITING_VERT_BOTTOM : MHW_CHROMA_SITING_HORZ_RIGHT))
                        {
                            iUvPhaseOffset = MOS_UF_ROUND(1.0F * 16.0F);  // U0.4
                        }

                        VPHAL_RENDER_CHK_STATUS_RETURN(CalcPolyphaseTablesUVOffset(
                            piUVCoefsParam,
                            3.0F,
                            fChromaScale,
                            iUvPhaseOffset));
                    }
                }
            }
        }
        else
        {
            // Clamp the Scaling Factor if > 1.0x
            fLumaScale = MOS_MIN(1.0F, fLumaScale);

            VPHAL_RENDER_CHK_STATUS_RETURN(CalcPolyphaseTablesY(
                piYCoefsParam,
                fLumaScale,
                Plane,
                SrcFormat,
                fHPStrength,
                true,
                dwHwPhrase));

            // If the 8-tap adaptive is enabled for all channel, then UV/RB use the same coefficient as Y/G
            // So, coefficient for UV/RB channels caculation can be passed
            if (!b8TapAdaptiveEnable)
            {
                {
                    if (fChromaScale == 1.0F)
                    {
                        VPHAL_RENDER_CHK_STATUS_RETURN(SetNearestModeTable(
                            piUVCoefsParam,
                            MHW_U_PLANE,
                            true));
                    }
                    else
                    {
                        // If Chroma Siting info is present
                        if (dwChromaSiting & (bVertical ? MHW_CHROMA_SITING_VERT_TOP : MHW_CHROMA_SITING_HORZ_LEFT))
                        {
                            // No Chroma Siting
                            VPHAL_RENDER_CHK_STATUS_RETURN(CalcPolyphaseTablesUV(
                                piUVCoefsParam,
                                2.0F,
                                fChromaScale));
                        }
                        else
                        {
                            // Chroma siting offset needs to be added
                            if (dwChromaSiting & (bVertical ? MHW_CHROMA_SITING_VERT_CENTER : MHW_CHROMA_SITING_HORZ_CENTER))
                            {
                                iUvPhaseOffset = MOS_UF_ROUND(0.5F * 16.0F);  // U0.4
                            }
                            else  //if (ChromaSiting & (bVertical ? MHW_CHROMA_SITING_VERT_BOTTOM : MHW_CHROMA_SITING_HORZ_RIGHT))
                            {
                                iUvPhaseOffset = MOS_UF_ROUND(1.0F * 16.0F);  // U0.4
                            }

                            VPHAL_RENDER_CHK_STATUS_RETURN(CalcPolyphaseTablesUVOffset(
                                piUVCoefsParam,
                                3.0F,
                                fChromaScale,
                                iUvPhaseOffset));
                        }
                    }
                }
            }
        }
    }
    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetNearestModeTable(
    int32_t *iCoefs,
    uint32_t dwPlane,
    bool     bBalancedFilter)
{
    VP_FUNC_CALL();
    uint32_t   dwNumEntries;
    uint32_t   dwOffset;
    uint32_t   i;
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;

    MHW_FUNCTION_ENTER;

    MHW_CHK_NULL(iCoefs);

    if (dwPlane == MHW_GENERIC_PLANE || dwPlane == MHW_Y_PLANE)
    {
        dwNumEntries = NUM_POLYPHASE_Y_ENTRIES;
        dwOffset     = 3;
    }
    else  // if (dwPlane == MHW_U_PLANE || dwPlane == MHW_V_PLANE)
    {
        dwNumEntries = NUM_POLYPHASE_UV_ENTRIES;
        dwOffset     = 1;
    }

    for (i = 0; i <= NUM_HW_POLYPHASE_TABLES / 2; i++)
    {
        iCoefs[i * dwNumEntries + dwOffset] = 0x40;
    }

    if (bBalancedFilter)
    {
        // Fix offset so that filter is balanced
        for (i = (NUM_HW_POLYPHASE_TABLES / 2 + 1); i < NUM_HW_POLYPHASE_TABLES; i++)
        {
            iCoefs[i * dwNumEntries + dwOffset + 1] = 0x40;
        }
    }

finish:
    return eStatus;
}

MOS_STATUS VpRenderCmdPacket::CalcPolyphaseTablesUV(
    int32_t *piCoefs,
    float    fLanczosT,
    float    fInverseScaleFactor)
{
    VP_FUNC_CALL();
    int32_t    phaseCount, tableCoefUnit, centerPixel, sumQuantCoefs;
    double     phaseCoefs[MHW_SCALER_UV_WIN_SIZE];
    double     startOffset, sf, base, sumCoefs, pos;
    int32_t    minCoef[MHW_SCALER_UV_WIN_SIZE];
    int32_t    maxCoef[MHW_SCALER_UV_WIN_SIZE];
    int32_t    i, j;
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;

    MHW_FUNCTION_ENTER;

    MHW_CHK_NULL(piCoefs);

    phaseCount    = MHW_TABLE_PHASE_COUNT;
    centerPixel   = (MHW_SCALER_UV_WIN_SIZE / 2) - 1;
    startOffset   = (double)(-centerPixel);
    tableCoefUnit = 1 << MHW_TBL_COEF_PREC;
    sf            = MOS_MIN(1.0, fInverseScaleFactor);  // Sf isn't used for upscaling

    MOS_ZeroMemory(piCoefs, sizeof(int32_t) * MHW_SCALER_UV_WIN_SIZE * phaseCount);
    MOS_ZeroMemory(minCoef, sizeof(minCoef));
    MOS_ZeroMemory(maxCoef, sizeof(maxCoef));

    if (sf < 1.0F)
    {
        fLanczosT = 2.0F;
    }

    for (i = 0; i < phaseCount; ++i, piCoefs += MHW_SCALER_UV_WIN_SIZE)
    {
        // Write all
        // Note - to shift by a half you need to a half to each phase.
        base     = startOffset - (double)(i) / (double)(phaseCount);
        sumCoefs = 0.0;

        for (j = 0; j < MHW_SCALER_UV_WIN_SIZE; ++j)
        {
            pos           = base + (double)j;
            phaseCoefs[j] = MosUtilities::MosLanczos((float)(pos * sf), MHW_SCALER_UV_WIN_SIZE, fLanczosT);
            sumCoefs += phaseCoefs[j];
        }
        // Normalize coefs and save
        for (j = 0; j < MHW_SCALER_UV_WIN_SIZE; ++j)
        {
            piCoefs[j] = (int32_t)floor((0.5 + (double)(tableCoefUnit) * (phaseCoefs[j] / sumCoefs)));

            //For debug purposes:
            minCoef[j] = MOS_MIN(minCoef[j], piCoefs[j]);
            maxCoef[j] = MOS_MAX(maxCoef[j], piCoefs[j]);
        }

        // Recalc center coef
        sumQuantCoefs = 0;
        for (j = 0; j < MHW_SCALER_UV_WIN_SIZE; ++j)
        {
            sumQuantCoefs += piCoefs[j];
        }

        // Fix center coef so that filter is balanced
        if (i <= phaseCount / 2)
        {
            piCoefs[centerPixel] -= sumQuantCoefs - tableCoefUnit;
        }
        else
        {
            piCoefs[centerPixel + 1] -= sumQuantCoefs - tableCoefUnit;
        }
    }

finish:
    return eStatus;
}

MOS_STATUS VpRenderCmdPacket::CalcPolyphaseTablesY(
    int32_t *  iCoefs,
    float      fScaleFactor,
    uint32_t   dwPlane,
    MOS_FORMAT srcFmt,
    float      fHPStrength,
    bool       bUse8x8Filter,
    uint32_t   dwHwPhase)
{
    VP_FUNC_CALL();
    uint32_t   dwNumEntries;
    uint32_t   dwTableCoefUnit;
    uint32_t   i, j;
    int32_t    k;
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;
    float      fPhaseCoefs[NUM_POLYPHASE_Y_ENTRIES];
    float      fPhaseCoefsCopy[NUM_POLYPHASE_Y_ENTRIES];
    float      fStartOffset;
    float      fHPFilter[3], fHPSum, fHPHalfPhase;  // Only used for Y_PLANE
    float      fBase, fPos, fSumCoefs;
    float      fLanczosT;
    int32_t    iCenterPixel;
    int32_t    iSumQuantCoefs;

    MHW_FUNCTION_ENTER;

    MHW_CHK_NULL(iCoefs);
    MHW_ASSERT((dwHwPhase == MHW_NUM_HW_POLYPHASE_TABLES) || (dwHwPhase == NUM_HW_POLYPHASE_TABLES));

    if (dwPlane == MHW_GENERIC_PLANE || dwPlane == MHW_Y_PLANE)
    {
        dwNumEntries = NUM_POLYPHASE_Y_ENTRIES;
    }
    else  // if (dwPlane == MHW_U_PLANE || dwPlane == MHW_V_PLANE)
    {
        dwNumEntries = NUM_POLYPHASE_UV_ENTRIES;
    }

    MOS_ZeroMemory(fPhaseCoefs, sizeof(fPhaseCoefs));
    MOS_ZeroMemory(fPhaseCoefsCopy, sizeof(fPhaseCoefsCopy));

    dwTableCoefUnit = 1 << MHW_AVS_TBL_COEF_PREC;
    iCenterPixel    = dwNumEntries / 2 - 1;
    fStartOffset    = (float)(-iCenterPixel);

    if ((IS_YUV_FORMAT(srcFmt) &&
            dwPlane != MHW_U_PLANE &&
            dwPlane != MHW_V_PLANE) ||
        ((IS_RGB32_FORMAT(srcFmt) ||
             srcFmt == Format_Y410 ||
             srcFmt == Format_AYUV) &&
            dwPlane == MHW_Y_PLANE))
    {
        if (fScaleFactor < 1.0F)
        {
            fLanczosT = 4.0F;
        }
        else
        {
            fLanczosT = 8.0F;
        }
    }
    else  // if (dwPlane == MHW_U_PLANE || dwPlane == MHW_V_PLANE || (IS_RGB_FORMAT(srcFmt) && dwPlane != MHW_V_PLANE))
    {
        fLanczosT = 2.0F;
    }

    for (i = 0; i < dwHwPhase; i++)
    {
        fBase     = fStartOffset - (float)i / (float)NUM_POLYPHASE_TABLES;
        fSumCoefs = 0.0F;

        for (j = 0; j < dwNumEntries; j++)
        {
            fPos = fBase + (float)j;

            if (bUse8x8Filter)
            {
                fPhaseCoefs[j] = fPhaseCoefsCopy[j] = MosUtilities::MosLanczos(fPos * fScaleFactor, dwNumEntries, fLanczosT);
            }
            else
            {
                fPhaseCoefs[j] = fPhaseCoefsCopy[j] = MosUtilities::MosLanczosG(fPos * fScaleFactor, NUM_POLYPHASE_5x5_Y_ENTRIES, fLanczosT);
            }

            fSumCoefs += fPhaseCoefs[j];
        }

        // Convolve with HP
        if (dwPlane == MHW_GENERIC_PLANE || dwPlane == MHW_Y_PLANE)
        {
            if (i <= NUM_POLYPHASE_TABLES / 2)
            {
                fHPHalfPhase = (float)i / (float)NUM_POLYPHASE_TABLES;
            }
            else
            {
                fHPHalfPhase = (float)(NUM_POLYPHASE_TABLES - i) / (float)NUM_POLYPHASE_TABLES;
            }
            fHPFilter[0] = fHPFilter[2] = -fHPStrength * MosUtilities::MosSinc(fHPHalfPhase * MOS_PI);
            fHPFilter[1]                = 1.0F + 2.0F * fHPStrength;

            for (j = 0; j < dwNumEntries; j++)
            {
                fHPSum = 0.0F;
                for (k = -1; k <= 1; k++)
                {
                    if ((((long)j + k) >= 0) && (j + k < dwNumEntries))
                    {
                        fHPSum += fPhaseCoefsCopy[(int32_t)j + k] * fHPFilter[k + 1];
                    }
                    fPhaseCoefs[j] = fHPSum;
                }
            }
        }

        // Normalize coefs and save
        iSumQuantCoefs = 0;
        for (j = 0; j < dwNumEntries; j++)
        {
            iCoefs[i * dwNumEntries + j] = (int32_t)floor(0.5F + (float)dwTableCoefUnit * fPhaseCoefs[j] / fSumCoefs);
            iSumQuantCoefs += iCoefs[i * dwNumEntries + j];
        }

        // Fix center coef so that filter is balanced
        if (i <= NUM_POLYPHASE_TABLES / 2)
        {
            iCoefs[i * dwNumEntries + iCenterPixel] -= iSumQuantCoefs - dwTableCoefUnit;
        }
        else
        {
            iCoefs[i * dwNumEntries + iCenterPixel + 1] -= iSumQuantCoefs - dwTableCoefUnit;
        }
    }

finish:
    return eStatus;
}

MOS_STATUS VpRenderCmdPacket::CalcPolyphaseTablesUVOffset(
    int32_t *piCoefs,
    float    fLanczosT,
    float    fInverseScaleFactor,
    int32_t  iUvPhaseOffset)
{
    VP_FUNC_CALL();
    int32_t    phaseCount, tableCoefUnit, centerPixel, sumQuantCoefs;
    double     phaseCoefs[MHW_SCALER_UV_WIN_SIZE];
    double     startOffset, sf, pos, sumCoefs, base;
    int32_t    minCoef[MHW_SCALER_UV_WIN_SIZE];
    int32_t    maxCoef[MHW_SCALER_UV_WIN_SIZE];
    int32_t    i, j;
    int32_t    adjusted_phase;
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;

    MHW_FUNCTION_ENTER;

    MHW_CHK_NULL(piCoefs);

    phaseCount    = MHW_TABLE_PHASE_COUNT;
    centerPixel   = (MHW_SCALER_UV_WIN_SIZE / 2) - 1;
    startOffset   = (double)(-centerPixel +
                           (double)iUvPhaseOffset / (double)(phaseCount));
    tableCoefUnit = 1 << MHW_TBL_COEF_PREC;

    MOS_ZeroMemory(minCoef, sizeof(minCoef));
    MOS_ZeroMemory(maxCoef, sizeof(maxCoef));
    MOS_ZeroMemory(piCoefs, sizeof(int32_t) * MHW_SCALER_UV_WIN_SIZE * phaseCount);

    sf = MOS_MIN(1.0, fInverseScaleFactor);  // Sf isn't used for upscaling
    if (sf < 1.0)
    {
        fLanczosT = 3.0;
    }

    for (i = 0; i < phaseCount; ++i, piCoefs += MHW_SCALER_UV_WIN_SIZE)
    {
        // Write all
        // Note - to shift by a half you need to a half to each phase.
        base     = startOffset - (double)(i) / (double)(phaseCount);
        sumCoefs = 0.0;

        for (j = 0; j < MHW_SCALER_UV_WIN_SIZE; ++j)
        {
            pos           = base + (double)j;
            phaseCoefs[j] = MosUtilities::MosLanczos((float)(pos * sf), 6 /*MHW_SCALER_UV_WIN_SIZE*/, fLanczosT);
            sumCoefs += phaseCoefs[j];
        }
        // Normalize coefs and save
        for (j = 0; j < MHW_SCALER_UV_WIN_SIZE; ++j)
        {
            piCoefs[j] = (int32_t)floor((0.5 + (double)(tableCoefUnit) * (phaseCoefs[j] / sumCoefs)));

            // For debug purposes:
            minCoef[j] = MOS_MIN(minCoef[j], piCoefs[j]);
            maxCoef[j] = MOS_MAX(maxCoef[j], piCoefs[j]);
        }

        // Recalc center coef
        sumQuantCoefs = 0;
        for (j = 0; j < MHW_SCALER_UV_WIN_SIZE; ++j)
        {
            sumQuantCoefs += piCoefs[j];
        }

        // Fix center coef so that filter is balanced
        adjusted_phase = i - iUvPhaseOffset;
        if (adjusted_phase <= phaseCount / 2)
        {
            piCoefs[centerPixel] -= sumQuantCoefs - tableCoefUnit;
        }
        else  // if(adjusted_phase < phaseCount)
        {
            piCoefs[centerPixel + 1] -= sumQuantCoefs - tableCoefUnit;
        }
    }

finish:
    return eStatus;
}

MOS_STATUS VpRenderCmdPacket::SubmitWithMultiKernel(MOS_COMMAND_BUFFER *commandBuffer, uint8_t packetPhase)
{
    VP_FUNC_CALL();
    PMOS_INTERFACE                  pOsInterface = nullptr;
    MOS_STATUS                      eStatus      = MOS_STATUS_SUCCESS;
    uint32_t                        dwSyncTag    = 0;
    int32_t                         i = 0, iRemaining = 0;
    PMHW_MI_INTERFACE               pMhwMiInterface     = nullptr;
    MhwRenderInterface *            pMhwRender          = nullptr;
    MHW_MEDIA_STATE_FLUSH_PARAM     FlushParam          = {};
    bool                            bEnableSLM          = false;
    RENDERHAL_GENERIC_PROLOG_PARAMS GenericPrologParams = {};
    MOS_RESOURCE                    GpuStatusBuffer     = {};
    MediaPerfProfiler *             pPerfProfiler       = nullptr;
    MOS_CONTEXT *                   pOsContext          = nullptr;
    PMHW_MI_MMIOREGISTERS           pMmioRegisters      = nullptr;
    std::shared_ptr<mhw::mi::Itf>   m_miItf             = nullptr;

    RENDER_PACKET_CHK_NULL_RETURN(m_renderHal);
    RENDER_PACKET_CHK_NULL_RETURN(m_renderHal->pMhwRenderInterface);
    RENDER_PACKET_CHK_NULL_RETURN(m_renderHal->pMhwMiInterface);
    RENDER_PACKET_CHK_NULL_RETURN(m_renderHal->pMhwRenderInterface->GetMmioRegisters());
    RENDER_PACKET_CHK_NULL_RETURN(m_renderHal->pOsInterface);
    RENDER_PACKET_CHK_NULL_RETURN(m_renderHal->pOsInterface->pOsContext);

    eStatus         = MOS_STATUS_UNKNOWN;
    pOsInterface    = m_renderHal->pOsInterface;
    pMhwMiInterface = m_renderHal->pMhwMiInterface;
    pMhwRender      = m_renderHal->pMhwRenderInterface;
    iRemaining      = 0;
    FlushParam      = g_cRenderHal_InitMediaStateFlushParams;
    pPerfProfiler   = m_renderHal->pPerfProfiler;
    pOsContext      = pOsInterface->pOsContext;
    pMmioRegisters  = pMhwRender->GetMmioRegisters();
    m_miItf         = std::static_pointer_cast<mhw::mi::Itf>(pMhwMiInterface->GetNewMiInterface());

    RENDER_PACKET_CHK_STATUS_RETURN(SetPowerMode(kernelCombinedFc));

    RENDER_PACKET_CHK_STATUS_RETURN(SetMediaFrameTracking(GenericPrologParams));

    // Initialize command buffer and insert prolog
    RENDER_PACKET_CHK_STATUS_RETURN(m_renderHal->pfnInitCommandBuffer(m_renderHal, commandBuffer, &GenericPrologParams));

    RENDER_PACKET_CHK_STATUS_RETURN(pPerfProfiler->AddPerfCollectStartCmd((void *)m_renderHal, pOsInterface, pMhwMiInterface, commandBuffer));

    // Write timing data for 3P budget
    RENDER_PACKET_CHK_STATUS_RETURN(m_renderHal->pfnSendTimingData(m_renderHal, commandBuffer, true));

    bEnableSLM = false;  // Media walker first
    RENDER_PACKET_CHK_STATUS_RETURN(m_renderHal->pfnSetCacheOverrideParams(
        m_renderHal,
        &m_renderHal->L3CacheSettings,
        bEnableSLM));

    // Flush media states
    VP_RENDER_CHK_STATUS_RETURN(SendMediaStates(m_renderHal, commandBuffer));

    // Write back GPU Status tag
    if (!pOsInterface->bEnableKmdMediaFrameTracking)
    {
        RENDER_PACKET_CHK_STATUS_RETURN(m_renderHal->pfnSendRcsStatusTag(m_renderHal, commandBuffer));
    }

    RENDER_PACKET_CHK_STATUS_RETURN(pPerfProfiler->AddPerfCollectEndCmd((void *)m_renderHal, pOsInterface, pMhwMiInterface, commandBuffer));

    // Write timing data for 3P budget
    RENDER_PACKET_CHK_STATUS_RETURN(m_renderHal->pfnSendTimingData(m_renderHal, commandBuffer, false));

    if (m_miItf)
    {
        SETPAR_AND_ADDCMD(PIPE_CONTROL, m_miItf, commandBuffer);
    }
    else
    {
        MHW_PIPE_CONTROL_PARAMS PipeControlParams;

        MOS_ZeroMemory(&PipeControlParams, sizeof(PipeControlParams));
        PipeControlParams.dwFlushMode                   = MHW_FLUSH_WRITE_CACHE;
        PipeControlParams.bGenericMediaStateClear       = true;
        PipeControlParams.bIndirectStatePointersDisable = true;
        PipeControlParams.bDisableCSStall               = false;

        RENDER_PACKET_CHK_NULL_RETURN(pOsInterface->pfnGetSkuTable);
        auto *skuTable = pOsInterface->pfnGetSkuTable(pOsInterface);
        if (skuTable && MEDIA_IS_SKU(skuTable, FtrEnablePPCFlush))
        {
            // Add PPC fulsh
            PipeControlParams.bPPCFlush = true;
        }
        RENDER_PACKET_CHK_STATUS_RETURN(pMhwMiInterface->AddPipeControl(commandBuffer, nullptr, &PipeControlParams));
    }

    if (MEDIA_IS_WA(m_renderHal->pWaTable, WaSendDummyVFEafterPipelineSelect))
    {
        MHW_VFE_PARAMS VfeStateParams       = {};
        VfeStateParams.dwNumberofURBEntries = 1;
        RENDER_PACKET_CHK_STATUS_RETURN(pMhwRender->AddMediaVfeCmd(commandBuffer, &VfeStateParams));
    }

    // Add media flush command in case HW not cleaning the media state
    if (MEDIA_IS_WA(m_renderHal->pWaTable, WaMSFWithNoWatermarkTSGHang))
    {
        FlushParam.bFlushToGo = true;
        if (m_walkerType == WALKER_TYPE_MEDIA)
        {
            FlushParam.ui8InterfaceDescriptorOffset = m_mediaWalkerParams.InterfaceDescriptorOffset;
        }
        else
        {
            RENDER_PACKET_ASSERTMESSAGE("ERROR, pWalkerParams is nullptr and cannot get InterfaceDescriptorOffset.");
        }
        if (m_miItf)
        {
            SETPAR_AND_ADDCMD(MEDIA_STATE_FLUSH, m_miItf, commandBuffer);
        }
        else
        {
            RENDER_PACKET_CHK_STATUS_RETURN(pMhwMiInterface->AddMediaStateFlush(commandBuffer, nullptr, &FlushParam));
        }
    }
    else if (MEDIA_IS_WA(m_renderHal->pWaTable, WaAddMediaStateFlushCmd))
    {
        RENDER_PACKET_CHK_STATUS_RETURN(pMhwMiInterface->AddMediaStateFlush(commandBuffer, nullptr, &FlushParam));
    }

    if (pBatchBuffer)
    {
        // Send Batch Buffer end command (HW/OS dependent)
        if (m_miItf)
        {
            m_miItf->AddMiBatchBufferEnd(commandBuffer, nullptr);
        }
        else
        {
            RENDER_PACKET_CHK_STATUS_RETURN(pMhwMiInterface->AddMiBatchBufferEnd(commandBuffer, nullptr));
        }
    }
    else if (IsMiBBEndNeeded(pOsInterface))
    {
        // Send Batch Buffer end command for 1st level Batch Buffer
        if (m_miItf)
        {
            m_miItf->AddMiBatchBufferEnd(commandBuffer, nullptr);
        }
        else
        {
            RENDER_PACKET_CHK_STATUS_RETURN(pMhwMiInterface->AddMiBatchBufferEnd(commandBuffer, nullptr));
        }
    }
    else if (m_renderHal->pOsInterface->bNoParsingAssistanceInKmd)
    {
        if (m_miItf)
        {
            m_miItf->AddMiBatchBufferEnd(commandBuffer, nullptr);
        }
        else
        {
            RENDER_PACKET_CHK_STATUS_RETURN(pMhwMiInterface->AddMiBatchBufferEnd(commandBuffer, nullptr));
        }
    }

    // Return unused command buffer space to OS
    pOsInterface->pfnReturnCommandBuffer(pOsInterface, commandBuffer, 0);

    MOS_NULL_RENDERING_FLAGS NullRenderingFlags;

    NullRenderingFlags =
        pOsInterface->pfnGetNullHWRenderFlags(pOsInterface);

    if ((NullRenderingFlags.VPLgca ||
            NullRenderingFlags.VPGobal) == false)
    {
        dwSyncTag = m_renderHal->pStateHeap->dwNextTag++;

        // Set media state and batch buffer as busy
        m_renderHal->pStateHeap->pCurMediaState->bBusy = true;
        if (pBatchBuffer)
        {
            pBatchBuffer->bBusy     = true;
            pBatchBuffer->dwSyncTag = dwSyncTag;
        }
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::DumpOutput()
{
    VP_FUNC_CALL();

    return MOS_STATUS_SUCCESS;
}

void VpRenderCmdPacket::PrintWalkerParas(MHW_WALKER_PARAMS &WalkerParams)
{
#if (_DEBUG || _RELEASE_INTERNAL)
    VP_RENDER_VERBOSEMESSAGE("WalkerParams: InterfaceDescriptorOffset = %x, CmWalkerEnable = %x, ColorCountMinusOne = %x, UseScoreboard = %x, ScoreboardMask = %x, MidLoopUnitX = %x, MidLoopUnitY = %x, MiddleLoopExtraSteps = %x",
        WalkerParams.InterfaceDescriptorOffset,
        WalkerParams.CmWalkerEnable,
        WalkerParams.ColorCountMinusOne,
        WalkerParams.UseScoreboard,
        WalkerParams.ScoreboardMask,
        WalkerParams.MidLoopUnitX,
        WalkerParams.MidLoopUnitY,
        WalkerParams.MiddleLoopExtraSteps);
    VP_RENDER_VERBOSEMESSAGE("WalkerParams: GroupIdLoopSelect = %x, InlineDataLength = %x, pInlineData = %x, dwLocalLoopExecCount = %x, dwGlobalLoopExecCount = %x, WalkerMode = %x, BlockResolution = %x, LocalStart = %x",
        WalkerParams.GroupIdLoopSelect,
        WalkerParams.InlineDataLength,
        WalkerParams.pInlineData,
        WalkerParams.dwLocalLoopExecCount,
        WalkerParams.dwGlobalLoopExecCount,
        WalkerParams.WalkerMode,
        WalkerParams.BlockResolution,
        WalkerParams.LocalStart);
    VP_RENDER_VERBOSEMESSAGE("WalkerParams: LocalEnd = %x, LocalOutLoopStride = %x, LocalInnerLoopUnit = %x, GlobalResolution = %x, GlobalStart = %x, GlobalOutlerLoopStride = %x, GlobalInnerLoopUnit = %x, bAddMediaFlush = %x, bRequestSingleSlice = %x, IndirectDataLength = %x, IndirectDataStartAddress = %x",
        WalkerParams.LocalEnd,
        WalkerParams.LocalOutLoopStride,
        WalkerParams.LocalInnerLoopUnit,
        WalkerParams.GlobalResolution,
        WalkerParams.GlobalStart,
        WalkerParams.GlobalOutlerLoopStride,
        WalkerParams.GlobalInnerLoopUnit,
        WalkerParams.bAddMediaFlush,
        WalkerParams.bRequestSingleSlice,
        WalkerParams.IndirectDataLength,
        WalkerParams.IndirectDataStartAddress);
#endif
}

MOS_STATUS VpRenderCmdPacket::SendMediaStates(
    PRENDERHAL_INTERFACE pRenderHal,
    PMOS_COMMAND_BUFFER  pCmdBuffer)
{
    VP_FUNC_CALL();
    PMOS_INTERFACE                  pOsInterface          = nullptr;
    MhwRenderInterface *            pMhwRender            = nullptr;
    PMHW_MI_INTERFACE               pMhwMiInterface       = nullptr;
    PRENDERHAL_STATE_HEAP           pStateHeap            = nullptr;
    MOS_STATUS                      eStatus               = MOS_STATUS_SUCCESS;
    MHW_VFE_PARAMS *                pVfeStateParams       = nullptr;
    MOS_CONTEXT *                   pOsContext            = nullptr;
    MHW_MI_LOAD_REGISTER_IMM_PARAMS loadRegisterImmParams = {};
    PMHW_MI_MMIOREGISTERS           pMmioRegisters        = nullptr;
    MOS_OCA_BUFFER_HANDLE           hOcaBuf               = 0;

    //---------------------------------------
    MHW_RENDERHAL_CHK_NULL(pRenderHal);
    MHW_RENDERHAL_CHK_NULL(pRenderHal->pMhwRenderInterface);
    MHW_RENDERHAL_CHK_NULL(pRenderHal->pMhwMiInterface);
    MHW_RENDERHAL_CHK_NULL(pRenderHal->pStateHeap);
    MHW_RENDERHAL_CHK_NULL(pRenderHal->pRenderHalPltInterface);
    MHW_RENDERHAL_ASSERT(pRenderHal->pStateHeap->bGshLocked);
    MHW_RENDERHAL_CHK_NULL(pRenderHal->pMhwRenderInterface->GetMmioRegisters());

    //---------------------------------------
    pOsInterface    = pRenderHal->pOsInterface;
    pMhwRender      = pRenderHal->pMhwRenderInterface;
    pMhwMiInterface = pRenderHal->pMhwMiInterface;
    pStateHeap      = pRenderHal->pStateHeap;
    pOsContext      = pOsInterface->pOsContext;
    pMmioRegisters  = pMhwRender->GetMmioRegisters();

    // Setup L3$ Config, LRI commands used here & hence must be launched from a secure bb
    pRenderHal->L3CacheSettings.bEnableSLM = (m_walkerType == WALKER_TYPE_COMPUTE && m_slmSize > 0) ? true : false;
    MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnEnableL3Caching(pRenderHal, &pRenderHal->L3CacheSettings));

    // Send L3 Cache Configuration
    MHW_RENDERHAL_CHK_STATUS(pMhwRender->SetL3Cache(pCmdBuffer));

    MHW_RENDERHAL_CHK_STATUS(pMhwRender->EnablePreemption(pCmdBuffer));

    // Send Pipeline Select command
    MHW_RENDERHAL_CHK_STATUS(pMhwRender->AddPipelineSelectCmd(pCmdBuffer, (m_walkerType == WALKER_TYPE_COMPUTE) ? true : false));

    // The binding table for surface states is at end of command buffer. No need to add it to indirect state heap.
    HalOcaInterface::OnIndirectState(*pCmdBuffer, *pOsContext, pRenderHal->StateBaseAddressParams.presInstructionBuffer, pStateHeap->CurIDEntryParams.dwKernelOffset, false, pStateHeap->iKernelUsedForDump);

    // Send State Base Address command
    MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnSendStateBaseAddress(pRenderHal, pCmdBuffer));

    if (pRenderHal->bComputeContextInUse)
    {
        pRenderHal->pRenderHalPltInterface->SendTo3DStateBindingTablePoolAlloc(pRenderHal, pCmdBuffer);
    }

    // Send Surface States
    MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnSendSurfaces(pRenderHal, pCmdBuffer));

    // Send SIP State if ASM debug enabled
    if (pRenderHal->bIsaAsmDebugEnable)
    {
        MHW_RENDERHAL_CHK_STATUS(pMhwRender->AddSipStateCmd(pCmdBuffer,
            &pRenderHal->SipStateParams));
    }

    pVfeStateParams = pRenderHal->pRenderHalPltInterface->GetVfeStateParameters();
    if (!pRenderHal->bComputeContextInUse)
    {
        // set VFE State
        MHW_RENDERHAL_CHK_STATUS(pMhwRender->AddMediaVfeCmd(pCmdBuffer, pVfeStateParams));
    }
    else
    {
        // set CFE State
        MHW_RENDERHAL_CHK_STATUS(pMhwRender->AddCfeStateCmd(pCmdBuffer, pVfeStateParams));
    }

    // Send CURBE Load
    if (!pRenderHal->bComputeContextInUse)
    {
        MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnSendCurbeLoad(pRenderHal, pCmdBuffer));
    }

    // Send Interface Descriptor Load
    if (!pRenderHal->bComputeContextInUse)
    {
        MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnSendMediaIdLoad(pRenderHal, pCmdBuffer));
    }

    // Send Chroma Keys
    MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnSendChromaKey(pRenderHal, pCmdBuffer));

    // Send Palettes in use
    MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnSendPalette(pRenderHal, pCmdBuffer));

    HalOcaInterface::OnDispatch(*pCmdBuffer, *pOsContext, *pRenderHal->pMhwMiInterface, *pMmioRegisters);

    for (uint32_t kernelIndex = 0; kernelIndex < m_kernelRenderData.size(); kernelIndex++)
    {
        auto it = m_kernelRenderData.find(kernelIndex);
        if (it == m_kernelRenderData.end())
        {
            eStatus = MOS_STATUS_INVALID_PARAMETER;
            goto finish;
        }

        if (kernelIndex > 0 && it->second.walkerParam.bSyncFlag)
        {
            MHW_PIPE_CONTROL_PARAMS pipeCtlParams = g_cRenderHal_InitPipeControlParams;
            pipeCtlParams.dwPostSyncOp            = MHW_FLUSH_NOWRITE;
            pipeCtlParams.dwFlushMode             = MHW_FLUSH_CUSTOM;
            pipeCtlParams.bInvalidateTextureCache = true;
            pipeCtlParams.bFlushRenderTargetCache = true;
            MHW_RENDERHAL_CHK_STATUS(pMhwMiInterface->AddPipeControl(pCmdBuffer,
                nullptr,
                &pipeCtlParams));
        }

        if (m_walkerType == WALKER_TYPE_MEDIA)
        {
            MOS_ZeroMemory(&m_mediaWalkerParams, sizeof(m_mediaWalkerParams));

            MHW_RENDERHAL_CHK_STATUS(PrepareMediaWalkerParams(it->second.walkerParam, m_mediaWalkerParams));

            MHW_RENDERHAL_CHK_STATUS(pMhwRender->AddMediaObjectWalkerCmd(
                pCmdBuffer,
                &m_mediaWalkerParams));

            PrintWalkerParas(m_mediaWalkerParams);
        }
        else if (m_walkerType == WALKER_TYPE_COMPUTE)
        {
            MOS_ZeroMemory(&m_gpgpuWalkerParams, sizeof(m_gpgpuWalkerParams));

            MHW_RENDERHAL_CHK_STATUS(PrepareComputeWalkerParams(it->second.walkerParam, m_gpgpuWalkerParams));

            if (m_submissionMode == MULTI_KERNELS_WITH_MULTI_MEDIA_STATES && m_bindingtableMode == MULTI_KERNELS_WITH_MULTI_BINDINGTABLES)
            {
                pRenderHal->pStateHeap->pCurMediaState = it->second.mediaState;
                MHW_RENDERHAL_CHK_NULL(pRenderHal->pStateHeap->pCurMediaState);
                pRenderHal->iKernelAllocationID        = it->second.kernelAllocationID;
                pRenderHal->pStateHeap->pCurMediaState->bBusy = true;
            }

            MHW_RENDERHAL_CHK_STATUS(pRenderHal->pRenderHalPltInterface->SendComputeWalker(
                pRenderHal,
                pCmdBuffer,
                &m_gpgpuWalkerParams));

             PrintWalkerParas(m_mediaWalkerParams);
        }
        else
        {
            eStatus = MOS_STATUS_UNIMPLEMENTED;
            goto finish;
        }
    }

    // This need not be secure, since PPGTT will be used here. But moving this after
    // L3 cache configuration will delay UMD from fetching another media state.
    // Send Sync Tag
    MHW_RENDERHAL_CHK_STATUS(pRenderHal->pfnSendSyncTag(pRenderHal, pCmdBuffer));

    m_kernelRenderData.clear();

finish:
    return eStatus;
}

MOS_STATUS VpRenderCmdPacket::SetDiFmdParams(PRENDER_DI_FMD_PARAMS params)
{
    VP_FUNC_CALL();

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetFcParams(PRENDER_FC_PARAMS params)
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(params);

    m_kernelConfigs.insert(std::make_pair(params->kernelId, (void *)params));

    KERNEL_PARAMS kernelParams = {};
    kernelParams.kernelId      = params->kernelId;
    m_renderKernelParams.push_back(kernelParams);

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS VpRenderCmdPacket::SetHdr3DLutParams(
    PRENDER_HDR_3DLUT_CAL_PARAMS params)
{
    VP_FUNC_CALL();
    VP_RENDER_CHK_NULL_RETURN(params);

    m_kernelConfigs.insert(std::make_pair(params->kernelId, (void *)params));

    KERNEL_PARAMS kernelParams = {};
    kernelParams.kernelId = params->kernelId;
    // kernelArgs will be initialized in VpRenderHdr3DLutKernel::Init with
    // kernel.GetKernelArgs().
    kernelParams.kernelThreadSpace.uWidth = params->threadWidth;
    kernelParams.kernelThreadSpace.uHeight = params->threadHeight;
    kernelParams.kernelArgs = params->kernelArgs;
    kernelParams.syncFlag = true;
    m_renderKernelParams.push_back(kernelParams);

    return MOS_STATUS_SUCCESS;
}

MHW_SETPAR_DECL_SRC(PIPE_CONTROL, VpRenderCmdPacket)
{
    MOS_ZeroMemory(&params, sizeof(params));
    params.dwFlushMode                   = MHW_FLUSH_WRITE_CACHE;
    params.bGenericMediaStateClear       = true;
    params.bIndirectStatePointersDisable = true;
    params.bDisableCSStall               = false;

    RENDER_PACKET_CHK_NULL_RETURN(m_osInterface);
    RENDER_PACKET_CHK_NULL_RETURN(m_osInterface->pfnGetSkuTable);
    auto *skuTable = m_osInterface->pfnGetSkuTable(m_osInterface);
    if (skuTable && MEDIA_IS_SKU(skuTable, FtrEnablePPCFlush))
    {
        // Add PPC fulsh
        params.bPPCFlush = true;
    }

    return MOS_STATUS_SUCCESS;
}
}  // namespace vp
