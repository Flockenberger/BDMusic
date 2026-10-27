#pragma once
#include "Core/Base.h"
#include "BDMusicFormat.h"
#include <map>
#include <string>
#include <vector>

struct BDMInstrument;

/// One instrument's part of a MIDI file: what one channel of one track plays with one program
struct BDMMidiPart
{
	flib::Int32 Track = 0;
	flib::Int32 Channel = 0;

	/// The General MIDI program, -1 on a drum channel
	flib::Int32 Program = 0;

	/// Channel 10, or a channel the file turned into drums (bank 120/127, GS or XG drum part)
	flib::Bool Drums = false;

	/// The track's name, when the file gives one
	flib::String Name;

	struct Note
	{
		flib::Int32 Key = 60;

		/// Semitones the file's tuning moves it: the channel's coarse tuning and key shift, the master
		/// transposition, and a pitch bend held from its start
		flib::Int32 Shift = 0;

		/// The note-on velocity as written, and scaled by the channel's expression and volume
		flib::Int32 RawVelocity = 100;
		flib::Double Velocity = 100.0;

		/// The note-off's velocity, -1 when it ended without one (a note-on of velocity 0, the file's end)
		flib::Int32 OffVelocity = -1;

		flib::Double StartSeconds = 0.0;

		/// A quarter note's seconds at the tempo where it starts
		flib::Double QuarterSeconds = 0.5;

		/// When the key was let go, and when the sound stopped - later, when a pedal held it
		flib::Double ReleaseSeconds = 0.0;
		flib::Double EndSeconds = 0.0;

		/// How it was played: bent a semitone or more after it started (+1 up, -1 down), with the
		/// modulation wheel up, under the sustain pedal
		flib::Int32 Bend = 0;
		flib::Bool Vibrato = false;
		flib::Bool Pedal = false;
	};

	std::vector<Note> Notes;

	/// The channel's volume (CC 7), reverb (CC 91), chorus (CC 93) and delay (CC 94) when the part's first note starts, 0-127
	flib::Int32 Volume = 100;
	flib::Int32 Reverb = 0;
	flib::Int32 Chorus = 0;
	flib::Int32 Delay = 0;

	flib::Bool IsDrums() const { return Drums; }
};

/// What the file sets for the whole song; -1 where it sets nothing
struct BDMMidiSongSettings
{
	/// The sequence's name: the first track's name
	flib::String Name;

	/// "GM", "GM2", "GS" or "XG" when the file resets a device to one
	flib::String Mode;

	/// From GS, XG or GM2 system exclusive messages, 0-127
	flib::Int32 ReverbTime = -1;
	flib::Int32 DelayFeedback = -1;
	flib::Int32 ChorusFeedback = -1;
	flib::Int32 ChorusDepth = -1;
	flib::Int32 ChorusRate = -1;

	/// The master volume at the first note, 0-127
	flib::Int32 MasterVolume = -1;
};

/**
 * \brief How to convert: every value left as it is takes what the MIDI file sets
 *
 * -1 (or empty) means "as the file has it"; anything else overrides the file.
 */
struct BDMConvertOptions
{
	flib::Int64 UserNo = 0;
	std::wstring Author;
	std::wstring LastComposer;   // empty: the author

	/// Part index -> instrument, BDMInstruments::None leaves the part out; parts not named go by their program
	std::map<flib::Int32, flib::Int32> PartInstruments;

	/// General MIDI program -> instrument, for every part with that program
	std::map<flib::Int32, flib::Int32> ProgramInstruments;

	/// Part index -> articulation for all its notes
	std::map<flib::Int32, flib::Int32> PartArticulations;

	/// Semitones on top of the file's own tuning
	flib::Int32 Transpose = 0;

	/// Off: velocities as written, without expression and volume
	flib::Bool UseDynamics = true;

	/// Off: the sustain and sostenuto pedals hold nothing
	flib::Bool UseSustain = true;

	/// Off: every note the instrument's plain articulation
	flib::Bool UseArticulations = true;

	/// Off: a note the file gives no technique plays the instrument's plain articulation, rather than
	/// the one a player would pick for it - Short or Mute when staccato, the winds' dynamics by its
	/// velocity, the sustain pedal on a piano part the file never pedals
	flib::Bool ArticulationDefaults = true;

	/// On: the off velocity is the on velocity, as the composer writes it, not the file's note-off velocity
	flib::Bool SameOffVelocity = false;

	/// The first note starts the song
	flib::Bool TrimLeadingSilence = false;

	flib::Int32 Bpm = -1;
	flib::Int32 Beat = -1;

	/// The song's effect settings, 0-100
	flib::Int32 ReverbTime = -1;
	flib::Int32 DelayFeedback = -1;
	flib::Int32 ChorusFeedback = -1;
	flib::Int32 ChorusDepth = -1;
	flib::Int32 ChorusRate = -1;

	/// Every track's volume and sends, 0-100
	flib::Int32 Volume = -1;
	flib::Int32 Reverb = -1;
	flib::Int32 Delay = -1;
	flib::Int32 Chorus = -1;
};

/**
 * \brief A MIDI file read into its parts, and turned into a Black Desert song
 *
 * Everything the file sets that a song can hold is taken over unless an
 * option overrides it: the name, tempo and time signature; programs, banks
 * and drum channels; volume, expression and the effect sends; the device's
 * effect settings, master volume and transposition from GS, XG and GM2 system
 * exclusive messages; coarse tuning and key shifts; the pedals, mono mode and
 * note-off velocities. Playing techniques the file implies - a program like
 * pizzicato strings or slap bass, the modulation wheel, bends, the sustain
 * pedal on the piano - become the instrument's articulations where it has
 * them. A note the file gives no technique gets the one a player would use:
 * Short (Mute on the drive and distortion guitars) when it is staccato, the
 * clarinet's and horn's dynamics by its velocity, and the sustain pedal on a
 * piano part the file never pedals. Each part goes to the composer's
 * instrument closest to its program, the drums to the drum set and the hand
 * drum; parts on the same instrument share its track, and notes outside an
 * instrument's keyboard move by octaves.
 */
class BDMConverter
{
public:
	/// False when the file cannot be read as MIDI
	flib::Bool Read(const flib::String& path);

	const std::vector<BDMMidiPart>& GetParts() const { return m_Parts; }
	const BDMMidiSongSettings& GetSongSettings() const { return m_Song; }

	/// The first tempo and time signature, 120 and 4/4 when the file has none
	flib::Double GetInitialBpm() const { return m_InitialBpm; }
	flib::Int32 GetBeatsPerBar() const { return m_Numerator; }
	flib::Int32 GetBeatUnit() const { return m_Denominator; }

	flib::Double GetDurationSeconds() const { return m_DurationSeconds; }

	/// The instrument a part goes to with \p options, BDMInstruments::None when it is left out
	flib::UInt8 InstrumentFor(flib::Int32 partIndex, const BDMConvertOptions& options) const;

	flib::Bool Convert(const BDMConvertOptions& options, BDMSong& out) const;

	/// What the file sets, its parts and the instruments they go to
	void PrintParts(const BDMConvertOptions& options) const;

private:
	/**
	 * \brief The articulation a note plays
	 *
	 * \p staccato: the note is let go well before the part's next one. \p velocity: its on velocity
	 * in the song. \p partPedals: some note of the part sounds under the sustain pedal.
	 */
	flib::UInt8 ArticulationFor(const BDMMidiPart& part, flib::Int32 partIndex, const BDMMidiPart::Note& note,
		const BDMInstrument& instrument, const BDMConvertOptions& options,
		flib::Bool staccato, flib::Int32 velocity, flib::Bool partPedals) const;

	std::vector<BDMMidiPart> m_Parts;
	BDMMidiSongSettings m_Song;

	flib::Double m_InitialBpm = 120.0;
	flib::Int32 m_Numerator = 4;
	flib::Int32 m_Denominator = 4;
	flib::Double m_DurationSeconds = 0.0;
};
