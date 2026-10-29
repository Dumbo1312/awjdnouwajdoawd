#pragma once
#include <string>
#include <vector>

class TargetVector;
namespace Ort { struct Value; }

namespace Game
{
	std::vector<std::string> GetModelList();
	using ParserFunc = void(*)(const Ort::Value&, const float, TargetVector&, int);
	void LoadModel(const int modelChoice, std::vector<unsigned char>& modelBytes, ParserFunc& outParser);

	int GetHF360(const float SensHF);
	int GetADS360(const float SensHF, const float SensADS);
}