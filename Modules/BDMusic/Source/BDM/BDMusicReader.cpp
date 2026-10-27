#include "BDMusicReader.h"
#include "Crypt/IceKey.h"
#include "Crypt/IceKeyHelper.h"
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
	struct ByteReader
	{
		const std::vector<flib::UInt8>& Bytes;
		size_t At = 0;
		flib::Bool Failed = false;

		template <typename T>
		T Get()
		{
			T value{};
			if (At + sizeof(T) > Bytes.size())
			{
				Failed = true;
				return value;
			}
			std::memcpy(&value, Bytes.data() + At, sizeof(T));
			At += sizeof(T);
			return value;
		}

		std::wstring GetName()
		{
			std::wstring name;
			for (flib::UInt32 i = 0; i < BDM_NAME_LENGTH; i++)
			{
				const wchar_t c = (wchar_t)Get<flib::UInt16>();
				if (c == L'\0')
				{
					At += 2 * (BDM_NAME_LENGTH - 1 - i);
					break;
				}
				name += c;
			}
			return name;
		}

		void Skip(size_t count)
		{
			if (At + count > Bytes.size())
			{
				Failed = true;
			}
			At += count;
		}
	};
}

flib::Bool BDMusicReader::Decrypt(const std::vector<flib::UInt8>& file, std::vector<flib::UInt8>& out, flib::String& error)
{
	if (file.size() < sizeof(flib::UInt32) || (file.size() - sizeof(flib::UInt32)) % IceKey::BlockSize)
	{
		error = "not a music file: the body is not whole cipher blocks";
		return false;
	}

	out = file;

	IceKey ice;
	ice.SetKey(BDO_ICE_KEY);
	ice.Decrypt(out.data() + sizeof(flib::UInt32), out.size() - sizeof(flib::UInt32));
	return true;
}

flib::Bool BDMusicReader::Parse(const std::vector<flib::UInt8>& file, BDMSong& out, flib::String& error)
{
	out = BDMSong();

	std::vector<flib::UInt8> plain;
	if (!Decrypt(file, plain, error))
	{
		return false;
	}

	ByteReader reader{ plain };
	out.Version = reader.Get<flib::UInt32>();
	if (out.Version != 8 && out.Version != 9)
	{
		error = "version " + std::to_string(out.Version) + " - only 8 and 9 are read";
		return false;
	}

	out.UserNo = reader.Get<flib::Int64>();
	out.Author = reader.GetName();
	out.LastComposer = reader.GetName();
	out.Bpm = reader.Get<flib::UInt16>();
	out.Beat = reader.Get<flib::UInt16>();
	reader.Skip(BDM_INSTRUMENT_LIST_LENGTH);

	const flib::UInt16 trackCount = reader.Get<flib::UInt16>();
	for (flib::UInt16 t = 0; t < trackCount && !reader.Failed; t++)
	{
		BDMTrack track;
		const flib::UInt16 chunks = reader.Get<flib::UInt16>();

		for (flib::UInt16 c = 0; c < chunks && !reader.Failed; c++)
		{
			const flib::UInt16 size = reader.Get<flib::UInt16>();
			const size_t end = reader.At + size;

			track.Instrument = reader.Get<flib::UInt8>();
			track.Volume = reader.Get<flib::Int8>();
			track.Reverb = reader.Get<flib::UInt8>();
			out.Effects.ReverbTime = reader.Get<flib::UInt8>();
			track.Delay = reader.Get<flib::UInt8>();
			out.Effects.DelayFeedback = reader.Get<flib::UInt8>();
			track.Chorus = reader.Get<flib::UInt8>();
			out.Effects.ChorusFeedback = reader.Get<flib::UInt8>();
			out.Effects.ChorusLfoDepth = reader.Get<flib::UInt8>();
			out.Effects.ChorusLfoFrequency = reader.Get<flib::UInt8>();

			const flib::UInt16 count = reader.Get<flib::UInt16>();
			for (flib::UInt16 n = 0; n < count && !reader.Failed; n++)
			{
				BDMNote note;
				note.NoteNo = reader.Get<flib::UInt8>();
				note.Articulation = reader.Get<flib::UInt8>();
				note.OnVelocity = reader.Get<flib::Int8>();
				note.OffVelocity = reader.Get<flib::Int8>();
				note.StartMs = reader.Get<flib::Double>();
				note.LengthMs = reader.Get<flib::Double>();
				track.Notes.push_back(note);
			}

			if (reader.At != end)
			{
				error = "a chunk's size does not match its notes";
				return false;
			}
		}

		out.Tracks.push_back(std::move(track));
	}

	out.Editable = reader.Get<flib::UInt8>() != 0;

	if (reader.Failed)
	{
		error = "the file ends early";
		return false;
	}

	// Only the zeros the cipher's blocks are padded with may follow
	for (size_t i = reader.At; i < plain.size(); i++)
	{
		if (plain[i] != 0)
		{
			error = "unexpected bytes after the end";
			return false;
		}
	}

	return true;
}

flib::Bool BDMusicReader::Read(const flib::String& path, BDMSong& out, flib::String& error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		error = "cannot open " + path;
		return false;
	}

	const std::vector<flib::UInt8> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return Parse(file, out, error);
}
