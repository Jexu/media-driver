/*
* Copyright (c) 2021, Intel Corporation
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
//! \file     decode_mpeg2_feature_manager.h
//! \brief    Defines the common interface for mpeg2 decode feature manager
//! \details  The mpeg2 decode feature manager is further sub-divided by codec type
//!           this file is for the base interface which is shared by all components.
//!

#ifndef __DECODE_MPEG2_FEATURE_MANAGER_H__
#define __DECODE_MPEG2_FEATURE_MANAGER_H__
#include <vector>
#include "decode_allocator.h"
#include "decode_feature_manager.h"
#include "codechal_hw.h"

namespace decode{

class DecodeMpeg2FeatureManager : public DecodeFeatureManager
{
public:
    //!
    //! \brief  DecodeMpeg2FeatureManager constructor
    //! \param  [in] allocator
    //!         Pointer to EncodeAllocator
    //! \param  [in] hwInterface
    //!         Pointer to CodechalHwInterface
    //! \param  [in] trackedBuf
    //!         Pointer to TrackedBuffer
    //! \param  [in] recycleBuf
    //!         Pointer to RecycleResource
    //!
    DecodeMpeg2FeatureManager(DecodeAllocator *allocator, CodechalHwInterface *hwInterface)
        : DecodeFeatureManager(allocator, hwInterface)
    {}

    //!
    //! \brief  DecodeMpeg2FeatureManager deconstructor
    //!
    virtual ~DecodeMpeg2FeatureManager() {}

protected:
    //!
    //! \brief  Create features
    //! \param  [in] codecSettings
    //!         codec settings
    //! \return MOS_STATUS
    //!         MOS_STATUS_SUCCESS if success, else fail reason
    //!
    virtual MOS_STATUS CreateFeatures(void *codecSettings);

MEDIA_CLASS_DEFINE_END(DecodeMpeg2FeatureManager)
};


}
#endif // !__DECODE_MPEG2_FEATURE_MANAGER_H__
