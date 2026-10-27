/*
 * C++ implementation of the ICE encryption algorithm.
 *
 * Written by Matthew Kwan - July 1996 (public domain).
 */

#include "IceKey.h"
#include "IceKeyHelper.h"


namespace
{
	/* Modulo values for the S-boxes */
	const flib::Int32 k_SMod[4][4] = {
		{ 333, 313, 505, 369 },
		{ 379, 375, 319, 391 },
		{ 361, 445, 451, 397 },
		{ 397, 425, 395, 505 } };

	/* XOR values for the S-boxes */
	const flib::Int32 k_SXor[4][4] = {
		{ 0x83, 0x85, 0x9b, 0xcd },
		{ 0xcc, 0xa7, 0xad, 0x41 },
		{ 0x4b, 0x2e, 0xd4, 0x33 },
		{ 0xea, 0xcb, 0x2e, 0x04 } };

	/* Permutation values for the P-box */
	const flib::UInt32 k_PBox[32] = {
		0x00000001, 0x00000080, 0x00000400, 0x00002000,
		0x00080000, 0x00200000, 0x01000000, 0x40000000,
		0x00000008, 0x00000020, 0x00000100, 0x00004000,
		0x00010000, 0x00800000, 0x04000000, 0x20000000,
		0x00000004, 0x00000010, 0x00000200, 0x00008000,
		0x00020000, 0x00400000, 0x08000000, 0x10000000,
		0x00000002, 0x00000040, 0x00000800, 0x00001000,
		0x00040000, 0x00100000, 0x02000000, 0x80000000 };

	/* The key rotation schedule */
	const flib::Int32 k_KeyRot[16] = {
		0, 1, 2, 3, 2, 1, 3, 0,
		1, 3, 2, 0, 3, 1, 0, 2 };

	/* 8-bit Galois Field multiplication of a by b, modulo m */
	flib::UInt32 GfMult(flib::UInt32 a, flib::UInt32 b, flib::UInt32 m)
	{
		flib::UInt32 res = 0;

		while (b)
		{
			if (b & 1)
			{
				res ^= a;
			}

			a <<= 1;
			b >>= 1;

			if (a >= 256)
			{
				a ^= m;
			}
		}

		return res;
	}

	/* Galois Field exponentiation: the base to the power of 7, modulo m */
	flib::UInt32 GfExp7(flib::UInt32 b, flib::UInt32 m)
	{
		if (b == 0)
		{
			return 0;
		}

		flib::UInt32 x = GfMult(b, b, m);
		x = GfMult(b, x, m);
		x = GfMult(x, x, m);
		return GfMult(b, x, m);
	}

	/* The ICE 32-bit P-box permutation */
	flib::UInt32 Perm32(flib::UInt32 x)
	{
		flib::UInt32 res = 0;
		const flib::UInt32* pbox = k_PBox;

		while (x)
		{
			if (x & 1)
			{
				res |= *pbox;
			}
			pbox++;
			x >>= 1;
		}

		return res;
	}

	/* The S-boxes, built the first time a key is made */
	struct SBoxes
	{
		flib::UInt32 Box[4][1024];

		SBoxes()
		{
			for (flib::Int32 i = 0; i < 1024; i++)
			{
				const flib::Int32 col = (i >> 1) & 0xff;
				const flib::Int32 row = (i & 0x1) | ((i & 0x200) >> 8);

				Box[0][i] = Perm32(GfExp7(col ^ k_SXor[0][row], k_SMod[0][row]) << 24);
				Box[1][i] = Perm32(GfExp7(col ^ k_SXor[1][row], k_SMod[1][row]) << 16);
				Box[2][i] = Perm32(GfExp7(col ^ k_SXor[2][row], k_SMod[2][row]) << 8);
				Box[3][i] = Perm32(GfExp7(col ^ k_SXor[3][row], k_SMod[3][row]));
			}
		}
	};

	const SBoxes& Boxes()
	{
		static const SBoxes boxes;
		return boxes;
	}

	/* The single round ICE f function */
	flib::UInt32 RoundF(flib::UInt32 p, const flib::UInt32* sk)
	{
		/* Left and right half expansion, to 20 bits each */
		const flib::UInt32 tl = ((p >> 16) & 0x3ff) | (((p >> 14) | (p << 18)) & 0xffc00);
		const flib::UInt32 tr = (p & 0x3ff) | ((p << 2) & 0xffc00);

		/* The salt permutation, then the subkey */
		flib::UInt32 al = sk[2] & (tl ^ tr);
		flib::UInt32 ar = al ^ tr;
		al ^= tl;

		al ^= sk[0];
		ar ^= sk[1];

		const SBoxes& boxes = Boxes();
		return boxes.Box[0][al >> 10] | boxes.Box[1][al & 0x3ff]
			| boxes.Box[2][ar >> 10] | boxes.Box[3][ar & 0x3ff];
	}
}

IceKey::IceKey(flib::Int32 level)
{
	Boxes();

	if (level < 1)
	{
		m_Size = 1;
		m_Rounds = 8;
	}
	else
	{
		m_Size = level;
		m_Rounds = level * 16;
	}

	m_Schedule.resize((size_t)m_Rounds);
}

void IceKey::EncryptBlock(const flib::UInt8* plain, flib::UInt8* cipher) const
{
	flib::UInt32 l = ((flib::UInt32)plain[0] << 24) | ((flib::UInt32)plain[1] << 16) | ((flib::UInt32)plain[2] << 8) | plain[3];
	flib::UInt32 r = ((flib::UInt32)plain[4] << 24) | ((flib::UInt32)plain[5] << 16) | ((flib::UInt32)plain[6] << 8) | plain[7];

	for (flib::Int32 i = 0; i < m_Rounds; i += 2)
	{
		l ^= RoundF(r, m_Schedule[(size_t)i].Val);
		r ^= RoundF(l, m_Schedule[(size_t)i + 1].Val);
	}

	for (flib::Int32 i = 0; i < 4; i++)
	{
		cipher[3 - i] = (flib::UInt8)(r & 0xff);
		cipher[7 - i] = (flib::UInt8)(l & 0xff);
		r >>= 8;
		l >>= 8;
	}
}

void IceKey::DecryptBlock(const flib::UInt8* cipher, flib::UInt8* plain) const
{
	flib::UInt32 l = ((flib::UInt32)cipher[0] << 24) | ((flib::UInt32)cipher[1] << 16) | ((flib::UInt32)cipher[2] << 8) | cipher[3];
	flib::UInt32 r = ((flib::UInt32)cipher[4] << 24) | ((flib::UInt32)cipher[5] << 16) | ((flib::UInt32)cipher[6] << 8) | cipher[7];

	for (flib::Int32 i = m_Rounds - 1; i > 0; i -= 2)
	{
		l ^= RoundF(r, m_Schedule[(size_t)i].Val);
		r ^= RoundF(l, m_Schedule[(size_t)i - 1].Val);
	}

	for (flib::Int32 i = 0; i < 4; i++)
	{
		plain[3 - i] = (flib::UInt8)(r & 0xff);
		plain[7 - i] = (flib::UInt8)(l & 0xff);
		r >>= 8;
		l >>= 8;
	}
}

flib::Bool IceKey::Encrypt(flib::UInt8* data, flib::UInt64 size) const
{
	if (size % BlockSize)
	{
		return false;
	}

	flib::UInt8 block[BlockSize];
	for (flib::UInt64 at = 0; at < size; at += BlockSize)
	{
		EncryptBlock(data + at, block);
		for (flib::UInt32 i = 0; i < BlockSize; i++)
		{
			data[at + i] = block[i];
		}
	}

	return true;
}

flib::Bool IceKey::Decrypt(flib::UInt8* data, flib::UInt64 size) const
{
	if (size % BlockSize)
	{
		return false;
	}

	flib::UInt8 block[BlockSize];
	for (flib::UInt64 at = 0; at < size; at += BlockSize)
	{
		DecryptBlock(data + at, block);
		for (flib::UInt32 i = 0; i < BlockSize; i++)
		{
			data[at + i] = block[i];
		}
	}

	return true;
}

/* Set 8 rounds [n, n+7] of the key schedule */
void IceKey::BuildSchedule(flib::UInt16* kb, flib::Int32 n, const flib::Int32* keyrot)
{
	for (flib::Int32 i = 0; i < 8; i++)
	{
		const flib::Int32 kr = keyrot[i];
		Subkey& isk = m_Schedule[(size_t)(n + i)];

		for (flib::Int32 j = 0; j < 3; j++)
		{
			isk.Val[j] = 0;
		}

		for (flib::Int32 j = 0; j < 15; j++)
		{
			flib::UInt32& currSk = isk.Val[j % 3];

			for (flib::Int32 k = 0; k < 4; k++)
			{
				flib::UInt16& currKb = kb[(kr + k) & 3];
				const flib::Int32 bit = currKb & 1;

				currSk = (currSk << 1) | (flib::UInt32)bit;
				currKb = (flib::UInt16)((currKb >> 1) | ((bit ^ 1) << 15));
			}
		}
	}
}

void IceKey::SetKey(const flib::UInt8* key)
{
	if (m_Rounds == 8)
	{
		flib::UInt16 kb[4];
		for (flib::Int32 i = 0; i < 4; i++)
		{
			kb[3 - i] = (flib::UInt16)((key[i * 2] << 8) | key[i * 2 + 1]);
		}

		BuildSchedule(kb, 0, k_KeyRot);
		return;
	}

	for (flib::Int32 i = 0; i < m_Size; i++)
	{
		flib::UInt16 kb[4];
		for (flib::Int32 j = 0; j < 4; j++)
		{
			kb[3 - j] = (flib::UInt16)((key[i * 8 + j * 2] << 8) | key[i * 8 + j * 2 + 1]);
		}

		BuildSchedule(kb, i * 8, k_KeyRot);
		BuildSchedule(kb, m_Rounds - 8 - i * 8, &k_KeyRot[8]);
	}
}
