

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
#include <linux/serial.h>

typedef enum model_id_t {
	MODEL_UNDEFINED,
	MODEL_COMMANDER_TM,
	MODEL_COMMANDER_I,
	MODEL_COMMANDER_II,
	MODEL_EMC_14,
	MODEL_EMC_16,
	MODEL_EMC_20H,
} model_id_t;

typedef enum family_id_t {
	FAMILY_COMMANDER_TM = 0,
	FAMILY_COMMANDER = 1,
	FAMILY_EMC = 2,
} family_id_t;

typedef enum read_mode_t {
	MODE_UNDEFINED,
	MODE_ID,
	MODE_CONF0,
	MODE_CONF1,
	MODE_MISC,
	MODE_RAM,
	MODE_ADDRESS,
	MODE_FULL,
} read_mode_t;

#define uint32_to_array_le(buf, n) ((buf)[0] = (n) & 0xff, \
									(buf)[1] = ((n) >> 8) & 0xff, \
									(buf)[2] = ((n) >> 16) & 0xff, \
									(buf)[3] = ((n) >> 24) & 0xff )
#define uint24_to_array_le(buf, n) ((buf)[0] = (n) & 0xff, \
									(buf)[1] = ((n) >> 8) & 0xff, \
									(buf)[2] = ((n) >> 16) & 0xff )
#define uint16_to_array_le(buf, n) ((buf)[0] = (n) & 0xff, \
									(buf)[1] = ((n) >> 8) & 0xff )

#define C_ARRAY_SIZE(array) (sizeof (array) / sizeof *(array))

#define UNSUPPORTED  0xFFFFFFFF

typedef struct device_t {
	unsigned char *name;
	model_id_t model;
	family_id_t family;
	unsigned int baud;
	unsigned int highbaud;
	unsigned int highbaud_byte;
	unsigned int ram_address;
	unsigned int ram_size;
	unsigned int log_address;
	unsigned int log_size;
} device_t;

#define F_COMMANDER_TM FAMILY_COMMANDER_TM, 9600, UNSUPPORTED, UNSUPPORTED,  0,          0x10000
#define F_COMMANDER    FAMILY_COMMANDER,    9600, 115200,      0x04,         0,          0x10000
#define F_EMC          FAMILY_EMC,          9600, 806400,      0x05,         0,          0x10000

device_t devices[] = {
	// name            model               family          log_address log_size
	{ "Commander TM",  MODEL_COMMANDER_TM, F_COMMANDER_TM, 0x10000,    0x10000  },
	{ "Commander I",   MODEL_COMMANDER_I,  F_COMMANDER,    0,          0x100000 },
	{ "Commander II",  MODEL_COMMANDER_II, F_COMMANDER,    0,          0x100000 },
	{ "EMC 14",        MODEL_EMC_14,       F_EMC,          0,          0x100000 },
	{ "EMC 16",        MODEL_EMC_14,       F_EMC,          0,          0x100000 },
	{ "EMC 20H",       MODEL_EMC_14,       F_EMC,          0,          0x100000 },
};



int open_serial(const char *file) {
	int fd;

	dprintf(STDERR_FILENO, "Opening %s\n", file);

	struct termios newtio;
	fd = open(file, O_RDWR | O_NONBLOCK | O_NOCTTY);

	if (fd == -1) {
		dprintf(STDERR_FILENO, "Unable to open serial port\n");
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

	newtio.c_oflag &= ~(ONLCR | OPOST);

	newtio.c_lflag &= ~(ISIG | ICANON | ECHO);

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

int set_baud(int fd, unsigned int baud_rate) {
	speed_t baud;
	int custom_baud = 0;
	struct termios tty;

	memset (&tty, 0, sizeof (tty));
	if (tcgetattr (fd, &tty) != 0) {
		return errno;
	}

	msleep(45);	// Give time to process received data

	switch (baud_rate) {
	case 0:		baud = B0; break;
	case 50:	baud = B50; break;
	case 75:	baud = B75; break;
	case 110:	baud = B110; break;
	case 134:	baud = B134; break;
	case 150:	baud = B150; break;
	case 200:	baud = B200; break;
	case 300:	baud = B300; break;
	case 600:	baud = B600; break;
	case 1200:	baud = B1200; break;
	case 1800:	baud = B1800; break;
	case 2400:	baud = B2400; break;
	case 4800:	baud = B4800; break;
	case 9600:	baud = B9600; break;
	case 19200: baud = B19200; break;
	case 38400: baud = B38400; break;
#ifdef B57600
	case 57600: baud = B57600; break;
#endif
#ifdef B115200
	case 115200: baud = B115200; break;
#endif
#ifdef B230400
	case 230400: baud = B230400; break;
#endif
#ifdef B460800
	case 460800: baud = B460800; break;
#endif
#ifdef B500000
	case 500000: baud = B500000; break;
#endif
#ifdef B576000
	case 576000: baud = B576000; break;
#endif
#ifdef B921600
	case 921600: baud = B921600; break;
#endif
#ifdef B1000000
	case 1000000: baud = B1000000; break;
#endif
#ifdef B1152000
	case 1152000: baud = B1152000; break;
#endif
#ifdef B1500000
	case 1500000: baud = B1500000; break;
#endif
#ifdef B2000000
	case 2000000: baud = B2000000; break;
#endif
#ifdef B2500000
	case 2500000: baud = B2500000; break;
#endif
#ifdef B3000000
	case 3000000: baud = B3000000; break;
#endif
#ifdef B3500000
	case 3500000: baud = B3500000; break;
#endif
#ifdef B4000000
	case 4000000: baud = B4000000; break;
#endif
	default:
	    baud = B38400; /* Required for custom baudrates on linux. */
	    custom_baud = 1;
	    break;
	}

	// Set baud
	if (cfsetispeed (&tty, baud) != 0 ||
		cfsetospeed (&tty, baud) != 0) {
		return errno;
	}

	// Apply the new settings.
	if (tcsetattr (fd, TCSANOW, &tty) != 0) {
#if 0 // who cares
		return errno;
#endif
	}

	// set custom baud
	if (custom_baud) {
#if defined(TIOCGSERIAL) && defined(TIOCSSERIAL) && !defined(__ANDROID__)
		// Get the current settings.
		struct serial_struct ss;
		if (ioctl (fd, TIOCGSERIAL, &ss) != 0) {
			return errno;
		}

		// Set the custom divisor.
		ss.custom_divisor = ss.baud_base / baud_rate;
		ss.flags &= ~ASYNC_SPD_MASK;
		ss.flags |= ASYNC_SPD_CUST;

		// Apply the new settings.
		if (ioctl (fd, TIOCSSERIAL, &ss) != 0) {
			return errno;
		}
#elif defined(IOSSIOSPEED)
		speed_t speed = baudrate;
		if (ioctl (fd, IOSSIOSPEED, &speed) != 0) {
			return errno;
		}
#else
		// Custom baudrates are not supported.
		return -1;
#endif
	}
    
	return 0;
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
	unsigned char buf[1];

	ioctl(fd, TIOCSBRK, NULL);
	msleep(16);
	ioctl(fd, TIOCCBRK, NULL);
	tcflush(fd, TCIOFLUSH);
	write(fd, "\x00", 1);
	usleep(16000);

	while (count++ < 200) {
		ret = read(fd, buf, 1);
		if (ret > 0) {
			return 0;
			if (buf[0] == 0xaa) return 0;
		}
		usleep(50000);
	}
	return 1;
}

int write_serial(int fd, const unsigned char *buf, unsigned int size, unsigned int hb) {

	dprintf(STDERR_FILENO, "Sending %d byte command\n", size);

	if (hb && wait_for_aa(fd))
		return -1;

	for (unsigned int x = 0; x < size; x++) {
		dprintf(STDERR_FILENO, "- %02hhx ", buf[x]);
		if (write(fd, buf + x, 1) != 1)
			return errno;
		msleep(16);
	}
	dprintf(STDERR_FILENO, "\n");

	return 0;
}


int read_serial(int fd, unsigned char *buf, unsigned int size) {

	int result_bytes = size;
	int readcnt = 0, bufptr = 0, progress = 0;

	// Hacker progress bar
	unsigned int tick_size = size / 64;
	if (tick_size < 1024) tick_size = 1024;
	unsigned int ticks = size / tick_size;
	unsigned char  *blank = "                                                                "; // 64 spaces

	if (ticks > 1) {
		dprintf(STDERR_FILENO, "[%s]\r[", blank + (64 - ticks));
	}

	while (bufptr < result_bytes)
	{
		readcnt = read(fd, buf + bufptr, result_bytes - bufptr);

		if (readcnt < 0 && errno != 11) dprintf(STDERR_FILENO, "readcnd less than zero: (%d) %s\n", errno, strerror(errno));

		if (readcnt > 0) {
			bufptr += readcnt;
			progress += readcnt;
			if (progress >= tick_size) {
				progress -= tick_size;
				dprintf(STDERR_FILENO, ".");
			}
		}
		msleep(16);
	}

	dprintf(STDERR_FILENO, "\nRead %d bytes\n", bufptr);
	return errno;
}


int do_cmd(device_t *device, int fd, unsigned char *cmd, unsigned int cmd_size, unsigned char *buf, unsigned int size) {

	if (write_serial(fd, cmd, cmd_size, 1)) {
		dprintf(STDERR_FILENO, "Error (%d) reading. %s\n", errno, strerror(errno));
		return errno;
	}

	return read_serial(fd, buf, size);
}


int do_cmd_high_baud(device_t *device, int fd, unsigned char *cmd, unsigned int cmd_size, unsigned char *buf, unsigned int size) {
	int rc;

	if (write_serial(fd, cmd, cmd_size, 1)) {
		dprintf(STDERR_FILENO, "Error (%d) reading. %s\n", errno, strerror(errno));
		return errno;
	}

	// Change baud
	if (set_baud(fd, device->highbaud))
		return errno;

	rc = read_serial(fd, buf, size);

	// Change baud
	if (set_baud(fd, device->baud))
		return errno;

	return rc;
}


int read_low_baud(device_t *device, int fd, unsigned int address, unsigned int read_size, unsigned char *buf, unsigned int size) {
	unsigned char cmd[6];
	unsigned int cmd_size = 6;

	cmd[0] = 0x05;

	unsigned int blk_size = 0x10000;

	for (unsigned int i = 0; i < read_size; i += blk_size) {
		int rc = 0;
		// Load address into command
		uint24_to_array_le(cmd + 1, address + i);

		if (read_size - i < blk_size)
			read_size -= i;

		// Load read size into command
		uint16_to_array_le(cmd + 4, read_size);

		if ((rc = do_cmd(device, fd, cmd, cmd_size, buf + i, read_size))) {
			return rc;
		}
	}
	return 0;
}

int read_high_baud(device_t *device, int fd, unsigned int address, unsigned int read_size, unsigned char *buf, unsigned int size) {
	unsigned char cmd[10];
	unsigned int cmd_size = 10;

	switch (device->family) {
	case FAMILY_COMMANDER:		// 24 bit command
		cmd[0] = 0x15;
		uint24_to_array_le(cmd + 1, address);
		uint24_to_array_le(cmd + 4, read_size);
		cmd[7] = device->highbaud_byte;
		cmd_size = 8;
		break;
	case FAMILY_EMC:				// 32 bit command
		cmd[0] = 0x15;
		uint32_to_array_le(cmd + 1, address);
		uint32_to_array_le(cmd + 5, read_size);
		cmd[9] = device->highbaud_byte;
		break;
	default:				// Commander TM has no high-speed read
		return -1;
	}

	return do_cmd_high_baud(device, fd, cmd, cmd_size, buf, size);
}


int read_id(device_t *device, int fd, unsigned char *buf, unsigned int size) {
	unsigned char cmd[6];
	unsigned int cmd_size = 6;

	switch (device->family) {
	case FAMILY_COMMANDER_TM:
		memcpy(cmd, "\x05\xBD\x7F\x00\x43\x00", cmd_size);
		break;
	case FAMILY_COMMANDER:
		memcpy(cmd, "\x05\xBD\x7F\x00\x43\x00", cmd_size);
		break;
	case FAMILY_EMC:
		memcpy(cmd, "\x05\x9D\xFF\x00\x43\x00", cmd_size);
		break;
	default:
		return -1;
		break;
	}

	return do_cmd(device, fd, cmd, cmd_size, buf, size);
}


int read_config0(device_t *device, int fd, unsigned char *buf, unsigned int size) {
	unsigned char cmd[2];
	unsigned int cmd_size;

	switch (device->family) {
	case FAMILY_COMMANDER_TM:
		cmd[0] = 0x96;
		cmd_size = 1;
		break;
	case FAMILY_COMMANDER:
	case FAMILY_EMC:
		cmd[0] = 0x96;
		cmd[1] = 0x00;
		cmd_size = 2;
		break;
	default:
		return -1;
		break;
	}

	return do_cmd(device, fd, cmd, cmd_size, buf, size);
}


int read_config1(device_t *device, int fd, unsigned char *buf, unsigned int size) {
	unsigned char cmd[2];
	unsigned int cmd_size;

	switch (device->family) {
	case FAMILY_COMMANDER_TM:
		dprintf(STDERR_FILENO, "Device %s doesn't have a second config page.\n", device->name);
		return 0;
		break;
	case FAMILY_COMMANDER:
	case FAMILY_EMC:
		cmd[0] = 0x96;
		cmd[1] = 0x01;
		cmd_size = 2;
		break;
	default:
		return -1;
		break;
	}

	return do_cmd(device, fd, cmd, cmd_size, buf, size);
}


int read_misc(device_t *device, int fd, unsigned char *buf, unsigned int size) {
	unsigned char cmd[6];
	unsigned int cmd_size = 6;

	memcpy(cmd, "\x05\xE0\x03\x00\xDC\x05", cmd_size);

	// Tell DC to refresh write current state data
	write_serial(fd, "\x89", 1, 1);

	return do_cmd(device, fd, cmd, cmd_size, buf, size);
}


int read_ram(device_t *device, int fd, unsigned char *buf, unsigned int size) {
	unsigned char cmd[6];
	unsigned int cmd_size = 6;

	// Tell DC to refresh write current state data
	write_serial(fd, "\x89", 1, 1);

	cmd[0] = 0x05;

	unsigned int read_size = 0x8000;

	for (unsigned int i = 0; i < device->ram_size; i += read_size) {
		// Load address into command
		uint24_to_array_le(cmd + 1, i);

		if (device->ram_size - i < read_size)
			read_size = device->ram_size - i;

		// Load read size into command
		uint16_to_array_le(cmd + 4, read_size);

		int rc = 0;
		if ((rc = do_cmd(device, fd, cmd, cmd_size, buf + i, read_size))) {
			return rc;
		}
	}
	return 0;
}


void usage(const char *name) {

	dprintf(STDERR_FILENO, "Usage: %s -m <model> [-ijcde | -a <addresss> -s <size>] -o file\n", name);
	dprintf(STDERR_FILENO, "Where: model is one of");
	for (int i = 0; i < C_ARRAY_SIZE(devices); i++) {
		if (i != 0) dprintf(STDERR_FILENO, ",");
		dprintf(STDERR_FILENO, " %s", devices[i].name);
	}
	dprintf(STDERR_FILENO, "\n");
	dprintf(STDERR_FILENO, "       -i            Read ID0 block\n");
	dprintf(STDERR_FILENO, "       -j            Read ID1 block\n");
	dprintf(STDERR_FILENO, "       -c            Read conf0 block\n");
	dprintf(STDERR_FILENO, "       -d            Read conf1 block\n");
	dprintf(STDERR_FILENO, "       -e            Read Misc block\n");
	dprintf(STDERR_FILENO, "       -f            Read all Log and Profile data\n");
	dprintf(STDERR_FILENO, "       -x            Use High-speed read command for address read mode\n");
	dprintf(STDERR_FILENO, "       -r            Read RAM/ROM memory\n");
	dprintf(STDERR_FILENO, "       -a <address>  Read memory at the address\n");
	dprintf(STDERR_FILENO, "       -s <size>     Read size bytes\n");
	dprintf(STDERR_FILENO, "       -o <file>     Output to file\n");
}

int main(int argc, char * argv[])
{
	read_mode_t mode = MODE_UNDEFINED;
	unsigned int address = 0xFFFFFFFF, size = 0;
	unsigned char *outfile;
	int result_size = 0;
	int high_speed = 0;
	device_t *device = NULL;
	unsigned int device_count = C_ARRAY_SIZE(devices);

	int c; 
	while ((c = getopt(argc, argv, "m:icdefxra:s:o:")) != -1) {
		switch (c) {
		case 'm': 	// Set model
			for (unsigned int i = 0; i < device_count; i++) {
				if (!strcmp(optarg, devices[i].name)) {
					device = &devices[i];
					break;
				}
			}
		case 'i':	// Read ID block
			mode = MODE_ID;
			result_size = 0x43;
			break;
		case 'c':	// Read conf0 block
			mode = MODE_CONF0;
			result_size = 512;
			break;
		case 'd':	// Read conf1 block
			mode = MODE_CONF1;
			result_size = 512;
			break;
		case 'e':	// Read Misc block
			mode = MODE_MISC;
			result_size = 1500;
			break;
		case 'f':	// Full read
			mode = MODE_FULL;
			break;
		case 'r':	// Read RAM/ROM
			mode = MODE_RAM;
			break;
		case 'x':	// Use high speed read
			high_speed = 1;
			break;
		case 'a':	// Read address
			mode = MODE_ADDRESS;
			address = (int) strtof(optarg, NULL);
			break;
		case 's':	// Read size
			size = (int) strtof(optarg, NULL);
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

	if (optind >= argc) {
		dprintf(STDERR_FILENO, "Device name must be spcified\n");
		exit(3);
	}

	if (device == NULL) {
		dprintf(STDERR_FILENO, "You must speficy a dive computer type.\n");
		exit(1);
	}

	int fd = open_serial(argv[optind]);
	if (fd < 0) {
		dprintf(STDERR_FILENO, "Error (%d) opening file \"%s\"\n", errno, strerror(errno));
		exit(1);
	}

	// Set baud
	int rc = set_baud(fd, device->baud);
	if (rc) {
		dprintf(STDERR_FILENO, "Error (%d) setting baud rate. %s\n", errno, strerror(errno));
		exit(1);
	}

	int of = STDOUT_FILENO;
	if (outfile) {
		of = open(outfile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	}

	if (of < 0) {
		dprintf(STDERR_FILENO, "Error (%d) opening file \"%s\"\n", errno, strerror(errno));
		exit(1);
	}
	unsigned char *buf;

	if (result_size) {
		buf = (unsigned char *) malloc(result_size);
		if (!buf) {
			dprintf(STDERR_FILENO, "Unable to allocate %d bytes of memory.\n", result_size);
			exit(1);
		}
	}

	switch (mode) {
	case MODE_ID:
		read_id(device, fd, buf, result_size);
		break;
	case MODE_CONF0:
		read_config0(device, fd, buf, result_size);
		break;
	case MODE_CONF1:
		read_config1(device, fd, buf, result_size);
		break;
	case MODE_MISC:
		read_misc(device, fd, buf, result_size);
		break;
	case MODE_RAM:
		result_size = device->ram_size;
		buf = (unsigned char *) malloc(result_size);
		if (!buf) {
			dprintf(STDERR_FILENO, "Unable to allocated %d bytes of memory.\n", result_size);
			exit(1);
		}

		read_low_baud(device, fd, device->ram_address, device->ram_size, buf, result_size);
		break;
	case MODE_FULL:	// Read log/profile data
		result_size = device->log_size;
		buf = (unsigned char *) malloc(result_size);
		if (!buf) {
			dprintf(STDERR_FILENO, "Unable to allocate %d bytes of memory.\n", result_size);
			exit(1);
		}

		if (device->highbaud == UNSUPPORTED) {
			read_low_baud(device, fd, device->log_address, device->log_size, buf, result_size);
		} else {
			read_high_baud(device, fd, device->log_address, device->log_size, buf, result_size);
		}
		break;
	case MODE_ADDRESS:
		if (address == 0xFFFFFFFF || size == 0) {
			dprintf(STDERR_FILENO, "Address and size must be specified in address mode.\n");
			usage(argv[0]);
			exit(1);
		}

		result_size = size;
		buf = (unsigned char *) malloc(result_size);
		if (!buf) {
			dprintf(STDERR_FILENO, "Unable to allocation %d bytes of memory.\n", result_size);
			exit(1);
		}

		if (!high_speed) {
			// Read at low baud (e.g. Commander TM)
			read_low_baud(device, fd, address, size, buf, result_size);
		} else {
			read_high_baud(device, fd, address, size, buf, result_size);
		}
		break;
	}

	if (result_size) {
		// store data
		write(of, buf, result_size);
	}

	close(fd);
	close(of);
}
