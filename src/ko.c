#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
/* nethack-ko: to avoid problems with BCC 5.5 pcre.h is included like this */
#include "pcre/pcre.h"
#include "config.h"
#include "wintype.h"
#include "winprocs.h"
#include "objclass.h"
#include "dungeon.h"
#include "monsym.h"
#include "decl.h"
#include "ko.h"

#define TRANSLATIONS_FILENAME	"ko.txt"
#define UNTRANSLATED_FILENAME	"untrans.txt"
#define LINE_BUFFER_SIZE	512
#define DELIMITER	'='
#define ESCAPE_CHAR	'\\'
#define ESCAPE_STR	"\\"
#define MAX_PARAMS	5
#define MAX_PARAM_LEN	15
#define MAX_TRANSLATED_LEN	200
#define MAX_UNTRANSLATED_LEN	200
#define WILD_CARD	"\\s"
#define SPECIAL_CHARS	".*^$|()[]\\"
#define STR_CHAR	's'
#define STR_RE	"(.*)"
#define STR_RE_LEN	(sizeof(STR_RE) - 1)
#define CHR_CHAR	'c'
#define CHR_RE	"(.)"
#define CHR_RE_LEN	(sizeof(CHR_RE) - 1)
#define NUM_CHAR	'#'
#define NUM_RE	"([0-9]+)"
#define NUM_RE_LEN	(sizeof(NUM_RE) - 1)
#define PCRE_ESC_CHAR	'\\'
#define MAX_CONVERTED_LEN	256
#define MAX_CAPTURES	10
#define NUM_SECTIONS	('z' - 'a' + 2)
#define COMMENT_CHAR	'#'
#define SECTION_CHAR	':'

static FILE* untranslated_file;
static struct translation {
	pcre* untranslated;
	char* translated;
} *tran[NUM_SECTIONS];
static num_trans[NUM_SECTIONS];
static char result_buffer[256];

static void
convert_pattern(const char* s, char* t)
{
	int n;
	*t++ = '^';
	while (1) {
		n = strcspn(s, SPECIAL_CHARS);
		memcpy(t, s, n);
		t += n;
		s += n;
		if (!*s) {
			break;
		} else if (*s == ESCAPE_CHAR) {
			static const char sym[]
				= {STR_CHAR, CHR_CHAR, NUM_CHAR, 0};
			static const struct {
				const char* re;
				int len;
			} tab[] = {
				{STR_RE, STR_RE_LEN},
				{CHR_RE, CHR_RE_LEN},
				{NUM_RE, NUM_RE_LEN},
			};
			char* r;
			++s;
			r = strchr(sym, *s);
			if (r) {
				++s;
				memcpy(t, tab[r-sym].re, tab[r-sym].len);
				t += tab[r-sym].len;
			}
		} else {
			*t++ = PCRE_ESC_CHAR;
			*t++ = *s++;
		}
	}
	*t++ = '$';
	*t = 0;
}

static int
add_translation(const char* untranslated, const char* translated, int section)
{
	void *t, *v;
	pcre* u;
	const char* error;
	int erroffset;
	char converted[MAX_CONVERTED_LEN];
	int result;

	t = realloc(tran[section],
			(num_trans[section]+1)*sizeof(*tran[section]));
	convert_pattern(untranslated, converted);
	u = pcre_compile(converted, NULL, &error, &erroffset, NULL);
	v = malloc(strlen(translated)+1);
	result = t && u && v;
	if (result) {
		tran[section] = (struct translation*) t;
		tran[section][num_trans[section]].untranslated = u;
		tran[section][num_trans[section]].translated = (char*) v;
		strcpy(tran[section][num_trans[section]].translated,
				translated);
		++num_trans[section];
	} else {
		if (u) {
			free(u);
		}
		if (v) {
			free(v);
		}
	}

	return result;
}

static int
to_section(char c)
{
	return tolower(c) - 'a' + 1;
}

static void
load_translations(void)
{
	FILE* translations_file;

	translations_file = fopen(TRANSLATIONS_FILENAME, "r");
	if (translations_file) {
		char line[LINE_BUFFER_SIZE];
		int section = 0;
		while (fgets(line, sizeof(line), translations_file)) {
			char* p;
			if (line[0] == COMMENT_CHAR) {
				continue;
			} else if (line[0] == SECTION_CHAR) {
				section = to_section(line[1]);
				continue;
			}
			p = strchr(line, DELIMITER);
			if (!p) {
				continue;
			}
			*p++ = 0;
			p[strcspn(p,"\r\n")] = 0;
			if (!add_translation(line, p, section)) {
				break;
			}
		}
		fclose(translations_file);
	}
}

void
ko_initialize(void)
{
	load_translations();
	untranslated_file = fopen(UNTRANSLATED_FILENAME, "w");
}

int find_translation(const char*, char*, int);

static void
format(char* s, const char* fmt, const char* p, int* v, int n)
{
	const char* t = fmt;
	while (1) {
		int c = strcspn(t, ESCAPE_STR);
		memcpy(s, t, c);
		s += c;
		t += c;
		if (!*t) {
			break;
		} else if (*t == ESCAPE_CHAR) {
			if (isdigit(*++t)) {	/* \N */
				c = (*t-'0') * 2;
				++t;
				memcpy(s, p+v[c], v[c+1]-v[c]);
				s += v[c+1] - v[c];
			} else if (isalpha(*t) && isdigit(*(t+1))) {
								/* \xN */
				const char* q;
				++t;
				c = *t - '0';
				if (pcre_get_substring(p, v, n, c, &q) <= 0) {
					++t;
					/* TODO: handle error */
					continue;
				}
				if (find_translation(q, s,
							to_section(*(t-1)))) {
					s += strlen(s);
				} else {
					s += pcre_copy_substring(p, v, n, c, s,
							100);
					/* TODO: fix the use of magic number */
				}
				++t;
				pcre_free_substring(q);
			} else {
				++t;
				/* do nothing */
			}
		} else {
			printf("\nformat: impossible!\n");
			system("pause");
		}
	}
	*s = 0;
}

static int
find_translation(const char* untranslated, char* translated, int section)
{
	int i;
	const char** params;

	for (i = 0; i < num_trans[section]; ++i) {
		int v[(MAX_CAPTURES + 1) * 3];
		const int vlen = sizeof(v) / sizeof(*v);
		int n = pcre_exec(tran[section][i].untranslated, NULL,
				untranslated, strlen(untranslated), 0, 0, v,
				vlen);
		if (n > 0) {
			format(translated, tran[section][i].translated,
					untranslated, v, n);
			break;
		}
	}
	return i < num_trans[section];
}

static void
save_untranslated(const char* untranslated)
{
	if (untranslated_file) {
		fputs(untranslated, untranslated_file);
		fputs("\n", untranslated_file);
	}
}

const char*
ko_translate(const char* untranslated)
{
	static char result[MAX_TRANSLATED_LEN];

	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		ko_initialize();
	}

	if (!find_translation(untranslated, result, 0)) {
		save_untranslated(untranslated);
		return untranslated;
	}
	return result;
}

void
putstr(winid window, int attr, const char* str)
{
	if (window == WIN_MESSAGE) {
		str = ko_translate(str);
	}
	real_putstr(window, attr, str);
}
