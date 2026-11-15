/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the Settings dialog (settings.c): EHYPVIEW.INF's settings.
 */
#ifndef EHV_SETTINGS_H
#define EHV_SETTINGS_H

#include "inf.h"

#define SETTINGS_START 1               /* START= changed */
#define SETTINGS_PATHS 2               /* PATHS= changed: look through the library again */
#define SETTINGS_LAYOUT 4              /* LIBRARY= changed: the library's layout */
#define SETTINGS_LOG 8                 /* LOG= changed: Write a log */

/*
 * Settings... (^,): inf_path is EHYPVIEW.INF, folder EHypView's folder
 * (FOLDER=, shown). What OK changed is written to the file; returns the
 * SETTINGS_* of what changed, 0 for Cancel (Esc, Undo) or nothing.
 */
int settings_dialog(const char *inf_path, const char *folder);

#define SETTINGS_LOG_NOTE "Write a log: 1 writes EHYPVIEW.LOG (the steps of quitting) next to the program, " \
	"0 writes none (the guidelines 6.5)"

/* LOG= as inf has it: 1 for Write a log, else 0 (off until ticked, 6.5) */
int settings_log_on(const inf_file *inf);

#endif
