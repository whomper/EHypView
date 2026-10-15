/*
 * EHypView - reader for ST-GUIDE.INF style configuration files.
 *
 * Lines have the form VAR=value.  Keys are case-insensitive; lines that
 * start with '#' or ';' and lines without '=' are ignored.  EHypView
 * reads an existing ST-GUIDE.INF so PATHS, REF etc. carry over.
 */
#ifndef EHV_INF_H
#define EHV_INF_H

typedef struct {
	char *key;
	char *value;
} inf_pair;

typedef struct {
	char *buf;               /* copy of the text; pairs point into it */
	inf_pair *pairs;
	unsigned int npairs;
} inf_file;

#define INF_OK          0
#define INF_ERR_OPEN   -1
#define INF_ERR_MEMORY -3

inf_file *inf_load(const char *path, int *err);
inf_file *inf_parse(const char *text, int *err);
void inf_free(inf_file *inf);

/* value of the last occurrence of key, or NULL */
const char *inf_get(const inf_file *inf, const char *key);

/*
 * EHYPVIEW.INF written back (the AERA guidelines, 6.1): text (the whole
 * file, or NULL for none yet) with key=value. Each line that sets key is
 * changed in place; with none, "; note", the line and an empty line are
 * added at the end. Notes, unknown settings and everything else stay as
 * they were; lines end CR LF. A malloc'ed copy, or NULL.
 */
char *inf_put(const char *text, const char *key, const char *value, const char *note);

/*
 * Split a path list ("C:\GUIDES\;D:\DOC" - separated by ';' or ',') into
 * a malloc'ed array of malloc'ed strings with blanks trimmed.  Returns
 * the number of entries; *out is NULL when there are none.
 */
unsigned int inf_split_list(const char *value, char ***out);
void inf_free_list(char **list, unsigned int n);

#endif
