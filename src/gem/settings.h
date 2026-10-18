/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the Settings dialog (settings.c): EHYPVIEW.INF's settings.
 */
#ifndef EHV_SETTINGS_H
#define EHV_SETTINGS_H

#define SETTINGS_START 1               /* START= changed */
#define SETTINGS_PATHS 2               /* PATHS= changed: look through the library again */

/*
 * Settings... (^,): inf_path is EHYPVIEW.INF, folder EHypView's folder
 * (FOLDER=, shown). What OK changed is written to the file; returns the
 * SETTINGS_* of what changed, 0 for Cancel (Esc, Undo) or nothing.
 */
int settings_dialog(const char *inf_path, const char *folder);

#endif
