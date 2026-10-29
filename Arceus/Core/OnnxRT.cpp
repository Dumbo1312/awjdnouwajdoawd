#include "OnnxRT.h"

#include <algorithm>
#include "User.h"
#include "ScreenCapture.h"
#include "Target.h"
#include "../Games/Game.h"
#include "../Utilities/Strings.hpp"
#include "../Utilities/Logger.hpp"
#include "../Utilities/Utilities.h"
#include "../Utilities/ModelEncrypt.h"
#include "ResourceExtract.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <onnxruntime_c_api.h>
#include <d3dx12.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "directml.lib")

static void YoloV8V12Parser(const Ort::Value& outputTensor, const float confidenceThr, TargetVector& outDetections, int imgSize)
{
    auto shape = outputTensor.GetTensorTypeAndShapeInfo().GetShape();

    int numDetections = 0;
    int elementsPerDetection = 0;
    bool transposed = false;

    if (shape.size() == 3)
    {
        if (shape[1] <= 100 && shape[2] > 100)
        {
            elementsPerDetection = static_cast<int>(shape[1]);
            numDetections = static_cast<int>(shape[2]);
            transposed = true;
        }
        else
        {
            numDetections = static_cast<int>(shape[1]);
            elementsPerDetection = static_cast<int>(shape[2]);
            transposed = false;
        }
    }
    else if (shape.size() == 2)
    {
        if (shape[0] <= 100 && shape[1] > 100)
        {
            elementsPerDetection = static_cast<int>(shape[0]);
            numDetections = static_cast<int>(shape[1]);
            transposed = true;
        }
        else
        {
            numDetections = static_cast<int>(shape[0]);
            elementsPerDetection = static_cast<int>(shape[1]);
            transposed = false;
        }
    }

    static TargetVector class0Detections;
    static TargetVector class1Detections;
    class0Detections.clear();
    class1Detections.clear();

    const float* data = outputTensor.GetTensorData<float>();

    for (int i = 0; i < numDetections; ++i)
    {
        float x_center, y_center, width, height, confClass0, confClass1;

        if (transposed)
        {
            x_center = data[0 * numDetections + i];
            y_center = data[1 * numDetections + i];
            width = data[2 * numDetections + i];
            height = data[3 * numDetections + i];
            confClass0 = (elementsPerDetection > 4) ? data[4 * numDetections + i] : 0.0f;
            confClass1 = (elementsPerDetection > 5) ? data[5 * numDetections + i] : 0.0f;
        }
        else
        {
            const float* walker = data + (i * elementsPerDetection);
            x_center = walker[0];
            y_center = walker[1];
            width = walker[2];
            height = walker[3];
            
            confClass0 = (elementsPerDetection > 4) ? walker[4] : 0.0f;
            confClass1 = (elementsPerDetection > 5) ? walker[5] : 0.0f;
        }

        float confidence = (std::max)(confClass0, confClass1);

        if (confidence < confidenceThr)
            continue;

        int classID = (confClass1 > confClass0) ? 1 : 0;

        bool isNormalized = (x_center <= 2.0f && y_center <= 2.0f && width <= 2.0f && height <= 2.0f);

        bool isCornerBased = false;
        if (width > x_center && height > y_center)
        {
            isCornerBased = true;
        }

        int px1, py1, pwidth, pheight;

        if (isNormalized)
        {
            if (isCornerBased)
            {
                px1 = (int)(x_center * (float)imgSize);
                py1 = (int)(y_center * (float)imgSize);
                pwidth = (int)((width - x_center) * (float)imgSize);
                pheight = (int)((height - y_center) * (float)imgSize);
            }
            else
            {
                float x1 = x_center - (width / 2.0f);
                float y1 = y_center - (height / 2.0f);

                px1 = (int)(x1 * (float)imgSize);
                py1 = (int)(y1 * (float)imgSize);
                pwidth = (int)(width * (float)imgSize);
                pheight = (int)(height * (float)imgSize);
            }
        }
        else
        {
            if (isCornerBased)
            {
                px1 = (int)x_center;
                py1 = (int)y_center;
                pwidth = (int)(width - x_center);
                pheight = (int)(height - y_center);
            }
            else
            {
                px1 = (int)(x_center - width / 2.0f);
                py1 = (int)(y_center - height / 2.0f);
                pwidth = (int)width;
                pheight = (int)height;
            }
        }

        px1 = (std::max)(0, (std::min)(px1, imgSize - 1));
        py1 = (std::max)(0, (std::min)(py1, imgSize - 1));
        pwidth = (std::max)(1, (std::min)(pwidth, imgSize - px1));
        pheight = (std::max)(1, (std::min)(pheight, imgSize - py1));

        if (classID == 0)
        {
            class0Detections.emplace_back(px1, py1, pwidth, pheight, px1 + pwidth / 2, py1 + pheight / 2, confidence, Type::Player);
        }
        else
        {
            class1Detections.emplace_back(px1, py1, pwidth, pheight, px1 + pwidth / 2, py1 + pheight / 2, confidence, Type::Head);
        }
    }

    Utility::PerformNMS(class0Detections, 0.4f);
    Utility::PerformNMS(class1Detections, 0.4f);

    outDetections.clear();
    outDetections.insert(outDetections.end(), class0Detections.begin(), class0Detections.end());
    outDetections.insert(outDetections.end(), class1Detections.begin(), class1Detections.end());
}

static void LegacyYoloParser(const Ort::Value& outputTensor, const float confidenceThr, TargetVector& outDetections, int imgSize)
{
    auto shape = outputTensor.GetTensorTypeAndShapeInfo().GetShape();

    int numDetections = (shape.size() == 3) ? static_cast<int>(shape[1]) : (shape.size() == 2 ? static_cast<int>(shape[0]) : 6300);
    int elementsPerDetection = (shape.size() == 3) ? static_cast<int>(shape[2]) : (shape.size() == 2 ? static_cast<int>(shape[1]) : 6);

    static TargetVector class0Detections;
    static TargetVector class1Detections;
    class0Detections.clear();
    class1Detections.clear();

    const float* walker = outputTensor.GetTensorData<float>();

    for (int i = 0; i < numDetections; ++i, walker += elementsPerDetection)
    {
        float confClass0 = (elementsPerDetection > 4) ? walker[4] : 0.0f;
        float_t confClass1 = (elementsPerDetection > 5) ? walker[5] : 0.0f;
        float confidence = (confClass0 > confClass1) ? confClass0 : confClass1;

        if (confidence < confidenceThr)
            continue;

        int classID = (confClass1 > confClass0) ? 1 : 0;

        float x1 = walker[0];
        float y1 = walker[1];
        float x2 = walker[2];
        float y2 = walker[3];

        int px1 = (int)(x1 * (float)imgSize);
        int py1 = (int)(y1 * (float)imgSize);
        int width = (int)(x2 * (float)imgSize) - px1;
        int height = (int)(y2 * (float)imgSize) - py1;

        if (classID == 0)
        {
            class0Detections.emplace_back(px1, py1, width, height, px1 + width / 2, py1 + height / 2, confidence, Type::Player);
        }
        else
        {
            class1Detections.emplace_back(px1, py1, width, height, px1 + width / 2, py1 + height / 2, confidence, Type::Head);
        }
    }

    Utility::PerformNMS(class0Detections, 0.4f);
    Utility::PerformNMS(class1Detections, 0.4f);

    outDetections.clear();
    outDetections.insert(outDetections.end(), class0Detections.begin(), class0Detections.end());
    outDetections.insert(outDetections.end(), class1Detections.begin(), class1Detections.end());
}

OnnxRT::OnnxRT(User* user)
    : m_User(user),
    m_env(OrtLoggingLevel::ORT_LOGGING_LEVEL_FATAL)
{
    Ort::SessionOptions sessionOptions;

    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    sessionOptions.SetIntraOpNumThreads(1);
    sessionOptions.SetInterOpNumThreads(1);
    sessionOptions.AddConfigEntry(SDetector::OptEnableCPUMemArena.get().c_str(), SDetector::Opt0.get().c_str());
    sessionOptions.DisableMemPattern();

    sessionOptions.AddConfigEntry(SDetector::OptEnableGraphCapture.get().c_str(), SDetector::OptTrue.get().c_str());
    sessionOptions.AddConfigEntry(SDetector::OptEnableDynamicGraphFusion.get().c_str(), SDetector::OptTrue.get().c_str());
    sessionOptions.AddConfigEntry(SDetector::OptDisableMetaCommands.get().c_str(), SDetector::OptFalse.get().c_str());
    sessionOptions.AddConfigEntry(SDetector::OptPerformancePreference.get().c_str(), SDetector::OptHighPerformance.get().c_str());
    sessionOptions.AddConfigEntry(SDetector::OptDisableGPUMemArena.get().c_str(), SDetector::OptFalse.get().c_str());
    sessionOptions.AddConfigEntry(SDetector::OptEnableCPUSyncSpinning.get().c_str(), SDetector::OptTrue.get().c_str());
    Logger::Progress(SDetector::LogCat, SDetector::LogOptimizations);

    if (!CreateD3D12Context())
        throw std::runtime_error(SDetector::ErrorD3D12Context);

    Ort::ThrowOnError(OrtGetApiBase()->GetApi(ORT_API_VERSION)->GetExecutionProviderApi(SDetector::ExecutionProvider.get().c_str(), ORT_API_VERSION, reinterpret_cast<const void**>(&m_dmlApi)));

    Ort::ThrowOnError(m_dmlApi->SessionOptionsAppendExecutionProvider_DML1(
        sessionOptions,
        m_dmlDevice.Get(),
        m_commandQueue.Get()
    ));
    Logger::Progress(SDetector::LogCat, SDetector::LogDirectMLProvider);

    std::vector<unsigned char> modelData;
    if (!m_User->Network.modelPath.empty()) {
        auto resolveModelPath = [this]() -> bool {
            namespace fs = std::filesystem;
            if (fs::exists(m_User->Network.modelPath)) return true;

            ResourceExtract::ForceExtractNow();
            fs::path rt(ResourceExtract::GetRuntimeDir());
            fs::path exe = fs::current_path();

            std::vector<std::string> names;
            for (const auto& d : { rt, exe })
            {
                if (!fs::exists(d)) continue;
                std::error_code ec;
                for (const auto& e : fs::directory_iterator(d, ec))
                {
                    if (ec) break;
                    if (!e.is_regular_file()) continue;
                    if (e.path().extension() != L".mdl") continue;
                    std::string n = e.path().filename().string();
                    if (std::find(names.begin(), names.end(), n) == names.end())
                        names.push_back(n);
                }
            }
            std::sort(names.begin(), names.end());

            auto pick = [&](int idx) -> bool {
                if (idx < 0 || idx >= static_cast<int>(names.size())) return false;
                fs::path fp = rt / names[idx];
                if (!fs::exists(fp)) fp = exe / names[idx];
                if (!fs::exists(fp)) return false;
                m_User->Network.modelPath = fp.string();
                return true;
            };

            if (pick(m_User->Network.modelChoice)) return true;
            if (!names.empty() && pick(0)) return true;

            if (ResourceExtract::ExtractEmbeddedModel())
            {
                m_User->Network.modelPath = (rt / L"fortnite.mdl").string();
                return fs::exists(m_User->Network.modelPath);
            }
            return false;
        };

        if (!resolveModelPath())
            throw std::runtime_error("Could not locate model file");

        std::ifstream file(m_User->Network.modelPath, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("Could not open model file: " + m_User->Network.modelPath);

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        modelData.resize(static_cast<size_t>(size));

        if (!file.read(reinterpret_cast<char*>(modelData.data()), size))
            throw std::runtime_error("Could not read model file");

        if (modelData.size() > 0) {
            if (modelData.size() >= 4 && *reinterpret_cast<const uint32_t*>(modelData.data()) == 0x4D444C32) {
                std::vector<uint8_t> decrypted;
                if (!ModelEncrypt::DecryptToBuffer(modelData, decrypted))
                    throw std::runtime_error("Failed to decrypt model file");
                modelData.assign(decrypted.begin(), decrypted.end());
            }
            else if (modelData[0] == 0x08) {
                {
                    std::ofstream rawFile(m_User->Network.modelPath, std::ios::binary);
                    if (rawFile) {
                        rawFile.write(reinterpret_cast<char*>(modelData.data()), modelData.size());
                    }
                }
                std::wstring wModelPath(m_User->Network.modelPath.begin(), m_User->Network.modelPath.end());
                if (!ModelEncrypt::EncryptFile(wModelPath, wModelPath))
                    throw std::runtime_error("Failed to encrypt model file");
                if (!ModelEncrypt::DecryptFile(wModelPath, modelData))
                    throw std::runtime_error("Failed to decrypt newly encrypted model");
            } else {
                for (size_t i = 0; i < modelData.size(); ++i)
                    modelData[i] ^= 0x50;
            }
        }

        m_parseFunction = nullptr;
    }
    else {
        Game::LoadModel(m_User->Network.modelChoice, modelData, m_parseFunction);
    }
    Logger::Progress(SDetector::LogCat, SDetector::LogModelRead);

    m_session = std::make_unique<Ort::Session>(m_env, modelData.data(), modelData.size(), sessionOptions);
    Logger::Progress(SDetector::LogCat, SDetector::LogSessionCreated);
    ResourceExtract::CleanupFiles();

    if (m_parseFunction == nullptr)
    {
        auto outputShape = m_session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();

        if (outputShape.size() == 3 && outputShape[1] <= 100 && outputShape[1] >= 4)
        {
            m_parseFunction = &YoloV8V12Parser;
            Logger::Progress(SDetector::LogCat, L"Detected YOLOv8/v11/v12 format (transposed)");
        }
        else if (outputShape.size() >= 2)
        {
            m_parseFunction = &YoloV8V12Parser;
            Logger::Progress(SDetector::LogCat, L"Detected YOLOv8/v11/v12 format (standard)");
        }
        else
        {
            m_parseFunction = &LegacyYoloParser;
            Logger::Progress(SDetector::LogCat, L"Using legacy YOLO parser");
        }
    }

    m_inputDims = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    Ort::AllocatedStringPtr inputName = m_session->GetInputNameAllocated(0, m_allocator);

    if (m_inputDims.size() == 4)
    {
        int64_t inputWidth = m_inputDims[3];
        if (inputWidth != -1)
            m_User->Network.size = static_cast<int>(inputWidth);
        else
            m_User->Network.size = 320;
    }

    m_inputName = inputName.get();
    
    size_t outputCount = m_session->GetOutputCount();
    for (size_t i = 0; i < outputCount; ++i)
    {
        Ort::AllocatedStringPtr outputName = m_session->GetOutputNameAllocated(i, m_allocator);
        m_outputNames.push_back(outputName.get());
        m_outputDims.push_back(m_session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
    }

    Logger::Progress(SDetector::LogCat, SDetector::LogIONodeDetails);

    if (!CreateStructuredBuffer())
        throw std::runtime_error(SDetector::ErrorStructuredBuffer);

    if (!CreateHeapDescriptorsAndFence())
        throw std::runtime_error(SDetector::ErrorHeapDescriptor);

    if (!CreateRootSignature())
        throw std::runtime_error(SDetector::ErrorRootSignature);

    if (!CreateComputePipelineState())
        throw std::runtime_error(SDetector::ErrorComputePipeline);

    BindIO();
}

OnnxRT::~OnnxRT()
{
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void OnnxRT::Detect(Frame& frame, TargetVector& outDetections)
{
    if (!PreprocessImage(frame.textureHandle)) {
        static uint32_t prepFailCount = 0;
        if (prepFailCount++ % 100 == 0) {
            Logger::Warning(SDetector::LogCat, L"PreprocessImage failed - check shared handle!");
        }
        return;
    }

    try {
        m_session->Run(Ort::RunOptions{ nullptr }, *m_ioBinding);
    } catch (const std::exception& e) {
        static uint32_t runFailCount = 0;
        if (runFailCount++ % 100 == 0) {
            std::string msg = e.what();
            std::wstring wmsg;
            for (char c : msg) wmsg += static_cast<wchar_t>(c);
            Logger::Warning(SDetector::LogCat, L"Inference failed: " + wmsg);
        }
        return;
    }

    outDetections.clear();
    static TargetVector allDetections;
    allDetections.clear();

    if (m_useDynamicOutput)
    { 
        auto outputValues = m_ioBinding->GetOutputValues();
        for (const auto& val : outputValues)
        { 
            static TargetVector layerDetections;
            layerDetections.clear();
            ParseDetections(val, m_User->Network.confidence, layerDetections);
            allDetections.insert(allDetections.end(), layerDetections.begin(), layerDetections.end());
        }
    }
    else
    { 
        for (const auto& tensor : m_outputTensors)
        { 
            static TargetVector layerDetections;
            layerDetections.clear();
            ParseDetections(tensor, m_User->Network.confidence, layerDetections);
            allDetections.insert(allDetections.end(), layerDetections.begin(), layerDetections.end());
        }
    }

    if (allDetections.empty())
        return;

    if (m_outputNames.size() > 1)
    {
        Utility::PerformNMS(allDetections, 0.45f);
    }

    outDetections = std::move(allDetections);
}

void OnnxRT::ParseDetections(const Ort::Value& outputTensor, const float confidence, TargetVector& outDetections)
{
    return (*m_parseFunction)(outputTensor, confidence, outDetections, m_User->Network.size);
}

int OnnxRT::GetNetworkSize() const
{
    return m_User->Network.size;
}

bool OnnxRT::PreprocessImage(HANDLE sharedTextureHandle)
{
    bool resourceChanged = false;

    if (sharedTextureHandle != m_lastTextureHandle || !m_cachedInputResource)
    {
        m_cachedInputResource.Reset();

        HRESULT hr = m_d3d12Device->OpenSharedHandle(sharedTextureHandle, IID_PPV_ARGS(&m_cachedInputResource));

        if (FAILED(hr)) {
            static uint32_t openFailCount = 0;
            if (openFailCount++ % 100 == 0) {
                Logger::Warning(SDetector::LogCat, L"D3D12 OpenSharedHandle failed");
            }
            return false;
        }

        m_lastTextureHandle = sharedTextureHandle;
        resourceChanged = true;
    }

    if (resourceChanged)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        
        D3D12_RESOURCE_DESC resDesc = m_cachedInputResource->GetDesc();
        srvDesc.Format = resDesc.Format;
        
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        m_d3d12Device->CreateShaderResourceView(
            m_cachedInputResource.Get(),
            &srvDesc,
            m_srvCpuHandle
        );
    }

    if (m_fence->GetCompletedValue() < m_fenceValue)
    {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        DWORD waitResult = WaitForSingleObject(m_fenceEvent, 2000);
        if (waitResult == WAIT_TIMEOUT)
            return false;
    }

    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    D3D12_RESOURCE_BARRIER initialBarriers[2] = {};

    initialBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    initialBarriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    initialBarriers[0].Transition.pResource = m_cachedInputResource.Get();
    initialBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    initialBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    initialBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    initialBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    initialBarriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    initialBarriers[1].Transition.pResource = m_preprocessedBuffer.Get();
    initialBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
    initialBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    initialBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(2, initialBarriers);

    m_commandList->SetComputeRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_computePipelineState.Get());

    ID3D12DescriptorHeap* heaps[] = { m_srvUavDescriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    m_commandList->SetComputeRootDescriptorTable(0, m_descriptorTableGpuHandle);
    m_commandList->SetComputeRoot32BitConstant(1, static_cast<UINT>(m_User->Network.size), 0);

    UINT groupX = static_cast<UINT>((m_User->Network.size + 15) / 16);
    UINT groupY = static_cast<UINT>((m_User->Network.size + 15) / 16);
    m_commandList->Dispatch(groupX, groupY, 1);

    D3D12_RESOURCE_BARRIER finalBarriers[3] = {};

    finalBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    finalBarriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    finalBarriers[0].UAV.pResource = m_preprocessedBuffer.Get();

    finalBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    finalBarriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    finalBarriers[1].Transition.pResource = m_cachedInputResource.Get();
    finalBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    finalBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    finalBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    finalBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    finalBarriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    finalBarriers[2].Transition.pResource = m_preprocessedBuffer.Get();
    finalBarriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    finalBarriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    finalBarriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(3, finalBarriers);

    HRESULT hr = m_commandList->Close();
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, commandLists);

    m_fenceValue++;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue);

    return true;
}

bool OnnxRT::CreateD3D12Context()
{
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d3d12Device));
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    Logger::Progress(SDetector::LogCat, SDetector::LogD3D12Device);

    hr = m_d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr)) return false;

    hr = m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
    if (FAILED(hr)) return false;

    hr = m_d3d12Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList));
    if (FAILED(hr)) return false;

    hr = m_commandList->Close();
    if (FAILED(hr)) return false;

    Logger::Progress(SDetector::LogCat, SDetector::LogCmdQueueAllocList);

    hr = DMLCreateDevice(m_d3d12Device.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&m_dmlDevice));
    if (FAILED(hr)) return false;

    Logger::Progress(SDetector::LogCat, SDetector::LogDirectMLDevice);

    return true;
}

bool OnnxRT::CreateStructuredBuffer()
{
    UINT totalPixels = m_User->Network.size * m_User->Network.size;
    UINT totalFloats = totalPixels * 3;
    UINT bufferSize = totalFloats * sizeof(float);

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = bufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    HRESULT hr = m_d3d12Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_preprocessedBuffer)
    );
    if (FAILED(hr)) return false;

    Logger::Progress(SDetector::LogCat, SDetector::LogD3D12Buffer);

    return true;
}

bool OnnxRT::CreateHeapDescriptorsAndFence()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 2;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = m_d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvUavDescriptorHeap));
    if (FAILED(hr)) return false;

    m_srvUavDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    m_srvCpuHandle = m_srvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    m_uavCpuHandle = m_srvCpuHandle;
    m_uavCpuHandle.ptr += m_srvUavDescriptorSize;

    m_descriptorTableGpuHandle = m_srvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    UINT totalPixels = m_User->Network.size * m_User->Network.size;
    UINT elementCount = (totalPixels * 3);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = elementCount;
    uavDesc.Buffer.StructureByteStride = sizeof(float);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    m_d3d12Device->CreateUnorderedAccessView(
        m_preprocessedBuffer.Get(),
        nullptr,
        &uavDesc,
        m_uavCpuHandle
    );

    Logger::Progress(SDetector::LogCat, SDetector::LogDescriptorsHeap);

    hr = m_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) return false;

    m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (!m_fenceEvent) return false;

    Logger::Progress(SDetector::LogCat, SDetector::LogFenceCreated);

    return true;
}

bool OnnxRT::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE1 ranges[2] = {};

    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 rootParameters[2];

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 2;
    rootParameters[0].DescriptorTable.pDescriptorRanges = ranges;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].Constants.ShaderRegister = 0;
    rootParameters[1].Constants.RegisterSpace = 0;
    rootParameters[1].Constants.Num32BitValues = 1;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 2;
    rootSigDesc.Desc_1_1.pParameters = rootParameters;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSigDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signature, &error);
    if (FAILED(hr)) return false;

    hr = m_d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

    Logger::Progress(SDetector::LogCat, SDetector::LogRootSignature);

    return SUCCEEDED(hr);
}

bool OnnxRT::CreateComputePipelineState()
{
    ComPtr<ID3DBlob> computeShader;
    UINT compileFlags = 0;

    HRESULT hr = D3DCompile(
        SDetector::ShaderPreprocessing.get().c_str(),
        strlen(SDetector::ShaderPreprocessing.get().c_str()),
        nullptr,
        nullptr, nullptr,
        SDetector::ShaderEntryPoint.get().c_str(),
        SDetector::ShaderModel.get().c_str(),
        compileFlags,
        0,
        &computeShader,
        nullptr
    );
    if (FAILED(hr)) return false;

    Logger::Progress(SDetector::LogCat, SDetector::LogShaderCompiled);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.CS.pShaderBytecode = computeShader->GetBufferPointer();
    psoDesc.CS.BytecodeLength = computeShader->GetBufferSize();

    hr = m_d3d12Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineState));
    if (FAILED(hr)) return false;

    Logger::Progress(SDetector::LogCat, SDetector::LogComputePipeline);

    return true;
}

bool OnnxRT::BindIO()
{
    m_ioBinding = std::make_unique<Ort::IoBinding>(*m_session);

    void* dml_resource;
    OrtStatusPtr hr = m_dmlApi->CreateGPUAllocationFromD3DResource(
        m_preprocessedBuffer.Get(),
        &dml_resource
    );

    Ort::MemoryInfo memory_info_dml(SDetector::ExecutionProvider.get().c_str(), OrtDeviceAllocator, 0, OrtMemTypeDefault);
    std::array<int64_t, 4> shape = { 1, 3, m_User->Network.size, m_User->Network.size };
    size_t d3d_buffer_size = 3 * m_User->Network.size * m_User->Network.size * sizeof(float);

    m_inputTensor = Ort::Value(Ort::Value::CreateTensor(
        memory_info_dml,
        dml_resource,
        d3d_buffer_size,
        shape.data(),
        shape.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
    ));

    m_ioBinding->BindInput(m_inputName.c_str(), m_inputTensor);
    Logger::Progress(SDetector::LogCat, SDetector::LogInputBinded);

    m_useDynamicOutput = false;
    for (const auto& dims : m_outputDims)
    { 
        for (int64_t dim : dims)
        { 
            if (dim == -1)
            { 
                m_useDynamicOutput = true;
                break;
            }
        }
        if (m_useDynamicOutput) break;
    }

    Ort::MemoryInfo cpuMemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    for (size_t i = 0; i < m_outputNames.size(); ++i)
    { 
        if (m_useDynamicOutput)
        { 
            m_ioBinding->BindOutput(m_outputNames[i].c_str(), cpuMemoryInfo);
        }
        else
        { 
            size_t outputTensorSize = 1;
            for (int64_t dim : m_outputDims[i]) outputTensorSize *= (size_t)dim;

            m_outputBuffers.emplace_back(outputTensorSize);
            m_outputTensors.push_back(Ort::Value::CreateTensor<float>(
                cpuMemoryInfo,
                m_outputBuffers.back().data(),
                m_outputBuffers.back().size(),
                m_outputDims[i].data(),
                m_outputDims[i].size()
            ));

            m_ioBinding->BindOutput(m_outputNames[i].c_str(), m_outputTensors.back());
        }
    }

    if (m_useDynamicOutput)
        Logger::Progress(SDetector::LogCat, L"Outputs bound to CPU (dynamic shape)");
    else
        Logger::Progress(SDetector::LogCat, SDetector::LogOutputBinded);

    return true;
}
