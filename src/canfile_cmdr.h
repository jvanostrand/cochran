/*
 * subsurface
 *
 * Copyright (C) 2014 John Van Ostrand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */


struct cochran_cmdr_log_t {
	// Pre-dive 128 bytes
	unsigned char minutes, seconds;			// 2 bytes
	unsigned char day, hour, year, month;	// 4 bytes
 	unsigned char sample_start_offset[4];	// 4 bytes
	unsigned char start_timestamp[4];		// 4 bytes
	unsigned char pre_dive_timestamp[4];	// 4 bytes
	unsigned char unknown1[6];				// 6 bytes
	unsigned char water_conductivity;		// 1 byte [0=low, 2=high]
	unsigned char unknown2[5];				// 5 bytes
//30
	unsigned char sample_pre_event_offset[4];// 4 bytes
	unsigned char unknown3[4];				// 4 bytes
	unsigned char start_battery_voltage[2];	// 2 bytes [/256]
//40
	unsigned char unknown4[2]; 				// 2 bytes
	unsigned char start_sgc[2];				// 2 bytes
	unsigned char unknown5[1];				// 1 byte
	unsigned char start_temp[1];			// 1 byte
	unsigned char unknown6[10];				// 10 bytes
//56
	unsigned char start_depth[2];			// 2 byte [/4]
	unsigned char unknown7[4];				// 3 bytes
	unsigned char start_psi[2];				// 2 bytes LE
	unsigned char unknown8[4];				// 4 bytes
	unsigned char sit[2];					// 2 bytes
//70
	unsigned char number[2];				// 2 bytes
	unsigned char unknown9[1];				// 1 byte
	unsigned char altitude;					// 1 byte [/4 = kft]
	unsigned char unknown10[28];				// 27 bytes
	unsigned char alarm_depth[2];			// 2 bytes
	unsigned char unknown11[4];				// 5 bytes
//108
	unsigned char repetitive_dive;			// 1 byte
	unsigned char unknown12[3];				// 3 bytes
	unsigned char start_tissue_nsat[16];	// 16 bytes [/256]

	// Post-dive 128 bytes
	unsigned char sample_end_offset[4];		// 4 bytes
	unsigned char unknown13[21];			// 21 bytes
	unsigned char temp;						// 1 byte
	unsigned char unknown14[12];			// 12 bytes
	unsigned char bt[2];					// 2 bytes [minutes]
	unsigned char max_depth[2];				// 2 bytes [/4]
	unsigned char avg_depth[2];				// 2 bytes
	unsigned char unknown15[38];			// 38 bytes
	unsigned char o2_percent[4][2];			// 8 bytes
	unsigned char unknown16[19];			// 19 bytes
	unsigned char profile_period;		 	// seconds between profile samples
	unsigned char unknown17[2];				// 2 bytes
	unsigned char end_tissue_nsat[16];		// 16 bytes [/256]
} __attribute__((packed));

 
typedef struct cochran_cmdr_log_t cochran_cmdr_log_t;


struct cochran_cmdr_config1_t {
	unsigned char unknown1[209];
	unsigned short int dive_count;
	unsigned char unknown2[274];
	unsigned short int serial_num; // @170
	unsigned char unknown3[24];
} __attribute__((packed));

typedef struct cochran_emc_config1_t cochran_emc_config1_t;
