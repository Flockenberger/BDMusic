#pragma once
#include "Core/Base.h"
#include <vector>

/*
 * ICE ("Information Concealment Engine"), the 64-bit block cipher by Matthew
 * Kwan (public domain), as Black Desert uses it: thin ICE - 8 rounds - with an
 * 8-byte key, one block after the other (ECB).
 */
class IceKey
{
public:
	static constexpr flib::UInt32 BlockSize = 8;

	explicit IceKey(flib::Int32 level = 0);

	IceKey(const IceKey&) = delete;
	IceKey& operator=(const IceKey&) = delete;

	void SetKey(const flib::UInt8* key);

	void EncryptBlock(const flib::UInt8* plain, flib::UInt8* cipher) const;
	void DecryptBlock(const flib::UInt8* cipher, flib::UInt8* plain) const;

	flib::Bool Encrypt(flib::UInt8* data, flib::UInt64 size) const;
	flib::Bool Decrypt(flib::UInt8* data, flib::UInt64 size) const;

private:
	struct Subkey
	{
		flib::UInt32 Val[3];
	};

	void BuildSchedule(flib::UInt16* kb, flib::Int32 n, const flib::Int32* keyrot);

	flib::Int32 m_Size = 0;
	flib::Int32 m_Rounds = 0;
	std::vector<Subkey> m_Schedule;
};


