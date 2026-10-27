/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - a name asked for (ask.c): `what` says what for, buf holds
 * the name shown at first and the one typed. 1 for OK with a name, 0 for
 * Cancel (Undo) or an empty field.
 */
#ifndef EHV_ASK_H
#define EHV_ASK_H

int ask_name(const char *what, char *buf, unsigned long size);

#endif
