#ifdef FORTNITE
#include "Game.h"
#include "../Core/OnnxRT.h"
#include "../Core/Target.h"
#include "../Utilities/Strings.hpp"
#include "../Utilities/Utilities.h"

#include "VirtualizerSDK.h"
#include "VirtualizerSDK_CAT_BLACK.h"

namespace Game
{
    std::vector<std::string> GetModelList()
    {
        static const std::vector<std::string> MODEL_LIST =
        {
            SGame::ModelNameFast,
            SGame::ModelNameAccurate,
            SGame::ModelNameCasual
        };
        return MODEL_LIST;
    }

    void ParseDetectionsYoloV43l(const Ort::Value& outputTensor, const float confidenceThr, TargetVector& outDetections, int imgSize)
    {
        VIRTUALIZER_MUTATE_ONLY_START

        constexpr int numDetections = 6300;

        static TargetVector class0Detections;
        static TargetVector class1Detections;

        class0Detections.clear();
        class1Detections.clear();

        const float* walker = outputTensor.GetTensorData<float>();

        for (int i = 0; i < numDetections; ++i, walker += 6)
        {
            const float confClass0 = walker[4];
            const float confClass1 = walker[5];
            const float confidence = (confClass0 > confClass1) ? confClass0 : confClass1;

            if (confidence < confidenceThr)
                continue;

            const int classID = (confClass1 > confClass0) ? 1 : 0;

            const float x1 = walker[0];
            const float y1 = walker[1];
            const float x2 = walker[2];
            const float y2 = walker[3];

            const int px1 = static_cast<int>(x1 * imgSize);
            const int py1 = static_cast<int>(y1 * imgSize);
            const int width = static_cast<int>(x2 * imgSize) - px1;
            const int height = static_cast<int>(y2 * imgSize) - py1;

            if (classID == 0)
            {
                class0Detections.emplace_back(
                    px1, py1, width, height,
                    0, 0, confidence, Type::Player);
            }
            else
            {
                class1Detections.emplace_back(
                    px1, py1, width, height,
                    0, 0, confidence, Type::Head);
            }
        }

        Utility::PerformNMS(class0Detections, 0.4f);
        Utility::PerformNMS(class1Detections, 0.4f);

        outDetections.clear();
        outDetections.insert(outDetections.end(), class0Detections.begin(), class0Detections.end());
        outDetections.insert(outDetections.end(), class1Detections.begin(), class1Detections.end());

        VIRTUALIZER_MUTATE_ONLY_END
    }

    bool GetModel(const std::string& modelName, std::vector<unsigned char>& modelBytes)
    {
        VIRTUALIZER_CAT_BLACK_START

        std::filesystem::path exeDir = Utility::GetExecutableDirectory();
        std::filesystem::path localBodyPath = exeDir / modelName;
        std::string tailName = modelName + SGame::ModelDot.get();
        std::filesystem::path localTailPath = exeDir / tailName;

        if (!Utility::LoadFileToVector(localBodyPath, modelBytes))
            return false;

        std::vector<unsigned char> tailBytes;
        if (!Utility::LoadFileToVector(localTailPath, tailBytes))
            return false;

        modelBytes.insert(modelBytes.end(), tailBytes.begin(), tailBytes.end());

        std::vector<unsigned char> decryptedBytes;
        if (!Utility::DecryptBuffer(modelBytes.data(), modelBytes.size(), decryptedBytes))
            return false;

        modelBytes = std::move(decryptedBytes);

        VIRTUALIZER_CAT_BLACK_END

        return true;
    }

    void LoadModel(const int modelChoice, std::vector<unsigned char>& modelBytes, ParserFunc& outParser)
    {
        switch (modelChoice)
        {
        case 0:
            throw std::runtime_error(SGame::ErrorModelInv);
            break;

        case 1:
            if(!GetModel(SGame::ModelFileAccurate, modelBytes))
                throw std::runtime_error(SGame::ErrorModelGet);
            outParser = &ParseDetectionsYoloV43l;
            break;

        case 2:
            throw std::runtime_error(SGame::ErrorModelInv);
            break;

        default:
            throw std::runtime_error(SGame::ErrorModelInv);
            break;
        }
    }
}

namespace Const
{
    constexpr float HF_FOV_X = 0.8390;

    constexpr float HF_FOV_Y = 0.4716;


    constexpr float ADS_FOV_X = 0.7002;

    constexpr float ADS_FOV_Y = 0.4040;


    constexpr int HF_360 = 64800;

    constexpr int ADS_360 = 6480600;

}

namespace Game
{
    int GetHF360(const float SensHF)
    {
        return static_cast<int>((Const::HF_360 / SensHF) / 360);
    }

    int GetADS360(const float SensHF, const float SensADS)
    {
        return static_cast<int>(((Const::ADS_360 / SensHF) / (SensADS)) / 360);
    }
}

#endif