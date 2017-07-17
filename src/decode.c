#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>




static unsigned int partial_decode(const unsigned int start, const unsigned int end,
                   const unsigned char *key, unsigned offset, const unsigned char mod,
                   const unsigned char *cipher, const unsigned int size, unsigned char *cleartext)
{
    unsigned i, sum = 0;

    for (i = start; i < end; i++) {
        unsigned char d = key[offset++];
        if (i >= size)
            break;
        if (offset == mod)
            offset = 0;
        d += cipher[i];
        if (cleartext)
            cleartext[i] = d;
        sum += d;
    }
    return sum;
}


void main(int argc, char **argv) {


#define SIZE 1860561
	int fd;
	unsigned char buf[SIZE + 1], cleartext[SIZE + 1];
	unsigned char mod;
	unsigned const char *key, *cipher;


	fd = open(argv[1], O_RDONLY);
	close(fd);

	read(fd, buf, SIZE);

	key = buf;
	mod = buf[100];
	cipher = buf + 101;

	partial_decode(0, SIZE - 101, key, 0, mod, cipher, SIZE - 101, cleartext);

	for (int x = 0; x < SIZE - 101; x++)
		putchar(cleartext[x]);
}
