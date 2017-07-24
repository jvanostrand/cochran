#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "cochran.h"
#include "cochran_log.h"

enum file_type {
	FILE_WAN,
	FILE_CAN,
};

struct config {
	enum file_type file_type;
	unsigned char file_format;
	unsigned int offset;		// Offset to after the list of dive pointers
	unsigned int address_size; 	// 3 or 4 depending on the byte at .offset
	unsigned int address_count;
	enum cochran_family_t family;
	unsigned int logbook_size;
	unsigned int sample_size;
	int decode_address[10];
	int decode_key_offset[10];
	unsigned int log_offset;
	unsigned int profile_offset;
} config;

struct memblock {
	unsigned char * buffer;
	unsigned int alloc, size;
};

typedef int (*cf_callback_t) (struct memblock dive, unsigned int dive_num, int last_dive, void *userdata);

#define ptr_uint(t,p,i)		( ((t) == 3 ? array_uint24_le((p) + (i) * 3) : array_uint32_le((p) + (i) * 4) ) )


#define TRUE 1
#define FALSE 0
int debug = FALSE;

/*
 * The Cochran file format is designed to be annoying to read. It's roughly:
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
static void decode(const unsigned int start, const unsigned int end,
				   const unsigned char *key, unsigned offset, const unsigned char mod,
				   const unsigned char *cipher, const unsigned int size, unsigned char *cleartext)
{
	for (unsigned int i = start; i < end && i < size; i++) {
		cleartext[i] = key[offset] + cipher[i];
		offset++;
		offset %= mod;
	}
}

#define hexchar(n) ("0123456789abcdef"[(n) & 15])

static int show_line(unsigned offset, const unsigned char *data,
					unsigned size, int show_empty)
{
	unsigned char bits;
	unsigned int i;
	int off;
	char buffer[120];

	if (size > 16)
		size = 16;

	bits = 0;
	memset(buffer, ' ', sizeof(buffer));
	off = sprintf(buffer, "%06x ", offset);
	for (i = 0; i < size; i++) {
		char *hex = buffer + off + 3 * i;
		char *asc = buffer + off + 50 + i;
		unsigned char byte = data[i];

		hex[0] = hexchar(byte >> 4);
		hex[1] = hexchar(byte);
		bits |= byte;
		if (byte < 32 || byte > 126)
			byte = '.';
		asc[0] = byte;
		asc[1] = 0;
	}

	if (bits) {
		puts(buffer);
		return 1;
	}
	if (show_empty)
		puts("...");
	return 0;
}

static void cochran_debug_write(const unsigned char *data, unsigned size)
{
	unsigned int show = 1,  i;

	for (i = 0; i < size; i += 16)
		show = show_line(i, data + i, size - i, show);
}


// Get useful values from header
static void parse_header(const struct memblock *clearfile)
{
	const unsigned char *header = clearfile->buffer + config.offset + 0x102;
	int header_size = ptr_uint(config.address_size, clearfile->buffer, 0) - (config.offset + 0x102);

	// Detect computer log version
	switch (header[0x31])
	{
// TODO: Nem2a format
// TODO: GemPNox format
	case '0':
	case '1':	// Cochran Commander, version I log format
		config.logbook_size = 90;
		config.family = FAMILY_COMMANDER_I;
		config.sample_size = 1;
		break;
	case '2':	// Cochran Commander, version II log format
		config.logbook_size = 256;
		if (header[0x30] == 0x10) {
			config.family = FAMILY_GEMINI;
			config.sample_size = 2;	// Gemini with tank PSI samples
		} else if (header[0x32] == '1') {
			config.family = FAMILY_COMMANDER_II;
			config.sample_size = 2;	// Commander
		} else {
			config.family = FAMILY_COMMANDER_III;
			config.sample_size = 2;	// Commander
		}
		break;
	case '3':	// Cochran EMC, version III log format
		config.family = FAMILY_EMC;
		config.logbook_size = 512;
		config.sample_size = 3;
		break;
	default:
		fprintf (stderr, "Unknown log format %02x %02x\n", header[0x30], header[0x31]);
		//exit(1);
		break;
	}

	//0x5dc - 0x64a : log          0x5dc - 0x6dc : log       0x5dc - 0x6dc : log
	// 0x64a - 0x659 : ????         0x6dc -                   0x6dc -
	// 0x659 - 0x6b9 : ????               - 0x7d5 : ???             - 0x6f1 : ???
	// 0x6b9 - end   : Samples      0x7d5 - end   : samples   0x6f1 - end   : samples
	//
	// Determine addressing format
	switch(config.file_format) {
	case 0x43:
		config.decode_address[0] = 0;
		config.decode_address[1] = 0x5dc;
		config.decode_address[2] = 0x64a;
		config.decode_address[3] = 0x659;
		config.decode_address[4] = 0x6b9;
		config.decode_address[5] = -1;
		config.decode_key_offset[0] = -1; // don't decode first section;
		for (int i = 1; i < 10; i++)
			config.decode_key_offset[i] = 0;
		config.log_offset = 0x5f1;
		config.profile_offset = (config.logbook_size == 90 ? 0x6b9 : 0x6f1);
		break;
	case 0x4f:
		config.decode_address[0] = 0;
		config.decode_address[1] = 0x5dc;
		if (header[0x32] == '0') {
			// GemPNox
			config.decode_address[2] = 0x6f1;	// 0x6f1: GemPNox, 0x6b9: others
		} else {
			config.decode_address[2] = 0x6b9;	// 0x6f1: GemPNox, 0x6b9: others
		}
		config.decode_address[3] = -1;
		config.decode_key_offset[0] = -1; // don't decode first section;
		for (int i = 1; i < 10; i++)
			config.decode_key_offset[i] = 0;
		config.log_offset = 0x5f1;
		config.profile_offset = (config.logbook_size == 90 ? 0x6b9 : 0x6f1);
		break;
	case 0x45:
		config.decode_address[0] = 0;
		config.decode_address[1] = 0x5dc;
		config.decode_address[2] = 0x6f1;// Cmd1Mix 0x6dc
		//config.decode_address[3] = 0x7d5;
		config.decode_address[3] = -1;
		config.decode_key_offset[0] = -1; // don't decode first section;
		for (int i = 1; i < 10; i++)
			config.decode_key_offset[i] = 0;
		config.log_offset = 0x5f1;
		config.profile_offset = (config.logbook_size == 90 ? 0x6b9 : 0x6f1);
		break;
	case 0x46:
		config.decode_address[0] = 0;
		config.decode_address[1] = 0x0fff;
		config.decode_address[2] = 0x1fff;
		config.decode_address[3] = 0x2fff;
		config.decode_address[4] = 0x48ff;
		config.decode_address[5] = 0x4914 + config.logbook_size;
		config.decode_address[6] = -1;
		config.decode_key_offset[0] = 1;
		for (int i = 1; i < 10; i++)
			config.decode_key_offset[i] = 0;
		config.log_offset = 0x4914;
		config.profile_offset = config.log_offset + config.logbook_size;
		break;
	default:
		fprintf(stderr, "Uknown file format %02x.\n", clearfile->buffer[config.offset]);
		exit(1);
		break;
	}

	config.address_count = config.offset / config.address_size;

	if (debug) {
		fputs("Header\n======\n\n", stderr);
		cochran_debug_write(header, header_size);
	}
}

/*
* Bytes expected after a pre-dive event code
*/
static int cochran_predive_event_bytes (unsigned char code)
{
	int x = 0;

	int cmdr_event_bytes[15][2] = {	{0x00, 16}, {0x01, 20}, {0x02, 17},
									{0x03, 16}, {0x06, 18}, {0x07, 18},
									{0x08, 18}, {0x09, 18}, {0x0a, 18},
									{0x0b, 18}, {0x0c, 18}, {0x0d, 18},
                                	{0x0e, 18}, {0x10, 20},
									{  -1,  0} };
	int emc_event_bytes[15][2] = {	{0x00, 18}, {0x01, 22}, {0x02, 19},
									{0x03, 18}, {0x06, 20}, {0x07, 20},
									{0x0a, 20}, {0x0b, 20}, {0x0f, 18},
									{0x10, 20},
									{  -1,  0} };

	switch (config.family)
	{
	case FAMILY_COMMANDER_I:
	case FAMILY_COMMANDER_II:
		// doesn't have inter-dive events;
		break;
	case FAMILY_GEMINI:
	case FAMILY_COMMANDER_III:
		while (cmdr_event_bytes[x][0] != code && cmdr_event_bytes[x][0] != -1)
			x++;

		return cmdr_event_bytes[x][1];
		break;
	case FAMILY_EMC:
		while (emc_event_bytes[x][0] != code && emc_event_bytes[x][0] != -1)
			x++;

		return emc_event_bytes[x][1];
		break;
	}
	return(0);
}


void parse_log(const unsigned char *log_buf, cochran_log_t *log) {
	switch (config.family) {
// TODO: Nemo2a format
// TODO: GemPNox format
	case FAMILY_COMMANDER_I:
		cochran_log_commander_I_parse(log_buf, log);
		break;
	case FAMILY_GEMINI:
	case FAMILY_COMMANDER_III:
		cochran_log_commander_III_parse(log_buf, log);
		break;
	case FAMILY_EMC:
		cochran_log_emc_parse(log_buf, log);
		break;
	default:
		fputs("Invalid conig.type", stderr);
		break;
	}
}


// Print dive heading
static int print_dive_summary(struct memblock dive, unsigned int dive_num, int last_dive, void *userdata)
{
	const unsigned char *log_buf =  dive.buffer + config.log_offset;

	if (last_dive)
		return(0);

	cochran_log_t log;
	parse_log(log_buf, &log);

	cochran_log_print_short(&log, dive_num);

	return(0);
}


// Callback function that tracks lines to reproduce heading
static int print_dive_summary_cb( struct memblock dive,
			unsigned int dive_num, int last_dive, void *userdata)
{
	static int count = 0;

	if (count == 0 && !last_dive) {
		cochran_log_print_short_header(1);
	}

	count++;
	if (count > 25) count = 0;

	return print_dive_summary(dive, dive_num, last_dive, userdata);
}


/*
* Parse sample data, extract events and build a dive
*/

static int print_dive_samples_cb(struct memblock dive,
		unsigned dive_num, int last_dive, void *userdata)
{
	unsigned int offset = 0, sample_cnt = 0;
	double depth = 0, temp = 0, depth_sample = 0, psi = 0, sgc_rate = 0;
	int ascent_rate = 0;
	unsigned int ndl = 0;
	unsigned int deco_obligation = 0, deco_ceiling = 0, deco_time = 0;
	char *event_desc;

	unsigned char *log_buf = dive.buffer + config.log_offset;
	cochran_log_t log;

	parse_log(log_buf, &log);

	unsigned char *samples;

	unsigned int sample_size;
	unsigned char profile_interval = log.profile_interval; 	// seconds between samples

	if (last_dive) {
		// last dive section contains only interdive events, no log
		samples = dive.buffer;
		sample_size = dive.size;
	} else {
		samples = dive.buffer + config.profile_offset;
		sample_size = dive.size - config.profile_offset;
	}

	// Skip past pre-dive events
	unsigned int x = 0;
	if (samples[x] != 0x40) {
		puts("\n");
		unsigned int c, y;
		while ( (samples[x] & 0x80) == 0 && samples[x] != 0x40 && x < sample_size) {
			c = cochran_predive_event_bytes(samples[x]) + 1;
			printf ("      Predive event: ");
			for (y = 0; y < c; y++) printf ("%02x ", samples[x + y]);
			putchar('\n');
			x += c;
		}
	}

	if (last_dive) return(0);

	puts("\n");

	// Invalid dive summary
	if (dive.size < config.profile_offset)
		return(0);

	cochran_log_print_short_header(1);
	print_dive_summary(dive, dive_num, last_dive, userdata);

	unsigned int sample_start_offset = 0, sample_end_offset = 0;
	sample_start_offset = log.profile_begin;
	sample_end_offset = log.profile_end;
	depth = log.depth_start;
	psi = log.tank_pressure_start;
	sgc_rate = log.gas_consumption_start;
	temp = log.temp_start;

	// Use the log information to determine actual profile sample size
	// Otherwise we will get surface time at end of dive.
	if (sample_start_offset < sample_end_offset && sample_end_offset != 0xffffffff)
		sample_size = x + sample_end_offset - sample_start_offset;

	// Now process samples
	offset = x;
	while (offset < sample_size) {
		const unsigned char *s;
		s = samples + offset;

		// Check for special sample (event or a temp change for early Commanders
		if (s[0] & 0x80 && s[0] & 0x60) {
			switch (s[0]) {
			case 0xC5:	// Deco obligation begins
				deco_obligation = 1;
				event_desc = "Deco obligation begins";
				break;
			case 0xDB:	// Deco obligation ends
				deco_obligation = 0;
				event_desc = "Deco obligation ends";
				break;
			case 0xAD:
				deco_ceiling -= 10; // ft
				event_desc = "Raise ceiling 10 ft";
				if (config.sample_size > 1 && offset + 3 < sample_size) {
					deco_time = (array_uint32_le(s + 3) + 1) * 60;
					offset += 4;	// skip 4 event bytes
				} else {
					deco_time = 60;
				}
				break;
			case 0xAB:
				deco_ceiling += 10;	// ft
				event_desc = "Lower ceiling 10 ft";
				if (config.sample_size > 1 && offset + 3 < sample_size) {
					deco_time = (array_uint32_le(s + 3) + 1) * 60;
					offset += 4;	// skip 4 event bytes
				} else {
					deco_time = 60;
				}
				break;

			case 0xA8:	// Entered Post Dive interval mode (surfaced)
				event_desc = "Surface";
				break;
			case 0xA9:	// Exited PDI mode (re-submierged)
				event_desc = "Re-submerge";
				break;
			case 0xBD:
				event_desc = "Switched to normal PO2 setting";
				break;
			case 0xC0:	// Switched to FO2 21% mode (generally upon surface)
				event_desc = "Switch to surface air";
				break;
			case 0xC1:
				event_desc = "Ascent rate alarm";
				break;
			case 0xC2:
				event_desc = "Low battery warning";
				break;
			case 0xC3:
				event_desc = "CNS warning";
				break;
			case 0xC4:
				event_desc = "Depth alarm begin";
				break;
			case 0xC8:
				event_desc = "PPO2 alarm begin";
				break;
			case 0xCC:
				event_desc = "Low cylinder 1 pressure";
				break;
			case 0xCD:
				event_desc = "Switch to deco blend setting";
				break;
			case 0xCE:
				event_desc = "NDL alarm begin";
				break;
			case 0xD0:
				event_desc = "Breathing rate alarm begin";
				break;
			case 0xD3:
				event_desc = "Low gas 1 flow rate alarm begin";
				break;
			case 0xD6:
				event_desc = "Ceiling alarm begin";
				break;
			case 0xD8:
				event_desc = "End decompression mode";
				break;
			case 0xE1:
				event_desc = "Ascent alarm end";
				break;
			case 0xE2:
				event_desc = "Low transmitter battery alarm";
				break;
			case 0xE3:
				event_desc = "Switch to FO2 mode";
				break;
			case 0xE5:
				event_desc = "Switched to PO2 mode";
				break;
			case 0xE8:
				event_desc = "PO2 too low alarm";
				break;
			case 0xEE:
				event_desc = "NDL alarm end";
				break;
			case 0xEF:
				event_desc = "Switch to blend 2";
				break;
			case 0xF0:
				event_desc = "Breathing rate alarm end";
				break;
			case 0xF3:	// Switch to blend 1 (often at dive start)
				event_desc = "Switch to blend 1";
				break;
			case 0xF6:
				event_desc = "Ceiling alarm end";
				break;
			default:
				event_desc = "";
				break;
			}

			printf("      Event %02x: %s\n", s[0], event_desc);

			offset++;
			continue;
		}

		if (config.sample_size == 1) {
			// This is an early Commander
			if (s[0] & 0x80) {
				// Temp sample
				int temp_change;
				if (s[0] & 0x40)
					temp_change = (s[0] & 0x3f) / 2;
				else
					temp_change = -(s[0] & 0x3f) / 2;
				temp += temp_change;
			} else {
				// Depth sample
				int depth_change;
				if (s[0] & 0x40)
					depth_change = -(s[0] & 0x3f) / 2;
				else
					depth_change = (s[0] & 0x3f) / 2;
				depth += depth_change;
			}
			printf("Hex: %02x Depth: %6.2f %5.1f\n", s[0], depth, temp);
			offset++;
			continue;
		}

		// Depth is in every sample
		depth_sample = (float) (s[0] & 0x3F) / 4 * (s[0] & 0x40 ? -1 : 1);
		depth += depth_sample;

		printf("      ");

		if (debug) {
			switch (config.family)
			{
			case FAMILY_GEMINI:
				switch (sample_cnt % 4)
				{
				case 0:
					printf("Hex: %02x %02x          ", s[0], s[1]);
					break;
				case 1:
					printf("Hex: %02x    %02x       ", s[0], s[1]);
					break;
				case 2:
					printf("Hex: %02x       %02x    ", s[0], s[1]);
					break;
				case 3:
					printf("Hex: %02x          %02x ", s[0], s[1]);
					break;
				}
				break;
			case FAMILY_COMMANDER_II:
				switch (sample_cnt % 2)
				{
				case 0:
					printf("Hex: %02x %02x    ", s[0], s[1]);
					break;
				case 1:
					printf("Hex: %02x    %02x ", s[0], s[1]);
					break;
				}
				break;
			case FAMILY_EMC:
				switch (sample_cnt % 2)
				{
				case 0:
					printf("Hex: %02x %02x    %02x ", s[0], s[1], s[2]);
					break;
				case 1:
					printf("Hex: %02x    %02x %02x ", s[0], s[1], s[2]);
					break;
				}
				break;
			}
		}

		printf ("%02d:%02d:%02d Depth: %-5.2f, ", (sample_cnt * profile_interval) / 3660,
							((sample_cnt * profile_interval) % 3660) / 60, (sample_cnt * profile_interval) % 60, depth);

		if (config.family == FAMILY_COMMANDER_III) {
			switch (sample_cnt % 2)
			{
			case 0:	// Ascent rate
				ascent_rate = (s[1] & 0x7f) * (s[1] & 0x80 ? 1: -1);
				printf("Ascent: %3d ft/min", ascent_rate);
				break;
			case 1:	// Temperature
				temp = s[1] / 2 + 20;
				printf ("  Temp: %2.1f    ", temp);
				break;
			}
		} else if (config.family == FAMILY_GEMINI) {
			// Gemini with tank pressure and SAC rate.
			switch (sample_cnt % 4)
			{
			case 0:	// Ascent rate
				ascent_rate = (s[1] & 0x7f) * (s[1] & 0x80 ? 1 : -1);
				printf("Ascent: %3d ft/min", ascent_rate);
				break;
			case 2:	// PSI change
				psi -= (float) (s[1] & 0x7f) * (s[1] & 0x80 ? 1 : -1) / 4;
				printf("PSI   : %4.1f    ", psi);
				break;
			case 1:	// SGC rate
				sgc_rate -= (float) (s[1] & 0x7f) * (s[1] & 0x80 ? 1 : -1) / 2;
				printf("SGC rt: %2.1f    ", sgc_rate);
				break;
			case 3:	// Temperature
				temp = (float) s[1] / 2 + 20;
				printf ("  Temp: %2.1f    ", temp);
				break;
			}
		} else if (config.family == FAMILY_EMC) {
			switch (sample_cnt % 2)
			{
			case 0:	// Ascent rate
				ascent_rate = (s[1] & 0x7f) * (s[1] & 0x80 ? 1: -1);
				printf("Ascent: %3d ft/min", ascent_rate);
				break;
			case 1:	// Temperature
				temp = (float) s[1] / 2 + 20;
				printf ("  Temp: %2.1f    ", temp);
				break;
			}
			// Get NDL and deco information
			switch (sample_cnt % 24)
			{
			case 20:
				if (deco_obligation) {
					// Deepest stop time
					deco_time = (s[2] + s[5] * 256 + 1) * 60; // seconds
				} else {
					// NDL
					ndl = (s[2] + s[5] * 256 + 1) * 60; // seconds
					printf (",  ndl: %dh %0dm", ndl / 3660, (ndl % 3600) / 60);
				}
				break;
			case 22:
				if (deco_obligation) {
					// Total stop time
					deco_time = (s[2] + s[5] * 256 + 1) * 60; // seconds
					printf (", deco: %dm, %02ds",
						deco_time / 3660, (deco_time % 3660) / 60);
				}
				break;
			}
		}

		offset += config.sample_size;
		putchar('\n');
		sample_cnt++;
	}
	return(0);
}


static int decode_dive(struct memblock dive, unsigned int dive_num, int last_dive, void *userdata) {
	struct memblock *clearfile = (struct memblock *) userdata;
	const unsigned char *key = clearfile->buffer + config.offset + 0x01;
	const unsigned char mod = key[0x100] + 1;
	const unsigned char *dives = (unsigned char *) clearfile->buffer;

	const int dive_offset = ptr_uint(config.address_size, dives, dive_num - 1);
	int *addr = config.decode_address;
	int *koff = config.decode_key_offset;

// Print cipher
/*
int from = 0x6f1  + 0, to = from + 256; //dive.size;
printf("cpher test\n\n");
	for (int c = from; c < to; c+= 32) {
		int end = 32;
		if (c + end > to) end = to - c;
		for (int cc = 0; cc < end; cc++)
			printf("%02x ", dive.buffer[c + cc]);
		printf("\n");
	}
for (int o = from; o < to; o++) {
			decode(o, to,
				key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
	printf("\n\noffset = %06x (mod %02x)\n\n", o, mod);
	for (int c = o; c < to; c+= 32) {
		int end = 32;
		if (c + end > to) end = to - c;
		for (int cc = 0; cc < end; cc++)
			printf("%02x ", clearfile->buffer[dive_offset + c + cc]);
		printf("\n");
	}
}
*/
	if (!last_dive) {
		while (*addr != -1) {
			int end_addr = *(addr + 1);
			if (end_addr == -1) end_addr = dive.size;

			if (*koff == -1)
				memcpy(clearfile->buffer + dive_offset, dive.buffer, end_addr - *addr);
			else
				decode(*addr, end_addr, key, *koff, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);

			addr++;
			koff++;
		}
	} else {
		decode(0, dive.size, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
	}

	return 0;
}



static int wan_decode_dive(struct memblock dive, unsigned int dive_num, int last_dive, void *userdata) {
	struct memblock *clearfile = (struct memblock *) userdata;
	const unsigned char *key = clearfile->buffer + config.offset + 0x01;
	const unsigned char mod = key[0x100] + 1;
	const unsigned char *dives = (unsigned char *) clearfile->buffer;
	const int dive_offset = ptr_uint(config.address_size, dives, dive_num - 1);

	if (last_dive == 0) {
		// WAN
		// 0x43                         0x45                      0x4f
		//
		// 0     - 0x5dc : zeros		0     - 0x5dc : zeros     0     - 0x5dc : zeros
		// 0x5dc - 0x64a : log          0x5dc - 0x6dc : log       0x5dc - 0x6dc : log
		// 0x64a - 0x659 : ????         0x6dc -                   0x6dc -
		// 0x659 - 0x6b9 : ????               - 0x7d5 : ???             - 0x6f1 : ???
		// 0x6b9 - end   : Samples		0x7d5 - end   : samples   0x6f1 - end   : samples


		// Decode (copy) header and log section
		memcpy(clearfile->buffer + dive_offset, dive.buffer, 0x5dc);
		decode(0x5dc, 0x64a, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);

		int sample_size = dive.size - 0x5dc - config.logbook_size;
		if (sample_size > 0) {

			switch (config.file_format) {
			case 0x43:		// Very old
				decode(0x64a, 0x659, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
				// this next one is likely wrong
				decode(0x659, 0x6b9, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
				decode(0x6b9, dive.size, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
				break;
			case 0x45:
				decode(0x6dc, 0x7d5, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
				// this next one is likely wrong
				decode(0x7d5, dive.size, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
				break;
			case 0x4f:
				decode(0x6dc, 0x6f1, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
				// this next one is likely wrong
				decode(0x6f1, dive.size, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
				break;
			}

// Print cipher
/*
int from = 0x6dc + 128, to = from + 96; //dive.size;
printf("cpher test\n\n");
	for (int c = from; c < to; c+= 32) {
		int end = 32;
		if (c + end > to) end = to - c;
		for (int cc = 0; cc < end; cc++)
			printf("%02x ", dive.buffer[c + cc]);
		printf("\n");
	}
for (int o = from; o < to; o++) {
			decode(o, to,
				key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
	printf("\n\noffset = %06x (mod %02x)\n\n", o, mod);
	for (int c = o; c < to; c+= 32) {
		int end = 32;
		if (c + end > to) end = to - c;
		for (int cc = 0; cc < end; cc++)
			printf("%02x ", clearfile->buffer[dive_offset + c + cc]);
		printf("\n");
	}
}
*/
		}
	}
	return(0);
}

static int can_decode_dive(struct memblock dive, unsigned int dive_num, int last_dive, void *userdata) {
	struct memblock *clearfile = (struct memblock *) userdata;
	const unsigned char *key = clearfile->buffer + config.offset + 0x01;
	const unsigned char mod = key[0x100] + 1;
	const unsigned char *dives = (unsigned char *) clearfile->buffer;
	const int dive_offset = ptr_uint(config.address_size, dives, dive_num - 1);

	if (last_dive == 0) {
		/*
	 	* The scrambling has odd boundaries. I think the boundaries
	 	* match some data structure size, but I don't know. They were
	 	* discovered the same way we dynamically discover the decode
	 	* size: automatically looking for least random output.
	 	*
	 	* The boundaries are also this confused "off-by-one" thing,
	 	* the same way the file size is off by one. It's as if the
	 	* cochran software forgot to write one byte at the beginning.
	 	*/
		decode(0,      0x0fff, key, 1, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		decode(0x0fff, 0x1fff, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		decode(0x1fff, 0x2fff, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		decode(0x2fff, 0x48ff, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);


		/*
	 	* This is not all the descrambling you need - the above are just
	 	* what appears to be the fixed-size blocks. The rest is also
	 	* scrambled, but there seems to be size differences in the data,
	 	* so this just descrambles part of it:
	 	*/
		// Decode log entry (512 bytes + random prefix)
		decode(0x48ff, 0x4914 + config.logbook_size,
				key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);

		int sample_size = dive.size - 0x4914 - config.logbook_size;
		if (sample_size > 0) {
			// Decode sample data
			decode(0x4914 + config.logbook_size, dive.size, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		}
	} else {
		decode(0, dive.size, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
	}

	return(0);
}


static int foreach_dive(const struct memblock clearfile, cf_callback_t callback, void *userdata) {
	const unsigned char *dives = (unsigned char *) clearfile.buffer;

	if (clearfile.size < config.offset + 0x102)
		return 0;

	int result = 0;
	unsigned int dive_end = 0;
	struct memblock dive;
	unsigned int i = 0;
	while (ptr_uint(config.address_size, dives, i) && i < config.address_count - 2) {
		dive_end = ptr_uint(config.address_size, dives, i + 1);

		// check out of range
		if (dive_end < ptr_uint(config.address_size, dives, i) || dive_end > clearfile.size)
			break;

		dive.size = dive_end - ptr_uint(config.address_size, dives, i);
		dive.buffer = malloc(dive.size);
		if (!dive.buffer) {
			fputs("Unable to allocate dive.buffer space.\n", stderr);
			exit(1);
		}

		memcpy(dive.buffer, clearfile.buffer + ptr_uint(config.address_size, dives, i), dive.size);

		if (callback)
			result = (callback)(dive, i+1, 0, userdata);

		free(dive.buffer);

		if (result)
			return(result);
		i++;
	}

	// Now process the trailing inter-dive events
	if (ptr_uint(config.address_size, dives, config.address_count - 2) - 1 > ptr_uint(config.address_size, dives, i) && ptr_uint(config.address_size, dives, config.address_count - 2) - 1 <= clearfile.size) {
		dive.size = ptr_uint(config.address_size, dives, config.address_count - 2) - 1 - ptr_uint(config.address_size, dives, i);

		dive.buffer = malloc(dive.size);
		if (!dive.buffer) {
			fputs("Unable to allocate dive.buffer space.\n", stderr);
			exit(1);
		}

		memcpy(dive.buffer, clearfile.buffer + ptr_uint(config.address_size, dives, i), dive.size);

		if (callback)
			result = (callback)(dive, i+1, 1, userdata);

		free(dive.buffer);

		if (result) return(result);
	}
	return(0);
}


struct memblock read_can(int fp) {
	struct memblock canfile;
	int bytes_read = 0;

	canfile.size = 0;
	canfile.alloc = 65536;
	canfile.buffer = malloc(canfile.alloc);

	if (!canfile.buffer) {
		fprintf(stderr, "Unable to allocate canfile buffer\n");
		exit(1);
	}

	while ((bytes_read = read(fp, canfile.buffer + canfile.size, 512)) > 0)
	{
		canfile.size += bytes_read;
		if (canfile.alloc - canfile.size < 512) {
			canfile.alloc += 16384;
			canfile.buffer = realloc(canfile.buffer, canfile.alloc);
		}
	}

	if (fp)
		close(fp);

	return(canfile);
}

void decode_file(struct memblock canfile, struct memblock *clearfile) {
	const unsigned char *key = canfile.buffer + config.offset + 0x01;
	const unsigned char mod = key[0x100] + 1;

	// The base offset we'll use for accessing the header
	unsigned int o = config.offset + 0x102;
	// Header size
	unsigned int hend = ptr_uint(config.address_size, canfile.buffer, 0);

	// Copy the non-encrypted header (dive pointers, key, and mod)
	memcpy(clearfile->buffer, canfile.buffer, o);
	clearfile->size = o;

	/* Decrypt the header information
	*  It was a weird cipher pattern that may tell us the boundaries of
	*  structures.
	*/
	/*
	* header size is the space between 0x40102 (0x30102 for WAN) and the first dive.
	* CAN					WAN
	* 0x0000 - 0x000c		0x0000 - 0x000c
	* 0x000c - 0x0a12		0x000c - 0x048e
	* 0x0a12 - 0x1a12		0x048e - 0x1a12
	* 0x1a12 - 0x2a12		0x1a12 - 0x2a12
	* 0x2a12 - 0x3a12		0x2a12 - 0x3a12
	* 0x3a12 - 0x5312		0x3a12 - 0x5312
	* 0x5312 - end			0x5312 - end
	*/

	if (config.file_type == FILE_WAN) {
		decode(o + 0x0000, o + 0x000c, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x000c, o + 0x048e, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x048e, hend,       key, 0, mod, canfile.buffer, hend, clearfile->buffer);
	} else {
		decode(o + 0x0000, o + 0x000c, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x000c, o + 0x0a12, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x0a12, o + 0x1a12, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x1a12, o + 0x2a12, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x2a12, o + 0x3a12, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x3a12, o + 0x5312, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x5312, o + 0x5d00, key, 0, mod, canfile.buffer, hend, clearfile->buffer);
		decode(o + 0x5d00, hend,       key, 0, mod, canfile.buffer, hend, clearfile->buffer);
	}

	parse_header(clearfile);

	foreach_dive(canfile, decode_dive, (void *) clearfile);

	// Erase the key, since we are decoded
	for (int x = config.offset + 0x01 ; x < config.offset + 0x101; x++)
		clearfile->buffer[x] = 0;

	clearfile->size = canfile.size;
}


void usage(const char *name) {
	fprintf(stderr, "Usage: %s [-d|-p|-s] file\n", name);
	fputs("Where: -D    Turn debug on\n", stderr);
	fputs("       -d    Dump decoded file to STDOUT\n", stderr);
	fputs("       -p    Decode and parse file showing profile data\n", stderr);
	fputs("       -s    Decode and parse file showing dive summary data\n", stderr);
}

int main(int argc, char *argv[]) {
	int fp = 0;
	int mode = 0;

	int opt;
	while ((opt = getopt(argc, argv, "Ddps")) != -1) {
		switch (opt) {
		case 'D':	// Debug on
			debug = TRUE;
			break;
		case 'd':	// Decode only
			// Decode file only
			mode = 0;
			break;
		case 'p':	// Decode, parse and show profile data
			mode = 1;
			break;
		case 's':	 // Decode, parse and show summary data
			mode = 2;
			break;
		default:
			fputs("Invalid option\n", stderr);
			usage(argv[0]);
			exit(1);
		}
	}

	if (optind < argc) {
		// File name on command line
		fp = open(argv[optind], O_RDONLY);
		if (fp == -1) {
			fprintf(stderr, "Error opening file (%s): %s\n", argv[optind], strerror(errno));
			exit(1);
		}
	}

	// Read file
	struct memblock canfile = read_can(fp);

	// Determine type of file (wan or can)
	int ext = strlen(argv[optind]) - 4;
	if (!strcasecmp(argv[optind] + ext, ".wan")) {
		config.file_type = FILE_WAN;
		config.offset = 0x30000;
	} else {
		config.file_type = FILE_CAN;
		config.offset = 0x40000;
	}

	config.file_format = canfile.buffer[config.offset];
	switch (config.file_format) {
	case 0x43:
	case 0x4f:
		config.address_size = 3;
		break;
	case 0x45:
	case 0x46:
		config.address_size = 4;
		break;
	}

	struct memblock clearfile;
	clearfile.alloc = canfile.size;
	clearfile.buffer = malloc(clearfile.alloc);
	if (!clearfile.buffer) {
		fputs("Unable to allocated memory for clearfile.", stderr);
		exit(1);
	}

	decode_file(canfile, &clearfile);

	canfile.alloc = canfile.size = 0;


	switch (mode) {
	case 0:		// Dump decoded file to stdout
		// Dump clear text dive offset pointers
		for (unsigned int x = 0; x < clearfile.size; x++) {
			putchar(clearfile.buffer[x]);
		}
		break;
	case 1:		// Summary only
		foreach_dive(clearfile, print_dive_samples_cb, 0);
		break;

	case 2: 	//Summary and profile
		foreach_dive(clearfile, print_dive_summary_cb, 0);
		break;
	}

	free(canfile.buffer);
	free(clearfile.buffer);

	exit(0);
}
