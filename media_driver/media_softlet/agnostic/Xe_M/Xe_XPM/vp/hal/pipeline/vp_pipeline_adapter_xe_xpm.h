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
#ifndef __VP_PIPELINE_ADAPTER_XE_XPM_H__
#define __VP_PIPELINE_ADAPTER_XE_XPM_H__

#include "vphal_xe_xpm.h"
#include "vp_pipeline.h"
#include "vp_pipeline_common.h"
#include "vp_pipeline_adapter_legacy.h"

class VpPipelineAdapterXe_Xpm : virtual public VphalStateXe_Xpm, virtual public VpPipelineAdapterLegacy
{
public:
    VpPipelineAdapterXe_Xpm(
        PMOS_INTERFACE          pOsInterface,
        PMOS_CONTEXT            pOsDriverContext,
        vp::VpPlatformInterface &vpPlatformInterface,
        MOS_STATUS              &eStatus);

    //!
    //! \brief    VpPipelineAdapterXe_Xpm Destuctor
    //! \details  Destroys VpPipelineAdapterXe_Xpm and all internal states and objects
    //! \return   void
    //!
    virtual ~VpPipelineAdapterXe_Xpm();

    //!
    //! \brief    Performs VP Rendering
    //! \details  Performs VP Rendering
    //!           - call default render of video
    //! \param    [in] pcRenderParams
    //!           Pointer to Render Params
    //! \return   MOS_STATUS
    //!           Return MOS_STATUS_SUCCESS if successful, otherwise failed
    //!
    virtual MOS_STATUS Render(
        PCVPHAL_RENDER_PARAMS   pcRenderParams) override;

    //!
    //! \brief    Allocate VP Resources
    //! \details  Allocate VP Resources
    //!           - Allocate and initialize HW states
    //!           - Allocate and initialize renderer states
    //! \param    [in] pVpHalSettings
    //!           Pointer to VPHAL Settings
    //! \return   MOS_STATUS
    //!           Return MOS_STATUS_SUCCESS if successful, otherwise failed
    //!
    virtual MOS_STATUS Allocate(
      const VphalSettings     *pVpHalSettings) override;

    virtual MOS_STATUS GetStatusReport(
        PQUERY_STATUS_REPORT_APP  pQueryReport,
        uint16_t                  numStatus) override;

    virtual MOS_STATUS Execute(PVP_PIPELINE_PARAMS params) override;

    //!
    //! \brief    Get feature reporting
    //! \details  Get feature reporting
    //! \return   VphalFeatureReport*
    //!           Pointer to VPHAL_FEATURE_REPOR: rendering features reported
    //!
    virtual VphalFeatureReport* GetRenderFeatureReport() override;

protected:
    virtual bool IsApoEnabled() override
    {
        return true;
    }
};
#endif // !__VP_PIPELINE_ADAPTER_XE_XPM_H__

