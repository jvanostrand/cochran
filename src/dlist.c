#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

// Cochran time stamps start at Jan 1, 1992
#define COCHRAN_EPOCH 694242000

#define array_to_uint32_le(p)   ((p)[0] + ((p)[1] << 8) + ((p)[2] << 16) + ((p)[3] << 24))
#define array_to_uint24_le(p)   ((p)[0] + ((p)[1] << 8) + ((p)[2] << 16))
#define array_to_uint16_le(p)   ((p)[0] + ((p)[1] << 8))

#define max_time(p)				( (p) > 599 ? 599 : (p) )


unsigned int log_start = 0x10000;
unsigned int profile_start = 0x1232b; // 0x1228??
int log_size = 90;
int max_log = 100;


unsigned char pt_log_start_ptr = 0;			// 3 bytes, LE
unsigned char pt_log_start_tissues = 3; 	// 12 bytes of starting tissue sats
unsigned char pt_log_start_timestamp = 15; 	// 4 bytes, LE, + 694242000 == unix ticks
unsigned char pt_log_rep_dive_count = 19;	// 1 byte, MAYBE REPET DIVE COUNT
unsigned char pt_log_dive_num = 20;		 	// 2 bytes, LE
unsigned char UNKNOWN22 = 22;				// 2 bytes
unsigned char pt_log_sit = 24;				// 2 bytes, LE,  minutes
unsigned char pt_log_voltage = 28;		 	// 2 bytes, LE, /256 = V
unsigned char UNKNOWN30 = 30;				// 5 bytes
unsigned char pt_log_end_tissues = 35;		// 12 bytes of ending tissues sats
unsigned char pt_log_bt = 47;				// 2 bytes, LE, seconds of bottom time
unsigned char pt_log_depth = 49;			// 2 bytes, LE, /4 = feet
unsigned char pt_log_depth_avg = 51;		// 2 bytes, LE, /4 = feet
unsigned char pt_log_ndl = 53;				// 2 bytes, LE, minutes, minimum NDL
unsigned char pt_log_deco_missed = 53;		// 2 bytes, LE, minutes, deco omitted
unsigned char pt_log_ceiling_max = 55; 		// 1 byte, /2 = feet, possible ceiling
unsigned char pt_log_ceiling_missed = 56;	// 1 byte, /2 = feet, possible ceiling
unsigned char pt_log_deco_max = 57; 		// 2 bytes, minutes, possible missed deco time
unsigned char UNKNOWN59 = 59;				// 9 bytes
unsigned char UNKNOWN68 = 68;				// 4 bytes, possible config settings
unsigned char pt_log_profile_interval = 72;	// 1 byte, seconds between samples
unsigned char pt_log_conservatism = 73;		// 1 byte, /2.55 = conservatism
unsigned char pt_log_o2 = 74;				// 2 bytes, LE, /256 = percent
unsigned char UNKNOWN76 = 76;				// 2 bytes, possible second gas
unsigned char UNKNOWN78 = 78;				// 3 bytes
unsigned char pt_log_temp_avg = 81;			// 1 byte, F
unsigned char pt_log_temp_min = 82; 		// 1 byte, F
unsigned char pt_log_temp_start = 83;		// 1 byte, F
unsigned char UNKNOWN84 = 84;				// 4 bytes
unsigned char pt_log_event_count = 88;		// 1 byte, # of warnings
unsigned char UNKNOWN89 = 89;				// 1 byte

unsigned int conf0_address = 0xfb80;
unsigned int pt_conf_dive_count = 0x146; // 2 bytes, BE
unsigned int pt_conf_last_log = 0x158; 	// 4 bytes, WORD_BE
unsigned int pt_conf_depth_alarm = 0x17e; 	// 2 bytes BE
unsigned int pt_conf_conservatism = 0x16d;  // 1 bytes, / 255 = %

void dump_hex( unsigned char *s, unsigned int len) {

	for (int i = 0; i < len; i++)
		printf("%02x ", s[i]);

	printf(" [");

	for (int i = 0; i < len; i++)
		printf("%3d ", s[i]);

	printf("]\n");
}

void print_log(unsigned char *log, unsigned char *sample, unsigned int size) {

	/*
	*           Dive: xxxx               Time: yy-mm-dd hh:mm:ss         Bottom time: x:xx
	*    Repet. dive: xxx           Depth-Max: xxx                 Avg: xxx                       
	*       Temp-Avg: xx                  Min:  xx                             Start: xx
	*          Gas 1: xx.xx%            Gas 2: xx.xx%               Profile interval: x
	*         Events: %xx             Voltage: x.xx                          Address: xxxxxx
	*        Min NDL: xx:xx       Max ceiling: xxx                    Missed ceiling: xxx
	*    Missed Deco: xx	         Max deco: xxx                               SIT: xx:xx
	*   Conservatism: xx
	*  Start tissues: xx xx xx xx xx xx xx xx xx xx xx xx
	*  End Tissues  : xx xx xx xx xx xx xx xx xx xx xx xx
	*      Unknown19: xx
	*      UNknown22: xx xx
	*      Unknown30: xx xx xx xx xx
	*      Unknown47: xx xx
	*      Unknown59: xx xx xx xx xx xx xx xx xx
	*      Unknown68: xx xx xx xx
	*      Unknown73: xx
	*      Unknown76: xx xx
	*      Unknown78: xx xx xx 
	*      Unknown84: xx xx xx xx
	*      Unknown89: xx
	*/

	time_t timestamp = array_to_uint24_le(log + 15) + COCHRAN_EPOCH;
	char timestr[20];
	strftime(timestr, 20, "%F %T", localtime(&timestamp));

	printf("\n\n");

	if (size > 4)
		if (sample[3] == 0xc7 || sample[4] == 0xc7) 
			printf("*** Gauge mode dive ***\n");
	
	printf("         Dive: %-4d               Time: %s         Bottom time: %01d:%02d\n",
			array_to_uint16_le(log + pt_log_dive_num), timestr, array_to_uint16_le(log + pt_log_bt) / 60, array_to_uint16_le(log + pt_log_bt) % 60);
	printf("  Repet. dive: %-3d           Depth-Max: %-3.0f                 Avg: %-3.0f\n", 
			log[pt_log_rep_dive_count], array_to_uint16_le(log + pt_log_depth) / 4.0, array_to_uint16_le(log + pt_log_depth_avg) / 4.0);
	printf("     Temp-Avg: %-3d                 Min: %-3d                             Start: %-3d\n", 
			log[pt_log_temp_avg], log[pt_log_temp_min], log[pt_log_temp_start]);
	printf("        Gas 1: %-5.2f%%            Gas 2: %-5.2f%%               Profile interval: %d\n", 
			array_to_uint16_le(log + pt_log_o2) / 256.0,  array_to_uint16_le(log + UNKNOWN76) / 256.0, log[pt_log_profile_interval]);
	printf("       Events: %-2d              Voltage: %-4.2f                          Address: %06x\n", 
			log[pt_log_event_count], array_to_uint16_le(log + pt_log_voltage) / 256.0, array_to_uint24_le(log));
	printf("      Min NDL: %2d:%02d       Max ceiling: %-3d                    Missed ceiling: %-3d\n", 
			max_time(array_to_uint16_le(log + pt_log_ndl)) / 60, max_time(array_to_uint16_le(log + pt_log_ndl)) % 60, log[pt_log_ceiling_missed] / 2, log[pt_log_ceiling_max] / 2);
	printf("  Missed Deco: %-6d         Max deco: %-3d                               SIT: %2d:%02d\n", 
			max_time(array_to_uint16_le(log + pt_log_deco_missed)), max_time(array_to_uint16_le(log + pt_log_deco_max)), 
			max_time(array_to_uint16_le(log + pt_log_sit)) / 60, max_time(array_to_uint16_le(log + pt_log_sit)) % 60);
	printf(" Conservatism: %-2.0f\n", log[pt_log_conservatism] / 2.55);
	printf("Start tissues: ");
	dump_hex(log + pt_log_start_tissues, 12);
	printf("End Tissues  : ");
	dump_hex(log + pt_log_end_tissues, 12);
	printf("    Unknown19: " );
	dump_hex(log + UNKNOWN22, 2);
	printf("    Unknown30: "); 
	dump_hex(log + UNKNOWN30, 5);
	printf("    Unknown47: "); 
	dump_hex(log + UNKNOWN59, 9);
	printf("    Unknown68: "); 
	dump_hex(log + UNKNOWN68, 4);
	printf("    Unknown76: "); 
	dump_hex(log + UNKNOWN76, 2);
	printf("    Unknown78: "); 
	dump_hex(log + UNKNOWN78, 3);
	printf("    Unknown84: "); 
	dump_hex(log + UNKNOWN84, 4);
	printf("    Unknown89: "); 
	dump_hex(log + UNKNOWN89, 1);
	printf("    timestamp: ");
	dump_hex(log + 15, 4);
}

void usage(char *name) {
	dprintf(STDERR_FILENO, "Usage: %s file\n", name);
	dprintf(STDERR_FILENO, "Where: file      Memory file from Commander TM\n");
}

int main(int argc, char *argv[]) {
	char *infile = NULL;

	if (argc != 2) {
		usage(argv[0]);
		exit(1);
	}

	infile = argv[1];

	int infd = open(infile, O_RDONLY);
	if (infd < 0) {
		dprintf(STDERR_FILENO, "Error (%d) opening file. %s\n", errno, strerror(errno));
		exit(2);
	}

	//lseek(infd, 0x10000, SEEK_SET);
	unsigned char buf[32768];
	
	read(infd, buf, 32768);

	// Read data
	for (unsigned char x = 0; x < 100; x++) {
		unsigned char *l = buf + x * 90;

		if (x == array_to_uint16_le(l + 20)) {
			unsigned int start = array_to_uint24_le(l);
			unsigned int end = 0;
			if (x == 99)
				end = array_to_uint24_le(buf);
			else
				end = array_to_uint24_le(l + 90);
			
			print_log(l, buf + start, end - start);
		}

		/*
  		for (int r=0; r < 6; r++) {
			printf ("%08x  ", r * 16);
			for (int c = 0; c < 16; c++) {
				if (c == 8) printf(" ");
				printf("%02x ", buf[r*16 + c]);
			}
			printf("\n");
		}
		*/
	}

	close(infd);

	exit(0);
}

