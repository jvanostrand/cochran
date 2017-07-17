/*
*	cochran.c
*
*	Copyright 2014, John Van Ostrand
*/

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

#include "cochran_settings.h"

extern struct cochran_config emc_cfg[];
extern struct cochran_config cmd_cfg[];

struct cochran_config_word cochran_config_get_word(const unsigned char *config, int word_num)
{
	struct cochran_config_word word;

	memcpy(word.data, config + word_num * 2, 2);
	word.data[2] = 0;

	return word;
}


void cochran_config_decode_value(struct cochran_config_word *word, struct cochran_config *cfg,
	union cochran_config_value *value)
{
	unsigned char shift, value_mask;

	// Extract the data
	switch (cfg->encoding) {
	case CONFIG_ENC_LE_INT:		// Little endian integer
		value->integer = word->data[0] + word->data[1] * 256;
		break;
	case CONFIG_ENC_BE_INT:		// Big endian integer
		value->integer = word->data[1] + word->data[0] * 256;
		break;
	case CONFIG_ENC_LE_INT_SEC:		// Little endian integer, take whole two bytes, div by 60
		value->integer = (word->data[0] + word->data[1] * 256) / 60;
		break;
	case CONFIG_ENC_BE_INT_SEC:		// Little endian integer, take whole two bytes, div by 60
		value->integer = (word->data[1] + word->data[0] * 256) / 60;
		break;
	case CONFIG_ENC_LE_DECIMAL:	// Little endian decimal, take whole two bytes
		value->rational = round(((float) word->data[0] / 256 + word->data[1]) * 100) / 100;
		break;
	case CONFIG_ENC_BE_DECIMAL:	// Little endian decimal, take whole two bytes
		value->rational = round(((float) word->data[1] / 256 + word->data[0]) * 100) / 100;
		break;
	case CONFIG_ENC_PERCENT:	// Take a whole byte
		value->integer =  round((float) word->data[cfg->byte] * 100 / 256);
		break;
	case CONFIG_ENC_BIT_INT:	// Mask bits and shift to a integer
		shift = cfg->start_bit + 1 - cfg->bits;
		value_mask = (1 << cfg->bits) - 1;
		value->integer = word->data[cfg->byte] >> shift & value_mask;
		break;
	}
}

void cochran_config_get_value(const unsigned char *config, struct cochran_config *cfg,
	union cochran_config_value *value)
{
	struct cochran_config_word word;

	word = cochran_config_get_word(config, cfg->word);
	cochran_config_decode_value(&word, cfg, value);
}


void cochran_config_encode_value(struct cochran_config_word *word, struct cochran_config *cfg,
	union cochran_config_value *value)
{
	unsigned char shift, value_mask, word_mask;

	// Extract the data
	switch (cfg->encoding) {
	case CONFIG_ENC_LE_INT:		// Little endian integer, take whole two bytes
		word->data[0] = value->integer & 0xff;
		word->data[1] = (value->integer >> 8) & 0xff;
		break;
	case CONFIG_ENC_BE_INT:		// Little endian integer, take whole two bytes
		word->data[1] = value->integer & 0xff;
		word->data[0] = (value->integer >> 8) & 0xff;
		break;
	case CONFIG_ENC_LE_INT_SEC:		// Little endian integer, take whole two bytes, mult by 60
		word->data[0] = (value->integer * 60) & 0xff;
		word->data[1] = ((value->integer * 60) >> 8) & 0xff;
		break;
	case CONFIG_ENC_BE_INT_SEC:		// Little endian integer, take whole two bytes, mult by 60
		word->data[1] = (value->integer * 60) & 0xff;
		word->data[0] = ((value->integer * 60) >> 8) & 0xff;
		break;
	case CONFIG_ENC_LE_DECIMAL:	// Little endian decimal, take whole two bytes
		word->data[0] = (int) (value->rational * 256) & 0xff;
		word->data[1] = ((int) (value->rational * 256) >> 8) & 0xff;
		break;
	case CONFIG_ENC_BE_DECIMAL:	// Little endian decimal, take whole two bytes
		word->data[1] = (int) (value->rational * 256) & 0xff;
		word->data[0] = ((int) (value->rational * 256) >> 8) & 0xff;
		break;
	case CONFIG_ENC_PERCENT:	// Take a whole byte
		word->data[cfg->byte] = (float) value->integer / 100 * 256;
		break;
	case CONFIG_ENC_BIT_INT:	// Shift/mask integer into a byte
		shift = cfg->start_bit + 1 - cfg->bits;
		value_mask = (1 << cfg->bits) - 1;
		word_mask = ~value_mask & 0xffff;
		word->data[cfg->byte] &= word_mask;
		word->data[cfg->byte] |= (value->integer & value_mask) << shift;
		break;
	}
}

struct cochran_config_word cochran_config_set_value(const unsigned char *config, struct cochran_config *cfg,
	union cochran_config_value *value)
{
	struct cochran_config_word word;

	word = cochran_config_get_word(config, cfg->word);
	cochran_config_encode_value(&word, cfg, value);

	return word;
}

void cochran_config_print_label (struct cochran_config *cfg)
{
	char unit[32];

	if (cfg->units)
		sprintf(unit, " (%s)", cfg->units);
	else
		unit[0] = 0;

	switch (cfg->encoding) {
	case CONFIG_ENC_LE_INT:
	case CONFIG_ENC_BE_INT:
	case CONFIG_ENC_PERCENT:
	case CONFIG_ENC_BIT_INT:
	case CONFIG_ENC_LE_INT_SEC:
	case CONFIG_ENC_BE_INT_SEC:
		if (cfg->edit.type == CONFIG_FIELD_TOGGLE)
			printf("%s%s [%s/%s]", cfg->label, unit, cfg->edit.toggle.on_label, cfg->edit.toggle.off_label);
		else
			printf("%s%s [%d - %d]", cfg->label, unit, (int) cfg->edit.range.low, (int) cfg->edit.range.high);
		break;
	case CONFIG_ENC_LE_DECIMAL:
	case CONFIG_ENC_BE_DECIMAL:
		printf("%s (%s) [%5.2f - %5.2f]", cfg->label, cfg->units, cfg->edit.range.low, cfg->edit.range.high);
		break;
	}
}

void cochran_config_print_value(struct cochran_config *cfg, union cochran_config_value *value)
{
	switch (cfg->encoding) {
	case CONFIG_ENC_LE_INT:
	case CONFIG_ENC_BE_INT:
	case CONFIG_ENC_PERCENT:
	case CONFIG_ENC_BIT_INT:
	case CONFIG_ENC_LE_INT_SEC:
	case CONFIG_ENC_BE_INT_SEC:
		if (cfg->edit.type == CONFIG_FIELD_TOGGLE) {
			if (value->integer == 0)
				printf("%s", cfg->edit.toggle.on_label);
			else
				printf("%s", cfg->edit.toggle.off_label);
		} else {
			printf("%d", value->integer);
		}
		break;
	case CONFIG_ENC_LE_DECIMAL:
	case CONFIG_ENC_BE_DECIMAL:
		printf("%5.2f", value->rational);
		break;
	}
}

void cochran_config_print(struct cochran_config *cfg, union cochran_config_value *value)
{
	cochran_config_print_label(cfg);
	printf(": ");
	cochran_config_print_value(cfg, value);
	printf("\n");
}


void main(int argc, char **argv)
{
	unsigned char config[512];

	read(0, config, 512);

	struct cochran_config_word word, orig;
	union cochran_config_value value;

	int x = 0; 
	while (emc_cfg[x].word != -1) {
		word = cochran_config_get_word(config, emc_cfg[x].word);
		cochran_config_get_value(config, &emc_cfg[x], &value);
		printf("<%2d> %02x %02x ", x, word.data[0], word.data[1]);
		cochran_config_print(&emc_cfg[x], &value);
		x++;
	}

	int i = atoi(argv[1]);

	orig = word = cochran_config_get_word(config, emc_cfg[i].word);
	cochran_config_get_value(config, &emc_cfg[i], &value);



	switch (emc_cfg[i].encoding) {
	case CONFIG_ENC_LE_INT:
	case CONFIG_ENC_BE_INT:
	case CONFIG_ENC_PERCENT:
	case CONFIG_ENC_BIT_INT:
	case CONFIG_ENC_LE_INT_SEC:
	case CONFIG_ENC_BE_INT_SEC:
		value.integer = atoi(argv[2]);
		break;
	case CONFIG_ENC_LE_DECIMAL:
	case CONFIG_ENC_BE_DECIMAL:
		value.rational = atof(argv[2]);
		break;
	}

	word = cochran_config_set_value(config, &emc_cfg[i], &value);

	cochran_config_print_label(&emc_cfg[i]);	
	printf("\nFrom %02x %02x To %02x %02x\n", orig.data[0], orig.data[1], word.data[0], word.data[1]);
}
