
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct memblock {
	unsigned char * buffer;
	unsigned int alloc, size;
};

// Convert 4 bytes into an INT
#define array_uint32_le(p) ( (unsigned int) (p)[0] \
							+ ((p)[1]<<8) + ((p)[2]<<16) \
							+ ((p)[3]<<24) )

static unsigned int partial_decode(unsigned int start, unsigned int end,
				   const unsigned char *decode, unsigned offset, unsigned mod,
				   const unsigned char *buf, unsigned int size, unsigned char *dst)
{
	unsigned i, sum = 0;

	for (i = start; i < end; i++) {
		unsigned char d = decode[offset++];
		if (i >= size)
			break;
		if (offset == mod)
			offset = 0;
		d += buf[i];
		if (dst)
			dst[i] = d;
		sum += d;
	}
	return sum;
}

#define hexchar(n) ("0123456789abcdef"[(n) & 15])

static int show_line(unsigned offset, const unsigned char *data,
					unsigned size, int show_empty)
{
	unsigned char bits;
	int i, off;
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
	int show = 1,  i;
	

	for (i = 0; i < size; i += 16)
		show = show_line(i, data + i, size - i, show);
}

static void parse_cochran_header(
				 const unsigned char *decode, unsigned mod,
				 const unsigned char *in, unsigned size)
{
	unsigned char *buf = malloc(size);

	/* Do the "null decode" using a one-byte decode array of '\0' */
	/* Copies in plaintext, will be overwritten later */
	partial_decode(0, 0x0100, "", 0, 1, in, size, buf);

	/*
	 * The header scrambling is different form the dive
	 * scrambling. Oh yay!
	 */
	partial_decode(0x0100, 0x010e, decode, 0, mod, in, size, buf);
	//partial_decode(0x010e, 0x0b14, decode, 0, mod, in, size, buf);
	partial_decode(0x010e, 0x0f14, decode, 0, mod, in, size, buf);
	partial_decode(0x0b14, 0x1b14, decode, 0, mod, in, size, buf);
	partial_decode(0x1b14, 0x2b14, decode, 0, mod, in, size, buf);
	partial_decode(0x2b14, 0x3b14, decode, 0, mod, in, size, buf);
	partial_decode(0x3b14, 0x5414, decode, 0, mod, in, size, buf);
	partial_decode(0x5414,  size, decode, 0, mod, in, size, buf);

/*
	// Detect log type
	switch (buf[0x133])
	{
	case '2':	// Cochran Commander, version II log format
		config.logbook_size = 256;
		if (buf[0x132] == 0x10) {
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
		printf ("Unknown log format v%c\n", buf[0x137]);
		exit(1);
		break;
	}
*/
	puts("Header\n======\n\n");
	//cochran_debug_write(buf + 0x102, 54);
	cochran_debug_write(buf, size);

	free(buf);
}

/*
* Bytes expected after a pre-dive event code
*/
static int cochran_predive_event_bytes (unsigned char code)
{

	int x = 0;

	int gem_event_bytes[15][2] = {	{0x00, 10}, {0x02, 17}, {0x08, 18},
									{0x09, 18}, {0x0c, 18}, {0x0d, 18},
									{0x0e, 18},
									{  -1,  0} };
	int cmdr_event_bytes[15][2] = {	{0x00, 16}, {0x01, 20}, {0x02, 17},
									{0x03, 16}, {0x06, 18}, {0x07, 18},
									{0x08, 18}, {0x09, 18}, {0x0a, 18},
									{0x0b, 20}, {0x0c, 18}, {0x0d, 18},
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
		while (gem_event_bytes[x][0] != code && gem_event_bytes[x][0] != -1)
			x++;

		return gem_event_bytes[x][1];
		break;
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

}


/*
* Parse sample data, extract events and build a dive
*/

static void cochran_parse_samples(const unsigned char *log,
							const unsigned char *samples, int size)
{
	const unsigned char *s;
	unsigned int offset = 0, seconds = 0;
	double depth = 0, temp = 0, depth_sample = 0, psi = 0, sgc_rate = 0;
	int ascent_rate = 0;
	unsigned int ndl = 0;
	unsigned int deco_obligation = 0, deco_ceiling = 0, deco_time = 0;
	char *event_desc;

	const cochran_cmdr_log_t *log_cmdr = (cochran_cmdr_log_t *) log;
	const cochran_emc_log_t *log_emc = (cochran_emc_log_t *) log;

	// Get starting depth and temp (tank PSI???)
	switch (config.type)
	{
	case TYPE_GEMINI:
		depth = (float) (log_cmdr->start_depth[0]
						+ log_cmdr->start_depth[1] * 256) / 4;
		psi = log_cmdr->start_psi[0] + log_cmdr->start_psi[1] * 256;
		sgc_rate = (float) (log_cmdr->start_sgc[0]
						+ log_cmdr->start_sgc[1] * 256) / 2;
		break;
	case TYPE_COMMANDER:
		depth = (float) (log_cmdr->start_depth[0]
						+ log_cmdr->start_depth[1] * 256) / 4;
		break;
	case TYPE_EMC:
		depth = (float) log_emc->start_depth[0] / 256 + log_emc->start_depth[1];
		temp = log_emc->start_temperature;
		break;
	}

	// Skip past pre-dive events
	unsigned int x = 0;
	if (samples[x] != 0x40) {
		unsigned int c, y;
		while ( (samples[x] & 0x80) == 0 && samples[x] != 0x40 && x < size) {
			c = cochran_predive_event_bytes(samples[x]) + 1;
			printf ("Predive event: ", samples[x]);
			for (y = 0; y < c; y++) printf ("%02x ", samples[x + y]);
			putchar('\n');
			x += c;
		}
	}

	// Now process samples
	offset = x;
	while (offset < size) {
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
				deco_time = (array_uint32_le(s + 3) + 1) * 60;
				offset += 4;	// skip 4 event bytes
				event_desc = "Raise ceiling 10 ft";
				break;
			case 0xAB:
				deco_ceiling += 10;	// ft
				deco_time = (array_uint32_le(s + 3) + 1) * 60;
				offset += 4;	// skip 4 event bytes
				event_desc = "Lower ceiling 10 ft";
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

			printf("Event %02x: %s\n", s[0], event_desc);

			offset++;
			continue;
		}

		// Depth is in every sample
		depth_sample = (float) (s[0] & 0x3F) / 4 * (s[0] & 0x40 ? -1 : 1);
		depth += depth_sample;

		switch (config.type)
		{
		case TYPE_GEMINI:
			switch (seconds % 4)
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
			switch (seconds % 2)
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
			switch (seconds % 2)
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

		printf ("%02dh %02dm %02ds: Depth: %-5.2f, ", seconds / 3660,
								(seconds % 3660) / 60, seconds % 60, depth);

		if (config.type == TYPE_COMMANDER) {
			switch (seconds % 2)
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
			switch (seconds % 4)
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
			switch (seconds % 2)
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
			switch (seconds % 24)
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
		seconds++;
	}
}


static void parse_cochran_dive(const unsigned char *decode, unsigned mod,
			       const unsigned char *in, unsigned size)
{
	unsigned char *buf = malloc(size);
	
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
	partial_decode(0, 0x0fff, decode, 1, mod, in, size, buf);
	partial_decode(0x0fff, 0x1fff, decode, 0, mod, in, size, buf);
	partial_decode(0x1fff, 0x2fff, decode, 0, mod, in, size, buf);
	partial_decode(0x2fff, 0x48ff, decode, 0, mod, in, size, buf);

	/*
	 * This is not all the descrambling you need - the above are just
	 * what appears to be the fixed-size blocks. The rest is also
	 * scrambled, but there seems to be size differences in the data,
	 * so this just descrambles part of it:
	 */
	// Decode log entry (512 bytes + random prefix)
	partial_decode(0x48ff, 0x4914 + config.logbook_size, decode,
					0, mod, in, size, buf);

	unsigned int sample_size = size - 0x4914 - config.logbook_size;

	// Decode sample data
	partial_decode(0x4914 + config.logbook_size, size, decode,
					0, mod, in, size, buf);

	// Display pre-logbook data
	puts("\nPre Logbook Data\n");
	cochran_debug_write(buf, 0x4914);

	// Display log book
	puts("\nLogbook Data\n");
	cochran_debug_write(buf + 0x4914,  config.logbook_size + 0x400);

	// Display sample data
	puts("\nSample Data\n");

	cochran_parse_samples(buf + 0x4914, buf + 0x4914
					+ config.logbook_size, sample_size);

	free(buf);
}

int try_to_open_cochran(struct memblock *mem)
{
	unsigned int i;
	unsigned int mod;
	unsigned int *offsets, dive1, dive2;
	unsigned char *decode = mem->buffer + 0x30001;

	if (mem->size < 0x30000)
		return 0;

	offsets = (int *) mem->buffer;
	dive1 = offsets[0];
	dive2 = offsets[1];

	if (dive1 < 0x30000 || dive2 < dive1 || dive2 > mem->size)
		return 0;

	mod = decode[0xff] + 1;

	printf ("Modulus: %d\n\n", mod);

	parse_cochran_header(decode, mod, mem->buffer + 0x30000, dive1 - 0x30000);

	// Decode each dive
	for (i = 0; i < 65534; i++) {
		dive1 = offsets[i];
		dive2 = offsets[i + 1];
		if (dive2 < dive1)
			break;
		if (dive2 > mem->size)
			break;
		printf("\nDive: %3d, Offset: %08x - %08x\n", i + 1, dive1, dive2);
		parse_cochran_dive(decode, mod, mem->buffer + dive1,
						dive2 - dive1);
	}

}


void main(void)
{
	struct memblock candata;
	int bytes_read;

	candata.alloc = 65536;
	candata.buffer = malloc(candata.alloc);

	while ((bytes_read = read(0, candata.buffer + candata.size, 512)) > 0)
	{
		candata.size += bytes_read;
		if (candata.alloc - candata.size < 512) {
			candata.alloc += 16384;
			candata.buffer = realloc(candata.buffer, candata.alloc);
		}
	}

	try_to_open_cochran(&candata);

}
