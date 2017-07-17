/*
 * list.c
 *
 * List cochran dives from a dump file.
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
#define array_uint24_le(p) ( (unsigned int) (p)[0] + ((p)[1] << 8) + ((p)[2] << 16) ) 
#define array_uint32_le(p) ( (unsigned int) (p)[0] + ((p)[1] << 8) \
										 + ((p)[2] << 16) + ((p)[3] << 24) )
#define COCHRAN_EPOCH 694242000

typedef enum { DC_UNKNOWN, DC_EMC, DC_CMDR_II, DC_CMDR_I, DC_CMDR_TM } dc_type;

void print_emc(const unsigned char *p);
void print_cmdr_II(const unsigned char *p);
void print_cmdr_I(const unsigned char *p);
void print_cmdr_tm(const unsigned char *p);


void usage(char *progname) {

	printf("Usage: %s [-e | -c] dump_file\n", progname);
	printf("Where: -e         Dump is from an EMC model\n");
	printf("       -c         Dump is from a Commander II model\n");
	printf("       -d         Dump is from a Commander I model (pre-21000)\n");
	printf("       -t         Dump is from a Commander TM model (circa 1995)\n");
	printf("       dump_file  Is the file containing the dive computer dump.\n");
	exit(1);
}


int main(int argc, char *argv[]) {
	dc_type dc = DC_UNKNOWN;
	int max_log, log_size;
	int data;

	if (argc < 2) usage(argv[0]);

	if (!strcmp(argv[1], "-e")) {
		dc = DC_EMC;
		max_log = 512;
		log_size = 512;
	} else if (!strcmp(argv[1], "-c")) {
		dc = DC_CMDR_II;
		max_log = 512;
		log_size = 256;
	} else if (!strcmp(argv[1], "-d")) {
		dc = DC_CMDR_I;
		max_log =  512;
		log_size = 256;
	} else if (!strcmp(argv[1], "-t")) {
		dc = DC_CMDR_TM;
		max_log = 90;
		log_size = 90;
	}

	if (dc == DC_UNKNOWN) usage(argv[0]);

	data = open(argv[2], 'r');
	if (!data) {
		printf("Unable to open %s for reading\n%s\n", argv[2], strerror(errno));
		exit(2);
	}

	unsigned char *log = malloc(log_size);

	for (int n = 0; n < max_log; n++) {
		if (read(data, log, log_size) != log_size) {
			printf("error reading file\n");
			exit(3);
		}

		if (dc != DC_CMDR_TM && array_uint32_le(log) == 0xFFFFFFFF)
			continue;

		switch (dc) {
		case DC_EMC:
			print_emc(log);
			break;
		case DC_CMDR_II:
			print_cmdr_II(log);
			break;
		case DC_CMDR_I:
			print_cmdr_I(log);
			break;
		case DC_CMDR_TM:
			print_cmdr_tm(log);
			break;
		}
	}

	close(data);

	exit(0);
}


void print_emc(const unsigned char *p) {
	static int count = 0;

	if ((count % 25) == 0)
		printf("  #   Dive Date       Time     BT    Depth Temp Pre Ptr  Pro Ptr  End Ptr\n");

	printf("[%3d] %4d %04d-%02d-%02d %02d:%02d:%02d %2d:%02d %5.1f %3dF %08x %08x %08x\n",
		count,
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

	count++;
}


void print_cmdr_II(const unsigned char * p) {
	static int count = 0;

	if ((count % 25) == 0)
		printf("  #   Dive Date       Time     BT    Depth Temp Pre Ptr  Pro Ptr  End Ptr\n");

	printf("[%3d] %4d %04d-%02d-%02d %02d:%02d:%02d %2d:%02d %5.1f %3dF %08x %08x %08x\n",
		count,
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

	count++;
}

void print_cmdr_I(const unsigned char * p) {
	static int count = 0;
	time_t timestamp;
	struct tm t;

	timestamp = array_uint32_le(p + 8) + COCHRAN_EPOCH;
	localtime_r(&timestamp, &t);

	if ((count % 25) == 0)
		printf("  #   Dive Date       Time     BT    Depth Temp Pre Ptr  Pro Ptr  End Ptr\n");

	printf("[%3d] %4d %04d-%02d-%02d %02d:%02d:%02d %2d:%02d %5.1f %3dF %08x %08x %08x\n",
		count,
		array_uint16_le(p + 68), // dive number
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, 	// Year, Mon, Day
		t.tm_hour, t.tm_min, t.tm_sec,				// Hour, Min, Sec
		array_uint16_le(p + 128 + 38)/60,			// Bottom time hours
		array_uint16_le(p + 128 + 38)%60,			// Bottom time Min
		array_uint16_le(p + 128 + 40)/4.0,			// Max depth
		*(p + 128 + 25),							// Temp
		array_uint32_le(p + 28),					// Pre dive sample ptr
		array_uint32_le(p + 0),						// Profile sample ptr
		array_uint32_le(p + 128)					// End profile ptr
	);

	count++;
}

void print_cmdr_tm(const unsigned char * p) {
	static int count = 0;
	time_t timestamp;
	struct tm t;

	timestamp = array_uint32_le(p + 15) + COCHRAN_EPOCH;
	localtime_r(&timestamp, &t);

	if ((count % 25) == 0)
		printf("  #   Dive Date       Time        BT Depth Temp Pro Ptr\n");

	printf("[%3d] %4d %04d-%02d-%02d %02d:%02d:%02d %2d:%02d %5.1f %3dF %06x\n",
		count,
		array_uint16_le(p + 20), // dive number
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, 	// Year, Mon, Day
		t.tm_hour, t.tm_min, t.tm_sec,				// Hour, Min, Sec
		array_uint16_le(p + 47)/60,					// Bottomtime minutes
		array_uint16_le(p + 47)%60,					// Bottomtime minutes
		array_uint16_le(p + 49)/4.0,				// Max depth
		*(p + 82),									// Temp
		array_uint24_le(p)							// Profile sample ptr
	);

	count++;
}
