#pragma once
#include "BDMusicFormat.h"
#include <vector>

/**
 * \brief A song file read back - one the client saved, or one written here
 *
 * Versions 8 and 9, which share a layout (the client converts 8's instrument
 * indices when it loads one). A track's chunks come back as one list of notes.
 */
class BDMusicReader
{
public:
	static flib::Bool Read(const flib::String& path, BDMSong& out, flib::String& error);
	static flib::Bool Parse(const std::vector<flib::UInt8>& file, BDMSong& out, flib::String& error);

	/// The file with its body decrypted, the version still in front
	static flib::Bool Decrypt(const std::vector<flib::UInt8>& file, std::vector<flib::UInt8>& out, flib::String& error);
};
