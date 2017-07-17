#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "canfile_emc.h"
#include "canfile_cmdr.h"

//#include "dive.h"
//#include "file.h"


#define LOG_ENTRY_OFFSET 0x4914

#define SUMMARY_HEAD "Dive  Date     Time     Duration Depth  Temp Predive  Sample   End     "
#define SUMMARY_LINE "----- -------- -------- -------- ------ ---- -------- -------- --------"
#define SUMMARY_FMT  "%5i %02i/%02i/%02i %02i:%02i:%02i %2ih%02i    %6.2f %3i   %08x %08x %08x\n"

enum cochran_type {
	TYPE_GEMINI,
	TYPE_COMMANDER,
	TYPE_EMC
};

struct config {
	enum cochran_type type;
	unsigned int logbook_size;
	unsigned int sample_size;
} config;
	
struct memblock {
	unsigned char * buffer;
	unsigned int alloc, size;
};

typedef int (*cf_callback_t) (struct memblock dive, unsigned int dive_num, int last_dive, void *userdata);

// Convert 4 bytes into an INT
#define array_uint32_le(p) ( (unsigned int) (p)[0] \
							+ ((p)[1]<<8) + ((p)[2]<<16) \
							+ ((p)[3]<<24) )
#define array_uint16_le(p) ( (unsigned int) (p)[0] + ((p)[1]<<8) )
#define array_uint16_be(p) ( (unsigned int) ((p)[0]<<8) + (p)[1] )


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
static unsigned int partial_decode(const unsigned int start, const unsigned int end,
				   const unsigned char *key, unsigned offset, const unsigned char mod,
				   const unsigned char *cipher, const unsigned int size, unsigned char *cleartext)
{
	unsigned i, sum = 0;

	for (i = start; i < end; i++) {
		unsigned char d = key[offset++];
		if (i >= size)
			break;
		if (offset == mod)
			offset = 0;
		d += cipher[i];
		if (cleartext)
			cleartext[i] = d;
		sum += d;
	}
	return sum;
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
static void parse_header(const struct memblock clearfile)
{
	const unsigned char *header = clearfile.buffer + 0x40102;
	int header_size = *(int *)clearfile.buffer - 0x40102;

	// Detect log type
	switch (header[0x031])
	{
	case '2':	// Cochran Commander, version II log format
		config.logbook_size = 256;
		if (header[0x030] == 0x10) {
			config.type = TYPE_GEMINI;
			config.sample_size = 2;	// Gemini with tank PSI samples
		} else  {
			config.type = TYPE_COMMANDER;
			config.sample_size = 2;	// Commander
		}
		break;
	case '3':	// Cochran EMC, version III log format
		config.type = TYPE_EMC;
		config.logbook_size = 512;
		config.sample_size = 3;
		break;
	default:
		fprintf (stderr, "Unknown log format v%c\n", header[0x137]);
		exit(1);
		break;
	}
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

	switch (config.type)
	{
	case TYPE_GEMINI:
	case TYPE_COMMANDER:
		while (cmdr_event_bytes[x][0] != code && cmdr_event_bytes[x][0] != -1)
			x++;

		return cmdr_event_bytes[x][1];
		break;
	case TYPE_EMC:
		while (emc_event_bytes[x][0] != code && emc_event_bytes[x][0] != -1)
			x++;

		return emc_event_bytes[x][1];
		break;
	}
	return(0);
}



// Print dive heading
static int print_dive_summary(struct memblock dive,
			unsigned int dive_num, int last_dive, void *userdata)
{
	const unsigned char *log =  dive.buffer + 0x4914;

	if (last_dive)
		return(0);

	if (debug) {
		// Display pre-logbook data
		fputs("\nPre Logbook Data\n", stderr);
		cochran_debug_write(dive.buffer, 0x4914);

		// Display log book
		fputs("\nLogbook Data\n", stderr);
		cochran_debug_write(log,  config.logbook_size + 0x700);
	}

	struct cochran_cmdr_log_t *cmdr_log = (cochran_cmdr_log_t *) log;
	struct cochran_emc_log_t *emc_log = (cochran_emc_log_t *) log;

	switch (config.type)
	{
	case TYPE_GEMINI:
	case TYPE_COMMANDER:
		printf(SUMMARY_FMT, dive_num, cmdr_log->day, cmdr_log->month, cmdr_log->year,
			cmdr_log->hour, cmdr_log->minutes, cmdr_log->seconds,
			array_uint16_le(cmdr_log->bt)/60, array_uint16_le(cmdr_log->bt)%60, 
			(float) array_uint16_le(cmdr_log->max_depth)/4,
			cmdr_log->temp,
			array_uint32_le(cmdr_log->sample_pre_event_offset),
			array_uint32_le(cmdr_log->sample_start_offset),
			array_uint32_le(cmdr_log->sample_end_offset));
		break;
	case TYPE_EMC:
		printf(SUMMARY_FMT, dive_num, emc_log->day, emc_log->month, emc_log->year,
			emc_log->hour, emc_log->minutes, emc_log->seconds,
			array_uint16_le(emc_log->bt)/60, array_uint16_le(emc_log->bt)%60, 
			(float) array_uint16_le(emc_log->max_depth)/4,
			emc_log->temp,
			array_uint32_le(emc_log->sample_pre_event_offset),
			array_uint32_le(emc_log->sample_start_offset),
			array_uint32_le(emc_log->sample_end_offset));
		break;
	default:
		fputs("Invalid conig.type", stderr);
		break;
	}

	return(0);
}

// Callback function that tracks lines to reproduce heading
static int print_dive_summary_cb( struct memblock dive,
			unsigned int dive_num, int last_dive, void *userdata)
{
	static int count = 0;

	if (count == 0 && !last_dive) {
		puts(SUMMARY_HEAD);
		puts(SUMMARY_LINE);
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

	unsigned char *samples;
	unsigned int sample_size;
	unsigned char profile_period = 1; 	// seconds between samples

	if (last_dive) {
		// last dive section contains only interdive events, no log
		samples = dive.buffer;
		sample_size = dive.size;
	} else {
		samples = dive.buffer + 0x4914 + config.logbook_size;
		sample_size = dive.size - 0x4914 - config.logbook_size;
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
	if (dive.size < 0x4914 + config.logbook_size)
		return(0);

	const cochran_cmdr_log_t *log_cmdr = (cochran_cmdr_log_t *) (dive.buffer + 0x4914);
	const cochran_emc_log_t *log_emc = (cochran_emc_log_t *) (dive.buffer + 0x4914);

	puts(SUMMARY_HEAD);
	puts(SUMMARY_LINE);
	print_dive_summary(dive, dive_num, last_dive, userdata);

	unsigned int sample_start_offset = 0, sample_end_offset = 0;
	// Get starting depth and temp (tank PSI???)
	switch (config.type)
	{
	case TYPE_GEMINI:
		profile_period = log_cmdr->profile_period;
		sample_start_offset = array_uint32_le(log_cmdr->sample_start_offset);
		sample_end_offset = array_uint32_le(log_cmdr->sample_end_offset);
		depth = (float) array_uint16_le(log_cmdr->start_depth) / 4;
		psi = array_uint16_le(log_cmdr->start_psi);
		sgc_rate = (float) array_uint16_le(log_cmdr->start_sgc) / 2;
		break;
	case TYPE_COMMANDER:
		profile_period = log_cmdr->profile_period;
		sample_start_offset = array_uint32_le(log_cmdr->sample_start_offset);
		sample_end_offset = array_uint32_le(log_cmdr->sample_end_offset);
		depth = (float) array_uint16_le(log_cmdr->start_depth) / 4;
		break;
	case TYPE_EMC:
		profile_period = log_emc->profile_period;
		sample_start_offset = array_uint32_le(log_emc->sample_start_offset);
		sample_end_offset = array_uint32_le(log_emc->sample_end_offset);
		depth = (float) array_uint16_le(log_emc->start_depth) / 256;
		temp = log_emc->start_temperature;
		break;
	}

	// Use the log information to determine actual profile sample size
	// Otherwise we will get surface time at end of dive.
	if (sample_start_offset < sample_end_offset && sample_end_offset != 0xffffffff)
		sample_size = x + sample_end_offset - sample_start_offset;

	// Now process samples
	offset = x;
	while (offset < sample_size) {
		const unsigned char *s;
		s = samples + offset;

		// Check for event
		if (s[0] & 0x80) {
			switch (s[0])
			{
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
				if (offset + 3 < sample_size) {
					deco_time = (array_uint32_le(s + 3) + 1) * 60;
					offset += 4;	// skip 4 event bytes
					event_desc = "Raise ceiling 10 ft";
				}
				break;
			case 0xAB:
				deco_ceiling += 10;	// ft
				if (offset + 3 < sample_size) {
					deco_time = (array_uint32_le(s + 3) + 1) * 60;
					offset += 4;	// skip 4 event bytes
					event_desc = "Lower ceiling 10 ft";
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

		// Depth is in every sample
		depth_sample = (float) (s[0] & 0x3F) / 4 * (s[0] & 0x40 ? -1 : 1);
		depth += depth_sample;

		printf("      ");

		if (debug) {
			switch (config.type)
			{
			case TYPE_GEMINI:
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
			case TYPE_COMMANDER:
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
			case TYPE_EMC:
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
	
		printf ("%02d:%02d:%02d Depth: %-5.2f, ", (sample_cnt * profile_period) / 3660,
							((sample_cnt * profile_period) % 3660) / 60, (sample_cnt * profile_period) % 60, depth);

		if (config.type == TYPE_COMMANDER) {
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
		} else if (config.type == TYPE_GEMINI) {
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
		} else if (config.type == TYPE_EMC) {
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
	const unsigned char *key = clearfile->buffer + 0x40001;
	const unsigned char mod = key[0x100] + 1;
	const int *dives = (int *) clearfile->buffer;
	const int dive_offset = dives[dive_num - 1];

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
		partial_decode(0,      0x0fff, key, 1, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		partial_decode(0x0fff, 0x1fff, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		partial_decode(0x1fff, 0x2fff, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		partial_decode(0x2fff, 0x48ff, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
	
		/*
	 	* This is not all the descrambling you need - the above are just
	 	* what appears to be the fixed-size blocks. The rest is also
	 	* scrambled, but there seems to be size differences in the data,
	 	* so this just descrambles part of it:
	 	*/
		// Decode log entry (512 bytes + random prefix)
		partial_decode(0x48ff, 0x4914 + config.logbook_size,
				key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
	
		int sample_size = dive.size - 0x4914 - config.logbook_size;
		if (sample_size > 0) {
			// Decode sample data
			partial_decode(0x4914 + config.logbook_size, dive.size,
				key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
		}
	} else {
		partial_decode(0, dive.size, key, 0, mod, dive.buffer, dive.size, clearfile->buffer + dive_offset);
	}
	
	return(0);
}


static int foreach_dive(const struct memblock clearfile, cf_callback_t callback, void *userdata) {
	const unsigned int *dives = (unsigned int *) clearfile.buffer;

	if (clearfile.size < 0x40102)
		return 0;

	int result = 0;
	unsigned int dive_end = 0;
	struct memblock dive;
	unsigned int i = 0;
	while (dives[i] && i < 65534) {
		dive_end = dives[i + 1];

		// check out of range
		if (dive_end < dives[i] || dive_end > clearfile.size)
			break;

		dive.size = dive_end - dives[i];
		dive.buffer = malloc(dive.size);
		if (!dive.buffer) {
			fputs("Unable to allocate dive.buffer space.\n", stderr);
			exit(1);
		}

		memcpy(dive.buffer, clearfile.buffer + dives[i], dive.size);

		if (callback)
			result = (callback)(dive, i+1, 0, userdata);

		free(dive.buffer);

		if (result)
			return(result);
		i++;
	}

	// Now process the trailing inter-dive events
	if (dives[65534] - 1 > dives[i] && dives[65534] - 1 <= clearfile.size) {
		dive.size = dives[65534] - 1 - dives[i];

		dive.buffer = malloc(dive.size);
		if (!dive.buffer) {
			fputs("Unable to allocate dive.buffer space.\n", stderr);
			exit(1);
		}
	
		memcpy(dive.buffer, clearfile.buffer + dives[i], dive.size);

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

struct memblock decode_can(struct memblock canfile) {

	struct memblock clearfile;

	clearfile.alloc = canfile.size;
	clearfile.buffer = malloc(clearfile.alloc);
	if (!clearfile.buffer) {
		fputs("Unable to allocated memory for clearfile.", stderr);
		exit(1);
	}

	// Copy the non-encrypted header
	memcpy(clearfile.buffer, canfile.buffer, 0x40102);
	clearfile.size = 0x40102;

	unsigned int hsize = (*(unsigned int *) clearfile.buffer) - 0x40102;

	/* Do the "null decode" using a one-byte decode array of '\0' */
	/* Copies in plaintext, will be overwritten later */
	partial_decode(0, 0x0102, (unsigned char *) "", 0, 1, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);

	/* Copy the header information
	*  It was a weird cipher patter that may tell us the boundaries of
	*  structures.
	*/
	// header size is the space between 0x40102 and the first dive.
	const unsigned char *key = canfile.buffer + 0x40001;
	const unsigned char mod = key[100] + 1;
	partial_decode(0x0000, 0x000c, key, 0, mod, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);
	partial_decode(0x000c, 0x0a12, key, 0, mod, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);
	partial_decode(0x0a12, 0x1a12, key, 0, mod, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);
	partial_decode(0x1a12, 0x2a12, key, 0, mod, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);
	partial_decode(0x2a12, 0x3a12, key, 0, mod, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);
	partial_decode(0x3a12, 0x5312, key, 0, mod, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);
	partial_decode(0x5312,  hsize, key, 0, mod, canfile.buffer + 0x40102, hsize, clearfile.buffer + 0x40102);

	parse_header(clearfile);
	
	foreach_dive(canfile, decode_dive, (void *) &clearfile);

	// Erase the key, since we are decoded
	for (int x = 0x40001 ; x < 0x40101; x++)
		clearfile.buffer[x] = 0;
	

	clearfile.size = canfile.size;

	return(clearfile);
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
	struct memblock clearfile = decode_can(canfile);
	//free(canfile.buffer);
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

	exit(0);
}
