#include "BDMusicWriter.h"
#include "Crypt/IceKey.h"
#include "Crypt/IceKeyHelper.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace
{
	struct ByteBuffer
	{
		std::vector<flib::UInt8> Bytes;

		template <typename T>
		void Put(T value)
		{
			const flib::UInt8* p = reinterpret_cast<const flib::UInt8*>(&value);
			Bytes.insert(Bytes.end(), p, p + sizeof(T));
		}

		/// UTF-16, 30 characters at most and always ended
		void PutName(const std::wstring& name)
		{
			for (flib::UInt32 i = 0; i < BDM_NAME_LENGTH; i++)
			{
				const wchar_t c = i + 1 < BDM_NAME_LENGTH && i < name.size() ? name[i] : L'\0';
				Put<flib::UInt16>((flib::UInt16)c);
			}
		}

		void PutText(const flib::String& text, flib::UInt32 length)
		{
			for (flib::UInt32 i = 0; i < length; i++)
			{
				Put<flib::Char>(i + 1 < length && i < text.size() ? text[i] : '\0');
			}
		}
	};

	/* How many chunks the client splits a track's notes into - its own sum:
	   as many as it takes at 21 bytes a note out of 15329, plus one, which
	   stays empty; a track without notes still has its one chunk. Each takes
	   730 notes at most, in order. */
	flib::UInt32 ChunkCount(size_t notes)
	{
		if (notes == 0)
		{
			return 1;
		}

		return (flib::UInt32)std::ceil((flib::Float)(flib::Int32)(21 * notes) / 15329.0f) + 1;
	}
}

std::vector<flib::UInt8> BDMusicWriter::SerializePlain(const BDMSong& song)
{
	ByteBuffer body;

	body.Put<flib::Int64>(song.UserNo);
	body.PutName(song.Author);
	body.PutName(song.LastComposer);
	body.Put<flib::UInt16>(song.Bpm);
	body.Put<flib::UInt16>(song.Beat);

	flib::String instruments;
	for (const BDMTrack& track : song.Tracks)
	{
		if (!instruments.empty())
		{
			instruments += ",";
		}
		instruments += std::to_string(track.Instrument);
	}
	body.PutText(instruments, BDM_INSTRUMENT_LIST_LENGTH);

	body.Put<flib::UInt16>((flib::UInt16)song.Tracks.size());

	for (const BDMTrack& track : song.Tracks)
	{
		const flib::UInt32 chunks = ChunkCount(track.Notes.size());
		body.Put<flib::UInt16>((flib::UInt16)chunks);

		size_t next = 0;
		for (flib::UInt32 chunk = 0; chunk < chunks; chunk++)
		{
			const size_t count = std::min<size_t>(BDM_MAX_NOTES_PER_CHUNK, track.Notes.size() - next);

			body.Put<flib::UInt16>((flib::UInt16)(12 + 20 * count));
			body.Put<flib::UInt8>(track.Instrument);
			body.Put<flib::Int8>(track.Volume);
			body.Put<flib::UInt8>(track.Reverb);
			body.Put<flib::UInt8>(song.Effects.ReverbTime);
			body.Put<flib::UInt8>(track.Delay);
			body.Put<flib::UInt8>(song.Effects.DelayFeedback);
			body.Put<flib::UInt8>(track.Chorus);
			body.Put<flib::UInt8>(song.Effects.ChorusFeedback);
			body.Put<flib::UInt8>(song.Effects.ChorusLfoDepth);
			body.Put<flib::UInt8>(song.Effects.ChorusLfoFrequency);
			body.Put<flib::UInt16>((flib::UInt16)count);

			for (size_t i = next; i < next + count; i++)
			{
				const BDMNote& note = track.Notes[i];
				body.Put<flib::UInt8>(note.NoteNo);
				body.Put<flib::UInt8>(note.Articulation);
				body.Put<flib::Int8>(note.OnVelocity);
				body.Put<flib::Int8>(note.OffVelocity);
				body.Put<flib::Double>(note.StartMs);
				body.Put<flib::Double>(note.LengthMs);
			}

			next += count;
		}
	}

	body.Put<flib::UInt8>(song.Editable ? 1 : 0);
	return body.Bytes;
}

std::vector<flib::UInt8> BDMusicWriter::Serialize(const BDMSong& song)
{
	std::vector<flib::UInt8> body = SerializePlain(song);

	// Whole blocks for the cipher, zeros after the end as the client pads it
	body.resize((body.size() + IceKey::BlockSize - 1) / IceKey::BlockSize * IceKey::BlockSize, 0);

	IceKey ice;
	ice.SetKey(BDO_ICE_KEY);
	ice.Encrypt(body.data(), body.size());

	std::vector<flib::UInt8> file(sizeof(flib::UInt32));
	const flib::UInt32 version = song.Version;
	std::copy_n(reinterpret_cast<const flib::UInt8*>(&version), sizeof(version), file.begin());
	file.insert(file.end(), body.begin(), body.end());
	return file;
}

flib::Bool BDMusicWriter::Write(const BDMSong& song, const flib::String& path)
{
	const std::vector<flib::UInt8> file = Serialize(song);

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		LOG_ERROR("Cannot write {}", path);
		return false;
	}

	out.write(reinterpret_cast<const char*>(file.data()), (std::streamsize)file.size());
	return (bool)out;
}
