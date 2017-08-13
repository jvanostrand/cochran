#include <stdio.h>
#include <time.h>
#include <string.h>

#include "cochran.h"
#include "cochran_log.h"

/*
 * FAMILY_COMMANDER_I
 * 		log size: 90
 * 		sample size: 1
 * 		interdive: NO
 *
 * FAMILY_COMMANDER_II
 * 		log size: 256
 * 		sample size : 2
 * 		interdive: YES
 * 		broken-down time: NO
 *
 * FAMILY_COMMANDER_III
 * 		log size: 256
 * 		sample size: 2
 * 		interdive: YES
 * 		broken-down time: YES
 *
 * FAMILY_EMC
 * 		log size: 512
 * 		sample_size: 3
 * 		broken-down time: YES
 */

void cochran_log_print_short_header(int ordinal) {
	if (ordinal < 0) {
		printf("Dive Rep YYYY/MM/DD hh:mm:ss   SIT    BT  Depth Temp   NDL  Deco Int Volt Con   O2   He  Pro Pre  Pro Beg  Pro End\n");
		printf("==== === =================== ===== ===== ====== ==== ===== ===== === ==== === ==== ==== ======== ======== ========\n");
	} else {
		printf("  # Dive Rep YYYY/MM/DD hh:mm:ss   SIT    BT  Depth Temp   NDL  Deco Int Volt Con   O2   He  Pro Pre  Pro Beg  Pro End\n");
		printf("=== ==== === =================== ===== ===== ====== ==== ===== ===== === ==== === ==== ==== ======== ======== ========\n");
	}
}


void cochran_log_print_short(cochran_log_t *log, int ordinal) {

	if (ordinal < 0) {
		//printf("Dive Rep YY/MM/DD hh:mm:ss   SIT    BT Depth Temp   NDL  Deco Int Volt Con   O2   He  Pro Pre   Pro Beg  Pro End\n");
		printf("%4d %3d %02d/%02d/%02d %02d:%02d:%02d %2dh%02d %2dh%02d %6.2f %4.1f %2dh%02d %2dh%02d %3d %4.2f %3d %4.1f %4.1f %08x %08x %08x\n",
			log->dive_num, log->rep_dive_num,
			log->time_start.tm_year + 1900, log->time_start.tm_mon + 1, log->time_start.tm_mday, log->time_start.tm_hour, log->time_start.tm_min, log->time_start.tm_sec,
			log->sit / 60, log->sit % 60, log->bt / 60, log->bt % 60,
			log->depth_max, log->temp_min,
			log->ndl_min / 60, log->ndl_min % 60, log->deco_max / 60, log->deco_max % 60,
			log->profile_interval, log->voltage_start, log->conservatism,
			log->mix[0].o2, log->mix[0].he,
			log->profile_pre, log->profile_begin, log->profile_end);
	} else {
		//printf("  # Dive Rep YY/MM/DD hh:mm:ss   SIT    BT Depth Temp   NDL  Deco Int Volt Con   O2   He  Pro Pre   Pro Beg  Pro End\n");
		printf("%3d %4d %3d %02d/%02d/%02d %02d:%02d:%02d %2dh%02d %2dh%02d %6.2f %4.1f %2dh%02d %2dh%02d %3d %4.2f %3d %4.1f %4.1f %08x %08x %08x\n",
			ordinal,
			log->dive_num, log->rep_dive_num,
			log->time_start.tm_year + 1900, log->time_start.tm_mon + 1, log->time_start.tm_mday, log->time_start.tm_hour, log->time_start.tm_min, log->time_start.tm_sec,
			log->sit / 60, log->sit % 60, log->bt / 60, log->bt % 60,
			log->depth_max, log->temp_min,
			log->ndl_min / 60, log->ndl_min % 60, log->deco_max / 60, log->deco_max % 60,
			log->profile_interval, log->voltage_start, log->conservatism,
			log->mix[0].o2, log->mix[0].he,
			log->profile_pre, log->profile_begin, log->profile_end);
	}
}


void cochran_log_nemesis_parse(const unsigned char *in, cochran_log_t *out) {
	memset(out, 0, sizeof(cochran_log_t));

	out->profile_begin			= array_uint24_le(in);

	memcpy(out->tissue_start, in + 3, 12);

	// I only had one file to determine this epoch and a version of
	// Analyst that didn't display seconds. It seems that the epoch
	// is approx Jan 1, 1892 00:00:00 "Dec 31, 1891 23:42:28"
	// which is almost 100 years from the later models' epoch of
	// Jan 1, 1992 00:00:00.
	out->timestamp_start		= array_uint32_le(in + 15) - 2461431600;
	localtime_r(&out->timestamp_start, &out->time_start);

	out->rep_dive_num			= in[19];
	out->dive_num				= array_uint16_le(in + 20);

	out->tank_pressure_start	= array_uint16_le(in + 26);
	out->voltage_start			= (in[39] >> 5) + (in[39] & 0x1f) / 32.0;
	out->bt						= array_uint16_le(in + 54);
	out->depth_max				= array_uint16_le(in + 56) / 4.0;
	out->depth_avg				= array_uint16_le(in + 51) / 4.0;
	out->tank_pressure_end		= array_uint16_le(in + 60);
	out->ndl_min				= array_uint16_le(in + 64);
	out->deco_max				= in[68];

	out->ascent_rate_max		= in[67];
	// out->ascent_rate_max_depth = in[68] * 2;
	out->ascent_rate_max_bt		= array_uint16_le(in + 69);

	out->profile_interval		= in[84]; //???
	out->conservatism			= in[85] / 2.55; //???
	out->mix[0].o2 				= array_uint16_le(in + 86) / 256;
	out->mix[1].o2 				= array_uint16_le(in + 88) / 256;

	out->voltage_end			= (in[92] >> 5) + (in[92] & 0x1f) / 32.0;
	out->no_fly_end				= (in[93] >> 5) + (in[93] & 0x1f) / 32.0;
	out->temp_avg				= in[95];
	out->temp_min				= in[96];
	out->temp_start				= in[97];// max

	out->event_count			= in[102];
}

// Early commander parser
void cochran_log_commander_I_parse(const unsigned char *in, cochran_log_t *out) {
	memset(out, 0, sizeof(cochran_log_t));

	out->profile_begin			= array_uint24_le(in);

	memcpy(out->tissue_start, in + 3, 12);

	out->timestamp_start		= array_uint32_le(in + 15) + COCHRAN_EPOCH;
	localtime_r(&out->timestamp_start, &out->time_start);

	out->rep_dive_num			= in[19];
	out->dive_num				= array_uint16_le(in + 20);
	out->sit					= array_uint16_le(in + 24);
	out->no_fly_start			= in[27] * 15;
	out->voltage_start			= (in[32] >> 5) + (in[32] & 0x1f) / 32.0;

 	memcpy(out->tissue_end, in + 35, 12);

	out->bt						= array_uint16_le(in + 47);
	out->depth_max				= array_uint16_le(in + 49) / 4.0;
	out->depth_avg				= array_uint16_le(in + 51) / 4.0;
	out->deco_ceiling_missed	= in[55] / 2.0;

	if (out->deco_ceiling_missed) {
		out->ndl_min			= 0;
		out->deco_missed		= array_uint16_le(in + 53);
	} else {
		out->ndl_min			= array_uint16_le(in + 53);
		out->deco_missed		= 0;
	}

	out->deco_ceiling_max		= in[56] / 2.0;
	out->deco_max				= array_uint16_le(in + 57);

	out->ascent_rate_max		= in[59];
	// out->ascent_rate_max_depth = in[60] * 2;
	out->ascent_rate_max_bt		= array_uint16_le(in + 61);

	out->profile_interval		= in[72];
	out->conservatism			= in[73] / 2.55;

	for (int i = 0; i < 3; i++)	// clear mixes
		out->mix[i].o2 = out->mix[i].he = 0;

	out->mix[0].o2				= array_uint16_le(in + 74) / 256.0;

	out->temp_avg				= in[81];
	out->temp_min				= in[82];

	out->temp_start				= in[83];// max
	out->voltage_end			= (in[84] >> 5) + (in[84] & 0x1f) / 32.0;
	out->no_fly_end				= in[85] * 15;

	out->event_count			= in[89];
}


// Pre-21000 parser
void cochran_log_commander_II_parse(const unsigned char *in, cochran_log_t *out) {
	memset(out, 0, sizeof(cochran_log_t));

	out->profile_begin			= array_uint32_le(in);
	out->timestamp_start		= array_uint32_le(in + 8) + COCHRAN_EPOCH;
	localtime_r(&out->timestamp_start, &out->time_start);
	out->water_conductivity		= in[24];
	out->profile_pre			= array_uint32_le(in + 28);
	out->temp_start				= in[43];
	out->depth_start			= array_uint16_le(in + 54) / 4.0;
	out->dive_num				= array_uint16_le(in + 68);
	out->altitude				= in[73] / 4.0;
	memcpy(out->tissue_start, in + 112, 16);
	out->profile_end			= array_uint32_le(in + 128);
	//out->temp_end				= in[153];
	out->bt						= array_uint16_le(in + 166);
	out->depth_max				= array_uint16_le(in + 168) / 4.0;
	out->depth_avg				= array_uint16_le(in + 170) / 4.0;
	for (unsigned int i = 0; i < 2; i++) {
		out->mix[i].o2 			= array_uint16_le(in + 210 + i * 2) / 256.0;
		out->mix[i].he			= 0;
	}
	out->mix[2].o2 = out->mix[2].he = 0;
	out->temp_min				= in[232];
	out->temp_avg				= in[233];
	memcpy(out->tissue_end, in + 240, 16);
}


// Post-21000 parser
void cochran_log_commander_III_parse(const unsigned char *in, cochran_log_t *out) {
	memset(out, 0, sizeof(cochran_log_t));

	out->time_start.tm_min		= in[0];
	out->time_start.tm_sec		= in[1];
	out->time_start.tm_mday		= in[2];
	out->time_start.tm_hour		= in[3];
	out->time_start.tm_year		= (in[4] < 92 ? in[5] + 100: in[4]);
	out->time_start.tm_mon		= in[5] - 1;

	out->profile_begin			= array_uint32_le(in + 6);
	out->timestamp_start		= array_uint32_le(in + 10);
	out->timestamp_pre			= array_uint32_le(in + 14);
	out->water_conductivity		= in[24];
	out->profile_pre			= array_uint32_le(in + 30);
	out->voltage_start			= array_uint16_le(in + 38) / 256.0;

	out->gas_consumption_start	= array_uint16_le(in + 42) / 2.0;
	out->temp_start				= in[45];
	out->depth_start			= array_uint16_le(in + 56) / 4.0;
	out->tank_pressure_start	= array_uint16_le(in + 62);
	out->sit					= array_uint16_le(in + 68);
	out->dive_num				= array_uint16_le(in + 70);
	out->altitude				= in[73] / 4;
	out->alarm_depth			= in[102];
	out->rep_dive_num			= in[108];
	memcpy(out->tissue_start, in + 112, 16);

	out->profile_end			= array_uint32_le(in + 128);
	out->temp_min				= in[153];
	out->bt						= array_uint16_le(in + 166);
	out->depth_max				= array_uint16_le(in + 168) / 4.0;
	out->depth_avg				= array_uint16_le(in + 170) / 4.0;
	for (int i = 0; i < 2; i++) {
		out->mix[i].o2			= array_uint16_le(in + 210 + i * 2) / 256.0;
		out->mix[i].he			= 0;
	}
	out->mix[2].o2				= array_uint16_le(in + 214) / 256.0;
	out->mix[2].he				= 0;
	out->profile_interval		= in[237];
	memcpy(out->tissue_end, in + 240, 16);
}


void cochran_log_gem_parse(const unsigned char *in, cochran_log_t *out) {
	memset(out, 0, sizeof(cochran_log_t));

	out->timestamp_start		= array_uint32_le(in + 8);
	localtime_r(&(out->timestamp_start), &(out->time_start));
	out->depth_start			= in[14];
	out->profile_begin			= array_uint32_le(in);
	out->voltage_start			= array_uint16_le(in + 68) / 256.0;
	out->dive_num				= array_uint16_le(in + 54);
	out->rep_dive_num			= in[78];

	memcpy(out->tissue_start, in + 80, 12);

	out->profile_end			= array_uint32_le(in + 128);
	out->bt						= array_uint16_le(in + 156);
	out->depth_max				= array_uint16_le(in + 158) / 4.0;
	out->deco_max				= array_uint16_le(in + 166);
	out->tank_pressure_start	= array_uint16_le(in + 194);
	for (int i = 0; i < 2; i++) {
		out->mix[i].o2 			= array_uint16_le(in + 196 + i * 2) / 256.0;
		out->mix[i].he 			= 0;
	}

	out->depth_avg				= in[205];	// uint16_le @ 51 maybe
	out->temp_min				= in[208] / 2.0 + 20;
	//out->temp_avg				= in[207] / 2.0 + 20;
	out->temp_start				= in[209] / 2.0 + 20;
	out->profile_interval		= in[213];
	memcpy(out->tissue_end, in + 216, 12);
}


void cochran_log_emc_parse(const unsigned char *in, cochran_log_t *out) {
	memset(out, 0, sizeof(cochran_log_t));
	out->time_start.tm_sec		= in[0];
	out->time_start.tm_min		= in[1];
	out->time_start.tm_hour		= in[2];
	out->time_start.tm_mday		= in[3];
	out->time_start.tm_mon		= in[4] - 1;
	out->time_start.tm_year		= (in[5] < 92 ? in[5] + 100: in[5]);

	out->profile_begin			= array_uint32_le(in + 6);
	out->timestamp_start		= array_uint32_le(in + 10);
	out->timestamp_pre			= array_uint32_le(in + 14);
	out->water_conductivity		= in[24];
	out->profile_pre			= array_uint32_le(in + 30);

	out->depth_start			= array_uint16_le(in + 42) / 256.0;
	out->voltage_start			= array_uint16_le(in + 46) / 256.0;
	out->temp_start				= in[55];
	out->sit					= array_uint16_le(in + 84);
	out->dive_num				= array_uint16_le(in + 86);
	out->altitude				= in[89] / 4;
	out->no_fly_start			= array_uint16_le(in + 90);
	//out->sit_post_dive		 = array_uint16_le(110) / 60;
	//out->po2_setpoint			 = 112 - 130 bytes
	out->alarm_po2				= array_uint16_le(in + 142) / 256.0;
	for (int i = 0; i < 3; i++) {
		out->mix[i].o2 = array_uint16_le(in + 144 + i * 2) / 256.0;
		out->mix[i].he = array_uint16_le(in + 164 + i * 2) / 256.0;
	}
	out->alarm_depth			= array_uint16_le(in + 184);
	out->conservatism			= in[200] / 2.55;
	out->rep_dive_num			= in[203];
	memcpy(out->tissue_start, in + 216, 40);

	out->profile_end			= array_uint32_le(in + 256);
	out->temp_min				= in[283];
	out->bt						= array_uint16_le(in + 304);
	out->depth_max				= array_uint16_le(in + 306) / 4.0;
	out->depth_avg				= array_uint16_le(in + 310) / 4.0;
	out->ndl_min				= array_uint16_le(in + 312);
	out->ndl_min_bt				= array_uint16_le(in + 314);
	out->deco_max				= array_uint16_le(in + 316);
	out->deco_max_bt			= array_uint16_le(in + 318);
	out->deco_ceiling_max		= array_uint16_le(in + 320);
	out->deco_ceiling_max_bt	= array_uint16_le(in + 322);
	out->ascent_rate_max		= in[334];
	out->ascent_rate_max_bt		= array_uint16_le(in + 338);
	out->voltage_end			= array_uint16_le(in + 394) / 256;
	out->temp_min_bt			= array_uint16_le(in + 404);
	out->no_fly_end				= array_uint16_le(in + 428);
	out->event_count			= array_uint16_le(in + 430);
	out->deco_actual			= array_uint16_le(in + 432);
	out->profile_interval		= in[435];
	memcpy(out->tissue_end, in + 216, 40);
}


int cochran_log_meta(cochran_log_meta_t *meta, const char *model) {
	cochran_log_meta_t meta_table[] = {
		{ "017", 90, cochran_log_commander_I_parse, 	"Early Commander" },
		{ "102", 256, cochran_log_gem_parse,			"Early Gemini" },
		{ "114", 108, cochran_log_nemesis_parse,		"Nemesis" },
		{ "120", 90, cochran_log_commander_I_parse,		"Early Commander" },
		{ "124", 90, cochran_log_commander_I_parse,		"Nemo" },
		{ "140", 90, cochran_log_commander_I_parse,		"AquaNox" },
		{ "213", 256, cochran_log_commander_II_parse,	"Pre-21000 Commander" },
		{ "215", 256, cochran_log_commander_III_parse,	"Gemini" },
		{ "216", 256, cochran_log_commander_III_parse,	"Gemini" },
		{ "221", 256, cochran_log_commander_III_parse,	"Commander" },
		{ "300", 512, cochran_log_emc_parse,			"EMC" },
		{ "301", 512, cochran_log_emc_parse,			"EMC" },
		{ "315", 512, cochran_log_emc_parse,			"EMC" },
	};

	int meta_cnt;
	meta_cnt = sizeof(meta_table) / sizeof(struct cochran_log_meta_t);

	for (int i = 0; i < meta_cnt; i++ ) {
		if (!strncmp(model, meta_table[i].model, 3)) {
			memcpy(meta, &(meta_table[i]), sizeof(cochran_log_meta_t));
			return 0;
		}
	}

	return 1;
}


/*
 * Determine parser to use and parse log
 */
int cochran_log_parse(const char *model, const unsigned char *in, cochran_log_t *out) {
	cochran_log_meta_t meta;

	if (cochran_log_meta(&meta, model))
		return 1;

	meta.parser(in, out);

	return 0;
}
