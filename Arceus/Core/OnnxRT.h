#pragma once
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <d3d12.h>
#include <directml.h>
#include <wrl.h>

class Frame;
class User;
class TargetVector;

using Microsoft::WRL::ComPtr;

class OnnxRT
{
public:
    OnnxRT(User* user);
    ~OnnxRT();
    void Detect(Frame& frame, TargetVector& outDetections);

private:
    void(*m_parseFunction)(const Ort::Value&, const float, TargetVector&, int);
    void ParseDetections(const Ort::Value& outputTensor, const float confidence, TargetVector& outDetections);

    int GetNetworkSize() const;

    bool CreateD3D12Context();
    bool CreateStructuredBuffer();
    bool CreateHeapDescriptorsAndFence();
    bool CreateRootSignature();
    bool CreateComputePipelineState();
    bool BindIO();

    bool PreprocessImage(HANDLE sharedTextureHandle);

    User* m_User = nullptr;

    std::unique_ptr<Ort::IoBinding> m_ioBinding;
    std::vector<std::vector<float>> m_outputBuffers;
    std::vector<Ort::Value> m_outputTensors;
    Ort::Value m_inputTensor = nullptr;
    bool m_useDynamicOutput = false;

    Ort::Env m_env;
    std::unique_ptr<Ort::Session> m_session;
    const OrtDmlApi* m_dmlApi = nullptr;

    ComPtr<ID3D12Device> m_d3d12Device;
    ComPtr<IDMLDevice> m_dmlDevice;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;

    HANDLE m_lastTextureHandle = nullptr;
    ComPtr<ID3D12Resource> m_cachedInputResource;
    ComPtr<ID3D12Resource> m_preprocessedBuffer;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_computePipelineState;
    ComPtr<ID3D12DescriptorHeap> m_srvUavDescriptorHeap;
    UINT m_srvUavDescriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavCpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE m_descriptorTableGpuHandle;

    Ort::AllocatorWithDefaultOptions m_allocator;
    std::string m_inputName;
    std::vector<std::string> m_outputNames;
    std::vector<int64_t> m_inputDims;
    std::vector<std::vector<int64_t>> m_outputDims;
};
