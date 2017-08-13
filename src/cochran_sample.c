#include <stdio.h>
#include <string.h>

#include "cochran.h"
#include "cochran_log.h"
#include "cochran_sample.h"

typedef struct cochran_events_t {
	unsigned char code;
	int size;
	char *description;
} cochran_events_t;

static const cochran_events_t cochran_events[] = {
    {0xA8, 1, "Entered PDI mode" },
    {0xA9, 1, "Exited PDI mode" },
    {0xAB, 5, "Deco ceiling lowered" },
    {0xAD, 5, "Deco ceiling raised" },
    {0xBD, 1, "Switched to nomal PO2 setting" },
    {0xC0, 1, "Switched to FO2 21% mode" },
    {0xC1, 1, "Ascent rate greater than limit" },
    {0xC2, 1, "Low battery warning" },
    {0xC3, 1, "CNS Oxygen toxicity warning" },
    {0xC4, 1, "Depth exceeds user set point" },
    {0xC5, 1, "Entered decompression mode" },
    {0xC7, 1, "Entered Gauge mode (e.g. locked out)" },
    {0xC8, 1, "PO2 too high" },
    {0xCC, 1, "Low Cylinder 1 pressure" },
    {0xCE, 1, "Non-decompression warning" },
    {0xCD, 1, "Switched to deco blend" },
    {0xD0, 1, "Breathing rate alarm" },
    {0xD3, 1, "Low gas 1 flow rate" },
    {0xD6, 1, "Depth is less than ceiling" },
    {0xD8, 1, "End decompression mode" },
    {0xE1, 1, "End ascent rate warning" },
    {0xE2, 1, "Low SBAT battery warning" },
    {0xE3, 1, "Switched to FO2 mode" },
    {0xE5, 1, "Switched to PO2 mode" },
    {0xEE, 1, "End non-decompresison warning" },
    {0xEF, 1, "Switch to blend 2" },
    {0xF0, 1, "Breathing rate alarm" },
    {0xF3, 1, "Switch to blend 1" },
    {0xF6, 1, "End Depth is less than ceiling" },
	{0x00, 1, "Unknown event" },
};


/*
* Bytes expected after a inter-dive event code
*/
static int cochran_sample_parse_inter_dive (cochran_family_t family, unsigned char code) {
	int x = 0;

	int gem_event_bytes[15][2] = {  {0x00, 10}, {0x02, 17}, {0x06, 18},
									{0x07, 18}, {0x08, 18}, {0x09, 18},
									{0x0a, 18}, {0x0c, 18}, {0x0e, 18},
									{  -1,  0} };
	int cmdr_event_bytes[15][2] = {	{0x00, 16}, {0x01, 20}, {0x02, 17},
									{0x03, 16}, {0x06, 18}, {0x07, 18},
									{0x08, 18}, {0x09, 18}, {0x0a, 18},
									{0x0b, 18}, {0x0c, 18}, {0x0d, 18},
                                	{0x0e, 18}, {0x10, 20},
									{  -1,  0} };
	int emc_event_bytes[15][2] = {	{0x00, 18}, {0x01, 22}, {0x02, 19},
									{0x03, 18}, {0x06, 20}, {0x07, 20},
									{0x0a, 20}, {0x0b, 20}, {0x0f, 18},
									{0x10, 20},
									{  -1,  0} };

	switch (family) {
	case FAMILY_COMMANDER_I:
		// doesn't have inter-dive events;
		break;
	case FAMILY_GEMINI:
		while (gem_event_bytes[x][0] != code && gem_event_bytes[x][0] != -1)
			x++;
		return gem_event_bytes[x][1];
		break;
	case FAMILY_COMMANDER_II:
	case FAMILY_COMMANDER_III:
		while (cmdr_event_bytes[x][0] != code && cmdr_event_bytes[x][0] != -1)
			x++;
		return cmdr_event_bytes[x][1];
		break;
	case FAMILY_EMC:
		while (emc_event_bytes[x][0] != code && emc_event_bytes[x][0] != -1)
			x++;
		return emc_event_bytes[x][1];
		break;
	}
	return(0);
}

/*
 * cochran_sample_parse_nemesis
 *
 * I think this was one of the first cochrans which sampled tank pressure.
 * The sample has a two byte header:
 *     sample[0] is start temperature in half degress F
 *     sample[1] is start depth in half feet
 * Samples thereafter consist of two bytes
 *     - depth sample is a byte where bit 0x80 is 0. Bit 0x40 indicates a
 *     negative value, bites 0x3f indicate change of depth in half feet.
 *     - tank sample is the next byte. Bit 0x80, if set, indicates a
 *     negative value, bits 0x7f indicate tank pressure change in 2 psi
 *     increments.
 * If the depth sample byte has bit 0x80 set and bits 0x60 clear then it's
 * a temperature change byte. Bit 0x10 if set indicates a negative
 * temperature change, bits 0xf indicate temperature change in half degress
 * F.
 * If the depth sample byte has but 0x80 set and one or both of bits 0x60
 * set then the byte is an event byte.
 */

void cochran_sample_parse_nemesis (const cochran_log_t *log, const unsigned char *samples, unsigned int size, cochran_sample_callback_t callback, void *userdata) {
	unsigned int sample_size = 2;
	unsigned int offset = 0;
	unsigned int sample_cnt = 0;
	cochran_sample_t sample = {0};

	double temp = samples[0] / 2.0;
	double depth = samples[1] / 2.0;
	unsigned int tank_pressure = log->tank_pressure_start;
	unsigned int deco_ceiling = 0;
	unsigned int deco_time = 0;

	// Issue initial depth/temp/tank pressure samples
	if (callback) {
		sample.type = SAMPLE_TEMP;
		sample.value.temp = temp;
		sample.raw.data = samples;
		sample.raw.size = 1;
		callback(0, &sample, userdata);

		sample.type = SAMPLE_DEPTH;
		sample.value.depth = depth;
		sample.raw.data = samples + 1;
		sample.raw.size = 1;
		callback(0, &sample, userdata);

		sample.type = SAMPLE_TANK_PRESSURE;
		sample.value.tank_pressure = tank_pressure;
		sample.raw.size = 0;
		callback(0, &sample, userdata);
	}

	offset = 2;

	while (offset < size) {
		const unsigned char *s = samples + offset;

		// Check for special sample (event or a temp change for early Commanders
		if (s[0] & 0x80 && s[0] & 0x60) {
			// Event code

			// Locate event info
			int e = 0;
			while (cochran_events[e].code && cochran_events[e].code != *s) e++;

			// Issue event sample
			sample.type = SAMPLE_EVENT;
			sample.value.event = cochran_events[e].description;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			switch (*s) {
			case 0xAB:		// Lower deco ceiling (deeper)
				deco_ceiling += 10;
				break;
			case 0xAD:		// Raise deco ceiling (shallower)
				deco_ceiling -= 10;
				break;
			case 0xC5:
				deco_time = 1;
				break;
			case 0xC8:
				deco_time = 0;
				break;
			default:
				// Just an event, we're done here.
				offset++;
				continue;
			}

			// Issue deco sample
			sample.type = SAMPLE_DECO;
			sample.value.deco.time = deco_time; // Minutes
			sample.value.deco.ceiling = deco_ceiling; // feet
			sample.raw.data = 0;
			sample.raw.size = 0;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);
			offset++;
			continue;
		} else if (s[0] & 0x80) {
			// Temp
			if (*s & 0x10)
				temp -= (*s & 0x0f) / 2.0;
			else
				temp += (*s & 0x0f) / 2.0;
			sample.type = SAMPLE_TEMP;
			sample.value.temp = temp;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);
			offset++;
		} else {
			// Depth
			if (*s & 0x40)
				depth -= (*s & 0x3f) / 2.0;
			else
				depth += (*s & 0x3f) / 2.0;
			sample_cnt++;
			sample.type = SAMPLE_DEPTH;
			sample.value.temp = depth;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			// Tank pressure
			if (s[1] & 0x80)
				tank_pressure -= (s[1] & 0x0f);
			else
				tank_pressure += (s[1] & 0x0f);

			sample.type = SAMPLE_TANK_PRESSURE;
			sample.value.tank_pressure = tank_pressure;
			sample.raw.data = s + 1;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);
			offset += sample_size;
		}
	}
}


/*
 * cochran_sample_parse_I
 *
 * The simplest of Cochran sample formats, this consists of single byte samples.
 * The sample has a two byte header:
 *     sample[0] is start temperature in half degress F
 *     sample[1] is start depth in half feet
 * Depth samples consist of a byte where bit 0x80 is 0. Bit 0x40 indicates a
 * negative value, bits 0x3f indicate change of depth in half feet.
 * A depth sample indicates the start of a new profile interval, i.e.
 * sample_cnt x profile_interval = time in seconds.
 * A temperature sample is a byte with 0x80 set and *none* of bits 0x60 set. Bit
 * 0x10 indicates a negative value. Bits 0x0f represent the change in temperature
 * in half degrees F.
 * An event byte (e.g. an alarm or deco change) is identified by bit 0x80 set and
 * one or more of bits 0x60 set.
 */

void cochran_sample_parse_I (const cochran_log_t *log, const unsigned char *samples, unsigned int size, cochran_sample_callback_t callback, void *userdata) {
	unsigned int sample_size = 1;
	unsigned int offset = 0;
	unsigned int sample_cnt = 0;
	cochran_sample_t sample = {0};

	double temp = samples[0] / 2.0;
	double depth = samples[1] / 2.0;
	unsigned int deco_ceiling = 0;
	unsigned int deco_time = 0;

	// Issue initial depth/temp samples
	if (callback) {
		sample.type = SAMPLE_TEMP;
		sample.value.temp = temp;
		sample.raw.data = samples;
		sample.raw.size = 1;
		callback(0, &sample, userdata);

		sample.type = SAMPLE_DEPTH;
		sample.value.depth = depth;
		sample.raw.data = samples + 1;
		sample.raw.size = 1;
		callback(0, &sample, userdata);
	}

	offset = 2;

	while (offset < size) {
		const unsigned char *s = samples + offset;

		// Check for special sample (event or a temp change for early Commanders
		if (s[0] & 0x80 && s[0] & 0x60) {
			// Event code

			// Locate event info
			int e = 0;
			while (cochran_events[e].code && cochran_events[e].code != *s) e++;

			// Issue event sample
			sample.type = SAMPLE_EVENT;
			sample.value.event = cochran_events[e].description;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			switch (*s) {
			case 0xAB:		// Lower deco ceiling (deeper)
				deco_ceiling += 10;
				break;
			case 0xAD:		// Raise deco ceiling (shallower)
				deco_ceiling -= 10;
				break;
			case 0xC5:
				deco_time = 1;
				break;
			case 0xC8:
				deco_time = 0;
				break;
			default:
				// Just an event, we're done here.
				offset++;
				continue;
			}

			// Issue deco sample
			sample.type = SAMPLE_DECO;
			sample.value.deco.time = deco_time; // Minutes
			sample.value.deco.ceiling = deco_ceiling; // feet
			sample.raw.data = 0;
			sample.raw.size = 0;
		} else if (s[0] & 0x80) {
			// Temp
			if (*s & 0x10)
				temp -= (*s & 0x0f) / 2.0;
			else
				temp += (*s & 0x0f) / 2.0;
			sample.type = SAMPLE_TEMP;
			sample.value.temp = temp;
			sample.raw.data = s;
			sample.raw.size = 1;
		} else {
			// Depth
			if (*s & 0x40)
				depth -= (*s & 0x3f) / 2.0;
			else
				depth += (*s & 0x3f) / 2.0;
			sample_cnt++;
			sample.type = SAMPLE_DEPTH;
			sample.value.temp = depth;
			sample.raw.data = s;
			sample.raw.size = 1;
		}

		if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);
		offset += sample_size;
	}
}


/*
 * cochran_sample_parse_II
 *
 * This log format has a header of inter-dive events of varying lengths.
 * These inter-dive events could have occured seconds or years before a dive.
 *
 * Samples are in two bytes. With single or multi-byte events inserted as
 * needed between samples.
 *
 * A sample can be distinguished from an event by inspecting bit 0x80. If it's
 * set, the byte is an event. Some events have data bytes that follow.
 *
 * Depth change is the first byte of every sample. Bit 0x40 indicates a negative
 * value, bits 0x3f represent the change in depth in 1/4 feet. The starting
 * depth is not 0 and must be obtained from the log.
 *
 * The second byte alternates between ascent rate (even samples) and temperature
 * (odd samples.)
 *
 * Ascent rate (in even samples) is measured in 1/4 feet/minute. Bit 0x80 indicates
 * a *positive* value, bits 0x7f represent the ascent rate in 1/4 feet per minute.
 *
 * Temperature (in odd samples) is measured in half degrees above 20 Fahrenheit.
 * There are no negative temperature values.
 */

void cochran_sample_parse_II(const cochran_log_t *log, const unsigned char *samples, unsigned int size, cochran_sample_callback_t callback, void *userdata) {
	unsigned int sample_size = 2;
	unsigned int offset = 0;
	unsigned int sample_cnt = 0;
	cochran_sample_t sample = {0};

	// process inter-dive event
	//if (samples[offset] != 0x40) {
	if (cochran_sample_parse_inter_dive(FAMILY_COMMANDER_II, samples[offset])) {
		while ( offset < size && (samples[offset] & 0x80) == 0 && samples[offset] != 0x40) {
			int event_size = 0;
			sample.type = SAMPLE_INTERDIVE;
			sample.value.interdive.code = samples[offset];
			sample.value.interdive.data = 0;
			event_size = cochran_sample_parse_inter_dive(FAMILY_COMMANDER_II, samples[offset]) + 1;
			if (offset + event_size < size && event_size > 5) {
				time_t t = array_uint32_le(samples + offset + 1) + COCHRAN_EPOCH;
				localtime_r(&t, &(sample.value.interdive.time));
				sample.value.interdive.size = event_size - 5;
				sample.value.interdive.data = samples + offset + 5;
				if (callback) callback(0, &sample, userdata);
			}
			offset += event_size;
		}
	}

	double depth = log->depth_start;
	double temp = log->temp_start;
	unsigned int deco_ceiling = 0;
	int deco_time = 0;

	// Issue initial depth/temp samples
	if (callback) {
		sample.type = SAMPLE_DEPTH;
		sample.value.depth = depth;
		callback(0, &sample, userdata);

		sample.type = SAMPLE_TEMP;
		sample.value.temp = temp;
		callback(0, &sample, userdata);
	}

	while (offset < size) {
		const unsigned char *s;
		s = samples + offset;

		// Check for an event
		if (*s & 0x80) {
			// Locate event info
			int e = 0;
			while (cochran_events[e].code && cochran_events[e].code != *s) e++;

			// Issue event sample
			sample.type = SAMPLE_EVENT;
			sample.value.event = cochran_events[e].description;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			switch (*s) {
			case 0xAB:		// Lower deco ceiling (deeper)
				deco_ceiling += 10; // feet
				if (offset + 4 < size && callback) {
					sample.type = SAMPLE_DECO_FIRST_STOP;
					sample.value.deco.ceiling = deco_ceiling; // feet
					sample.value.deco.time = array_uint16_le(s + 1) + 1; // Minutes
					sample.raw.data = s + 1;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);

					sample.type = SAMPLE_DECO;
					sample.value.deco.time = array_uint16_le(s + 3) + 1; // Minutes
					sample.raw.data = s + 3;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);
				}
				offset += 4;
				break;
			case 0xAD:		// Raise deco ceiling (shallower)
				deco_ceiling -= 10; // feet
				if (offset + 4 < size && callback) {
					sample.type = SAMPLE_DECO_FIRST_STOP;
					sample.value.deco.ceiling = deco_ceiling; // feet
					sample.value.deco.time = array_uint16_le(s + 1) + 1; // Minutes
					sample.raw.data = s + 1;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);

					sample.type = SAMPLE_DECO;
					sample.value.deco.time = array_uint16_le(s + 3) + 1; // Minutes
					sample.raw.data = s + 3;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);
				}
				offset += 4;
				break;
			case 0xC5:
				deco_time = 1;
				break;
			case 0xC8:
				deco_time = 0;
				break;
			default:
				// Just an event, we're done here.
				offset++;
				continue;
			}
			offset++;
		} else {
			// Parse normal sample
			sample_cnt++;

			if (*s & 0x40)
				depth -= (*s & 0x3f) / 4.0;
			else
				depth += (*s & 0x3f) / 4.0;

			sample.type = SAMPLE_DEPTH;
			sample.value.depth = depth;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			// Parse second byte
			if (((sample_cnt - 1) % 2) == 0) {
				// Temp sample
				sample.type = SAMPLE_TEMP;
				sample.value.temp = (s[1] & 0x7f) / 2.0 + 20;
			} else {
				// Ascent sample
				sample.type = SAMPLE_ASCENT_RATE;
				if (s[1] & 0x80)
					sample.value.ascent_rate = (s[1] & 0x7f) / 4.0;
				else
					sample.value.ascent_rate = -(s[1] & 0x7f) / 4.0;
			}
			sample.raw.data = (s + 1);
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);
		}
		offset += sample_size;
	}
}


/*
 * cochran_sample_parse_gem
 *
 * Gemini models sample tank pressure and calculate gas consumption rates. These models
 * have a sample format that allows for the extra data.
 *
 * Some models log inter-dive events, while early models did not. Because inter-dive
 * events aren't mandatory we can use the same algorithm for both.
 *
 * Samples size is two bytes but there is a four sample rotation. The first byte of
 * every sample is depth but the second byte rotates through ascent_rate,
 * gas_consumption_rate, tank_pressure_change and temp, in that order.
 *
 * Events appear between any sample and can be identified because bit 0x80 is set. So
 * when depth change is expected but has bit 0x80 set, it's really an event.
 *
 * Depth change is the first byte of every sample. Bit 0x40 indicates a negative value
 * and bits 0x3f represent the change in depth in 1/4 foot increments.
 *
 * Ascent rate is the first of the second bytes (sample_cnt % 4 == 0). Ascent rate
 * uses 0x80 as the *positive* bit and the ascent rate in 1/4 feet per minute is
 * stored in bits 0x7f.
 *
 * Gas consumption rate is second in the sample rotation (sample_cnt % 4 == 1). Bit
 * 0x80, if set, also indicates a negative value and consumption rate in 1/2 PSI is
 * stored in buts 0x7f.
 *
 * Tank pressure change is third in the sample rotation (sample_cnt % 4 == 3). Bit
 * 0x80, if set, indicated a negative value. Tank pressure change is stores as 1/4
 * PSI in bits 0x7f.
 *
 * Temperature is stored as 1/2 degrees F above 20F. There is no negative. This is
 * fourth and last in the sample rotation (sample_cnt % 4 == 3);
 */

void cochran_sample_parse_gem(const cochran_log_t *log, const unsigned char *samples, unsigned int size, cochran_sample_callback_t callback, void *userdata) {
	unsigned int sample_size = 2;
	unsigned int offset = 0;
	unsigned int sample_cnt = 0;
	cochran_sample_t sample = {0};

	// process inter-dive event
	if (samples[offset] != 0x40) {
		while ( offset < size && (samples[offset] & 0x80) == 0 && samples[offset] != 0x40) {
			int event_size = 0;
			sample.type = SAMPLE_INTERDIVE;
			sample.value.interdive.code = samples[offset];
			sample.value.interdive.data = 0;
			event_size = cochran_sample_parse_inter_dive(FAMILY_COMMANDER_II, samples[offset]) + 1;
			if (offset + event_size < size && event_size > 5) {
				time_t t = array_uint32_le(samples + offset + 1) + COCHRAN_EPOCH;
				localtime_r(&t, &(sample.value.interdive.time));
				sample.value.interdive.size = event_size - 5;
				sample.value.interdive.data = samples + offset + 5;
				if (callback) callback(0, &sample, userdata);
			}
			offset += event_size;
		}
	}

	double depth = log->depth_start;
	double temp = log->temp_start;
	double tank_pressure = log->tank_pressure_start;
	double gas_consumption_rate = 0;
	unsigned int deco_ceiling = 0;
	int deco_flag = 0;

	// Issue initial depth/temp samples
	if (callback) {
		sample.type = SAMPLE_DEPTH;
		sample.value.depth = depth;
		sample.raw.size = 0;
		callback(0, &sample, userdata);

		sample.type = SAMPLE_TEMP;
		sample.value.temp = temp;
		sample.raw.size = 0;
		callback(0, &sample, userdata);

		sample.type = SAMPLE_TANK_PRESSURE;
		sample.value.tank_pressure = tank_pressure;
		sample.raw.size = 0;
		callback(0, &sample, userdata);
	}

	while (offset < size) {
		const unsigned char *s;
		s = samples + offset;

		// Check for an event
		if (*s & 0x80) {
			// Locate event info
			int e = 0;
			while (cochran_events[e].code && cochran_events[e].code != *s) e++;

			// Issue event sample
			sample.type = SAMPLE_EVENT;
			sample.value.event = cochran_events[e].description;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			switch (*s) {
			case 0xAB:		// Lower deco ceiling (deeper)
				deco_ceiling += 10; // feet
				if (offset + 4 < size && callback) {
					sample.type = SAMPLE_DECO_FIRST_STOP;
					sample.value.deco.ceiling = deco_ceiling; // feet
					sample.value.deco.time = array_uint16_le(s + 1) + 1; // Minutes
					sample.raw.data = s + 1;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);

					sample.type = SAMPLE_DECO;
					sample.value.deco.time = array_uint16_le(s + 3) + 1; // Minutes
					sample.raw.data = s + 3;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);
				}
				offset += 4;
				break;
			case 0xAD:		// Raise deco ceiling (shallower)
				deco_ceiling -= 10; // feet
				if (offset + 4 < size && callback) {
					sample.type = SAMPLE_DECO_FIRST_STOP;
					sample.value.deco.ceiling = deco_ceiling; // feet
					sample.value.deco.time = array_uint16_le(s + 1) + 1; // Minutes
					sample.raw.data = s + 1;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);

					sample.type = SAMPLE_DECO;
					sample.value.deco.time = array_uint16_le(s + 3) + 1; // Minutes
					sample.raw.data = s + 3;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);
				}
				offset += 4;
				break;
			case 0xC5:
				deco_flag = 1;
				break;
			case 0xC8:
				deco_flag = 0;
				break;
			default:
				// Just an event, we're done here.
				offset++;
				continue;
			}

			offset++;
		} else {
			// Parse normal sample
			sample_cnt++;

			if (*s & 0x40)
				depth -= (*s & 0x3f) / 4.0;
			else
				depth += (*s & 0x3f) / 4.0;


			sample.type = SAMPLE_DEPTH;
			sample.value.depth = depth;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			// Parse second byte
			switch ((sample_cnt - 1) % 4) {
			case 0:
				// Ascent sample
				sample.type = SAMPLE_ASCENT_RATE;
				if (s[1] & 0x80)
					sample.value.ascent_rate = (s[1] & 0x7f) / 4.0;
				else
					sample.value.ascent_rate = -(s[1] & 0x7f) / 4.0;
				break;
			case 1:
				// Gas consumption rate
				sample.type = SAMPLE_GAS_CONSUMPTION_RATE;
				if (s[1] & 0x80)
					gas_consumption_rate -= (s[1] & 0x7f) / 4.0;
				else
					gas_consumption_rate += (s[1] & 0x7f) / 4.0;
				break;
			case 2:
				// Gas consumption rate
				sample.type = SAMPLE_TANK_PRESSURE;
				if (s[1] & 0x80)
					tank_pressure -= (s[1] & 0x7f) / 4.0;
				else
					tank_pressure += (s[1] & 0x7f) / 4.0;
				sample.value.tank_pressure = tank_pressure;
				break;
			case 3:
				// Temp sample
				sample.type = SAMPLE_TEMP;
				sample.value.temp = (s[1] & 0x7f) / 2.0 + 20;
				break;
			}
			sample.raw.data = s + 1;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			offset += sample_size;
		}
	}
}



/*
 *
 * Parse sample data, extract events and build a dive
 */

void cochran_sample_parse_emc(const cochran_log_t *log, const unsigned char *samples, unsigned int size, cochran_sample_callback_t callback, void *userdata) {
	int sample_size = 3;
	unsigned int offset = 0;
	unsigned int sample_cnt = 0;
	cochran_sample_t sample = {0};

	// process inter-dive event
	if (samples[offset] != 0x40) {
		while ( offset < size && (samples[offset] & 0x80) == 0 && samples[offset] != 0x40) {
			int event_size = 0;
			sample.type = SAMPLE_INTERDIVE;
			sample.value.interdive.code = samples[offset];
			sample.value.interdive.data = 0;
			event_size = cochran_sample_parse_inter_dive(FAMILY_EMC, samples[offset]) + 1;
			if (offset + event_size < size && event_size > 5) {
				time_t t = array_uint32_le(samples + offset + 1) + COCHRAN_EPOCH;
				localtime_r(&t, &(sample.value.interdive.time));
				sample.value.interdive.size = event_size - 5;
				sample.value.interdive.data = samples + offset + 5;
				sample.raw.data = samples + offset;
				sample.raw.size = event_size;
				if (callback) callback(0, &sample, userdata);
			}
			offset += event_size;
		}
	}

	double depth = log->depth_start;
	double temp = log->temp_start;
	unsigned int deco_ceiling = 0;
	unsigned int deco_flag = 0;

	// Issue initial depth/temp samples
	if (callback) {
		sample.type = SAMPLE_DEPTH;
		sample.value.depth = depth;
		sample.raw.data = 0;
		sample.raw.size = 0;
		callback(0, &sample, userdata);

		sample.type = SAMPLE_TEMP;
		sample.value.temp = temp;
		sample.raw.data = 0;
		sample.raw.size = 0;
		callback(0, &sample, userdata);
	}

	// Process samples
	while (offset < size) {
		const unsigned char *s;
		s = samples + offset;

		// Check for an event
		if (*s & 0x80) {
			// Locate event info
			int e = 0;
			while (cochran_events[e].code && cochran_events[e].code != *s) e++;

			// Issue event sample
			sample.type = SAMPLE_EVENT;
			sample.value.event = cochran_events[e].description;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			switch (*s) {
			case 0xAB:		// Lower deco ceiling (deeper)
				deco_ceiling += 10; // feet
				if (offset + 4 < size && callback) {
					sample.type = SAMPLE_DECO_FIRST_STOP;
					sample.value.deco.ceiling = deco_ceiling; // feet
					sample.value.deco.time = array_uint16_le(s + 1) + 1; // Minutes
					sample.raw.data = s + 1;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);

					sample.type = SAMPLE_DECO;
					sample.value.deco.time = array_uint16_le(s + 3) + 1; // Minutes
					sample.raw.data = s + 3;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);
				}
				offset += 4;
				break;
			case 0xAD:		// Raise deco ceiling (shallower)
				deco_ceiling -= 10; // feet
				if (offset + 4 < size && callback) {
					sample.type = SAMPLE_DECO_FIRST_STOP;
					sample.value.deco.ceiling = deco_ceiling; // feet
					sample.value.deco.time = array_uint16_le(s + 1) + 1; // Minutes
					sample.raw.data = s + 1;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);

					sample.type = SAMPLE_DECO;
					sample.value.deco.time = array_uint16_le(s + 3) + 1; // Minutes
					sample.raw.data = s + 3;
					sample.raw.size = 2;
					callback(sample_cnt * log->profile_interval, &sample, userdata);
				}
				offset += 4;
				break;
			case 0xC5:
				deco_flag = 1;
				break;
			case 0xC8:
				deco_flag = 0;
				break;
			default:
				// Just an event, we're done here.
				offset++;
				continue;
			}

			offset++;
		} else {
			// Parse normal sample
			sample_cnt++;

			if (*s & 0x40)
				depth -= (*s & 0x3f) / 4.0;
			else
				depth += (*s & 0x3f) / 4.0;


			sample.type = SAMPLE_DEPTH;
			sample.value.depth = depth;
			sample.raw.data = s;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			// Parse second byte
			switch ((sample_cnt - 1) % 4) {
			case 0:
				// Ascent sample
				sample.type = SAMPLE_ASCENT_RATE;
				if (s[1] & 0x80)
					sample.value.ascent_rate = (s[1] & 0x7f) / 4.0;
				else
					sample.value.ascent_rate = -(s[1] & 0x7f) / 4.0;
				break;
			case 1:
				// Temp sample
				sample.type = SAMPLE_TEMP;
				sample.value.temp = (s[1] & 0x7f) / 2.0 + 20;
				break;
			}
			sample.raw.data = s + 1;
			sample.raw.size = 1;
			if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);

			// Parse third byte samples, NDL and deco information
			unsigned char temp_sample[4];
			switch ((sample_cnt - 1) % 24) {
			case 19:
				// Tissue samples
				sample.type = SAMPLE_TISSUES;
				for (int i = 0; i < 20; i++)
					sample.value.tissues[i] = *(s + 2 - (19 - i) * sample_size);
				sample.raw.data = sample.value.tissues;
				sample.raw.size = 20;
				if (callback) callback(sample_cnt * log->profile_interval, &sample, userdata);
				break;
			case 20:
				temp_sample[0] = s[2];
				temp_sample[1] = s[5];
				sample.raw.data = temp_sample;
				sample.raw.size = 2;
				if (deco_flag) {
					// Deepest stop time
					sample.type = SAMPLE_DECO_FIRST_STOP;
					sample.value.deco.time = array_uint16_le(temp_sample) + 1; // minutes
					sample.value.deco.ceiling = deco_ceiling;
				} else {
					// NDL
					sample.type = SAMPLE_NDL;
					sample.value.ndl = array_uint16_le(temp_sample) + 1; // minutes
				}
				if (callback)
					callback(sample_cnt * log->profile_interval, &sample, userdata);
				break;
			case 22:
				if (deco_flag) {
					// Total stop time
					sample.type = SAMPLE_DECO;
					temp_sample[0] = s[2];
					temp_sample[1] = s[5];
					sample.value.deco.time = array_uint16_le(temp_sample) + 1; // minutes
					sample.raw.data = temp_sample;
					sample.raw.size = 2;
					if (callback)
						callback(sample_cnt * log->profile_interval, &sample, userdata);
				}
				break;
			}
			offset += sample_size;
		}
	}
}


cochran_sample_parser_t cochran_sample_get_parser(const unsigned char *model) {

	struct parser_table {
		const char *model;
		cochran_sample_parser_t parser;
		const char *description;
	};

	struct parser_table parser[] = {
		{ "017", cochran_sample_parse_I,	"Early Commander" },
		{ "102", cochran_sample_parse_gem,	"Early Gemini" },
		{ "114", cochran_sample_parse_nemesis,	"Nemesis" },
		{ "120", cochran_sample_parse_I,	"Early Commander" },
		{ "124", cochran_sample_parse_I,	"Nemo" },
		{ "140", cochran_sample_parse_I,	"AquaNox" },
		{ "213", cochran_sample_parse_II,	"Pre-21000 Commander" },
		{ "215", cochran_sample_parse_gem,	"Gemini" },
		{ "216", cochran_sample_parse_gem,	"Gemini" },
		{ "221", cochran_sample_parse_II,	"Commander" },
		{ "300", cochran_sample_parse_emc,	"EMC" },
		{ "301", cochran_sample_parse_emc,	"EMC" },
		{ "315", cochran_sample_parse_emc,	"EMC" },
	};

	int parser_cnt;
	parser_cnt = sizeof(parser) / sizeof(struct parser_table);

	for (int i = 0; i < parser_cnt; i++ )
	if (!strncmp(model, parser[i].model, 3)) {
		return parser[i].parser;
	}

	return NULL;
}


int cochran_sample_parse(const unsigned char *model, const cochran_log_t *log, const unsigned char *samples, unsigned int size, cochran_sample_callback_t callback, void *userdata) {

	cochran_sample_parser_t parser = cochran_sample_get_parser(model);

	if (!parser)
		return 1;

	parser(log, samples, size, callback, userdata);
	return 0;
}
