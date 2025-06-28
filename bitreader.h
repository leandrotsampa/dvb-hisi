/*
 * This header is created based on:
 *
 * MpegtTS Basic Parser
 * Copyright (c) jeoliva, All rights reserved.
 * URL: https://github.com/jeoliva/mpegts-basic-parser
 */

#ifndef __BITREADER_H__
#define __BITREADER_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/types.h>
#endif

typedef struct _BitReader {
	unsigned char *mData;
	size_t mSize;

	unsigned int mReservoir; /* Left-Aligned Bits */
	size_t mNumBitsLeft;
} BitReader;

void FillReservoir(BitReader *bitReader) {
	size_t i;
	bitReader->mReservoir = 0;

	for (i = 0; bitReader->mSize > 0 && i < 4; ++i) {
		bitReader->mReservoir = (bitReader->mReservoir << 8) | *(bitReader->mData);

		++bitReader->mData;
		--bitReader->mSize;
	}

	bitReader->mNumBitsLeft = 8 * i;
	bitReader->mReservoir <<= 32 - bitReader->mNumBitsLeft;
}

void PutBits(BitReader *bitReader, unsigned int x, size_t n) {
	bitReader->mReservoir = (bitReader->mReservoir >> n) | (x << (32 - n));
	bitReader->mNumBitsLeft += n;
}

void BitReaderInit(BitReader *bitReader, unsigned char *data, size_t size) {
	bitReader->mData = data;
	bitReader->mSize = size;
	bitReader->mReservoir = 0;
	bitReader->mNumBitsLeft = 0;
}

unsigned int GetBits(BitReader *bitReader, size_t n) {
	unsigned int result = 0;

	while (n > 0) {
		size_t m = n;

		if (bitReader->mNumBitsLeft == 0)
			FillReservoir(bitReader);
		if (m > bitReader->mNumBitsLeft)
			m = bitReader->mNumBitsLeft;

		result = (result << m) | (bitReader->mReservoir >> (32 - m));
		bitReader->mReservoir <<= m;
		bitReader->mNumBitsLeft -= m;
		n -= m;
	}

	return result;
}

void SkipBits(BitReader *bitReader, size_t n) {
	while (n > 32) {
		GetBits(bitReader, 32);
		n -= 32;
	}

	if (n > 0)
		GetBits(bitReader, n);
}

size_t NumBitsLeft(BitReader *bitReader) {
	return bitReader->mSize * 8 + bitReader->mNumBitsLeft;
}

unsigned char *GetBitReaderData(BitReader *bitReader) {
	return bitReader->mData - bitReader->mNumBitsLeft / 8;
}

#endif  /* __BITREADER_H__ */