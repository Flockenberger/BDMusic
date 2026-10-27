#include "BDMusicInstruments.h"

namespace
{
	using A = std::vector<flib::UInt8>;

	const A k_None = { BDMArticulation::None };
	const A k_Synth = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 17, 18, 19, 20, 21 };
	const A k_ElectricGuitar = { 0, 6, 13, 14, 25 };

	const std::vector<BDMInstrument> k_Instruments =
	{
		{  0, "Acoustic Guitar",                12, 119, false, { 0, 99 } },
		{  1, "Flute",                          12, 107, false, { 0, 99 } },
		{  2, "Recorder",                       36,  83, false, k_None },
		{  4, "Hand Drum",                      60,  79, true,  k_None },
		{  5, "Piatti Cymbals",                 60,  71, true,  k_None },
		{  6, "Harp",                           36,  95, false, k_None },
		{  7, "Piano",                          12, 107, false, k_None },
		{  8, "Violin",                         36,  83, false, k_None },
		{ 10, "Advanced Acoustic Guitar",       36,  88, false, { 0, 3, 12, 13, 14, 15 } },
		{ 11, "Advanced Flute",                 48,  88, false, { 0, 1, 2, 3, 4, 15 } },
		{ 13, "Advanced Drum Set",              48,  64, true,  k_None },
		{ 14, "Advanced Electric Bass",         28,  64, false, { 0, 3, 12, 13, 14, 16, 22, 23, 24 } },
		{ 15, "Advanced Contrabass",            28,  64, false, { 0, 3, 12, 13, 14, 23 } },
		{ 16, "Advanced Harp",                  12,  90, false, { 0, 9, 10, 16 } },
		{ 17, "Advanced Piano",                 12, 107, false, { 0, 11 } },
		{ 18, "Advanced Violin",                43,  88, false, { 0, 1, 2, 3, 4, 5, 6, 7, 8 } },
		{ 19, "Advanced Hand Pan",              45,  88, false, k_None },
		{ 20, "Synth Sine",                     12, 100, false, k_Synth },
		{ 21, "Synth Sine Stereo",              12, 100, false, k_Synth },
		{ 22, "Synth Sine Super",               12, 100, false, k_Synth },
		{ 23, "Synth Sine Super Octave",        12, 100, false, k_Synth },
		{ 24, "Synth Triangle",                 12, 100, false, k_Synth },
		{ 25, "Synth Triangle Stereo",          12, 100, false, k_Synth },
		{ 26, "Synth Triangle Super",           12, 100, false, k_Synth },
		{ 27, "Synth Triangle Super Octave",    12, 100, false, k_Synth },
		{ 28, "Synth Square",                   12, 100, false, k_Synth },
		{ 29, "Synth Square Stereo",            12, 100, false, k_Synth },
		{ 30, "Synth Square Super",             12, 100, false, k_Synth },
		{ 31, "Synth Square Super Octave",      12, 100, false, k_Synth },
		{ 32, "Synth Saw",                      12, 100, false, k_Synth },
		{ 33, "Synth Saw Stereo",               12, 100, false, k_Synth },
		{ 34, "Synth Saw Super",                12, 100, false, k_Synth },
		{ 35, "Synth Saw Super Octave",         12, 100, false, k_Synth },
		{ 36, "Electric Guitar Clean",          24,  95, false, k_ElectricGuitar },
		{ 37, "Electric Guitar Drive",          24,  95, false, k_ElectricGuitar },
		{ 38, "Electric Guitar Distortion",     24,  95, false, k_ElectricGuitar },
		{ 39, "Advanced Clarinet",              24,  95, false, { 0, 4, 7, 8, 15, 26, 27, 28 } },
		{ 40, "Advanced Horn",                  24,  95, false, { 0, 3, 4, 12, 26, 27, 28 } },
	};

	const flib::Char* const k_ArticulationNames[] =
	{
		"Sustain", "Tab", "Cut", "Slide Up", "Short", "Pizzicato", "Tremolo", "Trill Minor",
		"Trill Major", "Chord Major", "Chord Minor", "Sustain Pedal", "Slide Down", "Mute", "Harmonics", "Triplet",
		"Glissando", "Vibrato", "Marcato", "Filter Sustain", "Filter Brassy", "Filter Pluck", "Slap", "Gliss Up",
		"X-notes", "FX (C2 - G2)", "SusPiano", "SusMezzoForte", "SusForte",
	};
}

const flib::Char* BDMArticulation::Name(flib::Int32 articulation)
{
	if (articulation == None)
	{
		return "none";
	}

	if (articulation >= 0 && articulation < (flib::Int32)(sizeof(k_ArticulationNames) / sizeof(k_ArticulationNames[0])))
	{
		return k_ArticulationNames[articulation];
	}

	return "unknown";
}

flib::UInt8 BDMInstruments::ArticulationOfProgram(flib::Int32 program)
{
	switch (program)
	{
	case 28: return BDMArticulation::Mute;       // electric guitar (muted)
	case 31: return BDMArticulation::Harmonics;  // guitar harmonics
	case 36:                                     // slap bass 1
	case 37: return BDMArticulation::Slap;       // slap bass 2
	case 44: return BDMArticulation::Tremolo;    // tremolo strings
	case 45: return BDMArticulation::Pizzicato;  // pizzicato strings
	default: return None;
	}
}

const std::vector<BDMInstrument>& BDMInstruments::All()
{
	return k_Instruments;
}

const BDMInstrument* BDMInstruments::Find(flib::Int32 index)
{
	for (const BDMInstrument& instrument : k_Instruments)
	{
		if (instrument.Index == index)
		{
			return &instrument;
		}
	}

	return nullptr;
}

flib::UInt8 BDMInstruments::FromGeneralMidiProgram(flib::Int32 program)
{
	if (program < 0 || program > 127)
	{
		return 17;
	}

	if (program <= 7)
	{
		return 17;  // pianos
	}
	if (program <= 15)
	{
		return 16;  // celesta, glockenspiel, music box, vibraphone, marimba, xylophone, bells, dulcimer
	}
	if (program <= 20)
	{
		return 22;  // organs
	}
	if (program <= 23)
	{
		return 39;  // accordion, harmonica, bandoneon
	}
	if (program <= 25)
	{
		return 10;  // nylon and steel guitar
	}
	if (program == 29)
	{
		return 37;  // overdriven guitar
	}
	if (program == 30)
	{
		return 38;  // distortion guitar
	}
	if (program <= 31)
	{
		return 36;  // jazz, clean, muted guitar, harmonics
	}
	if (program == 32)
	{
		return 15;  // acoustic bass
	}
	if (program <= 39)
	{
		return 14;  // electric, fretless, slap and synth basses
	}
	if (program == 43)
	{
		return 15;  // contrabass
	}
	if (program == 46)
	{
		return 16;  // orchestral harp
	}
	if (program == 47)
	{
		return 15;  // timpani: low and pitched
	}
	if (program <= 51)
	{
		return 18;  // violin, viola, cello, tremolo, pizzicato, string ensembles
	}
	if (program <= 54)
	{
		return 22;  // choir, voice
	}
	if (program <= 63)
	{
		return 40;  // orchestra hit, brass
	}
	if (program <= 71)
	{
		return 39;  // saxophones, oboe, english horn, bassoon, clarinet
	}
	if (program <= 79)
	{
		return 11;  // piccolo, flute, recorder, pan flute, bottle, shakuhachi, whistle, ocarina
	}
	if (program == 80)
	{
		return 28;  // square lead
	}
	if (program == 81)
	{
		return 32;  // saw lead
	}
	if (program <= 83)
	{
		return 24;  // calliope, chiff
	}
	if (program == 84)
	{
		return 37;  // charang
	}
	if (program == 85)
	{
		return 22;  // voice lead
	}
	if (program <= 87)
	{
		return 34;  // fifths, bass + lead
	}
	if (program <= 95)
	{
		return 26;  // pads
	}
	if (program <= 103)
	{
		return 22;  // synth effects
	}
	if (program <= 107)
	{
		return 10;  // sitar, banjo, shamisen, koto
	}
	if (program == 108)
	{
		return 19;  // kalimba
	}
	if (program == 109)
	{
		return 39;  // bagpipe
	}
	if (program == 110)
	{
		return 18;  // fiddle
	}
	if (program == 111)
	{
		return 39;  // shanai
	}
	if (program == 112)
	{
		return 16;  // tinkle bell
	}
	if (program <= 118)
	{
		return 19;  // agogo, steel drums, woodblock, taiko, toms, synth drum
	}
	return None;                    // reverse cymbal and the sound effects
}

flib::Bool BDMInstruments::FromGeneralMidiDrum(flib::Int32 key, flib::UInt8& instrument, flib::UInt8& note)
{
	/* The drum set's sounds, by note: 48 kick, 49 snare side, 50 snare hit,
	   51 rim shot, 52 snare flam, 53 tom 1 (highest), 54 hi-hat closed,
	   55 tom 2, 56 hi-hat pedal, 57 tom 3, 58 hi-hat open, 59 tom 4,
	   60 tom 5 (lowest), 61 crash, 62 ride, 63/64 snare rolls */
	instrument = DrumSet;

	switch (key)
	{
	case 35: case 36: note = 48; return true;  // bass drums
	case 37:          note = 49; return true;  // side stick
	case 38: case 40: note = 50; return true;  // snares
	case 39:          note = 51; return true;  // hand clap: the sharpest crack the set has
	case 41: case 43: note = 60; return true;  // floor toms
	case 42:          note = 54; return true;  // closed hi-hat
	case 44:          note = 56; return true;  // pedal hi-hat
	case 45:          note = 59; return true;  // low tom
	case 46:          note = 58; return true;  // open hi-hat
	case 47:          note = 57; return true;  // low-mid tom
	case 48:          note = 55; return true;  // hi-mid tom
	case 50:          note = 53; return true;  // high tom
	case 49: case 52: case 55: case 57:
	                  note = 61; return true;  // crashes, china, splash
	case 51: case 53: case 59:
	                  note = 62; return true;  // rides, ride bell
	default: break;
	}

	/* Bongos and congas: the hand drum's 60 bongo 1 open, 65/66/67 bongo 2
	   open/closed/flam, 72/73/74 conga 1 open/closed/flam, 77/78/79 conga 2 */
	instrument = HandDrum;

	switch (key)
	{
	case 60: note = 60; return true;  // high bongo
	case 61: note = 65; return true;  // low bongo
	case 62: note = 73; return true;  // muted high conga
	case 63: note = 72; return true;  // open high conga
	case 64: note = 77; return true;  // low conga
	default: break;
	}

	instrument = None;
	return false;
}
