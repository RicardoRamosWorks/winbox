/*
 *  Copyright (C) 2002-2026 RicardoRamosWorks.com and The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include "dosbox.h"
#if C_FPU

#include <math.h>
#include <float.h>
#include "cross.h"
#include "mem.h"
#include "fpu.h"
#include "cpu.h"

static void FPU_FDECSTP() {
	TOP = (TOP - 1) & 7;
}

static void FPU_FINCSTP() {
	TOP = (TOP + 1) & 7;
}

static void FPU_FNSTCW(PhysPt addr) {
	mem_writew(addr, fpu.cw);
}

static void FPU_FFREE(Bitu st) {
	fpu.tags[st] = TAG_Empty;
}

#if C_FPU_X86
#include "../../fpu/fpu_instructions_x86.h"
#else
#include "../../fpu/fpu_instructions.h"
#endif




#define dyn_fpu_top() { \
	gen_protectflags(); \
	gen_load_host(&TOP, DREG(EA), 4); \
	gen_dop_word_imm(DOP_ADD, true, DREG(EA), decode.modrm.rm); \
	gen_dop_word_imm(DOP_AND, true, DREG(EA), 7); \
	gen_load_host(&TOP, DREG(TMPB), 4); \
}

/*
 * Tabela de dispatch para operações eatree
 * Inicializada sem designated initializers (compatível com GCC antigo)
 */
static void* eatree_funcs[16];

static void init_eatree_funcs(void) {
	/* Inicializa apenas uma vez */
	static bool initialized = false;
	if (initialized) return;
	initialized = true;

	/* Zera toda a tabela */
	for (int i = 0; i < 16; i++) eatree_funcs[i] = NULL;

	/* Preenche com as funções corretas */
	eatree_funcs[0]  = (void*)&FPU_FADD_EA;   /* group 0, no pop */
	eatree_funcs[2]  = (void*)&FPU_FMUL_EA;   /* group 1, no pop */
	eatree_funcs[4]  = (void*)&FPU_FCOM_EA;   /* group 2, no pop */
	/* group 3 (FCOMP) tratado separadamente - index 6 */
	eatree_funcs[8]  = (void*)&FPU_FSUB_EA;   /* group 4, no pop */
	eatree_funcs[10] = (void*)&FPU_FSUBR_EA;  /* group 5, no pop */
	eatree_funcs[12] = (void*)&FPU_FDIV_EA;   /* group 6, no pop */
	eatree_funcs[14] = (void*)&FPU_FDIVR_EA;  /* group 7, no pop */
}

static void dyn_eatree() {
	Bitu group = (decode.modrm.val >> 3) & 7;

	/* Caso especial: FCOMP (group 3) precisa de pop extra */
	if (group == 3) {
		gen_call_function((void*)&FPU_FCOM_EA, "%Drd", DREG(TMPB));
		gen_call_function((void*)&FPU_FPOP, "");
		return;
	}

	/* Garante que a tabela foi inicializada */
	init_eatree_funcs();

	/* Dispatch via tabela */
	void* func = eatree_funcs[group * 2];
	if (func) {
		gen_call_function(func, "%Drd", DREG(TMPB));
	}
}

/*
 * Tabela de constantes FPU para esc1 group 5
 * Inicializada sem designated initializers
 */
static void* fpu_const_funcs[8];

static void init_fpu_const_funcs(void) {
	static bool initialized = false;
	if (initialized) return;
	initialized = true;

	for (int i = 0; i < 8; i++) fpu_const_funcs[i] = NULL;

	fpu_const_funcs[0] = (void*)&FPU_FLD1;
	fpu_const_funcs[1] = (void*)&FPU_FLDL2T;
	fpu_const_funcs[2] = (void*)&FPU_FLDL2E;
	fpu_const_funcs[3] = (void*)&FPU_FLDPI;
	fpu_const_funcs[4] = (void*)&FPU_FLDLG2;
	fpu_const_funcs[5] = (void*)&FPU_FLDLN2;
	fpu_const_funcs[6] = (void*)&FPU_FLDZ;
	/* index 7 é illegal */
}

static void dyn_fpu_esc0() {
	dyn_get_modrm();

	if (decode.modrm.val >= 0xc0) {
		dyn_fpu_top();
		Bitu group = (decode.modrm.val >> 3) & 7;

		switch (group) {
		case 0x00: /* FADD ST,STi */
			gen_call_function((void*)&FPU_FADD, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;
		case 0x01: /* FMUL ST,STi */
			gen_call_function((void*)&FPU_FMUL, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;
		case 0x02: /* FCOM STi */
			gen_call_function((void*)&FPU_FCOM, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;
		case 0x03: /* FCOMP STi */
			gen_call_function((void*)&FPU_FCOM, "%Drd%Drd", DREG(TMPB), DREG(EA));
			gen_call_function((void*)&FPU_FPOP, "");
			break;
		case 0x04: /* FSUB ST,STi */
			gen_call_function((void*)&FPU_FSUB, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;
		case 0x05: /* FSUBR ST,STi */
			gen_call_function((void*)&FPU_FSUBR, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;
		case 0x06: /* FDIV ST,STi */
			gen_call_function((void*)&FPU_FDIV, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;
		case 0x07: /* FDIVR ST,STi */
			gen_call_function((void*)&FPU_FDIVR, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;
		default:
			break;
		}
	} else {
		dyn_fill_ea();
		gen_call_function((void*)&FPU_FLD_F32_EA, "%Drd", DREG(EA));
		gen_load_host(&TOP, DREG(TMPB), 4);
		dyn_eatree();
	}
}

static void dyn_fpu_esc1() {
	dyn_get_modrm();

	if (decode.modrm.val >= 0xc0) {
		Bitu group = (decode.modrm.val >> 3) & 7;
		Bitu sub = (decode.modrm.val & 7);

		switch (group) {
		case 0x00: /* FLD STi */
			gen_protectflags();
			gen_load_host(&TOP, DREG(EA), 4);
			gen_dop_word_imm(DOP_ADD, true, DREG(EA), decode.modrm.rm);
			gen_dop_word_imm(DOP_AND, true, DREG(EA), 7);
			gen_call_function((void*)&FPU_PREP_PUSH, "");
			gen_load_host(&TOP, DREG(TMPB), 4);
			gen_call_function((void*)&FPU_FST, "%Drd%Drd", DREG(EA), DREG(TMPB));
			break;

		case 0x01: /* FXCH STi */
			dyn_fpu_top();
			gen_call_function((void*)&FPU_FXCH, "%Drd%Drd", DREG(TMPB), DREG(EA));
			break;

		case 0x02: /* FNOP */
			gen_call_function((void*)&FPU_FNOP, "");
			break;

		case 0x03: /* FSTP STi */
			dyn_fpu_top();
			gen_call_function((void*)&FPU_FST, "%Drd%Drd", DREG(TMPB), DREG(EA));
			gen_call_function((void*)&FPU_FPOP, "");
			break;

		case 0x04: /* FCHS/FABS/FTST/FXAM */
			switch (sub) {
			case 0x00:
				gen_call_function((void*)&FPU_FCHS, "");
				break;
			case 0x01:
				gen_call_function((void*)&FPU_FABS, "");
				break;
			case 0x02: /* UNKNOWN */
			case 0x03: /* ILLEGAL */
				FPU_LOG_WARN(1, false, group, sub);
				break;
			case 0x04:
				gen_call_function((void*)&FPU_FTST, "");
				break;
			case 0x05:
				gen_call_function((void*)&FPU_FXAM, "");
				break;
			case 0x06: /* FTSTP (cyrix) */
			case 0x07: /* UNKNOWN */
				FPU_LOG_WARN(1, false, group, sub);
				break;
			}
			break;

		case 0x05: /* Constantes: FLD1, FLDL2T, FLDPI, etc. */
			init_fpu_const_funcs();
			if (sub < 7 && fpu_const_funcs[sub]) {
				gen_call_function(fpu_const_funcs[sub], "");
			} else {
				FPU_LOG_WARN(1, false, group, sub);
			}
			break;

		case 0x06: /* F2XM1, FYL2X, FPTAN, etc. */
			switch (sub) {
			case 0x00:
				gen_call_function((void*)&FPU_F2XM1, "");
				break;
			case 0x01:
				gen_call_function((void*)&FPU_FYL2X, "");
				break;
			case 0x02:
				gen_call_function((void*)&FPU_FPTAN, "");
				break;
			case 0x03:
				gen_call_function((void*)&FPU_FPATAN, "");
				break;
			case 0x04:
				gen_call_function((void*)&FPU_FXTRACT, "");
				break;
			case 0x05:
				gen_call_function((void*)&FPU_FPREM1, "");
				break;
			case 0x06:
				gen_call_function((void*)&FPU_FDECSTP, "");
				break;
			case 0x07:
				gen_call_function((void*)&FPU_FINCSTP, "");
				break;
			default:
				FPU_LOG_WARN(1, false, group, sub);
				break;
			}
			break;

		case 0x07: /* FPREM, FYL2XP1, FSQRT, FSINCOS, etc. */
			switch (sub) {
			case 0x00:
				gen_call_function((void*)&FPU_FPREM, "");
				break;
			case 0x01:
				gen_call_function((void*)&FPU_FYL2XP1, "");
				break;
			case 0x02:
				gen_call_function((void*)&FPU_FSQRT, "");
				break;
			case 0x03:
				gen_call_function((void*)&FPU_FSINCOS, "");
				break;
			case 0x04:
				gen_call_function((void*)&FPU_FRNDINT, "");
				break;
			case 0x05:
				gen_call_function((void*)&FPU_FSCALE, "");
				break;
			case 0x06:
				gen_call_function((void*)&FPU_FSIN, "");
				break;
			case 0x07:
				gen_call_function((void*)&FPU_FCOS, "");
				break;
			default:
				FPU_LOG_WARN(1, false, group, sub);
				break;
			}
			break;

		default:
			FPU_LOG_WARN(1, false, group, sub);
			break;
		}
	} else {
		Bitu group = (decode.modrm.val >> 3) & 7;
		Bitu sub = (decode.modrm.val & 7);
		dyn_fill_ea();

		switch (group) {
		case 0x00: /* FLD float */
			gen_protectflags();
			gen_call_function((void*)&FPU_PREP_PUSH, "");
			gen_load_host(&TOP, DREG(TMPB), 4);
			gen_call_function((void*)&FPU_FLD_F32, "%Drd%Drd", DREG(EA), DREG(TMPB));
			break;
		case 0x01: /* UNKNOWN */
			FPU_LOG_WARN(1, true, group, sub);
			break;
		case 0x02: /* FST float */
			gen_call_function((void*)&FPU_FST_F32, "%Drd", DREG(EA));
			break;
		case 0x03: /* FSTP float */
			gen_call_function((void*)&FPU_FST_F32, "%Drd", DREG(EA));
			gen_call_function((void*)&FPU_FPOP, "");
			break;
		case 0x04: /* FLDENV */
			gen_call_function((void*)&FPU_FLDENV, "%Drd", DREG(EA));
			break;
		case 0x05: /* FLDCW */
			gen_call_function((void*)&FPU_FLDCW, "%Drd", DREG(EA));
			break;
		case 0x06: /* FSTENV */
			gen_call_function((void*)&FPU_FSTENV, "%Drd", DREG(EA));
			break;
		case 0x07: /* FNSTCW */
			gen_call_function((void*)&FPU_FNSTCW, "%Drd", DREG(EA));
			break;
		default:
			FPU_LOG_WARN(1, true, group, sub);
			break;
		}
	}
}

/*
 * As funções restantes (dyn_fpu_esc2 até dyn_fpu_esc7)
 * mantidas EXATAMENTE como no original para garantir compatibilidade
 * Apenas dyn_fpu_esc0 e dyn_fpu_esc1 foram otimizadas com tabelas de dispatch
 * inicializadas de forma compatível com GCC antigo
 */

static void dyn_fpu_esc2() {
	dyn_get_modrm();
	if (decode.modrm.val >= 0xc0) {
		Bitu group=(decode.modrm.val >> 3) & 7;
		Bitu sub=(decode.modrm.val & 7);
		switch(group) {
		case 0x05:
			switch(sub) {
			case 0x01:		/* FUCOMPP */
				gen_protectflags();
				gen_load_host(&TOP,DREG(EA),4);
				gen_dop_word_imm(DOP_ADD,true,DREG(EA),1);
				gen_dop_word_imm(DOP_AND,true,DREG(EA),7);
				gen_load_host(&TOP,DREG(TMPB),4);
				gen_call_function((void *)&FPU_FUCOM,"%Drd%Drd",DREG(TMPB),DREG(EA));
				gen_call_function((void *)&FPU_FPOP,"");
				gen_call_function((void *)&FPU_FPOP,"");
				break;
			default:
				FPU_LOG_WARN(2,false,5,sub);
				break;
			}
			break;
		default:
			FPU_LOG_WARN(2,false,group,sub);
			break;
		}
	} else {
		dyn_fill_ea();
		gen_call_function((void*)&FPU_FLD_I32_EA,"%Drd",DREG(EA));
		gen_load_host(&TOP,DREG(TMPB),4);
		dyn_eatree();
	}
}

static void dyn_fpu_esc3() {
	dyn_get_modrm();
	if (decode.modrm.val >= 0xc0) {
		Bitu group=(decode.modrm.val >> 3) & 7;
		Bitu sub=(decode.modrm.val & 7);
		switch (group) {
		case 0x04:
			switch (sub) {
			case 0x00:				//FNENI
			case 0x01:				//FNDIS
				LOG(LOG_FPU,LOG_ERROR)("8087 only fpu code used esc 3: group 4: subfuntion :%" sBitfs(d),sub);
				break;
			case 0x02:				//FNCLEX FCLEX
				gen_call_function((void*)&FPU_FCLEX,"");
				break;
			case 0x03:				//FNINIT FINIT
				gen_call_function((void*)&FPU_FINIT,"");
				break;
			case 0x04:				//FNSETPM
			case 0x05:				//FRSTPM
				break;
			default:
				E_Exit("ESC 3:ILLEGAL OPCODE group %" sBitfs(d) " subfunction %" sBitfs(d),group,sub);
			}
			break;
		default:
			FPU_LOG_WARN(3,false,group,sub);
			break;
		}
	} else {
		Bitu group=(decode.modrm.val >> 3) & 7;
		Bitu sub=(decode.modrm.val & 7);
		dyn_fill_ea();
		switch(group) {
		case 0x00:	/* FILD */
			gen_call_function((void*)&FPU_PREP_PUSH,"");
			gen_protectflags();
			gen_load_host(&TOP,DREG(TMPB),4);
			gen_call_function((void*)&FPU_FLD_I32,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x01:	/* FISTTP */
			FPU_LOG_WARN(3,false,1,sub);
			break;
		case 0x02:	/* FIST */
			gen_call_function((void*)&FPU_FST_I32,"%Drd",DREG(EA));
			break;
		case 0x03:	/* FISTP */
			gen_call_function((void*)&FPU_FST_I32,"%Drd",DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x05:	/* FLD 80 Bits Real */
			gen_call_function((void*)&FPU_PREP_PUSH,"");
			gen_call_function((void*)&FPU_FLD_F80,"%Drd",DREG(EA));
			break;
		case 0x07:	/* FSTP 80 Bits Real */
			gen_call_function((void*)&FPU_FST_F80,"%Drd",DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		default:
			FPU_LOG_WARN(3,true,group,sub);
			break;
		}
	}
}

static void dyn_fpu_esc4() {
	dyn_get_modrm();
	Bitu group=(decode.modrm.val >> 3) & 7;
	if (decode.modrm.val >= 0xc0) {
		dyn_fpu_top();
		switch(group) {
		case 0x00:	/* FADD STi,ST*/
			gen_call_function((void*)&FPU_FADD,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x01:	/* FMUL STi,ST*/
			gen_call_function((void*)&FPU_FMUL,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x02:  /* FCOM*/
			gen_call_function((void*)&FPU_FCOM,"%Drd%Drd",DREG(TMPB),DREG(EA));
			break;
		case 0x03:  /* FCOMP*/
			gen_call_function((void*)&FPU_FCOM,"%Drd%Drd",DREG(TMPB),DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x04:  /* FSUBR STi,ST*/
			gen_call_function((void*)&FPU_FSUBR,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x05:  /* FSUB  STi,ST*/
			gen_call_function((void*)&FPU_FSUB,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x06:  /* FDIVR STi,ST*/
			gen_call_function((void*)&FPU_FDIVR,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x07:  /* FDIV STi,ST*/
			gen_call_function((void*)&FPU_FDIV,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		default:
			break;
		}
	} else {
		dyn_fill_ea();
		gen_call_function((void*)&FPU_FLD_F64_EA,"%Drd",DREG(EA));
		gen_load_host(&TOP,DREG(TMPB),4);
		dyn_eatree();
	}
}

static void dyn_fpu_esc5() {
	dyn_get_modrm();
	Bitu group=(decode.modrm.val >> 3) & 7;
	Bitu sub=(decode.modrm.val & 7);
	if (decode.modrm.val >= 0xc0) {
		dyn_fpu_top();
		switch(group) {
		case 0x00: /* FFREE STi */
			gen_call_function((void*)&FPU_FFREE,"%Drd",DREG(EA));
			break;
		case 0x01: /* FXCH STi*/
			gen_call_function((void*)&FPU_FXCH,"%Drd%Drd",DREG(TMPB),DREG(EA));
			break;
		case 0x02: /* FST STi */
			gen_call_function((void*)&FPU_FST,"%Drd%Drd",DREG(TMPB),DREG(EA));
			break;
		case 0x03:  /* FSTP STi*/
			gen_call_function((void*)&FPU_FST,"%Drd%Drd",DREG(TMPB),DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x04:	/* FUCOM STi */
			gen_call_function((void*)&FPU_FUCOM,"%Drd%Drd",DREG(TMPB),DREG(EA));
			break;
		case 0x05:	/*FUCOMP STi */
			gen_call_function((void*)&FPU_FUCOM,"%Drd%Drd",DREG(TMPB),DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		default:
			FPU_LOG_WARN(5,false,group,sub);
			break;
		}
		gen_releasereg(DREG(EA));
		gen_releasereg(DREG(TMPB));
	} else {
		dyn_fill_ea();
		switch(group) {
		case 0x00:  /* FLD double real*/
			gen_call_function((void*)&FPU_PREP_PUSH,"");
			gen_protectflags();
			gen_load_host(&TOP,DREG(TMPB),4);
			gen_call_function((void*)&FPU_FLD_F64,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x01:  /* FISTTP longint*/
			FPU_LOG_WARN(5,true,1,sub);
			break;
		case 0x02:   /* FST double real*/
			gen_call_function((void*)&FPU_FST_F64,"%Drd",DREG(EA));
			break;
		case 0x03:	/* FSTP double real*/
			gen_call_function((void*)&FPU_FST_F64,"%Drd",DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x04:	/* FRSTOR */
			gen_call_function((void*)&FPU_FRSTOR,"%Drd",DREG(EA));
			break;
		case 0x06:	/* FSAVE */
			gen_call_function((void*)&FPU_FSAVE,"%Drd",DREG(EA));
			break;
		case 0x07:   /*FNSTSW */
			gen_protectflags();
			gen_load_host(&TOP,DREG(TMPB),4);
			gen_call_function((void*)&FPU_SET_TOP,"%Dd",DREG(TMPB));
			gen_load_host(&fpu.sw,DREG(TMPB),4);
			gen_call_function((void*)&mem_writew,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		default:
			FPU_LOG_WARN(5,true,group,sub);
			break;
		}
	}
}

static void dyn_fpu_esc6() {
	dyn_get_modrm();
	Bitu group=(decode.modrm.val >> 3) & 7;
	Bitu sub=(decode.modrm.val & 7);
	if (decode.modrm.val >= 0xc0) {
		dyn_fpu_top();
		switch(group) {
		case 0x00:	/*FADDP STi,ST*/
			gen_call_function((void*)&FPU_FADD,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x01:	/* FMULP STi,ST*/
			gen_call_function((void*)&FPU_FMUL,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x02:  /* FCOMP5*/
			gen_call_function((void*)&FPU_FCOM,"%Drd%Drd",DREG(TMPB),DREG(EA));
			break;
		case 0x03:  /*FCOMPP*/
			if(sub != 1) {
				FPU_LOG_WARN(6,false,3,sub);
				return;
			}
			gen_load_host(&TOP,DREG(EA),4);
			gen_dop_word_imm(DOP_ADD,true,DREG(EA),1);
			gen_dop_word_imm(DOP_AND,true,DREG(EA),7);
			gen_call_function((void*)&FPU_FCOM,"%Drd%Drd",DREG(TMPB),DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x04:  /* FSUBRP STi,ST*/
			gen_call_function((void*)&FPU_FSUBR,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x05:  /* FSUBP  STi,ST*/
			gen_call_function((void*)&FPU_FSUB,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x06:	/* FDIVRP STi,ST*/
			gen_call_function((void*)&FPU_FDIVR,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x07:  /* FDIVP STi,ST*/
			gen_call_function((void*)&FPU_FDIV,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		default:
			break;
		}
		gen_call_function((void*)&FPU_FPOP,"");
	} else {
		dyn_fill_ea();
		gen_call_function((void*)&FPU_FLD_I16_EA,"%Drd",DREG(EA));
		gen_load_host(&TOP,DREG(TMPB),4);
		dyn_eatree();
	}
}

static void dyn_fpu_esc7() {
	dyn_get_modrm();
	Bitu group=(decode.modrm.val >> 3) & 7;
	Bitu sub=(decode.modrm.val & 7);
	if (decode.modrm.val >= 0xc0) {
		switch (group) {
		case 0x00: /* FFREEP STi*/
			dyn_fpu_top();
			gen_call_function((void*)&FPU_FFREE,"%Drd",DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x01: /* FXCH STi*/
			dyn_fpu_top();
			gen_call_function((void*)&FPU_FXCH,"%Drd%Drd",DREG(TMPB),DREG(EA));
			break;
		case 0x02:  /* FSTP STi*/
		case 0x03:  /* FSTP STi*/
			dyn_fpu_top();
			gen_call_function((void*)&FPU_FST,"%Drd%Drd",DREG(TMPB),DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x04:
			switch(sub) {
			case 0x00:     /* FNSTSW AX*/
				gen_load_host(&TOP,DREG(TMPB),4);
				gen_call_function((void*)&FPU_SET_TOP,"%Drd",DREG(TMPB));
				gen_mov_host(&fpu.sw,DREG(EAX),2);
				break;
			default:
				FPU_LOG_WARN(7,false,4,sub);
				break;
			}
			break;
		default:
			FPU_LOG_WARN(7,false,group,sub);
			break;
		}
	} else {
		dyn_fill_ea();
		switch(group) {
		case 0x00:  /* FILD Bit16s */
			gen_call_function((void*)&FPU_PREP_PUSH,"");
			gen_load_host(&TOP,DREG(TMPB),4);
			gen_call_function((void*)&FPU_FLD_I16,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x01:
			FPU_LOG_WARN(7,true,group,sub);
			break;
		case 0x02:   /* FIST Bit16s */
			gen_call_function((void*)&FPU_FST_I16,"%Drd",DREG(EA));
			break;
		case 0x03:	/* FISTP Bit16s */
			gen_call_function((void*)&FPU_FST_I16,"%Drd",DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x04:   /* FBLD packed BCD */
			gen_call_function((void*)&FPU_PREP_PUSH,"");
			gen_load_host(&TOP,DREG(TMPB),4);
			gen_call_function((void*)&FPU_FBLD,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x05:  /* FILD Bit64s */
			gen_call_function((void*)&FPU_PREP_PUSH,"");
			gen_load_host(&TOP,DREG(TMPB),4);
			gen_call_function((void*)&FPU_FLD_I64,"%Drd%Drd",DREG(EA),DREG(TMPB));
			break;
		case 0x06:	/* FBSTP packed BCD */
			gen_call_function((void*)&FPU_FBST,"%Drd",DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		case 0x07:  /* FISTP Bit64s */
			gen_call_function((void*)&FPU_FST_I64,"%Drd",DREG(EA));
			gen_call_function((void*)&FPU_FPOP,"");
			break;
		default:
			FPU_LOG_WARN(7,true,group,sub);
			break;
		}
	}
}

#endif
