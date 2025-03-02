#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cochran_can.h"
#include "cochran.h"
#include "cochran_log.h"
#include "cochran_sample.h"


// Handy way to access dive offset pointers
#define ptr_uint(t,p,i)		( ((t) == 3 ? array_uint24_le((p) + (i) * 3) : array_uint32_le((p) + (i) * 4) ) )

/*
 * The Cochran CAN file format is designed to be annoying to read.
 * k It's roughly:
 *
 * 0x00000: room for 65534 4-byte words, giving the starting offsets
 *   of the dives within this file.
 *
 * 0x3fff8: the size of the file + 1
 * 0x3ffff: 0 (high 32 bits of filesize? Bogus: the offsets into the file
 *   are 32-bit, so it can't be a large file anyway)
 *
 * 0x40000: byte 0x46
 * 0x40001: "block 0": 256 byte encryption key
 * 0x40101: the random modulus, or length of the key to use
 * 0x40102: block 1: Version and date of Analyst and a feature string identifying
 *          the computer features and the features of the file
 * 0x40138: Computer configuration page 1, 512 bytes
 * 0x40338: Computer configuration page 2, 512 bytes
 * 0x40538: Misc data (tissues) 1500 bytes
 * 0x40b14: Ownership data 512 bytes ???
 *
 * 0x4171c: Ownership data 512 bytes ??? <copy>
 *
 * 0x45415: Time stamp 17 bytes
 * 0x45426: Computer configuration page 1, 512 bytes <copy>
 * 0x45626: Computer configuration page 2, 512 bytes <copy>
 *
 */


/*
 * decode
 *
 * Decode is as simple as adding the key bytes to the cipher text.
 */

static void decode(const unsigned int start, const unsigned int end,
				   const unsigned char *key, unsigned offset, const unsigned char mod,
				   const unsigned char *cipher, const unsigned int size, unsigned char *cleartext) {
	for (unsigned int i = start; i < end && i < size; i++) {
		cleartext[i] = key[offset] + cipher[i];
		offset++;
		offset %= mod;
	}
}


/*
 * cochran_can_get_header_offset
 *
 * CAN files have 0x40000 bytes of dive pointers.
 * WAN files have 0x30000 bytes of dive pointers.
 * ANA files have 0x30000 bytes of dive pointers.
 */

static unsigned int cochran_can_get_header_offset(cochran_file_type_t file_type) {
	unsigned int offset = 0;

	switch (file_type) {
	case FILE_ANA:
	case FILE_WAN:
		offset = 0x30000;
		break;
	case FILE_CAN:
		offset = 0x40000;
		break;
	}

	return offset;
}


/*
 * cochran_can_get_address_size
 *
 * CAN files have 4 byte dive pointers.
 * WAN files can have 3 or 4 byte pointers.
 * ANA files have 3 byte pointers.
 */

static int cochran_can_get_address_size(cochran_file_type_t file_type, const unsigned char *ciphertext, unsigned int ciphertext_size) {
	int address_size = 0;

	if (file_type == FILE_ANA) return 3;

	switch (ciphertext[cochran_can_get_header_offset(file_type)]) {
	case 0x43:
	case 0x4f:
		address_size = 3;
		break;
	case 0x45:
	case 0x46:
   		address_size = 4;
   		break;
	}

	return address_size;
}


/*
 * cochran_can_ana_meta
 *
 * Determine information about the ANA file.
 */

static int cochran_can_ana_meta(cochran_can_meta_t * meta, const unsigned char *cleartext, unsigned int cleartext_size) {

	meta->header_offset = cochran_can_get_header_offset(FILE_ANA);
	meta->mod = cleartext[meta->header_offset] + 1;
	meta->key = cleartext + meta->header_offset + 1;
	meta->address_size = 3;
	meta->address_count = 0x10000;

	strncpy(meta->model, cleartext + meta->header_offset + meta->mod + 38, 3);

	// Dive decode information
	meta->decode_address[0] = 0;
	meta->decode_address[1] = 0x4c3;
	meta->decode_address[2] = 0x502;
	meta->decode_address[3] = 0x540;
	meta->decode_address[4] = -1;
	meta->decode_key_offset[0] = -1; // don't decode first section;
	for (int i = 1; i < 10; i++)
		meta->decode_key_offset[i] = 0;
	meta->decode_key_offset[2] = 0x3f; // strange key offset for 3rd section

	meta->log_offset = 0x4d8;
	meta->profile_offset = 0x540;

	return 0;
}


/*
 * cochran_can_can_meta
 *
 * Parse header to obtain meta data about the can or wan file.
 */

static int cochran_can_can_meta(cochran_can_meta_t *meta, cochran_file_type_t file_type, const unsigned char *cleartext, unsigned int cleartext_size) {

	meta->header_offset = cochran_can_get_header_offset(file_type);
	meta->address_size = cochran_can_get_address_size(file_type, cleartext, cleartext_size);

	meta->mod = cleartext[meta->header_offset + 0x101] + 1;
	meta->key = cleartext + meta->header_offset + 1;
	const unsigned char *header = cleartext + meta->header_offset + 0x102;

	strncpy(meta->model, header + 0x31, 3);
	meta->model[3] = 0;

	cochran_log_meta_t log_meta;
	if (cochran_log_meta(&log_meta, meta->model))
		return 1;

	meta->log_size = log_meta.log_size;
	meta->file_format = cleartext[meta->header_offset];

	// Determine addressing format
	switch(meta->file_format) {
	case 0x43:
		meta->decode_address[0] = 0;
		meta->decode_address[1] = 0x5dc;
		meta->decode_address[2] = 0x64a;
		meta->decode_address[3] = 0x659;
		meta->decode_address[4] = 0x6b9;
		meta->decode_address[5] = -1;
		meta->decode_key_offset[0] = -1; // don't decode first section;
		for (int i = 1; i < 10; i++)
			meta->decode_key_offset[i] = 0;
		meta->log_offset = 0x5f1;
		meta->profile_offset = (meta->log_size == 90 ? 0x6b9 : 0x6f1);
		break;
	case 0x4f:
		meta->decode_address[0] = 0;
		meta->decode_address[1] = 0x5dc;
		if (header[0x32] == '0') {
			// GemPNox
			meta->decode_address[2] = 0x6f1;	// 0x6f1: GemPNox, 0x6b9: others
		} else {
			meta->decode_address[2] = 0x6b9;	// 0x6f1: GemPNox, 0x6b9: others
		}
		meta->decode_address[3] = -1;
		meta->decode_key_offset[0] = -1; // don't decode first section;
		for (int i = 1; i < 10; i++)
			meta->decode_key_offset[i] = 0;
		meta->log_offset = 0x5f1;
		meta->profile_offset = (meta->log_size == 90 ? 0x6b9 : 0x6f1);
		break;
	case 0x45:
		meta->decode_address[0] = 0;
		meta->decode_address[1] = 0x5dc;
		meta->decode_address[2] = 0x6f1;// Cmd1Mix 0x6dc
		//meta->decode_address[3] = 0x7d5;
		meta->decode_address[3] = -1;
		meta->decode_key_offset[0] = -1; // don't decode first section;
		for (int i = 1; i < 10; i++)
			meta->decode_key_offset[i] = 0;
		meta->log_offset = 0x5f1;
		meta->profile_offset = (meta->log_size == 90 ? 0x6b9 : 0x6f1);
		break;
	case 0x46:
		meta->decode_address[0] = 0;
		meta->decode_address[1] = 0x0fff;
		meta->decode_address[2] = 0x1fff;
		meta->decode_address[3] = 0x2fff;
		meta->decode_address[4] = 0x48ff;
		meta->decode_address[5] = 0x4914 + meta->log_size;
		meta->decode_address[6] = -1;
		meta->decode_key_offset[0] = 1;
		for (int i = 1; i < 10; i++)
			meta->decode_key_offset[i] = 0;
		meta->log_offset = 0x4914;
		meta->profile_offset = meta->log_offset + meta->log_size;
		break;
	default:
		fprintf(stderr, "Uknown file format %02x.\n", cleartext[meta->header_offset]);
		exit(1);
		break;
	}

	meta->address_count = meta->header_offset / meta->address_size;

	return 0;
}


int cochran_can_meta(cochran_can_meta_t *meta, cochran_file_type_t file_type, const unsigned char *cleartext, unsigned int cleartext_size) {

	switch (file_type) {
	case FILE_ANA:
		return cochran_can_ana_meta(meta, cleartext, cleartext_size);
		break;
	case FILE_WAN:
	case FILE_CAN:
		return cochran_can_can_meta(meta, file_type, cleartext, cleartext_size);
		break;
	}

	return 1;
}


static int cochran_can_decode_dive(cochran_can_meta_t *meta, const unsigned char *dive, unsigned int dive_size, unsigned int dive_num, int last_dive, void *userdata) {
	unsigned char *cleartext = userdata;
	const unsigned char *dives = (unsigned char *) cleartext;

	const int dive_offset = ptr_uint(meta->address_size, dives, dive_num - 1);
	int *addr = meta->decode_address;
	int *koff = meta->decode_key_offset;

	if (!last_dive) {
		while (*addr != -1) {
			int end_addr = *(addr + 1);
			if (end_addr == -1) end_addr = dive_size;

			if (*koff == -1)
				memcpy(cleartext + dive_offset, dive, end_addr - *addr);
			else
				decode(*addr, end_addr, meta->key, *koff, meta->mod, dive, dive_size, cleartext + dive_offset);

			addr++;
			koff++;
		}
	} else {
		decode(0, dive_size, meta->key, 0, meta->mod, dive, dive_size, cleartext + dive_offset);
	}

	return 0;
}


/*
 * cochran_can_foreach_dive
 *
 * Process each dive blob through a user-supplied callback function.
 */

int cochran_can_foreach_dive(cochran_can_meta_t *meta, const unsigned char *cleartext, unsigned int cleartext_size, cochran_can_foreach_callback_t callback, void *userdata) {
	const unsigned char *dives = (unsigned char *) cleartext;

	if (cleartext_size < meta->header_offset + 0x102)
		return 1;

	int result = 0;
	unsigned int dive_end = 0;
	unsigned char *dive;
	unsigned int dive_size = 0;
	unsigned int i = 0;

	while (ptr_uint(meta->address_size, dives, i) && i < meta->address_count - 2) {
		if (ptr_uint(meta->address_size, dives, i) == 0xff0000) {
			// Skip past bad dives
			i++;
			continue;
		}

		// Find next good dive
		int n = i + 1;
		while ((dive_end = ptr_uint(meta->address_size, dives, n)) == 0xff0000 && n < meta->address_count - 2)
			n++;

		if (n == meta->address_count - 2)
			dive_end = cleartext_size;
		// check out of range
		if (dive_end < ptr_uint(meta->address_size, dives, i) || dive_end > cleartext_size)
			break;

		dive_size = dive_end - ptr_uint(meta->address_size, dives, i);
		dive = malloc(dive_size);
		if (!dive) {
			fputs("Unable to allocate dive space.\n", stderr);
			return 3;
		}

		memcpy(dive, cleartext + ptr_uint(meta->address_size, dives, i), dive_size);
		if (callback)
			result = (callback)(meta, dive, dive_size, i + 1, 0, userdata);

		free(dive);

		if (result)
			return(result);
		i++;
	}

	// Now process the trailing inter-dive events
	if (ptr_uint(meta->address_size, dives, meta->address_count - 2) - 1 > ptr_uint(meta->address_size, dives, i) && ptr_uint(meta->address_size, dives, meta->address_count - 2) - 1 <= cleartext_size) {
		dive_size = ptr_uint(meta->address_size, dives, meta->address_count - 2) - 1 - ptr_uint(meta->address_size, dives, i);

		dive = malloc(dive_size);
		if (!dive) {
			fputs("Unable to allocate dive.buffer space.\n", stderr);
			return 3;
		}

		memcpy(dive, cleartext + ptr_uint(meta->address_size, dives, i), dive_size);

		if (callback)
			result = (callback)(meta, dive, dive_size, i+1, 1, userdata);

		free(dive);

		if (result)
			 return(result);
	}
	return 0;
}


/*
 * ANA file structure
 *
 * 0x000000 - 0x02ffff       : Pointers into file for each dive.
 * 0x030000 - 0x030000       : Key length (mod)
 * 0x030001 - 0x030001 + mod : Key
 * 0x030001 + 0x0308ee + mod : 0x0308ee + mod
 * 0x0308ee + mod - ????     : Dive data
 *
 */

int cochran_can_decode_ana_file(const unsigned char *ciphertext, unsigned int ciphertext_size, unsigned char *cleartext) {
	unsigned int o = cochran_can_get_header_offset(FILE_ANA);
	int address_size = 3;
	const unsigned char *key = ciphertext + o + 1;
	const unsigned char mod = ciphertext[o] + 1;
	unsigned int hend = 0x308ef + mod;

	// Copy the non-encrypted header (dive pointers, mod and key)
	memcpy(cleartext, ciphertext, o + 1 + mod);

	decode(o + 1 + mod, o + 1 + mod + 0x482, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 1 + mod + 0x482, o + hend, key, 0, mod, ciphertext, hend, cleartext);

	cochran_can_meta_t meta;
	if (cochran_can_meta(&meta, FILE_ANA, ciphertext, ciphertext_size))
		return 1;

	int rc;
	if ((rc = cochran_can_foreach_dive(&meta, ciphertext, ciphertext_size, cochran_can_decode_dive, (void *) cleartext))) {
		fprintf(stderr, "Error %d decoding dives.\n", rc);
		return rc;
	}

	// Erase the key, since we are decoded
	for (unsigned int x = o + 0x01 ; x < o + mod; x++)
		cleartext[x] = 0;

	return 0;
}


int cochran_can_decode_wan_file(const unsigned char *ciphertext, unsigned int ciphertext_size, unsigned char *cleartext) {
	unsigned int offset = cochran_can_get_header_offset(FILE_WAN);
	const unsigned char *key = ciphertext + offset + 0x01;
	const unsigned char mod = key[0x100] + 1;

	int address_size = 0;
	address_size = cochran_can_get_address_size(FILE_WAN, ciphertext, ciphertext_size);

	// Analyst v3 records a 0xff0000 pointer for dives that weren't
	// downloaded from the DC.
	unsigned int i = 0;
	while (ptr_uint(address_size, ciphertext, i) == 0xff0000) i++;

	// Header size
	unsigned int hend = 0;
	hend = ptr_uint(address_size, ciphertext, i);

	// The base offset we'll use for accessing the header
	unsigned int o = offset + 0x102;

	// Copy the non-encrypted header (dive pointers, key, and mod)
	memcpy(cleartext, ciphertext, o);

	/*
 	* Decrypt the header information
	* It was a weird cipher pattern that may tell us the boundaries of
	* structures.
	*
	* header size is the space between 0x30102 and the first dive.
	*
	* Encrypted blocks
	* 0x0000 - 0x000c
	* 0x000c - 0x048e
	* 0x048e - end
	*/

	decode(o + 0x0000, o + 0x000c, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x000c, o + 0x048e, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x048e, hend,       key, 0, mod, ciphertext, hend, cleartext);

	cochran_can_meta_t meta;
	if (cochran_can_meta(&meta, FILE_WAN, cleartext, ciphertext_size))
		return 1;

	int rc;
	if ((rc = cochran_can_foreach_dive(&meta, ciphertext, ciphertext_size, cochran_can_decode_dive, (void *) cleartext))) {
		fprintf(stderr, "Error %d decoding dives.\n", rc);
		return rc;
	}

	// Erase the key, since we are decoded
	for (unsigned int x = offset + 0x01 ; x < offset + 0x101; x++)
		cleartext[x] = 0;

	return 0;
}


int cochran_can_decode_can_file(const unsigned char *ciphertext, unsigned int ciphertext_size, unsigned char *cleartext) {
	unsigned int offset = cochran_can_get_header_offset(FILE_CAN);
	const unsigned char *key = ciphertext + offset + 0x01;
	const unsigned char mod = key[0x100] + 1;

	int address_size = 0;
	address_size = cochran_can_get_address_size(FILE_CAN, ciphertext, ciphertext_size);

	unsigned int i = 0;
	while (ptr_uint(address_size, ciphertext, i) == 0xff0000) i++;

	// Header size
	unsigned int hend = 0;
	hend = ptr_uint(address_size, ciphertext, i);

	// The base offset we'll use for accessing the header
	unsigned int o = offset + 0x102;

	// Copy the non-encrypted header (dive pointers, key, and mod)
	memcpy(cleartext, ciphertext, o);

	/*
	* Decrypt the header information
	* It was a weird cipher pattern that may tell us the boundaries of
	* structures.
	*
	* header size is the space between 0x40102 and the first dive.
	*
	* Encrypted blocks
	* 0x0000 - 0x000c
	* 0x000c - 0x0a12
	* 0x0a12 - 0x1a12
	* 0x1a12 - 0x2a12
	* 0x2a12 - 0x3a12
	* 0x3a12 - 0x5312
	* 0x5312 - end
	*/

	decode(o + 0x0000, o + 0x000c, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x000c, o + 0x0a12, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x0a12, o + 0x1a12, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x1a12, o + 0x2a12, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x2a12, o + 0x3a12, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x3a12, o + 0x5312, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x5312, o + 0x5d00, key, 0, mod, ciphertext, hend, cleartext);
	decode(o + 0x5d00, hend,       key, 0, mod, ciphertext, hend, cleartext);

	cochran_can_meta_t meta;
	if (cochran_can_meta(&meta, FILE_CAN, cleartext, ciphertext_size))
		return 1;

	int rc;
	if ((rc = cochran_can_foreach_dive(&meta, ciphertext, ciphertext_size, cochran_can_decode_dive, (void *) cleartext))) {
		fprintf(stderr, "Error %d decoding dives.\n", rc);
		return rc;
	}

	// Erase the key, since we are decoded
	for (unsigned int x = offset + 0x01 ; x < offset + 0x101; x++)
		cleartext[x] = 0;

	return 0;
}


int cochran_can_decode_file(cochran_file_type_t file_type, const unsigned char *ciphertext, unsigned int ciphertext_size, unsigned char *cleartext) {


	switch (file_type) {
	case FILE_ANA:
		return cochran_can_decode_ana_file(ciphertext, ciphertext_size, cleartext);
		break;
	case FILE_WAN:
		return cochran_can_decode_wan_file(ciphertext, ciphertext_size, cleartext);
		break;
	case FILE_CAN:
		return cochran_can_decode_can_file(ciphertext, ciphertext_size, cleartext);
		break;
	}

	return 1;
}
