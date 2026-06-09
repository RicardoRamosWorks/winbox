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

// #define DEBUG 1
#ifdef DEBUG
#include <time.h>
#include <chrono>
#endif

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <limits.h> //GCC 2.95
#include <sstream>
#include <vector>
#include <sys/stat.h>
#include "cdrom.h"
#include "drives.h"
#include "support.h"
#include "setup.h"

#if !defined(WIN32)
#include <libgen.h>
#else
#include <string.h>
#endif

#if defined(WORDS_BIGENDIAN)
#define IS_BIGENDIAN true
#else
#define IS_BIGENDIAN false
#endif

using namespace std;

#define MAX_LINE_LENGTH 512
#define MAX_FILENAME_LENGTH 256

#ifdef DEBUG
char* get_time() {
	static time_t rawtime;
	struct tm* ptime;
	static char time_str[] = "00:00:00";

	time(&rawtime);
	ptime = localtime(&rawtime);
	sprintf(time_str, "%02d:%02d:%02d", ptime->tm_hour, ptime->tm_min, ptime->tm_sec);
	return time_str;
}
#endif

CDROM_Interface_Image::BinaryFile::BinaryFile(const char *filename,bool &error)
	: TrackFile(4096)
{
	file = new ifstream(filename, ios::in | ios::binary);

	static char audio_buf[64 * 1024];
	file->rdbuf()->pubsetbuf(audio_buf, sizeof(audio_buf));
	error = (!file || file->fail());
}

CDROM_Interface_Image::BinaryFile::~BinaryFile()
{
	delete file;
	file = NULL;
}

bool CDROM_Interface_Image::BinaryFile::seek(Bit32u offset)
{
	file->clear();
	file->seekg(offset, ios::beg);
	return !file->fail();
}

bool CDROM_Interface_Image::BinaryFile::read(uint8_t *buffer,
        int offset,
        int count)
{
	if (!seek(offset))
		return false;

	file->read((char*)buffer, count);

	return !file->fail();
}

Bit16u CDROM_Interface_Image::BinaryFile::decode(Bit8u *buffer)
{
	file->read((char*)buffer, chunkSize);
	return (Bit16u)file->gcount();
}

Bit32u CDROM_Interface_Image::BinaryFile::getRate()
{
	return 44100;
}

Bit8u CDROM_Interface_Image::BinaryFile::getChannels()
{
	return 2;
}

int CDROM_Interface_Image::BinaryFile::getLength()
{
	file->seekg(0, ios::end);

	std::streampos pos = file->tellg();

	if (pos < 0 || file->fail())
		return -1;

	return static_cast<int>(pos);
}

Bit16u CDROM_Interface_Image::BinaryFile::getEndian()
{
#if defined(WORDS_BIGENDIAN)
	return AUDIO_S16MSB;
#else
	return AUDIO_S16LSB;
#endif
}




// initialize static members
int CDROM_Interface_Image::refCount = 0;
CDROM_Interface_Image* CDROM_Interface_Image::images[26] = {};
CDROM_Interface_Image::imagePlayer CDROM_Interface_Image::player = {
	NULL,  // CDROM_Interface_Image*
	NULL,  // MixerChannel*
	{0},   // buffer[]
	0,     // startFrame
	0,     // currFrame
	0,     // numFrames
	false, // isPlaying
	false, // isPaused
	false, // ctrlUsed
	{0},   // ctrlData struct
	NULL,  // activeTrack
	NULL,  // addSamples
	0,     // playbackTotal
	0,     // playbackRemaining
	0,     // bufferPos
	0      // bufferConsumed
};

CDROM_Interface_Image::CDROM_Interface_Image(Bit8u subUnit)
	:subUnit(subUnit)
{
	images[subUnit] = this;
	if (refCount == 0) {
		if (player.channel == NULL) {
			// channel is kept dormant except during cdrom playback periods
			player.channel = MIXER_AddChannel(&CDAudioCallBack, 0, "CDAUDIO");
			player.channel->Enable(false);
			// LOG_MSG("CDROM: Initialized with %d-byte circular buffer", AUDIO_DECODE_BUFFER_SIZE);
		}
	}
	refCount++;
}

CDROM_Interface_Image::~CDROM_Interface_Image()
{
	refCount--;
	if (player.cd == this) player.cd = NULL;
	ClearTracks();
	if (refCount == 0) {
		StopAudio();
		MIXER_DelChannel(player.channel);
		player.channel = NULL;
		// LOG_MSG("CDROM: Audio channel freed");
	}
}

void CDROM_Interface_Image::InitNewMedia()
{
}

bool CDROM_Interface_Image::SetDevice(char* path, int /*forceCD*/)
{
	if (LoadCueSheet(path)) return true;
	if (LoadIsoFile(path)) return true;

	// print error message on dosbox console
	char buf[MAX_LINE_LENGTH];
	snprintf(buf, MAX_LINE_LENGTH, "Could not load image file: %s\r\n", path);
	Bit16u size = (Bit16u)strlen(buf);
	DOS_WriteFile(STDOUT, (Bit8u*)buf, &size);
	return false;
}

bool CDROM_Interface_Image::GetUPC(unsigned char& attr, char* upc)
{
	attr = 0;
	strcpy(upc, this->mcn.c_str());

	//#ifdef DEBUG
	//LOG_MSG("%s CDROM: GetUPC=%s", get_time(), upc);
	//#endif

	return true;
}

bool CDROM_Interface_Image::GetAudioTracks(int& stTrack,
        int& end,
        TMSF& leadOut)
{
	if (tracks.empty())
		return false;

	stTrack = 1;
	end = (int)(tracks.size() - 1);

	FRAMES_TO_MSF(tracks[tracks.size() - 1].start + 150,
	              &leadOut.min,
	              &leadOut.sec,
	              &leadOut.fr);

	return true;
}

bool CDROM_Interface_Image::GetAudioTrackInfo(int track,
        TMSF& start,
        unsigned char& attr)
{
	if (track < 1 || track >= (int)tracks.size())
		return false;

	FRAMES_TO_MSF(tracks[track - 1].start + 150,
	              &start.min,
	              &start.sec,
	              &start.fr);

	attr = tracks[track - 1].attr;

	return true;
}

bool CDROM_Interface_Image::GetAudioSub(unsigned char& attr, unsigned char& track, unsigned char& index, TMSF& relPos, TMSF& absPos)
{
	int cur_track = GetTrack(player.currFrame);
	if (cur_track < 1) return false;
	track = (unsigned char)cur_track;
	attr = tracks[track - 1].attr;
	index = 1;
	FRAMES_TO_MSF(player.currFrame + 150, &absPos.min, &absPos.sec, &absPos.fr);
	FRAMES_TO_MSF(player.currFrame - tracks[track - 1].start + 150, &relPos.min, &relPos.sec, &relPos.fr);

	//#ifdef DEBUG
	//LOG_MSG("%s CDROM: GetAudioSub attr=%u, track=%u, index=%u", get_time(), attr, track, index);

	//LOG_MSG("%s CDROM: GetAudioSub absoute  offset (%d), MSF=%d:%d:%d",
	//  get_time(),
	//  player.currFrame + 150,
	//  absPos.min,
	//  absPos.sec,
	//  absPos.fr);
	//LOG_MSG("%s CDROM: GetAudioSub relative offset (%d), MSF=%d:%d:%d",
	//  get_time(),
	//  player.currFrame - tracks[track - 1].start + 150,
	////  relPos.min,
	//  relPos.sec,
	//  relPos.fr);
	//#endif

	return true;
}

bool CDROM_Interface_Image::GetAudioStatus(bool& playing, bool& pause)
{
	playing = player.isPlaying;
	pause = player.isPaused;

#ifdef DEBUG
	LOG_MSG("%s CDROM: GetAudioStatus playing=%d, paused=%d", get_time(), playing, pause);
#endif

	return true;
}

bool CDROM_Interface_Image::GetMediaTrayStatus(bool& mediaPresent, bool& mediaChanged, bool& trayOpen)
{
	mediaPresent = true;
	mediaChanged = false;
	trayOpen = false;

#ifdef DEBUG
	LOG_MSG("%s CDROM: GetMediaTrayStatus present=%d, changed=%d, open=%d", get_time(), mediaPresent, mediaChanged, trayOpen);
#endif

	return true;
}

bool CDROM_Interface_Image::PlayAudioSector(unsigned long start, unsigned long len)
{
	bool is_playable(false);
	const int track = GetTrack(start) - 1;

	// The CDROM Red Book standard allows up to 99 tracks, which includes the data track
	if ( track < 0 || track > 99 )
		LOG(LOG_MISC, LOG_WARN)("Game tried to load track #%d, which is invalid", track);

	// Attempting to play zero sectors is a no-op
	else if (len == 0)
		LOG(LOG_MISC, LOG_WARN)("Game tried to play zero sectors, skipping");

	// The maximum storage achieved on a CDROM was ~900MB or just under 100 minutes
	// with overburning, so use this threshold to sanity-check the start sector.
	//else if (start > 450000)
	//	LOG(LOG_MISC, LOG_WARN)("Game tried to read sector %lu, which is beyond the 100-minute maximum of a CDROM", start);

	// We can't play audio from a data track (as it would result in garbage/static)
	else if(track >= 0 && tracks[track].attr == 0x40)
		LOG(LOG_MISC,LOG_WARN)("Game tries to play the data track. Not doing this");

	// Checks passed, setup the audio stream
	else {
		TrackFile* trackFile = tracks[track].file;

		// Convert the playback start sector to a time offset (milliseconds) relative to the track
		const Bit32u offset = tracks[track].skip + (start - tracks[track].start) * tracks[track].sectorSize;
		is_playable = trackFile->seek(offset);

		// only initialize the player elements if our track is playable
		if (is_playable) {
			trackFile->setAudioPosition(offset);
			const Bit8u channels = trackFile->getChannels();
			const Bit32u rate = trackFile->getRate();

			player.cd = this;
			player.trackFile = trackFile;
			player.startFrame = start;
			player.currFrame = start;
			player.numFrames = len;
			player.bufferPos = 0;
			player.bufferConsumed = 0;
			player.isPlaying = true;
			player.isPaused = false;

			if ( (!IS_BIGENDIAN && trackFile->getEndian() == AUDIO_S16SYS) ||
			        ( IS_BIGENDIAN && trackFile->getEndian() != AUDIO_S16SYS) )
				player.addSamples = channels ==  2  ? &MixerChannel::AddSamples_s16 \
				                    : &MixerChannel::AddSamples_m16;
			else
				player.addSamples = channels ==  2  ? &MixerChannel::AddSamples_s16_nonnative \
				                    : &MixerChannel::AddSamples_m16_nonnative;

			const float bytesPerMs = rate * channels * 2 / 1000.0;
			player.playbackTotal = lround(len * tracks[track].sectorSize * bytesPerMs / 176.4);
			player.playbackRemaining = player.playbackTotal;

			//#ifdef DEBUG
			//LOG_MSG(
			//  "%s CDROM: Playing track %d at %.1f KHz %d-channel at start sector %lu (%.1f minute-mark), seek %u (skip=%d,dstart=%d,secsize=%d), for %lu sectors (%.1f seconds)",
			//   get_time(),
			//  track,
			// rate/1000.0,
			//channels,
			//start,
			//offset * (1/10584000.0),
			//offset,
			//tracks[track].skip,
			//tracks[track].start,
			//tracks[track].sectorSize,
			//len,
			//player.playbackRemaining / (1000 * bytesPerMs)
			//);
			//#endif

			// start the channel!
			player.channel->SetFreq(rate);
			player.channel->Enable(true);
		}
	}
	if (!is_playable) StopAudio();
	return is_playable;
}

bool CDROM_Interface_Image::PauseAudio(bool resume)
{
	if (resume) {
		if (player.isPaused) {
			player.channel->Enable(true);
			player.isPaused = false;
		}
	} else {
		if (!player.isPaused) {
			player.channel->Enable(false);
			player.isPaused = true;
		}
	}

	//#ifdef DEBUG
	//LOG_MSG("%s CDROM: PauseAudio, state=%s",
	//        get_time(),
	//        resume ? "resumed" : "paused");
	//#endif

	return true;
}

bool CDROM_Interface_Image::StopAudio(void)
{
	// Only switch states if needed
	if (player.isPlaying) {
		player.channel->Enable(false);
		player.isPlaying = false;
		player.isPaused = false;
	}

	//#ifdef DEBUG
	//LOG_MSG("%s CDROM: StopAudio", get_time());
	//#endif

	return true;
}

void CDROM_Interface_Image::ChannelControl(TCtrl ctrl)
{
	player.ctrlUsed = (ctrl.out[0]!=0 || ctrl.out[1]!=1 || ctrl.vol[0]<0xfe || ctrl.vol[1]<0xfe);
	player.ctrlData = ctrl;
}

bool CDROM_Interface_Image::ReadSectors(PhysPt buffer,
                                        bool raw,
                                        unsigned long sector,
                                        unsigned long num)
{
	const int sectorSize = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;

	const size_t buflen = static_cast<size_t>(num) * sectorSize;

	std::vector<Bit8u> buf(buflen);

	bool success = true;

	for (unsigned long i = 0; i < num; i++) {
		success = ReadSector(&buf[i * sectorSize], raw, sector + i);

		if (!success)
			break;
	}

	MEM_BlockWrite(buffer, buf.data(), buflen);

	return success;
}

bool CDROM_Interface_Image::LoadUnloadMedia(bool /*unload*/)
{
	return true;
}

int CDROM_Interface_Image::GetTrack(int sector)
{
	if (tracks.size() < 2)
		return -1;

	vector<Track>::iterator i = tracks.begin();
	vector<Track>::iterator end = tracks.end() - 1;

	while (i != end) {
		Track &curr = *i;
		Track &next = *(i + 1);

		if (curr.start <= sector && sector < next.start)
			return curr.number;

		++i;
	}

	return -1;
}

bool CDROM_Interface_Image::ReadSector(Bit8u *buffer, bool raw, unsigned long sector)
{
	int track = GetTrack(sector) - 1;
	if (track < 0) return false;

	int seek = tracks[track].skip + (sector - tracks[track].start) * tracks[track].sectorSize;
	int length = (raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE);
	if (tracks[track].sectorSize != RAW_SECTOR_SIZE && raw) return false;
	if (tracks[track].sectorSize == RAW_SECTOR_SIZE && !tracks[track].mode2 && !raw) seek += 16;
	if (tracks[track].mode2 && !raw) seek += 24;

	// LOG_MSG("CDROM: ReadSector track=%d, desired raw=%s, sector=%ld, length=%d", track, raw ? "true":"false", sector, length);
	return tracks[track].file->read(buffer, seek, length);
}

void printProgress(double percentage, const char* msg)
{
	// 60 is the number of characters in the full progress bar
	int val  = (int)(percentage * 100);
	int lpad = (int)(percentage * 60);
	int rpad = 60 - lpad;
	//LOG_MSG("\r%3d%% [%.*s%*s] - %s", val, lpad,
	//   "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||", rpad, "", msg);
	fflush(stdout);
}

void CDROM_Interface_Image::CDAudioCallBack(Bitu len)
{
	if (!player.isPlaying || player.isPaused)
		return;

	TrackFile* file = player.trackFile;

	const Bit8u channels = file->getChannels();
	const Bit16u bytes_per_sample = channels * 2;

	const Bit16u bytes_to_read = len * bytes_per_sample;

	static Bit8u buffer[8192];

	Bit16u got = file->decode(buffer);

	if (got == 0) {
		player.cd->StopAudio();
		return;
	}

	(player.channel->*player.addSamples)(
	    got / bytes_per_sample,
	    (Bit16s*)buffer
	);

	player.playbackRemaining -= got;

	if (player.playbackRemaining <= 0) {
		player.cd->StopAudio();
	}
}

bool CDROM_Interface_Image::LoadIsoFile(char* filename)
{
	tracks.clear();

	Track track = {0, 0, 0, 0, 0, 0, false, nullptr};

	bool error;

	track.file = new BinaryFile(filename, error);

	if (error) {
		delete track.file;
		track.file = nullptr;
		return false;
	}

	track.number = 1;
	track.attr = 0x40;

	if (CanReadPVD(track.file, COOKED_SECTOR_SIZE, false)) {
		track.sectorSize = COOKED_SECTOR_SIZE;
		track.mode2 = false;
	}
	else if (CanReadPVD(track.file, RAW_SECTOR_SIZE, false)) {
		track.sectorSize = RAW_SECTOR_SIZE;
		track.mode2 = false;
	}
	else if (CanReadPVD(track.file, 2336, true)) {
		track.sectorSize = 2336;
		track.mode2 = true;
	}
	else if (CanReadPVD(track.file, RAW_SECTOR_SIZE, true)) {
		track.sectorSize = RAW_SECTOR_SIZE;
		track.mode2 = true;
	}
	else {
		delete track.file;
		track.file = nullptr;
		return false;
	}

	const int fileLength = track.file->getLength();

	if (fileLength <= 0) {
		delete track.file;
		track.file = nullptr;
		return false;
	}

	track.length = fileLength / track.sectorSize;

	tracks.push_back(track);

	track.number = 2;
	track.attr = 0;
	track.start = track.length;
	track.length = 0;
	track.file = nullptr;

	tracks.push_back(track);

	return true;
}

bool CDROM_Interface_Image::CanReadPVD(TrackFile *file,
                                       int sectorSize,
                                       bool mode2)
{
	Bit8u pvd[COOKED_SECTOR_SIZE];

	int seek = 16 * sectorSize;

	if (sectorSize == RAW_SECTOR_SIZE && !mode2)
		seek += 16;

	if (mode2)
		seek += 24;

	if (!file->read(pvd, seek, COOKED_SECTOR_SIZE))
		return false;

	return (
	           (pvd[0] == 1 &&
	            !strncmp((char*)(&pvd[1]), "CD001", 5) &&
	            pvd[6] == 1)
	           ||
	           (pvd[8] == 1 &&
	            !strncmp((char*)(&pvd[9]), "CDROM", 5) &&
	            pvd[14] == 1)
	       );
}

#if defined(WIN32)
static string dirname(char * file) {
	char * sep = strrchr(file, '\\');
	if (sep == NULL)
		sep = strrchr(file, '/');
	if (sep == NULL)
		return "";
	else {
		int len = (int)(sep - file);
		char tmp[MAX_FILENAME_LENGTH];
		safe_strncpy(tmp, file, len+1);
		return tmp;
	}
}
#endif

bool CDROM_Interface_Image::LoadCueSheet(char *cuefile)
{
	Track track = {0, 0, 0, 0, 0, 0, false, NULL};
	tracks.clear();
	int shift = 0;
	int currPregap = 0;
	int totalPregap = 0;
	int prestart = -1;
	bool success;
	bool canAddTrack = false;
	char tmp[MAX_FILENAME_LENGTH];  // dirname can change its argument
	safe_strncpy(tmp, cuefile, MAX_FILENAME_LENGTH);
	string pathname(dirname(tmp));
	ifstream in;
	in.open(cuefile, ios::in);
	if (in.fail()) return false;

	while (in.good()) {
		// get next line
		char buf[MAX_LINE_LENGTH];
		in.getline(buf, MAX_LINE_LENGTH);
		if (in.fail() && !in.eof()) return false;  // probably a binary file
		istringstream line(buf);

		string command;
		GetCueKeyword(command, line);

		if (command == "TRACK") {
			if (canAddTrack) success = AddTrack(track, shift, prestart, totalPregap, currPregap);
			else success = true;

			track.start = 0;
			track.skip = 0;
			currPregap = 0;
			prestart = -1;

			line >> track.number;
			string type;
			GetCueKeyword(type, line);

			if (type == "AUDIO") {
				track.sectorSize = RAW_SECTOR_SIZE;
				track.attr = 0;
				track.mode2 = false;
			} else if (type == "MODE1/2048") {
				track.sectorSize = COOKED_SECTOR_SIZE;
				track.attr = 0x40;
				track.mode2 = false;
			} else if (type == "MODE1/2352") {
				track.sectorSize = RAW_SECTOR_SIZE;
				track.attr = 0x40;
				track.mode2 = false;
			} else if (type == "MODE2/2336") {
				track.sectorSize = 2336;
				track.attr = 0x40;
				track.mode2 = true;
			} else if (type == "MODE2/2352") {
				track.sectorSize = RAW_SECTOR_SIZE;
				track.attr = 0x40;
				track.mode2 = true;
			} else success = false;

			canAddTrack = true;
		}
		else if (command == "INDEX") {
			int index;
			line >> index;
			int frame;
			success = GetCueFrame(frame, line);

			if (index == 1) track.start = frame;
			else if (index == 0) prestart = frame;
			// ignore other indices
		}
		else if (command == "FILE") {
			if (canAddTrack) success = AddTrack(track, shift, prestart, totalPregap, currPregap);
			else success = true;
			canAddTrack = false;

			string filename;
			GetCueString(filename, line);
			GetRealFileName(filename, pathname);
			string type;
			GetCueKeyword(type, line);

			track.file = NULL;
			bool error = true;
			if (type == "BINARY") {
				track.file = new BinaryFile(filename.c_str(), error);
			} else {
				//LOG_MSG("CDROM: compressed audio tracks are disabled");
				success = false;
			}
			// SDL_Sound first tries using a decoder having a matching registered extension
			// as the filename, and then falls back to trying each decoder before finally
			// giving up.
			if (error) {
				delete track.file;
				track.file = NULL;
				success = false;
			}
		}
		else if (command == "PREGAP") success = GetCueFrame(currPregap, line);
		else if (command == "CATALOG") success = GetCueString(mcn, line);
		// ignored commands
		else if (command == "CDTEXTFILE" || command == "FLAGS" || command == "ISRC"
		         || command == "PERFORMER" || command == "POSTGAP" || command == "REM"
		         || command == "SONGWRITER" || command == "TITLE" || command == "") success = true;
		// failure
		else {
			delete track.file;
			track.file = NULL;
			success = false;
		}
		if (!success) return false;
	}
	// add last track
	if (!AddTrack(track, shift, prestart, totalPregap, currPregap)) return false;

	// add leadout track
	track.number++;
	track.attr = 0;//sync with load iso
	track.start = 0;
	track.length = 0;
	track.file = NULL;
	if(!AddTrack(track, shift, -1, totalPregap, 0)) return false;

	return true;
}



bool CDROM_Interface_Image::AddTrack(Track &curr, int &shift, int prestart, int &totalPregap, int currPregap)
{
	// frames between index 0(prestart) and 1(curr.start) must be skipped
	int skip;
	if (prestart >= 0) {
		if (prestart > curr.start) return false;
		skip = curr.start - prestart;
	} else skip = 0;

	// first track (track number must be 1)
	if (tracks.empty()) {
		if (curr.number != 1) return false;
		curr.skip = skip * curr.sectorSize;
		curr.start += currPregap;
		totalPregap = currPregap;
		tracks.push_back(curr);
		return true;
	}

	Track &prev = *(tracks.end() - 1);

	// current track consumes data from the same file as the previous
	if (prev.file == curr.file) {
		curr.start += shift;
		if (!prev.length) {
			prev.length = curr.start + totalPregap - prev.start - skip;
		}
		curr.skip += prev.skip + prev.length * prev.sectorSize + skip * curr.sectorSize;
		totalPregap += currPregap;
		curr.start += totalPregap;
		// current track uses a different file as the previous track
	} else {
		if (!prev.length) {
			int tmp = prev.file->getLength() - prev.skip;
			prev.length = tmp / prev.sectorSize;
			if (tmp % prev.sectorSize != 0) prev.length++; // padding
		}
		curr.start += prev.start + prev.length + currPregap;
		curr.skip = skip * curr.sectorSize;
		shift += prev.start + prev.length;
		totalPregap = currPregap;
	}

	//#ifdef DEBUG
	//LOG_MSG("%s CDROM: AddTrack cur.start=%d cur.len=%d cur.start+len=%d | prev.start=%d prev.len=%d prev.start+len=%d",
	//        get_time(),
	//        curr.start, curr.length, curr.start + curr.length,
	//        prev.start, prev.length, prev.start + prev.length);
	//#endif

	// error checks
	if (curr.number <= 1) return false;
	if (prev.number + 1 != curr.number) return false;
	if (curr.start < prev.start + prev.length) return false;
	if (curr.length < 0) return false;

	tracks.push_back(curr);
	return true;
}

bool CDROM_Interface_Image::HasDataTrack(void)
{
	//Data track has attribute 0x40
	for(track_it it = tracks.begin(); it != tracks.end(); it++) {
		if ((*it).attr == 0x40) return true;
	}
	return false;
}


bool CDROM_Interface_Image::GetRealFileName(string &filename, string &pathname)
{
	// check if file exists
	struct stat test;
	if (stat(filename.c_str(), &test) == 0) return true;

	// check if file with path relative to cue file exists
	string tmpstr(pathname + "/" + filename);
	if (stat(tmpstr.c_str(), &test) == 0) {
		filename = tmpstr;
		return true;
	}
	// finally check if file is in a dosbox local drive
	char fullname[CROSS_LEN];
	char tmp[CROSS_LEN];
	safe_strncpy(tmp, filename.c_str(), CROSS_LEN);
	Bit8u drive;
	if (!DOS_MakeName(tmp, fullname, &drive)) return false;

	localDrive *ldp = dynamic_cast<localDrive*>(Drives[drive]);
	if (ldp) {
		ldp->GetSystemFilename(tmp, fullname);
		if (stat(tmp, &test) == 0) {
			filename = tmp;
			return true;
		}
	}
#if defined (WIN32) || defined(OS2)
	//Nothing
#else
	//Consider the possibility that the filename has a windows directory seperator (inside the CUE file)
	//which is common for some commercial rereleases of DOS games using DOSBox

	string copy = filename;
	size_t l = copy.size();
	for (size_t i = 0; i < l; i++) {
		if(copy[i] == '\\') copy[i] = '/';
	}

	if (stat(copy.c_str(), &test) == 0) {
		filename = copy;
		return true;
	}

	tmpstr = pathname + "/" + copy;
	if (stat(tmpstr.c_str(), &test) == 0) {
		filename = tmpstr;
		return true;
	}

#endif
	return false;
}

bool CDROM_Interface_Image::GetCueKeyword(string &keyword,
        istream &in)
{
	in >> keyword;

	for (Bitu i = 0; i < keyword.size(); i++) {
		keyword[i] = static_cast<char>(toupper(
		                                   static_cast<unsigned char>(keyword[i])));
	}

	return true;
}

bool CDROM_Interface_Image::GetCueFrame(int &frames,
                                        istream &in)
{
	string msf;

	in >> msf;

	int min, sec, fr;

	bool success =
	    sscanf(msf.c_str(), "%d:%d:%d", &min, &sec, &fr) == 3;

	if (!success)
		return false;

	if (sec >= 60 || fr >= 75)
		return false;

	frames = MSF_TO_FRAMES(min, sec, fr);

	return true;
}

bool CDROM_Interface_Image::GetCueString(string &str,
        istream &in)
{
	int pos = (int)in.tellg();

	in >> str;

	if (!str.empty() && str[0] == '"') {
		if (str[str.size() - 1] == '"') {
			str.assign(str, 1, str.size() - 2);
		} else {
			in.seekg(pos, ios::beg);

			char buffer[MAX_FILENAME_LENGTH];

			in.getline(buffer, MAX_FILENAME_LENGTH, '"');
			in.getline(buffer, MAX_FILENAME_LENGTH, '"');

			str = buffer;
		}
	}

	return true;
}

void CDROM_Interface_Image::ClearTracks()
{
	vector<Track>::iterator i = tracks.begin();
	vector<Track>::iterator end = tracks.end();

	TrackFile* last = nullptr;

	while (i != end) {
		Track &curr = *i;

		TrackFile* current = curr.file;

		if (current != last) {
			delete current;
			last = current;
		}

		++i;
	}

	tracks.clear();
}
void CDROM_Image_Destroy(Section*)
{
}

void CDROM_Image_Init(Section* sec)
{
	sec->AddDestroyFunction(CDROM_Image_Destroy, false);
}