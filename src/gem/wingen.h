/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * wingen.h - a window's work area asked of the AES once until a window
 * could have changed: an AES call is some 2 ms of an 8 MHz ST, and a line
 * scrolled asked for it eight times (the speed rig, 20 Nov 2026). Every
 * message and every wind_set, wind_open, wind_close, wind_create and
 * wind_delete of EHypView's own starts a new generation: a window moves
 * or changes size only by those (the AES tells a program by a message,
 * and the program sets it). Included after gem.h, in the files that set
 * windows (ehypview.c, libwin.c).
 */
#ifndef WINGEN_H
#define WINGEN_H

extern unsigned work_gen;

#define wind_set(...) (work_gen++, (wind_set)(__VA_ARGS__))
#define wind_open(...) (work_gen++, (wind_open)(__VA_ARGS__))
#define wind_close(...) (work_gen++, (wind_close)(__VA_ARGS__))
#define wind_delete(...) (work_gen++, (wind_delete)(__VA_ARGS__))
#define wind_create(...) (work_gen++, (wind_create)(__VA_ARGS__))

#endif
