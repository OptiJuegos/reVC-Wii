#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <unistd.h>

#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "common.h"
#include "crossplatform.h"
#include "audio_enums.h"
#include "DMAudio.h"
#include "FileMgr.h"
#include "Frontend.h"
#include "Game.h"
#include "main.h"
#include "Pad.h"
#include "PCSave.h"
#include "platform.h"
#include "skeleton.h"
#include "WiiLog.h"
#include "WiiPad.h"
#include "WiiTrace.h"

extern volatile int32 frameCount;

// Declared here rather than by including librw's private gx header, the same way
// TexRead.cpp reaches its sibling setNativeTextureTrace.
namespace rw { namespace gx { void setFrameTrace(bool32 enabled); } }

namespace
{


// MUY BIEN HARDCODEANDO COSAS 
constexpr unsigned int kFifoSize = 256 * 1024;
constexpr const char *kInstallDirectories[] = {
	"sd:/apps/reVC", "usb:/apps/reVC", "usb2:/apps/reVC",
	"usb3:/apps/reVC", "usb4:/apps/reVC"
};

GXRModeObj *s_renderMode;
void *s_frameBuffers[2];
char s_installDirectory[128] = "sd:/apps/reVC";

void
onVerticalRetrace(u32)
{
	frameCount++;
}

bool
fileExists(const char *path)
{
	FILE *file = std::fopen(path, "rb");
	if(file == nullptr)
		return false;
	std::fclose(file);
	return true;
}

// Everything up to the last separator of argv[0], which a Wii loader fills in
// with the ELF's own path (usb:/apps/gtavc/boot.dol and the like).
bool
elfDirectory(int argc, char **argv, char *out, size_t size)
{
	if(argc <= 0 || argv == nullptr || argv[0] == nullptr)
		return false;
	const char *lastSeparator = std::strrchr(argv[0], '/');
	if(lastSeparator == nullptr)
		return false;
	const size_t length = (size_t)(lastSeparator - argv[0]);
	if(length == 0 || length >= size)
		return false;
	std::memcpy(out, argv[0], length);
	out[length] = '\0';
	return true;
}

// Each miss is reported rather than counted, because the failure this guards
// against is a correct install under a name the list below cannot know, and the
// paths actually tried are the only thing that distinguishes that from data that
// is genuinely absent.
bool
tryInstallDirectory(const char *directory)
{
	char path[192];
	std::snprintf(path, sizeof(path), "%s/DATA/GTA_VC.DAT", directory);
	if(!fileExists(path)){
		WiiTraceReport("WII game boot: no data at %s\n", path);
		return false;
	}
	if(chdir(directory) != 0){
		WiiTraceReport("WII game boot: chdir failed for %s\n", directory);
		return false;
	}

	std::snprintf(s_installDirectory, sizeof(s_installDirectory), "%s", directory);
	WiiTraceReport("WII game boot: install=%s\n", directory);
	return true;
}

bool
selectInstallDirectory(const char *launchDirectory)
{
	// Where the ELF was launched from comes first.  The data sits beside it for
	// anyone who did not name the folder reVC, and the list below cannot know
	// what they did name it -- it only covers a loader that passed no argv for
	// that directory to be derived from.
	if(launchDirectory != nullptr && tryInstallDirectory(launchDirectory))
		return true;

	for(const char *directory : kInstallDirectories)
		if(tryInstallDirectory(directory))
			return true;
	
	// deberia tirar algun mensaje en vez de un logging para la gente gaga en el dolphin!!
	WiiTraceReport("WII game boot: DATA/GTA_VC.DAT not found\n");
	return false;
}

bool
initializeVideo()
{
	VIDEO_Init();
	s_renderMode = VIDEO_GetPreferredMode(nullptr);
	for(void *&frameBuffer : s_frameBuffers){
		frameBuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_renderMode));
		if(frameBuffer == nullptr)
			return false;

		// ESTA MIERDA SE COLOCA PORQUE SI NO SE BUGEAN TODOS LOS GRAFICOS por el framebuffer que viene ya garchado de los menus de la wii
		// en dolphin no pasa
		VIDEO_ClearFrameBuffer(s_renderMode, frameBuffer, COLOR_BLACK);
	}

	VIDEO_Configure(s_renderMode);
	VIDEO_SetNextFramebuffer(s_frameBuffers[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_SetPostRetraceCallback(onVerticalRetrace);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(s_renderMode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	void *fifo = memalign(32, kFifoSize);
	if(fifo == nullptr)
		return false;
	std::memset(fifo, 0, kFifoSize);
	GX_Init(fifo, kFifoSize);

	// para el background bien
	GXColor background = { 0, 0, 0, 255 };
	GX_SetCopyClear(background, GX_MAX_Z24);
	GX_SetViewport(0.0f, 0.0f, s_renderMode->fbWidth,
	               s_renderMode->efbHeight, 0.0f, 1.0f);
	GX_SetDispCopyYScale((f32)s_renderMode->xfbHeight/(f32)s_renderMode->efbHeight);
	GX_SetScissor(0, 0, s_renderMode->fbWidth, s_renderMode->efbHeight);
	GX_SetDispCopySrc(0, 0, s_renderMode->fbWidth, s_renderMode->efbHeight);
	GX_SetDispCopyDst(s_renderMode->fbWidth, s_renderMode->xfbHeight);
	GX_SetCopyFilter(s_renderMode->aa, s_renderMode->sample_pattern,
	                 GX_TRUE, s_renderMode->vfilter);
	GX_SetFieldMode(s_renderMode->field_rendering,
	                s_renderMode->viHeight == 2*s_renderMode->xfbHeight ?
	                GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_CopyDisp(s_frameBuffers[1], GX_TRUE);

	return true;
}

[[noreturn]] void
haltBoot(const char *stage)
{
	WiiTraceReport("WII game boot: halted at %s\n", stage);
	// Nothing after this ever runs, and the watchdog that would otherwise commit
	// the log may not have been started yet at this point in the boot, so the
	// reason for the halt is written out here or not at all.
	WiiTraceCloseLog();
	while(true)
		VIDEO_WaitVSync();
}

} // namespace

// Overrides libogc's weak symbol to make Arena2 the one and only sbrk arena.
// At its default of 0 the whole heap is what is left of MEM1 once the ELF, the
// two framebuffers and the GX FIFO are paid for, and the watchdog's heap line
// shows that running out: arena1 at zero with MEM2 untouched beside it.
//
// The cost is latency, because MEM2 is GDDR3 and everything malloc'd moves
// there.  If that ever measures badly the answer is a small MEM1 allocator for
// chosen buffers, not flipping this back.
//
// It has to be initialised data rather than something a constructor assigns:
// allocations happen during libc and static init, before any constructor of
// ours could run.  The extern "C" block is what keeps it from becoming a
// mangled C++ symbol that would not override anything.
extern "C" {
u32 MALLOC_MEM2 = 1;
}

long _dwOperatingSystemVersion = 0;
size_t _dwMemAvailPhys = 48 * 1024 * 1024;
RwUInt32 gGameState = 0;

extern "C" void
wiiLog(const char *format, ...)
{
#if !CREATE_LOG
	// The one that actually costs something.  The engine calls this for every
	// model, texture, collision file and audio stream it touches, and streaming
	// touches thousands of them while driving -- each one formatting into the
	// 512 byte buffer below before anything decides whether it is wanted.
	// Leaving early is what makes CREATE_LOG 0 worth switching to.
	(void)format;
#else
	
	// este logging de depuracion es una crotada pero funciona
	static const char *const suppressedPrefixes[] = {
		"WII animation:",
		"WII level file:",
		"WII level IPL",
		"WII clump:",
		"WII clump finalize:",
		"WII vehicle finalize:",
		"WII stream finalize:"
	};

	char message[512];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);

	// Note before suppressing.  The suppressed lines are the per-file and
	// per-model ones, which is exactly the progress the watchdog needs to tell
	// a slow load from a stalled one; they are only too frequent to print.
	WiiTraceNote(message);

	// The file gets them too, for the same reason and against the same cost.
	// SYS_Report goes to a cable someone is watching, where the per-model lines
	// are noise; debug.log is read afterwards to find out which model a load
	// stopped on, and suppressing them there would drop the answer.
	WiiTraceLogLine(message);

	for(size_t i = 0; i < ARRAY_SIZE(suppressedPrefixes); i++)
		if(std::strncmp(format, suppressedPrefixes[i], std::strlen(suppressedPrefixes[i])) == 0)
			return;

	SYS_Report("%s", message);
#endif
}

double
psTimer(void)
{
	return ticks_to_millisecs(gettime());
}

RwBool
psInitialize(void)
{
	RsGlobal.ps = nullptr;
	RsGlobal.maximumWidth = s_renderMode->fbWidth;
	RsGlobal.maximumHeight = s_renderMode->efbHeight;
	RsGlobal.width = s_renderMode->fbWidth;
	RsGlobal.height = s_renderMode->efbHeight;
	CFileMgr::Initialise();

	// librw is built as its own target and cannot see CREATE_LOG, so the switch
	// is carried across here rather than compiled in over there.
	rw::gx::setFrameTrace(CREATE_LOG != 0);

	// Saves land beside the game data, which is where the desktop skeletons put
	// them too -- glfw.cpp, sdl2.cpp and win.cpp all make this same call from
	// their own initialisation.  Without it DefaultPCSaveFileName stays empty and
	// every slot is written to a bare "1.b" in whatever the current directory
	// happens to be.  The directory is read straight off the variable rather than
	// through _psGetUserFilesFolder, which returns it but is defined further down
	// with the rest of the platform hooks.
	C_PcSave::SetSaveDirectory(s_installDirectory);
	return TRUE;
}

void psTerminate(void) {}

void
psCameraShowRaster(RwCamera *camera)
{
	RwCameraShowRaster(camera, nullptr, rwRASTERFLIPWAITVSYNC);
}

RwBool
psCameraBeginUpdate(RwCamera *camera)
{
	return RwCameraBeginUpdate(camera) != nullptr;
}

RwImage*
psGrabScreen(RwCamera *camera)
{
	rw::Image *image = RwCameraGetRaster(camera)->toImage();
	if(image)
		image->removeMask();
	return image;
}

void psMouseSetPos(RwV2d *) {}
RwBool psSelectDevice() { return TRUE; }
RwMemoryFunctions *psGetMemoryFunctions(void) { return nullptr; }
RwBool psInstallFileSystem(void) { return TRUE; }
RwBool psNativeTextureSupport(void) { return TRUE; }
const char *_psGetUserFilesFolder() { return s_installDirectory; }
void _InputTranslateShiftKeyUpDown(RsKeyCodes *) {}
long _InputInitialiseMouse(bool) { return 0; }
void _InputShutdownMouse() {}
bool _InputMouseNeedsExclusive() { return false; }
void _InputInitialiseJoys() {}
void InitialiseLanguage() {}
void _psSelectScreenVM(RwInt32) {}
RwBool _psSetVideoMode(RwInt32, RwInt32) { return TRUE; }
RwChar **_psGetVideoModeList() { return nullptr; }
RwInt32 _psGetNumVideModes() { return 1; }

void
CapturePad(RwInt32 padID)
{
	if(padID < 0 || padID >= MAX_PADS)
		return;

	CPad *pad = CPad::GetPad(padID);
	CControllerState &state = pad->PCTempJoyState;
	state.Clear();
	WiiPadCapture(padID, state);
}

// The single exit door, and the reason it is worth a log line of its own: a run
// that ends here wrote this line, and a run that ended any other way -- an
// unhandled exception, a failed allocation, a stack that ran off the end -- did
// not.  So whether debug.log ends with this or simply stops mid-file is what
// separates "something asked the game to quit" from "the game died", which are
// entirely different investigations.
void
HandleExit()
{
	if(!RsGlobal.quit)
		WiiTraceReport("WII game boot: exit requested\n");
	RsGlobal.quit = TRUE;
}

namespace
{

// The console's own buttons, and the Wiimote's power button.  They are
// registered rather than polled because the reset button and both power buttons
// are delivered as callbacks and never appear in any pad state, so there is
// nothing to poll for.
//
// All three go through HandleExit rather than exiting on the spot: it raises the
// same RsGlobal.quit the frontend's Quit option raises, so the main loop below
// unwinds and shuts the game down the one way it already knows how.  Calling
// SYS_ResetSystem from inside the callback would cut the frame in half instead.
// Each names itself first, because HandleExit cannot tell who called it and
// these three are indistinguishable from a menu Quit once the flag is set.
void
onResetButton(u32, void*)
{
	WiiTraceReport("WII system: reset button\n");
	HandleExit();
}

void
onPowerButton(void)
{
	WiiTraceReport("WII system: power button\n");
	HandleExit();
}

void
onWiimotePowerButton(s32)
{
	WiiTraceReport("WII system: wiimote power button\n");
	HandleExit();
}

} // namespace

int
main(int argc, char **argv)
{
	if(!initializeVideo()){
		WiiTraceReport("WII game boot: video=failed\n");
		haltBoot("video");
	}
	if(!fatInitDefault())
		haltBoot("storage mount");

	char launchPath[128];
	const char *launchDirectory =
		elfDirectory(argc, argv, launchPath, sizeof(launchPath)) ? launchPath : nullptr;

	// As early as storage allows, so a boot that never reaches the frontend still
	// leaves the reason behind.  The second call covers a loader that passed no
	// argv, and is a no-op once the first has already opened the file.
	WiiTraceOpenLog(launchDirectory);
	if(!selectInstallDirectory(launchDirectory))
		haltBoot("game data lookup");
	WiiTraceOpenLog(s_installDirectory);

	// psInitialize stores exactly these two into RsGlobal, and the pointer has to
	// report against the same pair, so both are taken from the render mode here
	// rather than from RsGlobal, which is still empty this early.
	WiiPadInitialise(s_renderMode->fbWidth, s_renderMode->efbHeight);
	SYS_SetResetCallback(onResetButton);
	SYS_SetPowerCallback(onPowerButton);
	WPAD_SetPowerButtonCallback(onWiimotePowerButton);
	WiiTraceStartWatchdog();
	WiiTraceHeap("boot");

	WiiTraceReport("WII game boot: rsINITIALIZE\n");
	if(RsEventHandler(rsINITIALIZE, nullptr) == rsEVENTERROR)
		haltBoot("rsINITIALIZE");

	rw::EngineOpenParams openParameters = {
		{ s_frameBuffers[0], s_frameBuffers[1] },
		s_renderMode->fbWidth,
		s_renderMode->efbHeight
	};
	WiiTraceReport("WII game boot: rsRWINITIALIZE\n");
	if(RsEventHandler(rsRWINITIALIZE, &openParameters) == rsEVENTERROR){
		WiiTraceReport("WII game boot: rsRWINITIALIZE=failed\n");
		haltBoot("rsRWINITIALIZE");
	}

	WiiTraceReport("WII game boot: RenderWare initialized\n");
	WiiTraceReport("WII game boot: InitialiseOnceAfterRW\n");
	if(!CGame::InitialiseOnceAfterRW())
		haltBoot("InitialiseOnceAfterRW");
	WiiTraceReport("WII game boot: core services initialized\n");

	// The game state machine the PC skeleton drives from its message loop.
	// The movie and PS2 memory card states have no counterpart here, so boot
	// straight into the frontend, which loads the game data only once a game
	// is actually started.
	FrontEndMenuManager.m_bGameNotLoaded = true;
	FrontEndMenuManager.m_bStartUpFrontEndRequested = true;
	gGameState = GS_FRONTEND;
	WiiTraceReport("WII game boot: entering frontend\n");

	while(!RsGlobal.quit){
		switch(gGameState){
		case GS_FRONTEND:
			RsEventHandler(rsFRONTENDIDLE, nullptr);
			if(FrontEndMenuManager.m_bWantToLoad){
				WiiTraceReport("WII game boot: loading saved game\n");
				WiiTraceHeap("pre-load");

				// Three steps and not one, which is what this used to be.
				// InitialiseGame alone is CGame::Initialise, and that is a NEW
				// game: it never reaches GenericLoad, so picking a save slot
				// started the story over from the beginning.
				//
				// The save is restored into the pools, streaming and world that
				// CGame::Initialise builds, so that still has to run first -- on
				// the desktop skeletons it already has by this point, because they
				// initialise before showing the menu, while this one boots
				// straight into the frontend and loads nothing until asked.
				// ShutDownForRestart then clears what Initialise placed in the
				// world, and InitialiseWhenRestarting is the one that actually
				// reads the slot.  Same order as glfw.cpp and sdl2.cpp.
				InitialiseGame();
				CGame::ShutDownForRestart();
				CGame::InitialiseWhenRestarting();
				DMAudio.ChangeMusicMode(MUSICMODE_GAME);

				FrontEndMenuManager.m_bGameNotLoaded = false;
				// Cleared here as well.  Leaving it set makes every later restart
				// look like another load request.
				FrontEndMenuManager.m_bWantToLoad = false;
				FrontEndMenuManager.m_bWantToRestart = false;
				gGameState = GS_PLAYING_GAME;

				WiiTraceHeap("post-load");
				WiiTraceReport("WII game boot: save loaded, entering game\n");
			}else if(!FrontEndMenuManager.m_bMenuActive)
				gGameState = GS_INIT_PLAYING_GAME;
			break;

		case GS_INIT_PLAYING_GAME:
			WiiTraceReport("WII game boot: loading DATA/GTA_VC.DAT\n");
			// Brackets the load, so what the frontend left behind and what the
			// world costs can be read off against the boot line.
			WiiTraceHeap("pre-load");
			InitialiseGame();
			FrontEndMenuManager.m_bGameNotLoaded = false;
			// Starting a game leaves this set so the desktop skeleton can run its
			// outer restart loop.  The Wii skeleton completes that transition here.
			FrontEndMenuManager.m_bWantToRestart = false;
			gGameState = GS_PLAYING_GAME;
			WiiTraceHeap("post-load");
			WiiTraceReport("WII game boot: game data initialized\n");
			break;

		case GS_PLAYING_GAME:
			RsEventHandler(rsIDLE, (void*)TRUE);
			break;

		default:
			break;
		}
	}

	WiiTraceReport("WII game boot: exiting\n");
	WiiTraceCloseLog();
	return 0;
}
