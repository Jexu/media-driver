#ifndef __MOS_GPU_CTX_MGR_WRAPPER_H__
#define __MOS_GPU_CTX_MGR_WRAPPER_H__

#include "mos_os.h" // For MOS_STATUS, PMOS_COMMAND_BUFFER, PMOS_INTERFACE
#include "mos_gpu_ctx_mgr_defs.h" // For GpuCtxMgrComponentFuncType
#include <map> // For std::map

// Forward declarations
class GpuContext;
class GpuContextMgr; // The existing manager, not this wrapper
class OsContext;

class GpuCtxMgrWrapper
{
public:
    GpuCtxMgrWrapper(GpuContextMgr* underlyingMgr, OsContext* osCtx);
    ~GpuCtxMgrWrapper();

    MOS_STATUS CreateGpuContext(GpuCtxMgrComponentFuncType funcType);
    MOS_STATUS SetGpuContext(GpuCtxMgrComponentFuncType funcType);
    MOS_STATUS DestroyGpuContext(GpuCtxMgrComponentFuncType funcType);

    MOS_STATUS VerifyCmdBufferAndPatchListSize(uint32_t requestedCmdBufSize, uint32_t requestedPatchListSize);
    MOS_STATUS GetCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags);
    MOS_STATUS ReturnCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags);
    MOS_STATUS SubmitCmdBuffer(PMOS_INTERFACE osInterface, PMOS_COMMAND_BUFFER cmdBuffer, bool nullRendering);

private:
    GpuContext* m_currentGpuContext;
    std::map<GpuCtxMgrComponentFuncType, GpuContext*> m_contextPool;
    GpuContextMgr* m_underlyingGpuContextMgr;
    OsContext* m_osContext;
};

#endif // __MOS_GPU_CTX_MGR_WRAPPER_H__
