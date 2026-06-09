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
/*
 * Funções helper de divisão - Otimizadas para Pentium M Dothan
 *
 * Pentium M tem divisão inteira relativamente lenta (~18-26 ciclos para 32-bit)
 * Otimizações focam em reduzir operações extras e melhorar branch prediction
 */

static bool dyn_helper_divb(Bit8u val) {
	if (val == 0) return CPU_PrepareException(0, 0);

	Bitu quo = reg_ax / val;
	Bit8u rem = (Bit8u)(reg_ax % val);

	/* Overflow check - se quo > 0xFF, exceção */
	if (quo > 0xFF) return CPU_PrepareException(0, 0);

	reg_ah = rem;
	reg_al = (Bit8u)quo;  /* Cast direto, já verificamos quo <= 0xFF */
	return false;
}

static bool dyn_helper_idivb(Bit8s val) {
	if (val == 0) return CPU_PrepareException(0, 0);

	Bit16s dividend = (Bit16s)reg_ax;
	Bits quo = dividend / val;
	Bit8s rem = (Bit8s)(dividend % val);

	/* Overflow check: quo deve caber em 8 bits com sinal */
	if (quo != (Bit8s)quo) return CPU_PrepareException(0, 0);

	reg_ah = (Bit8u)rem;  /* Armazena resto como unsigned */
	reg_al = (Bit8u)quo;
	return false;
}

static bool dyn_helper_divw(Bit16u val) {
	if (val == 0) return CPU_PrepareException(0, 0);

	/* Pentium M: acesso a reg_dx e reg_ax em sequência é rápido (L1 cache) */
	Bit32u num = ((Bit32u)reg_dx << 16) | reg_ax;
	Bit32u quo = num / val;

	/* Overflow check */
	if (quo > 0xFFFF) return CPU_PrepareException(0, 0);

	reg_dx = (Bit16u)(num % val);
	reg_ax = (Bit16u)quo;
	return false;
}

static bool dyn_helper_idivw(Bit16s val) {
	if (val == 0) return CPU_PrepareException(0, 0);

	Bit32s num = (Bit32s)(((Bit32u)reg_dx << 16) | reg_ax);
	Bit32s quo = num / val;

	/* Overflow check para 16-bit com sinal */
	if (quo != (Bit16s)quo) return CPU_PrepareException(0, 0);

	reg_dx = (Bit16u)(num % val);
	reg_ax = (Bit16u)quo;
	return false;
}

static bool dyn_helper_divd(Bit32u val) {
	if (val == 0) return CPU_PrepareException(0, 0);

	/*
	 * Pentium M: divisão 64-bit é emulada, ~40-60 ciclos
	 * Podemos otimizar casos especiais comuns
	 */

	/* Caso especial: divisor é potência de 2 (shift é muito mais rápido) */
	/* Nota: descomente se val for frequentemente potência de 2 */
	/*
	if ((val & (val - 1)) == 0) {
		// Divisão por potência de 2 pode ser otimizada com shift
		// mas o overflow check ainda é necessário
	}
	*/

	Bit64u num = ((Bit64u)reg_edx << 32) | reg_eax;
	Bit64u quo = num / val;

	/* Overflow check */
	if (quo > 0xFFFFFFFFULL) return CPU_PrepareException(0, 0);

	reg_edx = (Bit32u)(num % val);
	reg_eax = (Bit32u)quo;
	return false;
}

static bool dyn_helper_idivd(Bit32s val) {
	if (val == 0) return CPU_PrepareException(0, 0);

	/*
	 * Caso especial: divisor = -1 e dividendo = 0x80000000
	 * Resulta em overflow pois 0x80000000 / -1 = -0x80000000 (não cabe em 32 bits)
	 * O Pentium M pode detectar isso rapidamente
	 */

	Bit64s num = (Bit64s)(((Bit64u)reg_edx << 32) | reg_eax);
	Bit64s quo = num / val;

	/* Overflow check para 32-bit com sinal */
	if (quo != (Bit32s)quo) return CPU_PrepareException(0, 0);

	reg_edx = (Bit32u)(num % val);
	reg_eax = (Bit32u)quo;
	return false;
}

/*
 * Versões alternativas com early-out para casos comuns
 * (se o perfil de uso mostrar muitos divisores pequenos)
 */
#if 0  /* Descomente se necessário após profiling */

static bool dyn_helper_divd_fast(Bit32u val) {
	if (val == 0) return CPU_PrepareException(0, 0);

	/*
	 * Divisão 64-bit por 32-bit no Pentium M
	 * Caso comum: reg_edx < val -> quo cabe em 32 bits garantido
	 * Isso evita a divisão 64-bit completa em muitos casos
	 */
	if (reg_edx < val) {
		/*
		 * Podemos fazer divisão 32-bit:
		 * quo = reg_eax / val (parte baixa)
		 * rem = reg_eax % val
		 * Mas precisamos do quociente completo...
		 * Na verdade isso só funciona se reg_edx == 0
		 */
		if (reg_edx == 0) {
			reg_eax = reg_eax / val;
			reg_edx = reg_eax % val;
			return false;
		}
	}

	/* Fallback para divisão 64-bit completa */
	Bit64u num = ((Bit64u)reg_edx << 32) | reg_eax;
	Bit64u quo = num / val;

	if (quo > 0xFFFFFFFFULL) return CPU_PrepareException(0, 0);

	reg_edx = (Bit32u)(num % val);
	reg_eax = (Bit32u)quo;
	return false;
}

#endif /* 0 */
