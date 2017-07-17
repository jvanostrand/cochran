/*
 * dump.c
 *
 * Dump a cochran dive from a memory dump file.
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
	} else if (!strncmp(argv[1], "-b")) {
		dc = DC_CMDR_1;
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

	unsigned char *log = malloc(log_size);

	lseek(data, num * log_size, SEEK_SET);

	if (read(data, log, log_size) != log_size) {
		printf("error reading file\n");
		exit(3);
	}


	switch (dc) {
	case DC_EMC:
		print_emc(log);
		break;
	case DC_CMDR:
		print_cmdr(log);
		break;
	case DC_CMDR_1:
		print_cmdr_1(log);
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


void print_pre_dive(const unsigned char *profile, const int size) {

	int p = 0, e;

	printf("\nPre-dive sample size: %08x\n\n", size);

	printf(" Event Time-stamp          Display-time      Event data\n");
	
	while (p < size) {
		// Get event info
		for (e = 0; event_bytes[e][0] != profile[p] && event_bytes[e][0] != -1; e++);
		printf("  %02x ", profile[p]);

		// Get time stamp from event
		time_t time = array_uint32_le(profile + p + 1) + COCHRAN_TIMESTAMP_OFFSET;
		struct tm *t;
		t = gmtime(&time);

		// And display
		printf("%4d-%02d-%02d %02d:%02d:%02d ", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
		

		// Get display time from event
		if (dc == DC_EMC)
			printf("%2d-%02d-%02d %02d:%02d:%02d ", profile[p+10], profile[p+9], profile[p+8], profile[p+7], profile[p+6], profile[p+5]);
		else 
			printf("%2d-%02d-%02d %02d:%02d:%02d ", profile[p+9], profile[p+10], profile[p+7], profile[p+8], profile[p+5], profile[p+6]);
			
		for (int x = 11; x < event_bytes[e][1]; x++)
			printf("%02x ", profile[p+x]);
		printf("\n");

		if (event_bytes[e][1] > 0)
			p += event_bytes[e][1];
		else
			p++;
	}
}

void print_emc_profile(const char *profile, const int size) {
}




void print_emc(const unsigned char *p) {

	printf("Dive Date       Time     BT    Depth Temp Pre Ptr  Pro Ptr  End Ptr\n");
		
	printf("%4d %04d-%02d-%02d %02d:%02d:%02d %2d:%02d %5.1f %3dF %08x %08x %08x\n",
		array_uint16_le(p + 86), // dive number
		*(p + 5) + 2000, *(p + 4), *(p + 3), 		// Year, Mon, Day
		*(p + 2), *(p + 1), *p,						// Hour, Min, Sec
		array_uint16_le(p + 256 + 48)/60,			// Bottom time hours
		array_uint16_le(p + 256 + 48)%60,			// Bottom time Min
		array_uint16_le(p + 256 + 50)/4.0,			// Max depth
		*(p + 256 + 37),							// Temp
		array_uint32_le(p + 30),					// Pre dive sample ptr
		array_uint32_le(p + 6),						// Profile sample ptr
		array_uint32_le(p + 256)					// End profile ptr
	);

	// read profile data

	unsigned char *profile;
	int size;

	profile = read_block(&size, array_uint32_le(p + 30), array_uint32_le(p + 6));
	print_pre_dive(profile, size);
	free(profile);

	profile = read_block(&size, array_uint32_le(p + 6), array_uint32_le(p + 256));
	print_emc_profile(profile, size);
	free(profile);
}


void print_cmdr(const unsigned char * p) {
	printf("Dive Date       Time     BT    Depth Temp Pre Ptr  Pro Ptr  End Ptr\n");
		
	printf("%4d %04d-%02d-%02d %02d:%02d:%02d %2d:%02d %5.1f %3dF %08x %08x %08x\n",
		array_uint16_le(p + 70), // dive number
		*(p + 4) + 2000, *(p + 5), *(p + 2), 		// Year, Mon, Day
		*(p + 3), *p, *(p + 1),						// Hour, Min, Sec
		array_uint16_le(p + 128 + 38)/60,			// Bottom time hours
		array_uint16_le(p + 128 + 38)%60,			// Bottom time Min
		array_uint16_le(p + 128 + 40)/4.0,			// Max depth
		*(p + 128 + 25),							// Temp
		array_uint32_le(p + 30),					// Pre dive sample ptr
		array_uint32_le(p + 6),						// Profile sample ptr
		array_uint32_le(p + 128)					// End profile ptr
	);

	unsigned char *profile;
	int size;

	profile = read_block(&size, array_uint32_le(p+30), array_uint32_le(p+6));
	print_pre_dive(profile, size);
	free(profile);

	profile = read_block(&size, array_uint32_le(p + 6), array_uint32_le(p + 256));
	print_emc_profile(profile, size);
	free(profile);
}

void print_cmdr_1(const unsigned char * p) {
	printf("Dive Date       Time     BT    Depth Temp Pre Ptr  Pro Ptr  End Ptr\n");
		
	printf("%4d %04d-%02d-%02d %02d:%02d:%02d %2d:%02d %5.1f %3dF %08x %08x %08x\n",
		array_uint16_le(p + 70), // dive number
		*(p + 4) + 2000, *(p + 5), *(p + 2), 		// Year, Mon, Day
		*(p + 3), *p, *(p + 1),						// Hour, Min, Sec
		array_uint16_le(p + 128 + 38)/60,			// Bottom time hours
		array_uint16_le(p + 128 + 38)%60,			// Bottom time Min
		array_uint16_le(p + 128 + 40)/4.0,			// Max depth
		*(p + 128 + 25),							// Temp
		array_uint32_le(p + 30),					// Pre dive sample ptr
		array_uint32_le(p + 6),						// Profile sample ptr
		array_uint32_le(p + 128)					// End profile ptr
	);

	unsigned char *profile;
	int size;

	profile = read_block(&size, array_uint32_le(p+30), array_uint32_le(p+6));
	print_pre_dive(profile, size);
	free(profile);

	profile = read_block(&size, array_uint32_le(p + 6), array_uint32_le(p + 256));
	print_emc_profile(profile, size);
	free(profile);
}

void set_valid_dives(){

	unsigned char *log;
	unsigned int *predive_ptr, *start_dive_ptr, *end_dive_ptr, *dive_num;

	log = malloc(log_size);
	predive_ptr = malloc(sizeof(unsigned int) * max_log);
	start_dive_ptr = malloc(sizeof(unsigned int) * max_log);
	end_dive_ptr = malloc(sizeof(unsigned int) * max_log);
	dive_num = malloc(sizeof(unsigned int) * max_log);

	lseek(data, 0, SEEK_SET);

	for (int n = 0; n < max_log; n++) {
		read(data, log, log_size);

		dive_num[n] = array_uint16_le(log + 70);
		predive_ptr[n] = array_uint32_le(log + 30);
		start_dive_ptr[n] = array_uint32_le(log + 6);
		end_dive_ptr[n] = array_uint32_le(log + 128);
	}

	lseek(data, 0, SEEK_SET);

	// Now look through the data.
	//
	//
	// Find the latest dive, use dive number but ignore FFFF dives, that's
	// probably a corrupt dive
	int latest;
	for (latest = 0; latest < max_log - 1; latest++) 
		if (dive_num[latest + 1] < dive_num[latest] || dive_num[latest + 1] = 0xFFFF)
			break;

	// Now set the current ring pointer
	// from config or from this dive
	// using end-dive pointer means we might capture 

	int rb_head = end_dive_ptr[latest];

	// Now move backwards until we hit "latest", zero or a profile that after
	// wrapping around has reached rb_head
	
	// Three cases
	// 	   1 2 3 0 0 
	//     1 2 3 F F
	//     1 2 3 4 5
	//     6 2 3 4 5
	//	   6 7 8 4 5
	
	int n = latest -1;
	int profile_wrap = false;
	if (n < 0)
		n = max_log - 1;
	while (n != latest) {
		if (start_dive_ptr == 0xFFFFFFFF || start_dive_ptr == 0) {
			n--;
			if (n < 0) n = max_log - 1;
			continue;
		}

		if (profile_wrap) {
			if (start_dive_ptr[n] < rb_head) {
				// We've found the end
				n++;
				break;
			}
		} else {
			if (start_dive_ptr[n] > end_dive_pre[n]) {
				profile_wrap = true;
			}
		}
	}




		

