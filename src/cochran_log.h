#include <time.h>

typedef struct cochran_gasmix_t {
	float o2;		// percent
	float he;		// percent
} cochran_gasmix_t;



typedef struct cochran_log_t {
	unsigned int dive_num;
	unsigned int rep_dive_num;

	struct tm time_start;
	time_t timestamp_start;				// ticks
	time_t timestamp_pre;				// ticks
	unsigned int bt;					// minutes

	float depth_max;					// feet
	float depth_avg;					// feet
	float depth_start;					// feet

	float temp_min;						// F
	float temp_avg;						// F
	float temp_start;					// F
	unsigned int temp_min_bt;

	unsigned int tank_pressure_start;	// PSI
	unsigned int gas_consumption_start;	// PSI/minute
	unsigned int ascent_rate_max;		// feet/minute
	unsigned int ascent_rate_max_bt;	// seconds
	unsigned int sit;					// minutes

	unsigned int no_fly_start;			// minutes
	unsigned int no_fly_end;			// minutes

	unsigned int profile_pre;
	unsigned int profile_begin;
	unsigned int profile_end;

	float voltage_start;				// V
	float voltage_end;					// V
	unsigned int ndl_min;				// minutes
	unsigned int ndl_min_bt;			// minutes
	unsigned int deco_max;				// feet
	unsigned int deco_max_bt;			// minutes
	unsigned int deco_missed;			// minutes
	unsigned int deco_actual;			// minutes
	unsigned int deco_ceiling_max;		// feet
	unsigned int deco_ceiling_max_bt;	// minutes
	unsigned int deco_ceiling_missed;	// feet
	unsigned int event_count;
	unsigned char water_conductivity;
	unsigned int altitude;				// kilofeet

	cochran_gasmix_t mix[3];
	int alarm_depth;					// feet
	float alarm_po2;
	int conservatism;					// 0-50
	int profile_interval;				// seconds

	unsigned char tissue_start[40];
	unsigned char tissue_end[40];
} cochran_log_t;


void cochran_log_print_short_header(int ordinal);
void cochran_log_print_short(cochran_log_t *log, int ordinal);
void cochran_log_commander_I_parse(const unsigned char *in, cochran_log_t *out);
void cochran_log_commander_II_parse(const unsigned char *in, cochran_log_t *out);
void cochran_log_commander_III_parse(const unsigned char *in, cochran_log_t *out);
void cochran_log_emc_parse(const unsigned char *in, cochran_log_t *out);
