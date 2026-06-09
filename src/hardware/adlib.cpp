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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include "adlib.h"

#include "setup.h"
#include "mapper.h"
#include "mem.h"
#include "dbopl.h"
#include "cpu.h"

#define RAW_SIZE 1024

namespace Adlib {

/* Raw DRO capture stuff */

#ifdef _MSC_VER
#pragma pack (1)
#endif

#define HW_OPL2 0
#define HW_DUALOPL2 1
#define HW_OPL3 2

struct RawHeader {
    Bit8u id[8];                /* 0x00, "DBRAWOPL" */
    Bit16u versionHigh;         /* 0x08, size of the data following the m */
    Bit16u versionLow;          /* 0x0a, size of the data following the m */
    Bit32u commands;            /* 0x0c, Bit32u amount of command/data pairs */
    Bit32u milliseconds;        /* 0x10, Bit32u Total milliseconds of data in this chunk */
    Bit8u hardware;             /* 0x14, Bit8u Hardware Type 0=opl2,1=dual-opl2,2=opl3 */
    Bit8u format;               /* 0x15, Bit8u Format 0=cmd/data interleaved, 1 maybe all cdms, followed by all data */
    Bit8u compression;          /* 0x16, Bit8u Compression Type, 0 = No Compression */
    Bit8u delay256;             /* 0x17, Bit8u Delay 1-256 msec command */
    Bit8u delayShift8;          /* 0x18, Bit8u (delay + 1)*256 */            
    Bit8u conversionTableSize;  /* 0x191, Bit8u Raw Conversion Table size */
} GCC_ATTRIBUTE(packed);
#ifdef _MSC_VER
#pragma pack()
#endif

//Table to map the opl register to one <127 for dro saving
class Capture {
    //127 entries to go from raw data to registers
    Bit8u ToReg[127];
    //How many entries in the ToPort are used
    Bit8u RawUsed;
    //256 entries to go from port index to raw data
    Bit8u ToRaw[256];
    Bit8u delay256;
    Bit8u delayShift8;
    RawHeader header;

    FILE*   handle;             //File used for writing
    Bit32u  startTicks;         //Start used to check total raw length on end
    Bit32u  lastTicks;          //Last ticks when last last cmd was added
    Bit8u   buf[1024];          //16 added for delay commands and what not
    Bit32u  bufUsed;
    Bit8u   cmd[2];             //Last cmd's sent to either ports
    bool    doneOpl3;
    bool    doneDualOpl2;

    RegisterCache* cache;

    void MakeEntry( Bit8u reg, Bit8u& raw ) {
        ToReg[ raw ] = reg;
        ToRaw[ reg ] = raw;
        raw++;
    }
    void MakeTables( void ) {
        Bit8u index = 0;
        memset( ToReg, 0xff, sizeof ( ToReg ) );
        memset( ToRaw, 0xff, sizeof ( ToRaw ) );
        //Select the entries that are valid and the index is the mapping to the index entry
        MakeEntry( 0x01, index );                   //0x01: Waveform select
        MakeEntry( 0x04, index );                   //104: Four-Operator Enable
        MakeEntry( 0x05, index );                   //105: OPL3 Mode Enable
        MakeEntry( 0x08, index );                   //08: CSW / NOTE-SEL
        MakeEntry( 0xbd, index );                   //BD: Tremolo Depth / Vibrato Depth / Percussion Mode / BD/SD/TT/CY/HH On
        //Add the 32 byte range that hold the 18 operators
        for ( int i = 0 ; i < 24; i++ ) {
            if ( (i & 7) < 6 ) {
                MakeEntry(0x20 + i, index );        //20-35: Tremolo / Vibrato / Sustain / KSR / Frequency Multiplication Facto
                MakeEntry(0x40 + i, index );        //40-55: Key Scale Level / Output Level 
                MakeEntry(0x60 + i, index );        //60-75: Attack Rate / Decay Rate 
                MakeEntry(0x80 + i, index );        //80-95: Sustain Level / Release Rate
                MakeEntry(0xe0 + i, index );        //E0-F5: Waveform Select
            }
        }
        //Add the 9 byte range that hold the 9 channels
        for ( int i = 0 ; i < 9; i++ ) {
            MakeEntry(0xa0 + i, index );            //A0-A8: Frequency Number
            MakeEntry(0xb0 + i, index );            //B0-B8: Key On / Block Number / F-Number(hi bits) 
            MakeEntry(0xc0 + i, index );            //C0-C8: FeedBack Modulation Factor / Synthesis Type
        }
        //Store the amount of bytes the table contains
        RawUsed = index;
        delay256 = RawUsed;
        delayShift8 = RawUsed+1; 
    }

    void ClearBuf( void ) {
        fwrite( buf, 1, bufUsed, handle );
        header.commands += bufUsed / 2;
        bufUsed = 0;
    }
    void AddBuf( Bit8u raw, Bit8u val ) {
        buf[bufUsed++] = raw;
        buf[bufUsed++] = val;
        if ( bufUsed >= sizeof( buf ) ) {
            ClearBuf();
        }
    }
    void AddWrite( Bit32u regFull, Bit8u val ) {
        Bit8u regMask = regFull & 0xff;
        if ( header.hardware != HW_OPL3 && regFull == 0x104 && val && (*cache)[0x105] ) {
            header.hardware = HW_OPL3;
        } 
        if ( header.hardware == HW_OPL2 && regFull >= 0x1b0 && regFull <=0x1b8 && val ) {
            header.hardware = HW_DUALOPL2;
        }
        Bit8u raw = ToRaw[ regMask ];
        if ( raw == 0xff )
            return;
        if ( regFull & 0x100 )
            raw |= 128;
        AddBuf( raw, val );
    }
    void WriteCache( void  ) {
        Bitu i, val;
        for (i = 0;i < 256;i++) {
            val = (*cache)[ i ];
            if (i >= 0xb0 && i <= 0xb8) {
                val &= ~0x20;
            }
            if (i == 0xbd) {
                val &= ~0x1f;
            }
            if (val) {
                AddWrite( i, val );
            }
            val = (*cache)[ 0x100 + i ];
            if (i >= 0xb0 && i <= 0xb8) {
                val &= ~0x20;
            }
            if (val) {
                AddWrite( 0x100 + i, val );
            }
        }
    }
    void InitHeader( void ) {
        memset( &header, 0, sizeof( header ) );
        memcpy( header.id, "DBRAWOPL", 8 );
        header.versionLow = 0;
        header.versionHigh = 2;
        header.delay256 = delay256;
        header.delayShift8 = delayShift8;
        header.conversionTableSize = RawUsed;
    }
    void CloseFile( void ) {
        if ( handle ) {
            ClearBuf();
            var_write( &header.versionHigh, header.versionHigh );
            var_write( &header.versionLow, header.versionLow );
            var_write( &header.commands, header.commands );
            var_write( &header.milliseconds, header.milliseconds );
            fseek( handle, 0, SEEK_SET );
            fwrite( &header, 1, sizeof( header ), handle );
            fclose( handle );
            handle = 0;
        }
    }
public:
    bool DoWrite( Bit32u regFull, Bit8u val ) {
        Bit8u regMask = regFull & 0xff;
        if ( handle ) {
            Bit8u raw = ToRaw[ regMask ];
            if ( raw == 0xff ) {
                return true;
            }
            if ( (*cache)[ regFull ] == val )
                return true;
            Bitu passed = PIC_Ticks - lastTicks;
            lastTicks = PIC_Ticks;
            header.milliseconds += passed;
            if ( passed > 30000 ) {
                CloseFile();
                goto skipWrite; 
            }
            while (passed > 0) {
                if (passed < 257) {
                    AddBuf( delay256, passed - 1 );
                    passed = 0;
                } else {
                    Bitu shift = (passed >> 8);
                    passed -= shift << 8;
                    AddBuf( delayShift8, shift - 1 );
                }
            }
            AddWrite( regFull, val );
            return true;
        }
skipWrite:
        if ( !(
            ( regMask>=0xb0 && regMask<=0xb8 && (val&0x020) ) ||
            ( regMask == 0xbd && ( (val&0x3f) > 0x20 ) )
        )) {
            return true;
        }
        handle = OpenCaptureFile("Raw Opl",".dro");
        if (!handle)
            return false;
        InitHeader();
        fwrite( &header, 1, sizeof(header), handle );
        fwrite( &ToReg, 1, RawUsed, handle );
        WriteCache( );
        AddWrite( regFull, val );
        lastTicks = PIC_Ticks;  
        startTicks = PIC_Ticks;
        return true;
    }
    Capture( RegisterCache* _cache ) {
        cache = _cache;
        handle = 0;
        bufUsed = 0;
        MakeTables();
    }
    ~Capture() {
        CloseFile();
    }

};

/*
Chip
*/

Chip::Chip() : timer0(80), timer1(320) {
}

bool Chip::Write( Bit32u reg, Bit8u val ) {
    switch ( reg ) {
    case 0x02:
        timer0.Update(PIC_FullIndex() );
        timer0.SetCounter(val);
        return true;
    case 0x03:
        timer1.Update(PIC_FullIndex());
        timer1.SetCounter(val);
        return true;
    case 0x04:
        if ( val & 0x80 ) {
            timer0.Reset();
            timer1.Reset();
        } else {
            const double time = PIC_FullIndex();
            if (val & 0x1) {
                timer0.Start(time);
            }
            else {
                timer0.Stop();
            }
            if (val & 0x2) {
                timer1.Start(time);
            }
            else {
                timer1.Stop();
            }
            timer0.SetMask((val & 0x40) > 0);
            timer1.SetMask((val & 0x20) > 0);
        }
        return true;
    }
    return false;
}

Bit8u Chip::Read( ) {
    const double time( PIC_FullIndex() );
    Bit8u ret = 0;
    if (timer0.Update(time)) {
        ret |= 0x40;
        ret |= 0x80;
    }
    if (timer1.Update(time)) {
        ret |= 0x20;
        ret |= 0x80;
    }
    return ret;
}

void Module::CacheWrite( Bit32u reg, Bit8u val ) {
    if ( capture ) {
        capture->DoWrite( reg, val );
    }
    cache[ reg ] = val;
}

void Module::DualWrite( Bit8u index, Bit8u reg, Bit8u val ) {
    if ( reg == 5 ) {
        return;
    }
    if ( reg >= 0xE0 ) {
        val &= 3;
    } 
    if ( chip[index].Write( reg, val ) ) 
        return;
    if ( reg >= 0xc0 && reg <=0xc8 ) {
        val &= 0x0f;
        val |= index ? 0xA0 : 0x50;
    }
    Bit32u fullReg = reg + (index ? 0x100 : 0);
    handler->WriteReg( fullReg, val );
    CacheWrite( fullReg, val );
}

void Module::CtrlWrite( Bit8u val ) {
    switch ( ctrl.index ) {
    case 0x09:
        ctrl.lvol = val;
        goto setvol;
    case 0x0a:
        ctrl.rvol = val;
setvol:
        if ( ctrl.mixer ) {
            mixerChan->SetVolume( (float)(ctrl.lvol&0x1f)/31.0f, (float)(ctrl.rvol&0x1f)/31.0f );
        }
        break;
    }
}

Bitu Module::CtrlRead( void ) {
    switch ( ctrl.index ) {
    case 0x00:
        return 0x70;
    case 0x09:
        return ctrl.lvol;
    case 0x0a:
        return ctrl.rvol;
    case 0x15:
        return 0x388 >> 3;
    }
    return 0xff;
}

void Module::PortWrite( Bitu port, Bitu val, Bitu /*iolen*/ ) {
    lastUsed = PIC_Ticks;
    if ( !mixerChan->enabled ) {
        mixerChan->Enable(true);
    }
    if ( port&1 ) {
        switch ( mode ) {
        case MODE_OPL3GOLD:
            if ( port == 0x38b ) {
                if ( ctrl.active ) {
                    CtrlWrite( val );
                    break;
                }
            }
        case MODE_OPL2:
        case MODE_OPL3:
            if ( !chip[0].Write( reg.normal, val ) ) {
                handler->WriteReg( reg.normal, val );
                CacheWrite( reg.normal, val );
            }
            break;
        case MODE_DUALOPL2:
            if ( !(port & 0x8) ) {
                Bit8u index = ( port & 2 ) >> 1;
                DualWrite( index, reg.dual[index], val );
            } else {
                DualWrite( 0, reg.dual[0], val );
                DualWrite( 1, reg.dual[1], val );
            }
            break;
        }
    } else {
        switch ( mode ) {
        case MODE_OPL2:
            reg.normal = handler->WriteAddr( port, val ) & 0xff;
            break;
        case MODE_OPL3GOLD:
            if ( port == 0x38a ) {
                if ( val == 0xff ) {
                    ctrl.active = true;
                    break;
                } else if ( val == 0xfe ) {
                    ctrl.active = false;
                    break;
                } else if ( ctrl.active ) {
                    ctrl.index = val & 0xff;
                    break;
                }
            }
        case MODE_OPL3:
            reg.normal = handler->WriteAddr( port, val ) & 0x1ff;
            break;
        case MODE_DUALOPL2:
            if ( !(port & 0x8) ) {
                Bit8u index = ( port & 2 ) >> 1;
                reg.dual[index] = val & 0xff;
            } else {
                reg.dual[0] = val & 0xff;
                reg.dual[1] = val & 0xff;
            }
            break;
        }
    }
}

Bitu Module::PortRead( Bitu port, Bitu /*iolen*/ ) {
    Bits delaycyc = (CPU_CycleMax/2048); 
    if(GCC_UNLIKELY(delaycyc > CPU_Cycles)) delaycyc = CPU_Cycles;
    CPU_Cycles -= delaycyc;
    CPU_IODelayRemoved += delaycyc;

    switch ( mode ) {
    case MODE_OPL2:
        if ( !(port & 3 ) ) {
            return chip[0].Read() | 0x6;
        } else {
            return 0xff;
        }
    case MODE_OPL3GOLD:
        if ( ctrl.active ) {
            if ( port == 0x38a ) {
                return 0;
            } else if ( port == 0x38b ) {
                return CtrlRead();
            }
        }
    case MODE_OPL3:
        if ( !(port & 3 ) ) {
            return chip[0].Read();
        } else {
            return 0xff;
        }
    case MODE_DUALOPL2:
        if ( port & 1 ) {
            return 0xff;
        }
        return chip[ (port >> 1) & 1].Read() | 0x6;
    }
    return 0;
}

void Module::Init( Mode m ) {
    mode = m;
    memset(cache, 0, sizeof(cache));
    switch ( mode ) {
    case MODE_OPL3:
    case MODE_OPL3GOLD:
    case MODE_OPL2:
        break;
    case MODE_DUALOPL2:
        handler->WriteReg( 0x105, 1 );
        CacheWrite( 0x105, 1 );
        break;
    }
}

}; //namespace

static Adlib::Module* module = 0;

static void OPL_CallBack(Bitu len) {
    module->handler->Generate( module->mixerChan, len );
    if ((PIC_Ticks - module->lastUsed) > 30000) {
        Bitu i;
        for (i=0xb0;i<0xb9;i++) if (module->cache[i]&0x20||module->cache[i+0x100]&0x20) break;
        if (i==0xb9) module->mixerChan->Enable(false);
        else module->lastUsed = PIC_Ticks;
    }
}

static Bitu OPL_Read(Bitu port,Bitu iolen) {
    return module->PortRead( port, iolen );
}

void OPL_Write(Bitu port,Bitu val,Bitu iolen) {
    module->PortWrite( port, val, iolen );
}

static void OPL_SaveRawEvent(bool pressed) {
    if (!pressed) return;
    if ( module->capture ) {
        delete module->capture;
        module->capture = 0;
    } else {
        module->capture = new Adlib::Capture( &module->cache );
    }
}

namespace Adlib {

// Usar o DBOPL::Handler existente - não precisamos criar nosso próprio handler!
// O DBOPL::Handler já tem implementação em dbopl.cpp

Module::Module( Section* configuration ) : Module_base(configuration) {
    reg.dual[0] = 0;
    reg.dual[1] = 0;
    reg.normal = 0;
    ctrl.active = false;
    ctrl.index = 0;
    ctrl.lvol = 0xff;
    ctrl.rvol = 0xff;
    handler = 0;
    capture = 0;

    Section_prop * section=static_cast<Section_prop *>(configuration);
    Bitu base = section->Get_hex("sbbase");
    Bitu rate = section->Get_int("oplrate");
    Bitu strength = section->Get_int("fmstrength");
    if ( rate < 8000 )
        rate = 8000;
    ctrl.mixer = section->Get_bool("sbmixer");

    mixerChan = mixerObject.Install(OPL_CallBack, rate, "FM");
    float scale = ((float)strength)/100.0;
    mixerChan->SetScale( scale );

    // Use o DBOPL::Handler existente
    const bool opl3Mode = (oplmode >= OPL_opl3);
    handler = new DBOPL::Handler(opl3Mode);
    handler->Init(rate);
    
    bool single = false;
    switch ( oplmode ) {
    case OPL_opl2:
        single = true;
        Init( Adlib::MODE_OPL2 );
        break;
    case OPL_dualopl2:
        Init( Adlib::MODE_DUALOPL2 );
        break;
    case OPL_opl3:
    case OPL_opl3gold:
        Init( Adlib::MODE_OPL3 );
        break;
    default:
        Init( Adlib::MODE_OPL2 );
        break;
    }
    
    WriteHandler[0].Install(0x388, OPL_Write, IO_MB, 4);
    ReadHandler[0].Install(0x388, OPL_Read, IO_MB, 4);
    
    if ( !single ) {
        WriteHandler[1].Install(base, OPL_Write, IO_MB, 4);
        ReadHandler[1].Install(base, OPL_Read, IO_MB, 4);
    }
    WriteHandler[2].Install(base+8, OPL_Write, IO_MB, 2);
    ReadHandler[2].Install(base+8, OPL_Read, IO_MB, 1);

    MAPPER_AddHandler(OPL_SaveRawEvent, MK_f7, MMOD1|MMOD2, "caprawopl", "Cap OPL");
}

Module::~Module() {
    if ( capture ) {
        delete capture;
    }
    if ( handler ) {
        delete handler;
    }
}

OPL_Mode Module::oplmode = OPL_none;

};  //Adlib Namespace

void OPL_Init(Section* sec, OPL_Mode oplmode) {
    Adlib::Module::oplmode = oplmode;
    module = new Adlib::Module( sec );
}

void OPL_ShutDown(Section* /*sec*/) {
    delete module;
    module = 0;
}