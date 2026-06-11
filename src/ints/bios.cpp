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

/* $Id: bios.cpp,v 1.78 2009-10-10 13:26:46 h-a-l-9000 Exp $ */
#include <string.h>
#include "dosbox.h"
#include "mem.h"
#include "bios.h"
#include "regs.h"
#include "cpu.h"
#include "callback.h"
#include "inout.h"
#include "pic.h"
#include "hardware.h"
#include "joystick.h"
#include "mouse.h"
#include "setup.h"
#include "dos_inc.h" 

/* if mem_systems 0 then size_extended is reported as the real size else 
 * zero is reported. ems and xms can increase or decrease the other_memsystems
 * counter using the BIOS_ZeroExtendedSize call */
static Bit16u size_extended;
static Bits other_memsystems=0;
void CMOS_SetRegister(Bitu regNr, Bit8u val); //For setting equipment word

static Bitu INT70_Handler(void) {
	/* Acknowledge irq with cmos */
	IO_Write(0x70,0xc);
	IO_Read(0x71);
	if (mem_readb(BIOS_WAIT_FLAG_ACTIVE)) {
		Bit32u count=mem_readd(BIOS_WAIT_FLAG_COUNT);
		if (count>997) {
			mem_writed(BIOS_WAIT_FLAG_COUNT,count-997);
		} else {
			mem_writed(BIOS_WAIT_FLAG_COUNT,0);
			PhysPt where=Real2Phys(mem_readd(BIOS_WAIT_FLAG_POINTER));
			mem_writeb(where,mem_readb(where)|0x80);
			mem_writeb(BIOS_WAIT_FLAG_ACTIVE,0);
			mem_writed(BIOS_WAIT_FLAG_POINTER,RealMake(0,BIOS_WAIT_FLAG_TEMP));
			IO_Write(0x70,0xb);
			IO_Write(0x71,IO_Read(0x71)&~0x40);
		}
	} 
	/* Signal EOI to both pics */
	IO_Write(0xa0,0x20);
	IO_Write(0x20,0x20);
	return 0;
}


static Bitu INT1A_Handler(void) {
	switch (reg_ah) {
	case 0x00:	/* Get System time */
		{
			Bit32u ticks=mem_readd(BIOS_TIMER);
			reg_al=0;		/* Midnight never passes :) */
			reg_cx=(Bit16u)(ticks >> 16);
			reg_dx=(Bit16u)(ticks & 0xffff);
			break;
		}
	case 0x01:	/* Set System time */
		mem_writed(BIOS_TIMER,(reg_cx<<16)|reg_dx);
		break;
	case 0x02:	/* GET REAL-TIME CLOCK TIME (AT,XT286,PS) */
		IO_Write(0x70,0x04);		//Hours
		reg_ch=IO_Read(0x71);
		IO_Write(0x70,0x02);		//Minutes
		reg_cl=IO_Read(0x71);
		IO_Write(0x70,0x00);		//Seconds
		reg_dh=IO_Read(0x71);
		reg_dl=0;			//Daylight saving disabled
		CALLBACK_SCF(false);
		break;
	case 0x04:	/* GET REAL-TIME ClOCK DATE  (AT,XT286,PS) */
		IO_Write(0x70,0x32);		//Centuries
		reg_ch=IO_Read(0x71);
		IO_Write(0x70,0x09);		//Years
		reg_cl=IO_Read(0x71);
		IO_Write(0x70,0x08);		//Months
		reg_dh=IO_Read(0x71);
		IO_Write(0x70,0x07);		//Days
		reg_dl=IO_Read(0x71);
		CALLBACK_SCF(false);
		break;



	case 0xb1:		/* PCI Bios Calls */
		LOG(LOG_BIOS,LOG_ERROR)("INT1A:PCI bios call %2X",reg_al);
		CALLBACK_SCF(true);
		break;
	default:
		LOG(LOG_BIOS,LOG_ERROR)("INT1A:Undefined call %2X",reg_ah);
	}
	return CBRET_NONE;
}	

static Bitu INT11_Handler(void) {
	reg_ax=mem_readw(BIOS_CONFIGURATION);
	return CBRET_NONE;
}
/* 
 * Define the following define to 1 if you want dosbox to check 
 * the system time every 5 seconds and adjust 1/2 a second to sync them.
 */
#ifndef DOSBOX_CLOCKSYNC
#define DOSBOX_CLOCKSYNC 0
#endif
static Bitu INT8_Handler(void) {
	/* Increase the bios tick counter */
	Bit32u value = mem_readd(BIOS_TIMER) + 1;
#if DOSBOX_CLOCKSYNC
	static bool check = false;
	if((value %50)==0) {
		if(((value %100)==0) && check) {
			check = false;
			time_t curtime;struct tm *loctime;
			curtime = time (NULL);loctime = localtime (&curtime);
			Bit32u ticksnu = (Bit32u)((loctime->tm_hour*3600+loctime->tm_min*60+loctime->tm_sec)*(float)PIT_TICK_RATE/65536.0);
			Bit32s bios = value;Bit32s tn = ticksnu;
			Bit32s diff = tn - bios;
			if(diff>0) {
				if(diff < 18) { diff  = 0; } else diff = 9;
			} else {
				if(diff > -18) { diff = 0; } else diff = -9;
			}
	     
			value += diff;
		} else if((value%100)==50) check = true;
	}
#endif
	mem_writed(BIOS_TIMER,value);

	/* decrease floppy motor timer */
	Bit8u val = mem_readb(BIOS_DISK_MOTOR_TIMEOUT);
	if (val) mem_writeb(BIOS_DISK_MOTOR_TIMEOUT,val-1);
	/* and running drive */
	mem_writeb(BIOS_DRIVE_RUNNING,mem_readb(BIOS_DRIVE_RUNNING) & 0xF0);
	return CBRET_NONE;
}
#undef DOSBOX_CLOCKSYNC

static Bitu INT1C_Handler(void) {
	return CBRET_NONE;
}

static Bitu INT12_Handler(void) {
	reg_ax=mem_readw(BIOS_MEMORY_SIZE);
	return CBRET_NONE;
}



static Bit8u INT14_Wait(Bit16u port, Bit8u mask, Bit8u timeout) {
	double starttime = PIC_FullIndex();
	double timeout_f = timeout * 1000.0;
	Bit8u retval;
	while (((retval = IO_ReadB(port)) & mask) != mask) {
		if (starttime < (PIC_FullIndex() - timeout_f)) {
			retval |= 0x80;
			break;
		}
		CALLBACK_Idle();
	}
	return retval;
}



static Bitu INT15_Handler(void) {
	static Bit16u biosConfigSeg=0;
	switch (reg_ah) {
	case 0x06:
		LOG(LOG_BIOS,LOG_NORMAL)("INT15 Unkown Function 6");
		break;
	case 0xC0:	/* Get Configuration*/
		{
			if (biosConfigSeg==0) biosConfigSeg = DOS_GetMemory(1); //We have 16 bytes
			PhysPt data	= PhysMake(biosConfigSeg,0);
			mem_writew(data,8);						// 8 Bytes following
			
				mem_writeb(data+2,0xFC);					// Model ID (PC)
				mem_writeb(data+3,0x00);					// Submodel ID
				mem_writeb(data+4,0x01);					// Bios Revision
				mem_writeb(data+5,(1<<6)|(1<<5)|(1<<4));	// Feature Byte 1
			
			mem_writeb(data+6,(1<<6));				// Feature Byte 2
			mem_writeb(data+7,0);					// Feature Byte 3
			mem_writeb(data+8,0);					// Feature Byte 4
			mem_writeb(data+9,0);					// Feature Byte 5
			CPU_SetSegGeneral(es,biosConfigSeg);
			reg_bx = 0;
			reg_ah = 0;
			CALLBACK_SCF(false);
		}; break;
	case 0x4f:	/* BIOS - Keyboard intercept */
		/* Carry should be set but let's just set it just in case */
		CALLBACK_SCF(true);
		break;
	case 0x83:	/* BIOS - SET EVENT WAIT INTERVAL */
		{
			if(reg_al == 0x01) { /* Cancel it */
				mem_writeb(BIOS_WAIT_FLAG_ACTIVE,0);
				IO_Write(0x70,0xb);
				IO_Write(0x71,IO_Read(0x71)&~0x40);
				CALLBACK_SCF(false);
				break;
			}
			if (mem_readb(BIOS_WAIT_FLAG_ACTIVE)) {
				reg_ah=0x80;
				CALLBACK_SCF(true);
				break;
			}
			Bit32u count=(reg_cx<<16)|reg_dx;
			mem_writed(BIOS_WAIT_FLAG_POINTER,RealMake(SegValue(es),reg_bx));
			mem_writed(BIOS_WAIT_FLAG_COUNT,count);
			mem_writeb(BIOS_WAIT_FLAG_ACTIVE,1);
			/* Reprogram RTC to start */
			IO_Write(0x70,0xb);
			IO_Write(0x71,IO_Read(0x71)|0x40);
			CALLBACK_SCF(false);
		}
		break;
	case 0x84:	/* BIOS - JOYSTICK SUPPORT (XT after 11/8/82,AT,XT286,PS) */
		if (reg_dx == 0x0000) {
			// Get Joystick button status
			if (JOYSTICK_IsEnabled(0) || JOYSTICK_IsEnabled(1)) {
				reg_al = IO_ReadB(0x201)&0xf0;
				CALLBACK_SCF(false);
			} else {
				// dos values
				reg_ax = 0x00f0; reg_dx = 0x0201;
				CALLBACK_SCF(true);
			}
		} else if (reg_dx == 0x0001) {
			if (JOYSTICK_IsEnabled(0)) {
				reg_ax = (Bit16u)(JOYSTICK_GetMove_X(0)*127+128);
				reg_bx = (Bit16u)(JOYSTICK_GetMove_Y(0)*127+128);
				if(JOYSTICK_IsEnabled(1)) {
					reg_cx = (Bit16u)(JOYSTICK_GetMove_X(1)*127+128);
					reg_dx = (Bit16u)(JOYSTICK_GetMove_Y(1)*127+128);
				}
				else {
					reg_cx = reg_dx = 0;
				}
				CALLBACK_SCF(false);
			} else if (JOYSTICK_IsEnabled(1)) {
				reg_ax = reg_bx = 0;
				reg_cx = (Bit16u)(JOYSTICK_GetMove_X(1)*127+128);
				reg_dx = (Bit16u)(JOYSTICK_GetMove_Y(1)*127+128);
				CALLBACK_SCF(false);
			} else {			
				reg_ax = reg_bx = reg_cx = reg_dx = 0;
				CALLBACK_SCF(true);
			}
		} else {
			LOG(LOG_BIOS,LOG_ERROR)("INT15:84:Unknown Bios Joystick functionality.");
		}
		break;
	case 0x86:	/* BIOS - WAIT (AT,PS) */
		{
			if (mem_readb(BIOS_WAIT_FLAG_ACTIVE)) {
				reg_ah=0x83;
				CALLBACK_SCF(true);
				break;
			}
			Bit32u count=(reg_cx<<16)|reg_dx;
			mem_writed(BIOS_WAIT_FLAG_POINTER,RealMake(0,BIOS_WAIT_FLAG_TEMP));
			mem_writed(BIOS_WAIT_FLAG_COUNT,count);
			mem_writeb(BIOS_WAIT_FLAG_ACTIVE,1);
			/* Reprogram RTC to start */
			IO_Write(0x70,0xb);
			IO_Write(0x71,IO_Read(0x71)|0x40);
			while (mem_readd(BIOS_WAIT_FLAG_COUNT)) {
				CALLBACK_Idle();
			}
			CALLBACK_SCF(false);
		}
	case 0x87:	/* Copy extended memory */
		{
			bool enabled = MEM_A20_Enabled();
			MEM_A20_Enable(true);
			Bitu   bytes	= reg_cx * 2;
			PhysPt data		= SegPhys(es)+reg_si;
			PhysPt source	= (mem_readd(data+0x12) & 0x00FFFFFF) + (mem_readb(data+0x16)<<24);
			PhysPt dest		= (mem_readd(data+0x1A) & 0x00FFFFFF) + (mem_readb(data+0x1E)<<24);
			MEM_BlockCopy(dest,source,bytes);
			reg_ax = 0x00;
			MEM_A20_Enable(enabled);
			CALLBACK_SCF(false);
			break;
		}	
	case 0x88:	/* SYSTEM - GET EXTENDED MEMORY SIZE (286+) */
		reg_ax=other_memsystems?0:size_extended;
		LOG(LOG_BIOS,LOG_NORMAL)("INT15:Function 0x88 Remaining %04X kb",reg_ax);
		CALLBACK_SCF(false);
		break;
	case 0x89:	/* SYSTEM - SWITCH TO PROTECTED MODE */
		{
			IO_Write(0x20,0x10);IO_Write(0x21,reg_bh);IO_Write(0x21,0);
			IO_Write(0xA0,0x10);IO_Write(0xA1,reg_bl);IO_Write(0xA1,0);
			MEM_A20_Enable(true);
			PhysPt table=SegPhys(es)+reg_si;
			CPU_LGDT(mem_readw(table+0x8),mem_readd(table+0x8+0x2) & 0xFFFFFF);
			CPU_LIDT(mem_readw(table+0x10),mem_readd(table+0x10+0x2) & 0xFFFFFF);
			CPU_SET_CRX(0,CPU_GET_CRX(0)|1);
			CPU_SetSegGeneral(ds,0x18);
			CPU_SetSegGeneral(es,0x20);
			CPU_SetSegGeneral(ss,0x28);
			reg_sp+=6;			//Clear stack of interrupt frame
			CPU_SetFlags(0,FMASK_ALL);
			reg_ax=0;
			CPU_JMP(false,0x30,reg_cx,0);
		}
		break;
	case 0x90:	/* OS HOOK - DEVICE BUSY */
		CALLBACK_SCF(false);
		reg_ah=0;
		break;
	case 0x91:	/* OS HOOK - DEVICE POST */
		CALLBACK_SCF(false);
		reg_ah=0;
		break;
	case 0xc2:	/* BIOS PS2 Pointing Device Support */
		switch (reg_al) {
		case 0x00:		// enable/disable
			if (reg_bh==0) {	// disable
				Mouse_SetPS2State(false);
				reg_ah=0;
				CALLBACK_SCF(false);
			} else if (reg_bh==0x01) {	//enable
				if (!Mouse_SetPS2State(true)) {
					reg_ah=5;
					CALLBACK_SCF(true);
					break;
				}
				reg_ah=0;
				CALLBACK_SCF(false);
			} else {
				CALLBACK_SCF(true);
				reg_ah=1;
			}
			break;
		case 0x01:		// reset
			reg_bx=0x00aa;	// mouse
			// fall through
		case 0x05:		// initialize
			Mouse_SetPS2State(false);
			CALLBACK_SCF(false);
			reg_ah=0;
			break;
		case 0x02:		// set sampling rate
		case 0x03:		// set resolution
			CALLBACK_SCF(false);
			reg_ah=0;
			break;
		case 0x04:		// get type
			reg_bh=0;	// ID
			CALLBACK_SCF(false);
			reg_ah=0;
			break;
		case 0x06:		// extended commands
			if ((reg_bh==0x01) || (reg_bh==0x02)) {
				CALLBACK_SCF(false); 
				reg_ah=0;
			} else {
				CALLBACK_SCF(true);
				reg_ah=1;
			}
			break;
		case 0x07:		// set callback
			Mouse_ChangePS2Callback(SegValue(es),reg_bx);
			CALLBACK_SCF(false);
			reg_ah=0;
			break;
		default:
			CALLBACK_SCF(true);
			reg_ah=1;
			break;
		}
		break;
	case 0xc3:      /* set carry flag so BorlandRTM doesn't assume a VECTRA/PS2 */
		reg_ah=0x86;
		CALLBACK_SCF(true);
		break;
	case 0xc4:	/* BIOS POS Programm option Select */
		LOG(LOG_BIOS,LOG_NORMAL)("INT15:Function %X called, bios mouse not supported",reg_ah);
		CALLBACK_SCF(true);
		break;
	default:
		LOG(LOG_BIOS,LOG_ERROR)("INT15:Unknown call %4X",reg_ax);
		reg_ah=0x86;
		CALLBACK_SCF(true);
		if ((IS_EGAVGA_ARCH) || (machine==MCH_CGA)) {
			/* relict from comparisons, as int15 exits with a retf2 instead of an iret */
			CALLBACK_SZF(false);
		}
	}
	return CBRET_NONE;
}

static Bitu Reboot_Handler(void) {
	// switch to text mode, notify user (let's hope INT10 still works)
	const char* const text = "\n\n   Reboot requested, quitting now.";
	reg_ax = 0;
	CALLBACK_RunRealInt(0x10);
	reg_ah = 0xe;
	reg_bx = 0;
	for(Bitu i = 0; i < strlen(text);i++) {
		reg_al = text[i];
		CALLBACK_RunRealInt(0x10);
	}
	LOG_MSG(text);
	double start = PIC_FullIndex();
	while((PIC_FullIndex()-start)<3000) CALLBACK_Idle();
	throw 1;
	return CBRET_NONE;
}

void BIOS_ZeroExtendedSize(bool in) {
	if(in) other_memsystems++; 
	else other_memsystems--;
	if(other_memsystems < 0) other_memsystems=0;
}

void BIOS_SetupKeyboard(void);
void BIOS_SetupDisks(void);

class BIOS:public Module_base{
private:
	CALLBACK_HandlerObject callback[11];
public:
	BIOS(Section* configuration):Module_base(configuration){
		/* tandy DAC can be requested in tandy_sound.cpp by initializing this field */

		/* Clear the Bios Data Area (0x400-0x5ff, 0x600- is accounted to DOS) */
		for (Bit16u i=0;i<0x200;i++) real_writeb(0x40,i,0);

		/* Setup all the interrupt handlers the bios controls */

		/* INT 8 Clock IRQ Handler */
		Bitu call_irq0=CALLBACK_Allocate();	
		CALLBACK_Setup(call_irq0,INT8_Handler,CB_IRQ0,Real2Phys(BIOS_DEFAULT_IRQ0_LOCATION),"IRQ 0 Clock");
		RealSetVec(0x08,BIOS_DEFAULT_IRQ0_LOCATION);
		// pseudocode for CB_IRQ0:
		//	callback INT8_Handler
		//	push ax,dx,ds
		//	int 0x1c
		//	cli
		//	pop ds,dx
		//	mov al, 0x20
		//	out 0x20, al
		//	pop ax
		//	iret

		mem_writed(BIOS_TIMER,0);			//Calculate the correct time

		/* INT 11 Get equipment list */
		callback[1].Install(&INT11_Handler,CB_IRET,"Int 11 Equipment");
		callback[1].Set_RealVec(0x11);

		/* INT 12 Memory Size default at 640 kb */
		callback[2].Install(&INT12_Handler,CB_IRET,"Int 12 Memory");
		callback[2].Set_RealVec(0x12);
		mem_writew(BIOS_MEMORY_SIZE,640);
		
		/* INT 13 Bios Disk Support */
		BIOS_SetupDisks();

		/* INT 14 Serial Ports */
		//callback[3].Install(&INT14_Handler,CB_IRET_STI,"Int 14 COM-port");
		//callback[3].Set_RealVec(0x14);

		/* INT 15 Misc Calls */
		callback[4].Install(&INT15_Handler,CB_IRET,"Int 15 Bios");
		callback[4].Set_RealVec(0x15);

		/* INT 16 Keyboard handled in another file */
		BIOS_SetupKeyboard();

		/* INT 17 Printer Routines */
		//callback[5].Install(&INT17_Handler,CB_IRET_STI,"Int 17 Printer");
		//callback[5].Set_RealVec(0x17);

		/* INT 1A TIME and some other functions */
		callback[6].Install(&INT1A_Handler,CB_IRET_STI,"Int 1a Time");
		callback[6].Set_RealVec(0x1A);

		/* INT 1C System Timer tick called from INT 8 */
		callback[7].Install(&INT1C_Handler,CB_IRET,"Int 1c Timer");
		callback[7].Set_RealVec(0x1C);
		
		/* IRQ 8 RTC Handler */
		callback[8].Install(&INT70_Handler,CB_IRET,"Int 70 RTC");
		callback[8].Set_RealVec(0x70);

		/* Irq 9 rerouted to irq 2 */
		callback[9].Install(NULL,CB_IRQ9,"irq 9 bios");
		callback[9].Set_RealVec(0x71);

		/* Reboot */
		callback[10].Install(&Reboot_Handler,CB_IRET,"reboot");
		callback[10].Set_RealVec(0x18);
		RealPt rptr = callback[10].Get_RealPointer();
		RealSetVec(0x19,rptr);
		// set system BIOS entry point too
		phys_writeb(0xFFFF0,0xEA);	// FARJMP
		phys_writew(0xFFFF1,RealOff(rptr));	// offset
		phys_writew(0xFFFF3,RealSeg(rptr));	// segment

		/* Irq 2 */
		Bitu call_irq2=CALLBACK_Allocate();	
		CALLBACK_Setup(call_irq2,NULL,CB_IRET_EOI_PIC1,Real2Phys(BIOS_DEFAULT_IRQ2_LOCATION),"irq 2 bios");
		RealSetVec(0x0a,BIOS_DEFAULT_IRQ2_LOCATION);

		/* Some hardcoded vectors */
		phys_writeb(Real2Phys(BIOS_DEFAULT_HANDLER_LOCATION),0xcf);	/* bios default interrupt vector location -> IRET */
		phys_writew(Real2Phys(RealGetVec(0x12))+0x12,0x20); //Hack for Jurresic

		phys_writeb(0xffffe,0xfc);	/* PC */

		// System BIOS identification
		const char* const b_type =
			"IBM COMPATIBLE 486 BIOS COPYRIGHT The DOSBox Team.";
		for(Bitu i = 0; i < strlen(b_type); i++) phys_writeb(0xfe00e + i,b_type[i]);
		
		// System BIOS version
		const char* const b_vers =
			"DOSBox FakeBIOS v1.0";
		for(Bitu i = 0; i < strlen(b_vers); i++) phys_writeb(0xfe061+i,b_vers[i]);

		// write system BIOS date
		const char* const b_date = "01/01/92";
		for(Bitu i = 0; i < strlen(b_date); i++) phys_writeb(0xffff5+i,b_date[i]);
		phys_writeb(0xfffff,0x55); // signature

		
		
	
		/* Setup some stuff in 0x40 bios segment */
		
		// port timeouts
		// always 1 second even if the port does not exist
		mem_writeb(BIOS_LPT1_TIMEOUT,1);
		mem_writeb(BIOS_LPT2_TIMEOUT,1);
		mem_writeb(BIOS_LPT3_TIMEOUT,1);
		mem_writeb(BIOS_COM1_TIMEOUT,1);
		mem_writeb(BIOS_COM2_TIMEOUT,1);
		mem_writeb(BIOS_COM3_TIMEOUT,1);
		mem_writeb(BIOS_COM4_TIMEOUT,1);
		
		/* detect parallel ports */
		Bitu ppindex=0; // number of lpt ports
		if ((IO_Read(0x378)!=0xff)|(IO_Read(0x379)!=0xff)) {
			// this is our LPT1
			mem_writew(BIOS_ADDRESS_LPT1,0x378);
			ppindex++;
			if((IO_Read(0x278)!=0xff)|(IO_Read(0x279)!=0xff)) {
				// this is our LPT2
				mem_writew(BIOS_ADDRESS_LPT2,0x278);
				ppindex++;
				if((IO_Read(0x3bc)!=0xff)|(IO_Read(0x3be)!=0xff)) {
					// this is our LPT3
					mem_writew(BIOS_ADDRESS_LPT3,0x3bc);
					ppindex++;
				}
			} else if((IO_Read(0x3bc)!=0xff)|(IO_Read(0x3be)!=0xff)) {
				// this is our LPT2
				mem_writew(BIOS_ADDRESS_LPT2,0x3bc);
				ppindex++;
			}
		} else if((IO_Read(0x3bc)!=0xff)|(IO_Read(0x3be)!=0xff)) {
			// this is our LPT1
			mem_writew(BIOS_ADDRESS_LPT1,0x3bc);
			ppindex++;
			if((IO_Read(0x278)!=0xff)|(IO_Read(0x279)!=0xff)) {
				// this is our LPT2
				mem_writew(BIOS_ADDRESS_LPT2,0x278);
				ppindex++;
			}
		} else if((IO_Read(0x278)!=0xff)|(IO_Read(0x279)!=0xff)) {
			// this is our LPT1
			mem_writew(BIOS_ADDRESS_LPT1,0x278);
			ppindex++;
		}

		/* Setup equipment list */
		// look http://www.bioscentral.com/misc/bda.htm
		
		//Bit16u config=0x4400;	//1 Floppy, 2 serial and 1 parallel 
		Bit16u config = 0x0;
		
		// set number of parallel ports
		// if(ppindex == 0) config |= 0x8000; // looks like 0 ports are not specified
		//else if(ppindex == 1) config |= 0x0000;
		if(ppindex == 2) config |= 0x4000;
		else config |= 0xc000;	// 3 ports
#if (C_FPU)
		//FPU
		config|=0x2;
#endif
		switch (machine) {
		case MCH_HERC:
			//Startup monochrome
			config|=0x30;
			break;
		case EGAVGA_ARCH_CASE:
		case MCH_CGA:

		default:
			//EGA VGA
			config|=0;
			break;
		}
		// PS2 mouse
		config |= 0x04;
		// Gameport
		config |= 0x1000;
		mem_writew(BIOS_CONFIGURATION,config);
		CMOS_SetRegister(0x14,(Bit8u)(config&0xff)); //Should be updated on changes
		/* Setup extended memory size */
		IO_Write(0x70,0x30);
		size_extended=IO_Read(0x71);
		IO_Write(0x70,0x31);
		size_extended|=(IO_Read(0x71) << 8);
	}
	~BIOS(){
		/* abort DAC playing */

		real_writeb(0x40,0xd4,0x00);

	}
};

// set com port data in bios data area
// parameter: array of 4 com port base addresses, 0 = none
void BIOS_SetComPorts(Bit16u baseaddr[]) {
	Bit16u portcount=0;
	Bit16u equipmentword;
	for(Bitu i = 0; i < 4; i++) {
		if(baseaddr[i]!=0) portcount++;
		if(i==0)		mem_writew(BIOS_BASE_ADDRESS_COM1,baseaddr[i]);
		else if(i==1)	mem_writew(BIOS_BASE_ADDRESS_COM2,baseaddr[i]);
		else if(i==2)	mem_writew(BIOS_BASE_ADDRESS_COM3,baseaddr[i]);
		else			mem_writew(BIOS_BASE_ADDRESS_COM4,baseaddr[i]);
	}
	// set equipment word
	equipmentword = mem_readw(BIOS_CONFIGURATION);
	equipmentword &= (~0x0E00);
	equipmentword |= (portcount << 9);
	mem_writew(BIOS_CONFIGURATION,equipmentword);
	CMOS_SetRegister(0x14,(Bit8u)(equipmentword&0xff)); //Should be updated on changes
}


static BIOS* test;

void BIOS_Destroy(Section* /*sec*/){
	delete test;
}

void BIOS_Init(Section* sec) {
	test = new BIOS(sec);
	sec->AddDestroyFunction(&BIOS_Destroy,false);
}
