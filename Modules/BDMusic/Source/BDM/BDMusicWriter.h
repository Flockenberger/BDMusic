#pragma once
#include "BDMusicFormat.h"
#include <vector>

/**
 * \brief A song as the client saves it: the version, then the encrypted body
 *
 * Laid out the way the client's own writer does it, down to how it splits a
 * track's notes - see BDMusicFormat.h.
 */
class BDMusicWriter
{
public:
	/// The whole file
	static std::vector<flib::UInt8> Serialize(const BDMSong& song);

	/// The body before it is encrypted (and padded), for looking at
	static std::vector<flib::UInt8> SerializePlain(const BDMSong& song);

	static flib::Bool Write(const BDMSong& song, const flib::String& path);
};
