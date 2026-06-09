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
#include <string.h>
#include <sys/types.h>
#include <math.h>

#if defined (WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#include "mem.h"
#include "pic.h"
#include "dosbox.h"
#include "mixer.h"
#include "timer.h"
#include "setup.h"
#include "cross.h"
#include "support.h"
#include "mapper.h"
#include "hardware.h"
#include "programs.h"
#include "midi.h"

#define MIXER_SSIZE 4

#define MIXER_VOLSHIFT 13

#define FREQ_SHIFT 14
#define FREQ_NEXT ( 1 << FREQ_SHIFT)
#define FREQ_MASK ( FREQ_NEXT -1 )

#define TICK_SHIFT 24
#define TICK_NEXT ( 1 << TICK_SHIFT)
#define TICK_MASK (TICK_NEXT -1)

// WaveOut structures
static HWAVEOUT       g_hWaveOut = NULL;
static WAVEHDR        g_waveHeaders[4];
static BYTE*          g_pWaveBuffers[4];
static CRITICAL_SECTION g_csAudioLock;
static bool           g_bWaveOutActive = false;
static HANDLE         g_hMixerThread = NULL;
static DWORD          g_dwBufferSize = 0;

static INLINE Bit16s MIXER_CLIP(Bits SAMP) {
	if (SAMP < MAX_AUDIO) {
		if (SAMP > MIN_AUDIO)
			return SAMP;
		else return MIN_AUDIO;
	} else return MAX_AUDIO;
}

static bool pcSpeakerWarningShown = false;

static struct {
	Bit32s work[MIXER_BUFSIZE][2];
	Bitu pos,done;
	Bitu needed, min_needed, max_needed;
	Bit32u tick_add;
	Bit32u tick_counter;
	float mastervol[2];
	MixerChannel * channels;
	bool nosound;
	Bit32u freq;
	Bit32u blocksize;
} mixer;

Bit8u MixTemp[MIXER_BUFSIZE];

MixerChannel * MIXER_AddChannel(MIXER_Handler handler,Bitu freq,const char * name) {
	MixerChannel * chan=new MixerChannel();
	chan->scale = 1.0;
	chan->handler=handler;
	chan->name=name;
	chan->next=mixer.channels;
	chan->SetVolume(1,1);
	chan->enabled=false;
	chan->interpolate = false;
	chan->SetFreq(freq);
	chan->last_samples_were_silence = true;
	chan->last_samples_were_stereo = false;
	chan->offset[0] = 0;
	chan->offset[1] = 0;
	mixer.channels = chan;
	return chan;
}

MixerChannel * MIXER_FindChannel(const char * name) {
	MixerChannel * chan=mixer.channels;
	while (chan) {
		if (!strcasecmp(chan->name,name)) break;
		chan=chan->next;
	}
	return chan;
}

void MIXER_DelChannel(MixerChannel* delchan) {
	MixerChannel * chan=mixer.channels;
	MixerChannel * * where=&mixer.channels;
	while (chan) {
		if (chan==delchan) {
			*where=chan->next;
			delete delchan;
			return;
		}
		where=&chan->next;
		chan=chan->next;
	}
}

void MixerChannel::UpdateVolume(void) {
	volmul[0]=(Bits)((1 << MIXER_VOLSHIFT)*scale*volmain[0]*mixer.mastervol[0]);
	volmul[1]=(Bits)((1 << MIXER_VOLSHIFT)*scale*volmain[1]*mixer.mastervol[1]);
}

void MixerChannel::SetVolume(float _left,float _right) {
	volmain[0]=_left;
	volmain[1]=_right;
	UpdateVolume();
}

void MixerChannel::SetScale( float f ) {
	scale = f;
	UpdateVolume();
}

void MixerChannel::Enable(bool _yesno) {
	if (_yesno==enabled) return;
	enabled=_yesno;
	if (enabled) {
		freq_counter = 0;
		EnterCriticalSection(&g_csAudioLock);
		if (done<mixer.done) done=mixer.done;
		LeaveCriticalSection(&g_csAudioLock);
	}
}

void MixerChannel::SetFreq(Bitu freq) {
	freq_add=(freq<<FREQ_SHIFT)/mixer.freq;
	if (freq != mixer.freq) {
		interpolate = true;
	} else {
		interpolate = false;
	}
}

void MixerChannel::Mix(Bitu _needed) {
	needed = _needed;
	while (enabled && needed > done) {
		Bitu left = (needed - done);
		left *= freq_add;
		left = (left >> FREQ_SHIFT) + ((left & FREQ_MASK) != 0);
		handler(left);
	}
}

void MixerChannel::AddSilence(void) {
	if (done < needed) {
		if(prevSample[0] == 0 && prevSample[1] == 0) {
			done = needed;
			nextSample[0] = 0;
			nextSample[1] = 0;
			freq_counter = FREQ_NEXT;
		} else {
			bool stereo = last_samples_were_stereo;
			Bitu mixpos = mixer.pos + done;
			while (done < needed) {
				if (prevSample[0] > 4)       nextSample[0] = prevSample[0] - 4;
				else if (prevSample[0] < -4) nextSample[0] = prevSample[0] + 4;
				else nextSample[0] = 0;
				if (prevSample[1] > 4)       nextSample[1] = prevSample[1] - 4;
				else if (prevSample[1] < -4) nextSample[1] = prevSample[1] + 4;
				else nextSample[1] = 0;

				mixpos &= MIXER_BUFMASK;
				Bit32s* write = mixer.work[mixpos];

				write[0] += prevSample[0] * volmul[0];
				write[1] += (stereo ? prevSample[1] : prevSample[0]) * volmul[1];

				prevSample[0] = nextSample[0];
				prevSample[1] = nextSample[1];
				mixpos++;
				done++;
				freq_counter = FREQ_NEXT;
			}
		}
	}
	last_samples_were_silence = true;
	offset[0] = offset[1] = 0;
}

#define MIXER_UPRAMP_STEPS 0
#define MIXER_UPRAMP_SAVE 512

template<class Type,bool stereo,bool signeddata,bool nativeorder>
inline void MixerChannel::AddSamples(Bitu len, const Type* data) {
	last_samples_were_stereo = stereo;

	Bitu mixpos = mixer.pos + done;
	Bitu pos = 0;
	while (1) {
		while (freq_counter >= FREQ_NEXT) {
			if (pos >= len) {
				last_samples_were_silence = false;
				return;
			}
			freq_counter -= FREQ_NEXT;
			prevSample[0] = nextSample[0];
			if (stereo) {
				prevSample[1] = nextSample[1];
			}
			if ( sizeof( Type) == 1) {
				if (!signeddata) {
					if (stereo) {
						nextSample[0]=(((Bit8s)(data[pos*2+0] ^ 0x80)) << 8);
						nextSample[1]=(((Bit8s)(data[pos*2+1] ^ 0x80)) << 8);
					} else {
						nextSample[0]=(((Bit8s)(data[pos] ^ 0x80)) << 8);
					}
				} else {
					if (stereo) {
						nextSample[0]=(data[pos*2+0] << 8);
						nextSample[1]=(data[pos*2+1] << 8);
					} else {
						nextSample[0]=(data[pos] << 8);
					}
				}
			} else  {
				if (signeddata) {
					if (stereo) {
						if (nativeorder) {
							nextSample[0]=data[pos*2+0];
							nextSample[1]=data[pos*2+1];
						} else {
							if ( sizeof( Type) == 2) {
								nextSample[0]=(Bit16s)host_readw((HostPt)&data[pos*2+0]);
								nextSample[1]=(Bit16s)host_readw((HostPt)&data[pos*2+1]);
							} else {
								nextSample[0]=(Bit32s)host_readd((HostPt)&data[pos*2+0]);
								nextSample[1]=(Bit32s)host_readd((HostPt)&data[pos*2+1]);
							}
						}
					} else {
						if (nativeorder) {
							nextSample[0] = data[pos];
						} else {
							if ( sizeof( Type) == 2) {
								nextSample[0]=(Bit16s)host_readw((HostPt)&data[pos]);
							} else {
								nextSample[0]=(Bit32s)host_readd((HostPt)&data[pos]);
							}
						}
					}
				} else {
					if (stereo) {
						if (nativeorder) {
							nextSample[0]=(Bits)data[pos*2+0]-32768;
							nextSample[1]=(Bits)data[pos*2+1]-32768;
						} else {
							if ( sizeof( Type) == 2) {
								nextSample[0]=(Bits)host_readw((HostPt)&data[pos*2+0])-32768;
								nextSample[1]=(Bits)host_readw((HostPt)&data[pos*2+1])-32768;
							} else {
								nextSample[0]=(Bits)host_readd((HostPt)&data[pos*2+0])-32768;
								nextSample[1]=(Bits)host_readd((HostPt)&data[pos*2+1])-32768;
							}
						}
					} else {
						if (nativeorder) {
							nextSample[0]=(Bits)data[pos]-32768;
						} else {
							if ( sizeof( Type) == 2) {
								nextSample[0]=(Bits)host_readw((HostPt)&data[pos])-32768;
							} else {
								nextSample[0]=(Bits)host_readd((HostPt)&data[pos])-32768;
							}
						}
					}
				}
			}
			pos++;
		}
		mixpos &= MIXER_BUFMASK;
		Bit32s* write = mixer.work[mixpos];
		if (!interpolate) {
			write[0] += prevSample[0] * volmul[0];
			write[1] += (stereo ? prevSample[1] : prevSample[0]) * volmul[1];
		}
		else {
			Bits diff_mul = freq_counter & FREQ_MASK;
			Bits sample = prevSample[0] + (((nextSample[0] - prevSample[0]) * diff_mul) >> FREQ_SHIFT);
			write[0] += sample*volmul[0];
			if (stereo) {
				sample = prevSample[1] + (((nextSample[1] - prevSample[1]) * diff_mul) >> FREQ_SHIFT);
			}
			write[1] += sample*volmul[1];
		}
		freq_counter += freq_add;
		mixpos++;
		done++;
	}
}

void MixerChannel::AddStretched(Bitu len,Bit16s * data) {
	if (done >= needed) {
		//LOG_MSG("Can't add, buffer full");
		return;
	}
	Bitu outlen = needed - done;
	Bitu index = 0;
	Bitu index_add = (len << FREQ_SHIFT)/outlen;
	Bitu mixpos = mixer.pos + done;
	done = needed;
	Bitu pos = 0;

	while (outlen--) {
		Bitu new_pos = index >> FREQ_SHIFT;
		if (pos != new_pos) {
			pos = new_pos;
			prevSample[0] = data[0];
			data++;
		}
		Bits diff = data[0] - prevSample[0];
		Bits diff_mul = index & FREQ_MASK;
		index += index_add;
		mixpos &= MIXER_BUFMASK;
		Bits sample = prevSample[0] + ((diff * diff_mul) >> FREQ_SHIFT);
		mixer.work[mixpos][0] += sample * volmul[0];
		mixer.work[mixpos][1] += sample * volmul[1];
		mixpos++;
	}
}

void MixerChannel::AddSamples_m8(Bitu len, const Bit8u * data) {
	AddSamples<Bit8u,false,false,true>(len,data);
}
void MixerChannel::AddSamples_s8(Bitu len,const Bit8u * data) {
	AddSamples<Bit8u,true,false,true>(len,data);
}
void MixerChannel::AddSamples_m8s(Bitu len,const Bit8s * data) {
	AddSamples<Bit8s,false,true,true>(len,data);
}
void MixerChannel::AddSamples_s8s(Bitu len,const Bit8s * data) {
	AddSamples<Bit8s,true,true,true>(len,data);
}
void MixerChannel::AddSamples_m16(Bitu len,const Bit16s * data) {
	AddSamples<Bit16s,false,true,true>(len,data);
}
void MixerChannel::AddSamples_s16(Bitu len,const Bit16s * data) {
	AddSamples<Bit16s,true,true,true>(len,data);
}
void MixerChannel::AddSamples_m16u(Bitu len,const Bit16u * data) {
	AddSamples<Bit16u,false,false,true>(len,data);
}
void MixerChannel::AddSamples_s16u(Bitu len,const Bit16u * data) {
	AddSamples<Bit16u,true,false,true>(len,data);
}
void MixerChannel::AddSamples_m32(Bitu len,const Bit32s * data) {
	AddSamples<Bit32s,false,true,true>(len,data);
}
void MixerChannel::AddSamples_s32(Bitu len,const Bit32s * data) {
	AddSamples<Bit32s,true,true,true>(len,data);
}
void MixerChannel::AddSamples_m16_nonnative(Bitu len,const Bit16s * data) {
	AddSamples<Bit16s,false,true,false>(len,data);
}
void MixerChannel::AddSamples_s16_nonnative(Bitu len,const Bit16s * data) {
	AddSamples<Bit16s,true,true,false>(len,data);
}
void MixerChannel::AddSamples_m16u_nonnative(Bitu len,const Bit16u * data) {
	AddSamples<Bit16u,false,false,false>(len,data);
}
void MixerChannel::AddSamples_s16u_nonnative(Bitu len,const Bit16u * data) {
	AddSamples<Bit16u,true,false,false>(len,data);
}
void MixerChannel::AddSamples_m32_nonnative(Bitu len,const Bit32s * data) {
	AddSamples<Bit32s,false,true,false>(len,data);
}
void MixerChannel::AddSamples_s32_nonnative(Bitu len,const Bit32s * data) {
	AddSamples<Bit32s,true,true,false>(len,data);
}

void MixerChannel::FillUp(void) {
	if (!enabled) return;

	EnterCriticalSection(&g_csAudioLock);
	if (done < mixer.done) {
		LeaveCriticalSection(&g_csAudioLock);
		return;
	}
	float index = PIC_TickIndex();
	Mix((Bitu)(index * mixer.needed));
	LeaveCriticalSection(&g_csAudioLock);
}

extern bool ticksLocked;
static inline bool Mixer_irq_important(void) {
	return (ticksLocked || (CaptureState & (CAPTURE_WAVE|CAPTURE_VIDEO)));
}

static Bit32u calc_tickadd(Bit32u freq) {
	return (freq<<TICK_SHIFT)/1000;
}

static void MIXER_MixData(Bitu needed) {
	MixerChannel * chan=mixer.channels;
	while (chan) {
		chan->Mix(needed);
		chan=chan->next;
	}
	if (CaptureState & (CAPTURE_WAVE|CAPTURE_VIDEO)) {
		Bit16s convert[1024][2];
		Bitu added=needed-mixer.done;
		if (added>1024)
			added=1024;
		Bitu readpos=(mixer.pos+mixer.done)&MIXER_BUFMASK;
		for (Bitu i=0; i<added; i++) {
			Bits sample=mixer.work[readpos][0] >> MIXER_VOLSHIFT;
			convert[i][0]=MIXER_CLIP(sample);
			sample=mixer.work[readpos][1] >> MIXER_VOLSHIFT;
			convert[i][1]=MIXER_CLIP(sample);
			readpos=(readpos+1)&MIXER_BUFMASK;
		}
		CAPTURE_AddWave( mixer.freq, added, (Bit16s*)convert );
	}
	if( Mixer_irq_important() )
		mixer.tick_add = calc_tickadd(mixer.freq);
	mixer.done = needed;
}

static void MIXER_Mix(void) {

	if (g_bWaveOutActive) return;

	static int mixCount = 0;

	EnterCriticalSection(&g_csAudioLock);

	if (mixer.needed == 0) {
		mixer.needed = mixer.min_needed + 1;
	}

	MIXER_MixData(mixer.needed);
	mixer.tick_counter += mixer.tick_add;
	mixer.needed += (mixer.tick_counter >> TICK_SHIFT);
	mixer.tick_counter &= TICK_MASK;

	LeaveCriticalSection(&g_csAudioLock);
	mixCount++;

	EnterCriticalSection(&g_csAudioLock);

	// Se needed chegou a 0, reinicializar
	if (mixer.needed == 0) {
		mixer.needed = mixer.min_needed + 1;
		if (mixCount % 100 == 0) {
			//LOG_MSG("MIXER_Mix: resetting needed to %d", mixer.needed);
		}
	}

	MIXER_MixData(mixer.needed);
	mixer.tick_counter += mixer.tick_add;
	mixer.needed += (mixer.tick_counter >> TICK_SHIFT);
	mixer.tick_counter &= TICK_MASK;

	LeaveCriticalSection(&g_csAudioLock);

	if (mixCount % 100 == 0) {
		//LOG_MSG("MIXER_Mix: done=%d, needed=%d, pos=%d", mixer.done, mixer.needed, mixer.pos);
	}
	mixCount++;

}

static void MIXER_Mix_NoSound(void) {
	MIXER_MixData(mixer.needed);
	for (Bitu i=0; i<mixer.needed; i++) {
		mixer.work[mixer.pos][0]=0;
		mixer.work[mixer.pos][1]=0;
		mixer.pos=(mixer.pos+1)&MIXER_BUFMASK;
	}
	for (MixerChannel * chan=mixer.channels; chan; chan=chan->next) {
		if (chan->done>mixer.needed) chan->done-=mixer.needed;
		else chan->done=0;
	}
	mixer.tick_counter += mixer.tick_add;
	mixer.needed = (mixer.tick_counter >> TICK_SHIFT);
	mixer.tick_counter &= TICK_MASK;
	mixer.done=0;
}

static DWORD WINAPI WaveOutThread(LPVOID lpParam) {
	DWORD dwCurrentBuffer = 0;
	DWORD lastPos = 0;
	DWORD silenceCount = 0;

	//LOG_MSG("MIXER: WaveOutThread started");

	while (g_bWaveOutActive && g_hWaveOut != NULL) {
		if (g_waveHeaders[dwCurrentBuffer].dwFlags & WHDR_INQUEUE) {
			Sleep(1);
			continue;
		}

		if (g_waveHeaders[dwCurrentBuffer].dwFlags & WHDR_PREPARED) {
			waveOutUnprepareHeader(g_hWaveOut, &g_waveHeaders[dwCurrentBuffer], sizeof(WAVEHDR));
		}

		EnterCriticalSection(&g_csAudioLock);

		// Forçar mixagem de novos dados
		mixer.needed = mixer.min_needed + 1;
		MIXER_MixData(mixer.needed);
		mixer.tick_counter += mixer.tick_add;
		mixer.needed += (mixer.tick_counter >> TICK_SHIFT);
		mixer.tick_counter &= TICK_MASK;

		Bitu need = mixer.blocksize;
		Bit16s output[2048];

		Bitu samplesToCopy = (mixer.done < need) ? mixer.done : need;

		if (samplesToCopy > 0) {
			Bitu pos = mixer.pos;
			for (Bitu i = 0; i < samplesToCopy; i++) {
				pos &= MIXER_BUFMASK;
				Bits sample = mixer.work[pos][0] >> MIXER_VOLSHIFT;
				output[i*2] = MIXER_CLIP(sample);
				sample = mixer.work[pos][1] >> MIXER_VOLSHIFT;
				output[i*2+1] = MIXER_CLIP(sample);
				mixer.work[pos][0] = 0;
				mixer.work[pos][1] = 0;
				pos++;
			}
			mixer.pos = pos & MIXER_BUFMASK;
			mixer.done = 0;
			mixer.needed = 0;

			for (MixerChannel * chan = mixer.channels; chan; chan = chan->next) {
				if (chan->done > samplesToCopy) chan->done -= samplesToCopy;
				else chan->done = 0;
			}
			silenceCount = 0;
		} else {
			samplesToCopy = need;
			memset(output, 0, need * MIXER_SSIZE);
			silenceCount++;
			if (silenceCount > 100) {
				LeaveCriticalSection(&g_csAudioLock);
				Sleep(10);
				continue;
			}
		}

		if (samplesToCopy < need) {
			memset(&output[samplesToCopy * 2], 0, (need - samplesToCopy) * MIXER_SSIZE);
		}

		LeaveCriticalSection(&g_csAudioLock);

		memcpy(g_pWaveBuffers[dwCurrentBuffer], output, need * MIXER_SSIZE);
		g_waveHeaders[dwCurrentBuffer].dwBufferLength = need * MIXER_SSIZE;
		g_waveHeaders[dwCurrentBuffer].dwFlags = 0;

		// Verificar se o dispositivo ainda está válido antes de escrever
		if (g_hWaveOut) {
			waveOutPrepareHeader(g_hWaveOut, &g_waveHeaders[dwCurrentBuffer], sizeof(WAVEHDR));
			waveOutWrite(g_hWaveOut, &g_waveHeaders[dwCurrentBuffer], sizeof(WAVEHDR));
		}

		dwCurrentBuffer = (dwCurrentBuffer + 1) % 4;
	}

	//LOG_MSG("MIXER: WaveOutThread exiting");
	return 0;
}

static void MIXER_Stop(Section* /*sec*/) {
	if (g_bWaveOutActive) {
		g_bWaveOutActive = false;
		if (g_hMixerThread) {
			WaitForSingleObject(g_hMixerThread, 1000);
			CloseHandle(g_hMixerThread);
			g_hMixerThread = NULL;
		}
		if (g_hWaveOut) {
			waveOutReset(g_hWaveOut);
			for (int i = 0; i < 4; i++) {
				if (g_waveHeaders[i].dwFlags & WHDR_PREPARED) {
					waveOutUnprepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
				}
				if (g_pWaveBuffers[i]) {
					delete[] g_pWaveBuffers[i];
					g_pWaveBuffers[i] = NULL;
				}
			}
			waveOutClose(g_hWaveOut);
			g_hWaveOut = NULL;
		}
		DeleteCriticalSection(&g_csAudioLock);
	}
}

class MIXER : public Program {
public:
	void MakeVolume(char * scan,float & vol0,float & vol1) {
		Bitu w=0;
		bool db=(toupper(*scan)=='D');
		if (db) scan++;
		while (*scan) {
			if (*scan==':') {
				++scan;
				w=1;
			}
			char * before=scan;
			float val=(float)strtod(scan,&scan);
			if (before==scan) {
				++scan;
				continue;
			}
			if (!db) val/=100;
			else val=powf(10.0f,(float)val/20.0f);
			if (val<0) val=1.0f;
			if (!w) {
				vol0=val;
			} else {
				vol1=val;
			}
		}
		if (!w) vol1=vol0;
	}

	void Run(void) {
		if(cmd->FindExist("/LISTMIDI")) {
			ListMidi();
			return;
		}
		if (cmd->FindString("MASTER",temp_line,false)) {
			MakeVolume((char *)temp_line.c_str(),mixer.mastervol[0],mixer.mastervol[1]);
		}
		MixerChannel * chan = mixer.channels;
		while (chan) {
			if (cmd->FindString(chan->name,temp_line,false)) {
				MakeVolume((char *)temp_line.c_str(),chan->volmain[0],chan->volmain[1]);
			}
			chan->UpdateVolume();
			chan = chan->next;
		}
		if (cmd->FindExist("/NOSHOW")) return;
		WriteOut("Channel  Main    Main(dB)\n");
		ShowVolume("MASTER",mixer.mastervol[0],mixer.mastervol[1]);
		for (chan = mixer.channels; chan; chan = chan->next)
			ShowVolume(chan->name,chan->volmain[0],chan->volmain[1]);
	}
private:
	void ShowVolume(const char * name,float vol0,float vol1) {
		WriteOut("%-8s %3.0f:%-3.0f  %+3.2f:%-+3.2f \n",name,
		         vol0*100,vol1*100,
		         20*log(vol0)/log(10.0f),20*log(vol1)/log(10.0f)
		        );
	}

	void ListMidi() {
		if(midi.handler) midi.handler->ListAll(this);
	};
};

static void MIXER_ProgramStart(Program * * make) {
	*make=new MIXER;
}

MixerChannel* MixerObject::Install(MIXER_Handler handler,Bitu freq,const char * name) {
	if(!installed) {
		if(strlen(name) > 31) E_Exit("Too long mixer channel name");
		safe_strncpy(m_name,name,32);
		installed = true;
		return MIXER_AddChannel(handler,freq,name);
	} else {
		E_Exit("already added mixer channel.");
		return 0;
	}
}

MixerObject::~MixerObject() {
	if(!installed) return;
	MIXER_DelChannel(MIXER_FindChannel(m_name));
}

void MIXER_Init(Section* sec) {
	sec->AddDestroyFunction(&MIXER_Stop);

	Section_prop * section=static_cast<Section_prop *>(sec);
	mixer.freq=section->Get_int("rate");
	mixer.nosound=section->Get_bool("nosound");
	mixer.blocksize=section->Get_int("blocksize");

	const Bitu MIN_BLOCKSIZE = 128;
	const Bitu MAX_BLOCKSIZE = 1024;
	const Bitu IDEAL_BLOCKSIZE = 256;

	if (mixer.blocksize < MIN_BLOCKSIZE) {
		mixer.blocksize = MIN_BLOCKSIZE;
	} else if (mixer.blocksize > MAX_BLOCKSIZE) {
		mixer.blocksize = MAX_BLOCKSIZE;
	}

	int prebuffer = section->Get_int("prebuffer");
	if (prebuffer < 20) {
		prebuffer = 20;
	} else if (prebuffer > 200) {
		prebuffer = 200;
	}
	mixer.min_needed = (mixer.freq * prebuffer) / 1000;

	mixer.channels=0;
	mixer.pos=0;
	mixer.done=0;
	memset(mixer.work,0,sizeof(mixer.work));
	mixer.mastervol[0]=1.0f;
	mixer.mastervol[1]=1.0f;
	mixer.tick_counter=0;

	// Se nosound foi forçado na configuração, usar modo silencioso
	if (mixer.nosound) {
		//LOG_MSG("MIXER: No Sound Mode Selected by configuration.");
		mixer.tick_add=calc_tickadd(mixer.freq);
		TIMER_AddTickHandler(MIXER_Mix_NoSound);
		return;
	}

	// Tentar inicializar o WaveOut
	WAVEFORMATEX wfx;
	ZeroMemory(&wfx, sizeof(WAVEFORMATEX));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = mixer.freq;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	// Tentar diferentes dispositivos se o padrão falhar
	MMRESULT mmr = MMSYSERR_ERROR;
	UINT numDevices = waveOutGetNumDevs();

	// Primeiro tentar WAVE_MAPPER
	mmr = waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);

	// Se falhar, tentar cada dispositivo disponível
	if (mmr != MMSYSERR_NOERROR) {
		for (UINT i = 0; i < numDevices; i++) {
			mmr = waveOutOpen(&g_hWaveOut, i, &wfx, 0, 0, CALLBACK_NULL);
			if (mmr == MMSYSERR_NOERROR) {
				//LOG_MSG("MIXER: Using waveOut device %d", i);
				break;
			}
		}
	}

	// Se ainda falhou, entrar em modo nosound
	if (mmr != MMSYSERR_NOERROR) {
		//LOG_MSG("MIXER: No audio device found (error %d), running in nosound mode.", mmr);
		mixer.nosound = true;
		mixer.tick_add=calc_tickadd(mixer.freq);
		TIMER_AddTickHandler(MIXER_Mix_NoSound);

		// Limpar qualquer estado parcial
		if (g_hWaveOut) {
			waveOutClose(g_hWaveOut);
			g_hWaveOut = NULL;
		}
		return;
	}

	// Configuração bem sucedida do WaveOut
	waveOutSetVolume(g_hWaveOut, 0xFFFF);

	g_dwBufferSize = mixer.blocksize * MIXER_SSIZE;

	for (int i = 0; i < 4; i++) {
		g_pWaveBuffers[i] = new BYTE[g_dwBufferSize];
		memset(g_pWaveBuffers[i], 0, g_dwBufferSize);
		ZeroMemory(&g_waveHeaders[i], sizeof(WAVEHDR));
		g_waveHeaders[i].lpData = (LPSTR)g_pWaveBuffers[i];
		g_waveHeaders[i].dwBufferLength = g_dwBufferSize;
		g_waveHeaders[i].dwFlags = 0;

		mmr = waveOutPrepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
		if (mmr != MMSYSERR_NOERROR) {
			//LOG_MSG("MIXER: waveOutPrepareHeader failed: %d", mmr);
			// Fallback para nosound se não conseguir preparar headers
			MIXER_Stop(NULL);
			mixer.nosound = true;
			mixer.tick_add=calc_tickadd(mixer.freq);
			TIMER_AddTickHandler(MIXER_Mix_NoSound);
			return;
		}

		mmr = waveOutWrite(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
		if (mmr != MMSYSERR_NOERROR) {
			// LOG_MSG("MIXER: waveOutWrite failed: %d", mmr);
			MIXER_Stop(NULL);
			mixer.nosound = true;
			mixer.tick_add=calc_tickadd(mixer.freq);
			TIMER_AddTickHandler(MIXER_Mix_NoSound);
			return;
		}
	}

	InitializeCriticalSection(&g_csAudioLock);

	g_bWaveOutActive = true;

	mixer.min_needed = section->Get_int("prebuffer");
	if (mixer.min_needed > 100) mixer.min_needed = 100;
	mixer.min_needed = (mixer.freq * mixer.min_needed) / 1000;
	mixer.max_needed = mixer.blocksize * 2 + 2 * mixer.min_needed;
	mixer.needed = mixer.min_needed + 1;

	TIMER_AddTickHandler(MIXER_Mix);

	g_hMixerThread = CreateThread(NULL, 0, WaveOutThread, NULL, 0, NULL);

	if (g_hMixerThread == NULL) {
		//LOG_MSG("MIXER: Failed to create mixer thread, falling back to nosound");
		MIXER_Stop(NULL);
		mixer.nosound = true;
		mixer.tick_add=calc_tickadd(mixer.freq);
		TIMER_AddTickHandler(MIXER_Mix_NoSound);
		return;
	}

	SetThreadPriority(g_hMixerThread, THREAD_PRIORITY_HIGHEST);
	//LOG_MSG("MIXER: WaveOut initialized at %d Hz", mixer.freq);

	PROGRAMS_MakeFile("MIXER.COM", MIXER_ProgramStart);
}