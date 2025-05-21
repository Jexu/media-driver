#include "mos_gpu_ctx_mgr_wrapper.h"
#include "mos_gpucontextmgr.h"     // For GpuContextMgr (the existing one)
#include "mos_gpucontext.h"        // For GpuContext
#include "mos_gpucontext_specific.h" // For GpuContextSpecific for casting
#include "mos_os.h"                // For OsContext and other MOS types
#include "mos_util_debug.h"        // For MOS_OS_NORMALMESSAGE, MOS_OS_ASSERTMESSAGE, MOS_OS_FUNCTION_ENTER, MOS_OS_FUNCTION_EXIT
#include "mos_defs.h"              // For MOS_GPU_CONTEXT, MOS_GPU_NODE
#include "mos_context_specific.h"  // For OsContextSpecific
#include "mos_cmdbufmgr.h"         // For CmdBufMgr

GpuCtxMgrWrapper::GpuCtxMgrWrapper(GpuContextMgr* underlyingMgr, OsContext* osCtx) :
    m_underlyingGpuContextMgr(underlyingMgr),
    m_osContext(osCtx),
    m_currentGpuContext(nullptr)
{
    // m_contextPool is default-initialized (empty)
}

GpuCtxMgrWrapper::~GpuCtxMgrWrapper()
{
    MOS_OS_FUNCTION_ENTER;

    if (m_underlyingGpuContextMgr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Underlying GpuContextMgr is null, cannot clean up contexts.");
        // MOS_OS_FUNCTION_EXIT; // Not strictly necessary before return in a destructor
        return;
    }

    for (auto& pair : m_contextPool)
    {
        if (pair.second != nullptr)
        {
            m_underlyingGpuContextMgr->DestroyGpuContext(pair.second);
            // pair.second = nullptr; // Optional: Mark as destroyed, but pool will be cleared
        }
    }
    m_contextPool.clear();
    m_currentGpuContext = nullptr;

    MOS_OS_FUNCTION_EXIT;
}

MOS_STATUS GpuCtxMgrWrapper::CreateGpuContext(GpuCtxMgrComponentFuncType funcType)
{
    MOS_OS_FUNCTION_ENTER;

    if (m_contextPool.count(funcType))
    {
        MOS_OS_NORMALMESSAGE("Context for this type already exists.");
        return MOS_STATUS_SUCCESS;
    }

    MOS_GPU_CONTEXT mosGpuContext;
    MOS_GPU_NODE gpuNode;

    switch (funcType)
    {
        case GPU_CTX_MGR_FUNC_TYPE_DECODE:
            mosGpuContext = MOS_GPU_CONTEXT_VIDEO_DECODE;
            gpuNode = MOS_GPU_NODE_VIDEO;
            break;
        case GPU_CTX_MGR_FUNC_TYPE_ENCODE:
            mosGpuContext = MOS_GPU_CONTEXT_VDBOX2_VIDEO3; // PAK context
            gpuNode = MOS_GPU_NODE_VIDEO;
            break;
        case GPU_CTX_MGR_FUNC_TYPE_VP_COMPUTE:
            mosGpuContext = MOS_GPU_CONTEXT_COMPUTE;
            // Assuming MOS_GPU_NODE_COMPUTE is defined (found in linux specific header by grep)
            // If it were not, the fallback is MOS_GPU_NODE_RENDER as per subtask.
            gpuNode = MOS_GPU_NODE_COMPUTE; 
            break;
        case GPU_CTX_MGR_FUNC_TYPE_VP_VEBOX:
            mosGpuContext = MOS_GPU_CONTEXT_VEBOX;
            // MOS_GPU_NODE_VEBOX not found by grep, using fallback MOS_GPU_NODE_VIDEO as per subtask.
            gpuNode = MOS_GPU_NODE_VIDEO; 
            break;
        case GPU_CTX_MGR_FUNC_TYPE_RENDER:
            mosGpuContext = MOS_GPU_CONTEXT_RENDER;
            gpuNode = MOS_GPU_NODE_RENDER;
            break;
        default:
            MOS_OS_ASSERTMESSAGE("Unknown component function type.");
            return MOS_STATUS_INVALID_PARAMETER;
    }

    if (m_underlyingGpuContextMgr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Underlying GpuContextMgr is nullptr.");
        return MOS_STATUS_NULL_POINTER;
    }

    if (m_osContext == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("OsContext is nullptr.");
        return MOS_STATUS_NULL_POINTER;
    }

    OsContextSpecific* osCtxSpecific = static_cast<OsContextSpecific*>(m_osContext);
    if (osCtxSpecific == nullptr) 
    {
        MOS_OS_ASSERTMESSAGE("Failed to cast OsContext to OsContextSpecific.");
        return MOS_STATUS_NULL_POINTER;
    }
    CmdBufMgr* cmdBufMgr = osCtxSpecific->GetCmdBufMgr();
    if (cmdBufMgr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("CmdBufMgr is nullptr.");
        return MOS_STATUS_NULL_POINTER;
    }

    // Corrected parameter order for CreateGpuContext
    GpuContext* newContext = m_underlyingGpuContextMgr->CreateGpuContext(gpuNode, cmdBufMgr, mosGpuContext);

    if (newContext == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Failed to create underlying GPU context.");
        return MOS_STATUS_UNINITIALIZED; 
    }

    m_contextPool[funcType] = newContext;

    MOS_OS_FUNCTION_EXIT;
    return MOS_STATUS_SUCCESS;
}

MOS_STATUS GpuCtxMgrWrapper::DestroyGpuContext(GpuCtxMgrComponentFuncType funcType)
{
    MOS_OS_FUNCTION_ENTER;

    if (m_underlyingGpuContextMgr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Underlying GpuContextMgr is nullptr.");
        return MOS_STATUS_NULL_POINTER;
    }

    auto it = m_contextPool.find(funcType);
    if (it == m_contextPool.end())
    {
        MOS_OS_ASSERTMESSAGE("GPU context for the specified function type not found, cannot destroy.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    GpuContext* contextToDestroy = it->second;
    if (contextToDestroy == nullptr)
    {
        // This case should ideally not happen if the context is in the map,
        // but it's a good defensive check.
        MOS_OS_ASSERTMESSAGE("GPU context found in pool is nullptr, cannot destroy.");
        m_contextPool.erase(it); // Clean up the map entry
        return MOS_STATUS_NULL_POINTER;
    }

    m_underlyingGpuContextMgr->DestroyGpuContext(contextToDestroy);
    m_contextPool.erase(it);

    if (m_currentGpuContext == contextToDestroy)
    {
        m_currentGpuContext = nullptr;
    }

    MOS_OS_FUNCTION_EXIT;
    return MOS_STATUS_SUCCESS;
}

MOS_STATUS GpuCtxMgrWrapper::VerifyCmdBufferAndPatchListSize(uint32_t requestedCmdBufSize, uint32_t requestedPatchListSize)
{
    MOS_OS_FUNCTION_ENTER;

    if (m_currentGpuContext == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Current GPU context is null.");
        return MOS_STATUS_NULL_POINTER;
    }

    GpuContextSpecific* currentCtxSpecific = static_cast<GpuContextSpecific*>(m_currentGpuContext);
    if (currentCtxSpecific == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Failed to cast current GPU context to GpuContextSpecific.");
        return MOS_STATUS_INVALID_HANDLE; // Or MOS_STATUS_NULL_POINTER if more appropriate
    }

    MOS_STATUS statusCmd = currentCtxSpecific->VerifyCommandBufferSize(requestedCmdBufSize);
    if (MOS_FAILED(statusCmd))
    {
        // MOS_OS_ASSERTMESSAGE or MOS_OS_NORMALMESSAGE could be added here if needed for debugging
        return statusCmd; // Return the specific error from VerifyCommandBufferSize
    }

    MOS_STATUS statusPatch = currentCtxSpecific->VerifyPatchListSize(requestedPatchListSize);
    if (MOS_FAILED(statusPatch))
    {
        // MOS_OS_ASSERTMESSAGE or MOS_OS_NORMALMESSAGE could be added here if needed for debugging
        return statusPatch; // Return the specific error from VerifyPatchListSize
    }

    MOS_OS_FUNCTION_EXIT;
    return MOS_STATUS_SUCCESS;
}

MOS_STATUS GpuCtxMgrWrapper::GetCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags)
{
    MOS_OS_FUNCTION_ENTER;

    if (m_currentGpuContext == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Current GPU context is null.");
        return MOS_STATUS_NULL_POINTER;
    }

    GpuContextSpecific* currentCtxSpecific = static_cast<GpuContextSpecific*>(m_currentGpuContext);
    if (currentCtxSpecific == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Failed to cast current GPU context to GpuContextSpecific.");
        return MOS_STATUS_INVALID_HANDLE; // Or MOS_STATUS_NULL_POINTER
    }

    return currentCtxSpecific->GetCommandBuffer(cmdBuffer, flags);
}

MOS_STATUS GpuCtxMgrWrapper::ReturnCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags)
{
    MOS_OS_FUNCTION_ENTER;

    if (m_currentGpuContext == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Current GPU context is null.");
        return MOS_STATUS_NULL_POINTER;
    }

    GpuContextSpecific* currentCtxSpecific = static_cast<GpuContextSpecific*>(m_currentGpuContext);
    if (currentCtxSpecific == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Failed to cast current GPU context to GpuContextSpecific.");
        return MOS_STATUS_INVALID_HANDLE; // Or MOS_STATUS_NULL_POINTER
    }

    currentCtxSpecific->ReturnCommandBuffer(cmdBuffer, flags);

    MOS_OS_FUNCTION_EXIT;
    return MOS_STATUS_SUCCESS;
}

MOS_STATUS GpuCtxMgrWrapper::SubmitCmdBuffer(PMOS_INTERFACE osInterface, PMOS_COMMAND_BUFFER cmdBuffer, bool nullRendering)
{
    MOS_OS_FUNCTION_ENTER;

    if (m_currentGpuContext == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Current GPU context is null.");
        return MOS_STATUS_NULL_POINTER;
    }

    if (osInterface == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("osInterface is null.");
        return MOS_STATUS_NULL_POINTER;
    }

    GpuContextSpecific* currentCtxSpecific = static_cast<GpuContextSpecific*>(m_currentGpuContext);
    if (currentCtxSpecific == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Failed to cast current GPU context to GpuContextSpecific.");
        return MOS_STATUS_INVALID_HANDLE; // Or MOS_STATUS_NULL_POINTER
    }

    return currentCtxSpecific->SubmitCommandBuffer(osInterface, cmdBuffer, nullRendering);
}

MOS_STATUS GpuCtxMgrWrapper::SetGpuContext(GpuCtxMgrComponentFuncType funcType)
{
    MOS_OS_FUNCTION_ENTER;

    auto it = m_contextPool.find(funcType);
    if (it == m_contextPool.end())
    {
        MOS_OS_ASSERTMESSAGE("GPU context for the specified function type not found or not created yet.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    m_currentGpuContext = it->second;

    MOS_OS_FUNCTION_EXIT;
    return MOS_STATUS_SUCCESS;
}
