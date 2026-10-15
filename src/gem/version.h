/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * version.h - EHypView's version, as in EMail, ECal and EContact: VERSION
 * holds the last release (major.minor, e.g. "1.0"), which a release build
 * shows; other builds show "1.0-dev". The Makefile writes version.c;
 * tools/bump.py starts a release.
 */
#ifndef VERSION_H
#define VERSION_H
extern const char app_version[];	/* "1.0" or "1.0-dev" */
#endif
