/*
 * dumpdve.c
 *
 * Dump a cochran dive profile from a memory dump file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#define array_uint16_le(p) ( (unsigned int) (p)[0] + ((p)[1] << 8) )
#define array_uint32_le(p) ( (unsigned int) (p)[0] + ((p)[1] << 8) \
										 + ((p)[2] << 16) + ((p)[3] << 24) )

#define COCHRAN_TIMESTAMP_OFFSET 694242000

typedef enum { DC_UNKNOWN, DC_EMC, DC_CMDR } dc_type;


void print_emc(const unsigned char *p);
void print_cmdr(const unsigned char *p);

int data, max_log, log_size;
unsigned int sample_size, sample_start, sample_end;
int event_bytes[15][2];
dc_type dc = DC_UNKNOWN;

int valid_dive_start = -1, valid_dive_end = -1;

int cmdr_event_bytes[15][2] = { {0x00, 17}, {0x01, 21}, {0x02, 18},
								{0x03, 17}, {0x06, 19}, {0x07, 19},
								{0x08, 19}, {0x09, 19}, {0x0a, 19},
								{0x0b, 21}, {0x0c, 19}, {0x0d, 19},
								{0x0e, 19}, {0x10, 21},
								{  -1,  1} };
int emc_event_bytes[15][2] = {  {0x00, 19}, {0x01, 23}, {0x02, 20},
								{0x03, 19}, {0x06, 21}, {0x07, 21},
								{0x0a, 21}, {0x0b, 21}, {0x0f, 19},
								{0x10, 21},
								{  -1,  1} };

void dump_emc(const unsigned char *log);
void dump_cmdr(const unsigned char *log);

void usage(char *progname) {

	printf("Usage: %s [-e | -c] -n num dump_file\n", progname);
	printf("Where: -e         Dump is from an EMC model\n");
	printf("       -c         Dump is from a Commander model\n");
	printf("       -n num     Dump log entry num (from 0 to 255 or 511)\n");
	printf("       dump_file  Is the file containing the dive computer dump.\n");
	exit(1);
}


int main(int argc, char *argv[]) {
	
	if (argc < 4) usage(argv[0]);


	if (!strcmp(argv[1], "-e")) {
		dc = DC_EMC;
		max_log = 512;
		log_size = 512;
		for (int x = 0; x < 15; x++) {
			event_bytes[x][0] = emc_event_bytes[x][0];
			event_bytes[x][1] = emc_event_bytes[x][1];
		}
	} else if (!strcmp(argv[1], "-c")) {
		dc = DC_CMDR;
		max_log = 512;
		log_size = 256;
		for (int x = 0; x < 15; x++) {
			event_bytes[x][0] = cmdr_event_bytes[x][0];
			event_bytes[x][1] = cmdr_event_bytes[x][1];
		}
	}

	if (dc == DC_UNKNOWN) usage(argv[0]);

	if (strcmp(argv[2], "-n")) usage(argv[0]);

	int num = atoi(argv[3]);

	if (num >= max_log) {
		printf("Error log num is too large.\n");
		usage(argv[0]);
	}

	data = open(argv[4], 'r');
	if (!data) {
		printf("Unable to open %s for reading\n%s\n", argv[2], strerror(errno));
		exit(2);
	}


	// get info about the file to set memory information
	struct stat f;
	if (stat(argv[4], &f) == -1) {
		printf("%s\n", strerror(errno));
		exit(5);
	}

	sample_end = f.st_size;

	if (dc == DC_EMC) {
		switch (sample_end) {
		case 0x1000000:	// EMC 20H
			sample_start = 0x94000;
			break;
		case 0x800000:	// EMC 16
			sample_start = 0x94000;
			break;
		case 0x200000:	// EMC 14
			sample_start = 0x22000;
			break;
		default:	// Unknown
			printf("The memory file is not a known size. I can't continue.\n");
			exit(4);
		}
	} else {
		// Commander
		sample_start = 0x20000;
	}

	unsigned char *log = malloc(log_size * 2);

	lseek(data, num * log_size, SEEK_SET);

	if (read(data, log, log_size * 2) != log_size * 2) {
		printf("error reading file\n");
		exit(3);
	}


	switch (dc) {
	case DC_EMC:
		dump_emc(log);
		break;
	case DC_CMDR:
		dump_cmdr(log);
		break;
	}

	close(data);

	exit(0);
}


unsigned char* read_block(int *size, unsigned int start, unsigned int end) {
	unsigned char *block;

	if ( end < start ){
		// block wraps ring buffer, read twice
		*size = sample_end - start + end - sample_start;
		block = malloc(*size);

		lseek(data, start, SEEK_SET);
		read(data, block, sample_end - start);

		lseek(data, sample_start, SEEK_SET);
		read(data, block + sample_end - start, end - sample_start);

	} else {
		// This is easy, one read.
		*size = end - start;
		block = malloc(*size);
		lseek(data, start, SEEK_SET);
		read(data, block, *size);
	}

	return(block);
}

void dump_emc(const unsigned char *log) {
	unsigned char *profile;
	int size;
	int profile_end;

	profile_end = array_uint32_le(log + 256);

	if (profile_end == 0xFFFFFFFF)
		profile_end = array_uint32_le(log + 6 + log_size);

	profile = read_block(&size, array_uint32_le(log + 6), profile_end);

	write(STDOUT_FILENO, profile, size);
		
	free(profile);

}


void dump_cmdr(const unsigned char *log) {
	unsigned char *profile;
	int size;
	int profile_end;

	profile_end = array_uint32_le(log + 128);

	if (profile_end == 0xFFFFFFFF)
		profile_end = array_uint32_le(log + 6 + log_size);

	profile = read_block(&size, array_uint32_le(log + 6), profile_end);

	write(STDOUT_FILENO, profile, size);

	free(profile);
}
