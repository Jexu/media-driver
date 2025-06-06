# Copyright (c) 2020-2021, Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

media_include_subdirectory(vdbox)

set(TMP_HEADERS_
    ${CMAKE_CURRENT_LIST_DIR}/mhw_hwcmd.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_cmdpar.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_itf.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_impl.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_hwcmd_process_cmdfields.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_render_cmdpar.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_render_impl.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_render_itf.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_mi_cmdpar.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_mi_impl.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_mi_itf.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_mmio_common.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_utilities_next.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_vebox_cmdpar.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_vebox_impl.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_vebox_itf.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_sfc_cmdpar.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_sfc_impl.h
    ${CMAKE_CURRENT_LIST_DIR}/mhw_sfc_itf.h
)

set(TMP_SOURCES_
    ${CMAKE_CURRENT_LIST_DIR}/mhw_utilities_next.cpp
)

set(SOURCES_
    ${SOURCES_}
    ${TMP_SOURCES_}
 )

set(HEADERS_
    ${HEADERS_}
    ${TMP_HEADERS_}
)

set(COMMON_SOURCES_
    ${COMMON_SOURCES_}
    ${TMP_SOURCES_}
)

set(COMMON_HEADERS_
    ${COMMON_HEADERS_}
    ${TMP_HEADERS_}
)

source_group( "MHW" FILES ${TMP_SOURCES_} ${TMP_HEADERS_} )

media_add_curr_to_include_path()
