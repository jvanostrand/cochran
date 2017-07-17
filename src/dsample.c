#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>


#define COCHRAN_EPOCH 694242000
#define METRES(f) ((f) * .3048)
#define CELSIUS(f) (((f) - 32) / 1.8)


#define uint32_to_array_le(p)	((p)[0] + ((p)[1] << 8) + ((p)[2] << 16) + ((p)[3] << 24))
#define uint24_to_array_le(p)	((p)[0] + ((p)[1] << 8) + ((p)[2] << 16))
#define uint16_to_array_le(p)	((p)[0] + ((p)[1] << 8))


void usage(char *name) {
	dprintf(STDERR_FILENO, "Usage: %s -f infile (-n dive | -a address -s size) [-o outfile]\n", name);
	dprintf(STDERR_FILENO, "Where: infile      File containg dive profile data\n");
	dprintf(STDERR_FILENO, "       dive        Dive number (origin 0) to sample\n");
	dprintf(STDERR_FILENO, "       address     infile offset address in decimal, octal or hexadecimal\n");
	dprintf(STDERR_FILENO, "       size        size of samples in bytes in decimal, octal or hexadecimal\n");
	dprintf(STDERR_FILENO, "       outfile     File to output to\n");
}

int main(int argc, char *argv[]) {


	unsigned int address = 0xffffffff, size = 0, dive_num = 0;
	char *infile = NULL, *outfile = NULL;

	int c;
	while ((c = getopt(argc, argv, "a:f:n:o:s:")) != -1) {
		switch (c) {
		case 'a':	// Address to start
			address = atoi(optarg);
			break;
		case 'n':	// Number of dive to use
			dive_num = atoi(optarg);
			break;
		case 'f':	// in file
			infile = optarg;
			break;
		case 'o':	// out file
			outfile = optarg;
			break;
		case 's':	// Size in bytes
			size = atoi(optarg);
			break;
		default:
			dprintf(STDERR_FILENO, "Invalid option\n");
			usage(argv[0]);
			exit(1);
		}
	}

	
	if (!infile || (!dive_num && (!address || !size))) {
		usage(argv[0]);
		exit(1);
	}

	int infd = open(infile, O_RDONLY);
	if (infd < 0) {
		dprintf(STDERR_FILENO, "Error (%d) opening file. %s\n", errno, strerror(errno));
		exit(2);
	}


	int outfd = STDOUT_FILENO;
	if (outfile) {
		outfd = open(outfile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (outfd < 0) {
			dprintf(STDERR_FILENO, "Error (%d) creating file. %s\n", errno, strerror(errno));
			exit(3);
		}
	}

	// Start XML
	dprintf(outfd, "<device>\n<dive>\n");

	if (dive_num) {
		lseek(infd, 90 * dive_num, SEEK_SET);
		unsigned char dive[90], next[90];
		int rc = read(infd, dive, 90);
		if (rc != 90) {
			dprintf(STDERR_FILENO, "Error (%d) reading file. %s\n", errno, strerror(errno));
			exit(3);
		}
		// TODO don't assume the next dive follows, use ringbuffer
		rc = read(infd, next, 90);
		if (rc != 90) {
			dprintf(STDERR_FILENO, "Error (%d) reading file. %s\n", errno, strerror(errno));
			exit(3);
		}

		// Get address and size
		address = uint24_to_array_le(dive);
		unsigned int end_address = uint24_to_array_le(next);
		size = uint24_to_array_le(next) - address;
		time_t date = uint32_to_array_le(dive + 15) + COCHRAN_EPOCH;
		struct tm t;
		localtime_r(&date, &t);
		char date_str[210];
		strftime(date_str, 20, "%F %T", &t);
		// Print out header
		dprintf(outfd, "<number>%d</number>\n", uint16_to_array_le(dive + 20));
		dprintf(outfd, "<size start=\"%06x\" end=\"%06x\" byte=\"%02x\" >%d</size>\n", address, end_address, *next, size);
		dprintf(outfd, "<fingerprint>%08x</fingerprint>\n", date);
		dprintf(outfd, "<datetime>%s</datetime>\n", date_str);
		dprintf(outfd, "<maxdepth>%.2f</maxdepth>\n", METRES(uint16_to_array_le(dive + 49) / 4));
		dprintf(outfd, "<temperature type=\"minimum\">%.1f</temperature>\n", CELSIUS(dive[82]));
	} else {
		dprintf(outfd, "<number>1</number>\n");
	}

	lseek(infd, address, SEEK_SET);

	// Read data
	unsigned char *buf = malloc(size);
	read(infd, buf, size);
	close(infd);

	unsigned int sample_interval = 4; // seconds
	unsigned int deco_ceiling = 0;

	int temp = buf[0];
	int depth = buf[1];

//dprintf(outfd, "%02x %02x : ", buf[0], buf[1]);
//dprintf(outfd, "Start depth: %d, start temp %d\n", depth, temp);
	int i = 2;
	int time = 0;
	while (i < size) {
		unsigned char *s = buf + i;

		dprintf(outfd, "<sample>\n\t<time>%02d:%02d</time>\n", time / 60, time % 60);

		if (*s & 0x80) {
			dprintf(outfd, "\t<depth>%.2f</depth>\n", METRES(depth / 2.0));
			if (*s & 0x60) {
				// Event
				switch (*s) {
				case 0xce:	// NDL warning
					break;
				case 0xc4:	// Depth alarm
					dprintf(outfd, "\t<event type=\"18\" time=\"0\" flags=\"0\" value=\"0\">maxdepth</event>\n");
					break;
				case 0xc5:	// Entered Deco
					dprintf(outfd, "\t<deco time=\"1\" depth=\"0\">deco</deco>\n");
					break;
				case 0xab:	// Lower ceiling
					deco_ceiling += 10;
					dprintf(outfd, "\t<deco depth=\"%.2f\">deco</deco>\n", METRES(deco_ceiling));
					break;
				case 0xad:	// Raise ceiling
					deco_ceiling -= 10;
					dprintf(outfd, "\t<deco depth=\"%.2f\">deco</deco>\n", METRES(deco_ceiling));
					break;
				default:	// Unknown
					dprintf(outfd, "\t<event type=\"18\" time=\"0\" flags=\"0\" value=\"%0x\">unknown</event>\n", *s);
					break;
				}
			} else {
				// temp change
				if (*s & 0x10)
					temp -= (*s & 0x0f);
				else
					temp += (*s & 0x0f);
				dprintf(outfd, "\t<temperature>%.1f</temperature>\n", CELSIUS(temp / 2.0));
			}
			i++;
			dprintf(outfd, "</sample>\n");
			continue;
		}

		if (s[0] & 0x40) 
			depth -= s[0] & 0x3f;
		else
			depth += s[0] & 0x3f;

		dprintf(outfd, "\t<depth>%.2f</depth>\n</sample>\n", METRES(depth / 2.0));
//dprintf(outfd, "%02x %02x : ", s[0], s[1]);

//dprintf(outfd, "%6.2f  %6.2f\n", depth /2.0 , temp / 4.0);

		i++;
		time += sample_interval;
	}

	dprintf(outfd, "</dive>\n</device>\n");

	free(buf);
	close(outfd);

	exit(0);
}
