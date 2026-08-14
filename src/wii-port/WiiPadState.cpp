// WiiPadState.cpp — reads the Wii's controllers and feeds lwjgl::Keyboard /
// lwjgl::Mouse, the same way src/ps2/Ps2PadState.cpp does for the DualShock.
//
// Supported, all at the same time and with no build-time choice between them:
// GameCube pad, Classic Controller, and Wiimote (+ Nunchuk).
//
// One virtual key mask, not four
// ------------------------------
// The earlier version had each device push key events directly, against its own
// remembered button mask, and arbitrated by letting the GameCube pad claim the
// whole frame when it looked "active" -- which meant an idle pad with a couple
// of counts of stick drift could starve the Wiimote completely. That is a bug
// factory: two devices bound to the same key fight over the up/down edges, and
// any device that loses a frame leaves its keys stuck down.
//
// Instead every device now ORs into ONE mask of virtual actions per frame, and
// the edges are taken once, at the end, against the previous frame's mask. A
// key is down if *any* device asks for it, releases when none do, and no device
// can starve another. Movement is likewise summed into a single vector and
// pushed once.
//
// Absolute IR versus relative camera
// ----------------------------------
// The genuinely hard part of this port: IR is ABSOLUTE and BOUNDED (it says
// where on the screen you are pointing) while the camera is RELATIVE and
// UNBOUNDED (it wants how far to turn, and it can turn forever).
//
// Differencing successive pointer positions -- what this file used to do -- is
// the obvious translation and it is wrong in a specific, reported way: the total
// camera travel available is one screen width. Point up-right and hold, and the
// view swings until the pointer reaches the edge of the virtual resolution and
// then stops, because a clamped pointer produces no more deltas. There is no way
// to keep turning without physically sweeping back and forth.
//
// So in game the offset from the centre of the screen is read as a RATE instead:
// direction and speed, not distance. Hold the Wiimote up-right and the camera
// keeps going up-right, faster the further out you point, and the direction of
// the turn matches the direction you are pointing at every angle. This is the
// "virtual joystick" / bounding-box camera every Wii FPS converges on.
//
// In a MENU the pointer stays absolute -- there it really is a cursor, and
// pointing at a button has to put the cursor on that button. lwjgl::Mouse's
// grabbed flag is what separates the two, and it is the engine's own notion of
// "the game owns the mouse", set by MouseHelper::grabMouseCursor.
//
// Read the coordinate-space note in the IR block before touching it: swapping
// ir.x/ir.y for the smoothed pair looks like free noise reduction and silently
// pins the camera to the middle of the screen.
#ifdef WII_PLATFORM

#include <cmath>
#include <cstdio>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "lwjgl/Keyboard.h"
#include "lwjgl/Mouse.h"
#include "net/minecraft/src/VirtualKeyboard.h"
#include "wii/gx_wii.h"
#include "WiiPadState.h"

static WiiTextInputSnapshot s_wiiTextInputSnapshot = {};
static u32 s_wiiTextInputLastHeld = 0;
// Built once per poll from every supported controller, then converted to
// edges after all devices have contributed.  This lets a GameCube pad and a
// Wiimote share the on-screen keyboard without either one cancelling the
// other's held button.
static u32 s_wiiTextInputHeld = 0;

void wiiTextInputSnapshotUpdate(u32 held)
{
	s_wiiTextInputSnapshot.held = held;
	s_wiiTextInputSnapshot.pressed = held & ~s_wiiTextInputLastHeld;
	s_wiiTextInputLastHeld = held;
}

WiiTextInputSnapshot wiiTextInputSnapshot()
{
	return s_wiiTextInputSnapshot;
}

namespace
{

// --- tuning -----------------------------------------------------------------
// Sticks report roughly -1..1 after normalisation. Below this they are noise:
// an uncalibrated Classic Controller rests a few percent off centre and would
// otherwise walk the player forwards forever.
constexpr float kStickDeadzone = 0.20f;
constexpr float kDegToRad      = 0.017453292519943295f;

// --- look / camera tuning ----------------------------------------------------
//
// The engine's conversion, so these numbers can be written in units that mean
// something: Entity::turnEntity applies 0.15 degrees per unit of mouse delta,
// and EntityRenderer multiplies by (sensitivity*0.6+0.2)^3 * 8, which is exactly
// 1.0 at the default GameSettings::mouseSensitivity of 0.5. So one virtual mouse
// pixel is 0.15 degrees of yaw at the default setting, and the Options slider
// still scales everything below.
constexpr float kDegreesPerMousePixel = 0.15f;

// Turn rate at full deflection, degrees per second. Per SECOND and not per
// frame: this port's frame rate moves with the render distance and the scene,
// and a per-frame constant means the camera turns twice as fast in an empty
// cave as it does looking at trees, which reads as the sensitivity changing by
// itself.
constexpr float kStickLookDegPerSec = 160.0f;
constexpr float kIrLookDegPerSec    = 180.0f;

// Stick-driven cursor speed while a GUI is open, in screen pixels per second.
// Unrelated to the camera: here the stick is steering a pointer across a
// 640x480 screen, so it is measured in that screen's own units.
constexpr float kCursorPixelsPerSec = 750.0f;

// --- the IR rate curve -------------------------------------------------------
// All three are fractions of half the screen HEIGHT (see applyIrLook for why the
// height and not each axis' own extent).
//
//   deadzone    how far off centre you can point before the camera moves at all.
//               Has to cover ordinary hand shake plus the couple of pixels of
//               jitter the sensor itself has, or the view drifts while you hold
//               still. Too big and the camera feels stuck.
//   saturation  where the turn rate reaches its maximum. Deliberately short of
//               1.0: the pointer becomes unreliable near the edge of the
//               sensor's field, and needing to aim there to turn quickly is what
//               makes a rate camera feel like it is fighting you.
//   curveLinear how much of the response is linear rather than quadratic. The
//               quadratic half is what makes small offsets slow enough to line
//               up a block face; the linear half is what stops the first third
//               of the travel from doing nothing perceptible.
constexpr float kIrDeadzone    = 0.16f;
constexpr float kIrSaturation  = 0.85f;
constexpr float kIrCurveLinear = 0.35f;

// Set to false to go back to the old 1:1 differencing camera in game. The menu
// path is the same code either way, so this costs no dead branch -- it only
// picks which of the two IR meanings applies while the game has the mouse.
constexpr bool kIrRateCamera = true;

// Frame-time clamp. A dt of zero (two polls inside one timebase tick) would
// freeze the camera, and a huge one -- the first frame after a chunk-load stall
// -- would fling it, so the rate is only integrated over plausible frame times.
//
// The ceiling is 1/15 s and not something rounder because of a limit further
// down the pipe: MouseHelper::mouseXYChange discards anything past 80 units in
// one frame as a warp spike. kIrLookDegPerSec / kDegreesPerMousePixel is 1200
// units per second, and 1200/15 is exactly 80 -- integrating over a longer frame
// than that would silently lose turn speed at precisely the moment the game is
// already struggling, which reads as the camera getting heavier under load.
constexpr float kMinLookDt = 1.0f / 240.0f;
constexpr float kMaxLookDt = 1.0f / 15.0f;

// --- virtual actions --------------------------------------------------------
// Every device maps into this set, and only this set reaches the game. Adding a
// binding is then a one-line change in one device's poll function, and two
// devices sharing a binding costs nothing.
enum VirtualKey
{
	VK_FORWARD = 0,
	VK_BACK,
	VK_LEFT,
	VK_RIGHT,
	VK_JUMP,
	VK_SNEAK,
	VK_INVENTORY,
	VK_DROP,
	VK_ESCAPE,
	VK_DEBUG,        // F3 -- the FPS / position / chunk readout
	VK_THIRDPERSON,  // F5 -- third-person toggle. GameCube pad: D-pad down.
	VK_ARROW_UP,
	VK_ARROW_DOWN,
	VK_ARROW_LEFT,
	VK_ARROW_RIGHT,
	VK_COUNT
};

// Indexed by VirtualKey. Kept in the same order as the enum.
const int kVirtualKeyCodes[VK_COUNT] = {
	lwjgl::Keyboard::KEY_W,
	lwjgl::Keyboard::KEY_S,
	lwjgl::Keyboard::KEY_A,
	lwjgl::Keyboard::KEY_D,
	lwjgl::Keyboard::KEY_SPACE,
	lwjgl::Keyboard::KEY_LSHIFT,
	lwjgl::Keyboard::KEY_E,
	lwjgl::Keyboard::KEY_Q,
	lwjgl::Keyboard::KEY_ESCAPE,
	lwjgl::Keyboard::KEY_F3,
	lwjgl::Keyboard::KEY_F5,
	lwjgl::Keyboard::KEY_UP,
	lwjgl::Keyboard::KEY_DOWN,
	lwjgl::Keyboard::KEY_LEFT,
	lwjgl::Keyboard::KEY_RIGHT,
};

// Mouse buttons, same idea. 0 = attack/mine, 1 = place/use, matching the
// desktop bindings the shared game code already listens to.
constexpr u32 VM_ATTACK = 1u << 0;
constexpr u32 VM_USE    = 1u << 1;

// --- per-frame accumulators -------------------------------------------------
u32   g_keys      = 0;   // built up during a poll, compared at the end
u32   g_mouse     = 0;
float g_moveX     = 0.0f;
float g_moveY     = 0.0f;
float g_lookX     = 0.0f;   // sticks, normalised -1..1
float g_lookY     = 0.0f;
float g_lookPxX   = 0.0f;   // IR, already converted to virtual mouse pixels
float g_lookPxY   = 0.0f;
int   g_wheel     = 0;

// --- state that has to survive between frames -------------------------------
u32 g_prevKeys  = 0;
u32 g_prevMouse = 0;

// Measured frame time, seconds, for the rate-based look. Seeded at 60 Hz so the
// first frame is not a special case.
float g_lookDt       = 1.0f / 60.0f;
u64   g_lookLastTime = 0;

// Sub-pixel remainder of the look output. The engine takes integer mouse deltas,
// and a rate camera spends most of its time asking for well under one pixel per
// frame just outside the dead zone -- truncating each frame independently would
// round all of that to zero and leave a band where slow, precise aiming simply
// does not work.
float g_lookResidX = 0.0f;
float g_lookResidY = 0.0f;

// --- diagnostics ------------------------------------------------------------
// A snapshot of what the hardware said this frame, rendered into the F3 overlay
// by GuiIngame. There is no debugger on a Wii mid-game and no host console once
// GX owns the framebuffer, so without this a controller that misbehaves on
// someone else's hardware can only be diagnosed by guessing -- and the two
// things worth seeing (is the expansion detected at all, and are its calibrated
// polar values sane) are exactly the ones that are invisible from the outside.
struct PadDebug
{
	bool  gcConnected;
	u16   gcButtons;
	int   gcStickX, gcStickY;

	bool  wmConnected;
	u32   wmButtons;
	int   expType;
	int   expProbed;      // what WPAD_Probe said, before the exp.type fallback

	float jsAng, jsMag;
	int   jsRawX, jsRawY;
	int   jsCenterX, jsCenterY;

	bool  irValid;
	int   irX, irY;       // bounded coords, i.e. the WPAD_SetVRes space
	int   irDots;
	int   irMag;          // rate-camera deflection, percent of full
};
PadDebug g_dbg;
char g_dbgLine[224];

bool g_haveIrPrev = false;
int  g_irPrevX = 0;
int  g_irPrevY = 0;

int g_cursorX = 0;
int g_cursorY = 0;

inline void setKey(VirtualKey key, bool down)
{
	if (down) g_keys |= 1u << key;
}

inline void setMouse(u32 bit, bool down)
{
	if (down) g_mouse |= bit;
}

float applyDeadzone(float v)
{
	if (v > -kStickDeadzone && v < kStickDeadzone) return 0.0f;
	// Rescale so motion starts at zero just outside the deadzone instead of
	// jumping straight to 20% speed.
	const float sign = (v < 0.0f) ? -1.0f : 1.0f;
	float out = sign * ((std::fabs(v) - kStickDeadzone) / (1.0f - kStickDeadzone));
	if (out >  1.0f) out =  1.0f;
	if (out < -1.0f) out = -1.0f;
	return out;
}

// Reads one wiiuse joystick (Nunchuk stick, or either Classic stick) into a
// -1..1 vector, with +Y meaning "forward/up".
//
// libogc hands back polar coordinates: js.ang in degrees clockwise from "up",
// js.mag normalised 0..1. Those are derived from a calibration block read during
// the expansion handshake, and that is the part that goes wrong in practice: on
// a third-party Nunchuk, or when the handshake is still in flight, the
// centre/min/max come back as zeros and the polar pair is then either stuck at
// zero (the stick does nothing at all) or wildly out of range (the player walks
// in one direction forever). Both are reported as "the nunchuk doesn't work".
//
// So the polar values are used when they are sane, and the raw byte position
// against the calibrated centre is the fallback. The fallback is also what runs
// for a genuinely centred stick, where it correctly produces zero, so this
// needs no "is the calibration broken" flag anywhere.
void readJoystick(const joystick_t &js, float &outX, float &outY)
{
	outX = 0.0f;
	outY = 0.0f;

	const float mag = (js.mag > 1.0f) ? 1.0f : js.mag;
	if (mag > 0.0f && std::isfinite(mag) && std::isfinite(js.ang))
	{
		const float rad = js.ang * kDegToRad;
		outX = std::sin(rad) * mag;
		outY = std::cos(rad) * mag;
		return;
	}

	// Raw fallback. pos/center/min/max are unsigned bytes; a centre of zero
	// means no calibration was ever read, and there is nothing to measure
	// against, so report centred rather than invent a direction.
	const int cx = js.center.x;
	const int cy = js.center.y;
	if (cx == 0 && cy == 0)
		return;

	// Half of the calibrated travel, or the Nunchuk's nominal ~±100 counts when
	// the calibration block is obviously not usable.
	const float rangeX = (js.max.x > js.min.x) ? (js.max.x - js.min.x) * 0.5f : 100.0f;
	const float rangeY = (js.max.y > js.min.y) ? (js.max.y - js.min.y) * 0.5f : 100.0f;

	outX = ((float)js.pos.x - (float)cx) / rangeX;
	outY = ((float)js.pos.y - (float)cy) / rangeY;

	if (outX >  1.0f) outX =  1.0f; else if (outX < -1.0f) outX = -1.0f;
	if (outY >  1.0f) outY =  1.0f; else if (outY < -1.0f) outY = -1.0f;
}

// --- GameCube pad -----------------------------------------------------------
//
// Wired GameCube pads and Wavebirds, in port 1. Presence comes from the mask
// PAD_ScanPads() returns -- disassembling it (`powerpc-eabi-ar x libogc.a pad.o`)
// shows it setting bit (1<<chan) only for a channel whose PADStatus.err came
// back PAD_ERR_NONE, so it is a real connected/readable test. The previous
// version instead guessed presence from stick deflection, and an idle pad
// resting a couple of counts off centre then claimed every frame and starved
// the Wiimote.
void pollGameCubePad(u32 connectedMask)
{
	if (!(connectedMask & 1))
		return;

	const u16 held = PAD_ButtonsHeld(0);
	const u16 down = PAD_ButtonsDown(0);

	g_dbg.gcConnected = true;
	g_dbg.gcButtons   = held;
	g_dbg.gcStickX    = PAD_StickX(0);
	g_dbg.gcStickY    = PAD_StickY(0);

	setKey(VK_JUMP,        (held & PAD_BUTTON_A) != 0);
	setKey(VK_SNEAK,       (held & PAD_BUTTON_B) != 0);
	setKey(VK_INVENTORY,   (held & PAD_BUTTON_X) != 0);
	setKey(VK_DROP,        (held & PAD_BUTTON_Y) != 0);
	setKey(VK_ESCAPE,      (held & PAD_BUTTON_START) != 0);

	// L+R opens chat. Suppress both mouse actions while the chord is held so the
	// same press neither places nor breaks a block. Trigger only when the second
	// shoulder goes down; holding the chord cannot repeatedly reopen the screen.
	const bool gcChatChord = (held & (PAD_TRIGGER_L | PAD_TRIGGER_R)) ==
	                         (PAD_TRIGGER_L | PAD_TRIGGER_R);
	if (gcChatChord && (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) &&
	    lwjgl::Mouse::isGrabbed() && !VirtualKeyboard::instance().isActive())
	{
		lwjgl::Keyboard::detail::pushKey(lwjgl::Keyboard::KEY_T, true);
		lwjgl::Keyboard::detail::pushKey(lwjgl::Keyboard::KEY_T, false);
	}

	// Text-entry bindings for a GameCube pad.  They are collected separately
	// from the normal game bindings and only consumed while VirtualKeyboard is
	// active, so A/B/X/Y/Z keep their ordinary in-game meanings otherwise.
	if (held & PAD_BUTTON_A)     s_wiiTextInputHeld |= WII_TEXT_TYPE;
	if (held & PAD_BUTTON_B)     s_wiiTextInputHeld |= WII_TEXT_BACK;
	if (held & PAD_BUTTON_X)     s_wiiTextInputHeld |= WII_TEXT_SPACE;
	if (held & PAD_TRIGGER_Z)    s_wiiTextInputHeld |= WII_TEXT_SHIFT;
	if (held & PAD_BUTTON_START) s_wiiTextInputHeld |= WII_TEXT_ENTER;
	if (held & PAD_BUTTON_Y)     s_wiiTextInputHeld |= WII_TEXT_CLOSE;
	if (held & PAD_BUTTON_UP)    s_wiiTextInputHeld |= WII_TEXT_UP;
	if (held & PAD_BUTTON_DOWN)  s_wiiTextInputHeld |= WII_TEXT_DOWN;
	if (held & PAD_BUTTON_LEFT)  s_wiiTextInputHeld |= WII_TEXT_LEFT;
	if (held & PAD_BUTTON_RIGHT) s_wiiTextInputHeld |= WII_TEXT_RIGHT;

	// D-pad up is the debug overlay (F3), doubling up on top of VK_ARROW_UP the
	// same way D-pad left/right double as hotbar prev/next below. It used to be
	// Z, right behind the R trigger -- squeezing R to attack/mine kept bumping
	// Z with the same finger and toggling the overlay by accident. The FPS /
	// position / chunk readout is the main way to see what the port is doing
	// without a host log, same reason the PS2 build spends its Select button on
	// it (src/ps2/main_ps2.cpp), so it stays bound to something, just not a
	// button next to R anymore.
	setKey(VK_ARROW_UP,    (held & PAD_BUTTON_UP) != 0);
	setKey(VK_DEBUG,       (held & PAD_BUTTON_UP) != 0);
	// D-pad down doubles as the third-person toggle (F5), same idea as up/F3
	// above. VK_THIRDPERSON was already wired to KEY_F5 in kVirtualKeyCodes but
	// left unbound on every device; down was the one direction on this pad with
	// nothing else on it.
	setKey(VK_ARROW_DOWN,  (held & PAD_BUTTON_DOWN) != 0);
	setKey(VK_THIRDPERSON, (held & PAD_BUTTON_DOWN) != 0);
	setKey(VK_ARROW_LEFT,  (held & PAD_BUTTON_LEFT) != 0);
	setKey(VK_ARROW_RIGHT, (held & PAD_BUTTON_RIGHT) != 0);

	// Triggers are the two mouse buttons: L = place/use, R = attack/mine.
	setMouse(VM_USE,    !gcChatChord && (held & PAD_TRIGGER_L) != 0);
	setMouse(VM_ATTACK, !gcChatChord && (held & PAD_TRIGGER_R) != 0);

	// D-pad left/right double as hotbar prev/next. Safe to bind on top of the
	// arrow keys: Beta 1.7.3 binds nothing to the arrows in game, and in a menu
	// the wheel scrolls a list, which is the useful reading of the same press.
	// Without this there is no way to change the selected item on a GameCube
	// pad, which is the difference between "supported" and "playable".
	if (down & PAD_BUTTON_RIGHT) g_wheel -= 1;
	if (down & PAD_BUTTON_LEFT)  g_wheel += 1;

	// PAD_Stick* report roughly -100..100 at the edge of the gate.
	const float lx = applyDeadzone(PAD_StickX(0)    / 100.0f);
	const float ly = applyDeadzone(PAD_StickY(0)    / 100.0f);
	const float rx = applyDeadzone(PAD_SubStickX(0) / 100.0f);
	const float ry = applyDeadzone(PAD_SubStickY(0) / 100.0f);

	g_moveX += lx;
	g_moveY += ly;
	g_lookX += rx;
	g_lookY += -ry;   // stick Y is up-positive; screen Y is down-positive
}

// --- Wiimote (+ Nunchuk / Classic expansion) --------------------------------

// Pointer offset from the centre of the screen -> turn rate, in virtual mouse
// pixels for this frame. This is the in-game half of the absolute/relative
// problem described at the top of the file.
//
// Neutral is the centre of the screen, always. Latching it to wherever the
// player happened to be pointing -- on grab, or on a button -- sounds friendlier
// and is not: close the inventory with the pointer in a corner and the entire
// mapping is suddenly half a screen out, with nothing on screen to show that it
// happened or how to undo it. A fixed neutral is one sentence to explain ("point
// at the middle of the TV and the camera stops") and cannot drift.
void applyIrLook(int ix, int iy, int w, int h)
{
	// Normalise BOTH axes by half the HEIGHT, not by each axis' own half-extent.
	// Dividing x by w/2 and y by h/2 stretches the vector horizontally, so a
	// pointer 45 degrees up-right would not turn the camera 45 degrees up-right
	// -- and matching the direction at every angle is the whole point of this.
	// The cost is that maximum yaw rate arrives at about three quarters of the
	// way to the left/right edge instead of at the edge itself, which is if
	// anything an improvement; see kIrSaturation.
	const float half = (float)h * 0.5f;
	const float ux = ((float)ix - (float)w * 0.5f) / half;
	const float uy = ((float)iy - (float)h * 0.5f) / half;

	const float mag = std::sqrt(ux * ux + uy * uy);
	g_dbg.irMag = (int)(mag * 100.0f);

	// Radial dead zone, not a per-axis one. A square dead zone lets a pointer
	// that is inside it on X but outside on Y turn the camera straight up, which
	// is the "it only moves vertically near the middle" complaint.
	if (mag <= kIrDeadzone)
		return;

	float t = (mag - kIrDeadzone) / (kIrSaturation - kIrDeadzone);
	if (t > 1.0f) t = 1.0f;
	const float curve = t * (kIrCurveLinear + (1.0f - kIrCurveLinear) * t);

	// The unit direction is taken from the raw vector, so only the SPEED goes
	// through the curve. Applying the curve per axis would bend diagonals toward
	// the nearest axis, which is the same directional error the aspect-ratio
	// normalisation above avoids.
	const float pixels = (kIrLookDegPerSec / kDegreesPerMousePixel) * curve * g_lookDt;
	g_lookPxX += (ux / mag) * pixels;
	g_lookPxY += (uy / mag) * pixels;
}

void pollWiimote()
{
	WPADData *wd = WPAD_Data(WPAD_CHAN_0);
	if (!wd)
		return;

	// WPAD_Probe is the supported "what is plugged into this channel" query, and
	// it is consulted in preference to wd->exp.type because that field only
	// describes the last data report that arrived: around a hot-plug, or while a
	// handshake is still in flight, it can read WPAD_EXP_NONE with a Nunchuk very
	// much attached. Trusting it alone is one of the ways a Nunchuk ends up "not
	// working".
	//
	// But neither query gets to VETO the poll. This is the only presence check in
	// the function on purpose: a probe that fails for one frame, or an err field
	// holding some transient not-ready value, must not silence the buttons and
	// the pointer as well -- that would trade a Nunchuk bug for a dead Wiimote.
	// So the two are merged, and the poll continues regardless.
	u32 expType = WPAD_EXP_NONE;
	if (WPAD_Probe(WPAD_CHAN_0, &expType) != WPAD_ERR_NONE)
		expType = WPAD_EXP_NONE;

	g_dbg.wmConnected = true;
	g_dbg.expProbed   = (int)expType;

	if (expType == WPAD_EXP_NONE)
		expType = (u32)wd->exp.type;

	g_dbg.expType   = (int)expType;
	g_dbg.wmButtons = wd->btns_h;
	g_dbg.irValid   = wd->ir.valid != 0;
	g_dbg.irX       = (int)wd->ir.x;
	g_dbg.irY       = (int)wd->ir.y;
	g_dbg.irDots    = wd->ir.num_dots;
	s_wiiTextInputSnapshot.irValid = wd->ir.valid != 0;
	s_wiiTextInputSnapshot.irX = (int)wd->ir.x;
	s_wiiTextInputSnapshot.irY = (int)wd->ir.y;
	s_wiiTextInputSnapshot.irWidth = wiigl_width();
	s_wiiTextInputSnapshot.irHeight = wiigl_height();

	// btns_d is libogc's own "pressed this frame" mask, and it covers the
	// expansion bits too -- WPAD_ReadPending computes it as (held & ~last) over
	// the full 32-bit mask, of which the top half is the Nunchuk/Classic
	// buttons. So the pulse-style bindings below need no remembered state.
	const u32 held = wd->btns_h;
	const u32 down = wd->btns_d;

	setKey(VK_JUMP,        (held & WPAD_BUTTON_A) != 0);
	setKey(VK_INVENTORY,   (held & WPAD_BUTTON_MINUS) != 0);
	setKey(VK_ESCAPE,      (held & WPAD_BUTTON_PLUS) != 0);
	setKey(VK_ARROW_UP,    (held & WPAD_BUTTON_UP) != 0);
	setKey(VK_ARROW_DOWN,  (held & WPAD_BUTTON_DOWN) != 0);
	setKey(VK_ARROW_LEFT,  (held & WPAD_BUTTON_LEFT) != 0);
	setKey(VK_ARROW_RIGHT, (held & WPAD_BUTTON_RIGHT) != 0);
	setMouse(VM_ATTACK,    (held & WPAD_BUTTON_B) != 0);

	if (held & WPAD_BUTTON_A)      s_wiiTextInputHeld |= WII_TEXT_TYPE;
	if (held & WPAD_BUTTON_B)      s_wiiTextInputHeld |= WII_TEXT_BACK;
	if (held & WPAD_BUTTON_PLUS)   s_wiiTextInputHeld |= WII_TEXT_ENTER;
	if (held & WPAD_BUTTON_MINUS)  s_wiiTextInputHeld |= WII_TEXT_SPACE;
	if (held & WPAD_BUTTON_1)      s_wiiTextInputHeld |= WII_TEXT_CLOSE;
	if (held & WPAD_BUTTON_2)      s_wiiTextInputHeld |= WII_TEXT_SHIFT;
	if (held & WPAD_BUTTON_UP)     s_wiiTextInputHeld |= WII_TEXT_UP;
	if (held & WPAD_BUTTON_DOWN)   s_wiiTextInputHeld |= WII_TEXT_DOWN;
	if (held & WPAD_BUTTON_LEFT)   s_wiiTextInputHeld |= WII_TEXT_LEFT;
	if (held & WPAD_BUTTON_RIGHT)  s_wiiTextInputHeld |= WII_TEXT_RIGHT;

	// Chat moved to the two-trigger chord below. Restore the simple utility
	// bindings: 1 = diagnostics, 2 = drop.
	setKey(VK_DEBUG, (held & WPAD_BUTTON_1) != 0);
	setKey(VK_DROP,  (held & WPAD_BUTTON_2) != 0);

	// D-pad left/right double as hotbar prev/next, exactly as on the GameCube
	// pad -- see the note there for why binding this on top of the arrow keys
	// is safe.
	if (down & WPAD_BUTTON_RIGHT) g_wheel -= 1;
	if (down & WPAD_BUTTON_LEFT)  g_wheel += 1;

	if (expType == WPAD_EXP_NUNCHUK)
	{
		const joystick_t &js = wd->exp.nunchuk.js;
		g_dbg.jsAng     = js.ang;
		g_dbg.jsMag     = js.mag;
		g_dbg.jsRawX    = js.pos.x;
		g_dbg.jsRawY    = js.pos.y;
		g_dbg.jsCenterX = js.center.x;
		g_dbg.jsCenterY = js.center.y;

		float x, y;
		readJoystick(wd->exp.nunchuk.js, x, y);
		g_moveX += applyDeadzone(x);
		g_moveY += applyDeadzone(y);

		if (wd->exp.nunchuk.btns_held & NUNCHUK_BUTTON_Z)
			s_wiiTextInputHeld |= WII_TEXT_SPACE;
		if (wd->exp.nunchuk.btns_held & NUNCHUK_BUTTON_C)
			s_wiiTextInputHeld |= WII_TEXT_SHIFT;

		// libogc reports the expansion's buttons twice: merged into the Wiimote
		// mask (WPAD_NUNCHUK_BUTTON_* are the low bits shifted up by 16) and in
		// the expansion struct's own byte. Either is normally enough; taking
		// both costs one OR and removes a whole class of "works on my Wiimote"
		// difference, and it is safe here precisely because the edges are taken
		// centrally from the assembled mask rather than per source.
		const u8 nunHeld = wd->exp.nunchuk.btns_held;
		const bool nunchukZ = (held & WPAD_NUNCHUK_BUTTON_Z) != 0 ||
		                      (nunHeld & NUNCHUK_BUTTON_Z) != 0;
		const bool nunchukChatChord = (held & WPAD_BUTTON_B) != 0 && nunchukZ;
		if (nunchukChatChord &&
		    (down & (WPAD_BUTTON_B | WPAD_NUNCHUK_BUTTON_Z)) &&
		    lwjgl::Mouse::isGrabbed() && !VirtualKeyboard::instance().isActive())
		{
			lwjgl::Keyboard::detail::pushKey(lwjgl::Keyboard::KEY_T, true);
			lwjgl::Keyboard::detail::pushKey(lwjgl::Keyboard::KEY_T, false);
		}
		if (nunchukChatChord)
			g_mouse &= ~(VM_ATTACK | VM_USE);

		setKey(VK_SNEAK, (held & WPAD_NUNCHUK_BUTTON_C) != 0 || (nunHeld & NUNCHUK_BUTTON_C) != 0);
		setMouse(VM_USE, !nunchukChatChord && nunchukZ);
	}
	else if (expType == WPAD_EXP_CLASSIC)
	{
		const bool classicChatChord =
			(held & (WPAD_CLASSIC_BUTTON_FULL_L | WPAD_CLASSIC_BUTTON_FULL_R)) ==
			(WPAD_CLASSIC_BUTTON_FULL_L | WPAD_CLASSIC_BUTTON_FULL_R);
		if (classicChatChord &&
		    (down & (WPAD_CLASSIC_BUTTON_FULL_L | WPAD_CLASSIC_BUTTON_FULL_R)) &&
		    lwjgl::Mouse::isGrabbed() && !VirtualKeyboard::instance().isActive())
		{
			lwjgl::Keyboard::detail::pushKey(lwjgl::Keyboard::KEY_T, true);
			lwjgl::Keyboard::detail::pushKey(lwjgl::Keyboard::KEY_T, false);
		}
		setKey(VK_JUMP,      (held & WPAD_CLASSIC_BUTTON_A) != 0);
		setKey(VK_SNEAK,     (held & WPAD_CLASSIC_BUTTON_B) != 0);
		setKey(VK_INVENTORY, (held & WPAD_CLASSIC_BUTTON_X) != 0);
		setKey(VK_DROP,      (held & WPAD_CLASSIC_BUTTON_Y) != 0);
		setKey(VK_ESCAPE,    (held & WPAD_CLASSIC_BUTTON_PLUS) != 0);
		setKey(VK_DEBUG,     (held & WPAD_CLASSIC_BUTTON_MINUS) != 0);
		setKey(VK_ARROW_UP,    (held & WPAD_CLASSIC_BUTTON_UP) != 0);
		setKey(VK_ARROW_DOWN,  (held & WPAD_CLASSIC_BUTTON_DOWN) != 0);
		setKey(VK_ARROW_LEFT,  (held & WPAD_CLASSIC_BUTTON_LEFT) != 0);
		setKey(VK_ARROW_RIGHT, (held & WPAD_CLASSIC_BUTTON_RIGHT) != 0);
		setMouse(VM_USE,     !classicChatChord && (held & WPAD_CLASSIC_BUTTON_FULL_L) != 0);
		setMouse(VM_ATTACK,  !classicChatChord && (held & WPAD_CLASSIC_BUTTON_FULL_R) != 0);

		if (held & WPAD_CLASSIC_BUTTON_A)      s_wiiTextInputHeld |= WII_TEXT_TYPE;
		if (held & WPAD_CLASSIC_BUTTON_B)      s_wiiTextInputHeld |= WII_TEXT_BACK;
		if (held & WPAD_CLASSIC_BUTTON_PLUS)   s_wiiTextInputHeld |= WII_TEXT_ENTER;
		if (held & WPAD_CLASSIC_BUTTON_MINUS)  s_wiiTextInputHeld |= WII_TEXT_SPACE;
		if (held & WPAD_CLASSIC_BUTTON_UP)     s_wiiTextInputHeld |= WII_TEXT_UP;
		if (held & WPAD_CLASSIC_BUTTON_DOWN)   s_wiiTextInputHeld |= WII_TEXT_DOWN;
		if (held & WPAD_CLASSIC_BUTTON_LEFT)   s_wiiTextInputHeld |= WII_TEXT_LEFT;
		if (held & WPAD_CLASSIC_BUTTON_RIGHT)  s_wiiTextInputHeld |= WII_TEXT_RIGHT;

		// ZL/ZR are the hotbar, for the same reason the GameCube pad puts it on
		// the D-pad: without it there is no way to change the selected item.
		if (down & WPAD_CLASSIC_BUTTON_ZR) g_wheel -= 1;
		if (down & WPAD_CLASSIC_BUTTON_ZL) g_wheel += 1;

		const classic_ctrl_t &cc = wd->exp.classic;
		float lx, ly, rx, ry;
		readJoystick(cc.ljs, lx, ly);
		readJoystick(cc.rjs, rx, ry);
		g_moveX += applyDeadzone(lx);
		g_moveY += applyDeadzone(ly);
		g_lookX += applyDeadzone(rx);
		g_lookY += -applyDeadzone(ry);
	}

	// --- IR pointer -> look ---
	// Two different meanings, picked by whether the engine has the mouse:
	//
	//   grabbed (in game)  offset from centre is a RATE. Hold the Wiimote
	//                      pointing up-right and the camera keeps turning
	//                      up-right instead of stopping at the edge of the
	//                      screen. See applyIrLook.
	//   not grabbed (GUI)  the pointer IS the cursor, absolutely, because
	//                      pointing at a button has to put the cursor on it.
	//
	// ir.valid goes false the moment the player points away from the sensor bar.
	// Dropping the frame *and* forgetting the previous position is essential:
	// keeping it would turn the next re-acquisition into one enormous delta and
	// snap the camera across the world.
	//
	// Use ir.x / ir.y and nothing else. ir_t carries the pointer in THREE
	// different coordinate spaces and only the last one is the one we want:
	//
	//   ax, ay   raw sensor space
	//   sx, sy   smoothed, still an intermediate space
	//   x,  y    bounded -- the only pair expressed in the virtual resolution
	//            handed to WPAD_SetVRes (Display_wii.cpp passes the framebuffer
	//            size), which is the space the mouse cursor lives in
	//
	// Reading sx/sy instead looks like a free noise reduction and is not: the
	// deltas come out on a different scale, round to zero, and the camera then
	// sits in the middle of the screen and never turns.
	if (wd->ir.valid)
	{
		const int ix = (int)wd->ir.x;
		const int iy = (int)wd->ir.y;

		if (kIrRateCamera && lwjgl::Mouse::isGrabbed())
		{
			applyIrLook(ix, iy, wiigl_width(), wiigl_height());

			// The pointer is not the cursor in this mode, so its position must
			// not be carried across into the GUI: the first frame after closing
			// a menu would otherwise difference against a position from however
			// long ago and jump the cursor -- the same "one enormous delta"
			// failure that losing the sensor bar causes.
			g_haveIrPrev = false;
		}
		else
		{
			if (g_haveIrPrev)
			{
				const int dx = ix - g_irPrevX;
				const int dy = iy - g_irPrevY;
				if (dx || dy)
				{
					g_cursorX = ix;
					g_cursorY = iy;
					lwjgl::Mouse::detail::pushMotion(g_cursorX, g_cursorY, dx, dy);
				}
			}
			g_irPrevX = ix;
			g_irPrevY = iy;
			g_haveIrPrev = true;
		}
	}
	else
	{
		g_haveIrPrev = false;
	}
}

// --- flushing the frame's state to lwjgl ------------------------------------

void flushMovement()
{
	// Clamp, because two devices can both be pushing the stick.
	float x = g_moveX, y = g_moveY;
	if (x >  1.0f) x =  1.0f; else if (x < -1.0f) x = -1.0f;
	if (y >  1.0f) y =  1.0f; else if (y < -1.0f) y = -1.0f;

	// Movement reaches the engine as WASD, because that is what the shared GUI
	// and gameplay code already listens to.
	setKey(VK_FORWARD, y > 0.0f);
	setKey(VK_BACK,    y < 0.0f);
	setKey(VK_LEFT,    x < 0.0f);
	setKey(VK_RIGHT,   x > 0.0f);
}

// Sticks and IR both end up here, in the same unit (virtual mouse pixels), and
// are summed once -- the same reason the keys go through one mask. A player on a
// Wiimote + Nunchuk with a GameCube pad plugged in must not get two cameras.
void flushLook()
{
	const bool grabbed = lwjgl::Mouse::isGrabbed();

	// Clamp, because two devices can both be pushing a look stick.
	float x = g_lookX, y = g_lookY;
	if (x >  1.0f) x =  1.0f; else if (x < -1.0f) x = -1.0f;
	if (y >  1.0f) y =  1.0f; else if (y < -1.0f) y = -1.0f;

	// One scale, chosen by what the stick is steering: the camera when the
	// engine owns the mouse, the GUI cursor otherwise. Both are per second, so
	// neither depends on the frame rate.
	const float stickScale = grabbed
		? (kStickLookDegPerSec / kDegreesPerMousePixel) * g_lookDt
		: kCursorPixelsPerSec * g_lookDt;

	const float px = x * stickScale + g_lookPxX;
	const float py = y * stickScale + g_lookPxY;

	if (px == 0.0f && py == 0.0f)
	{
		// Nothing asked for movement this frame. Drop the sub-pixel remainder
		// rather than let it sit and fire a stray pixel of drift later.
		g_lookResidX = 0.0f;
		g_lookResidY = 0.0f;
		return;
	}

	g_lookResidX += px;
	g_lookResidY += py;
	const int ix = (int)g_lookResidX;   // truncates toward zero, so the sign survives
	const int iy = (int)g_lookResidY;
	if (ix == 0 && iy == 0) return;
	g_lookResidX -= (float)ix;
	g_lookResidY -= (float)iy;

	if (!grabbed)
	{
		// GUI: the cursor is the thing being moved, so it has to stay on screen.
		// In game the cursor is not on screen at all and is left where it was --
		// MouseHelper::ungrabMouseCursor re-centres it on the way back out.
		const int w = wiigl_width();
		const int h = wiigl_height();
		g_cursorX += ix;
		g_cursorY += iy;
		if (g_cursorX < 0) g_cursorX = 0; else if (g_cursorX >= w) g_cursorX = w - 1;
		if (g_cursorY < 0) g_cursorY = 0; else if (g_cursorY >= h) g_cursorY = h - 1;
	}

	lwjgl::Mouse::detail::pushMotion(g_cursorX, g_cursorY, ix, iy);
}

// Everything downstream is edge-triggered (lwjgl pushes key down/up events), so
// only the transitions in the assembled mask are sent.
void flushKeysAndButtons()
{
	const u32 changed = g_keys ^ g_prevKeys;
	if (changed)
	{
		for (int i = 0; i < VK_COUNT; ++i)
		{
			const u32 bit = 1u << i;
			if (changed & bit)
				lwjgl::Keyboard::detail::pushKey(kVirtualKeyCodes[i], (g_keys & bit) != 0);
		}
	}
	g_prevKeys = g_keys;

	if ((g_mouse ^ g_prevMouse) & VM_ATTACK)
		lwjgl::Mouse::detail::pushButton(0, (g_mouse & VM_ATTACK) != 0, g_cursorX, g_cursorY);
	if ((g_mouse ^ g_prevMouse) & VM_USE)
		lwjgl::Mouse::detail::pushButton(1, (g_mouse & VM_USE) != 0, g_cursorX, g_cursorY);
	g_prevMouse = g_mouse;

	if (g_wheel != 0)
		lwjgl::Mouse::detail::pushWheel(g_wheel, g_cursorX, g_cursorY);
}

} // namespace

// One line of "what did the hardware actually say", for the F3 overlay.
//
// Everything is printed as integers on purpose: %f pulls newlib's floating point
// formatter into a function that runs every frame the overlay is up, and the
// values only need to be readable off a TV, not precise. Angles are degrees,
// magnitudes and stick positions are x100, raw/centre are the unscaled bytes
// libogc calibrated against.
const char *wiiPadDebugLine()
{
	static const char *const kExpNames[] = { "none", "nunchuk", "classic", "gh3", "board" };
	const char *exp = (g_dbg.expType >= 0 && g_dbg.expType <= 4)
	                  ? kExpNames[g_dbg.expType] : "?";

	// "m" after the dot count is the rate camera's deflection as a percentage of
	// half the screen height: below kIrDeadzone*100 the camera is deliberately
	// still, at kIrSaturation*100 it is turning as fast as it can. It is the one
	// number that says whether a camera that "does not move" is a dead zone that
	// is too wide or a pointer that is not being read at all.
	std::snprintf(g_dbgLine, sizeof(g_dbgLine),
	              "GC%c b%04X s%d,%d | WM%c b%08lX %s(p%d) | ir%c %d,%d d%d m%d | js a%d m%d r%d,%d c%d,%d",
	              g_dbg.gcConnected ? '+' : '-',
	              (unsigned)g_dbg.gcButtons, g_dbg.gcStickX, g_dbg.gcStickY,
	              g_dbg.wmConnected ? '+' : '-',
	              (unsigned long)g_dbg.wmButtons, exp, g_dbg.expProbed,
	              g_dbg.irValid ? '+' : '-', g_dbg.irX, g_dbg.irY, g_dbg.irDots, g_dbg.irMag,
	              (int)g_dbg.jsAng, (int)(g_dbg.jsMag * 100.0f),
	              g_dbg.jsRawX, g_dbg.jsRawY, g_dbg.jsCenterX, g_dbg.jsCenterY);
	return g_dbgLine;
}

void wiiPadPoll()
{
	// PAD_ScanPads returns the mask of connected, readable GameCube pads; WPAD's
	// equivalent question is asked per channel by WPAD_Probe inside pollWiimote.
	const u32 gcConnected = (u32)PAD_ScanPads();
	WPAD_ScanPads();

	// Frame time for the rate-based look. gettime() is the 60 MHz-ish timebase,
	// which is monotonic and always alive here -- unlike the PS2, where
	// System::nanoTime can be dead on some BIOS revisions and had to be probed
	// at boot before anything was allowed to gate on it.
	const u64 now = gettime();
	if (g_lookLastTime != 0)
	{
		const float dt = (float)diff_usec(g_lookLastTime, now) * 1e-6f;
		g_lookDt = (dt < kMinLookDt) ? kMinLookDt : ((dt > kMaxLookDt) ? kMaxLookDt : dt);
	}
	g_lookLastTime = now;

	g_keys  = 0;
	g_mouse = 0;
	g_moveX = g_moveY = 0.0f;
	g_lookX = g_lookY = 0.0f;
	g_lookPxX = g_lookPxY = 0.0f;
	g_wheel = 0;
	s_wiiTextInputHeld = 0;
	s_wiiTextInputSnapshot.irValid = false;

	// Cleared, not carried over: a device that stops reporting has to show as
	// absent in the overlay, otherwise the diagnostic lies in exactly the case
	// it exists for.
	g_dbg = PadDebug();

	// Both are polled every frame. Neither can starve the other, because they
	// contribute to the same mask rather than competing for the frame.
	pollGameCubePad(gcConnected);
	pollWiimote();
	wiiTextInputSnapshotUpdate(s_wiiTextInputHeld);

	// A focused text box owns controller buttons until it is closed.  This
	// mirrors the PS2 path: the virtual keyboard receives the Wii snapshot and
	// emits only text/key events, while normal A/B/D-pad bindings are kept out
	// of the menu and gameplay queues.
	if (VirtualKeyboard::instance().isActive())
	{
		g_keys = 0;
		g_mouse = 0;
		g_wheel = 0;
		VirtualKeyboard::instance().tick();
	}

	flushMovement();
	flushLook();
	flushKeysAndButtons();
}

#endif // WII_PLATFORM
