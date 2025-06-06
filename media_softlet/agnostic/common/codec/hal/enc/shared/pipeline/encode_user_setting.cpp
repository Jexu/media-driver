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
//! \file     encode_user_setting.cpp
//! \brief    Initialize user setting of encode
//!

#include "encode_pipeline.h"
namespace encode
{
MOS_STATUS EncodePipeline::InitUserSetting(MediaUserSettingSharedPtr userSettingPtr)
{
    ENCODE_FUNC_CALL();
    ENCODE_CHK_STATUS_RETURN(MediaPipeline::InitUserSetting(userSettingPtr));
    DeclareUserSettingKey(
        userSettingPtr,
        "HEVC Encode",
        MediaUserSetting::Group::Sequence,
        int32_t(1),
        false);
    DeclareUserSettingKey(
        userSettingPtr,
        "Set Media Encode Mode",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        false);
    DeclareUserSettingKey(
        userSettingPtr,
        "Set Media Encode Downscaled Ratio",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        false);
    DeclareUserSettingKey(
        userSettingPtr,
        "Disable Media Encode Scalability",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        true);

    DeclareUserSettingKey(
        userSettingPtr,
        "HuC Firmware Load Failed",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        true);

    DeclareUserSettingKey(
        userSettingPtr,
        "HuC Valid Imem Load Failed",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        true);

#if (_DEBUG || _RELEASE_INTERNAL)
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "Enable Media Encode Scalability",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "Huc Status2 Value",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "Encode RateControl Method",
        MediaUserSetting::Group::Sequence,
        (int32_t)0,
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "Simulation In Use",
        MediaUserSetting::Group::Sequence,
        (int32_t)0,
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "Enable Encode VE CtxBasedScheduling",
        MediaUserSetting::Group::Sequence,
        false,
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "VDENC In Use",
        MediaUserSetting::Group::Sequence,
        (int32_t)0,
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "Encode RateControl Method",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "Encode BRC In Use",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        true);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "BRC SW Simulation",
        MediaUserSetting::Group::Sequence,
        int32_t(0),
        false);
    DeclareUserSettingKeyForDebug(
        userSettingPtr,
        "BRC SW Simulation Modules Path",
        MediaUserSetting::Group::Sequence,
        "",
        false);
    DeclareUserSettingKey(
        userSettingPtr,
        "TCBRC Quality Boost Mode",
        MediaUserSetting::Group::Sequence,
        int32_t(3),
        true);
#endif
    return MOS_STATUS_SUCCESS;
}
}  // namespace encode