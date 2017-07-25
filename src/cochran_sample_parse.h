
typedef enum cochran_sample_type_t {
	SAMPLE_DEPTH,
	SAMPLE_TEMP,
	SAMPLE_ASCENT_RATE,
	SAMPLE_TANK_PRESSURE,
	SAMPLE_GAS_CONSUMPTION_RATE,
	SAMPLE_NDL,
	SAMPLE_DECO,
	SAMPLE_DECO_FIRST_STOP,
	SAMPLE_TISSUES,
	SAMPLE_EVENT,
	SAMPLE_INTERDIVE,
} cochran_sample_type_t;


typedef union cochran_sample_value_t {
    double depth;				// ft
    double tank_pressure;		// PSI
    double temp;				// F
	double ascent_rate;			// ft/min
	double gas_consumption_rate;	// psi/min
    const char *event;
	unsigned int ndl;			// minutes
    struct {
        unsigned int time;		// minutes
        int ceiling;			// feet
    } deco;
	unsigned char tissues[20];
	struct {
		unsigned char code;
		struct tm time;
		const unsigned char *data;
		unsigned int size;
	} interdive;
} cochran_sample_value_t;



typedef struct cochran_sample_t {
	cochran_sample_type_t type;
	cochran_sample_value_t value;
} cochran_sample_t;



// Sample parse callback
typedef int (*cochran_sample_callback_t) (int time, cochran_sample_t *sample, void *userdata);

void cochran_sample_parse (cochran_family_t family, const cochran_log_t *log, const unsigned char *samples, unsigned int size, cochran_sample_callback_t callback, void *userdata);

