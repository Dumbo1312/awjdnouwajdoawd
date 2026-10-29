#pragma once

struct GameConfig
{
	float HF_FOV_X;
	float HF_FOV_Y;
	float ADS_FOV_X;
	float ADS_FOV_Y;
	int HF_360;
	int ADS_360;
	bool THIRD_PERSON;
	bool HAS_TRIGGERBOT;
	bool FORCE_TRIGGER;
};

enum class GameType { Fortnite, Valorant, CS2, RainbowSix, COD, Warzone, Rust, Custom };

inline const GameConfig FORTNITE_CONFIG = {
	0.8390f,
	0.4716f,
	0.7002f,
	0.4040f,
	64800,
	6480600,
	true,
	true,
	false
};

inline const GameConfig VALORANT_CONFIG = {
	0.8189f,
	0.4606f,
	0.6822f,
	0.3837f,
	64800,
	6480600,
	false,
	true,
	false
};

inline const GameConfig CS2_CONFIG = {
	0.8220f,
	0.4624f,
	0.6850f,
	0.3853f,
	64800,
	6480600,
	false,
	true,
	false
};

inline const GameConfig RAINBOW6_CONFIG = {
	0.8000f,
	0.4500f,
	0.6667f,
	0.3750f,
	64800,
	6480600,
	false,
	true,
	false
};

inline const GameConfig COD_CONFIG = {
	0.8300f,
	0.4669f,
	0.6917f,
	0.3892f,
	64800,
	6480600,
	false,
	true,
	false
};

inline const GameConfig WARZONE_CONFIG = {
	0.8300f,
	0.4669f,
	0.6917f,
	0.3892f,
	64800,
	6480600,
	false,
	true,
	false
};

inline const GameConfig RUST_CONFIG = {
	0.8500f,
	0.4781f,
	0.7083f,
	0.3984f,
	64800,
	6480600,
	true,
	false,
	false
};

inline const GameConfig CUSTOM_CONFIG = {
	0.8390f,
	0.4716f,
	0.7002f,
	0.4040f,
	64800,
	6480600,
	false,
	false,
	false
};

inline GameConfig ActiveGame = FORTNITE_CONFIG;
inline GameType ActiveGameType = GameType::Fortnite;

inline void SetActiveGame(GameType type)
{
	ActiveGameType = type;
	switch (type)
	{
	case GameType::Fortnite:	ActiveGame = FORTNITE_CONFIG;	break;
	case GameType::Valorant:	ActiveGame = VALORANT_CONFIG;	break;
	case GameType::CS2:			ActiveGame = CS2_CONFIG;		break;
	case GameType::RainbowSix:	ActiveGame = RAINBOW6_CONFIG;	break;
	case GameType::COD:			ActiveGame = COD_CONFIG;		break;
	case GameType::Warzone:		ActiveGame = WARZONE_CONFIG;	break;
	case GameType::Rust:		ActiveGame = RUST_CONFIG;		break;
	case GameType::Custom:		ActiveGame = CUSTOM_CONFIG;		break;
	}
}

namespace Game2
{
	inline float HF_FOV_X() { return ActiveGame.HF_FOV_X; }
	inline float HF_FOV_Y() { return ActiveGame.HF_FOV_Y; }
	inline float ADS_FOV_X() { return ActiveGame.ADS_FOV_X; }
	inline float ADS_FOV_Y() { return ActiveGame.ADS_FOV_Y; }
	inline int HF_360() { return ActiveGame.HF_360; }
	inline int ADS_360() { return ActiveGame.ADS_360; }
	inline bool THIRD_PERSON() { return ActiveGame.THIRD_PERSON; }
	inline bool HAS_TRIGGERBOT() { return ActiveGame.HAS_TRIGGERBOT; }
	inline bool FORCE_TRIGGER() { return ActiveGame.FORCE_TRIGGER; }
}
