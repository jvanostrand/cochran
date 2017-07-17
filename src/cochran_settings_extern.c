/*
*	cochran_settings.c
*
*	Copyright 2014, John Van Ostrand
*/

#include <string.h>
#include "cochran_settings.h"

#define EDIT_ON_OFF { CONFIG_FIELD_TOGGLE, { .toggle = { "On", "Off" } } }
#define EDIT_OFF_ON { CONFIG_FIELD_TOGGLE, { .toggle = { "Off", "On" } } }
#define EDIT_DIS_ENA { CONFIG_FIELD_TOGGLE, { .toggle = { "Disable", "Enable" } } }
#define EDIT_NOR_RED { CONFIG_FIELD_TOGGLE, { .toggle = { "Normal", "Reduced" } } }
#define EDIT_FIX_PRO { CONFIG_FIELD_TOGGLE, { .toggle = { "Fixed", "Proportional" } } }

#define EDIT_TIME_MIN { CONFIG_FIELD_RANGE, { .range = { 0, 59, 1 } } }
#define EDIT_TIME_HOUR { CONFIG_FIELD_RANGE, { .range = { 0, 23, 1 } } }
#define EDIT_EMC_CONSERVATISM { CONFIG_FIELD_RANGE, { .range = { 0, 75, 1 } } }
#define EDIT_CMD_CONSERVATISM { CONFIG_FIELD_RANGE, { .range = { 0, 50, 1 } } }
#define EDIT_EMC_PO2 { CONFIG_FIELD_RANGE, { .range = { 0.50, 1.50, 0.01 } } }
#define EDIT_CMD_PO2 { CONFIG_FIELD_RANGE, { .range = { 0.50, 1.59, 0.01 } } }
#define EDIT_EMC_DEPTH { CONFIG_FIELD_RANGE, { .range = { 0, 410, 1 } } }
#define EDIT_CMD_DEPTH { CONFIG_FIELD_RANGE, { .range = { 0, 330, 1 } } }
#define EDIT_EMC_DEPTH_ALARM { CONFIG_FIELD_RANGE, { .range = { 30, 410, 1 } } }
#define EDIT_CMD_DEPTH_ALARM { CONFIG_FIELD_RANGE, { .range = { 30, 330, 1 } } }
#define EDIT_BT_MINUTES { CONFIG_FIELD_RANGE, { .range = { 0, 999, 1 } } }
#define EDIT_SIT_MINUTES { CONFIG_FIELD_RANGE, { .range = { 10, 30, 1 } } }
#define EDIT_CNS_OTU { CONFIG_FIELD_RANGE, { .range = { 40, 80, 1 } } }
#define EDIT_ASCENT_ALARM { CONFIG_FIELD_RANGE, { .range = { 20, 60, 1 } } }
#define EDIT_VIEW_SECONDS { CONFIG_FIELD_RANGE, { .range = { 3, 10, 1 } } }
#define EDIT_LIGHT_SECONDS { CONFIG_FIELD_RANGE, { .range = { 0, 99, 1 } } }
#define EDIT_RESPONSE { CONFIG_FIELD_RANGE, { .range = { 0, 7, 1 } } }
#define EDIT_EMC_FO2_PO2 { CONFIG_FIELD_TOGGLE, { .toggle = { "FO2", "PO2" } } }
#define EDIT_EMC_O2_PCT { CONFIG_FIELD_RANGE, { .range = { 5, 99.9, 0.1 } } }
#define EDIT_CMD_O2_PCT { CONFIG_FIELD_RANGE, { .range = { 21, 50.0, 0.1 } } }
#define EDIT_EMC_HE_PCT { CONFIG_FIELD_RANGE, { .range = { 0, 95.0, 0.1 } } }

char *deco_disp_labels[] = { "Total", "Stop", "Both" };
float deco_disp_values[] = { 0, 1, 3 };

#define EDIT_DECO_DISP { CONFIG_FIELD_LIST, { .list = { 3, deco_disp_labels, deco_disp_values } } }
#define EDIT_UNITS { CONFIG_FIELD_TOGGLE, { .toggle = { "Imperial", "Metric" } } }


struct cochran_config emc_cfg[] = {
	{ 0x00a5, 0, 5, 6, CONFIG_ENC_BIT_INT, EDIT_TIME_MIN, "minute", "Alarm minute", NULL },
	{ 0x00a5, 1, 7, 1, CONFIG_ENC_BIT_INT, EDIT_ON_OFF, NULL, "Alarm clock", NULL },
	{ 0x00a5, 1, 4, 5, CONFIG_ENC_BIT_INT, EDIT_TIME_HOUR, "hour", "Alarm hour", NULL },
	{ 0x00a7, 0, 7, 8, CONFIG_ENC_PERCENT, EDIT_EMC_CONSERVATISM, NULL, "Conservatism", NULL },
	{ 0x00a8, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_PO2, "ata", "Normal dive PO2 set point/PO2 set point #1" },
	{ 0x00a9, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_PO2, "ata", "Decompression dive PO2 set point/PO2 set point #2" },
	{ 0x00b1, 0, 0, 0, CONFIG_ENC_LE_INT,  EDIT_EMC_DEPTH, "feet", "Blend #3 ascent depth activate" },
	{ 0x00b2, 0, 0, 0, CONFIG_ENC_LE_INT,  EDIT_EMC_DEPTH, "feet", "Blend #2 ascent depth activate" },
	{ 0x00b3, 0, 0, 0, CONFIG_ENC_LE_INT,  EDIT_BT_MINUTES, "minutes", "Blend #2 bottom time activate" },
	{ 0x00b6, 0, 0, 0, CONFIG_ENC_LE_INT_SEC,  EDIT_SIT_MINUTES, "minutes", "Post dive surface interval" },
	{ 0x00b7, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_PO2, "ata", "PO2 high point alarm"},
	{ 0x00b8, 0, 0, 0, CONFIG_ENC_LE_INT,  EDIT_EMC_DEPTH_ALARM, "feet", "Depth alarm"},
	{ 0x00ba, 0, 5, 6, CONFIG_ENC_BIT_INT, EDIT_ASCENT_ALARM, "ft/min", "Ascent rate alarm"},
	{ 0x00bb, 0, 6, 7, CONFIG_ENC_BIT_INT, EDIT_CNS_OTU, "CNS%", "CNS high point alarm"},
	{ 0x00bb, 1, 6, 7, CONFIG_ENC_BIT_INT, EDIT_CNS_OTU, "OTU", "OTU high point alarm"},
	{ 0x00bc, 0, 4, 5, CONFIG_ENC_BIT_INT, EDIT_VIEW_SECONDS, "seconds", "Alternate screen viewing time"},
	{ 0x00bc, 1, 6, 7, CONFIG_ENC_BIT_INT, EDIT_LIGHT_SECONDS, "seconds", "Taclite on time"},
	{ 0x00bd, 0, 6, 1, CONFIG_ENC_BIT_INT, EDIT_NOR_RED, NULL, "Temperature dependent conservatism"},
	{ 0x00bd, 0, 5, 1, CONFIG_ENC_BIT_INT, EDIT_FIX_PRO, NULL, "Ascent rate bar graph"},
	{ 0x00bd, 0, 2, 1, CONFIG_ENC_BIT_INT, EDIT_DIS_ENA, NULL, "Blend #2 switching"},
	{ 0x00bd, 0, 1, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Alititude as one zone"},
	{ 0x00bd, 1, 7, 2, CONFIG_ENC_BIT_INT, EDIT_DECO_DISP, NULL, "Decompresson time display"},
	{ 0x00bd, 1, 4, 1, CONFIG_ENC_BIT_INT, EDIT_DIS_ENA, NULL, "Blend #3 switching"},
	{ 0x00bd, 1, 3, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Variable ascent rate alarm"},
	{ 0x00bd, 1, 2, 3, CONFIG_ENC_BIT_INT, EDIT_RESPONSE, NULL, "Ascent rate response"},
	{ 0x00be, 0, 7, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Repetitive dive dependency N2"},
	{ 0x00be, 0, 3, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Training mode"},
	{ 0x00be, 0, 2, 1, CONFIG_ENC_BIT_INT, EDIT_EMC_FO2_PO2, NULL, "Constant mode computation"},
	{ 0x00be, 1, 5, 3, CONFIG_ENC_BIT_INT, EDIT_RESPONSE, NULL, "Remaining time response"},
	{ 0x00be, 1, 0, 1, CONFIG_ENC_BIT_INT, EDIT_UNITS, NULL, "Display units"},
	{ 0x00bf, 0, 6, 1, CONFIG_ENC_BIT_INT, EDIT_ON_OFF, NULL, "Audible alarms"},
	{ 0x00bf, 0, 5, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Clock functions"},
	{ 0x00bf, 0, 4, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Ceiling display, divide by 10"},
	{ 0x00bf, 0, 1, 1, CONFIG_ENC_BIT_INT, EDIT_DIS_ENA, NULL, "Gas #2 as first gas"},
	{ 0x00bf, 0, 0, 1, CONFIG_ENC_BIT_INT, EDIT_DIS_ENA, NULL, "Enable helium computations"},
	{ 0x00bf, 1, 2, 1, CONFIG_ENC_BIT_INT, EDIT_DIS_ENA, NULL, "Automatic PO2/FO2 switching"},
	{ 0x00bf, 1, 1, 1, CONFIG_ENC_BIT_INT, EDIT_DIS_ENA, NULL, "Touch programming PO2/FO2 switch"},
	{ 0x00d1, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_O2_PCT, "percentage", "Blend #1 O2"},
	{ 0x00d2, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_O2_PCT, "percentage", "Blend #2 O2"},
	{ 0x00d3, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_O2_PCT, "percentage", "Blend #3 O2"},
	{ 0x00db, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_HE_PCT, "percentage", "Blend #1 He"},
	{ 0x00dc, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_HE_PCT, "percentage", "Blend #2 He"},
	{ 0x00dd, 0, 0, 0, CONFIG_ENC_LE_DECIMAL, EDIT_EMC_HE_PCT, "percentage", "Blend #3 He"},
	{ -1, 0, 0, 0, 0, {0}, NULL, NULL}
};

struct cochran_config cmd_cfg[] = {
	{ 0x003c, 0, 5, 6, CONFIG_ENC_BIT_INT, EDIT_TIME_MIN, "minute", "Alarm minute", NULL },
	{ 0x003c, 1, 7, 1, CONFIG_ENC_BIT_INT, EDIT_ON_OFF, NULL, "Alarm clock", NULL },
	{ 0x003c, 1, 4, 5, CONFIG_ENC_BIT_INT, EDIT_TIME_HOUR, "hour", "Alarm hour", NULL },
	{ 0x003e, 0, 7, 8, CONFIG_ENC_PERCENT, EDIT_EMC_CONSERVATISM, NULL, "Conservatism", NULL },
	{ 0x0043, 0, 0, 0, CONFIG_ENC_BE_DECIMAL, EDIT_CMD_O2_PCT, "percentage", "Blend #1 O2"},
	{ 0x0046, 0, 0, 0, CONFIG_ENC_BE_INT_SEC,  EDIT_SIT_MINUTES, "minutes", "Post dive surface interval" },
	{ 0x0048, 0, 0, 0, CONFIG_ENC_BE_DECIMAL, EDIT_EMC_PO2, "ata", "PO2 high point alarm"},
	{ 0x0049, 0, 0, 0, CONFIG_ENC_BE_INT,  EDIT_EMC_DEPTH_ALARM, "feet", "Depth alarm"},
	{ 0x004b, 0, 5, 6, CONFIG_ENC_BIT_INT, EDIT_ASCENT_ALARM, "ft/min", "Ascent rate alarm"},
	{ 0x004c, 0, 6, 7, CONFIG_ENC_BIT_INT, EDIT_CNS_OTU, "CNS%", "CNS high point alarm"},
	{ 0x004c, 1, 6, 7, CONFIG_ENC_BIT_INT, EDIT_CNS_OTU, "OTU", "OTU high point alarm"},
	{ 0x004d, 0, 4, 5, CONFIG_ENC_BIT_INT, EDIT_VIEW_SECONDS, "seconds", "Alternate screen viewing time"},
	{ 0x004d, 1, 6, 7, CONFIG_ENC_BIT_INT, EDIT_LIGHT_SECONDS, "seconds", "Taclite on time"},
	{ 0x004e, 0, 5, 1, CONFIG_ENC_BIT_INT, EDIT_FIX_PRO, NULL, "Ascent rate bar graph"},
	{ 0x004e, 0, 2, 1, CONFIG_ENC_BIT_INT, EDIT_NOR_RED, NULL, "Temperature dependent conservatism"},
	{ 0x004e, 0, 1, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Alititude as one zone"},
	{ 0x004e, 1, 7, 2, CONFIG_ENC_BIT_INT, EDIT_DECO_DISP, NULL, "Decompresson time display"},
	{ 0x004e, 1, 3, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Variable ascent rate alarm"},
	{ 0x004e, 1, 2, 3, CONFIG_ENC_BIT_INT, EDIT_RESPONSE, NULL, "Ascent rate response"},
	{ 0x004f, 0, 7, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Repetitive dive dependency N2"},
	{ 0x004f, 0, 6, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Training mode"},
	{ 0x004f, 0, 4, 1, CONFIG_ENC_BIT_INT, EDIT_UNITS, NULL, "Display units"},
	{ 0x004f, 1, 5, 3, CONFIG_ENC_BIT_INT, EDIT_RESPONSE, NULL, "Remaining time response"},
	{ 0x0050, 0, 6, 1, CONFIG_ENC_BIT_INT, EDIT_ON_OFF, NULL, "Audible alarms"},
	{ 0x0050, 0, 5, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Clock functions"},
	{ 0x0050, 0, 4, 1, CONFIG_ENC_BIT_INT, EDIT_OFF_ON, NULL, "Ceiling display, divide by 10"},
	{ -1, 0, 0, 0, 0, {0}, NULL, NULL}
};
