/* linenoise_win.c -- Windows stand-in for linenoise.
 *
 * linenoise itself is termios-only. This file implements the six entry
 * points the tzpl CLI uses (see lang/src/main.cpp) with plain stdio, so the
 * REPL works on Windows without line editing, completion, or history.
 * Building a real editor on the console API can replace it later; the API
 * surface to keep is exactly these functions.
 *
 * BSD-2-Clause, following linenoise (see LICENSE in this directory).
 */

#include "linenoise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *linenoise(const char *prompt) {
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    size_t cap = 256, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            if (len == 0) { free(buf); return NULL; }
            break;
        }
        if (c == '\n') break;
        if (len + 2 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    while (len > 0 && buf[len - 1] == '\r') --len;
    buf[len] = '\0';
    return buf;
}

void linenoiseFree(void *ptr) { free(ptr); }

int linenoiseHistoryAdd(const char *line) { (void)line; return 0; }
int linenoiseHistorySetMaxLen(int len) { (void)len; return 1; }
int linenoiseHistorySave(const char *filename) { (void)filename; return 0; }
int linenoiseHistoryLoad(const char *filename) { (void)filename; return 0; }

/* Unused by tzpl, kept so the header's declarations all resolve. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) { (void)fn; }
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) { (void)fn; }
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) { (void)fn; }
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *s) { (void)lc; (void)s; }
void linenoiseClearScreen(void) {}
void linenoiseSetMultiLine(int ml) { (void)ml; }
void linenoiseMaskModeEnable(void) {}
void linenoiseMaskModeDisable(void) {}
void linenoisePrintKeyCodes(void) {}
