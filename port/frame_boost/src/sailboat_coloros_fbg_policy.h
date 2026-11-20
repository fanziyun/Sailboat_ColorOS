/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAILBOAT_COLOROS_FBG_POLICY_H
#define SAILBOAT_COLOROS_FBG_POLICY_H

static inline unsigned int sailboat_coloros_fbg_freq_floor(unsigned long util,
		unsigned long capacity, unsigned int hardware_max,
		unsigned int minimum, unsigned int maximum)
{
	unsigned long long frequency;

	if (!capacity || !hardware_max || minimum > maximum)
		return 0;
	if (util > capacity)
		util = capacity;
	frequency = ((unsigned long long)hardware_max + hardware_max / 4) * util;
	frequency /= capacity;
	if (frequency < minimum)
		frequency = minimum;
	if (frequency > maximum)
		frequency = maximum;
	return (unsigned int)frequency;
}

#endif
