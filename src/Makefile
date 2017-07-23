
all: test simple read find_all ftdi_test read_eeprom serial_test

.c.o:
	gcc -Wall -Wextra -c -g $(CFLAGS) -o $@ $<

emcdump.o: emcdump.c cochran_decode.h
emcsampledump.o: emcsampledump.c cochran_decode.h
cmdrdump.o: cmdrdump.c cochran_decode.h
cmdrsampledump.o: cmdrsampledump.c cochran_decode.h

CFLAGS=-L libdivecomputer/src/.libs -I libdivecomputer/include
LDFLAGS=-ldivecomputer -lm

dumpsamples: dumpsamples.o cochran_decode.o
	gcc -o ../bin/dumpsamples dumpsamples.o cochran_decode.o

emcdump: emcdump.o cochran_decode.o printdebug.o
	gcc -g -lm -o ../bin/emcdump emcdump.o cochran_decode.o printdebug.o
	
emcsampledump: emcsampledump.o cochran_decode.o
	gcc -g -lm -o ../bin/emcsampledump emcsampledump.o cochran_decode.o printdebug.o

cmdrdump: cmdrdump.o cochran_decode.o
	gcc -g -lm -o ../bin/cmdrdump cmdrdump.o cochran_decode.o printdebug.o

cmdrsampledump: cmdrsampledump.o cochran_decode.o
	gcc -g -lm -o ../bin/cmdrsampledump cmdrsampledump.o cochran_decode.o printdebug.o

simcochran: simcochran.o
	gcc -g -lutil -o ../bin/simcochran simcochran.o

cochran_download: cochran_download.o
	gcc -g $(CFLAGS) $(LDFLAGS) -o ../bin/cochran_download cochran_download.o

canfile.o: cochran.h cochran_log.h

cochran_log.o: cochran.h cochran_log.h

canfile: canfile.o cochran_log.o
	gcc -Wall -Wextra -g $(CFLAGS) -lm -o ../bin/canfile canfile.o cochran_log.o

wanfile.o: canfile_cmdr.h canfile_emc.h

wanfile: wanfile.o
	gcc -g $(CFLAGS) $(LDFLAGS) -o ../bin/wanfile wanfile.o

cochran_settings.o: cochran_settings.c cochran_settings.h

cochran_settings_extern.o: cochran_settings_extern.c cochran_settings.h

cochran_settings: cochran_settings.o cochran_settings_extern.o
	gcc -g -Wall -lm -o ../bin/$@ cochran_settings.o cochran_settings_extern.o
