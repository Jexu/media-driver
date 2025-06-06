/*
* Copyright (c) 2020, Intel Corporation
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
//! \file     vp_vebox_cmd_packet_xe_xpm_base.h
//! \brief    vebox packet which used in by mediapipline.
//! \details  vebox packet provide the structures and generate the cmd buffer which mediapipline will used.
//!

#ifndef __VP_VEBOX_CMD_PACKET_XE_XPM_BASE_H__
#define __VP_VEBOX_CMD_PACKET_XE_XPM_BASE_H__

#include "vp_vebox_cmd_packet_g12.h"

namespace vp {

// Base class for M12.5+
class VpVeboxCmdPacketXe_Xpm_Base : virtual public VpVeboxCmdPacketG12
{
public:
    VpVeboxCmdPacketXe_Xpm_Base(MediaTask * task, PVP_MHWINTERFACE hwInterface, PVpAllocator &allocator, VPMediaMemComp *mmc, bool disbaleSfcDithering);

    virtual ~VpVeboxCmdPacketXe_Xpm_Base();

    virtual MOS_STATUS InitSurfMemCacheControl(VP_EXECUTE_CAPS packetCaps) override;

protected:

    bool m_disableSfcDithering;             //!< flag for dithering enable or disable, for debug purpose.
};

}
#endif // !__VP_VEBOX_CMD_PACKET_XE_XPM_BASE_H__
