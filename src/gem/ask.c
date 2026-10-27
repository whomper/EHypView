/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - a name asked for in a small dialog: a line saying what for,
 * a framed field (the guidelines, 5.7) and OK and Cancel, run by the
 * shared dialog loop (common/ui/dialog.c): Esc empties the field, Undo
 * and Cancel leave it (3.5); 3D where the AES draws it (5.6).
 */
#include <string.h>
#include "gem.h"
#include "gemapp.h"
#include "dialog.h"
#include "ask.h"

#define FIELD 32                   /* characters the field holds */
#define W 40                       /* the dialog, in characters */

enum { O_BOX, O_TEXT, O_FIELD, O_CANCEL, O_OK, O_N };

int ask_name(const char *what, char *buf, unsigned long size)
{
	static char text[FIELD + 1], tmpl[FIELD + 1], valid[FIELD + 1];
	static char line[W - 3];
	OBJECT tree[O_N];
	TEDINFO ted;
	short i, r;

	memset(tree, 0, sizeof(tree));
	memset(&ted, 0, sizeof(ted));
	strncpy(text, buf, FIELD);
	text[FIELD] = '\0';
	memset(tmpl, '_', FIELD);
	memset(valid, 'X', FIELD);
	tmpl[FIELD] = valid[FIELD] = '\0';
	strncpy(line, what, sizeof(line) - 1);
	line[sizeof(line) - 1] = '\0';
	ted.te_ptext = text;
	ted.te_ptmplt = tmpl;
	ted.te_pvalid = valid;
	ted.te_font = 3;
	ted.te_color = 0x1180;
	ted.te_thickness = -1;             /* a frame round it */
	ted.te_txtlen = FIELD + 1;
	ted.te_tmplen = FIELD + 1;

	tree[O_BOX].ob_type = G_BOX;
	tree[O_BOX].ob_state = OUTLINED;
	tree[O_BOX].ob_spec = 0x00021100L;
	tree[O_BOX].ob_width = W;
	tree[O_BOX].ob_height = 7;
	tree[O_TEXT].ob_type = G_STRING;
	tree[O_TEXT].ob_spec = (long) line;
	tree[O_TEXT].ob_x = 2;
	tree[O_TEXT].ob_y = 1;
	tree[O_TEXT].ob_width = (short) strlen(line);
	tree[O_TEXT].ob_height = 1;
	tree[O_FIELD].ob_type = G_FBOXTEXT;
	tree[O_FIELD].ob_flags = EDITABLE;
	tree[O_FIELD].ob_spec = (long) &ted;
	tree[O_FIELD].ob_x = 2;
	tree[O_FIELD].ob_y = 3;
	tree[O_FIELD].ob_width = FIELD;
	tree[O_FIELD].ob_height = 1;
	tree[O_CANCEL].ob_type = G_BUTTON;
	tree[O_CANCEL].ob_flags = SELECTABLE | EXIT;
	tree[O_CANCEL].ob_spec = (long) "Cancel";
	tree[O_CANCEL].ob_x = W - 24;
	tree[O_CANCEL].ob_y = 5;
	tree[O_CANCEL].ob_width = 10;
	tree[O_CANCEL].ob_height = 1;
	tree[O_OK].ob_type = G_BUTTON;
	tree[O_OK].ob_flags = SELECTABLE | EXIT | DEFAULT | LASTOB;
	tree[O_OK].ob_spec = (long) "OK";
	tree[O_OK].ob_x = W - 12;
	tree[O_OK].ob_y = 5;
	tree[O_OK].ob_width = 10;
	tree[O_OK].ob_height = 1;
	tree[O_BOX].ob_next = -1;
	tree[O_BOX].ob_head = O_TEXT;
	tree[O_BOX].ob_tail = O_OK;
	for (i = O_TEXT; i < O_N; i++)
	{
		tree[i].ob_next = i + 1 < O_N ? i + 1 : O_BOX;
		tree[i].ob_head = tree[i].ob_tail = -1;
	}
	for (i = 0; i < O_N; i++)
		rsrc_obfix(tree, i);

	log_event("dialog", "ask", what);
	r = dlg_modal(tree, O_FIELD, O_CANCEL);
	log_event("dialog-closed", "ask", r == O_OK ? text : "cancel");
	if (r != O_OK)
		return 0;
	/* blanks trimmed */
	{
		char *p = text, *e;

		while (*p == ' ')
			p++;
		e = p + strlen(p);
		while (e > p && e[-1] == ' ')
			*--e = '\0';
		if (*p == '\0')
			return 0;
		strncpy(buf, p, size - 1);
		buf[size - 1] = '\0';
	}
	return 1;
}
