/*
 *  Copyright (C) 2002-2010  The DOSBox Team
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* $Id: vga_memory.cpp,v 1.53 2009-07-04 21:23:35 qbix79 Exp $ */

#include <stdlib.h>
#include <string.h>
#include "dosbox.h"
#include "mem.h"
#include "vga.h"
#include "paging.h"
#include "pic.h"
#include "inout.h"
#include "setup.h"


#ifndef C_VGARAM_CHECKED
#define C_VGARAM_CHECKED 1
#endif

#if C_VGARAM_CHECKED
// Checked linear offset
#define CHECKED(v) ((v)&(vga.vmemwrap-1))
// Checked planar offset (latched access)
#define CHECKED2(v) ((v)&((vga.vmemwrap>>2)-1))
#else
#define CHECKED(v) (v)
#define CHECKED2(v) (v)
#endif

#define CHECKED3(v) ((v)&(vga.vmemwrap-1))
#define CHECKED4(v) ((v)&((vga.vmemwrap>>2)-1))


#ifdef VGA_KEEP_CHANGES
#define MEM_CHANGED( _MEM ) vga.changes.map[ (_MEM) >> VGA_CHANGE_SHIFT ] |= vga.changes.writeMask;
//#define MEM_CHANGED( _MEM ) vga.changes.map[ (_MEM) >> VGA_CHANGE_SHIFT ] = 1;
#else
#define MEM_CHANGED( _MEM ) 
#endif

#define TANDY_VIDBASE(_X_)  &MemBase[ 0x80000 + (_X_)]

template <class Size>
static INLINE void hostWrite(HostPt off, Bitu val) {
	if ( sizeof( Size ) == 1)
		host_writeb( off, (Bit8u)val );
	else if ( sizeof( Size ) == 2)
		host_writew( off, (Bit16u)val );
	else if ( sizeof( Size ) == 4)
		host_writed( off, (Bit32u)val );
}

template <class Size>
static INLINE Bitu  hostRead(HostPt off ) {
	if ( sizeof( Size ) == 1)
		return host_readb( off );
	else if ( sizeof( Size ) == 2)
		return host_readw( off );
	else if ( sizeof( Size ) == 4)
		return host_readd( off );
	return 0;
}


/* how much delay to add to VGA memory I/O in nanoseconds */
int vga_memio_delay_ns = 1000;
bool vga_memio_lfb_delay = false;

void VGAMEM_USEC_read_delay() {
	if (vga_memio_delay_ns > 0) {
		Bits delaycyc = (CPU_CycleMax * vga_memio_delay_ns) / 1000000;
		CPU_Cycles -= delaycyc;
	}
}

void VGAMEM_USEC_write_delay() {
	if (vga_memio_delay_ns > 0) {
		Bits delaycyc = (CPU_CycleMax * vga_memio_delay_ns * 3) / (1000000 * 4);
		CPU_Cycles -= delaycyc;
	}
}

template <class baseLFBHandler> class VGA_SlowLFBHandler : public baseLFBHandler {
	public:
		VGA_SlowLFBHandler() : baseLFBHandler(PFLAG_NOCODE) {}
		void writeb(PhysPt addr,Bitu val) {
			VGAMEM_USEC_write_delay();
			baseLFBHandler::writeb(addr,val);
		}
		void writew(PhysPt addr,Bitu val) {
			VGAMEM_USEC_write_delay();
			baseLFBHandler::writew(addr,val);
		}
		void writed(PhysPt addr,Bitu val) {
			VGAMEM_USEC_write_delay();
			baseLFBHandler::writed(addr,val);
		}
		Bitu readb(PhysPt addr) {
			VGAMEM_USEC_read_delay();
			return baseLFBHandler::readb(addr);
		}
		Bitu readw(PhysPt addr) {
			VGAMEM_USEC_read_delay();
			return baseLFBHandler::readw(addr);
		}
		Bitu readd(PhysPt addr) {
			VGAMEM_USEC_read_delay();
			return baseLFBHandler::readd(addr);
		}
};

void VGA_MapMMIO(void);

// Called whenever VRAM is written to, can be used to trigger redraw
static inline void vga_vram_write_trigger_update(void) {
	// No-op for now (DOSBox-X has a must_complete_frame flag)
}

//Nice one from DosEmu
INLINE static Bit32u RasterOp(Bit32u input,Bit32u mask) {
	switch (vga.config.raster_op) {
	case 0x00:	/* None */
		return (input & mask) | (vga.latch.d & ~mask);
	case 0x01:	/* AND */
		return (input | ~mask) & vga.latch.d;
	case 0x02:	/* OR */
		return (input & mask) | vga.latch.d;
	case 0x03:	/* XOR */
		return (input & mask) ^ vga.latch.d;
	};
	return 0;
}

INLINE static Bit32u ModeOperation(Bit8u val) {
	Bit32u full;
	switch (vga.config.write_mode) {
	case 0x00:
		// Write Mode 0: In this mode, the host data is first rotated as per the Rotate Count field, then the Enable Set/Reset mechanism selects data from this or the Set/Reset field. Then the selected Logical Operation is performed on the resulting data and the data in the latch register. Then the Bit Mask field is used to select which bits come from the resulting data and which come from the latch register. Finally, only the bit planes enabled by the Memory Plane Write Enable field are written to memory. 
		val=((val >> vga.config.data_rotate) | (val << (8-vga.config.data_rotate)));
		full=ExpandTable[val];
		full=(full & vga.config.full_not_enable_set_reset) | vga.config.full_enable_and_set_reset; 
		full=RasterOp(full,vga.config.full_bit_mask);
		break;
	case 0x01:
		// Write Mode 1: In this mode, data is transferred directly from the 32 bit latch register to display memory, affected only by the Memory Plane Write Enable field. The host data is not used in this mode. 
		full=vga.latch.d;
		break;
	case 0x02:
		//Write Mode 2: In this mode, the bits 3-0 of the host data are replicated across all 8 bits of their respective planes. Then the selected Logical Operation is performed on the resulting data and the data in the latch register. Then the Bit Mask field is used to select which bits come from the resulting data and which come from the latch register. Finally, only the bit planes enabled by the Memory Plane Write Enable field are written to memory. 
		full=RasterOp(FillTable[val&0xF],vga.config.full_bit_mask);
		break;
	case 0x03:
		// Write Mode 3: In this mode, the data in the Set/Reset field is used as if the Enable Set/Reset field were set to 1111b. Then the host data is first rotated as per the Rotate Count field, then logical ANDed with the value of the Bit Mask field. The resulting value is used on the data obtained from the Set/Reset field in the same way that the Bit Mask field would ordinarily be used. to select which bits come from the expansion of the Set/Reset field and which come from the latch register. Finally, only the bit planes enabled by the Memory Plane Write Enable field are written to memory.
		val=((val >> vga.config.data_rotate) | (val << (8-vga.config.data_rotate)));
		full=RasterOp(vga.config.full_set_reset,ExpandTable[val] & vga.config.full_bit_mask);
		break;
	default:
		LOG(LOG_VGAMISC,LOG_NORMAL)("VGA:Unsupported write mode %d",vga.config.write_mode);
		full=0;
		break;
	}
	return full;
}

/* Gonna assume that whoever maps vga memory, maps it on 32/64kb boundary */

#define VGA_PAGES		(128/4)
#define VGA_PAGE_A0		(0xA0000/4096)
#define VGA_PAGE_B0		(0xB0000/4096)
#define VGA_PAGE_B8		(0xB8000/4096)

static struct {
	Bitu base, mask;
} vgapages;

static inline Bitu VGA_Generic_Read_Handler(PhysPt planeaddr,PhysPt rawaddr,Bitu plane) {
	const Bitu hobit_n = ((vga.seq.memory_mode&2) || (IS_VGA_ARCH)) ? 16u : 14u;

	if (!(vga.seq.memory_mode&4)) /* Odd Even Host Memory Write Addressing Disable (is not set) */
		plane = (plane & ~1u) + (rawaddr & 1u);

	if ((vga.gfx.miscellaneous&2)) {/* Odd/Even enable */
		const PhysPt mask = (vga.config.compatible_chain4 ? 0u : ~0xFFFFu) + (1u << hobit_n) - 2u;
		const PhysPt hobit = (planeaddr >> hobit_n) & 1u;
		planeaddr = (planeaddr & mask & (vga.vmemwrap >> 2u - 1u)) + hobit;
	}
	else {
		const PhysPt mask = (vga.config.compatible_chain4 ? 0u : ~0xFFFFu) + (1u << hobit_n) - 1u;
		planeaddr &= mask & (vga.vmemwrap >> 2u - 1u);
	}

	vga.latch.d=((Bit32u*)vga.mem.linear)[planeaddr];
	switch (vga.config.read_mode) {
		case 0:
			return (vga.latch.b[plane]);
		case 1:
			VGA_Latch templatch;
			templatch.d=(vga.latch.d & FillTable[vga.config.color_dont_care]) ^ FillTable[vga.config.color_compare & vga.config.color_dont_care];
			return (Bit8u)~(templatch.b[0] | templatch.b[1] | templatch.b[2] | templatch.b[3]);
	}

	return 0;
}

template <const bool chained> static inline void VGA_Generic_Write_Handler(PhysPt planeaddr,PhysPt rawaddr,Bit8u val) {
	const Bitu hobit_n = ((vga.seq.memory_mode&2) || (IS_VGA_ARCH)) ? 16u : 14u;
	Bit32u mask = vga.config.full_map_mask;

	if (chained) {
		if (!(vga.seq.memory_mode&4))
			mask &= 0xFF00FFu << ((rawaddr & 1u) * 8u);
		else
			mask &= 0xFFu << ((rawaddr & 3u) * 8u);
	}
	else {
		if (!(vga.seq.memory_mode&4))
			mask &= 0xFF00FFu << ((rawaddr & 1u) * 8u);
	}

	if ((vga.gfx.miscellaneous&2)) {/* Odd/Even enable */
		const PhysPt mask = (vga.config.compatible_chain4 ? 0u : ~0xFFFFu) + (1u << hobit_n) - 2u;
		const PhysPt hobit = (planeaddr >> hobit_n) & 1u;
		planeaddr = (planeaddr & mask & (vga.vmemwrap >> 2u - 1u)) + hobit;
	}
	else {
		const PhysPt mask = (vga.config.compatible_chain4 ? 0u : ~0xFFFFu) + (1u << hobit_n) - 1u;
		planeaddr &= mask & (vga.vmemwrap >> 2u - 1u);
	}

	Bit32u data=ModeOperation(val);
	VGA_Latch pixels;
	pixels.d =((Bit32u*)vga.mem.linear)[planeaddr];
	pixels.d&=~mask;
	pixels.d|=(data & mask);
	((Bit32u*)vga.mem.linear)[planeaddr]=pixels.d;
}
	
class VGA_UnchainedRead_Handler : public PageHandler {
public:
	Bitu readHandler(PhysPt start) {
		vga.latch.d=((Bit32u*)vga.mem.linear)[start];
		switch (vga.config.read_mode) {
		case 0:
			return (vga.latch.b[vga.config.read_map_select]);
		case 1:
			VGA_Latch templatch;
			templatch.d=(vga.latch.d &	FillTable[vga.config.color_dont_care]) ^ FillTable[vga.config.color_compare & vga.config.color_dont_care];
			return (Bit8u)~(templatch.b[0] | templatch.b[1] | templatch.b[2] | templatch.b[3]);
		}
		return 0;
	}
public:
	Bitu readb(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED2(addr);
		return readHandler(addr);
	}
	Bitu readw(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED2(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		return  ret;
	}
	Bitu readd(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED2(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		ret     |= (readHandler(addr+2) << 16);
		ret     |= (readHandler(addr+3) << 24);
		return ret;
	}
};

class VGA_ChainedEGA_Handler : public PageHandler {
public:
	Bitu readHandler(PhysPt addr) {
		return vga.mem.linear[addr];
	}
	void writeHandler(PhysPt start, Bit8u val) {
		ModeOperation(val);
		/* Update video memory and the pixel buffer */
		VGA_Latch pixels;
		vga.mem.linear[start] = val;
		start >>= 2;
		pixels.d=((Bit32u*)vga.mem.linear)[start];

		Bit8u * write_pixels=&vga.fastmem[start<<3];

		Bit32u colors0_3, colors4_7;
		VGA_Latch temp;temp.d=(pixels.d>>4) & 0x0f0f0f0f;
		colors0_3 = 
			Expand16Table[0][temp.b[0]] |
			Expand16Table[1][temp.b[1]] |
			Expand16Table[2][temp.b[2]] |
			Expand16Table[3][temp.b[3]];
		*(Bit32u *)write_pixels=colors0_3;
		temp.d=pixels.d & 0x0f0f0f0f;
		colors4_7 = 
			Expand16Table[0][temp.b[0]] |
			Expand16Table[1][temp.b[1]] |
			Expand16Table[2][temp.b[2]] |
			Expand16Table[3][temp.b[3]];
		*(Bit32u *)(write_pixels+4)=colors4_7;
	}
public:	
	VGA_ChainedEGA_Handler()  {
		flags=PFLAG_NOCODE;
	}
	void writeb(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		MEM_CHANGED( addr << 3);
		writeHandler(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		MEM_CHANGED( addr << 3);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		MEM_CHANGED( addr << 3);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
		writeHandler(addr+2,(Bit8u)(val >> 16));
		writeHandler(addr+3,(Bit8u)(val >> 24));
	}
	Bitu readb(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return readHandler(addr);
	}
	Bitu readw(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		return ret;
	}
	Bitu readd(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		ret     |= (readHandler(addr+2) << 16);
		ret     |= (readHandler(addr+3) << 24);
		return ret;
	}
};

class VGA_UnchainedEGA_Handler : public VGA_UnchainedRead_Handler {
public:
	template< bool wrapping>
	void writeHandler(PhysPt start, Bit8u val) {
		Bit32u data=ModeOperation(val);
		/* Update video memory and the pixel buffer */
		VGA_Latch pixels;
		pixels.d=((Bit32u*)vga.mem.linear)[start];
		pixels.d&=vga.config.full_not_map_mask;
		pixels.d|=(data & vga.config.full_map_mask);
		((Bit32u*)vga.mem.linear)[start]=pixels.d;
		Bit8u * write_pixels=&vga.fastmem[start<<3];

		Bit32u colors0_3, colors4_7;
		VGA_Latch temp;temp.d=(pixels.d>>4) & 0x0f0f0f0f;
			colors0_3 = 
			Expand16Table[0][temp.b[0]] |
			Expand16Table[1][temp.b[1]] |
			Expand16Table[2][temp.b[2]] |
			Expand16Table[3][temp.b[3]];
		*(Bit32u *)write_pixels=colors0_3;
		temp.d=pixels.d & 0x0f0f0f0f;
		colors4_7 = 
			Expand16Table[0][temp.b[0]] |
			Expand16Table[1][temp.b[1]] |
			Expand16Table[2][temp.b[2]] |
			Expand16Table[3][temp.b[3]];
		*(Bit32u *)(write_pixels+4)=colors4_7;
	}
public:	
	VGA_UnchainedEGA_Handler()  {
		flags=PFLAG_NOCODE;
	}
	void writeb(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		MEM_CHANGED( addr << 3);
		writeHandler<true>(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		MEM_CHANGED( addr << 3);
		writeHandler<true>(addr+0,(Bit8u)(val >> 0));
		writeHandler<true>(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		MEM_CHANGED( addr << 3);
		writeHandler<true>(addr+0,(Bit8u)(val >> 0));
		writeHandler<true>(addr+1,(Bit8u)(val >> 8));
		writeHandler<true>(addr+2,(Bit8u)(val >> 16));
		writeHandler<true>(addr+3,(Bit8u)(val >> 24));
	}
};

// Fast path for chained VGA (256-color) mode.
// Does direct chain4remap without ROP/bitmask processing for maximum performance.
class VGA_ChainedVGA_Handler : public PageHandler {
public:
	VGA_ChainedVGA_Handler()  {
		flags=PFLAG_NOCODE;
	}

	static INLINE PhysPt chain4remap(const PhysPt addr) {
		return ((addr & ~3u) << 2u) + (addr & 3u);
	}
	static INLINE PhysPt map(const PhysPt addr) {
		return chain4remap((PAGING_GetPhysicalAddress(addr)&vgapages.mask)+(PhysPt)vga.svga.bank_read_full)&(vga.vmemwrap-1);
	}

	template <typename T=Bit8u> static INLINE T do_read_aligned(const PhysPt a) {
		return *((T*)(&vga.mem.linear[a]));
	}
	template <typename T=Bit8u> static INLINE T do_read(const PhysPt a) {
		if (sizeof(T) == 4)
			return (Bit32u)do_read<Bit16u>(a) + ((Bit32u)do_read<Bit16u>(a+2) << (Bit32u)16u);
		else if (sizeof(T) == 2)
			return (Bit16u)do_read<Bit8u>(a) + ((Bit16u)do_read<Bit8u>(a+1) << (Bit32u)8u);
		else
			return do_read_aligned<T>(map(a));
	}
	template <typename T=Bit8u> static INLINE void do_write_aligned(const PhysPt a,const T v) {
		vga_vram_write_trigger_update();
		*((T*)(&vga.mem.linear[a])) = v;
	}
	template <typename T=Bit8u> static INLINE void do_write(const PhysPt a,const T v) {
		if (sizeof(T) == 4)
			{ do_write<Bit16u>(a,(Bit16u)v); do_write<Bit16u>(a+2,(Bit16u)(v >> (T)16u)); }
		else if (sizeof(T) == 2)
			{ do_write<Bit8u>(a,(Bit8u)v); do_write<Bit8u>(a+1,(Bit8u)(v >> (T)8u)); }
		else
			do_write_aligned<T>(map(a),v);
	}

	Bitu readb(PhysPt addr) {
		VGAMEM_USEC_read_delay(); return do_read<Bit8u>(addr);
	}
	Bitu readw(PhysPt addr) {
		VGAMEM_USEC_read_delay(); return do_read<Bit16u>(addr);
	}
	Bitu readd(PhysPt addr) {
		VGAMEM_USEC_read_delay(); return do_read<Bit32u>(addr);
	}
	void writeb(PhysPt addr, Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit8u>(addr,(Bit8u)val);
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit16u>(addr,(Bit16u)val);
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit32u>(addr,(Bit32u)val);
	}
};

// Slow accurate version of chained VGA handler.
// Takes bitmask, ROP, and other complex operations into account.
class VGA_ChainedVGA_Slow_Handler : public PageHandler {
public:
	VGA_ChainedVGA_Slow_Handler()  {
		flags=PFLAG_NOCODE;
	}
	static INLINE PhysPt map(const PhysPt addr) {
		return (PAGING_GetPhysicalAddress(addr)&vgapages.mask)+(PhysPt)vga.svga.bank_read_full;
	}
	static INLINE PhysPt planemap(const PhysPt addr) {
		return ((PAGING_GetPhysicalAddress(addr)&vgapages.mask)+(PhysPt)vga.svga.bank_read_full) & ((vga.vmemwrap>>2u)-1u);
	}
	static INLINE Bit8u readHandler8(PhysPt addr) {
		return VGA_Generic_Read_Handler(addr&~3u, addr, (Bit8u)(addr&3u));
	}
	static INLINE void writeHandler8(PhysPt addr, Bit8u val) {
		vga_vram_write_trigger_update();
		return VGA_Generic_Write_Handler<true>(addr&~3u, addr, (Bit8u)val);
	}
	template <typename T=Bit8u> static INLINE T do_read(const PhysPt a) {
		if (sizeof(T) == 4)
			return (Bit32u)do_read<Bit16u>(a) + ((Bit32u)do_read<Bit16u>(a+2) << (Bit32u)16u);
		else if (sizeof(T) == 2)
			return (Bit16u)do_read<Bit8u>(a) + ((Bit16u)do_read<Bit8u>(a+1) << (Bit32u)8u);
		else
			return (T)readHandler8(map(a));
	}
	template <typename T=Bit8u> static INLINE void do_write(const PhysPt a,const T v) {
		if (sizeof(T) == 4)
			{ do_write<Bit16u>(a,(Bit16u)v); do_write<Bit16u>(a+2,(Bit16u)(v >> (T)16u)); }
		else if (sizeof(T) == 2)
			{ do_write<Bit8u>(a,(Bit8u)v); do_write<Bit8u>(a+1,(Bit8u)(v >> (T)8u)); }
		else
			writeHandler8(planemap(a),v);
	}
	Bitu readb(PhysPt addr) {
		VGAMEM_USEC_read_delay(); return do_read<Bit8u>(addr);
	}
	Bitu readw(PhysPt addr) {
		VGAMEM_USEC_read_delay(); return do_read<Bit16u>(addr);
	}
	Bitu readd(PhysPt addr) {
		VGAMEM_USEC_read_delay(); return do_read<Bit32u>(addr);
	}
	void writeb(PhysPt addr, Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit8u>(addr,(Bit8u)val);
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit16u>(addr,(Bit16u)val);
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit32u>(addr,(Bit32u)val);
	}
};

class VGA_UnchainedVGA_Handler : public VGA_UnchainedRead_Handler {
public:
	void writeHandler( PhysPt addr, Bit8u val ) {
		Bit32u data=ModeOperation(val);
		VGA_Latch pixels;
		pixels.d=((Bit32u*)vga.mem.linear)[addr];
		pixels.d&=vga.config.full_not_map_mask;
		pixels.d|=(data & vga.config.full_map_mask);
		((Bit32u*)vga.mem.linear)[addr]=pixels.d;
	}
public:
	VGA_UnchainedVGA_Handler()  {
		flags=PFLAG_NOCODE;
	}
	void writeb(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		MEM_CHANGED( addr << 2 );
		writeHandler(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		MEM_CHANGED( addr << 2);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		MEM_CHANGED( addr << 2);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
		writeHandler(addr+2,(Bit8u)(val >> 16));
		writeHandler(addr+3,(Bit8u)(val >> 24));
	}
};

// Fast version for unchained VGA modes when no complex bitmask/ROP is needed
class VGA_UnchainedVGA_Fast_Handler : public VGA_UnchainedVGA_Handler {
public:
	VGA_UnchainedVGA_Fast_Handler() : VGA_UnchainedVGA_Handler() {
		flags=PFLAG_NOCODE;
	}
	static INLINE PhysPt map(const PhysPt addr) {
		return ((PAGING_GetPhysicalAddress(addr)&vgapages.mask)+(PhysPt)vga.svga.bank_read_full)&(vga.vmemwrap>>2u)-1u;
	}
	static INLINE void writeHandler(PhysPt addr, Bit8u val) {
		vga_vram_write_trigger_update();
		((Bit32u*)vga.mem.linear)[addr] =
			(((Bit32u*)vga.mem.linear)[addr] & vga.config.full_not_map_mask) + (ExpandTable[val] & vga.config.full_map_mask);
	}
	template <typename T=Bit8u> static INLINE void do_write(const PhysPt a,const T v) {
		if (sizeof(T) == 4)
			{ do_write<Bit16u>(a,(Bit16u)v); do_write<Bit16u>(a+2,(Bit16u)(v >> (T)16u)); }
		else if (sizeof(T) == 2)
			{ do_write<Bit8u>(a,(Bit8u)v); do_write<Bit8u>(a+1,(Bit8u)(v >> (T)8u)); }
		else
			writeHandler(map(a),v);
	}
	void writeb(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit8u>(addr,(Bit8u)val);
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit16u>(addr,(Bit16u)val);
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay(); do_write<Bit32u>(addr,(Bit32u)val);
	}
};

class VGA_TEXT_PageHandler : public PageHandler {
public:
	VGA_TEXT_PageHandler() {
		flags=PFLAG_NOCODE;
	}
	Bitu readb(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		return vga.draw.font[addr];
	}
	void writeb(PhysPt addr,Bitu val){
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		if (vga.seq.map_mask & 0x4) {
			vga.draw.font[addr]=(Bit8u)val;
		}
	}
};

class VGA_Map_Handler : public PageHandler {
public:
	VGA_Map_Handler() {
		flags=PFLAG_READABLE|PFLAG_WRITEABLE|PFLAG_NOCODE;
	}
	HostPt GetHostReadPt(Bitu phys_page) {
 		phys_page-=vgapages.base;
		return &vga.mem.linear[CHECKED3(vga.svga.bank_read_full+phys_page*4096)];
	}
	HostPt GetHostWritePt(Bitu phys_page) {
 		phys_page-=vgapages.base;
		return &vga.mem.linear[CHECKED3(vga.svga.bank_write_full+phys_page*4096)];
	}
};

class VGA_Changes_Handler : public PageHandler {
public:
	VGA_Changes_Handler() {
		flags=PFLAG_NOCODE;
	}
	Bitu readb(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return hostRead<Bit8u>( &vga.mem.linear[addr] );
	}
	Bitu readw(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return hostRead<Bit16u>( &vga.mem.linear[addr] );
	}
	Bitu readd(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return hostRead<Bit32u>( &vga.mem.linear[addr] );
	}
	void writeb(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		MEM_CHANGED( addr );
		hostWrite<Bit8u>( &vga.mem.linear[addr], val );
	}
	void writew(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		MEM_CHANGED( addr );
		hostWrite<Bit16u>( &vga.mem.linear[addr], val );
	}
	void writed(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		MEM_CHANGED( addr );	
		hostWrite<Bit32u>( &vga.mem.linear[addr], val );
	}
};

class VGA_LIN4_Handler : public VGA_UnchainedEGA_Handler {
public:
	VGA_LIN4_Handler() {
		flags=PFLAG_NOCODE;
	}
	void writeb(PhysPt addr,Bitu val) {
		addr = vga.svga.bank_write_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		MEM_CHANGED( addr << 3 );
		writeHandler<false>(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		addr = vga.svga.bank_write_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		MEM_CHANGED( addr << 3 );
		writeHandler<false>(addr+0,(Bit8u)(val >> 0));
		writeHandler<false>(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		addr = vga.svga.bank_write_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		MEM_CHANGED( addr << 3 );
		writeHandler<false>(addr+0,(Bit8u)(val >> 0));
		writeHandler<false>(addr+1,(Bit8u)(val >> 8));
		writeHandler<false>(addr+2,(Bit8u)(val >> 16));
		writeHandler<false>(addr+3,(Bit8u)(val >> 24));
	}
	Bitu readb(PhysPt addr) {
		addr = vga.svga.bank_read_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		return readHandler(addr);
	}
	Bitu readw(PhysPt addr) {
		addr = vga.svga.bank_read_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		return ret;
	}
	Bitu readd(PhysPt addr) {
		addr = vga.svga.bank_read_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		ret     |= (readHandler(addr+2) << 16);
		ret     |= (readHandler(addr+3) << 24);
		return ret;
	}
};


class VGA_LFBChanges_Handler : public PageHandler {
public:
	VGA_LFBChanges_Handler() {
		flags=PFLAG_NOCODE;
	}
	Bitu readb(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) - vga.lfb.addr;
		addr = CHECKED(addr);
		return hostRead<Bit8u>( &vga.mem.linear[addr] );
	}
	Bitu readw(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) - vga.lfb.addr;
		addr = CHECKED(addr);
		return hostRead<Bit16u>( &vga.mem.linear[addr] );
	}
	Bitu readd(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) - vga.lfb.addr;
		addr = CHECKED(addr);
		return hostRead<Bit32u>( &vga.mem.linear[addr] );
	}
	void writeb(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) - vga.lfb.addr;
		addr = CHECKED(addr);
		hostWrite<Bit8u>( &vga.mem.linear[addr], val );
		MEM_CHANGED( addr );
	}
	void writew(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) - vga.lfb.addr;
		addr = CHECKED(addr);
		hostWrite<Bit16u>( &vga.mem.linear[addr], val );
		MEM_CHANGED( addr );
	}
	void writed(PhysPt addr,Bitu val) {
		addr = PAGING_GetPhysicalAddress(addr) - vga.lfb.addr;
		addr = CHECKED(addr);
		hostWrite<Bit32u>( &vga.mem.linear[addr], val );
		MEM_CHANGED( addr );
	}
};

class VGA_LFB_Handler : public PageHandler {
public:
	VGA_LFB_Handler() {
		flags=PFLAG_READABLE|PFLAG_WRITEABLE|PFLAG_NOCODE;
	}
	HostPt GetHostReadPt( Bitu phys_page ) {
		phys_page -= vga.lfb.page;
		return &vga.mem.linear[CHECKED3(phys_page * 4096)];
	}
	HostPt GetHostWritePt( Bitu phys_page ) {
		return GetHostReadPt( phys_page );
	}
};

extern void XGA_Write(Bitu port, Bitu val, Bitu len);
extern Bitu XGA_Read(Bitu port, Bitu len);

class VGA_MMIO_Handler : public PageHandler {
public:
	VGA_MMIO_Handler() {
		flags=PFLAG_NOCODE;
	}
	void writeb(PhysPt addr,Bitu val) {
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		XGA_Write(port, val, 1);
	}
	void writew(PhysPt addr,Bitu val) {
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		XGA_Write(port, val, 2);
	}
	void writed(PhysPt addr,Bitu val) {
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		XGA_Write(port, val, 4);
	}

	Bitu readb(PhysPt addr) {
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		return XGA_Read(port, 1);
	}
	Bitu readw(PhysPt addr) {
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		return XGA_Read(port, 2);
	}
	Bitu readd(PhysPt addr) {
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		return XGA_Read(port, 4);
	}
};

class VGA_TANDY_PageHandler : public PageHandler {
public:
	VGA_TANDY_PageHandler() {
		flags=PFLAG_READABLE|PFLAG_WRITEABLE;
//			|PFLAG_NOCODE;
	}
	HostPt GetHostReadPt(Bitu phys_page) {
		if (vga.tandy.mem_bank & 1) 
			phys_page&=0x03;
		else 
			phys_page&=0x07;
		return vga.tandy.mem_base + (phys_page * 4096);
	}
	HostPt GetHostWritePt(Bitu phys_page) {
		return GetHostReadPt( phys_page );
	}
};


class VGA_PCJR_Handler : public PageHandler {
public:
	VGA_PCJR_Handler() {
		flags=PFLAG_READABLE|PFLAG_WRITEABLE;
	}
	HostPt GetHostReadPt(Bitu phys_page) {
		phys_page-=0xb8;
		//test for a unaliged bank, then replicate 2x16kb
		if (vga.tandy.mem_bank & 1) 
			phys_page&=0x03;
		return vga.tandy.mem_base + (phys_page * 4096);
	}
	HostPt GetHostWritePt(Bitu phys_page) {
		return GetHostReadPt( phys_page );
	}
};

class VGA_Empty_Handler : public PageHandler {
public:
	VGA_Empty_Handler() {
		flags=PFLAG_NOCODE;
	}
	Bitu readb(PhysPt /*addr*/) {
//		LOG(LOG_VGA, LOG_NORMAL ) ( "Read from empty memory space at %x", addr );
		return 0xff;
	} 
	void writeb(PhysPt /*addr*/,Bitu /*val*/) {
//		LOG(LOG_VGA, LOG_NORMAL ) ( "Write %x to empty memory space at %x", val, addr );
	}
};

static struct vg {
	VGA_Map_Handler				map;
	VGA_Changes_Handler			changes;
	VGA_TEXT_PageHandler		text;
	VGA_TANDY_PageHandler		tandy;
	VGA_ChainedEGA_Handler		cega;
	VGA_ChainedVGA_Handler		cvga;
	VGA_ChainedVGA_Slow_Handler	cvga_slow;
	VGA_UnchainedEGA_Handler	uega;
	VGA_UnchainedVGA_Handler	uvga;
	VGA_UnchainedVGA_Fast_Handler	uvga_fast;
	VGA_PCJR_Handler			pcjr;
	VGA_LIN4_Handler			lin4;
	VGA_LFB_Handler				lfb;
	VGA_LFBChanges_Handler		lfbchanges;
	VGA_MMIO_Handler			mmio;
	VGA_Empty_Handler			empty;
} vgaph;

void VGA_ChangedBank(void) {
#ifndef VGA_LFB_MAPPED
	//If the mode is accurate than the correct mapper must have been installed already
	if ( vga.mode >= M_LIN4 && vga.mode <= M_LIN32 ) {
		return;
	}
#endif
	VGA_SetupHandlers();
}

void VGA_SetupHandlers(void) {
	vga.svga.bank_read_full = vga.svga.bank_read*vga.svga.bank_size;
	vga.svga.bank_write_full = vga.svga.bank_write*vga.svga.bank_size;

	PageHandler *newHandler;
	switch (machine) {
	case MCH_CGA:
	case MCH_PCJR:
		MEM_SetPageHandler( VGA_PAGE_B8, 8, &vgaph.pcjr );
		goto range_done;
	case MCH_HERC:
		vgapages.base=VGA_PAGE_B0;
		if (vga.herc.enable_bits & 0x2) {
			vgapages.mask=0xffff;
			MEM_SetPageHandler(VGA_PAGE_B0,16,&vgaph.map);
		} else {
			vgapages.mask=0x7fff;
			/* With hercules in 32kb mode it leaves a memory hole on 0xb800 */
			MEM_SetPageHandler(VGA_PAGE_B0,8,&vgaph.map);
			MEM_SetPageHandler(VGA_PAGE_B8,8,&vgaph.empty);
		}
		goto range_done;
	case MCH_TANDY:
		/* Always map 0xa000 - 0xbfff, might overwrite 0xb800 */
		vgapages.base=VGA_PAGE_A0;
		vgapages.mask=0x1ffff;
		MEM_SetPageHandler(VGA_PAGE_A0, 32, &vgaph.map );
		if ( vga.tandy.extended_ram & 1 ) {
			//You seem to be able to also map different 64kb banks, but have to figure that out
			//This seems to work so far though
			vga.tandy.draw_base = vga.mem.linear;
			vga.tandy.mem_base = vga.mem.linear;
		} else {
			vga.tandy.draw_base = TANDY_VIDBASE( vga.tandy.draw_bank * 16 * 1024);
			vga.tandy.mem_base = TANDY_VIDBASE( vga.tandy.mem_bank * 16 * 1024);
			MEM_SetPageHandler( 0xb8, 8, &vgaph.tandy );
		}
		goto range_done;
//		MEM_SetPageHandler(vga.tandy.mem_bank<<2,vga.tandy.is_32k_mode ? 0x08 : 0x04,range_handler);
	case EGAVGA_ARCH_CASE:
		break;
	default:
		LOG_MSG("Illegal machine type %d", machine );
		return;
	}

	/* This should be vga only */
	switch (vga.mode) {
	case M_ERROR:
	default:
		return;
	case M_LIN4:
		newHandler = &vgaph.lin4;
		break;	
	case M_LIN15:
	case M_LIN16:
	case M_LIN32:
		newHandler = &vgaph.uvga_fast;
		break;
	case M_LIN8:
	case M_VGA:
		if (vga.config.chained) {
			if(vga.config.compatible_chain4)
				newHandler = &vgaph.cvga;
			else
				newHandler = &vgaph.cvga_slow;
		} else {
			newHandler = &vgaph.uvga_fast;
		}
		break;
	case M_EGA:
		if (vga.config.chained) 
			newHandler = &vgaph.cega;
		else
			newHandler = &vgaph.uega;
		break;	
	case M_TEXT:
		/* Check if we're not in odd/even mode */
		if (vga.gfx.miscellaneous & 0x2) newHandler = &vgaph.map;
		else newHandler = &vgaph.text;
		break;
	case M_CGA4:
	case M_CGA2:
		newHandler = &vgaph.map;
		break;
	}
	switch ((vga.gfx.miscellaneous >> 2) & 3) {
	case 0:
		vgapages.base = VGA_PAGE_A0;
		switch (svgaCard) {
		case SVGA_TsengET3K:
		case SVGA_TsengET4K:
			vgapages.mask = 0xffff;
			break;
		case SVGA_S3Trio:
		default:
			vgapages.mask = 0x1ffff;
			break;
		}
		MEM_SetPageHandler(VGA_PAGE_A0, 32, newHandler );
		break;
	case 1:
		vgapages.base = VGA_PAGE_A0;
		vgapages.mask = 0xffff;
		MEM_SetPageHandler( VGA_PAGE_A0, 16, newHandler );
		MEM_ResetPageHandler( VGA_PAGE_B0, 16);
		break;
	case 2:
		vgapages.base = VGA_PAGE_B0;
		vgapages.mask = 0x7fff;
		MEM_SetPageHandler( VGA_PAGE_B0, 8, newHandler );
		MEM_ResetPageHandler( VGA_PAGE_A0, 16 );
		MEM_ResetPageHandler( VGA_PAGE_B8, 8 );
		break;
	case 3:
		vgapages.base = VGA_PAGE_B8;
		vgapages.mask = 0x7fff;
		MEM_SetPageHandler( VGA_PAGE_B8, 8, newHandler );
		MEM_ResetPageHandler( VGA_PAGE_A0, 16 );
		MEM_ResetPageHandler( VGA_PAGE_B0, 8 );
		break;
	}
	if(svgaCard == SVGA_S3Trio && (vga.s3.ext_mem_ctrl & 0x10))
		MEM_SetPageHandler(VGA_PAGE_A0, 16, &vgaph.mmio);
range_done:
	PAGING_ClearTLB();
}

void VGA_StartUpdateLFB(void) {
	vga.lfb.page = vga.s3.la_window << 4;
	vga.lfb.addr = vga.s3.la_window << 16;
#ifdef VGA_LFB_MAPPED
	vga.lfb.handler = &vgaph.lfb;
#else
	vga.lfb.handler = &vgaph.lfbchanges;
#endif
	MEM_SetLFB(vga.s3.la_window << 4 ,vga.vmemsize/4096, vga.lfb.handler, &vgaph.mmio);
}

static void VGA_Memory_ShutDown(Section * /*sec*/) {
	delete[] vga.mem.linear_orgptr;
	delete[] vga.fastmem_orgptr;
#ifdef VGA_KEEP_CHANGES
	delete[] vga.changes.map;
#endif
}

void VGA_SetupMemory(Section* sec) {
	vga.svga.bank_read = vga.svga.bank_write = 0;
	vga.svga.bank_read_full = vga.svga.bank_write_full = 0;

	Bit32u vga_allocsize=vga.vmemsize;
	// Keep lower limit at 512k
	if (vga_allocsize<512*1024) vga_allocsize=512*1024;
	// We reserve extra 2K for one scan line
	vga_allocsize+=2048;
	vga.mem.linear_orgptr = new Bit8u[vga_allocsize+16];
	vga.mem.linear=(Bit8u*)(((Bitu)vga.mem.linear_orgptr + 16-1) & ~(16-1));
	memset(vga.mem.linear,0,vga_allocsize);

	vga.fastmem_orgptr = new Bit8u[(vga.vmemsize<<1)+4096+16];
	vga.fastmem=(Bit8u*)(((Bitu)vga.fastmem_orgptr + 16-1) & ~(16-1));

	// In most cases these values stay the same. Assumptions: vmemwrap is power of 2,
	// vmemwrap <= vmemsize, fastmem implicitly has mem wrap twice as big
	vga.vmemwrap = vga.vmemsize;

#ifdef VGA_KEEP_CHANGES
	memset( &vga.changes, 0, sizeof( vga.changes ));
	int changesMapSize = (vga.vmemsize >> VGA_CHANGE_SHIFT) + 32;
	vga.changes.map = new Bit8u[changesMapSize];
	memset(vga.changes.map, 0, changesMapSize);
#endif
	vga.svga.bank_read = vga.svga.bank_write = 0;
	vga.svga.bank_read_full = vga.svga.bank_write_full = 0;
	vga.svga.bank_size = 0x10000; /* most common bank size is 64K */

	sec->AddDestroyFunction(&VGA_Memory_ShutDown);

	if (machine==MCH_PCJR) {
		/* PCJr does not have dedicated graphics memory but uses
		   conventional memory below 128k */
		//TODO map?	
	} 
}
