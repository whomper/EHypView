/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - common's click.h (press_track, PRESS_IN...: the guidelines,
 * 5.8) for EHypView's own files: its gemui.h renames these names for
 * common's files (ui.h), which EHypView's own fill(), text_at()... keep.
 */
#ifndef EHV_PRESS_H
#define EHV_PRESS_H

#include "click.h"
#undef fill
#undef frame
#undef hline
#undef line_col
#undef text_at
#undef clip_on
#undef clip_off
#undef st_ram
#undef scr_w
#undef scr_h
#undef gl_wchar
#undef gl_hchar

#endif
