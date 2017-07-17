/*
*	cochran_settings.h
*
*	Copyright 2014, John Van Ostrand
*
*/

union cochran_config_value {
	int integer;
	float rational;
};

enum cochran_config_encoding {
	CONFIG_ENC_BIT_INT,
	CONFIG_ENC_BE_INT,
	CONFIG_ENC_LE_INT,
	CONFIG_ENC_BE_INT_SEC,
	CONFIG_ENC_LE_INT_SEC,
	CONFIG_ENC_BE_DECIMAL,
	CONFIG_ENC_LE_DECIMAL,
	CONFIG_ENC_PERCENT,
};

enum cochran_config_field_type {
	CONFIG_FIELD_RANGE,
	CONFIG_FIELD_TOGGLE,
	CONFIG_FIELD_LIST,
};

struct cochran_config_edit {
	enum cochran_config_field_type type;
	union {
		struct {
			float low;
			float high;
			float step;
		} range;
		struct {
			char *on_label;
			char *off_label;
		} toggle;
		struct {
			int count;
			char **label;
			float *value;
		} list;
	};
};

struct cochran_config {
	int word;
	char byte;
	char start_bit;
	char bits;
	enum cochran_config_encoding encoding;
	struct cochran_config_edit edit;
	char *units;
	char *label;
	char *description;
};

struct cochran_config_word {
	unsigned char data[3];
};

