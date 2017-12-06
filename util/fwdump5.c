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
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rnn.h"
#include "rnndec.h"
//#include "adreno_pm4.xml.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define PACKED __attribute__((__packed__))


/*

  0007: 305d0838        CP_ME_INSTR_BASE_LO
  0006: 88050835        CP_PFP_INSTR_BASE_LO
  00ff: 981f0806        CP_RB_RPTR  ???

  0466: 881d0b15        CP_WFI_PEND_CTR   <-- ME write in CP_WAIT_FOR_ME

Note in these cases, fw probably writes at least 3 consecutive regs:

  0534: 32fde7b9        HLSQ_CS_KERNEL_GROUP_X  <-- write in CP_EXEC_CS
  054b: 32fde7b1        HLSQ_CS_NDRANGE_1  <-- write in CP_EXEC_CS_INDIRECT

  08fe: 318c04d2        RBBM_ALWAYSON_COUNTER_LO <-- pfp in CP_RECORD_PFP_TIMESTAMP
                                                     presumably a read


  d8000000 <-- seems to be where it waits for new packet?
  981f0806 <-- seems to always follow usually at end of packet handler

  XXX trying:
  040f: 318c0b7f        CP_SCRATCH[0x7].REG

In ME:
  CP_RUN_OPENCL:
    0524: c2a00047  op30
    0525: 981f1006  op26
    0526: b0068031  op2c
    0527: 32fde781  op0c
    0528: 9802f806  op26  RBBM_SECVID_TSB_UCHE_STATUS_LO
    0529: 32fde3a1  op0c
    052a: 9802f806  op26  RBBM_SECVID_TSB_UCHE_STATUS_LO
    052b: c0c00003  op30
    052c: 8b0500c7  movi $s05, 0x00c7 << 24
    052d: 30a50300  op0c
    052e: d40007cd  op35
    052f: 9805e806  op26
    0530: d8000000  op36
    0531: 981f0806  op26  CP_RB_RPTR

  CP_EXEC_CS:
    0532: c2a00038  op30 $00, $15
    0533: 981f1006  op26 $data, $00
    ; $17 perhaps holds the current context-bank for banked registers?
    0534: 32fde7b9  ori $addr, $17, 0xe7b9        ; HLSQ_CS_KERNEL_GROUP_X
    0535: c800fff1  br -15 (0526)
    0536: 9c1ff806  op27 $data, $00               ; RBBM_SECVID_TSB_UCHE_STATUS_LO

In PFP:
  CP_RUN_OPENCL:
  CP_EXEC_CS:
    04ce: 2a82000f  op0a
    04cf: 8043000f  op20
    04d0: cc60ff3e  op33
    04d1: 88020001  movi $s02, 0x0001
    04d2: c800ff38  br -200 (040a)  <-- maybe PC relative load instead
    04d3: a802801c  op2a                since not a sensible place to
                                        branch to.. maybe op27 is the br?


  0466: 881d0b7f  <-- mov $addr, 0x0b7f
  0467: 881f0123  <-- mov $data, 0x0123

note that successive reads of $data consume contents of packet.. the
following writes the first 3 dwords of packet to $08->$09..

  040e: 2be8ffff  andi $08, $data, 0xffff
  040f: 2be9ffff  andi $09, $data, 0xffff
  0410: 2beaffff  andi $0a, $data, 0xffff


note:  881a0b7f   <-- mov $state[0x1a], 0x0b7f

frequently see this pattern at the end of packet handler:

  0530: d8000000
  0531: 981f0806        CP_RB_RPTR

possibly wait-for-event + dispatch.. it would kinda make sense
to be referencing CP_RB_RPTR, but why do both PFP and ME do it?

  0485: 8a02703f  movi $s02, 0x703f << 16
  0486: c702000b  op31   <-- maybe OR'ing 0x000b into $s02 ??

  0550: 8a0301f0  movi $03, 0x01f0 << 16
  0551: 30634fff  or $03, $03, 0x4fff

CP_NOP:
  ;840e: 2be8ffff  andi $08, $data, 0xffff
  ;040f: 2be9ffff  andi $09, $data, 0xffff
  ;0410: 2beaffff  andi $0a, $data, 0xffff
  ;0000: 981f3007
  ;0000: 981f3008
  ;0000: 9c1f3006  $06=0000000f
  ;0000: 981f3806  $07=0000001f
  0000: 981f4006  $08=0000001f
  0000: 98084806  $09=0000001f  mov %09, %08 ???
  0411: 9c1f0006  op27 $data, $00
  0412: d8000000  op36 $00, $00
  0413: 981f0806  op26 $data, $00               ; CP_RB_RPTR

 */

typedef enum {
	OPC_NOP  = 0x00,

	/* Note this ordering is basically same as alu_opc.. so more
	 * like it is the same thing..
	 */
	OPC_ADDI  = 0x02,   /* add immediate */
	OPC_ADDI2 = 0x04,   /* add immediate (???) */
	OPC_SUBI  = 0x06,   /* subtract immediate */
	OPC_SUBI2 = 0x08,   /* subtract immediate (????  carry flag?) */
	OPC_ANDI  = 0x0a,   /* AND immediate */
	OPC_ORI   = 0x0c,   /* OR immediate */
	OPC_XORI  = 0x0e,   /* XOR immediate */
	OPC_NOTI  = 0x10,   /* bitwise not of immed (src ignored */
	OPC_SHLI  = 0x12,   /* shift-left immediate */
	OPC_USHRI = 0x14,   /* unsigned shift right by immediate */
	OPC_ISHRI = 0x16,   /* signed shift right by immediate */
	// 0x18 ??
	OPC_MUL8I = 0x1a,   /* 8bit multiply by immediate */
	OPC_MINI  = 0x1c,
	OPC_MAXI  = 0x1e,

	OPC_MOVI = 0x22,  /* move immediate */
	OPC_ALU  = 0x26,  /* move register */
	OPC_BR   = 0x32,
} opc;

typedef struct PACKED {
	union PACKED {
		/* addi, subi, andi, ori, xori, etc: */
		struct PACKED {
			uint32_t uimm    : 16;
			uint32_t dst     : 5;
			uint32_t src     : 5;
			uint32_t opc     : 6;
		} alui;
		struct PACKED {
			uint32_t uimm    : 16;
			uint32_t dst     : 5;
			uint32_t shift   : 5;
			uint32_t opc     : 6;
		} movi;
		struct PACKED {
			uint32_t alu     : 5;
			uint32_t pad0    : 6;
			uint32_t dst     : 5;
			uint32_t src2    : 5;
			uint32_t src1    : 5;
			uint32_t opc     : 6;
		} alu;
		struct PACKED {
			int32_t  iimm    : 16;
			uint32_t bit     : 5;
			uint32_t src     : 5;
			uint32_t opc     : 6;
		} br;
		struct PACKED {
			uint32_t pad     : 26;
			uint32_t opc     : 6;
		};
	};
} instr;

/* ALU opcodes for "ALU" instruction: */
typedef enum {
	// 0x0 -> zero??
	ALU_ADD  = 0x1,
	ALU_ADD2 = 0x2,   /* probably add-hi + carry for upper 64b */
	ALU_SUB  = 0x3,
	ALU_SUB2 = 0x4,   /* probably sub-hi + carry for upper 64b */
	ALU_AND  = 0x5,
	ALU_OR   = 0x6,
	ALU_XOR  = 0x7,
	ALU_NOT  = 0x8,  /* src1 ignored */
	ALU_SHL  = 0x9,
	ALU_USHR = 0xa,  /* unsigned shift-right */
	ALU_ISHR = 0xb,  /* signed shift-right */
	// 0xc ???
	ALU_MUL8 = 0xd,  /* multiply low eight bits or src1/src2 */
	ALU_MIN  = 0xe,
	ALU_MAX  = 0xf,
	// 0x10 ???
} alu_opc;


static int gpuver;

static struct rnndeccontext *ctx;
static struct rnndb *db;
struct rnndomain *dom[2];

static void print_gpu_reg(uint32_t regbase)
{
	struct rnndomain *d = NULL;

	if (regbase < 0x100)
		return;

	if (rnndec_checkaddr(ctx, dom[0], regbase, 0))
		d = dom[0];
	else if (rnndec_checkaddr(ctx, dom[1], regbase, 0))
		d = dom[1];

	if (d) {
		struct rnndecaddrinfo *info = rnndec_decodeaddr(ctx, d, regbase, 0);
		if (info) {
			printf("\t; %s", info->name);
			free(info->name);
			free(info);
			return;
		}
	}
}

static void print_reg(unsigned reg)
{
// XXX seems like *reading* $00 --> literal zero??
	if (reg == 0x1d)
		printf("$addr");
	else if (reg == 0x1f)
		printf("$data");
	else
		printf("$%02x", reg);
}

static char *getpm4(uint32_t id)
{
	return rnndec_decode_enum(ctx, "adreno_pm4_type3_packets", id);
}


static uint32_t label_offsets[0x512];
static int num_label_offsets;

static int label_idx(uint32_t offset, bool create)
{
	int i;
	for (i = 0; i < num_label_offsets; i++)
		if (offset == label_offsets[i])
			return i;
	if (!create)
		return -1;
	label_offsets[i] = offset;
	num_label_offsets = i+1;
	return i;
}


static struct {
	uint32_t offset;
	uint32_t num_jump_labels;
	uint32_t jump_labels[256];
} jump_labels[1024];
int num_jump_labels;

static void add_jump_table_entry(uint32_t n, uint32_t offset)
{
	int i;

	if (n > 128) /* can't possibly be a PM4 PKT3.. */
		return;

	for (i = 0; i < num_jump_labels; i++)
		if (jump_labels[i].offset == offset)
			goto add_label;

	num_jump_labels = i + 1;
	jump_labels[i].offset = offset;
	jump_labels[i].num_jump_labels = 0;

add_label:
	jump_labels[i].jump_labels[jump_labels[i].num_jump_labels++] = n;
	assert(jump_labels[i].num_jump_labels < 256);
//printf("add %d -> %04x (%d)\n", n, offset, i);
}

static int get_jump_table_entry(uint32_t offset)
{
	int i;

	for (i = 0; i < num_jump_labels; i++)
		if (jump_labels[i].offset == offset)
			return i;

	return -1;
}

static void disasm(uint32_t *buf, int sizedwords)
{
	uint32_t *instrs = buf;
	const int jmptbl_start = instrs[1];
	uint32_t *jmptbl = &buf[jmptbl_start];
	int i;

	/* parse jumptable: */
	for (i = 0; i < 0x7f; i++) {
		unsigned offset = jmptbl[i];
		unsigned n = i;// + CP_NOP;
		add_jump_table_entry(n, offset);
	}

	/* do a pre-pass to find instructions that are potential branch targets,
	 * and add labels for them:
	 */
	for (i = 0; i < jmptbl_start; i++) {
		instr *instr = (void *)&instrs[i];

		if (instr->opc == OPC_BR) {
			label_idx(i + instr->br.iimm, true);
		}
	}

	/* print instructions: */
	for (i = 0; i < jmptbl_start; i++) {
		instr *instr = (void *)&instrs[i];
		int idx = label_idx(i, false);
		int jump_label_idx = get_jump_table_entry(i);

		if (jump_label_idx >= 0) {
			int j;
			printf("\n");
			for (j = 0; j < jump_labels[jump_label_idx].num_jump_labels; j++) {
				uint32_t jump_label = jump_labels[jump_label_idx].jump_labels[j];
				char *name = getpm4(jump_label);
				if (name) {
					printf("%s:\n", name);
				} else {
					printf("UNKN%d:\n", jump_label);
				}
			}
		}

		if (idx >= 0) {
			printf(" l%02d: ", idx);
		} else {
			printf("      ");
		}


		printf("\t%04x: %08x  ", i, instrs[i]);

		switch (instr->opc) {
		case OPC_NOP:
			printf("nop");

			/* This only seems to happen for first two dwords, which might
			 * not be instructions but might instead be a header:
			 */
			if (instrs[i] != 0x0)
				printf(" XXX");

			break;
		case OPC_ADDI:
		case OPC_ADDI2:
		case OPC_SUBI:
		case OPC_SUBI2:
		case OPC_ANDI:
		case OPC_ORI:
		case OPC_XORI:
		case OPC_NOTI:
		case OPC_SHLI:
		case OPC_USHRI:
		case OPC_ISHRI:
		case OPC_MUL8I:
		case OPC_MINI:
		case OPC_MAXI: {
			bool src1 = true;

			if (instr->opc == OPC_ADDI) {
				printf("addi ");
			} else if (instr->opc == OPC_ADDI2) {
				printf("addi2 ");
			} else if (instr->opc == OPC_SUBI) {
				printf("subi ");
			} else if (instr->opc == OPC_SUBI2) {
				printf("subi2 ");
			} else if (instr->opc == OPC_ANDI) {
				printf("andi ");
			} else if (instr->opc == OPC_ORI) {
				printf("ori ");
			} else if (instr->opc == OPC_XORI) {
				printf("xori ");
			} else if (instr->opc == OPC_NOTI) {
				printf("noti ");
				src1 = false;
			} else if (instr->opc == OPC_SHLI) {
				printf("shli ");
			} else if (instr->opc == OPC_USHRI) {
				printf("ushri ");
			} else if (instr->opc == OPC_ISHRI) {
				printf("ishri ");
			} else if (instr->opc == OPC_MUL8I) {
				printf("imul8i ");
			} else if (instr->opc == OPC_MINI) {
				printf("mini ");
			} else if (instr->opc == OPC_MAXI) {
				printf("maxi ");
			}
			print_reg(instr->alui.dst);
			printf(", ");
			if (src1) {
				print_reg(instr->alui.src);
				printf(", ");
			}
			printf("0x%04x", instr->alui.uimm);
			print_gpu_reg(instr->alui.uimm);

			/* print out unexpected bits: */
			if (instr->alui.src && !src1)
				printf("  (src=%02x)", instr->alui.src);

			break;
		}
		case OPC_BR: {
			unsigned off = i + instr->br.iimm;

			/* Since $00 reads back zero, it can be used as src for
			 * unconditional branches.
			 *
			 * If bit=0 then branch is taken if *all* bits are zero.
			 * Otherwise it is taken if bit (bit-1) is clear.
			 *
			 * Note the instruction after a jump/branch is executed
			 * regardless of whether branch is taken, so use nop or
			 * take that into account in code.
			 */
			if (instr->br.src) {
				printf("br ");
				print_reg(instr->br.src);
				if (instr->br.bit) {
					printf(", b%u", instr->br.bit);
				}
			} else {
				printf("jump");
				if (instr->br.bit)
					printf("  (bit=%03x)", instr->br.bit - 1);
			}

			printf(" #l%02d", label_idx(off, false));
			printf(" (#%d, %04x)", instr->br.iimm, off);
			break;
		}
		case OPC_MOVI:
			printf("movi ");
			print_reg(instr->movi.dst);
			printf(", 0x%04x", instr->movi.uimm);
			if (instr->movi.shift)
				printf(" << %u", instr->movi.shift);
			print_gpu_reg(instr->movi.uimm << instr->movi.shift);
			break;
		case OPC_ALU: {
			bool src1 = true;

			/* special case mnemonics:
			 *   reading $00 seems to always yield zero, and so:
			 *      or $dst, $00, $src -> mov $dst, $src
			 *   Maybe add one for negate too, ie.
			 *      sub $dst, $00, $src ???
			 */
			if ((instr->alu.alu == ALU_OR) && !instr->alu.src1) {
				printf("mov ");
				src1 = false;
			} else if (instr->alu.alu == ALU_ADD) {
				printf("add ");
			} else if (instr->alu.alu == ALU_ADD2) {
				printf("add2 ");
			} else if (instr->alu.alu == ALU_SUB) {
				printf("sub ");
			} else if (instr->alu.alu == ALU_SUB2) {
				printf("sub2 ");
			} else if (instr->alu.alu == ALU_AND) {
				printf("and ");
			} else if (instr->alu.alu == ALU_OR) {
				printf("or ");
			} else if (instr->alu.alu == ALU_XOR) {
				printf("xor ");
			} else if (instr->alu.alu == ALU_NOT) {
				printf("not ");
				src1 = false;
			} else if (instr->alu.alu == ALU_SHL) {
				printf("shl ");
			} else if (instr->alu.alu == ALU_USHR) {
				printf("ushr ");
			} else if (instr->alu.alu == ALU_ISHR) {
				printf("ishr ");
			} else if (instr->alu.alu == ALU_MUL8) {
				printf("mul8 ");
			} else if (instr->alu.alu == ALU_MIN) {
				printf("min ");
			} else if (instr->alu.alu == ALU_MAX) {
				printf("max ");
			} else {
				printf("alu%02x ", instr->alu.alu);
			}

			print_reg(instr->alu.dst);
			if (src1) {
				printf(", ");
				print_reg(instr->alu.src1);
			}
			printf(", ");
			print_reg(instr->alu.src2);

			/* print out unexpected bits: */
			if (instr->alu.pad0)
				printf("  (pad0=%03x)", instr->alu.pad0);
			if (instr->alu.src1 && !src1)
				printf("  (src1=%02x)", instr->alu.src1);
			break;
		}
		default:
			printf("op%02x ", instr->opc);
			print_reg(instr->alui.dst);
			printf(", ");
			print_reg(instr->alui.src);
			printf("\t");
			print_gpu_reg(instrs[i] & 0xffff);
			break;
		}
		printf("\n");
	}

	/* print jumptable: */
	printf(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
	printf("; JUMP TABLE\n");
	for (i = 0; i < 0x7f; i++) {
		int n = i;// + CP_NOP;
		uint32_t offset = jmptbl[i];
		char *name = getpm4(n);
		printf("%3d %02x: ", n, n);
		printf("%04x", offset);
		if (name) {
			printf("   ; %s", name);
		} else {
			printf("   ; UNKN%d", n);
		}
		printf("\n");
	}
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
	uint32_t *buf;
	char *file, *name;
	int sz;

	if (argc != 2) {
		printf("usage: fwdump5 <pm4.fw>\n");
		return -1;
	}

	file = argv[1];

	if (strstr(file, "a5")) {
		printf("; matching a5xx\n");
		gpuver = 500;
		name = "A5XX";
	} else {
		printf("unknown GPU version!\n");
		return -1;
	}

	rnn_init();
	db = rnn_newdb();

	ctx = rnndec_newcontext(db);
	ctx->colors = &envy_null_colors;

	rnn_parsefile(db, "adreno.xml");
	dom[0] = rnn_finddomain(db, name);
	dom[1] = rnn_finddomain(db, "AXXX");

	buf = (uint32_t *)readfile(file, &sz);

	if (strstr(file, "_pm4") || strstr(file, "_me")) {
		printf("; Disassembling microcode (PM4) %s:\n", file);
		printf("; Version: %08x\n\n", buf[1]);
		disasm(&buf[1], sz/4 - 1);
	} else if (strstr(file, "_pfp")) {
		printf("; Disassembling microcode (PM4) %s:\n", file);
		printf("; Version: %08x\n\n", buf[1]);
		disasm(&buf[1], sz/4 - 1);
	}

	return 0;
}
