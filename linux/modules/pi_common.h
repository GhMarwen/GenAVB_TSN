/*
 * PI controller

 * Copyright 2014-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _PI_COMMON_H_
#define _PI_COMMON_H_

struct pi {
	unsigned int ki;
	unsigned int kp;
	int err;
	int64_t integral;
	int u;
};

void pi_reset(struct pi *p, int u);

void pi_init(struct pi *p, unsigned int ki, unsigned int kp);

int pi_update(struct pi *p, int err);

#endif /* _PI_COMMON_H_ */

