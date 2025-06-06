/*
* Copyright (c) 2020-2021, Intel Corporation
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
//! \file     media_copy.cpp
//! \brief    Common interface and structure used in media copy
//! \details  Common interface and structure used in media copy which are platform independent
//!

#include "media_copy.h"
#include "vphal_debug.h"

MediaCopyBaseState::MediaCopyBaseState():
    m_osInterface(nullptr)
{

}

MediaCopyBaseState::~MediaCopyBaseState()
{
    MOS_STATUS              eStatus;

    if (m_mhwInterfaces)
    {
        if (m_mhwInterfaces->m_cpInterface)
        {
            Delete_MhwCpInterface(m_mhwInterfaces->m_cpInterface);
            m_mhwInterfaces->m_cpInterface = nullptr;
        }
        MOS_Delete(m_mhwInterfaces->m_miInterface);
        MOS_Delete(m_mhwInterfaces->m_veboxInterface);
        MOS_Delete(m_mhwInterfaces->m_bltInterface);
        MOS_Delete(m_mhwInterfaces->m_renderInterface);
        MOS_Delete(m_mhwInterfaces);
        m_mhwInterfaces = nullptr;
    }

    if (m_osInterface)
    {
        m_osInterface->pfnDestroy(m_osInterface, false);
        MOS_FreeMemory(m_osInterface);
        m_osInterface = nullptr;
    }

    if (m_inUseGPUMutex)
    {
        MosUtilities::MosDestroyMutex(m_inUseGPUMutex);
        m_inUseGPUMutex = nullptr;
    }

   #if (_DEBUG || _RELEASE_INTERNAL)
    if (m_surfaceDumper != nullptr)
    {
       MOS_Delete(m_surfaceDumper);
       m_surfaceDumper = nullptr;
    }
   #endif
}

//!
//! \brief    init Media copy
//! \details  init func.
//! \param    none
//! \return   MOS_STATUS
//!           Return MOS_STATUS_SUCCESS if success, otherwise return failed.
//!
MOS_STATUS MediaCopyBaseState::Initialize(PMOS_INTERFACE osInterface, MhwInterfaces *mhwInterfaces)
{
    if (m_inUseGPUMutex == nullptr)
    {
        m_inUseGPUMutex     = MosUtilities::MosCreateMutex();
        MCPY_CHK_NULL_RETURN(m_inUseGPUMutex);
    }

   #if (_DEBUG || _RELEASE_INTERNAL)
    if (m_surfaceDumper == nullptr)
    {
       m_surfaceDumper = MOS_New(VphalSurfaceDumper, osInterface);
       MOS_OS_CHK_NULL_RETURN(m_surfaceDumper);
    }
   #endif
    return MOS_STATUS_SUCCESS;
}

//!
//! \brief    check copy capability.
//! \details  to determine surface copy is supported or not.
//! \param    none
//! \return   MOS_STATUS
//!           Return MOS_STATUS_SUCCESS if support, otherwise return unspoort.
//!
MOS_STATUS MediaCopyBaseState::CapabilityCheck()
{
    // init hw enigne caps.
    m_mcpyEngineCaps.engineVebox  = 1;
    m_mcpyEngineCaps.engineBlt    = 1;
    m_mcpyEngineCaps.engineRender = 1;

    // derivate class specific check. include HW engine avaliable check.
    MCPY_CHK_STATUS_RETURN(FeatureSupport(m_mcpySrc.OsRes, m_mcpyDst.OsRes, m_mcpySrc, m_mcpyDst, m_mcpyEngineCaps));

    // common policy check
    // legal check
    // Blt engine does not support protection, allow the copy if dst is staging buffer in system mem
    if (m_mcpySrc.CpMode == MCPY_CPMODE_CP && m_mcpyDst.CpMode == MCPY_CPMODE_CLEAR && !m_allowCPBltCopy)
    {
        MCPY_ASSERTMESSAGE("illegal usage");
        return MOS_STATUS_INVALID_PARAMETER;
    }

    // vebox cap check.
    if (!IsVeboxCopySupported(m_mcpySrc.OsRes, m_mcpyDst.OsRes) || // format check, implemented on Gen derivate class.
        (m_mcpyDst.CompressionMode == MOS_MMC_RC) || // compression check
        m_mcpySrc.bAuxSuface)
    {
         m_mcpyEngineCaps.engineVebox = false;
    }

    // Eu cap check.
    if (!RenderFormatSupportCheck(m_mcpySrc.OsRes, m_mcpyDst.OsRes) || // format check, implemented on Gen derivate class.
        (m_mcpyDst.CompressionMode == MOS_MMC_MC) ||
        m_mcpySrc.bAuxSuface)
    {
        m_mcpyEngineCaps.engineRender = false;
    }

    // blt check.
    if ((m_mcpySrc.CompressionMode != MOS_MMC_DISABLED) ||
        (m_mcpyDst.CompressionMode != MOS_MMC_DISABLED))
    {
        m_mcpyEngineCaps.engineBlt = false;
    }

    if (!m_mcpyEngineCaps.engineVebox && !m_mcpyEngineCaps.engineBlt && !m_mcpyEngineCaps.engineRender)
    {
        return MOS_STATUS_INVALID_PARAMETER; // unsupport copy on each hw engine.
    }

    return MOS_STATUS_SUCCESS;
}

//!
//! \brief    surface copy pre process.
//! \details  pre process before doing surface copy.
//! \param    none
//! \return   MOS_STATUS
//!           Return MOS_STATUS_SUCCESS if support, otherwise return unspoort.
//!
MOS_STATUS MediaCopyBaseState::PreProcess(MCPY_METHOD preferMethod)
{
    return MOS_STATUS_SUCCESS;
}

//!
//! \brief    dispatch copy task if support.
//! \details  dispatch copy task to HW engine (vebox, EU, Blt) based on customer and default.
//! \param    src
//!           [in] Pointer to source surface
//! \param    dst
//!           [in] Pointer to destination surface
//! \return   MOS_STATUS
//!           Return MOS_STATUS_SUCCESS if support, otherwise return unspoort.
//!
MOS_STATUS MediaCopyBaseState::CopyEnigneSelect(MCPY_METHOD preferMethod)
{
    // assume perf render > vebox > blt. blt data should be measured.
    // driver should make sure there is at least one he can process copy even customer choice doesn't match caps.
    switch (preferMethod)
    {
        case MCPY_METHOD_PERFORMANCE:
            m_mcpyEngine = m_mcpyEngineCaps.engineRender?MCPY_ENGINE_RENDER:(m_mcpyEngineCaps.engineBlt ? MCPY_ENGINE_BLT : MCPY_ENGINE_VEBOX);
            break;
        case MCPY_METHOD_BALANCE:
            m_mcpyEngine = m_mcpyEngineCaps.engineVebox?MCPY_ENGINE_VEBOX:(m_mcpyEngineCaps.engineBlt?MCPY_ENGINE_BLT:MCPY_ENGINE_RENDER);
            break;
        case MCPY_METHOD_POWERSAVING:
            m_mcpyEngine = m_mcpyEngineCaps.engineBlt?MCPY_ENGINE_BLT:(m_mcpyEngineCaps.engineVebox?MCPY_ENGINE_VEBOX:MCPY_ENGINE_RENDER);
            break;
        default:
            break;
    }

    return MOS_STATUS_SUCCESS;
}

//!
//! \brief    surface copy func.
//! \details  copy surface.
//! \param    src
//!           [in] Pointer to source surface
//! \param    dst
//!           [in] Pointer to destination surface
//! \return   MOS_STATUS
//!           Return MOS_STATUS_SUCCESS if support, otherwise return unspoort.
//!
MOS_STATUS MediaCopyBaseState::SurfaceCopy(PMOS_RESOURCE src, PMOS_RESOURCE dst, MCPY_METHOD preferMethod)
{
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;

    MOS_SURFACE ResDetails;
    MOS_ZeroMemory(&ResDetails, sizeof(MOS_SURFACE));
    MCPY_CHK_STATUS_RETURN(m_osInterface->pfnGetResourceInfo(m_osInterface, src, &ResDetails));
    m_mcpySrc.CompressionMode = ResDetails.CompressionMode;
    m_mcpySrc.CpMode          = src->pGmmResInfo->GetSetCpSurfTag(false, 0)?MCPY_CPMODE_CP:MCPY_CPMODE_CLEAR;
    m_mcpySrc.TileMode        = ResDetails.TileType;
    m_mcpySrc.OsRes           = src;

    MOS_ZeroMemory(&ResDetails, sizeof(MOS_SURFACE));
    MCPY_CHK_STATUS_RETURN(m_osInterface->pfnGetResourceInfo(m_osInterface, dst, &ResDetails));
    m_mcpyDst.CompressionMode = ResDetails.CompressionMode;
    m_mcpyDst.CpMode          = dst->pGmmResInfo->GetSetCpSurfTag(false, 0)?MCPY_CPMODE_CP:MCPY_CPMODE_CLEAR;
    m_mcpyDst.TileMode        = ResDetails.TileType;
    m_mcpyDst.OsRes           = dst;

    MCPY_CHK_STATUS_RETURN(PreProcess(preferMethod));

    MCPY_CHK_STATUS_RETURN(CapabilityCheck());

    CopyEnigneSelect(preferMethod);

    MCPY_CHK_STATUS_RETURN(TaskDispatch());

    return eStatus;
}

#if (_DEBUG || _RELEASE_INTERNAL)
MOS_STATUS MediaCopyBaseState::CloneResourceInfo(PVPHAL_SURFACE pVphalSurface, PMOS_SURFACE pMosSurface)
{
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;

    if(pVphalSurface == nullptr || pMosSurface == nullptr)
    {
        return MOS_STATUS_INVALID_PARAMETER;
    }

    pVphalSurface->SurfType          = SURF_NONE;
    pVphalSurface->OsResource        = pMosSurface->OsResource;
    pVphalSurface->dwWidth           = pMosSurface->dwWidth;
    pVphalSurface->dwHeight          = pMosSurface->dwHeight;
    pVphalSurface->dwPitch           = pMosSurface->dwPitch;
    pVphalSurface->Format            = pMosSurface->Format;
    pVphalSurface->TileType          = pMosSurface->TileType;
    pVphalSurface->TileModeGMM       = pMosSurface->TileModeGMM;
    pVphalSurface->bGMMTileEnabled   = pMosSurface->bGMMTileEnabled;
    pVphalSurface->dwDepth           = pMosSurface->dwDepth;
    pVphalSurface->dwSlicePitch      = pMosSurface->dwSlicePitch;
    pVphalSurface->dwOffset          = pMosSurface->dwOffset;
    pVphalSurface->bCompressible     = pMosSurface->bCompressible;
    pVphalSurface->bIsCompressed     = pMosSurface->bIsCompressed;
    pVphalSurface->CompressionMode   = pMosSurface->CompressionMode;
    pVphalSurface->CompressionFormat = pMosSurface->CompressionFormat;

    pVphalSurface->YPlaneOffset.iLockSurfaceOffset = pMosSurface->YPlaneOffset.iLockSurfaceOffset;
    pVphalSurface->YPlaneOffset.iSurfaceOffset     = pMosSurface->YPlaneOffset.iSurfaceOffset;
    pVphalSurface->YPlaneOffset.iXOffset           = pMosSurface->YPlaneOffset.iXOffset;
    pVphalSurface->YPlaneOffset.iYOffset           = pMosSurface->YPlaneOffset.iYOffset;

    pVphalSurface->UPlaneOffset.iLockSurfaceOffset = pMosSurface->UPlaneOffset.iLockSurfaceOffset;
    pVphalSurface->UPlaneOffset.iSurfaceOffset     = pMosSurface->UPlaneOffset.iSurfaceOffset;
    pVphalSurface->UPlaneOffset.iXOffset           = pMosSurface->UPlaneOffset.iXOffset;
    pVphalSurface->UPlaneOffset.iYOffset           = pMosSurface->UPlaneOffset.iYOffset;

    pVphalSurface->VPlaneOffset.iLockSurfaceOffset = pMosSurface->VPlaneOffset.iLockSurfaceOffset;
    pVphalSurface->VPlaneOffset.iSurfaceOffset     = pMosSurface->VPlaneOffset.iSurfaceOffset;
    pVphalSurface->VPlaneOffset.iXOffset           = pMosSurface->VPlaneOffset.iXOffset;
    pVphalSurface->VPlaneOffset.iYOffset           = pMosSurface->VPlaneOffset.iYOffset;

    return eStatus;
}
#endif

MOS_STATUS MediaCopyBaseState::TaskDispatch()
{
    MOS_STATUS eStatus = MOS_STATUS_SUCCESS;
    MosUtilities::MosLockMutex(m_inUseGPUMutex);

    switch(m_mcpyEngine)
    {
        case MCPY_ENGINE_VEBOX:
            eStatus = MediaVeboxCopy(m_mcpySrc.OsRes, m_mcpyDst.OsRes);
            break;
        case MCPY_ENGINE_BLT:
            eStatus = MediaBltCopy(m_mcpySrc.OsRes, m_mcpyDst.OsRes);
            break;
        case MCPY_ENGINE_RENDER:
            eStatus = MediaRenderCopy(m_mcpySrc.OsRes, m_mcpyDst.OsRes);
            break;
        default:
            break;
    }
    MosUtilities::MosUnlockMutex(m_inUseGPUMutex);

#if (_DEBUG || _RELEASE_INTERNAL)
    char *CopyEngine = (char *)(m_mcpyEngine?(m_mcpyEngine == MCPY_ENGINE_BLT?"BLT":"Render"):"VeBox");
    WriteUserFeatureString(__MEDIA_USER_FEATURE_MCPY_MODE_ID, CopyEngine, strlen(CopyEngine), m_osInterface->pOsContext);
#endif
    MCPY_NORMALMESSAGE("Media Copy works on %s Engine", m_mcpyEngine?(m_mcpyEngine == MCPY_ENGINE_BLT?"BLT":"Render"):"VeBox");

    return eStatus;
}

//!
//! \brief    aux surface copy.
//! \details  copy surface.
//! \param    src
//!           [in] Pointer to source surface
//! \param    dst
//!           [in] Pointer to destination surface
//! \return   MOS_STATUS
//!           Return MOS_STATUS_SUCCESS if support, otherwise return unspoort.
//!
MOS_STATUS MediaCopyBaseState::AuxCopy(PMOS_RESOURCE src, PMOS_RESOURCE dst)
{
    // only support form Gen12+, will implement on deriavete class.
    MCPY_ASSERTMESSAGE("doesn't support");
    return MOS_STATUS_INVALID_HANDLE;
}

