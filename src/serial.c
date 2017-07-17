

#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/ioctl.h>

typedef enum dc_model_t {
	MODEL_UNDEFINED,
	MODEL_COMMANDER_TM,
	MODEL_COMMANDER_I,
	MODEL_COMMANDER_II,
	MODEL_EMC,
} dc_model_t;

typedef enum read_mode_t {
	MODE_UNDEFINED,
	MODE_ID0,
	MODE_ID1,
	MODE_CONF0,
	MODE_CONF1,
	MODE_MISC,
	MODE_ADDRESS,
	MODE_FULL,
} read_mode_t;

#define uint32_to_array_le(buf, n) ((buf)[0] = n & 0xff, \
									(buf)[1] = (n >> 8) & 0xff, \
									(buf)[2] = (n >> 16) & 0xff, \
									(buf)[3] = (n >> 24) & 0xff )
#define uint24_to_array_le(buf, n) ((buf)[0] = n & 0xff, \
									(buf)[1] = (n >> 8) & 0xff, \
									(buf)[2] = (n >> 16) & 0xff )
#define uint16_to_array_le(buf, n) ((buf)[0] = n & 0xff, \
									(buf)[1] = (n >> 8) & 0xff )

int wait_for_aa(int fd);
void printhex(unsigned char*, int);
int main(int argc, char *argv[]);

void set_baud(int fd, int baud) {
	struct termios newtio;

	tcgetattr(fd, &newtio);
	cfsetospeed(&newtio, baud);
	cfsetispeed(&newtio, baud);

	tcsetattr(fd, TCSANOW, &newtio);
}

int open_dc(const char *file) {
	int fd;

	dprintf(STDERR_FILENO, "Opening %s...\n", file);

	struct termios newtio;
	fd = open("/dev/ttyUSB0", O_RDWR | O_NONBLOCK | O_NOCTTY);

	if (fd == -1) {
		dprintf(STDERR_FILENO, "UNable to open serial port\n");
		return fd;
	}

	tcgetattr(fd, &newtio);
	cfsetospeed(&newtio, B9600);
	cfsetispeed(&newtio, B9600);
	
	newtio.c_cflag |= CLOCAL | CREAD;

	// Set N, 8, 1
	newtio.c_cflag &= ~PARENB;
	newtio.c_cflag &= ~CSTOPB;
	newtio.c_cflag &= ~CSIZE;
	newtio.c_cflag |= CS8;

	// Set no flow control
	newtio.c_cflag &= ~CRTSCTS;
	newtio.c_iflag &= ~(IXON | IXOFF | IXANY);
	
	newtio.c_iflag |= IGNPAR | IGNBRK;

	newtio.c_oflag &= ~ONLCR;

	//newtio.c_lflag = 0;

	tcsetattr(fd, TCSANOW, &newtio);

	return fd;
}

int msleep(unsigned int millis) {
	struct timespec ts;

	ts.tv_sec  = (millis / 1000);
	ts.tv_nsec = (millis % 1000) * 1000000;
	while (nanosleep (&ts, &ts) != 0) {
		int errcode = errno;
		if (errcode != EINTR ) {
			return errcode;
		}
	}

	return 0;
}

int write_dc(int fd, const unsigned char *buf, unsigned int size, unsigned int hb) {

	dprintf(STDERR_FILENO, "Sending %d byte command\n", size);

	if (hb && wait_for_aa(fd))
		return -1;

	for (int x = 0; x < size; x++) {
		dprintf(STDERR_FILENO, "- %02hhx ", buf[x]);
		if (write(fd, buf + x, 1) != 1)
			return errno;
		msleep(16);
	}
	dprintf(STDERR_FILENO, "\n");

	return 0;
}

int read_dc(int fd, char *buf, unsigned int size) {

	int result_bytes = size;
	int readcnt = 0, bufptr = 0;
	while (bufptr < result_bytes)
	{
		readcnt = read(fd, buf + bufptr, result_bytes - bufptr);

		if (readcnt < 0 && errno != 11) dprintf(STDERR_FILENO, "readcnd less than zero: (%d) %s\n", errno, strerror(errno));
		
		if (readcnt > 0) {
//			printhex(&(buf[bufptr]), readcnt);
			bufptr += readcnt;
			if ((bufptr % 1024) == 0) 
				dprintf(STDERR_FILENO, ".");
		}
		msleep(16);
	}

	dprintf(STDERR_FILENO, "Read %d bytes\n", bufptr);
	return bufptr;
}


void usage(const char *name) {

	dprintf(STDERR_FILENO, "Usage: %s -m <model> [-ijcde | -a <addresss> -s <size>] -o file\n", name);
	dprintf(STDERR_FILENO, "Where: model is one of Commander-TM, Commander-I, Commander-II or EMC\n");
	dprintf(STDERR_FILENO, "       -i            Read ID0 block\n");
	dprintf(STDERR_FILENO, "       -j            Read ID1 block\n");
	dprintf(STDERR_FILENO, "       -c            Read conf0 block\n");
	dprintf(STDERR_FILENO, "       -d            Read conf1 block\n");
	dprintf(STDERR_FILENO, "       -e            Read Misc block\n");
	dprintf(STDERR_FILENO, "       -f            Read all memory\n");
	dprintf(STDERR_FILENO, "       -a <address>  Read memory at the address\n");
	dprintf(STDERR_FILENO, "       -s <size>     Read size bytes\n");
	dprintf(STDERR_FILENO, "       -o <file>     Output to file\n");
}

int main(int argc, char * argv[])
{
	dc_model_t model = MODEL_UNDEFINED;
	read_mode_t mode = MODE_UNDEFINED;
	unsigned int address = 0xFFFFFFFF, size = 0;
	unsigned char *outfile;
	unsigned char cmd[10];
	int cmd_size = 0, result_size = 0;
	int highbaud = 9600;

	int c; 
	while ((c = getopt(argc, argv, "m:ijcdefa:s:o:")) != -1) {
		switch (c) {
		case 'm': 	// Set mode
			if (!strcmp(optarg, "Commander-TM"))
				model = MODEL_COMMANDER_TM;
			else if (!strcmp(optarg, "Commander-I"))
				model = MODEL_COMMANDER_I;
			else if (!strcmp(optarg, "Commander-II"))
				model = MODEL_COMMANDER_II;
			else if (!strcmp(optarg, "EMC"))
				model = MODEL_EMC;
			break;
		case 'i':	// Read ID block
			mode = MODE_ID0;
			cmd_size = 6;
			memcpy(cmd, "\x05\x9D\xFF\x00\x43\x00", cmd_size);
			result_size = 0x43;
			break;
		case 'j':	// Read ID block
			mode = MODE_ID1;
			cmd_size = 6;
			memcpy(cmd, "\x05\xBD\x7F\x00\x43\x00", cmd_size);
			result_size = 0x43;
			break;
		case 'c':	// Read conf0 block
			mode = MODE_CONF0;
			cmd_size = 2;
			memcpy(cmd,"\x96\x00", cmd_size);
			result_size = 512;
			break;
		case 'd':	// Read conf1 block
			mode = MODE_CONF1;
			cmd_size = 2;
			memcpy(cmd,"\x96\x01", cmd_size);
			result_size = 512;
			break;
		case 'e':	// Read Misc block
			mode = MODE_MISC;
			cmd_size = 6;
			memcpy(cmd, "\x05\xE0\x03\x00\xDC\x05", cmd_size);
			result_size = 1500;
			break;
		case 'f':	// Full read
			mode = MODE_FULL;
			break;
		case 'a':	// Read address
			mode = MODE_ADDRESS;
			address = atoi(optarg);
			break;
		case 's':	// Read size
			size = atoi(optarg);
			break;
		case 'o': 	// Output file
			outfile = optarg;
			break;
		default:
			usage(argv[0]);
			exit(1);
			break;
		}
	}

	if (mode == MODE_ADDRESS) {
		if (address == 0xFFFFFFFF ) { // || size == 0) {
			dprintf(STDERR_FILENO, "Address and size must be specified in address mode.\n");
			usage(argv[0]);
			exit(1);
		}

		// Create command
		switch (model) {
		case MODEL_COMMANDER_TM:
			cmd[0] = 0x05;
			uint24_to_array_le(cmd + 1, address);
			uint16_to_array_le(cmd + 4, size);
			cmd_size = 6;
			result_size = size;
			break;
		case MODEL_COMMANDER_I:
		case MODEL_COMMANDER_II:
			cmd[0] = 0x15;
			uint24_to_array_le(cmd + 1, address);
			uint16_to_array_le(cmd + 4, size);
			cmd[6] = 0x05;
			cmd_size = 6;
			result_size = size;
			highbaud = B19200;
			break;
		case MODEL_EMC:
			cmd[0] = 0x15;
			uint32_to_array_le(cmd + 1, address);
			uint32_to_array_le(cmd + 5, size);
			cmd[9] = 0x04;
			cmd_size = 10;
			result_size = size;
			highbaud = B115200;
			break;
		}
	} else if (mode == MODE_FULL) {
		switch (model) {
		case MODEL_COMMANDER_TM:
			result_size = 131072;
			break;
		case MODEL_COMMANDER_I:
			result_size = 131072;
			break;
		case MODEL_COMMANDER_II:
			result_size = 131072;
			break;
		case MODEL_EMC:
			result_size = 131072;
			break;
		}
	}

	unsigned char *buf;

	buf = (unsigned char *) malloc(result_size);
	if (buf == 0) {
		dprintf(STDERR_FILENO, "Unable to allocate buffer\n");
		exit(2);
	}

	int readcnt;
	int x;

	if (optind >= argc) {
		dprintf(STDERR_FILENO, "Device name must be spcified\n");
		exit(3);
	}
	
	int fd = open_dc(argv[optind]);
	if (fd < 0) exit(1);

	int of = STDOUT_FILENO;
	if (outfile) {
		of = open(outfile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	}

	if (of < 0) {
		dprintf(STDERR_FILENO, "Error opening file (%d) %s\n", errno, strerror(errno));
		exit(1);
	}

	if (mode == MODE_MISC) {
		// Send 0x89 command first (probably to store ram to flash
		write_dc(fd, "\x89", 1, 1);
	}

	if (mode == MODE_FULL) {
		for (int a = 0; a < result_size; a += 32768) {
			int s = 32768;
			if (a + 32768 > result_size) 
				s = a % 32768;
			cmd[0] = 0x05;
			cmd[1] = a & 0xff;
			cmd[2] = (a >> 8) & 0xff;
			cmd[3] = (a >> 16) & 0xff; 
			cmd[4] = s & 0xff;
			cmd[5] = (s >> 8) & 0xff;
			write_dc(fd, cmd, 6, !a);

			// Change baud if needed
			if (highbaud != B9600)
				set_baud(fd, highbaud);

			int result = read_dc(fd, buf + a, s);
			if (result != s) {
				dprintf(STDERR_FILENO, "Error reading from DC (%d) %s\n", errno, strerror(errno));
				exit(1);
			}

			if (highbaud != B9600)
				set_baud(fd, B9600);
		}
		write(of, buf, result_size);
	} else {
		write_dc(fd, cmd, cmd_size, 1);

		// Change baud if needed
		if (highbaud != B9600)
			set_baud(fd, highbaud);

		int result = read_dc(fd, buf, result_size);

		write(of, buf, result);
	}

	close(fd);
	close(of);

	return(0);
}


void printhex(unsigned char *buf, int len)
{
	int ptr = 0;
	int lineptr;
    unsigned char ascii[17];

	ascii[16] = 0;

	while (ptr < len)
	{
		lineptr = 0;
		dprintf(STDERR_FILENO, "%04x  ", ptr); // print byte count
		while (ptr < len && lineptr < 16) {
			if (lineptr == 8)
				dprintf(STDERR_FILENO, "    %02X ", buf[ptr]);
			else
				dprintf(STDERR_FILENO, "%02X ", buf[ptr]);

			// Build ascii representation
			if (buf[ptr] >31 && buf[ptr] < 127)
				ascii[lineptr] = buf[ptr];
			else
				ascii[lineptr] = '.';
			ptr++;
			lineptr++;
		}

		if (lineptr < 16) ascii[lineptr] = 0;
		dprintf(STDERR_FILENO, " %s \n", ascii);
	}
}


int wait_for_aa(int fd) {
	int ret, count = 0;
	unsigned char buf[4096];
	
	ioctl(fd, TIOCSBRK, NULL);
	msleep(16);
	ioctl(fd, TIOCCBRK, NULL);
	tcflush(fd, TCIOFLUSH);
	write(fd, "\x00", 1);
	usleep(16000);

	while (count++ < 200) {
		ret = read(fd, buf, 1);
		if (ret > 0) {
//			dprintf(STDERR_FILENO, "%x ", buf[0]);
			return 0;
			if (buf[0] == 0xaa) return 0;
		}
//		dprintf(STDERR_FILENO, ".");
		usleep(50000);
	}
	return 1;
}
