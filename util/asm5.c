/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.cabelsom>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rnn.h"
#include "rnndec.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static struct rnndeccontext *ctx;
static struct rnndb *db;
struct rnndomain *dom[2];
struct rnnenum *pm4enum;

static uint32_t jumptable[127];
static uint32_t instrs[8192];

static int decode_label(const char *name)
{
	int i;
	for (i = 0; i < pm4enum->valsnum; i++)
		if (!strcmp(pm4enum->vals[i]->name, name))
			return pm4enum->vals[i]->value;
	/* it could be "UNKN%d": */
	if (sscanf(name, "UNKN%d", &i) == 1)
		return i;
	return -1;
}

#define CHUNKSIZE 4096

static char * readfile(const char *path, int *sz)
{
	char *buf = NULL;
	int fd, ret, n = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;

	while (1) {
		buf = realloc(buf, n + CHUNKSIZE);
		ret = read(fd, buf + n, CHUNKSIZE);
		if (ret < 0) {
			free(buf);
			*sz = 0;
			return NULL;
		} else if (ret < CHUNKSIZE) {
			n += ret;
			*sz = n;
			return buf;
		} else {
			n += CHUNKSIZE;
		}
	}
}

int main(int argc, char **argv)
{
	char *infile, *outfile;
	char *buf;
	int sz, fd, off = 0;
	uint32_t dummy, val;

	if (argc != 3) {
		printf("usage: asm5 <in.asm> <out.fw>\n");
		return -1;
	}

	infile = argv[1];
	outfile = argv[2];

	rnn_init();
	db = rnn_newdb();

	ctx = rnndec_newcontext(db);
	ctx->colors = &envy_null_colors;

	rnn_parsefile(db, "adreno.xml");
	dom[0] = rnn_finddomain(db, "A5XX");
	dom[1] = rnn_finddomain(db, "AXXX");

	pm4enum = rnn_findenum(db, "adreno_pm4_type3_packets");

	buf = readfile(infile, &sz);
	if (!buf)
		return -1;

	fd = open(outfile, O_WRONLY | O_CREAT, 0644);
	if (fd < 0)
		return -1;

	/*
	 * a line of input either looks like:
	 *
	 *   "; %s\n"           <-- comment
	 *   "%s:\n"            <-- label
	 *   "%04x: %08x ...\n" <-- instruction
	 *
	 * Bail when you see something that doesn't match those, it
	 * means we reached the jumptable or the end.
	 *
	 * This is super-hard-coded.. but enough for now.
	 */

	while (*buf != '\0') {
		char label[64];

		/* ignore whitespace: */
		if ((*buf == '\n') || (*buf == ' ') || (*buf == '\t')) {
			buf++;
			continue;
		}

		if (*buf == ';') {
			/* comment, ignore rest of line */
		} else if (sscanf(buf, "%04x: %08x", &dummy, &val) == 2) {
			instrs[off++] = val;
		} else if (sscanf(buf, "%s:", label) == 1) {
			/* scanf is a lame parser, we end up w/ ':' in label name: */
			char *c = strchr(label, ':');
			if (c)
				*c = '\0';
			int id = decode_label(label);
			if (id < 0)
				break;
			jumptable[id] = off;
		}

		/* ignore trailing garbage, scan fwd until next newline: */
		while ((*buf != '\0') && (*buf != '\n'))
			buf++;
	}

	/* patch up offset to jumptable: */
	instrs[1] = off;

	/* first dword is ignored by kernel: */
	val = 0;
	write(fd, &val, 4);

	/* then instructions: */
	write(fd, instrs, 4 * off);

	/* and finally jumptable: */
	write(fd, jumptable, sizeof(jumptable));
	close(fd);

	return 0;
}
