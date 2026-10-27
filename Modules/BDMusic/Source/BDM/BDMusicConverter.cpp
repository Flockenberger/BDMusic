#include "BDMusicConverter.h"
#include "BDMusicInstruments.h"
#include "Core/Log.h"
#include "Midi/MidiFile.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <tuple>

namespace
{
	/// A note as short as a click still has to last: drum hits often end on the tick they start
	constexpr flib::Double k_MinLengthMs = 10.0;

	/// MIDI's usual channel volume (CC 7 = 100) is played at the composer's usual track volume (70)
	constexpr flib::Double k_VolumeScale = 70.0 / 100.0;

	/// Staccato: held for an eighth note or less, and for at most this share of the time until the
	/// part's next note - notation programs play a staccato at half its length, a run of short
	/// notes that lead into each other stays as it is
	constexpr flib::Double k_StaccatoShare = 0.6;

	/// Notes starting this close together start together, as a chord's do
	constexpr flib::Double k_SameStartSeconds = 0.001;

	/// Where the clarinet's and horn's dynamics change: below the first SusPiano, below the second SusMezzoForte, else SusForte
	constexpr flib::Int32 k_MezzoForteVelocity = 64;
	constexpr flib::Int32 k_ForteVelocity = 96;

	struct ChannelState
	{
		flib::Int32 Program = 0;
		flib::Int32 BankMsb = -1;          // CC 0, -1 until the file sets it
		flib::Bool Drums = false;

		flib::Int32 Volume = 100;          // CC 7
		flib::Int32 Expression = 127;      // CC 11
		flib::Int32 Reverb = 0;            // CC 91
		flib::Int32 Chorus = 0;            // CC 93
		flib::Int32 Delay = 0;             // CC 94
		flib::Int32 Modulation = 0;        // CC 1

		flib::Bool Sustain = false;        // CC 64
		flib::Bool Sostenuto = false;      // CC 66
		flib::Bool Mono = false;           // CC 126 on, CC 127 off

		flib::Int32 Bend = 0;              // -8192 to 8191
		flib::Int32 BendRangeSemitones = 2;  // RPN 0
		flib::Int32 BendRangeCents = 0;
		flib::Int32 CoarseTune = 0;        // semitones, RPN 2
		flib::Int32 KeyShift = 0;          // semitones, GS part key shift

		flib::Int32 RpnMsb = 127;          // where data entry goes; 127/127 is nowhere
		flib::Int32 RpnLsb = 127;
		flib::Bool Nrpn = false;

		flib::Double BendSemitones() const
		{
			return Bend / 8192.0 * (BendRangeSemitones + BendRangeCents / 100.0);
		}
	};

	/// A note still sounding: its part, its place in the part's notes, and what its start was like
	struct OpenNote
	{
		size_t Part = 0;
		size_t Note = 0;
		flib::Double OnsetBend = 0.0;    // semitones
		flib::Bool Caught = false;       // its key was down when the sostenuto pedal went down
	};

	flib::Int32 Clamp(flib::Int32 value, flib::Int32 low, flib::Int32 high)
	{
		return std::max(low, std::min(high, value));
	}

	flib::Double RoundMs(flib::Double ms)
	{
		return std::round(ms * 1000.0) / 1000.0;
	}

	/// A MIDI value, 0-127, on the composer's scale, 0-100
	flib::Int32 To100(flib::Int32 value)
	{
		return Clamp((flib::Int32)std::lround(value * 100.0 / 127.0), 0, 100);
	}

	/// GS and XG write a key shift as 64 plus semitones, 0x28 to 0x58
	flib::Int32 KeyShiftOf(flib::Int32 value)
	{
		return Clamp(value, 0x28, 0x58) - 64;
	}

	/// A feedback written around 64 (GS delay, XG chorus): how much of it, on the 0-127 scale
	flib::Int32 CenteredAmount(flib::Int32 value)
	{
		return Clamp(std::abs(value - 64) * 2, 0, 127);
	}

	/// GS numbers its parts' blocks with the drum part first: 0 is channel 10, 1-9 channels 1-9, A-F 11-16
	flib::Int32 GsBlockChannel(flib::Int32 block)
	{
		return block == 0 ? 9 : (block <= 9 ? block - 1 : block);
	}

	flib::String InstrumentName(flib::UInt8 index)
	{
		if (index == BDMInstruments::None)
		{
			return "(left out)";
		}

		const BDMInstrument* instrument = BDMInstruments::Find(index);
		return instrument ? flib::String(instrument->Name) : "unknown instrument " + std::to_string(index);
	}

	flib::String Trimmed(const flib::String& text)
	{
		const size_t first = text.find_first_not_of(" \t\r\n");
		if (first == flib::String::npos)
		{
			return flib::String();
		}
		return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	}
}

flib::Bool BDMConverter::Read(const flib::String& path)
{
	m_Parts.clear();
	m_Song = BDMMidiSongSettings();
	m_InitialBpm = 120.0;
	m_Numerator = 4;
	m_Denominator = 4;
	m_DurationSeconds = 0.0;

	smf::MidiFile midi;
	if (!midi.read(path))
	{
		LOG_ERROR("{} is not a MIDI file that can be read", path);
		return false;
	}

	// The tracks' names, before they are joined into one; the first track's names the sequence
	std::vector<flib::String> trackNames((size_t)midi.getTrackCount());
	for (flib::Int32 track = 0; track < midi.getTrackCount(); track++)
	{
		for (flib::Int32 i = 0; i < midi[track].getEventCount(); i++)
		{
			if (midi[track][i].isTrackName())
			{
				trackNames[(size_t)track] = Trimmed(midi[track][i].getMetaContent());
				break;
			}
		}
	}
	if (!trackNames.empty())
	{
		m_Song.Name = trackNames[0];
	}

	/* The events in time order, each track's in the order the file has them.
	   Not midifile's joinTracks or its time analysis: both sort with qsort,
	   which orders a tick's controllers by number - data entry (6) before the
	   parameter it sets (100, 101) - and a bank select and a program change
	   either way round */
	midi.makeAbsoluteTicks();

	struct Ordered
	{
		flib::Int32 Tick;
		flib::Int32 Track;
		flib::Int32 Index;
	};

	std::vector<Ordered> order;
	std::vector<std::pair<flib::Int32, flib::Double>> tempos;   // tick, seconds a tick from there on
	const flib::Int32 ticksPerQuarter = std::max(1, midi.getTicksPerQuarterNote());

	for (flib::Int32 track = 0; track < midi.getTrackCount(); track++)
	{
		for (flib::Int32 i = 0; i < midi[track].getEventCount(); i++)
		{
			const smf::MidiEvent& event = midi[track][i];
			order.push_back({ event.tick, track, i });
			if (event.isTempo())
			{
				tempos.push_back({ event.tick, event.getTempoSPT(ticksPerQuarter) });
			}
		}
	}

	std::stable_sort(order.begin(), order.end(), [](const Ordered& a, const Ordered& b) { return a.Tick < b.Tick; });
	std::stable_sort(tempos.begin(), tempos.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	if (!tempos.empty())
	{
		m_InitialBpm = 60.0 / (tempos.front().second * ticksPerQuarter);
	}

	// The tempo map: where each tempo starts, in ticks and seconds; 120 bpm until the first
	struct TempoSpan
	{
		flib::Int32 Tick;
		flib::Double Seconds;
		flib::Double SecondsPerTick;
	};

	std::vector<TempoSpan> spans = { { 0, 0.0, 60.0 / (120.0 * ticksPerQuarter) } };
	for (const auto& [tick, secondsPerTick] : tempos)
	{
		const TempoSpan& previous = spans.back();
		const flib::Double seconds = previous.Seconds + (tick - previous.Tick) * previous.SecondsPerTick;
		if (tick == previous.Tick)
		{
			spans.back().SecondsPerTick = secondsPerTick;   // the last tempo of a tick holds
		}

		else
		{
			spans.push_back({ tick, seconds, secondsPerTick });
		}
	}

	auto spanAt = [&spans](flib::Int32 tick) -> const TempoSpan&
		{
			auto it = std::upper_bound(spans.begin(), spans.end(), tick, [](flib::Int32 t, const TempoSpan& span) { return t < span.Tick; });
			return *std::prev(it);
		};

	auto secondsAt = [&spanAt](flib::Int32 tick)
		{
			const TempoSpan& span = spanAt(tick);
			return span.Seconds + (tick - span.Tick) * span.SecondsPerTick;
		};

	ChannelState channels[16];
	channels[9].Drums = true;

	std::vector<std::deque<OpenNote>> open(16 * 128);   // keys down, by channel and key
	std::vector<OpenNote> held[16];                       // let go under the sustain pedal
	std::vector<OpenNote> sostenutoHeld[16];              // let go under the sostenuto pedal
	std::map<std::tuple<flib::Int32, flib::Int32, flib::Int32, flib::Bool>, size_t> partOf;

	flib::Bool signatureSeen = false;
	flib::Bool noteSeen = false;
	flib::Double lastSeconds = 0.0;
	flib::Int32 masterTranspose = 0;
	flib::Int32 masterVolume = -1;

	auto noteOf = [this](const OpenNote& open) -> BDMMidiPart::Note&
		{
			return m_Parts[open.Part].Notes[open.Note];
		};

	/// A setting for the whole song: what it is when the music starts, else the first time the file sets it
	auto setSong = [&](flib::Int32& field, flib::Int32 value)
		{
			if (!noteSeen || field < 0)
			{
				field = Clamp(value, 0, 127);
			}
		};

	auto forEachSounding = [&](flib::Int32 channel, const std::function<void(OpenNote&)>& action)
		{
			for (flib::Int32 key = 0; key < 128; key++)
			{
				for (OpenNote& note : open[(size_t)(channel * 128 + key)])
				{
					action(note);
				}
			}

			for (OpenNote& note : held[channel])
			{
				action(note);
			}

			for (OpenNote& note : sostenutoHeld[channel])
			{
				action(note);
			}
		};

	/* A note's velocity is the loudest its channel's expression and volume
	   make it while it sounds: a note struck at the bottom of a swell still
	   carries, one struck into silence that stays silent does not */
	auto gainOf = [&](const ChannelState& state, BDMMidiPart& part)
		{
			// A part that starts out muted takes its level from where it first becomes audible
			if (part.Volume == 0)
			{
				part.Volume = state.Volume;
			}

			return (state.Expression / 127.0) * (part.Volume > 0 ? (flib::Double)state.Volume / part.Volume : 0.0);
		};

	auto raiseSounding = [&](flib::Int32 channel)
		{
			forEachSounding(channel, [&](OpenNote& sounding)
				{
					BDMMidiPart::Note& note = noteOf(sounding);
					note.Velocity = std::max(note.Velocity, note.RawVelocity * gainOf(channels[channel], m_Parts[sounding.Part]));
				});
		};

	/// Stops everything sounding on a channel - the keys still down and what the pedals hold
	auto stopChannel = [&](flib::Int32 channel, flib::Double seconds)
		{
			for (flib::Int32 key = 0; key < 128; key++)
			{
				auto& queue = open[(size_t)(channel * 128 + key)];
				for (const OpenNote& note : queue)
				{
					noteOf(note).ReleaseSeconds = seconds;
					noteOf(note).EndSeconds = seconds;
				}
				queue.clear();
			}

			for (const OpenNote& note : held[channel])
			{
				noteOf(note).EndSeconds = seconds;
			}

			for (const OpenNote& note : sostenutoHeld[channel])
			{
				noteOf(note).EndSeconds = seconds;
			}
			held[channel].clear();
			sostenutoHeld[channel].clear();
		};

	auto releaseSustain = [&](flib::Int32 channel, flib::Double seconds)
		{
			ChannelState& state = channels[channel];
			for (const OpenNote& note : held[channel])
			{
				// Still caught by the sostenuto pedal: it keeps sounding
				if (state.Sostenuto && note.Caught)
				{
					sostenutoHeld[channel].push_back(note);
				}

				else
				{
					noteOf(note).EndSeconds = seconds;
				}
			}
			held[channel].clear();
			state.Sustain = false;
		};

	auto partFor = [&](flib::Int32 track, flib::Int32 channel) -> size_t
		{
			const ChannelState& state = channels[channel];
			const flib::Int32 program = state.Drums ? -1 : state.Program;
			const auto key = std::make_tuple(track, channel, program, state.Drums);

			auto it = partOf.find(key);
			if (it != partOf.end())
			{
				return it->second;
			}

			BDMMidiPart part;
			part.Track = track;
			part.Channel = channel;
			part.Program = program;
			part.Drums = state.Drums;
			part.Name = track >= 0 && track < (flib::Int32)trackNames.size() ? trackNames[(size_t)track] : flib::String();
			part.Volume = state.Volume;
			part.Reverb = state.Reverb;
			part.Chorus = state.Chorus;
			part.Delay = state.Delay;

			m_Parts.push_back(part);
			partOf[key] = m_Parts.size() - 1;
			return m_Parts.size() - 1;
		};

	auto setMasterVolume = [&](flib::Int32 value)
		{
			masterVolume = Clamp(value, 0, 127);
			if (noteSeen && m_Song.MasterVolume < 0)
			{
				m_Song.MasterVolume = masterVolume;
			}
		};

	/// Roland GS parameters, by address
	auto gsParameter = [&](flib::Int32 address, flib::Int32 value)
		{
			switch (address)
			{
			case 0x40007F: m_Song.Mode = "GS"; return;                       // GS reset
			case 0x400004: setMasterVolume(value); return;
			case 0x400005: masterTranspose = KeyShiftOf(value); return;      // master key shift
			case 0x400134: setSong(m_Song.ReverbTime, value); return;
			case 0x40013B: setSong(m_Song.ChorusFeedback, value); return;
			case 0x40013D: setSong(m_Song.ChorusRate, value); return;
			case 0x40013E: setSong(m_Song.ChorusDepth, value); return;
			case 0x400159: setSong(m_Song.DelayFeedback, CenteredAmount(value)); return;
			default: break;
			}

			// A part's: 40 1x yy, x its block
			if ((address & 0xFFF000) == 0x401000)
			{
				const flib::Int32 channel = GsBlockChannel((address >> 8) & 0x0F);
				switch (address & 0xFF)
				{
				case 0x15: channels[channel].Drums = value != 0; break;            // use for rhythm part
				case 0x16: channels[channel].KeyShift = KeyShiftOf(value); break;  // pitch key shift
				default: break;
				}
			}
		};

	/// Yamaha XG parameters, by address
	auto xgParameter = [&](flib::Int32 address, flib::Int32 value)
		{
			switch (address)
			{
			case 0x00007E: m_Song.Mode = "XG"; return;                       // XG system on
			case 0x000004: setMasterVolume(value); return;
			case 0x000006: masterTranspose = KeyShiftOf(value); return;      // transpose
			case 0x020102: setSong(m_Song.ReverbTime, (flib::Int32)std::lround(Clamp(value, 0, 69) * 127.0 / 69.0)); return;
			case 0x020122: setSong(m_Song.ChorusRate, value); return;        // chorus LFO frequency
			case 0x020123: setSong(m_Song.ChorusDepth, value); return;       // chorus LFO depth
			case 0x020124: setSong(m_Song.ChorusFeedback, CenteredAmount(value)); return;
			default: break;
			}

			// A part's mode: 08 nn 07, 0 normal, 1-5 drums
			if ((address & 0xFF00FF) == 0x080007)
			{
				const flib::Int32 part = (address >> 8) & 0xFF;
				if (part < 16)
				{
					channels[part].Drums = value != 0;
				}
			}
		};

	auto systemExclusive = [&](const smf::MidiEvent& event)
		{
			const size_t size = event.size();
			auto at = [&](size_t i) -> flib::Int32 { return i < size ? (flib::Int32)event[i] : -1; };

			// The data a message carries ends before F7, and before the checksum where it has one
			const size_t end = size > 0 && at(size - 1) == 0xF7 ? size - 1 : size;

			if (at(1) == 0x7E && at(3) == 0x09)
			{
				if (at(4) == 0x01)
				{
					m_Song.Mode = "GM";
				}

				if (at(4) == 0x03)
				{
					m_Song.Mode = "GM2";
				}
			}

			else if (at(1) == 0x7F && at(3) == 0x04)
			{
				if (at(4) == 0x01 && end >= 7)
				{
					setMasterVolume(at(6));
				}

				else if (at(4) == 0x04 && end >= 7)
				{
					masterTranspose = at(6) - 64;   // master coarse tuning
				}

				else if (at(4) == 0x05 && end >= 12 && at(5) == 1 && at(6) == 1 && at(7) == 1)
				{
					// Global parameter control: a slot - 01 01 the reverb, 01 02 the chorus - then parameter/value pairs
					const flib::Int32 slot = (at(8) << 8) | at(9);
					for (size_t i = 10; i + 1 < end; i += 2)
					{
						const flib::Int32 parameter = at(i), value = at(i + 1);
						if (slot == 0x0101 && parameter == 1)
						{
							setSong(m_Song.ReverbTime, value);
						}

						if (slot == 0x0102 && parameter == 1)
						{
							setSong(m_Song.ChorusRate, value);
						}

						if (slot == 0x0102 && parameter == 2)
						{
							setSong(m_Song.ChorusDepth, value);
						}

						if (slot == 0x0102 && parameter == 3)
						{
							setSong(m_Song.ChorusFeedback, value);
						}
					}
				}
			}
			else if (at(1) == 0x41 && at(3) == 0x42 && at(4) == 0x12 && end >= 10)
			{
				// GS data set: an address, the data, a checksum
				const flib::Int32 address = (at(5) << 16) | (at(6) << 8) | at(7);
				
				for (size_t i = 8; i + 1 < end; i++)
				{
					gsParameter(address + (flib::Int32)(i - 8), at(i));
				}
			}

			else if (at(1) == 0x43 && (at(2) & 0xF0) == 0x10 && at(3) == 0x4C && end >= 8)
			{
				// XG parameter change: an address, the data
				const flib::Int32 address = (at(4) << 16) | (at(5) << 8) | at(6);
				
				for (size_t i = 7; i < end; i++)
				{
					xgParameter(address + (flib::Int32)(i - 7), at(i));
				}
			}
		};

	// Every event in the file's order, as a device would have received them
	for (const Ordered& at : order)
	{
		smf::MidiEvent& event = midi[at.Track][at.Index];
		const flib::Bool noteOn = event.isNoteOn();

		const flib::Double seconds = secondsAt(at.Tick);
		lastSeconds = std::max(lastSeconds, seconds);

		if (event.isMeta())
		{
			if (event.isTimeSignature() && !signatureSeen && event.size() >= 5)
			{
				m_Numerator = event[3];
				m_Denominator = 1 << std::min<flib::Int32>(event[4], 6);
				signatureSeen = true;
			}

			continue;
		}

		const flib::Int32 status = event.size() > 0 ? event[0] : 0;
		if (status == 0xF0)
		{
			systemExclusive(event);
			continue;
		}

		if (status < 0x80 || status > 0xEF)
		{
			continue;
		}

		const flib::Int32 channel = status & 0x0F;
		ChannelState& state = channels[channel];

		if (noteOn)
		{
			const flib::Int32 key = event.getKeyNumber();

			// One note at a time in mono mode: the new one ends the others
			if (state.Mono)
			{
				stopChannel(channel, seconds);
			}

			// Struck again while a pedal still holds it: the held one ends here
			for (auto* pedal : { &held[channel], &sostenutoHeld[channel] })
			{
				for (auto it = pedal->begin(); it != pedal->end(); )
				{
					if (noteOf(*it).Key == key)
					{
						noteOf(*it).EndSeconds = seconds;
						it = pedal->erase(it);
					}
					else
					{
						++it;
					}
				}
			}

			if (!noteSeen)
			{
				m_Song.MasterVolume = masterVolume;
				noteSeen = true;
			}

			const size_t partIndex = partFor(event.track, channel);
			BDMMidiPart& part = m_Parts[partIndex];
			const flib::Double bend = state.BendSemitones();

			BDMMidiPart::Note note;
			note.Key = key;
			note.Shift = state.Drums ? 0 : state.CoarseTune + state.KeyShift + masterTranspose + (flib::Int32)std::lround(bend);
			note.RawVelocity = event.getVelocity();
			note.Velocity = note.RawVelocity * gainOf(state, part);
			note.StartSeconds = seconds;
			note.QuarterSeconds = spanAt(at.Tick).SecondsPerTick * ticksPerQuarter;
			note.ReleaseSeconds = seconds;
			note.EndSeconds = seconds;
			note.Vibrato = state.Modulation > 0;
			note.Pedal = state.Sustain;

			part.Notes.push_back(note);
			open[(size_t)(channel * 128 + key)].push_back({ partIndex, part.Notes.size() - 1, bend, false });
		}
		else if (event.isNoteOff())
		{
			auto& queue = open[(size_t)(channel * 128 + event.getKeyNumber())];
			if (queue.empty())
			{
				continue;
			}

			const OpenNote sounding = queue.front();
			queue.pop_front();

			BDMMidiPart::Note& note = noteOf(sounding);
			note.ReleaseSeconds = seconds;
			
			if ((status & 0xF0) == 0x80)
			{
				note.OffVelocity = event.getVelocity();
			}

			if (state.Sustain)
			{
				note.Pedal = true;
				held[channel].push_back(sounding);
			}

			else if (state.Sostenuto && sounding.Caught)
			{
				sostenutoHeld[channel].push_back(sounding);
			}

			else
			{
				note.EndSeconds = seconds;
			}
		}
		else if ((status & 0xF0) == 0xE0)
		{
			// A bend of a semitone or more after the note started slides it
			state.Bend = ((event.getP2() << 7) | event.getP1()) - 8192;
			const flib::Double bend = state.BendSemitones();
			forEachSounding(channel, [&](OpenNote& sounding)
				{
					BDMMidiPart::Note& note = noteOf(sounding);
					const flib::Double moved = bend - sounding.OnsetBend;
					
					if (note.Bend == 0 && std::abs(moved) >= 1.0)
					{
						note.Bend = moved > 0 ? 1 : -1;
					}
				});
		}

		else if (event.isController())
		{
			const flib::Int32 value = event.getP2();

			switch (event.getP1())
			{
			case 0:  state.BankMsb = value; break;
			case 7:  state.Volume = value; raiseSounding(channel); break;
			case 11: state.Expression = value; raiseSounding(channel); break;
			case 91: state.Reverb = value; break;
			case 93: state.Chorus = value; break;
			case 94: state.Delay = value; break;

			case 1:
				state.Modulation = value;
				if (value > 0)
				{
					forEachSounding(channel, [&](OpenNote& sounding) { noteOf(sounding).Vibrato = true; });
				}
				break;

			case 64:
				if (value >= 64)
				{
					state.Sustain = true;
					forEachSounding(channel, [&](OpenNote& sounding) { noteOf(sounding).Pedal = true; });
				}
				else if (state.Sustain)
				{
					releaseSustain(channel, seconds);
				}
				break;

			case 66:
				if (value >= 64 && !state.Sostenuto)
				{
					// It holds the keys down right now, and nothing struck after
					state.Sostenuto = true;
					for (flib::Int32 key = 0; key < 128; key++)
					{
						for (OpenNote& note : open[(size_t)(channel * 128 + key)])
						{
							note.Caught = true;
						}
					}
				}
				else if (value < 64 && state.Sostenuto)
				{
					state.Sostenuto = false;
					for (OpenNote& note : sostenutoHeld[channel])
					{
						note.Caught = false;
						if (state.Sustain)
						{
							held[channel].push_back(note);
						}
						else
						{
							noteOf(note).EndSeconds = seconds;
						}
					}
					sostenutoHeld[channel].clear();
					for (flib::Int32 key = 0; key < 128; key++)
					{
						for (OpenNote& note : open[(size_t)(channel * 128 + key)])
						{
							note.Caught = false;
						}
					}
				}
				break;

				// Registered parameters: the bend range and the coarse tuning
			case 101: state.RpnMsb = value; state.Nrpn = false; break;
			case 100: state.RpnLsb = value; state.Nrpn = false; break;
			case 99:
			case 98:  state.Nrpn = true; break;

			case 6:
				if (!state.Nrpn && state.RpnMsb == 0 && state.RpnLsb == 0)
				{
					state.BendRangeSemitones = value;
				}
				else if (!state.Nrpn && state.RpnMsb == 0 && state.RpnLsb == 2)
				{
					state.CoarseTune = value - 64;
				}
				break;

			case 38:
				if (!state.Nrpn && state.RpnMsb == 0 && state.RpnLsb == 0)
				{
					state.BendRangeCents = value;
				}
				break;

			case 120: // all sound off
				stopChannel(channel, seconds);
				break;

			case 121: // reset all controllers: not the volume, the bank or the program
				state.Expression = 127;
				state.Modulation = 0;
				state.Bend = 0;
				state.RpnMsb = state.RpnLsb = 127;
				state.Nrpn = false;
				if (state.Sustain)
				{
					releaseSustain(channel, seconds);
				}
				state.Sostenuto = false;
				for (const OpenNote& note : sostenutoHeld[channel])
				{
					noteOf(note).EndSeconds = seconds;
				}
				sostenutoHeld[channel].clear();
				break;

			case 123: // all notes off - what the pedals hold keeps sounding
				for (flib::Int32 key = 0; key < 128; key++)
				{
					auto& queue = open[(size_t)(channel * 128 + key)];
					for (const OpenNote& sounding : queue)
					{
						BDMMidiPart::Note& note = noteOf(sounding);
						note.ReleaseSeconds = seconds;
						if (state.Sustain)
						{
							note.Pedal = true;
							held[channel].push_back(sounding);
						}
						else if (state.Sostenuto && sounding.Caught)
						{
							sostenutoHeld[channel].push_back(sounding);
						}
						else
						{
							note.EndSeconds = seconds;
						}
					}
					queue.clear();
				}
				break;

			case 126: // mono mode, which also ends what sounds
				stopChannel(channel, seconds);
				state.Mono = true;
				break;

			case 127: // poly mode
				stopChannel(channel, seconds);
				state.Mono = false;
				break;

			default:
				break;
			}
		}
		else if (event.isPatchChange())
		{
			// A bank picks drums or melody where the file's device reads it so; GS keeps its drum parts by SysEx
			state.Program = event.getP1();
			if (state.BankMsb == 120 || state.BankMsb == 127)
			{
				state.Drums = true;
			}
			else if (state.BankMsb >= 0 && (m_Song.Mode == "XG" || m_Song.Mode == "GM2"))
			{
				state.Drums = false;
			}
		}
	}

	// Whatever still sounds when the file ends stops there
	for (flib::Int32 channel = 0; channel < 16; channel++)
	{
		stopChannel(channel, lastSeconds);
	}

	m_DurationSeconds = lastSeconds;

	// Listed by track and channel, as a MIDI editor shows them
	std::stable_sort(m_Parts.begin(), m_Parts.end(), [](const BDMMidiPart& a, const BDMMidiPart& b)
		{
			return std::tie(a.Track, a.Channel) < std::tie(b.Track, b.Channel);
		});

	return true;
}

flib::UInt8 BDMConverter::InstrumentFor(flib::Int32 partIndex, const BDMConvertOptions& options) const
{
	if (partIndex < 0 || partIndex >= (flib::Int32)m_Parts.size())
	{
		return BDMInstruments::None;
	}

	auto named = options.PartInstruments.find(partIndex);
	if (named != options.PartInstruments.end())
	{
		return BDMInstruments::Find(named->second) ? (flib::UInt8)named->second : BDMInstruments::None;
	}

	const BDMMidiPart& part = m_Parts[(size_t)partIndex];
	if (part.IsDrums())
	{
		return BDMInstruments::DrumSet;
	}

	auto byProgram = options.ProgramInstruments.find(part.Program);
	if (byProgram != options.ProgramInstruments.end())
	{
		return BDMInstruments::Find(byProgram->second) ? (flib::UInt8)byProgram->second : BDMInstruments::None;
	}

	return BDMInstruments::FromGeneralMidiProgram(part.Program);
}

flib::UInt8 BDMConverter::ArticulationFor(const BDMMidiPart& part, flib::Int32 partIndex, const BDMMidiPart::Note& note,
	const BDMInstrument& instrument, const BDMConvertOptions& options,
	flib::Bool staccato, flib::Int32 velocity, flib::Bool partPedals) const
{
	auto forced = options.PartArticulations.find(partIndex);
	if (forced != options.PartArticulations.end())
	{
		return instrument.CanPlay(forced->second) ? (flib::UInt8)forced->second : instrument.DefaultArticulation();
	}

	if (!options.UseArticulations || instrument.Keyed)
	{
		return instrument.DefaultArticulation();
	}

	// What the file asks for, the most particular first: the program's technique, a slide, vibrato, the pedal
	const flib::UInt8 candidates[] =
	{
		part.IsDrums() ? BDMInstruments::None : BDMInstruments::ArticulationOfProgram(part.Program),
		note.Bend > 0 ? BDMArticulation::SlideUp : BDMInstruments::None,
		note.Bend > 0 ? BDMArticulation::GlissUp : BDMInstruments::None,
		note.Bend < 0 ? BDMArticulation::SlideDown : BDMInstruments::None,
		note.Vibrato ? BDMArticulation::Vibrato : BDMInstruments::None,
		note.Pedal && options.UseSustain ? BDMArticulation::SustainPedal : BDMInstruments::None,
	};

	for (flib::UInt8 articulation : candidates)
	{
		if (articulation != BDMInstruments::None && instrument.CanPlay(articulation))
		{
			return articulation;
		}
	}

	if (!options.ArticulationDefaults)
	{
		return instrument.DefaultArticulation();
	}

	// Nothing in the file says how: what a player would do with a note like this one
	if (staccato)
	{
		if (instrument.CanPlay(BDMArticulation::Short))
		{
			return BDMArticulation::Short;
		}

		// Palm-muted, as a driven guitar plays its short notes
		if ((instrument.Index == BDMInstruments::GuitarDrive || instrument.Index == BDMInstruments::GuitarDistortion)
			&& instrument.CanPlay(BDMArticulation::Mute))
		{
			return BDMArticulation::Mute;
		}
	}

	// The clarinet and horn sample each dynamic on its own; the velocity is the note's dynamic
	if (instrument.CanPlay(BDMArticulation::SusPiano) && instrument.CanPlay(BDMArticulation::SusMezzoForte)
		&& instrument.CanPlay(BDMArticulation::SusForte))
	{
		if (velocity < k_MezzoForteVelocity)
		{
			return BDMArticulation::SusPiano;
		}
		if (velocity < k_ForteVelocity)
		{
			return BDMArticulation::SusMezzoForte;
		}
		return BDMArticulation::SusForte;
	}

	// A piano part with no pedal at all, as notation programs write them: played with it held
	if (instrument.Index == BDMInstruments::AdvancedPiano && !partPedals && options.UseSustain
		&& instrument.CanPlay(BDMArticulation::SustainPedal))
	{
		return BDMArticulation::SustainPedal;
	}

	return instrument.DefaultArticulation();
}

flib::Bool BDMConverter::Convert(const BDMConvertOptions& options, BDMSong& out) const
{
	out = BDMSong();
	out.UserNo = options.UserNo;
	out.Author = options.Author;
	out.LastComposer = options.LastComposer.empty() ? options.Author : options.LastComposer;

	const flib::Double bpm = options.Bpm > 0 ? options.Bpm : m_InitialBpm;
	out.Bpm = (flib::UInt16)Clamp((flib::Int32)std::lround(bpm), 1, BDM_MAX_BPM);
	if (bpm > BDM_MAX_BPM)
	{
		LOG_WARN("The tempo, {:.1f} bpm, is more than the composer allows; the song says {} - the notes still play where they are", bpm, out.Bpm);
	}

	// The composer's three: triple time, compound time, and the rest as four
	if (options.Beat == 3 || options.Beat == 4 || options.Beat == 6)
	{
		out.Beat = (flib::UInt16)options.Beat;
	}
	else if (m_Denominator == 8 && m_Numerator % 3 == 0)
	{
		out.Beat = 6;
	}
	else if (m_Numerator == 3)
	{
		out.Beat = 3;
	}
	else
	{
		out.Beat = 4;
	}

	auto effect = [](flib::Int32 option, flib::Int32 fromFile) -> flib::UInt8
		{
			return (flib::UInt8)(option >= 0 ? Clamp(option, 0, 100) : (fromFile >= 0 ? To100(fromFile) : 0));
		};
	out.Effects.ReverbTime = effect(options.ReverbTime, m_Song.ReverbTime);
	out.Effects.DelayFeedback = effect(options.DelayFeedback, m_Song.DelayFeedback);
	out.Effects.ChorusFeedback = effect(options.ChorusFeedback, m_Song.ChorusFeedback);
	out.Effects.ChorusLfoDepth = effect(options.ChorusDepth, m_Song.ChorusDepth);
	out.Effects.ChorusLfoFrequency = effect(options.ChorusRate, m_Song.ChorusRate);

	const flib::Double masterVolume = m_Song.MasterVolume >= 0 ? m_Song.MasterVolume / 127.0 : 1.0;

	struct Contribution
	{
		const BDMMidiPart* Part;
		flib::Int32 PartIndex;
		flib::UInt8 Instrument;
		const BDMMidiPart::Note* Note;
		flib::Int32 Key;
	};

	// Which notes go where: a part to its instrument, the drums note by note
	std::vector<Contribution> contributions;
	size_t leftOutParts = 0, leftOutNotes = 0, unknownDrums = 0;

	for (size_t i = 0; i < m_Parts.size(); i++)
	{
		const BDMMidiPart& part = m_Parts[i];
		const flib::UInt8 instrument = InstrumentFor((flib::Int32)i, options);

		if (instrument == BDMInstruments::None)
		{
			leftOutParts++;
			leftOutNotes += part.Notes.size();
			continue;
		}

		const flib::Bool drumMap = part.IsDrums() && instrument == BDMInstruments::DrumSet;

		for (const BDMMidiPart::Note& note : part.Notes)
		{
			flib::UInt8 target = instrument;
			flib::Int32 key = note.Key;

			if (drumMap)
			{
				flib::UInt8 mapped = 0;
				if (!BDMInstruments::FromGeneralMidiDrum(note.Key, target, mapped))
				{
					unknownDrums++;
					continue;
				}
				key = mapped;
			}
			else
			{
				key += note.Shift + options.Transpose;
			}

			contributions.push_back({ &part, (flib::Int32)i, target, &note, key });
		}
	}

	// A track's level is its loudest part's; quieter parts sharing it play softer
	std::map<flib::UInt8, flib::Int32> trackLevel, trackReverb, trackChorus, trackDelay;
	for (const Contribution& c : contributions)
	{
		trackLevel[c.Instrument] = std::max(trackLevel[c.Instrument], c.Part->Volume);
		trackReverb[c.Instrument] = std::max(trackReverb[c.Instrument], c.Part->Reverb);
		trackChorus[c.Instrument] = std::max(trackChorus[c.Instrument], c.Part->Chorus);
		trackDelay[c.Instrument] = std::max(trackDelay[c.Instrument], c.Part->Delay);
	}

	flib::Double firstStart = 0.0;
	if (options.TrimLeadingSilence && !contributions.empty())
	{
		firstStart = contributions.front().Note->StartSeconds;
		for (const Contribution& c : contributions)
		{
			firstStart = std::min(firstStart, c.Note->StartSeconds);
		}
	}

	/* How each part is played, for the notes the file gives no technique: where its notes start,
	   which tells a staccato note from a short one leading into the next, and whether it ever
	   uses the sustain pedal */
	std::vector<std::vector<flib::Double>> starts(m_Parts.size());
	std::vector<flib::Bool> partPedals(m_Parts.size(), false);
	for (size_t i = 0; i < m_Parts.size(); i++)
	{
		for (const BDMMidiPart::Note& note : m_Parts[i].Notes)
		{
			starts[i].push_back(note.StartSeconds);
			if (note.Pedal)
			{
				partPedals[i] = true;
			}
		}
		std::sort(starts[i].begin(), starts[i].end());
	}

	for (const auto& [part, articulation] : options.PartArticulations)
	{
		const flib::UInt8 instrument = InstrumentFor(part, options);
		const BDMInstrument* info = BDMInstruments::Find(instrument);
		if (info && !info->CanPlay(articulation))
		{
			LOG_WARN("{} cannot play {} - part {} keeps its plain notes", info->Name, BDMArticulation::Name(articulation), part);
		}
	}

	std::map<flib::UInt8, BDMTrack> tracks;
	size_t moved = 0, silent = 0, doubled = 0;

	for (const Contribution& c : contributions)
	{
		const BDMMidiPart::Note& source = *c.Note;

		// Struck where its channel was silent, and silent it stayed
		if (options.UseDynamics && source.Velocity < 0.5)
		{
			silent++;
			continue;
		}

		const BDMInstrument* instrument = BDMInstruments::Find(c.Instrument);
		BDMTrack& track = tracks[c.Instrument];
		track.Instrument = c.Instrument;

		// Into the instrument's keyboard, by octaves
		flib::Int32 key = c.Key;
		while (key < instrument->LowestNote)
		{
			key += 12;
		}
		while (key > instrument->HighestNote)
		{
			key -= 12;
		}
		if (key != c.Key)
		{
			moved++;
		}

		const flib::Int32 level = trackLevel[c.Instrument];
		const flib::Double share = level > 0 ? (flib::Double)c.Part->Volume / level : 1.0;
		const flib::Double velocity = options.UseDynamics ? source.Velocity * share : (flib::Double)source.RawVelocity;
		const flib::Double end = options.UseSustain ? source.EndSeconds : source.ReleaseSeconds;

		// Let go after an eighth or less, and well before the part's next note starts
		const std::vector<flib::Double>& partStarts = starts[(size_t)c.PartIndex];
		auto next = std::upper_bound(partStarts.begin(), partStarts.end(), source.StartSeconds + k_SameStartSeconds);
		const flib::Double held = source.ReleaseSeconds - source.StartSeconds;
		const flib::Bool staccato = next != partStarts.end() && held <= source.QuarterSeconds / 2.0 + k_SameStartSeconds
			&& held <= (*next - source.StartSeconds) * k_StaccatoShare;

		BDMNote note;
		note.NoteNo = (flib::UInt8)Clamp(key, 0, 127);
		note.OnVelocity = (flib::Int8)Clamp((flib::Int32)std::lround(velocity), 1, 127);
		note.Articulation = ArticulationFor(*c.Part, c.PartIndex, source, *instrument, options,
			staccato, note.OnVelocity, partPedals[(size_t)c.PartIndex]);
		note.OffVelocity = options.SameOffVelocity || source.OffVelocity < 0 ? note.OnVelocity : (flib::Int8)Clamp(source.OffVelocity, 0, 127);
		note.StartMs = RoundMs((source.StartSeconds - firstStart) * 1000.0);
		note.LengthMs = RoundMs(std::max((end - source.StartSeconds) * 1000.0, k_MinLengthMs));

		track.Notes.push_back(note);
	}

	auto send = [](flib::Int32 option, flib::Int32 fromFile) -> flib::UInt8
		{
			return (flib::UInt8)(option >= 0 ? Clamp(option, 0, 100) : To100(fromFile));
		};

	for (auto& [index, track] : tracks)
	{
		track.Volume = (flib::Int8)(options.Volume >= 0
			? Clamp(options.Volume, 0, 100)
			: Clamp((flib::Int32)std::lround(trackLevel[index] * k_VolumeScale * masterVolume), 0, 100));
		track.Reverb = send(options.Reverb, trackReverb[index]);
		track.Delay = send(options.Delay, trackDelay[index]);
		track.Chorus = send(options.Chorus, trackChorus[index]);

		std::stable_sort(track.Notes.begin(), track.Notes.end(), [](const BDMNote& a, const BDMNote& b)
			{
				return a.StartMs != b.StartMs ? a.StartMs < b.StartMs : a.NoteNo < b.NoteNo;
			});

		/* Parts that double each other - files often carry a line twice, on
		   two tracks - would strike the same key twice at once on one
		   instrument: one note, the louder and the longer */
		std::vector<BDMNote> unique;
		unique.reserve(track.Notes.size());
		for (const BDMNote& note : track.Notes)
		{
			if (!unique.empty() && unique.back().StartMs == note.StartMs && unique.back().NoteNo == note.NoteNo)
			{
				BDMNote& kept = unique.back();
				if (note.OnVelocity > kept.OnVelocity)
				{
					kept.OnVelocity = note.OnVelocity;
					kept.OffVelocity = note.OffVelocity;
				}
				kept.LengthMs = std::max(kept.LengthMs, note.LengthMs);
				doubled++;
				continue;
			}
			unique.push_back(note);
		}
		track.Notes = std::move(unique);

		out.Tracks.push_back(std::move(track));
	}

	if (out.Tracks.empty())
	{
		LOG_ERROR("Nothing to write: no part has notes an instrument plays");
		return false;
	}

	// What became of the file
	LOG_INFO("  {} bpm, beat {}; reverb time {}, delay feedback {}, chorus feedback {}, depth {}, rate {}",
		out.Bpm, out.Beat, (flib::Int32)out.Effects.ReverbTime, (flib::Int32)out.Effects.DelayFeedback,
		(flib::Int32)out.Effects.ChorusFeedback, (flib::Int32)out.Effects.ChorusLfoDepth, (flib::Int32)out.Effects.ChorusLfoFrequency);

	for (const BDMTrack& track : out.Tracks)
	{
		flib::Int32 low = 127, high = 0;
		std::map<flib::Int32, size_t> articulations;
		for (const BDMNote& note : track.Notes)
		{
			low = std::min(low, (flib::Int32)note.OnVelocity);
			high = std::max(high, (flib::Int32)note.OnVelocity);
			articulations[note.Articulation]++;
		}

		flib::String used;
		for (const auto& [articulation, uses] : articulations)
		{
			used += (used.empty() ? "" : ", ") + flib::String(BDMArticulation::Name(articulation)) + " " + std::to_string(uses);
		}

		LOG_INFO("  {:<28} {:>5} notes, velocity {}-{}, volume {}, sends {}/{}/{}, {}", InstrumentName(track.Instrument),
			track.Notes.size(), low, high, (flib::Int32)track.Volume, (flib::Int32)track.Reverb, (flib::Int32)track.Delay, (flib::Int32)track.Chorus, used);

		if (track.Notes.size() > BDM_COMPOSER_NOTE_LIMIT)
		{
			LOG_WARN("{} has more notes than the composer lets one write on a track ({})", InstrumentName(track.Instrument), BDM_COMPOSER_NOTE_LIMIT);
		}
	}

	if (moved)
	{
		LOG_INFO("{} notes moved by octaves into their instrument's range", moved);
	}

	if (silent)
	{
		LOG_INFO("{} notes left out that the channel's volume or expression kept silent", silent);
	}

	if (doubled)
	{
		LOG_INFO("{} notes merged that two parts played on the same key at the same time", doubled);
	}

	if (unknownDrums)
	{
		LOG_WARN("{} drum notes left out: percussion neither the drum set nor the hand drum has", unknownDrums);
	}

	if (leftOutParts)
	{
		LOG_WARN("{} parts ({} notes) left out", leftOutParts, leftOutNotes);
	}

	return true;
}

void BDMConverter::PrintParts(const BDMConvertOptions& options) const
{
	LOG_INFO("\"{}\": {} parts, {:.1f} s, {:.1f} bpm, {}/{}{}", m_Song.Name, m_Parts.size(), m_DurationSeconds, m_InitialBpm,
		m_Numerator, m_Denominator, m_Song.Mode.empty() ? flib::String() : ", " + m_Song.Mode);

	auto setting = [](flib::Int32 value) { return value >= 0 ? std::to_string(To100(value)) : flib::String("-"); };
	LOG_INFO("  the file sets: reverb time {}, delay feedback {}, chorus feedback {}, depth {}, rate {}, master volume {}",
		setting(m_Song.ReverbTime), setting(m_Song.DelayFeedback), setting(m_Song.ChorusFeedback),
		setting(m_Song.ChorusDepth), setting(m_Song.ChorusRate), m_Song.MasterVolume >= 0 ? std::to_string(m_Song.MasterVolume) : flib::String("-"));

	for (size_t i = 0; i < m_Parts.size(); i++)
	{
		const BDMMidiPart& part = m_Parts[i];

		flib::Int32 low = 127, high = 0, shiftLow = 0, shiftHigh = 0;
		flib::Double softest = 127.0, loudest = 0.0;
		size_t vibrato = 0, pedal = 0, bends = 0;
		for (const BDMMidiPart::Note& note : part.Notes)
		{
			low = std::min(low, note.Key);
			high = std::max(high, note.Key);
			shiftLow = std::min(shiftLow, note.Shift);
			shiftHigh = std::max(shiftHigh, note.Shift);
			softest = std::min(softest, note.Velocity);
			loudest = std::max(loudest, note.Velocity);
			vibrato += note.Vibrato;
			pedal += note.Pedal;
			bends += note.Bend != 0;
		}

		flib::String played;
		if (shiftLow != 0 || shiftHigh != 0)
		{
			played += ", tuned " + std::to_string(shiftLow) + (shiftHigh != shiftLow ? ".." + std::to_string(shiftHigh) : flib::String());
		}
		if (pedal)
		{
			played += ", pedal " + std::to_string(pedal);
		}
		if (vibrato)
		{
			played += ", vibrato " + std::to_string(vibrato);
		}
		if (bends)
		{
			played += ", bends " + std::to_string(bends);
		}

		const flib::String program = part.IsDrums() ? "Drums" : smf::MidiFile::getGMInstrumentName(part.Program);
		const flib::UInt8 instrument = InstrumentFor((flib::Int32)i, options);
		const flib::String target = part.IsDrums() && instrument == BDMInstruments::DrumSet
			? "Advanced Drum Set + Hand Drum" : InstrumentName(instrument);

		LOG_INFO("  [{}] track {} channel {:>2} {:<24} {:>5} notes, keys {}-{}, velocity {:.0f}-{:.0f}, volume {}, sends {}/{}/{}{}  -> {}{}",
			i, part.Track, part.Channel + 1, program, part.Notes.size(), low, high, softest, loudest,
			part.Volume, part.Reverb, part.Delay, part.Chorus, played, target,
			part.Name.empty() ? flib::String() : "   \"" + part.Name + "\"");
	}
}
