#!/usr/bin/awk -f

@load "ordchr"

BEGIN {
	for (i = 1; i < ARGC; i++) {
		if (ARGV[i] == "-h") {
			printf "\nUsage: %s [bus=<busid>] [devid=<devid>] [file]\n", ARGV[0];
			exit;
		}
	}

	flag = 0;
	last_dir = "";
	last_time = -1;
}


/^ *1160 *$/ {
	flag = 0;
}

flag == 1 {
	flag = 0;
	line = $0;

	gsub(/ /, "", line);

	if (dir == "<")
		i = 5;
	else
		i = 1;

	if (last_dir != dir || time - last_time > 0.1) {
		column = 0;
		row = 0;
	}

	last_dir = dir;
	last_time = time;

	while (substr(line, i, 2) != "") {
		if (row == 0 && column == 0) {
			printf "  %s\n%s: [%s] %s\n", ascii, time, line, dir;
			ascii = "";
		}
		if (column == 0)
			printf "    %08x  ", row * 16;
		hex = substr(line, i, 2);
		dec = strtonum("0x" hex);

		printf "%02x ", dec;

	

		column++;
		i += 2;

		if (dec >= 32 && dec <=127)
			ascii = ascii chr(dec)
		else
			ascii = ascii "."

		if (column == 8) {
			printf " ";
			ascii = ascii " "
		} else if (column == 16) {
			row++;
			printf "  %s\n", ascii;
			column =0;
			ascii = "";
		}
	}
}
		
# Send data
/[0-9a-f]{8} [.0-9]+ S Bo:[0-9]+:[0-9]+:[0-9]+ [-0-9]+ [-0-9]+ =/ {
	dir = ">";
	time = $2;
	split($4, id, ":");
	b = id[1];
	d = id[2];

	if ((bus == "" || bus == b) && (devid == "" || devid == d))
		flag = 1;
}

# Receive data
/[0-9a-f]{8} [.0-9]+ C Bi:[0-9]+:[0-9]+:[0-9]+ [0-9]+ [0-9]+ =/ {
	dir = "<";
	
	time = $2;
	split($4, id, ":");
	b = id[1];
	d = id[2];

	if ((bus == "" || bus == b) && (devid == "" || devid == d))
		flag = 1;
}

# Modem control
/.{33}40 / {
	printf "\n";
	print $0;
}
