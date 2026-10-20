/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - its drawing under the names the AERA programs' shared GEM
 * building blocks use (common/ui/gemui.h: the popup and the About box).
 * Only those include this file; the implementations are in gemui.c.
 */
#ifndef EHV_UI_H
#define EHV_UI_H

#include "grect.h"

#define TX_BOLD    1
#define TX_INVERSE 2
#define TX_LTR     16
#define TX_DIM     128

#ifndef KEY_SCAN                       /* gemapp.h has them too */
#define KEY_SCAN(k)  (((unsigned short) (k)) >> 8)
#define KEY_ASCII(k) ((k) & 0xff)
#endif

/* EHypView's own fill() and text_at() take other arguments */
#define fill      ehv_ui_fill
#define frame     ehv_ui_frame
#define hline     ehv_ui_hline
#define line_col  ehv_ui_line_col
#define text_at   ehv_ui_text_at
#define clip_on   ehv_ui_clip_on
#define clip_off  ehv_ui_clip_off
#define st_ram    ehv_ui_st_ram
#define scr_w     ehv_ui_scr_w
#define scr_h     ehv_ui_scr_h
/* the AES's cell (EHypView's sys_cw, sys_ch: fontsel.c has statics of
   that name) */
#define gl_wchar  ehv_ui_gl_wchar
#define gl_hchar  ehv_ui_gl_hchar

#endif
