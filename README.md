# Winbox
Winbox is a 16-bit layer for running legacy software on Windows without the need for NTVDM.

![Imagem](https://raw.githubusercontent.com/RicardoRamosWorks/ricardoramosworks.github.io/refs/heads/main/Images/Winbox.png)


	888       888 d8b          888
	888   o   888 Y8P          888
	888  d8b  888              888
	888 d888b 888 888 88888b.  88888b.   .d88b.  888  888
	888d88888b888 888 888 "88b 888 "88b d88""88b `Y8bd8P'
	88888P Y88888 888 888  888 888  888 888  888   X88K
	8888P   Y8888 888 888  888 888 d88P Y88..88P .d8""8b.
	888P     Y888 888 888  888 88888P"   "Y88P"  888  888


This is a highly modified fork of dosbox-ece, dosbox 0.74-3, and a bit of dosbox-x low end.

The focus is on making DOSBox extremely lightweight and fast enough to run on something as low as the 1GHz Pentium M of the first-generation Apple TV and 256MB of RAM.

This is the main branch; from it, a dedicated version with exclusive optimizations for the first-generation Apple TV will be forked, and at least one experimental version will be created that has had the SDL window and renderer replaced with Win32 + direct draw.

Any support other than Win32 has been dropped from the project. I do not recommend attempting to compile for any platform other than Win32 without reviewing the code.

The main improvements are in:

- All the code has been reviewed, some things have been corrected to avoid warnings during compilation, everything is standardized in a way that gcc 7 is happy with.

- The console window has been removed, and the logging system has been disabled to improve performance.

- The audio backend (mixer.cpp) has been completely rewritten. SDL added some overhead, so everything works based on wave out (direct x), which guarantees some headroom for the CPU.

- OpenGL has been improved to better fit the 7300M and the driver; instead of drawing a giant triangle and placing the screen texture in it, a rectangle is created. This adds minimal overhead but ensures that it works well with any driver, even older ones.

- The entire dynamic compiler and a good part of the CPU have been rewritten, using the improvements found in the low-end dosbox-x. Whenever possible, I tried to adjust things to 16 or 32 kb to help with the cache. I tried, as much as I could, not to overload the cache, because due to the way the Pentium M works, it will work happily with small chunks.

- Disabled or removed: MT32 emulation, Fluidsynth, Tandy, PCJr, complex shader rendering, complex CPU scalers, splash screen, recording codec support, recording, and other expensive embellishments.

- The configuration file generator has been heavily modified. Help texts have been formatted using ASCII and tabs.

- The dynamic compiler's internal cache has been readjusted to much more conservative levels in order to try to save RAM usage.

## USAGE:

In this Git repository you will only find the code and distribution for ntvdbm.exe. If you are interested in the complete solution to replace NTVDM, visit [NTVDBM Suite](https://github.com/RicardoRamosWorks/NTVDBM-Suite).

## Compiling

You will need Mingw64/MSYS2, and the toolchain available in the dosbox-x source code, which already includes a complete version of gcc-7.

I strongly recommend downloading the SDL 1.2.15 source code and recompiling it without console redirection, to avoid creating stdout.txt and stderr.txt files in the exe directory every time the program starts.

Once your toolchain is ready, simply run ./autogen.sh (it's modified to export the optimization flags for Pentium M) and run ./configure with the appropriate arguments, and make using all processor cores, as well as renaming the exe from dosbox.exe to ntvdbm.exe.

## Compatibility

All the games I tested from 1987 to 1999 ran perfectly (in the sense that there were no problems with the handler), I believe I achieved 100% compatibility with anything that should run in DOSBox.

Win32 executables are passed to the kernel normally; I believe I've fixed the problem with too many arguments in the call, so the handler (so far) no longer needs to be disabled for compatibility. The Shortcut Creator also differentiates between MSDOS icons and Windows icons when creating the generic icon.

Unfortunately, this toolchain is Posyx and works directly with libwinpthread-1.dll. As far as I know, there's no way to compile it statically, so the DLL in the executable directory is a requirement.

