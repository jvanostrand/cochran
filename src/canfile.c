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
#include "cochran_sample_parse.h"

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
	cochran_family_t family;
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
		for (int i = 0; i < 0x43; i++)
			fprintf(stderr, "%02x ", header[i]);
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


void parse_log(const unsigned char *log_buf, cochran_log_t *log) {
	switch (config.family) {
// TODO: Nemo2a format
// TODO: GemPNox format
	case FAMILY_COMMANDER_I:
		cochran_log_commander_I_parse(log_buf, log);
		break;
	case FAMILY_GEMINI:
	case FAMILY_COMMANDER_II:
		cochran_log_commander_II_parse(log_buf, log);
		break;
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


static int cochran_sample_parse_cb(int time, cochran_sample_t *sample, void *userdata) {
	static int last_time = -1;
	static float depth = 0, temp = 0, tank_pressure = 0, gas_consumption_rate = 0;
	static float ascent_rate = 0;
	static int event_flag = 0;

	if (!sample) {
		// Flush output
		printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m\n",
			last_time / 60, last_time % 60,
			depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
		// Reset and leave
		last_time = -1;
		depth = temp = tank_pressure = gas_consumption_rate = 0;
		ascent_rate = 0;
		event_flag = 0;
		return 0;
	}

	if (last_time != time && sample->type != SAMPLE_EVENT && sample->type != SAMPLE_INTERDIVE) {
		if (last_time != -1) {
			// print last line
			printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m\n",
				last_time / 60, last_time % 60,
				depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
		}
		last_time = time;
	}

	if (sample->type != SAMPLE_EVENT) event_flag = 0;

	switch (sample->type) {
	case SAMPLE_DEPTH:
		depth = sample->value.depth;
		break;
	case SAMPLE_TEMP:
		temp = sample->value.temp;
		break;
	case SAMPLE_EVENT:
		if (!event_flag)
			printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m\n",
				time / 60, time % 60,
				depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
		printf("       %s\n", sample->value.event);
		event_flag = 1;
		break;
	case SAMPLE_ASCENT_RATE:
		ascent_rate = sample->value.ascent_rate;
		break;
	case SAMPLE_TANK_PRESSURE:
		tank_pressure = sample->value.tank_pressure;
		break;
	case SAMPLE_GAS_CONSUMPTION_RATE:
		gas_consumption_rate = sample->value.gas_consumption_rate;
		break;
	case SAMPLE_DECO:
		printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m\n",
			time / 60, time % 60,
			depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
		printf("       Deco: Ceiling: %dft %d min total\n", sample->value.deco.ceiling, sample->value.deco.time);
		break;
	case SAMPLE_DECO_FIRST_STOP:
		printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m\n",
			time / 60, time % 60,
			depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
		printf("       Deco: Ceiling: %dft %d min first stop\n", sample->value.deco.ceiling, sample->value.deco.time);
		break;
	case SAMPLE_NDL:
		printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m\n",
			time / 60, time % 60,
			depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
		printf("       NDL: %d\n", sample->value.ndl);
		break;
	case SAMPLE_TISSUES:
		printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m\n",
			time / 60, time % 60,
			depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
		printf("       Tissues:");
		for (int i = 0; i < 20; i++)
			printf(" %02x", sample->value.tissues[i]);
		putchar('\n');
		break;
	case SAMPLE_INTERDIVE:
		printf("       Interdive: Code: %02x  Date: %04d/%02d/%02d %02d:%02d:%02d  Data:", 
			sample->value.interdive.code,
			sample->value.interdive.time.tm_year + 1900, sample->value.interdive.time.tm_mon + 1,
			sample->value.interdive.time.tm_mday, sample->value.interdive.time.tm_hour,
			sample->value.interdive.time.tm_min, sample->value.interdive.time.tm_sec);
		for (int i = 0; i < sample->value.interdive.size; i++) {
			printf(" %02x", (unsigned char ) sample->value.interdive.data[i]);
		}
		putchar('\n');
	}

	return 0;
}


/*
* Parse sample data, extract events and build a dive
*/

static int print_dive_samples_cb(struct memblock dive, unsigned dive_num, int last_dive, void *userdata) {
	const unsigned char *log_buf =  dive.buffer + config.log_offset;
	const unsigned char *samples = dive.buffer + config.profile_offset;
	cochran_log_t log;
	unsigned int samples_size;

	parse_log(log_buf, &log);

	// Calculate size
	if (log.profile_end == 0xFFFFFFFF || log.profile_end == 0 || log.profile_end < log.profile_pre) {
		// Corrupt dive end log
		samples_size = dive.size - config.profile_offset;
	} else {
		samples_size = log.profile_end - log.profile_pre;
	}

	puts("\n");
	cochran_log_print_short_header(0);
	print_dive_summary(dive, dive_num, last_dive, userdata);
	cochran_sample_parse(config.family, &log, samples, samples_size, cochran_sample_parse_cb, 0);
	cochran_sample_parse_cb(-1, NULL, userdata);	// Force an end

	return 0;
}


static int dump_log_cb(struct memblock dive, unsigned int dive_num, int last_dive, void *userdata) {
	const char *outdir = (char *) userdata;
	const unsigned char *log_buf =  dive.buffer + config.log_offset;
	const unsigned char *samples = dive.buffer + config.profile_offset;
	int samples_size = dive.size - config.logbook_size - config.profile_offset;
	char path[128];

	snprintf(path, 128, "%s/%d.memory", outdir, dive_num);
	int outfd = open(path, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IROTH);
	if (outfd) {
		write(outfd, log_buf, config.logbook_size);
		write(outfd, samples, samples_size);
		close(outfd);
	}
	return 0;
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
int from = 0x4914 + 512, to = from + 256; //dive.size;
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
	fprintf(stderr, "Usage: %s [-d|-p|-s|-l dir] file\n", name);
	fputs("Where: -D    Turn debug on\n", stderr);
	fputs("       -d    Dump decoded file to STDOUT\n", stderr);
	fputs("       -p    Decode and parse file showing profile data\n", stderr);
	fputs("       -s    Decode and parse file showing dive summary data\n", stderr);
	fputs("       -l    Dump distinct logs + profile files into directory \"dir\"\n", stderr);
}

int main(int argc, char *argv[]) {
	int fp = 0;
	int mode = 0;
	char *outdir;

	int opt;
	while ((opt = getopt(argc, argv, "Ddpsl:")) != -1) {
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
		case 'l':
			mode = 3;
			outdir = optarg;
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
	case 1:		// Summar and profile only
		foreach_dive(clearfile, print_dive_samples_cb, 0);
		break;
	case 2: 	// Summary only
		foreach_dive(clearfile, print_dive_summary_cb, 0);
		break;
	case 3:		// Dump logs into individual files in outdir
		mkdir(outdir, S_IRWXU | S_IRWXG | S_IROTH);
		// id0
		char path[128];
		snprintf(path, 128, "%s/%s", outdir, "id0");
		int outfd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		write(outfd, clearfile.buffer + config.offset + 0x102, 0x36);
		close(outfd);
		// config0
		snprintf(path, 128, "%s/%s", outdir, "config0");
		outfd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		write(outfd, clearfile.buffer + config.offset + 0x102 + 0x36, 512);
		close(outfd);
		// config1
		snprintf(path, 128, "%s/%s", outdir, "config1");
		outfd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		write(outfd, clearfile.buffer + config.offset + 0x102 + 0x36 + 512, 512);
		close(outfd);

		foreach_dive(clearfile, dump_log_cb, (void *) outdir);
		break;
	}

	free(canfile.buffer);
	free(clearfile.buffer);

	exit(0);
}
