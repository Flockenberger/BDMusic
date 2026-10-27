#pragma once
#include "Core/Base.h"
#include <vector>

/*
 * The composer's instruments: the index a track names, what the composer
 * shows of each - the notes its keyboard spans (musiccomposition.js) - and the
 * articulations it plays (gamecommondata midiinstrument; the first is a plain
 * note's, 99 where an instrument has none).
 */
struct BDMInstrument
{
	flib::UInt8 Index;
	const flib::Char* Name;

	flib::UInt8 LowestNote;
	flib::UInt8 HighestNote;

	/// Its notes pick sounds rather than pitches - the drum set, the hand drum, the cymbals
	flib::Bool Keyed;

	std::vector<flib::UInt8> Articulations;

	flib::UInt8 DefaultArticulation() const { return Articulations.empty() ? 99 : Articulations.front(); }

	flib::Bool CanPlay(flib::Int32 articulation) const
	{
		for (flib::UInt8 a : Articulations)
		{
			if (a == articulation)
			{
				return true;
			}
		}
		return false;
	}
};

/// The articulations by index, as the client names them (string table AUDIO_MIDI_ARTICULATION_n)
namespace BDMArticulation
{
	constexpr flib::UInt8 Sustain = 0;
	constexpr flib::UInt8 Tab = 1;
	constexpr flib::UInt8 Cut = 2;
	constexpr flib::UInt8 SlideUp = 3;
	constexpr flib::UInt8 Short = 4;
	constexpr flib::UInt8 Pizzicato = 5;
	constexpr flib::UInt8 Tremolo = 6;
	constexpr flib::UInt8 TrillMinor = 7;
	constexpr flib::UInt8 TrillMajor = 8;
	constexpr flib::UInt8 ChordMajor = 9;
	constexpr flib::UInt8 ChordMinor = 10;
	constexpr flib::UInt8 SustainPedal = 11;
	constexpr flib::UInt8 SlideDown = 12;
	constexpr flib::UInt8 Mute = 13;
	constexpr flib::UInt8 Harmonics = 14;
	constexpr flib::UInt8 Triplet = 15;
	constexpr flib::UInt8 Glissando = 16;
	constexpr flib::UInt8 Vibrato = 17;
	constexpr flib::UInt8 Marcato = 18;
	constexpr flib::UInt8 FilterSustain = 19;
	constexpr flib::UInt8 FilterBrassy = 20;
	constexpr flib::UInt8 FilterPluck = 21;
	constexpr flib::UInt8 Slap = 22;
	constexpr flib::UInt8 GlissUp = 23;
	constexpr flib::UInt8 XNotes = 24;
	constexpr flib::UInt8 Fx = 25;
	constexpr flib::UInt8 SusPiano = 26;
	constexpr flib::UInt8 SusMezzoForte = 27;
	constexpr flib::UInt8 SusForte = 28;
	constexpr flib::UInt8 None = 99;

	/// "Pizzicato", or the number for one the client does not name
	const flib::Char* Name(flib::Int32 articulation);
}

class BDMInstruments
{
public:
	static constexpr flib::UInt8 None = 255;
	static constexpr flib::UInt8 HandDrum = 4;
	static constexpr flib::UInt8 DrumSet = 13;
	static constexpr flib::UInt8 AdvancedPiano = 17;
	static constexpr flib::UInt8 GuitarDrive = 37;
	static constexpr flib::UInt8 GuitarDistortion = 38;

	static const std::vector<BDMInstrument>& All();

	/// Null when the composer has no such instrument
	static const BDMInstrument* Find(flib::Int32 index);

	/// The closest instrument to a General MIDI program (0-127); None for sound effects
	static flib::UInt8 FromGeneralMidiProgram(flib::Int32 program);

	/// The technique a General MIDI program names - muted guitar, slap bass, pizzicato strings; None for the others
	static flib::UInt8 ArticulationOfProgram(flib::Int32 program);

	/**
	 * \brief Where a General MIDI drum (the key on a drum channel) goes
	 *
	 * Kicks, snares, toms, hi-hats and cymbals to the drum set, bongos and
	 * congas to the hand drum. False for the percussion neither has - shakers,
	 * cowbells, whistles.
	 */
	static flib::Bool FromGeneralMidiDrum(flib::Int32 key, flib::UInt8& instrument, flib::UInt8& note);
};
