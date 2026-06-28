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

#include "callback.h"
#include "control.h"
#include "dosbox.h"
#include "regs.h"
#include "shell.h"
#include "support.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

Bitu call_shellstop;
/* Larger scope so shell_del autoexec can use it to
 * remove things from the environment */
DOS_Shell *first_shell = 0;

static Bitu shellstop_handler(void) {
	return CBRET_STOP;
}

static void SHELL_ProgramStart(Program **make) {
	*make = new DOS_Shell;
}
// Repeat it with the correct type, could do it in the function below, but this
// way it should be clear that if the above function is changed, this function
// might need a change as well.
static void SHELL_ProgramStart_First_shell(DOS_Shell **make) {
	*make = new DOS_Shell;
}

#define AUTOEXEC_SIZE 4096
static char autoexec_data[AUTOEXEC_SIZE] = {0};
static std::list<std::string> autoexec_strings;
typedef std::list<std::string>::iterator auto_it;

void VFILE_Remove(const char *name);

void AutoexecObject::Install(const std::string &in) {
	if (GCC_UNLIKELY(installed))
		E_Exit("autoexec: already created %s", buf.c_str());
	installed = true;
	buf = in;
	autoexec_strings.push_back(buf);
	this->CreateAutoexec();

	// autoexec.bat is normally created AUTOEXEC_Init.
	// But if we are already running (first_shell)
	// we have to update the envirionment to display changes

	if (first_shell) {
		// create a copy as the string will be modified
		std::string::size_type n = buf.size();
		char *buf2 = new char[n + 1];
		safe_strncpy(buf2, buf.c_str(), n + 1);
		if ((strncasecmp(buf2, "set ", 4) == 0) && (strlen(buf2) > 4)) {
			char *after_set = buf2 + 4; // move to variable that is being set
			char *test = strpbrk(after_set, "=");
			if (!test) {
				first_shell->SetEnv(after_set, "");
				return;
			}
			*test++ = 0;
			// If the shell is running/exists update the environment
			first_shell->SetEnv(after_set, test);
		}
		delete[] buf2;
	}
}

void AutoexecObject::InstallBefore(const std::string &in) {
	if (GCC_UNLIKELY(installed))
		E_Exit("autoexec: already created %s", buf.c_str());
	installed = true;
	buf = in;
	autoexec_strings.push_front(buf);
	this->CreateAutoexec();
}

void AutoexecObject::CreateAutoexec(void) {
	/* Remove old autoexec.bat if the shell exists */
	if (first_shell)
		VFILE_Remove("AUTOEXEC.BAT");

	// Create a new autoexec.bat
	autoexec_data[0] = 0;
	size_t auto_len;
	for (auto_it it = autoexec_strings.begin(); it != autoexec_strings.end();
	        it++) {

		std::string linecopy = (*it);
		std::string::size_type offset = 0;
		// Lets have \r\n as line ends in autoexec.bat.
		while (offset < linecopy.length()) {
			std::string::size_type n = linecopy.find("\n", offset);
			if (n == std::string::npos)
				break;
			std::string::size_type rn = linecopy.find("\r\n", offset);
			if (rn != std::string::npos && rn + 1 == n) {
				offset = n + 1;
				continue;
			}
			// \n found without matching \r
			linecopy.replace(n, 1, "\r\n");
			offset = n + 2;
		}

		auto_len = strlen(autoexec_data);
		if ((auto_len + linecopy.length() + 3) > AUTOEXEC_SIZE) {
			E_Exit("SYSTEM:Autoexec.bat file overflow");
		}
		sprintf((autoexec_data + auto_len), "%s\r\n", linecopy.c_str());
	}
	if (first_shell)
		VFILE_Register("AUTOEXEC.BAT", (Bit8u *)autoexec_data,
		               (Bit32u)strlen(autoexec_data));
}

AutoexecObject::~AutoexecObject() {
	if (!installed)
		return;

	// Remove the line from the autoexecbuffer and update environment
	for (auto_it it = autoexec_strings.begin(); it != autoexec_strings.end();) {
		if ((*it) == buf) {
			std::string::size_type n = buf.size();
			char *buf2 = new char[n + 1];
			safe_strncpy(buf2, buf.c_str(), n + 1);
			bool stringset = false;
			// If it's a environment variable remove it from there as well
			if ((strncasecmp(buf2, "set ", 4) == 0) && (strlen(buf2) > 4)) {
				char *after_set = buf2 + 4; // move to variable that is being
				// set
				char *test = strpbrk(after_set, "=");
				if (!test) {
					delete[] buf2;
					continue;
				}
				*test = 0;
				stringset = true;
				// If the shell is running/exists update the environment
				if (first_shell)
					first_shell->SetEnv(after_set, "");
			}
			delete[] buf2;
			if (stringset && first_shell && first_shell->bf &&
			        first_shell->bf->filename.find("AUTOEXEC.BAT") !=
			        std::string::npos) {
				// Replace entry with spaces if it is a set and from
				// autoexec.bat, as else the location counter will be off.
				*it = buf.assign(buf.size(), ' ');
				it++;
			} else {
				it = autoexec_strings.erase(it);
			}
		} else
			it++;
	}
	this->CreateAutoexec();
}

DOS_Shell::DOS_Shell() : Program() {
	input_handle = STDIN;
	echo = true;
	exit = false;
	bf = 0;
	call = false;
	completion_start = NULL;
}

Bitu DOS_Shell::GetRedirection(char *s, char **ifn, char **ofn, bool *append) {

	char *lr = s;
	char *lw = s;
	char ch;
	Bitu num = 0;
	bool quote = false;
	char *t;

	while ((ch = *lr++)) {
		if (quote &&
		        ch != '"') {
			/* don't parse redirection within quotes. Not perfect
			                yet. Escaped quotes will mess the count up */
			*lw++ = ch;
			continue;
		}

		switch (ch) {
		case '"':
			quote = !quote;
			break;
		case '>':
			*append = ((*lr) == '>');
			if (*append)
				lr++;
			lr = ltrim(lr);
			if (*ofn)
				free(*ofn);
			*ofn = lr;
			while (*lr && *lr != ' ' && *lr != '<' && *lr != '|')
				lr++;
			// if it ends on a : => remove it.
			if ((*ofn != lr) && (lr[-1] == ':'))
				lr[-1] = 0;
			//			if(*lr && *(lr+1))
			//				*lr++=0;
			//			else
			//				*lr=0;
			t = (char *)malloc(lr - *ofn + 1);
			safe_strncpy(t, *ofn, lr - *ofn + 1);
			*ofn = t;
			continue;
		case '<':
			if (*ifn)
				free(*ifn);
			lr = ltrim(lr);
			*ifn = lr;
			while (*lr && *lr != ' ' && *lr != '>' && *lr != '|')
				lr++;
			if ((*ifn != lr) && (lr[-1] == ':'))
				lr[-1] = 0;
			//			if(*lr && *(lr+1))
			//				*lr++=0;
			//			else
			//				*lr=0;
			t = (char *)malloc(lr - *ifn + 1);
			safe_strncpy(t, *ifn, lr - *ifn + 1);
			*ifn = t;
			continue;
		case '|':
			ch = 0;
			num++;
		}
		*lw++ = ch;
	}
	*lw = 0;
	return num;
}

void DOS_Shell::ParseLine(char *line) {
	LOG(LOG_EXEC, LOG_ERROR)("Parsing command line: %s", line);
	/* Check for a leading @ */
	if (line[0] == '@')
		line[0] = ' ';
	line = trim(line);

	/* Do redirection and pipe checks */

	char *in = 0;
	char *out = 0;

	Bit16u dummy, dummy2;
	Bit32u bigdummy = 0;
	Bitu num = 0; /* Number of commands in this line */
	bool append;
	bool normalstdin = false;  /* wether stdin/out are open on start. */
	bool normalstdout = false; /* Bug: Assumed is they are "con"      */

	num = GetRedirection(line, &in, &out, &append);
	// if (num>1) LOG_MSG("SHELL: Multiple command on 1 line not supported");
	if (in || out) {
		normalstdin = (psp->GetFileHandle(0) != 0xff);
		normalstdout = (psp->GetFileHandle(1) != 0xff);
	}
	if (in) {
		if (DOS_OpenFile(in, OPEN_READ, &dummy)) { // Test if file exists
			DOS_CloseFile(dummy);
			// LOG_MSG("SHELL: Redirect input from %s",in);
			if (normalstdin)
				DOS_CloseFile(0);                // Close stdin
			DOS_OpenFile(in, OPEN_READ, &dummy); // Open new stdin
		}
	}
	if (out) {
		// LOG_MSG("SHELL: Redirect output to %s",out);
		if (normalstdout)
			DOS_CloseFile(1);
		if (!normalstdin && !in)
			DOS_OpenFile("con", OPEN_READWRITE, &dummy);
		bool status = true;
		/* Create if not exist. Open if exist. Both in read/write mode */
		if (append) {
			if ((status = DOS_OpenFile(out, OPEN_READWRITE, &dummy))) {
				DOS_SeekFile(1, &bigdummy, DOS_SEEK_END);
			} else {
				status = DOS_CreateFile(out, DOS_ATTR_ARCHIVE,
				                        &dummy); // Create if not exists.
			}
		} else {
			status = DOS_OpenFileExtended(out, OPEN_READWRITE, DOS_ATTR_ARCHIVE,
			                              0x12, &dummy, &dummy2);
		}

		if (!status && normalstdout)
			DOS_OpenFile("con", OPEN_READWRITE,
			             &dummy); // Read only file, open con again
		if (!normalstdin && !in)
			DOS_CloseFile(0);
	}
	/* Run the actual command */
	DoCommand(line);
	/* Restore handles */
	if (in) {
		DOS_CloseFile(0);
		if (normalstdin)
			DOS_OpenFile("con", OPEN_READWRITE, &dummy);
		free(in);
	}
	if (out) {
		DOS_CloseFile(1);
		if (!normalstdin)
			DOS_OpenFile("con", OPEN_READWRITE, &dummy);
		if (normalstdout)
			DOS_OpenFile("con", OPEN_READWRITE, &dummy);
		if (!normalstdin)
			DOS_CloseFile(0);
		free(out);
	}
}

void DOS_Shell::RunInternal(void) {
	char input_line[CMD_MAXLINE] = {0};
	while (bf) {
		if (bf->ReadLine(input_line)) {
			if (echo) {
				if (input_line[0] != '@') {
					ShowPrompt();
					WriteOut_NoParsing(input_line);
					WriteOut_NoParsing("\n");
				}
			}
			ParseLine(input_line);
			if (echo)
				WriteOut_NoParsing("\n");
		}
	}
}

void DOS_Shell::Run(void) {
	char input_line[CMD_MAXLINE] = {0};
	std::string line;
	if (cmd->FindStringRemainBegin("/C", line)) {
		strcpy(input_line, line.c_str());
		char *sep = strpbrk(input_line, "\r\n"); // GTA installer
		if (sep)
			*sep = 0;
		DOS_Shell temp;
		temp.echo = echo;
		temp.ParseLine(input_line); // for *.exe *.com  |*.bat creates the bf
		// needed by runinternal;
		temp.RunInternal(); // exits when no bf is found.
		return;
	}
	/* Start a normal shell and check for a first command init */
	if (cmd->FindString("/INIT", line, true)) {
		WriteOut_NoParsing("\x1b[2J\x1b[H");
		WriteOut_NoParsing(MSG_Get("SHELL_STARTUP_BEGIN"));

#if C_DEBUG
		// WriteOut(MSG_Get("SHELL_STARTUP_DEBUG"));
#endif
		// if (machine == MCH_CGA) WriteOut(MSG_Get("SHELL_STARTUP_CGA"));
		// if (machine == MCH_HERC) WriteOut(MSG_Get("SHELL_STARTUP_HERC"));
		// WriteOut_NoParsing(MSG_Get("SHELL_STARTUP_END"));
		// WriteOut_NoParsing(MSG_Get("SHELL_STARTUP_END2"));

		strcpy(input_line, line.c_str());
		line.erase();
		ParseLine(input_line);
	} else {
		WriteOut(MSG_Get("SHELL_STARTUP_SUB"), VERSION);
	}
	do {
		if (bf) {
			if (bf->ReadLine(input_line)) {
				if (echo) {
					if (input_line[0] != '@') {
						ShowPrompt();
						WriteOut_NoParsing(input_line);
						WriteOut_NoParsing("\n");
					};
				};
				ParseLine(input_line);
				if (echo)
					WriteOut("\n");
			}
		} else {
			if (echo)
				ShowPrompt();
			InputCommand(input_line);
			ParseLine(input_line);
			if (echo && !bf)
				WriteOut_NoParsing("\n");
		}
	} while (!exit);
}

void DOS_Shell::SyntaxError(void) {
	WriteOut(MSG_Get("SHELL_SYNTAXERROR"));
}

class AUTOEXEC : public Module_base {
private:
	AutoexecObject autoexec[18];
	AutoexecObject autoexec_echo;

public:
	AUTOEXEC(Section *configuration) : Module_base(configuration) {
		/* Register a virtual AUOEXEC.BAT file */
		std::string line;
		Section_line *section = static_cast<Section_line *>(configuration);


		/* add stuff from the configfile unless -noautexec or -securemode is
		 * specified. */
		char *extra = const_cast<char *>(section->data.c_str());
		if (extra &&
		        !control->cmdline->FindExist("-noautoexec", true)) {
			/* detect if "echo off" is the first line */
			size_t firstline_length = strcspn(extra, "\r\n");
			bool echo_off = !strncasecmp(extra, "echo off", 8);
			if (echo_off && firstline_length == 8)
				extra += 8;
			else {
				echo_off = !strncasecmp(extra, "@echo off", 9);
				if (echo_off && firstline_length == 9)
					extra += 9;
				else
					echo_off = false;
			}
			autoexec_echo.InstallBefore("@echo off");
			/* if "echo off" move it to the front of autoexec.bat */
			if (echo_off) {
				autoexec_echo.InstallBefore("@echo off");
				if (*extra == '\r')
					extra++; // It can point to \0
				if (*extra == '\n')
					extra++; // same
			}

			/* Install the stuff from the configfile if anything left after
			 * moving echo off */

			if (*extra)
				autoexec[0].Install(std::string(extra));
		}

		/* Check to see for extra command line options to be added (before the
		 * command specified on commandline) */
		/* Maximum of extra commands: 10 */
		

		/* Check for the -exit switch which causes NTVDBM to when the command on
		 * the commandline has finished */

		/* Check for first command being a directory or file */
		char buffer[CROSS_LEN + 1];
		char orig[CROSS_LEN + 1];
		char cross_filesplit[2] = {CROSS_FILESPLIT, 0};

		Bitu dummy = 1;
		bool command_found = false;
		/* Skip original processing if boot.dir is set (main() already consumed argv[1]) */
		if (!boot.dir[0]) {
		while (control->cmdline->FindCommand(dummy++, line) && !command_found) {
			struct stat test;
			if (line.length() > CROSS_LEN)
				continue;
			strcpy(buffer, line.c_str());
			if (stat(buffer, &test)) {
				if (getcwd(buffer, CROSS_LEN) == NULL)
					continue;
				if (strlen(buffer) + line.length() + 1 > CROSS_LEN)
					continue;
				strcat(buffer, cross_filesplit);
				strcat(buffer, line.c_str());
				if (stat(buffer, &test))
					continue;
			}
			
			/* Combining -securemode, noautoexec and no parameters leaves you with a
			 * lovely Z:\. */
			if (!command_found) {
				
			}
		} // closes while loop
		} // closes if (!boot.dir[0])
		autoexec[10].Install(std::string("MOUNT W .\\winbox\\"));
		autoexec[11].Install(std::string("MOUNT C c:\\"));
		autoexec[12].Install("W:");
		autoexec[13].Install("call autoexec.bat");
		autoexec[14].Install("CD WINDOWS");
		
		if (boot.dir[0]) {

			char cmd1[4096];
			char cmd2[4096];
			
			sprintf(cmd1, "MOUNT D %s", boot.dir);
			autoexec[15].Install(cmd1);

			sprintf(cmd2, "WIN D:\\%s", boot.exe);
			autoexec[16].Install(cmd2);

		} else {

			autoexec[15].Install("WIN.COM PROGMAN.EXE");
		}
		autoexec[17].Install("EXIT");
		VFILE_Register("AUTOEXEC.BAT", (Bit8u *)autoexec_data,
		               (Bit32u)strlen(autoexec_data));
	}
};

static AUTOEXEC *test;

void AUTOEXEC_Init(Section *sec) {
	test = new AUTOEXEC(sec);
}

static Bitu INT2E_Handler(void) {
	/* Save return address and current process */
	RealPt save_ret = real_readd(SegValue(ss), reg_sp);
	Bit16u save_psp = dos.psp();

	/* Set first shell as process and copy command */
	dos.psp(DOS_FIRST_SHELL);
	DOS_PSP psp(DOS_FIRST_SHELL);
	psp.SetCommandTail(RealMakeSeg(ds, reg_si));
	SegSet16(ss, RealSeg(psp.GetStack()));
	reg_sp = 2046;

	/* Read and fix up command string */
	CommandTail tail;
	MEM_BlockRead(PhysMake(dos.psp(), 128), &tail, 128);
	if (tail.count < 127)
		tail.buffer[tail.count] = 0;
	else
		tail.buffer[126] = 0;
	char *crlf = strpbrk(tail.buffer, "\r\n");
	if (crlf)
		*crlf = 0;

	/* Execute command */
	if (strlen(tail.buffer)) {
		DOS_Shell temp;
		temp.ParseLine(tail.buffer);
		temp.RunInternal();
	}

	/* Restore process and "return" to caller */
	dos.psp(save_psp);
	SegSet16(cs, RealSeg(save_ret));
	reg_ip = RealOff(save_ret);
	reg_ax = 0;
	return CBRET_NONE;
}

static char const *const path_string = "PATH=Z:\\";
static char const *const comspec_string = "COMSPEC=Z:\\COMMAND.COM";
static char const *const full_name = "Z:\\COMMAND.COM";
static char const *const init_line = "/INIT AUTOEXEC.BAT";

void SHELL_Init() {
	/* Add messages */
	MSG_Add("SHELL_ILLEGAL_PATH", "Illegal Path.\n");
	MSG_Add("SHELL_CMD_HELP",
	        "If you want a list of all supported commands type \033[33;1mhelp "
	        "/all\033[0m .\nA short list of the most often used commands:\n");
	MSG_Add("SHELL_CMD_ECHO_ON", "ECHO is on.\n");
	MSG_Add("SHELL_CMD_ECHO_OFF", "ECHO is off.\n");
	MSG_Add("SHELL_ILLEGAL_SWITCH", "Illegal switch: %s.\n");
	MSG_Add("SHELL_MISSING_PARAMETER", "Required parameter missing.\n");
	MSG_Add("SHELL_CMD_CHDIR_ERROR", "Unable to change to: %s.\n");
	MSG_Add("SHELL_CMD_CHDIR_HINT",
	        "Hint: To change to different drive type \033[31m%c:\033[0m\n");
	MSG_Add("SHELL_CMD_CHDIR_HINT_2",
	        "directoryname is longer than 8 characters and/or contains "
	        "spaces.\nTry \033[31mcd %s\033[0m\n");
	MSG_Add("SHELL_CMD_CHDIR_HINT_3",
	        "You are still on drive Z:, change to a mounted drive with "
	        "\033[31mC:\033[0m.\n");
	MSG_Add("SHELL_CMD_DATE_ERROR", "The specified date is not correct.\n");
	MSG_Add("SHELL_CMD_DATE_DAYS",
	        "3SunMonTueWedThuFriSat"); // "2SoMoDiMiDoFrSa"
	MSG_Add("SHELL_CMD_DATE_NOW", "Current date: ");
	MSG_Add("SHELL_CMD_DATE_SETHLP", "Type 'date MM-DD-YYYY' to change.\n");
	MSG_Add("SHELL_CMD_DATE_FORMAT", "M/D/Y");

	MSG_Add("SHELL_CMD_TIME_NOW", "Current time: ");

	MSG_Add("SHELL_CMD_MKDIR_ERROR", "Unable to make: %s.\n");
	MSG_Add("SHELL_CMD_RMDIR_ERROR", "Unable to remove: %s.\n");
	MSG_Add("SHELL_CMD_DEL_ERROR", "Unable to delete: %s.\n");
	MSG_Add("SHELL_SYNTAXERROR", "The syntax of the command is incorrect.\n");
	MSG_Add("SHELL_CMD_SET_NOT_SET", "Environment variable %s not defined.\n");
	MSG_Add("SHELL_CMD_SET_OUT_OF_SPACE",
	        "Not enough environment space left.\n");
	MSG_Add("SHELL_CMD_IF_EXIST_MISSING_FILENAME",
	        "IF EXIST: Missing filename.\n");
	MSG_Add("SHELL_CMD_IF_ERRORLEVEL_MISSING_NUMBER",
	        "IF ERRORLEVEL: Missing number.\n");
	MSG_Add("SHELL_CMD_IF_ERRORLEVEL_INVALID_NUMBER",
	        "IF ERRORLEVEL: Invalid number.\n");
	MSG_Add("SHELL_CMD_GOTO_MISSING_LABEL",
	        "No label supplied to GOTO command.\n");
	MSG_Add("SHELL_CMD_GOTO_LABEL_NOT_FOUND", "GOTO: Label %s not found.\n");
	MSG_Add("SHELL_CMD_FILE_NOT_FOUND", "File %s not found.\n");
	MSG_Add("SHELL_CMD_FILE_EXISTS", "File %s already exists.\n");
	MSG_Add("SHELL_CMD_DIR_INTRO", "Directory of %s.\n");
	MSG_Add("SHELL_CMD_DIR_BYTES_USED", "%5d File(s) %17s Bytes.\n");
	MSG_Add("SHELL_CMD_DIR_BYTES_FREE", "%5d Dir(s)  %17s Bytes free.\n");
	MSG_Add("SHELL_EXECUTE_DRIVE_NOT_FOUND",
	        "Drive %c does not exist!\nYou must \033[31mmount\033[0m it first. "
	        "Type \033[1;33mintro\033[0m or \033[1;33mintro mount\033[0m for "
	        "more information.\n");
	MSG_Add("SHELL_EXECUTE_ILLEGAL_COMMAND", "Illegal command: %s.\n");
	MSG_Add("SHELL_CMD_PAUSE", "Press any key to continue.\n");
	MSG_Add("SHELL_CMD_PAUSE_HELP", "Waits for 1 keystroke to continue.\n");
	MSG_Add("SHELL_CMD_COPY_FAILURE", "Copy failure : %s.\n");
	MSG_Add("SHELL_CMD_COPY_SUCCESS", "   %d File(s) copied.\n");
	MSG_Add("SHELL_CMD_SUBST_NO_REMOVE",
	        "Unable to remove, drive not in use.\n");
	MSG_Add("SHELL_CMD_SUBST_FAILURE",
	        "SUBST failed. You either made an error in your commandline or the "
	        "target drive is already used.\nIt's only possible to use SUBST on "
	        "Local drives");

	MSG_Add(
	    "SHELL_STARTUP_BEGIN",
	    "Winbox version 1.0\033[0m\n"
"                                                                \n"
"                                \x1B[1;30m\xDC\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDC\x1B[0m\n"
"                 \x1B[30m\xDB\xDB\x1B[37m           \x1B[1;30m\xDC\xDF\x1B[37m\xDC\x1B[47m\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\x1B[1;40m\xDF\x1B[0;1;30m\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m\n"
"                 \x1B[30m\xDB\xDB\x1B[37m          \x1B[1;30m\xDB\x1B[0m \xDB\xDB\x1B[1;30;47m\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\x1B[0m\xDB\xDB\x1B[1;30m\xDB\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m\n"
"                 \x1B[30m\xDB\xDB\x1B[37m  \x1B[30m\xDB\x1B[37m       \x1B[1;30m\xDB\x1B[0m \xDB\x1B[1;30m\xDB\x1B[0m \x1B[34m\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\x1B[37m \x1B[1m\xDB\x1B[0m\xDB\x1B[1;30m\xDB\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m\n"
"                \x1B[30m\xDB\xDB\xDB\x1B[37m \x1B[30m\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\x1B[1m\xDB\x1B[0m \xDB\x1B[1;30m\xDB\x1B[0;30m\xDB\x1B[34m\xDB\x1B[1;36m\xDB\x1B[0;34m\xDB\xDB\xDB\xDB\xDB\xDB\xDB\x1B[37m \x1B[1m\xDB\x1B[0m\xDB\x1B[1;30m\xDB\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m\n"
"                \x1B[30m\xDB\xDB\xDB\x1B[37m \x1B[30m\xDB\xDB\xDB\xDB\x1B[37m \x1B[1;30m\xDC\xDF\xDF\xDC\xDB\x1B[0m \xDB\x1B[1;30m\xDB\x1B[0;30m\xDB\x1B[34m\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\x1B[37m \x1B[1m\xDB\x1B[0m\xDB\x1B[1;30m\xDB\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m\n"
"                \x1B[30m\xDB\xDB\xDB\x1B[37m \x1B[30m\xDB\xDB\xDB\x1B[1m\xDC\xDF\x1B[0m\xDC\xDB \x1B[1;30m\xDB\xDF\x1B[37m\xDC\xDC\xDC\xDC\xDC\x1B[0;34m\xDF\xDB\xDB\xDB\xDB\xDB\xDB\xDB\x1B[37m \x1B[1m\xDB\x1B[0m\xDB\x1B[1;30m\xDB\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m    \x1B[30m\xDB\x1B[37m\n"
"                \x1B[30m\xDB\xDB\xDB\x1B[37m  \x1B[1;30m\xDC\xDF\x1B[0m\xDC\xDB\x1B[1;30;47m\xDC\xDF\x1B[0m\xDB\xDC \x1B[1m\xDF\xDF\xDF\xDF\xDF\x1B[0m \xDC\xDC\x1B[1m\xDC\xDC\xDC\xDC\xDC\xDC\x1B[47m\xDF\x1B[0m\xDB\x1B[1;30m\xDB\xDB\x1B[0m \x1B[1;30m\xDF\xDF\xDF\xDF\xDC\x1B[0;30m\xDB\x1B[37m\n"
"                 \x1B[30m\xDB\xDB\x1B[37m \x1B[30m\xDB\x1B[1m\xDB\x1B[0m \x1B[1;30;47m\xDC\xDF\x1B[0m\xDF\xDF\xDF \x1B[1;30m\xDF\x1B[0m\xDB\xDB\xDB\xDF\x1B[34m\xDC\x1B[37m \x1B[1;30m\xDF\xDF\x1B[0m\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\x1B[1;30m\xDF\xDC\xDB\xDB\x1B[47m\xDF\xDC\x1B[40m\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m\n"
"                 \x1B[30m\xDB\xDB\x1B[37m \x1B[30m\xDB\xDB\x1B[1m\xDF\xDC\x1B[0m \x1B[1m\xDB\xDB\xDB\xDB\xDB\x1B[0m \xDF\x1B[34m\xDC\xDB\xDB\x1B[37m \x1B[1;30;47m\xDF\xDF\x1B[37m\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\x1B[0m\xDB\xDB\xDB\x1B[1;30;47m\xDC\x1B[40m\xDB\xDB\xDB\x1B[0m \x1B[1;30m\xDB\x1B[0m\n"
"                \x1B[30m\xDB\xDB\xDB\x1B[37m \x1B[30m\xDB\xDB\xDB\x1B[1m\xDB\x1B[0m \x1B[34m\xDC\xDC\xDC\xDC\xDC\x1B[37m \x1B[34m\xDB\xDB\xDB\x1B[37;44m\xDC\x1B[40m \x1B[1;30m\xDB\xDB\x1B[0m\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDB\x1B[1;30m\xDB\xDB\xDB\xDB\xDF\xDC\xDF\x1B[0m\n"
"                \x1B[30m\xDB\xDB\x1B[37m  \x1B[30m\xDB\xDB\xDB\x1B[1m\xDB\x1B[0m \x1B[34m\xDB\xDB\xDB\xDB\xDB\x1B[37m \x1B[34m\xDB\x1B[37;44m\xDC\x1B[40m\xDB\xDB \x1B[1;30m\xDB\xDB\x1B[0m\xDF   \x1B[1m\xDF\x1B[0m  \x1B[1;30m\xDC\xDB\x1B[47m\xDC\x1B[40m\xDB\xDB\xDF\xDC\xDF\x1B[0;30m\xDB\xDB\xDB\xDB\x1B[37m\n"
"                    \x1B[30m\xDB\xDB\xDB\x1B[1m\xDB\x1B[0m \x1B[1;44m\xDC\xDC\xDC\xDC\xDC\x1B[30;40m\xDC\x1B[0m\xDB\xDB\xDB\xDB \x1B[1;30m\xDF\x1B[0m \x1B[1;33m\xDF\x1B[37m\xDF\x1B[33m\xDF\x1B[37m\xDF\x1B[33m\xDF\x1B[37m\xDF\x1B[33m\xDF\x1B[30m\xDF\xDF\xDF\xDF\xDC\xDF\x1B[0;30m\xDB\xDB\xDB\xDB\xDB\x1B[37m\n"
"                    \x1B[30m\xDB\xDB\xDB\x1B[1m\xDB\x1B[0m \x1B[1m\xDB\xDB\xDB\xDB\xDB\x1B[30m\xDB\x1B[0m\xDB\xDB\xDB\xDB\x1B[30m\xDB\x1B[1m\xDB\xDF\xDF\xDF\xDF\xDF\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDC\xDC\xDC\x1B[0;30m\xDB\xDB\xDB\xDB\xDB\x1B[37m\n"
"                      \x1B[30m\xDB\x1B[1m\xDB\x1B[0m \x1B[1m\xDB\xDB\xDB\xDB\xDB\x1B[30m\xDB\x1B[0m\xDB\xDB\xDF\x1B[1;30m\xDC\xDF\x1B[0;30m\xDB\xDB\xDB\x1B[37m \x1B[1;30m\xDC\xDF\x1B[37m\xDC\x1B[33m\xDC\x1B[37m\xDC\x1B[33m\xDC\x1B[37m\xDC\x1B[33m\xDC\x1B[37m\xDC\x1B[33m\xDC\x1B[0m \x1B[1;30m\xDC\xDF\x1B[0;30m\xDB\xDB\xDB\x1B[37m\n"
"                      \x1B[30m\xDB\x1B[1m\xDB\x1B[0m \x1B[1m\xDB\xDB\xDB\xDB\xDB\x1B[30m\xDB\x1B[0m\xDF\x1B[1;30m\xDC\xDF\x1B[0;30m\xDB\xDB\xDB\xDB\x1B[1m\xDC\xDF\x1B[0m    \x1B[1m\xDC\x1B[0m    \x1B[1;30m\xDC\xDF\x1B[0;30m\xDB\xDB\xDB\xDB\x1B[37m\n"
"                      \x1B[30m\xDB\x1B[1m\xDF\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDF\x1B[1;30m    \x1B[1m\xDC\xDF\x1B[30m         \x1B[1;30m\xDC\xDF\x1B[0;37m\n"
"                       \x1B[30m\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\x1B[1;30m  \xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\x1B\n"
"\x1B[0;37;40m"
"\x1B[37;40m"
"\n"
"        A layer to replace Windows 16 subsystem. By \033[36mricardoramosworks.com\033[0m\n"
"                 Project based on dosbox 0.74-3: \033[36mdosbox.com\033[0m\n"
"\x1B[0m"
"\x1B[37;40m"

);
	MSG_Add("SHELL_STARTUP_SUB",
	        "\n\n\033[32;1mNTVDBM %s Command Shell\033[0m\n\n");

	MSG_Add("SHELL_CMD_CHOICE_HELP_LONG",
	        "CHOICE [/C:choices] [/N] [/S] text\n"
	        "  /C[:]choices  -  Specifies allowable keys.  Default is: yn.\n"
	        "  /N  -  Do not display the choices at end of prompt.\n"
	        "  /S  -  Enables case-sensitive choices to be selected.\n"
	        "  text  -  The text to display as a prompt.\n");

	MSG_Add("SHELL_CMD_VER_HELP", "View and set the reported DOS version.\n");
	MSG_Add("SHELL_CMD_VER_VER",
	        "NTVDBM version %s. Reported DOS version %d.%02d.\n");

	/* Regular startup */
	call_shellstop = CALLBACK_Allocate();
	/* Setup the startup CS:IP to kill the last running machine when exitted */
	RealPt newcsip = CALLBACK_RealPointer(call_shellstop);
	SegSet16(cs, RealSeg(newcsip));
	reg_ip = RealOff(newcsip);

	CALLBACK_Setup(call_shellstop, shellstop_handler, CB_IRET, "shell stop");
	PROGRAMS_MakeFile("COMMAND.COM", SHELL_ProgramStart);

	/* Now call up the shell for the first time */
	Bit16u psp_seg = DOS_FIRST_SHELL;
	Bit16u env_seg = DOS_FIRST_SHELL + 19; // DOS_GetMemory(1+(4096/16))+1;
	Bit16u stack_seg = DOS_GetMemory(2048 / 16);
	SegSet16(ss, stack_seg);
	reg_sp = 2046;

	/* Set up int 24 and psp (Telarium games) */
	real_writeb(psp_seg + 16 + 1, 0, 0xea); /* far jmp */
	real_writed(psp_seg + 16 + 1, 1, real_readd(0, 0x24 * 4));
	real_writed(0, 0x24 * 4, ((Bit32u)psp_seg << 16) | ((16 + 1) << 4));

	/* Set up int 23 to "int 20" in the psp. Fixes what.exe */
	real_writed(0, 0x23 * 4, ((Bit32u)psp_seg << 16));

	/* Set up int 2e handler */
	Bitu call_int2e = CALLBACK_Allocate();
	RealPt addr_int2e = RealMake(psp_seg + 16 + 1, 8);
	CALLBACK_Setup(call_int2e, &INT2E_Handler, CB_IRET_STI,
	               Real2Phys(addr_int2e), "Shell Int 2e");
	RealSetVec(0x2e, addr_int2e);

	/* Setup MCBs */
	DOS_MCB pspmcb((Bit16u)(psp_seg - 1));
	pspmcb.SetPSPSeg(psp_seg); // MCB of the command shell psp
	pspmcb.SetSize(0x10 + 2);
	pspmcb.SetType(0x4d);
	DOS_MCB envmcb((Bit16u)(env_seg - 1));
	envmcb.SetPSPSeg(psp_seg); // MCB of the command shell environment
	envmcb.SetSize(DOS_MEM_START - env_seg);
	envmcb.SetType(0x4d);

	/* Setup environment */
	PhysPt env_write = PhysMake(env_seg, 0);
	MEM_BlockWrite(env_write, path_string, (Bitu)(strlen(path_string) + 1));
	env_write += (PhysPt)(strlen(path_string) + 1);
	MEM_BlockWrite(env_write, comspec_string,
	               (Bitu)(strlen(comspec_string) + 1));
	env_write += (PhysPt)(strlen(comspec_string) + 1);
	mem_writeb(env_write++, 0);
	mem_writew(env_write, 1);
	env_write += 2;
	MEM_BlockWrite(env_write, full_name, (Bitu)(strlen(full_name) + 1));

	DOS_PSP psp(psp_seg);
	psp.MakeNew(0);
	dos.psp(psp_seg);

	/* The start of the filetable in the psp must look like this:
	 * 01 01 01 00 02
	 * In order to achieve this: First open 2 files. Close the first and
	 * duplicate the second (so the entries get 01) */
	Bit16u dummy = 0;
	DOS_OpenFile("CON", OPEN_READWRITE, &dummy); /* STDIN  */
	DOS_OpenFile("CON", OPEN_READWRITE, &dummy); /* STDOUT */
	DOS_CloseFile(0);                            /* Close STDIN */
	DOS_ForceDuplicateEntry(1, 0);               /* "new" STDIN */
	DOS_ForceDuplicateEntry(1, 2);               /* STDERR */
	DOS_OpenFile("CON", OPEN_READWRITE, &dummy); /* STDAUX */
	DOS_OpenFile("PRN", OPEN_READWRITE, &dummy); /* STDPRN */

	/* Create appearance of handle inheritance by first shell */
	for (Bit16u i = 0; i < 5; i++) {
		Bit8u handle = psp.GetFileHandle(i);
		if (Files[handle])
			Files[handle]->AddRef();
	}

	psp.SetParent(psp_seg);
	/* Set the environment */
	psp.SetEnvironment(env_seg);
	/* Set the command line for the shell start up */
	CommandTail tail;
	tail.count = (Bit8u)strlen(init_line);
	memset(&tail.buffer, 0, 127);
	strcpy(tail.buffer, init_line);
	MEM_BlockWrite(PhysMake(psp_seg, 128), &tail, 128);

	/* Setup internal DOS Variables */
	dos.dta(RealMake(psp_seg, 0x80));
	dos.psp(psp_seg);

	SHELL_ProgramStart_First_shell(&first_shell);
	first_shell->Run();
	delete first_shell;
	first_shell = 0; // Make clear that it shouldn't be used anymore
}