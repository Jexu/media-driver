#ifndef __MOS_INTERFACE_GPU_WRAPPER_H__
#define __MOS_INTERFACE_GPU_WRAPPER_H__

#include "mos_os.h" // For PMOS_INTERFACE, MOS_STATUS, MOS_GPU_CONTEXT, PMOS_COMMAND_BUFFER etc.
#include "mos_interface_gpu_wrapper_defs.h" // For GpuWrapperComponentFuncType
#include <map>     // For std::map

class MosInterfaceGpuWrapper
{
public:
    MosInterfaceGpuWrapper(PMOS_INTERFACE osInterface);
    ~MosInterfaceGpuWrapper();

    MOS_STATUS CreateGpuContext(GpuWrapperComponentFuncType funcType);
    MOS_STATUS SetGpuContext(GpuWrapperComponentFuncType funcType);
    MOS_STATUS DestroyGpuContext(GpuWrapperComponentFuncType funcType);

    MOS_STATUS VerifyCmdBufferAndPatchListSize(uint32_t requestedCmdBufSize, uint32_t requestedPatchListSize);
    MOS_STATUS GetCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags);
    MOS_STATUS ReturnCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags);
    MOS_STATUS SubmitCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, bool nullRendering); // Corrected signature

private:
    PMOS_INTERFACE m_osInterface;
    std::map<GpuWrapperComponentFuncType, MOS_GPU_CONTEXT> m_contextMap;
    GpuWrapperComponentFuncType m_currentComponentFuncType;
};

#endif // __MOS_INTERFACE_GPU_WRAPPER_H__
