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
#include "cochran_can.h"
#include "cochran_log.h"
#include "cochran_sample.h"


// Print dive heading
static int print_dive_summary(cochran_can_meta_t *meta, const unsigned char *dive, unsigned int dive_size, unsigned int dive_num, int last_dive, void *userdata) {
	const unsigned char *log_buf = dive + meta->log_offset;

	if (last_dive)
		return(0);

	cochran_log_t log;
	cochran_log_parse(meta->model, log_buf, &log);

	cochran_log_print_short(&log, dive_num);

	return(0);
}


// Callback function that tracks lines to reproduce heading
static int print_dive_summary_cb(cochran_can_meta_t *meta, const unsigned char *dive, unsigned int dive_size, unsigned int dive_num, int last_dive, void *userdata) {
	static int count = 0;

	if (count == 0 && !last_dive) {
		cochran_log_print_short_header(1);
	}

	count++;
	if (count > 25) count = 0;

	return print_dive_summary(meta, dive, dive_size, dive_num, last_dive, userdata);
}


static int cochran_sample_parse_cb(int time, cochran_sample_t *sample, void *userdata) {
	static int last_time = -1;
	static float depth = 0, temp = 0, tank_pressure = 0, gas_consumption_rate = 0;
	static float ascent_rate = 0;
	static cochran_sample_type_t last_type = SAMPLE_UNDEFINED;
	static unsigned char raw_data[32];
	static unsigned int raw_size = 0;

	if (!sample && last_time == -1)	// Nothing to do
		return 0;

	// Do we need to print out a sample line?
	if (last_time != -1) {
		if (!sample 								// Direct call to cleanup
			|| last_time != time	// Normal time change
				// On a special event if the previous event wasn't also an special event
			|| ((sample->type == SAMPLE_EVENT || sample->type == SAMPLE_INTERDIVE
				|| sample->type == SAMPLE_DECO || sample->type == SAMPLE_DECO_FIRST_STOP
				|| sample->type == SAMPLE_NDL  || sample->type == SAMPLE_TISSUES)
				&& (last_type != SAMPLE_EVENT && last_type != SAMPLE_INTERDIVE
				&& last_type != SAMPLE_DECO && last_type != SAMPLE_DECO_FIRST_STOP
				&& sample->type != SAMPLE_NDL  && sample->type != SAMPLE_TISSUES))) {

			// Print sample line
			printf("%3dm%02d %6.2fft %4.1fF %6.2ff/m %6.1fpsi %4.1fpsi/m  [",
				last_time / 60, last_time % 60,
				depth, temp, ascent_rate, tank_pressure, gas_consumption_rate);
			// ... and raw data too
			for (unsigned int i = 0; i < raw_size; i++) printf(" %02x", raw_data[i]);
			printf(" ]\n");
			raw_size = 0;
		}
	}

	if (!sample) {
		// Reset and leave
		last_time = -1;
		last_type = SAMPLE_UNDEFINED;
		depth = temp = tank_pressure = gas_consumption_rate = 0;
		ascent_rate = 0;
		raw_size = 0;
		return 0;
	}

	// Collect raw samples
	for (unsigned int i = 0;  i < sample->raw.size; i++) raw_data[raw_size++] = sample->raw.data[i];

	switch (sample->type) {
	case SAMPLE_DEPTH:
		depth = sample->value.depth;
		break;
	case SAMPLE_TEMP:
		temp = sample->value.temp;
		break;
	case SAMPLE_EVENT:
		printf("       %s  [", sample->value.event);
		for (unsigned int i = 0; i < sample->raw.size; i++) printf(" %02x", sample->raw.data[i]);
		printf(" ]\n");
		raw_size -= sample->raw.size;	// roll-back
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
		printf("       Deco: Ceiling: %dft %d min total  [", sample->value.deco.ceiling, sample->value.deco.time);
		for (unsigned int i = 0; i < sample->raw.size; i++) printf(" %02x", sample->raw.data[i]);
		printf(" ]\n");
		raw_size -= sample->raw.size;	// roll-back
		break;
	case SAMPLE_DECO_FIRST_STOP:
		printf("       Deco: Ceiling: %dft %d min first stop  [", sample->value.deco.ceiling, sample->value.deco.time);
		for (unsigned int i = 0; i < sample->raw.size; i++) printf(" %02x", sample->raw.data[i]);
		printf(" ]\n");
		raw_size -= sample->raw.size;	// roll-back
		break;
	case SAMPLE_NDL:
		printf("       NDL: %d [", sample->value.ndl);
		for (unsigned int i = 0; i < sample->raw.size; i++) printf(" %02x", sample->raw.data[i]);
		printf(" ]\n");
		raw_size -= sample->raw.size;	// roll-back
		break;
	case SAMPLE_TISSUES:
		printf("       Tissues:");
		for (int i = 0; i < 20; i++)
			printf(" %02x", sample->value.tissues[i]);
		putchar('\n');
		raw_size -= sample->raw.size;	// roll-back
		break;
	case SAMPLE_INTERDIVE:
		printf("       Interdive: Code: %02x  Date: %04d/%02d/%02d %02d:%02d:%02d  Data:", 
			sample->value.interdive.code,
			sample->value.interdive.time.tm_year + 1900, sample->value.interdive.time.tm_mon + 1,
			sample->value.interdive.time.tm_mday, sample->value.interdive.time.tm_hour,
			sample->value.interdive.time.tm_min, sample->value.interdive.time.tm_sec);
		for (unsigned int i = 0; i < sample->value.interdive.size; i++) {
			printf(" %02x", (unsigned char ) sample->value.interdive.data[i]);
		}
		putchar('\n');
		raw_size -= sample->raw.size;	// roll-back
		break;
	case SAMPLE_UNDEFINED:
		// do nothing.
		break;
	}

	last_time = time;
	last_type = sample->type;
	return 0;
}


/*
* Parse sample data, extract events and build a dive
*/

static int print_dive_samples_cb(cochran_can_meta_t *meta, const unsigned char *dive, unsigned int dive_size, unsigned dive_num, int last_dive, void *userdata) {
	const unsigned char *log_buf =  dive + meta->log_offset;
	const unsigned char *samples = dive + meta->profile_offset;
	cochran_log_t log;
	unsigned int samples_size = 0;

	if (dive_size < meta->profile_offset) return 0;

	cochran_log_parse(meta->model, log_buf, &log);

	// Calculate size
	if (log.profile_end == 0xFFFFFFFF || log.profile_end == 0 || log.profile_end < log.profile_pre) {
		// Corrupt dive end log
		samples_size = dive_size - meta->profile_offset;
	} else {
		if (dive_size - meta->profile_offset < log.profile_end - log.profile_pre) 
			samples_size = dive_size - meta->profile_offset;
		else
			samples_size = log.profile_end - log.profile_pre;
	}

	puts("\n");
	cochran_log_print_short_header(0);
	print_dive_summary(meta, dive, dive_size, dive_num, last_dive, userdata);
	cochran_sample_parse(meta->model, &log, samples, samples_size, cochran_sample_parse_cb, 0);
	cochran_sample_parse_cb(-1, NULL, userdata);	// Force an end

	return 0;
}


static int dump_log_cb(cochran_can_meta_t *meta, const unsigned char *dive, unsigned dive_size, unsigned int dive_num, int last_dive, void *userdata) {
	const char *outdir = (char *) userdata;
	const unsigned char *log_buf = dive + meta->log_offset;
	const unsigned char *samples = dive + meta->profile_offset;
	int samples_size = dive_size - meta->log_size - meta->profile_offset;
	char path[128];

	snprintf(path, 128, "%s/%d.memory", outdir, dive_num);
	int outfd = open(path, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IROTH);
	if (outfd) {
		write(outfd, log_buf, meta->log_size);
		write(outfd, samples, samples_size);
		close(outfd);
	}
	return 0;
}



void usage(const char *name) {
	fprintf(stderr, "Usage: %s [-d|-p|-s|-l dir] file\n", name);
	fputs("Where: -d    Dump decoded file to STDOUT\n", stderr);
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


	// Read encrypted file
	unsigned char *canfile;
	unsigned int canfile_size = 0;
	struct stat st;
	char *filename;

	if (optind < argc) {
		// File name on command line
		filename = argv[optind];
		fp = open(filename, O_RDONLY);
		if (fp == -1) {
			fprintf(stderr, "Error opening file (%s): %s\n", filename, strerror(errno));
			exit(1);
		}

		if (fstat(fp, &st)) {
			fprintf(stderr, "Error reading file (%s): %s\n", filename, strerror(errno));
			exit(1);
		}
		canfile_size = st.st_size;
		canfile = malloc(canfile_size);
		if (!canfile) {
			fprintf(stderr, "Error allocating %d bytes.\n", canfile_size);
			exit(1);
		}
		unsigned int bytes_read = read(fp, canfile, canfile_size);

		if (bytes_read != canfile_size) {
			fprintf(stderr, "Error reading file (%s): %s\n", filename, strerror(errno));
			exit(1);
		}
	}


	// Prepare a buffer to accept decoded file
	unsigned char *clearfile;
	unsigned int clearfile_size = canfile_size;
	clearfile = malloc(clearfile_size);
	if (!clearfile) {
		fprintf(stderr, "Error allocating %d bytes.", canfile_size);
		exit(1);
	}

	// Determine file type
	cochran_file_type_t file_type;
	char *fileext = filename + strlen(filename) - 4;
	if (!strcasecmp(fileext, ".wan")) {
		file_type = FILE_WAN;
	} else if (!strcasecmp(fileext, ".can")) {
		file_type = FILE_CAN;
	} else {
		fputs("Unknown file type. File must end in .WAN or .CAN\n", stderr);
		exit(1);
	}

	// decode file
	if (cochran_can_decode_file(file_type, canfile, canfile_size, clearfile)) {
		fputs("Error decoding file\n", stderr);
		exit(1);
	}

	// Do something
	cochran_can_meta_t meta;
	cochran_can_meta(&meta, file_type, clearfile, clearfile_size);

	switch (mode) {
	case 0:		// Dump decoded file to stdout
		// Dump clear text dive offset pointers
		for (unsigned int x = 0; x < clearfile_size; x++) {
			putchar(clearfile[x]);
		}
		break;
	case 1:		// Summar and profile only
		cochran_can_foreach_dive(&meta, clearfile, clearfile_size, print_dive_samples_cb, 0);
		break;
	case 2: 	// Summary only
		cochran_can_foreach_dive(&meta, clearfile, clearfile_size, print_dive_summary_cb, 0);
		break;
	case 3:		// Dump logs into individual files in outdir

		mkdir(outdir, S_IRWXU | S_IRWXG | S_IROTH);
		// id0
		char path[128];
		snprintf(path, 128, "%s/%s", outdir, "id0");
		int outfd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		write(outfd, clearfile + meta.header_offset + 0x102, 0x36);
		close(outfd);
		// config0
		snprintf(path, 128, "%s/%s", outdir, "config0");
		outfd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		write(outfd, clearfile + meta.header_offset + 0x102 + 0x36, 512);
		close(outfd);
		// config1
		snprintf(path, 128, "%s/%s", outdir, "config1");
		outfd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		write(outfd, clearfile + meta.header_offset + 0x102 + 0x36 + 512, 512);
		close(outfd);

		cochran_can_foreach_dive(&meta, clearfile, clearfile_size, dump_log_cb, (void *) outdir);
		break;
	}

	free(canfile);
	free(clearfile);

	exit(0);
}
