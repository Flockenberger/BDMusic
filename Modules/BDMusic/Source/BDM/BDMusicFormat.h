#pragma once
#include "Core/Base.h"
#include <string>
#include <vector>

constexpr flib::UInt32 BDM_FILE_VERSION = 9;
constexpr flib::UInt32 BDM_NAME_LENGTH = 31;              // wchar_t, the last one always 0
constexpr flib::UInt32 BDM_INSTRUMENT_LIST_LENGTH = 201;
constexpr flib::UInt16 BDM_MAX_BPM = 200;
constexpr flib::UInt32 BDM_MAX_NOTES_PER_CHUNK = 730;
constexpr flib::Int8 BDM_DEFAULT_VOLUME = 70;

/// The most notes the composer lets one write on a track, as seen in game (the client sends it with FromClient_SetMusicLimit) - the file holds more
constexpr flib::UInt32 BDM_COMPOSER_NOTE_LIMIT = 10000;

struct BDMNote
{
	flib::UInt8 NoteNo = 60;
	flib::UInt8 Articulation = 0;
	flib::Int8 OnVelocity = 100;
	flib::Int8 OffVelocity = 100;

	flib::Double StartMs = 0.0;
	flib::Double LengthMs = 0.0;
};

/// The settings the composer keeps once for the whole song
struct BDMEffects
{
	flib::UInt8 ReverbTime = 0;
	flib::UInt8 DelayFeedback = 0;
	flib::UInt8 ChorusFeedback = 0;
	flib::UInt8 ChorusLfoDepth = 0;
	flib::UInt8 ChorusLfoFrequency = 0;
};

struct BDMTrack
{
	flib::UInt8 Instrument = 17;
	flib::Int8 Volume = BDM_DEFAULT_VOLUME;

	/// How much of the track goes to each effect, 0-100
	flib::UInt8 Reverb = 0;
	flib::UInt8 Delay = 0;
	flib::UInt8 Chorus = 0;

	std::vector<BDMNote> Notes;
};

struct BDMSong
{
	flib::UInt32 Version = BDM_FILE_VERSION;

	flib::Int64 UserNo = 0;
	std::wstring Author;
	std::wstring LastComposer;

	flib::UInt16 Bpm = 120;
	flib::UInt16 Beat = 4;
	BDMEffects Effects;

	std::vector<BDMTrack> Tracks;

	flib::Bool Editable = true;
};
