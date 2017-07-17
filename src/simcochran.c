/*
*  simcochran.c
*
*  Simulates a Cochran dive computer's serial interface. Useful for
*  testing libdivecomputer.
*
*  How to:
*
*		Build using:
*
*			gcc -g -o simcochran -lutil simcochran.c
*
*  		A directory containing a set of data files is needed. These
*  		data files are memory dumps from a Cochran dive computer.
*  		You can download the relevant memory from a DC using 
*  		libdivecomputer's dctool example program.
*
*		mkdir mydir # Create a directory to contain the dump
*  		dctool -d Commander -f Cochran -l mydir/dump.log dump -o mydir/memory
*
*  		Now we need to extract "vendor" event data from the dump.log file.
*  		The data will be in hexadecimal format and needs to be converted back
*  		to binary. The file names need to be:
*
*  			id0		(first 67 byte Vendor event)
*  			id1		(second 67 byte Vendor event, not on all DCs)
*  			config0	(first 512 byte Vendor event)
*  			config1 (second 512 byte Ventor event)
*  			config2 (third 512 byte Vendor event, not on all DCs)
*  			config3 (fourth 512 byte Vendor event, not on all DCs)
*  			misc	(first 1500 byte Vendor event)
*
* 		The Vendor event data can be converted back to binary in this way:
*
* 		echo "E1005200803F3FE0E0E0E000FF00FF8400FF2C181C78C4B873FFECD097FFFF
* 			00000FFC7F1FFFFB7977FFF00C8000000FFFFFCFFFFFFFF001F00F0F0FF58009
* 			F710F1" | xxd -r -p - mydir/id0
*
* 		Now with "mydir" containing the above files run the simulator with
*		this command to simulate an EMC model:
*
* 		./simcochran emc mydir
*
* 		The simulator will print out the psuedo TTY device to connect your
* 		application to.
*
*		IMPORTANT NOTE:
*
*			The psuedo TTY can't handle the high custom baud rate of the EMC
*			family computers. You will need to change your software to use
*			a standard baud rate, like 115200. In libdivecomputer this is done
*			in src/cochran_commander.c in the "cochran_layout_emc*" structures.
* 		
*/

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <termios.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <signal.h>


#define array_uint16_le(p) ( (unsigned int) (p)[0] + ((p)[1] << 8) )
#define array_uint24_le(p) ( (unsigned int) (p)[0] + ((p)[1] << 8) + ((p)[2] << 16) )
#define array_uint32_le(p) ( (unsigned int) (p)[0] + ((p)[1] << 8) \
                                         + ((p)[2] << 16) + ((p)[3] << 24) )

void heartbeat(int signal) {
}


int pty_setup() {
	int mfd, sfd;
	char pts_name[256];

	if ((openpty(&mfd, &sfd, pts_name, 0, 0)) == -1) {
		printf("%s: Unable to open PTY\n", strerror(errno));
		return -1;
	}

	printf("Simulator running on device %s\n", pts_name);

	// Set parameters
	struct termios tty;
	memset(&tty, 0, sizeof(tty));
	if (tcgetattr (mfd, &tty) != 0) {
		printf("Unable to get attributes, %s\n", strerror(errno));
		close(mfd);
		return -1;
	}

	// Setup raw input/output mode without echo.
	tty.c_iflag &= ~(IGNBRK | BRKINT | ISTRIP | INLCR | IGNCR | ICRNL);
	tty.c_oflag &= ~(OPOST);
	tty.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);

	// Enable the receiver (CREAD) and ignore modem control lines (CLOCAL).
	tty.c_cflag |= (CLOCAL | CREAD);
	if (cfsetispeed (&tty, B115200) != 0 ||
		cfsetospeed (&tty, B115200) != 0) {
		printf("Unable to set baud, %s\n", strerror(errno));
		close(mfd);
		return -1;
	}

	tty.c_cflag |= CS8;
	tty.c_cflag &= ~(PARENB | PARODD);
	tty.c_iflag &= ~(IGNPAR | PARMRK | INPCK);
	tty.c_iflag |= IGNPAR;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;
	if (tcsetattr (mfd, TCSANOW, &tty) != 0) {
		printf("Unable to set parameters, %s\n", strerror(errno));
		close(mfd);
		return -1;
	}

	return mfd;
}


// Setup signal handler for heartbeat
void setup_signals() {
	struct sigaction sa, old_sa;

	sa.sa_handler = heartbeat;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGALRM);
	sa.sa_flags = 0;

	sigaction(SIGALRM, &sa, &old_sa);
}


void send_data(int mfd, const unsigned char *command, const char* data_dir, int address_size) {
	char filename[256];
	int seek = 0, size = -1, chunk = 12;
	int data_fd;
	char buffer[4096];

	// Build file name
	switch (command[0]) {
	case 0x05:		// Read id blocks, 67 bytes
		size = array_uint16_le(command + 4);
		if (command[1] == 0x9d)
			sprintf(filename, "%s/id0", data_dir);
		else
			sprintf(filename, "%s/id1", data_dir);
		break;
	case 0x96:		// Read config block
		size = 512;
		sprintf(filename, "%s/config%d", data_dir, command[1]);
		break;
	case 0x89:
		size = array_uint16_le(command + 5);
		sprintf(filename, "%s/misc", data_dir);
		break;
	case 0x15:
		chunk = 4096;
		if (address_size == 4) {
			// 32 LE address and size
			seek = array_uint32_le(command + 1);
			size = array_uint32_le(command + 5);
		} else {
			seek = array_uint24_le(command + 1);
			size = array_uint24_le(command + 4);
		}
		sprintf(filename, "%s/memory", data_dir);
		break;
	default:
		printf("Unknown command %02x.\n", command[0]);
	}


	printf("Sending file %s, bytes (%d - %d)", filename, seek, seek + size);

	// Open file and dump to device
	data_fd = open(filename, O_RDONLY);

	if (data_fd < 0) {
		printf("%s: opening %s\n", strerror(errno), filename);
		return;
	}

	// Position file pointer
	if (seek) {
		if (lseek(data_fd, seek, SEEK_SET) == -1) {
			printf("%s while positioning file pointer.\n", strerror(errno));
			return;
		}
	}

	if (size < chunk) chunk = size;
	while (size > 0) {
		int byte_count;

		byte_count = read(data_fd, buffer, chunk);
		if (byte_count != chunk) {
			printf("%s: error reading %s\n", strerror(errno), filename);
			return;
		}
		byte_count = write(mfd, buffer, chunk);
		if (byte_count != chunk) {
			printf("%3: error writing\n", strerror(errno));
			return;
		}

		size -= chunk;
		if (size < chunk) chunk = size;
	}

	close(data_fd);

	printf("\n");
}

void main(int argc, char *argv[]) {
	char *data_dir;
	int mfd,  address_size;

	if (argc != 3) {
		printf("Usage: %s [-c|-e] <data_dir>\n", argv[0]);
		printf("Where:  [-c|-e]    Model to simulate, e=EMC, c=Commander\n");
		printf("        <data_dir> Directory that contains data files.\n");
		return;
	}

	if (strcmp(argv[1], "-c") == 0) {
		address_size = 3;
	} else if (strcmp(argv[1], "-e") == 0) {
		address_size = 4;
	}

	data_dir = argv[2];

	setbuf(stdout, 0);

	unsigned char buffer[4096];
	int byte_count, bytes_expected;
	int bytes_read;
	char whirlygig[5] = "-\\|/";
	int whirlygig_ndx = 0;

	if ((mfd = pty_setup()) == -1) 
		exit(1);

	setup_signals();

	while (1) {
		alarm(1);
		byte_count = read(mfd, buffer, 1);
		alarm(0);

		if (byte_count < 0) {
			if (errno == 4) {
				// alarm, send hearbeat
				write(mfd, "\xAA", 1);
				putchar(whirlygig[whirlygig_ndx++]);
				putchar(0xd);
				if (whirlygig_ndx > 3) whirlygig_ndx = 0;
			} else {
				printf("[%d]%s\n", errno, strerror(errno));
			}
			continue;
		}

		switch (buffer[0])
		{
		case 0x05:
			bytes_expected = 5;
			break;
		case 0x96:
			bytes_expected = 1;
			break;
		case 0x89:
			bytes_expected = 6;
			break;
		case 0x15:
			bytes_expected = address_size * 2 + 1;
			break;
		default:
			printf("Unknown command %02x.\n", buffer[0]);
			continue;
		}

		bytes_read = 0;
		while (bytes_read < bytes_expected) {
			byte_count = read(mfd, &(buffer[1 + bytes_read]), bytes_expected);
			if (byte_count < 0) {
				printf("%s: error reading device\n", strerror(errno));
				continue;
			}
			bytes_read += byte_count;
		}

		printf("Command: ");

		for (int n = 0; n <= bytes_read; n++) 
			printf("%02x ", buffer[n]);
		printf("\n");


		send_data(mfd, buffer, data_dir, address_size);
	}
}



