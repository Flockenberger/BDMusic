#include "Core/Log.h"
#include "BDM/BDMusicConverter.h"
#include "BDM/BDMusicInstruments.h"
#include "BDM/BDMusicReader.h"
#include "BDM/BDMusicWriter.h"
#include "Main/Version.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>

namespace
{
	constexpr flib::Int64 k_DefaultUserNo = 123456;
	const flib::Char* const k_DefaultAuthor = "BDMusic";

	void PrintUsage() 
	{
		std::cout <<
			"Turns a MIDI file into a Black Desert song. Whatever the MIDI file sets is used\n"
			"unless an option below says otherwise.\n"
			"\n"
			"  BDMusic <song.mid> [options]       convert; by default into Documents\\Black Desert\\Music\n"
			"  BDMusic --parts <song.mid>         what the MIDI sets, its parts and the instruments they go to\n"
			"  BDMusic --dump <song> [notes]      show what a Black Desert song file holds\n"
			"  BDMusic --instruments              list the composer's instruments and their articulations\n"
			"  BDMusic --version                  the program's version\n"
			"\n"
			"  -o, --out <path>          the file to write, or the folder to write it in\n"
			"  --name <text>             the song's name, which is its file's (the MIDI file's name)\n"
			"  --force                   replace a song of that name\n"
			"  --author <text>           shown as the author (" << k_DefaultAuthor << ")\n"
			"  --composer <text>         shown as the last composer (the author)\n"
			"  --user-no <n>             the author's user number (" << k_DefaultUserNo << ")\n"
			"  --part <i>=<instrument>   play part i (from --parts) on that instrument, none leaves it out\n"
			"  --program <gm>=<instr.>   the same for every part with that General MIDI program\n"
			"  --articulation <i>=<a>    every note of part i with that articulation (see --instruments)\n"
			"  --no-articulations        plain notes, whatever technique the MIDI implies\n"
			"  --plain-defaults          notes the MIDI gives no technique stay plain: no Short or Mute when\n"
			"                            staccato, no dynamics on clarinet and horn, no pedal on unpedalled piano\n"
			"  --transpose <semitones>   move every note but the drums, on top of the MIDI's own tuning\n"
			"  --bpm <n>, --beat 3|4|6   instead of the MIDI's first tempo and time signature\n"
			"  --trim                    start the song at its first note\n"
			"  --no-sustain              ignore the sustain and sostenuto pedals\n"
			"  --no-dynamics             velocities as written, without expression and volume changes\n"
			"  --same-off-velocity       the off velocity the same as the on velocity, as the composer writes it\n"
			"  --volume <0-100>          every track's volume (from the MIDI's channel and master volume)\n"
			"  --reverb, --delay, --chorus <0-100>                  every track's effect sends (CC 91, 94, 93)\n"
			"  --reverb-time, --delay-feedback, --chorus-feedback,\n"
			"  --chorus-depth, --chorus-rate <0-100>                the song's effect settings (GS, XG, GM2)\n";
	}

	std::wstring Widen(const flib::String& text)
	{
		if (text.empty())
		{
			return std::wstring();
		}

		const int length = MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), nullptr, 0);
		std::wstring wide((size_t)std::max(length, 0), L'\0');
		MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), wide.data(), length);
		return wide;
	}

	flib::String Narrow(const std::wstring& text)
	{
		if (text.empty())
		{
			return flib::String();
		}

		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		flib::String narrow((size_t)std::max(length, 0), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), narrow.data(), length, nullptr, nullptr);
		return narrow;
	}

	/// Documents\Black Desert\Music, where the game lists the songs it plays; empty when it is not there
	std::filesystem::path GameMusicFolder()
	{
		PWSTR documents = nullptr;
		std::filesystem::path folder;

		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)))
		{
			folder = std::filesystem::path(documents) / L"Black Desert" / L"Music";
		}
		CoTaskMemFree(documents);

		std::error_code ignored;
		return std::filesystem::is_directory(folder, ignored) ? folder : std::filesystem::path();
	}

	/// The song's name as a file name: what Windows does not allow becomes _
	flib::String SafeFileName(flib::String name)
	{
		for (flib::Char& c : name)
		{
			if ((flib::UInt8)c < 0x20 || std::strchr("<>:\"/\\|?*", c))
			{
				c = '_';
			}
		}

		while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
		{
			name.pop_back();
		}

		return name.empty() ? flib::String("Song") : name;
	}

	flib::Bool ParseNumber(const flib::String& text, flib::Int64& out)
	{
		try
		{
			size_t used = 0;
			out = std::stoll(text, &used);
			return used == text.size();
		}
		catch (...)
		{
			return false;
		}
	}

	/// "a=b", b a number or "none"
	flib::Bool ParseMapping(const flib::String& text, flib::Int32& key, flib::Int32& value)
	{
		const size_t equals = text.find('=');
		if (equals == flib::String::npos)
		{
			return false;
		}

		flib::Int64 left = 0, right = 0;
		if (!ParseNumber(text.substr(0, equals), left))
		{
			return false;
		}

		const flib::String target = text.substr(equals + 1);
		if (target == "none" || target == "-1")
		{
			right = BDMInstruments::None;
		}
		else if (!ParseNumber(target, right) || !BDMInstruments::Find((flib::Int32)right))
		{
			return false;
		}

		key = (flib::Int32)left;
		value = (flib::Int32)right;
		return true;
	}

	flib::String InstrumentName(flib::UInt8 index)
	{
		const BDMInstrument* instrument = BDMInstruments::Find(index);
		return instrument ? flib::String(instrument->Name) : "instrument " + std::to_string(index);
	}

	/// "a=b" with b an articulation the client names, or 99 for none
	flib::Bool ParseArticulation(const flib::String& text, flib::Int32& part, flib::Int32& articulation)
	{
		const size_t equals = text.find('=');
		flib::Int64 left = 0, right = 0;
		if (equals == flib::String::npos || !ParseNumber(text.substr(0, equals), left) || !ParseNumber(text.substr(equals + 1), right))
		{
			return false;
		}

		if (flib::String(BDMArticulation::Name((flib::Int32)right)) == "unknown")
		{
			return false;
		}

		part = (flib::Int32)left;
		articulation = (flib::Int32)right;
		return true;
	}

	int ListInstruments()
	{
		for (const BDMInstrument& instrument : BDMInstruments::All())
		{
			flib::String articulations;
			for (flib::UInt8 a : instrument.Articulations)
			{
				articulations += (articulations.empty() ? "" : ", ") + std::to_string(a) + " " + BDMArticulation::Name(a);
			}

			LOG_INFO("  {:>2}  {:<28} notes {}-{}{}  articulations: {}", (flib::Int32)instrument.Index, instrument.Name,
				(flib::Int32)instrument.LowestNote, (flib::Int32)instrument.HighestNote, instrument.Keyed ? " (keyed: notes pick sounds)" : "", articulations);
		}
		return 0;
	}

	int Dump(const flib::String& path, flib::Int64 notesToShow)
	{
		BDMSong song;
		flib::String error;
		if (!BDMusicReader::Read(path, song, error))
		{
			LOG_ERROR("{}: {}", path, error);
			return 1;
		}

		LOG_INFO("{}: version {}, by {} (user {}), last saved by {}, {} bpm, beat {}, {}",
			path, song.Version, Narrow(song.Author), song.UserNo, Narrow(song.LastComposer), song.Bpm, song.Beat,
			song.Editable ? "editable" : "not editable");
		LOG_INFO("  effects: reverb time {}, delay feedback {}, chorus feedback {}, depth {}, rate {}",
			(flib::Int32)song.Effects.ReverbTime, (flib::Int32)song.Effects.DelayFeedback, (flib::Int32)song.Effects.ChorusFeedback,
			(flib::Int32)song.Effects.ChorusLfoDepth, (flib::Int32)song.Effects.ChorusLfoFrequency);

		for (const BDMTrack& track : song.Tracks)
		{
			flib::Int32 lowKey = 127, highKey = 0, softest = 127, loudest = 0;
			flib::Double end = 0.0;
			std::set<flib::Int32> articulations;

			for (const BDMNote& note : track.Notes)
			{
				lowKey = std::min(lowKey, (flib::Int32)note.NoteNo);
				highKey = std::max(highKey, (flib::Int32)note.NoteNo);
				softest = std::min(softest, (flib::Int32)note.OnVelocity);
				loudest = std::max(loudest, (flib::Int32)note.OnVelocity);
				end = std::max(end, note.StartMs + note.LengthMs);
				articulations.insert(note.Articulation);
			}

			flib::String used;
			for (flib::Int32 a : articulations)
			{
				used += (used.empty() ? "" : ", ") + flib::String(BDMArticulation::Name(a));
			}

			LOG_INFO("  {:>2} {:<28} volume {:>3}, sends {}/{}/{}, {:>5} notes, keys {}-{}, velocity {}-{}, articulations {}, ends at {:.1f} s",
				(flib::Int32)track.Instrument, InstrumentName(track.Instrument), (flib::Int32)track.Volume,
				(flib::Int32)track.Reverb, (flib::Int32)track.Delay, (flib::Int32)track.Chorus,
				track.Notes.size(), lowKey, highKey, softest, loudest, used, end / 1000.0);

			for (size_t i = 0; i < track.Notes.size() && (flib::Int64)i < notesToShow; i++)
			{
				const BDMNote& note = track.Notes[i];
				LOG_INFO("       key {:>3} articulation {:>2} velocity {:>3}/{:>3} at {:>10.3f} ms for {:>9.3f} ms",
					(flib::Int32)note.NoteNo, (flib::Int32)note.Articulation, (flib::Int32)note.OnVelocity, (flib::Int32)note.OffVelocity,
					note.StartMs, note.LengthMs);
			}
		}

		return 0;
	}
}

int main(int argc, char** argv)
{
	std::cout << "==" << "============================================================================" << "==" << "\n";
	std::cout << "==" << "============================================================================" << "==" << "\n";
	std::cout << "== " << "  __ _            _                                                        " << "==" << "\n";
	std::cout << "== " << " / _| |          | |                                                       " << "==" << "\n";
	std::cout << "== " << "| |_| | ___   ___| | ____      ____ _ _ __ ___                             " << "==" << "\n";
	std::cout << "== " << "|  _| |/ _ \\ / __| |/ /\\ \\ /\\ / / _` | '__/ _ \\                            " << "==" << "\n";
	std::cout << "== " << "| | | | (_) | (__|   <  \\ V  V / (_| | | |  __/                            " << "==" << "\n";
	std::cout << "== " << "|_| |_|\\___/ \\___|_|\\_\\  \\_/\\_/ \\__,_|_|  \\___|                            " << "==" << "\n";
	std::cout << "== " << "                                                                           " << "==" << "\n";
	std::cout << "==" << "============================================================================" << "==" << "\n";
	std::cout << "==" << "============================================================================" << "==" << "\n";

	std::vector<flib::String> args(argv + 1, argv + argc);
	if (args.empty() || args[0] == "-h" || args[0] == "--help")
	{
		PrintUsage();
		return args.empty() ? 1 : 0;
	}

	if (args[0] == "--version")
	{
		std::cout << "BDMusic " BDMUSIC_VERSION_TEXT " by " BDMUSIC_AUTHOR "\n";
		return 0;
	}

	if (args[0] == "--instruments")
	{
		return ListInstruments();
	}

	if (args[0] == "--dump")
	{
		if (args.size() < 2)
		{
			PrintUsage();
			return 1;
		}

		flib::Int64 notes = 0;
		if (args.size() > 2 && !ParseNumber(args[2], notes))
		{
			notes = 0;
		}
		return Dump(args[1], notes);
	}

	const flib::Bool listParts = args[0] == "--parts";
	flib::String input;
	flib::String output;
	flib::String name;
	flib::Bool force = false;

	BDMConvertOptions options;
	options.UserNo = k_DefaultUserNo;
	options.Author = Widen(k_DefaultAuthor);

	for (size_t i = listParts ? 1 : 0; i < args.size(); i++)
	{
		const flib::String& arg = args[i];
		const flib::Bool hasValue = i + 1 < args.size();
		flib::Int64 number = 0;

		auto value = [&]() -> flib::String { return args[++i]; };
		auto numberIn = [&](flib::Int64 low, flib::Int64 high, flib::Int32& target) -> flib::Bool
			{
				if (!hasValue || !ParseNumber(args[i + 1], number) || number < low || number > high)
				{
					return false;
				}
				target = (flib::Int32)number;
				++i;
				return true;
			};
		flib::Bool ok = true;

		if ((arg == "-o" || arg == "--out") && hasValue)
		{
			output = value();
		}
		else if (arg == "--name" && hasValue)
		{
			name = value();
		}
		else if (arg == "--author" && hasValue)
		{
			options.Author = Widen(value());
		}
		else if (arg == "--composer" && hasValue)
		{
			options.LastComposer = Widen(value());
		}
		else if (arg == "--user-no" && hasValue)
		{
			ok = ParseNumber(value(), options.UserNo);
		}
		else if (arg == "--force")
		{
			force = true;
		}
		else if (arg == "--trim")
		{
			options.TrimLeadingSilence = true;
		}
		else if (arg == "--no-sustain")
		{
			options.UseSustain = false;
		}
		else if (arg == "--no-dynamics")
		{
			options.UseDynamics = false;
		}
		else if (arg == "--no-articulations")
		{
			options.UseArticulations = false;
		}
		else if (arg == "--plain-defaults")
		{
			options.ArticulationDefaults = false;
		}
		else if (arg == "--same-off-velocity")
		{
			options.SameOffVelocity = true;
		}
		else if (arg == "--transpose")
		{
			ok = numberIn(-48, 48, options.Transpose);
		}
		else if (arg == "--bpm")
		{
			ok = numberIn(1, 1000, options.Bpm);
		}
		else if (arg == "--beat")
		{
			ok = numberIn(3, 6, options.Beat) && options.Beat != 5;
		}
		else if (arg == "--volume")
		{
			ok = numberIn(0, 100, options.Volume);
		}
		else if (arg == "--reverb")
		{
			ok = numberIn(0, 100, options.Reverb);
		}
		else if (arg == "--delay")
		{
			ok = numberIn(0, 100, options.Delay);
		}
		else if (arg == "--chorus")
		{
			ok = numberIn(0, 100, options.Chorus);
		}
		else if (arg == "--reverb-time")
		{
			ok = numberIn(0, 100, options.ReverbTime);
		}
		else if (arg == "--delay-feedback")
		{
			ok = numberIn(0, 100, options.DelayFeedback);
		}
		else if (arg == "--chorus-feedback")
		{
			ok = numberIn(0, 100, options.ChorusFeedback);
		}
		else if (arg == "--chorus-depth")
		{
			ok = numberIn(0, 100, options.ChorusDepth);
		}
		else if (arg == "--chorus-rate")
		{
			ok = numberIn(0, 100, options.ChorusRate);
		}
		else if ((arg == "--part" || arg == "--program") && hasValue)
		{
			flib::Int32 key = 0, target = 0;
			ok = ParseMapping(value(), key, target);
			if (ok)
			{
				(arg == "--part" ? options.PartInstruments : options.ProgramInstruments)[key] = target;
			}
		}
		else if (arg == "--articulation" && hasValue)
		{
			flib::Int32 part = 0, articulation = 0;
			ok = ParseArticulation(value(), part, articulation);
			if (ok)
			{
				options.PartArticulations[part] = articulation;
			}
		}
		else if (!arg.empty() && arg[0] != '-' && input.empty())
		{
			input = arg;
		}
		else
		{
			ok = false;
		}

		if (!ok)
		{
			LOG_ERROR("Cannot make sense of {} - see BDMusic --help, and --instruments for the instrument numbers", arg);
			return 1;
		}
	}

	if (input.empty())
	{
		PrintUsage();
		return 1;
	}

	BDMConverter converter;
	if (!converter.Read(input))
	{
		return 1;
	}

	if (listParts)
	{
		converter.PrintParts(options);
		return 0;
	}

	BDMSong song;
	if (!converter.Convert(options, song))
	{
		return 1;
	}

	/* Named as asked, else as the MIDI file is called. Not by the sequence name the file's first
	   track carries: often enough that is an instrument's - "Xylophone", "Guitar Lead 1" - or
	   "Untitled", and whoever saved the file gave it the name they knew it by */
	const std::filesystem::path midi(input);
	if (name.empty())
	{
		name = midi.stem().string();
	}
	const flib::String fileName = SafeFileName(name);

	// Where it goes: as asked, else into the game's music folder, else beside the MIDI file
	std::filesystem::path target;
	std::error_code ignored;
	if (!output.empty())
	{
		target = std::filesystem::is_directory(output, ignored) ? std::filesystem::path(output) / fileName : std::filesystem::path(output);
	}
	else if (const std::filesystem::path music = GameMusicFolder(); !music.empty())
	{
		target = music / fileName;
	}
	else
	{
		target = midi.parent_path() / fileName;
	}

	if (std::filesystem::exists(target, ignored) && !force)
	{
		LOG_ERROR("{} already exists - --force replaces it, --name picks another name", target.string());
		return 1;
	}

	if (!BDMusicWriter::Write(song, target.string()))
	{
		return 1;
	}

	// Read back what was written, the way the game will
	BDMSong check;
	flib::String error;
	if (!BDMusicReader::Read(target.string(), check, error))
	{
		LOG_ERROR("{} does not read back: {}", target.string(), error);
		return 1;
	}

	size_t notes = 0;
	for (const BDMTrack& track : check.Tracks)
	{
		notes += track.Notes.size();
	}

	LOG_INFO("Wrote {}: {} tracks, {} notes, {} bpm, beat {}", target.string(), check.Tracks.size(), notes, check.Bpm, check.Beat);
	return 0;
}
