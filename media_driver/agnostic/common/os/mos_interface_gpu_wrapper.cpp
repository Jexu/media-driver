#include "mos_interface_gpu_wrapper.h"
#include "mos_util_debug.h" // For MOS_OS_ASSERTMESSAGE, MOS_OS_NORMALMESSAGE
#include "mos_os_specific.h" // May be needed for specific definitions or utilities if MOS_INTERFACE functions require more context

// Further includes might be added as function bodies are implemented.

MosInterfaceGpuWrapper::MosInterfaceGpuWrapper(PMOS_INTERFACE osInterface) :
    m_osInterface(osInterface), // Store the passed osInterface
    m_currentComponentFuncType(GPU_WRAPPER_FUNC_TYPE_INVALID)
{
    MOS_OS_FUNCTION_ENTER;
    // No MOS_OS_CHK_NULL_NO_STATUS_RETURN here as we are just storing it.
    // If we were to dereference osInterface here, a check would be good.
}

MosInterfaceGpuWrapper::~MosInterfaceGpuWrapper()
{
    MOS_OS_FUNCTION_ENTER;

    if (m_osInterface == nullptr || m_osInterface->pfnDestroyGpuContext == nullptr)
    {
        MOS_OS_NORMALMESSAGE(MOS_MESSAGE_LVL_WARNING, "MOS_INTERFACE or pfnDestroyGpuContext is null, cannot clean up contexts in wrapper destructor.");
        return;
    }

    for (auto const& [funcType, mosCtxEnum] : m_contextMap)
    {
        MOS_STATUS destroyStatus = m_osInterface->pfnDestroyGpuContext(m_osInterface, mosCtxEnum);
        if (MOS_FAILED(destroyStatus))
        {
            // Using MOS_MESSAGE_LVL_ERROR as this is an unexpected failure during cleanup
            MOS_OS_NORMALMESSAGE(MOS_MESSAGE_LVL_ERROR, "Failed to destroy context %d during wrapper cleanup. Status: %d", mosCtxEnum, destroyStatus);
        }
    }

    m_contextMap.clear();
    m_currentComponentFuncType = GPU_WRAPPER_FUNC_TYPE_INVALID;
    m_osInterface = nullptr; // Optional, but good practice
}

MOS_STATUS MosInterfaceGpuWrapper::CreateGpuContext(GpuWrapperComponentFuncType funcType)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_OS_CHK_NULL_RETURN(m_osInterface);
    MOS_OS_CHK_NULL_RETURN(m_osInterface->pfnCreateGpuContext);

    MOS_GPU_CONTEXT mosCtxEnum = MOS_GPU_CONTEXT_INVALID_HANDLE;
    MOS_GPU_NODE gpuNode = MOS_GPU_NODE_INVALID;

    switch (funcType)
    {
        case GPU_WRAPPER_FUNC_TYPE_DECODE:
            mosCtxEnum = MOS_GPU_CONTEXT_VIDEO_DECODE;
            gpuNode = MOS_GPU_NODE_VIDEO;
            break;
        case GPU_WRAPPER_FUNC_TYPE_ENCODE:
            mosCtxEnum = MOS_GPU_CONTEXT_VDBOX2_VIDEO3; // Corresponds to PAK
            gpuNode = MOS_GPU_NODE_VIDEO;
            break;
        case GPU_WRAPPER_FUNC_TYPE_VP_COMPUTE:
            mosCtxEnum = MOS_GPU_CONTEXT_COMPUTE;
            gpuNode = MOS_GPU_NODE_COMPUTE; // Found in mos_os_specific.h (Linux)
            break;
        case GPU_WRAPPER_FUNC_TYPE_VP_VEBOX:
            mosCtxEnum = MOS_GPU_CONTEXT_VEBOX;
            // MOS_GPU_NODE_VEBOX was not found by grep. Using MOS_GPU_NODE_VIDEO as fallback.
            gpuNode = MOS_GPU_NODE_VIDEO;
            break;
        case GPU_WRAPPER_FUNC_TYPE_RENDER:
            mosCtxEnum = MOS_GPU_CONTEXT_RENDER;
            gpuNode = MOS_GPU_NODE_RENDER;
            break;
        case GPU_WRAPPER_FUNC_TYPE_INVALID: // Explicitly handle invalid
        default:
            MOS_OS_ASSERTMESSAGE("Invalid function type provided.");
            return MOS_STATUS_INVALID_PARAMETER;
    }

    if (mosCtxEnum == MOS_GPU_CONTEXT_INVALID_HANDLE || gpuNode == MOS_GPU_NODE_INVALID)
    {
        MOS_OS_ASSERTMESSAGE("MOS GPU Context or GPU Node remains invalid after switch.");
        return MOS_STATUS_INVALID_PARAMETER;
    }

    // Using default options for now.
    // PMOS_GPUCTX_CREATOPTIONS_ENHANCED createOptionsEnhanced = nullptr; // Example if enhanced needed
    PMOS_GPUCTX_CREATOPTIONS createOptions = nullptr; 

    MOS_STATUS status = m_osInterface->pfnCreateGpuContext(m_osInterface, mosCtxEnum, gpuNode, createOptions);
    MOS_OS_CHK_STATUS_RETURN(status);

    m_contextMap[funcType] = mosCtxEnum;

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS MosInterfaceGpuWrapper::SetGpuContext(GpuWrapperComponentFuncType funcType)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_OS_CHK_NULL_RETURN(m_osInterface);
    MOS_OS_CHK_NULL_RETURN(m_osInterface->pfnSetGpuContext);

    auto it = m_contextMap.find(funcType);
    if (it == m_contextMap.end())
    {
        MOS_OS_ASSERTMESSAGE("Context for the specified function type has not been created via this wrapper.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    MOS_GPU_CONTEXT mosCtxEnum = it->second;

    MOS_STATUS status = m_osInterface->pfnSetGpuContext(m_osInterface, mosCtxEnum);
    MOS_OS_CHK_STATUS_RETURN(status);

    m_currentComponentFuncType = funcType;

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS MosInterfaceGpuWrapper::DestroyGpuContext(GpuWrapperComponentFuncType funcType)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_OS_CHK_NULL_RETURN(m_osInterface);
    MOS_OS_CHK_NULL_RETURN(m_osInterface->pfnDestroyGpuContext);

    auto it = m_contextMap.find(funcType);
    if (it == m_contextMap.end())
    {
        MOS_OS_NORMALMESSAGE(MOS_MESSAGE_LVL_WARNING, "Context for the specified function type not found or not created via this wrapper, cannot destroy.");
        return MOS_STATUS_SUCCESS; // Not a hard error for this wrapper
    }

    MOS_GPU_CONTEXT mosCtxEnum = it->second;

    MOS_STATUS status = m_osInterface->pfnDestroyGpuContext(m_osInterface, mosCtxEnum);
    if (MOS_FAILED(status))
    {
        MOS_OS_ASSERTMESSAGE("Failed to destroy GPU context via MOS_INTERFACE.");
        return status; // Propagate the error
    }

    m_contextMap.erase(it);

    if (m_currentComponentFuncType == funcType)
    {
        m_currentComponentFuncType = GPU_WRAPPER_FUNC_TYPE_INVALID;
        // Optionally, set GPU context to an invalid/default handle if required by MOS_INTERFACE.
        // For now, just resetting the wrapper's state.
        // Example: m_osInterface->pfnSetGpuContext(m_osInterface, MOS_GPU_CONTEXT_INVALID_HANDLE);
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS MosInterfaceGpuWrapper::VerifyCmdBufferAndPatchListSize(uint32_t requestedCmdBufSize, uint32_t requestedPatchListSize)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_OS_CHK_NULL_RETURN(m_osInterface);
    MOS_OS_CHK_NULL_RETURN(m_osInterface->pfnResizeCommandBufferAndPatchList);

    if (m_currentComponentFuncType == GPU_WRAPPER_FUNC_TYPE_INVALID)
    {
        MOS_OS_ASSERTMESSAGE("No GPU context has been set via SetGpuContext.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    return m_osInterface->pfnResizeCommandBufferAndPatchList(m_osInterface, requestedCmdBufSize, requestedPatchListSize, 0);
}

MOS_STATUS MosInterfaceGpuWrapper::GetCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_OS_CHK_NULL_RETURN(m_osInterface);
    MOS_OS_CHK_NULL_RETURN(m_osInterface->pfnGetCommandBuffer);

    if (m_currentComponentFuncType == GPU_WRAPPER_FUNC_TYPE_INVALID)
    {
        MOS_OS_ASSERTMESSAGE("No GPU context has been set via SetGpuContext.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    return m_osInterface->pfnGetCommandBuffer(m_osInterface, cmdBuffer, flags);
}

MOS_STATUS MosInterfaceGpuWrapper::ReturnCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, uint32_t flags)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_OS_CHK_NULL_RETURN(m_osInterface);
    MOS_OS_CHK_NULL_RETURN(m_osInterface->pfnReturnCommandBuffer);

    if (m_currentComponentFuncType == GPU_WRAPPER_FUNC_TYPE_INVALID)
    {
        MOS_OS_ASSERTMESSAGE("No GPU context has been set via SetGpuContext.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    m_osInterface->pfnReturnCommandBuffer(m_osInterface, cmdBuffer, flags);
    return MOS_STATUS_SUCCESS;
}

MOS_STATUS MosInterfaceGpuWrapper::SubmitCmdBuffer(PMOS_COMMAND_BUFFER cmdBuffer, bool nullRendering)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_OS_CHK_NULL_RETURN(m_osInterface);
    MOS_OS_CHK_NULL_RETURN(m_osInterface->pfnSubmitCommandBuffer);

    if (m_currentComponentFuncType == GPU_WRAPPER_FUNC_TYPE_INVALID)
    {
        MOS_OS_ASSERTMESSAGE("No GPU context has been set via SetGpuContext.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    return m_osInterface->pfnSubmitCommandBuffer(m_osInterface, cmdBuffer, nullRendering);
}
