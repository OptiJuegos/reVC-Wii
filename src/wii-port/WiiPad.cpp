#include <cmath>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "common.h"
#include "ControllerConfig.h"
#include "Frontend.h"
#include "Pad.h"
#include "Timer.h"
#include "WiiPad.h"
#include "WiiTrace.h"
#include "platform.h"
#include "skeleton.h"

// Stick handling ported from the BetaPlusPlus Wii port (src/wii/wii/
// WiiPadState.cpp in the reference tree).  The failure modes it works around
// are the ones that only show up on real hardware, so the reasoning is kept
// with the code rather than rediscovered later.

namespace
{

// Every axis in CControllerState is measured against 128, not against the range
// the int16 holding it could carry.  CPad::LookAroundUpDown only starts turning
// the camera past 85 and treats 128 as the edge of the gate,
// CPad::GetSteeringLeftRight subtracts 35 and rescales by 128/(128 - 35),
// CPad::GetAnalogueUpDown weighs the stick against a D-pad worth 127, and every
// other joystick backend writes value*128*sensitivity (the XInput path in
// Pad.cpp, sdl2.cpp, glfw.cpp).  Filling the int16 range instead puts a barely
// moved stick two orders of magnitude past hard over, so all of those thresholds
// are already saturated and the first nudge reads as a full speed turn.
constexpr float kAxisFullScale = 128.0f;

// PAD_ScanPads() calls PAD_Read() and nothing else, so the sticks arrive
// unclamped: PAD_Stick* return the raw sample minus the calibrated origin, with
// neither the deadzone nor the gate PAD_Clamp() would have imposed.  Where that
// gate is written down is the useful part, because it is not a circle: PAD_Clamp
// subtracts a 15 count deadzone per axis and then clamps into an OCTAGON, with
// vertices at (72, 0) and (40, 40) for the main stick and (59, 0) and (31, 31)
// for the C-stick.  Adding the deadzone back gives the same gate in the raw
// counts this backend actually sees -- 87 out to a cardinal notch, 55 on each
// axis out to a diagonal one.
//
// Those are the limits every pad is guaranteed to reach, which is why they are
// what to normalise against: the ~100 counts the s8 could carry is a number many
// pads never report, so dividing by it leaves a stick held hard against the gate
// still reading as only two thirds deflected.
constexpr float kGameCubeStickRange = 87.0f;
constexpr float kGameCubeStickCorner = 55.0f;
constexpr float kGameCubeSubStickRange = 74.0f;
constexpr float kGameCubeSubStickCorner = 46.0f;

// A deadzone of one or more would divide the rescale in applyDeadzone by zero,
// and nothing validates the settings on the way in.
constexpr float kMaximumDeadzone = 0.9f;

constexpr float kDegreesToRadians = 3.14159265358979323846f/180.0f;

// --- the pointer as a camera ------------------------------------------------
// The hard part of a pointer on this engine: IR is ABSOLUTE and BOUNDED, it says
// where on the screen the player is aiming, while the camera is RELATIVE and
// UNBOUNDED, it wants how far to turn and can turn forever.  Differencing
// successive pointer positions is the obvious translation and it is wrong in a
// specific way: the total camera travel available becomes one screen width, so
// aiming into a corner and holding swings the view until the pointer clamps and
// then stops.  There is no way to keep turning without sweeping back and forth.
//
// So in game the offset from the centre of the screen is read as a RATE instead:
// direction and speed, not distance.  Hold the Wiimote up and right and the
// camera keeps going up and right, faster the further out it points.  In a MENU
// the pointer stays absolute, because there it really is a cursor and aiming at
// an option has to put the cursor on that option.
//
// Neutral is the centre of the screen, always.  Latching it to wherever the
// player happened to be aiming sounds friendlier and is not: close a menu with
// the pointer in a corner and the whole mapping is half a screen out, with
// nothing on screen to say that it happened or how to undo it.
//
// All three of these are fractions of half the screen HEIGHT, including the
// horizontal one -- see irPointerRate for why not each axis' own extent.
//
//   deadzone    how far off centre the player can aim before the camera moves.
//               Has to cover ordinary hand shake plus the sensor's own jitter or
//               the view drifts while they hold still, and it is also what keeps
//               a centred pointer from overriding the sticks.
//   saturation  where the turn rate reaches its maximum.  Deliberately short of
//               1.0: the pointer gets unreliable near the edge of the sensor's
//               field, and having to aim there to turn quickly is what makes a
//               rate camera feel like it is fighting the player.
//   curve       how much of the response is linear rather than quadratic.  The
//               quadratic part is what keeps small offsets slow enough to aim
//               with; the linear part stops the first third from doing nothing.
constexpr float kPointerDeadzone = 0.13f;
constexpr float kPointerSaturation = 0.75f;
constexpr float kPointerCurveLinear = 0.45f;

// Turn rate at full deflection, in the units GetMouseX and GetMouseY are read
// in, PER SECOND.  Per second and not per frame because this port's frame rate
// moves with the scene, and a per frame constant makes the camera turn faster in
// an empty street than in traffic, which reads as the sensitivity changing by
// itself.
//
// Cam.cpp turns a horizontal delta into 2.5*x*m_fMouseAccelHorzntl radians of
// yaw, so at the default sensitivity of 0.0025 one unit is 0.00625 rad and this
// works out to about 143 degrees per second at 60Hz.  The options screen's mouse
// sensitivity slider scales m_fMouseAccelHorzntl, and therefore scales the
// pointer with it, which is the control to reach for rather than this number:
// Frontend.cpp clamps it between 1/3200 and 1/200, so the pointer can be tuned
// across a factor of sixteen without touching the source.
constexpr float kPointerRatePerSec = 400.0f;

// The same delta pitches further than it yaws: Cam.cpp scales the vertical one
// by 4.0*m_fMouseAccelVertical against 2.5*m_fMouseAccelHorzntl for the
// horizontal, and m_fMouseAccelVertical is m_fMouseAccelHorzntl + 0.0005, so at
// the default settings pitch comes out about twice as fast as yaw.  This takes
// that back out and leaves the pitch a little slower than the yaw instead, the
// way the stick path's own 0.6 factor in Cam.cpp does.
constexpr float kPointerPitchScale = 0.32f;

// The pointer stops being tracked the moment it leaves the sensor bar's field,
// which is exactly what happens at the END of a long turn: the remote is still
// held out to the side and the player still wants to keep turning.  Freezing
// there caps a single turn at the sensor's field of view and forces them to saw
// the remote back and forth, so the last rate is held instead.
//
// Not held forever, though.  A remote set down, or pointed at the floor, looks
// identical from here, and a camera that keeps spinning until someone picks it
// back up reads as a hang rather than a feature.  This is long enough to finish
// any turn a player is actually in the middle of.
constexpr float kPointerHoldSeconds = 2.0f;

// Frame time clamp for the rate above.  A dt of zero (two polls inside one
// timebase tick) would freeze the camera and a huge one -- the first frame after
// a streaming stall -- would fling it, so the rate is only integrated over
// plausible frame times.
constexpr float kMinPointerDt = 1.0f/240.0f;
constexpr float kMaxPointerDt = 1.0f/15.0f;

// --- what one scan leaves behind for the rest of the frame -------------------
// WiiPadScan fills these and everything below reads them, which is the whole
// reason it is a separate entry point; see WiiPad.h.
uint32 s_connectedGameCubePads;
float s_pointerDt = 1.0f/60.0f;   // seeded so the first frame is not a special case
u64 s_pointerLastTime;

// The last rate the pointer asked for, kept so it can go on being applied while
// tracking is lost.  Stored per second rather than per frame: it outlives the
// frame it was measured in, and the frames it is replayed over are not the same
// length as that one.
float s_heldRateX;
float s_heldRateY;
float s_heldSeconds;

int16
toAxis(float value, float sensitivity)
{
	if(value > 1.0f)
		value = 1.0f;
	if(value < -1.0f)
		value = -1.0f;
	return (int16)(value*kAxisFullScale*sensitivity);
}

void
setButton(int16 &field, bool down)
{
	if(down)
		field = 255;
}

float
clampDeadzone(float deadzone)
{
	// Written as a failed greater-than so a NaN out of the settings lands here
	// instead of propagating into every axis.
	if(!(deadzone > 0.0f))
		return 0.0f;
	if(deadzone > kMaximumDeadzone)
		return kMaximumDeadzone;
	return deadzone;
}

// Deadzone and rescale for a stick as a whole rather than for each of its axes
// on its own.  A per axis deadzone lets an axis through the moment it clears the
// threshold by itself, so a stick pushed nearly horizontally still leaks its
// small vertical component and the camera climbs while the player believes they
// are only panning.  Working from the magnitude also leaves the direction alone:
// only the length is rescaled, so a diagonal stays a diagonal instead of being
// bent towards the nearer axis.
void
applyDeadzone(float &x, float &y, float deadzone)
{
	const float magnitude = std::sqrt(x*x + y*y);
	if(magnitude <= deadzone || magnitude <= 0.0f){
		x = 0.0f;
		y = 0.0f;
		return;
	}

	// Movement starts at zero just outside the deadzone instead of jumping
	// straight to whatever fraction of full deflection the deadzone covers.
	float length = (magnitude - deadzone)/(1.0f - deadzone);
	if(length > 1.0f)
		length = 1.0f;
	const float scale = length/magnitude;
	x *= scale;
	y *= scale;
}

// Rescales a raw GameCube reading so that every point on the gate reaches full
// deflection, a diagonal notch as much as a cardinal one, with the direction
// left alone.  Dividing both axes by the cardinal reach instead treats the gate
// as a square: a diagonal notch only carries 55 counts per axis against the 87 a
// cardinal one carries, so the same physical travel reads about a fifth shorter
// once the stick is held off axis, and the camera turns visibly slower on the
// diagonals than it does straight up or sideways.
//
// The gate's radius in the direction being pushed needs no trigonometry.  The
// octagon's boundary, in the sector where one axis dominates, is
// corner*major + (range - corner)*minor = corner*range, so evaluating that left
// hand side against corner*range IS the deflection as a fraction of the gate.
void
normalizeGameCubeStick(float &x, float &y, float range, float corner)
{
	const float magnitude = std::sqrt(x*x + y*y);
	if(magnitude <= 0.0f)
		return;

	const float absX = std::fabs(x);
	const float absY = std::fabs(y);
	const float deflection = (corner*Max(absX, absY) +
		(range - corner)*Min(absX, absY))/(corner*range);

	const float scale = deflection/magnitude;
	x *= scale;
	y *= scale;
}

// libogc reports an expansion stick in polar form derived from a calibration
// block read during the expansion handshake, and that block is what goes wrong
// in practice: on a third party Classic Controller, or while the handshake is
// still in flight, centre/min/max come back as zeros and the polar pair is then
// either stuck at zero (the stick does nothing) or wildly out of range (the
// player walks in one direction forever).  So the polar values are used when
// they are sane and the raw position measured against the calibrated centre is
// the fallback.  The fallback also produces zero for a genuinely centred stick,
// which is why no "is the calibration broken" flag is needed anywhere.
void
readJoystick(const joystick_t &joystick, float &outX, float &outY)
{
	outX = 0.0f;
	outY = 0.0f;

	const float magnitude = joystick.mag > 1.0f ? 1.0f : joystick.mag;
	if(magnitude > 0.0f && std::isfinite(magnitude) && std::isfinite(joystick.ang)){
		const float radians = joystick.ang*kDegreesToRadians;
		outX = std::sin(radians)*magnitude;
		outY = std::cos(radians)*magnitude;
		return;
	}

	// A centre of zero means no calibration was ever read.  There is nothing to
	// measure against, so report centred rather than invent a direction.
	const int centerX = joystick.center.x;
	const int centerY = joystick.center.y;
	if(centerX == 0 && centerY == 0)
		return;

	// Half the calibrated travel, or the nominal counts when the calibration
	// block is obviously unusable.
	const float rangeX = joystick.max.x > joystick.min.x ?
		(joystick.max.x - joystick.min.x)*0.5f : 100.0f;
	const float rangeY = joystick.max.y > joystick.min.y ?
		(joystick.max.y - joystick.min.y)*0.5f : 100.0f;
	outX = ((float)joystick.pos.x - (float)centerX)/rangeX;
	outY = ((float)joystick.pos.y - (float)centerY)/rangeY;

	if(outX > 1.0f)
		outX = 1.0f;
	else if(outX < -1.0f)
		outX = -1.0f;
	if(outY > 1.0f)
		outY = 1.0f;
	else if(outY < -1.0f)
		outY = -1.0f;
}

struct StickAccumulator
{
	float leftX;
	float leftY;
	float rightX;
	float rightY;
};

// The joystick tuning the rest of the game already carries, read once per
// capture so every device merged into this pad is shaped the same way and the
// numbers live where the other backends already look for them.
struct StickSettings
{
	float leftDeadzone;
	float rightDeadzone;
	float leftSensitivityX;
	float leftSensitivityY;
	float rightSensitivityX;
	float rightSensitivityY;
};

StickSettings
currentStickSettings(void)
{
	StickSettings settings;
	settings.leftDeadzone = clampDeadzone(ControlsManager.m_lStickDeadzone);
	settings.rightDeadzone = clampDeadzone(ControlsManager.m_rStickDeadzone);
	settings.leftSensitivityX = ControlsManager.m_lStickSensX;
	settings.leftSensitivityY = ControlsManager.m_lStickSensY;
	settings.rightSensitivityX = ControlsManager.m_rStickSensX;
	settings.rightSensitivityY = ControlsManager.m_rStickSensY;
	return settings;
}

// The deadzone is applied per device, before the merge, because each one has its
// own idle noise: a Classic Controller resting off centre must not be able to
// push a GameCube pad that is genuinely centred past the threshold.
void
addStick(float x, float y, float deadzone, float &outX, float &outY)
{
	applyDeadzone(x, y, deadzone);
	outX += x;
	outY += y;
}

// PAD_ScanPads() returns the mask of channels whose status came back without
// error, so it is a real connected test.  Guessing presence from stick
// deflection instead lets an idle pad resting a couple of counts off centre
// claim every frame.
void
captureGameCube(int channel, uint32 connectedMask, CControllerState &state,
	StickAccumulator &sticks, const StickSettings &settings)
{
	if((connectedMask & (1 << channel)) == 0)
		return;

	const u16 buttons = PAD_ButtonsHeld(channel);
	setButton(state.Cross, buttons & PAD_BUTTON_A);
	setButton(state.Circle, buttons & PAD_BUTTON_B);
	setButton(state.Square, buttons & PAD_BUTTON_X);
	setButton(state.Triangle, buttons & PAD_BUTTON_Y);
	setButton(state.LeftShoulder1, buttons & PAD_TRIGGER_L);
	setButton(state.RightShoulder1, buttons & PAD_TRIGGER_R);
	setButton(state.RightShoulder2, buttons & PAD_TRIGGER_Z);
	setButton(state.Start, buttons & PAD_BUTTON_START);
	setButton(state.DPadUp, buttons & PAD_BUTTON_UP);
	setButton(state.DPadDown, buttons & PAD_BUTTON_DOWN);
	setButton(state.DPadLeft, buttons & PAD_BUTTON_LEFT);
	setButton(state.DPadRight, buttons & PAD_BUTTON_RIGHT);

	// PAD_Stick* report raw counts offset from the calibrated origin, so the gate
	// is normalised away before anything else looks at them.  Their Y grows
	// upwards while the game's grows down the screen; the octagon is symmetric
	// about both axes, so flipping it first costs nothing.
	float x = PAD_StickX(channel);
	float y = -PAD_StickY(channel);
	normalizeGameCubeStick(x, y, kGameCubeStickRange, kGameCubeStickCorner);
	addStick(x, y, settings.leftDeadzone, sticks.leftX, sticks.leftY);

	x = PAD_SubStickX(channel);
	y = -PAD_SubStickY(channel);
	normalizeGameCubeStick(x, y, kGameCubeSubStickRange, kGameCubeSubStickCorner);
	addStick(x, y, settings.rightDeadzone, sticks.rightX, sticks.rightY);
}

// WPAD_Probe is the supported "what is plugged into this channel" query and is
// consulted in preference to exp.type, which only describes the last data report
// that arrived: around a hot plug, or while a handshake is still in flight, it
// can read NONE with an expansion very much attached.  Neither query gets to veto
// the other, so a probe that fails for a single frame cannot silence the pad.
u32
probeExpansion(int channel, const WPADData &data)
{
	u32 expansion = WPAD_EXP_NONE;
	if(WPAD_Probe(channel, &expansion) != WPAD_ERR_NONE)
		expansion = WPAD_EXP_NONE;
	if(expansion == WPAD_EXP_NONE)
		expansion = (u32)data.exp.type;
	return expansion;
}

void
captureClassic(const WPADData &data, CControllerState &state,
	StickAccumulator &sticks, const StickSettings &settings)
{
	const u32 buttons = data.btns_h;
	setButton(state.Cross, buttons & WPAD_CLASSIC_BUTTON_B);
	setButton(state.Circle, buttons & WPAD_CLASSIC_BUTTON_A);
	setButton(state.Square, buttons & WPAD_CLASSIC_BUTTON_Y);
	setButton(state.Triangle, buttons & WPAD_CLASSIC_BUTTON_X);
	setButton(state.LeftShoulder1, buttons & WPAD_CLASSIC_BUTTON_FULL_L);
	setButton(state.RightShoulder1, buttons & WPAD_CLASSIC_BUTTON_FULL_R);
	setButton(state.LeftShoulder2, buttons & WPAD_CLASSIC_BUTTON_ZL);
	setButton(state.RightShoulder2, buttons & WPAD_CLASSIC_BUTTON_ZR);
	setButton(state.Start, buttons & WPAD_CLASSIC_BUTTON_PLUS);
	setButton(state.Select, buttons & WPAD_CLASSIC_BUTTON_MINUS);
	setButton(state.DPadUp, buttons & WPAD_CLASSIC_BUTTON_UP);
	setButton(state.DPadDown, buttons & WPAD_CLASSIC_BUTTON_DOWN);
	setButton(state.DPadLeft, buttons & WPAD_CLASSIC_BUTTON_LEFT);
	setButton(state.DPadRight, buttons & WPAD_CLASSIC_BUTTON_RIGHT);

	// readJoystick already normalises to -1..1 with +Y upwards, so only the sign
	// of Y has to be turned around to match the screen.
	const classic_ctrl_t &classic = data.exp.classic;
	float x, y;
	readJoystick(classic.ljs, x, y);
	addStick(x, -y, settings.leftDeadzone, sticks.leftX, sticks.leftY);
	readJoystick(classic.rjs, x, y);
	addStick(x, -y, settings.rightDeadzone, sticks.rightX, sticks.rightY);
}

// The Wiimote itself, and the Nunchuk when one is attached.  Only the forward
// grip is supported -- the remote pointed at the screen, D-pad under the thumb,
// B trigger under the index finger -- because that is the grip the pointer needs
// and the pointer is what stands in for the right stick.  Held sideways the
// D-pad would be rotated a quarter turn and the pointer unusable, which is a
// different mapping rather than the same one with a caveat.
void
captureWiimote(const WPADData &data, u32 expansion, CControllerState &state,
	StickAccumulator &sticks, const StickSettings &settings)
{
	const u32 buttons = data.btns_h;
	setButton(state.Circle, buttons & WPAD_BUTTON_B);
	setButton(state.Cross, buttons & WPAD_BUTTON_A);
	setButton(state.Square, buttons & WPAD_BUTTON_1);
	setButton(state.Triangle, buttons & WPAD_BUTTON_2);
	setButton(state.Start, buttons & WPAD_BUTTON_PLUS);
	setButton(state.Select, buttons & WPAD_BUTTON_MINUS);

	// CPad::GetPedWalkUpDown and GetSteeringUpDown weigh the D-pad against the
	// left stick at 255/2, so on a bare Wiimote the D-pad alone walks and drives
	// without any synthetic stick behind it.
	setButton(state.DPadUp, buttons & WPAD_BUTTON_UP);
	setButton(state.DPadDown, buttons & WPAD_BUTTON_DOWN);
	setButton(state.DPadLeft, buttons & WPAD_BUTTON_LEFT);
	setButton(state.DPadRight, buttons & WPAD_BUTTON_RIGHT);

	if(expansion != WPAD_EXP_NUNCHUK)
		return;

	// libogc reports the expansion's buttons twice: merged into the Wiimote mask
	// (WPAD_NUNCHUK_BUTTON_* are the low bits shifted up by 16) and in the
	// expansion struct's own byte.  Either is normally enough; taking both costs
	// one OR and removes a whole class of "works on my Wiimote" difference.
	const nunchuk_t &nunchuk = data.exp.nunchuk;
	setButton(state.RightShoulder1, (buttons & WPAD_NUNCHUK_BUTTON_Z) ||
		(nunchuk.btns_held & NUNCHUK_BUTTON_Z));
	setButton(state.LeftShoulder1, (buttons & WPAD_NUNCHUK_BUTTON_C) ||
		(nunchuk.btns_held & NUNCHUK_BUTTON_C));

	float x, y;
	readJoystick(nunchuk.js, x, y);
	addStick(x, -y, settings.leftDeadzone, sticks.leftX, sticks.leftY);
}

// Offset from the centre of the screen turned into a turn rate, in the units
// CPad::GetMouseX and GetMouseY are read in, PER SECOND -- the caller decides
// how long to apply it for, which is what lets the rate outlive the frame it was
// measured in when tracking drops out.  Returns false when the pointer is inside
// the dead zone, which is what leaves the sticks in charge of the camera while
// the player is not aiming anywhere in particular.
bool
irPointerRate(const WPADData &data, float &outX, float &outY)
{
	const float width = (float)RsGlobal.maximumWidth;
	const float height = (float)RsGlobal.maximumHeight;
	if(width <= 0.0f || height <= 0.0f)
		return false;

	// Normalise BOTH axes by half the HEIGHT, not by each axis' own half extent.
	// Dividing x by w/2 and y by h/2 stretches the vector horizontally, so a
	// pointer 45 degrees up and right would not turn the camera 45 degrees up and
	// right, and matching the direction at every angle is the whole point.  The
	// cost is that the maximum yaw rate arrives about three quarters of the way
	// to the left and right edges rather than at the edge itself, which given
	// kPointerSaturation is no cost at all.
	const float half = height*0.5f;
	const float unitX = (data.ir.x - width*0.5f)/half;
	const float unitY = (data.ir.y - height*0.5f)/half;

	// Radial dead zone, not a per axis one.  A square dead zone lets a pointer
	// inside it on X but outside on Y turn the camera straight up, which is the
	// "near the middle it only moves vertically" complaint.
	const float magnitude = std::sqrt(unitX*unitX + unitY*unitY);
	if(magnitude <= kPointerDeadzone)
		return false;

	float t = (magnitude - kPointerDeadzone)/(kPointerSaturation - kPointerDeadzone);
	if(t > 1.0f)
		t = 1.0f;
	const float curve = t*(kPointerCurveLinear + (1.0f - kPointerCurveLinear)*t);

	// The unit direction comes from the raw vector, so only the SPEED goes
	// through the curve.  Curving each axis on its own would bend diagonals
	// towards the nearer axis, the same directional error the normalisation
	// above avoids.
	const float rate = kPointerRatePerSec*curve;
	outX = (unitX/magnitude)*rate;
	outY = (unitY/magnitude)*rate*kPointerPitchScale;
	return true;
}

// Ends any hold in progress, so a rate that was being replayed while tracking
// was lost cannot survive into whatever the pointer does next.
void
stopPointerHold(void)
{
	s_heldRateX = 0.0f;
	s_heldRateY = 0.0f;
	s_heldSeconds = kPointerHoldSeconds;
}

} // namespace

void
WiiPadInitialise(int pointerWidth, int pointerHeight)
{
	PAD_Init();
	WPAD_Init();

	// wiiuse picks the report mode itself, from three state flags rather than
	// from the format asked for here: an attached expansion is what adds the
	// expansion bytes (report 0x34 without acceleration, 0x35 with it, 0x37 with
	// the pointer as well), so a Classic Controller would have reported its
	// sticks under any of these.  What this call does decide is the pointer,
	// which arrives only in the IR modes and which nothing else can turn on.
	WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);

	// ir.x and ir.y are the only pair of the three coordinate spaces in ir_t
	// expressed in a resolution of our choosing, and this is where that
	// resolution is set.  Without it they stay in wiiuse's default space and
	// every offset measured against the centre of the screen is wrong.
	WPAD_SetVRes(WPAD_CHAN_ALL, (u32)pointerWidth, (u32)pointerHeight);
}

void
WiiPadScan(void)
{
	s_connectedGameCubePads = (uint32)PAD_ScanPads();
	WPAD_ScanPads();

	// HOME is what a Wii player reaches for to leave a game, and it is not part
	// of any CControllerState, so it is handled here rather than mapped.  Both
	// masks are tested together because btns_d carries the Classic Controller's
	// buttons in its top half, so one test covers the remote and the pad on it.
	// It raises the same RsGlobal.quit the power and reset buttons raise; there
	// is no HOME overlay to fall back on, so this DISCARDS unsaved progress.
	if(WPAD_ButtonsDown(WPAD_CHAN_0) & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)){
		WiiTraceReport("WII pad: HOME pressed\n");
		HandleExit();
	}

	// Frame time for the pointer's rate camera.  gettime() is the timebase, which
	// is monotonic and always alive here, unlike CTimer, which stops with the
	// game and would freeze the pointer along with it in any paused state.
	const u64 now = gettime();
	if(s_pointerLastTime != 0){
		const float dt = (float)diff_usec(s_pointerLastTime, now)*1e-6f;
		s_pointerDt = dt < kMinPointerDt ? kMinPointerDt :
			(dt > kMaxPointerDt ? kMaxPointerDt : dt);
	}
	s_pointerLastTime = now;
}

void
WiiPadCapture(int padID, CControllerState &state)
{
	const StickSettings settings = currentStickSettings();

	StickAccumulator sticks = { 0.0f, 0.0f, 0.0f, 0.0f };
	captureGameCube(padID, s_connectedGameCubePads, state, sticks, settings);

	WPADData *data = WPAD_Data(padID);
	if(data != nullptr){
		const u32 expansion = probeExpansion(padID, *data);
		if(expansion == WPAD_EXP_CLASSIC)
			captureClassic(*data, state, sticks, settings);
		else
			captureWiimote(*data, expansion, state, sticks, settings);
	}

	state.LeftStickX = toAxis(sticks.leftX, settings.leftSensitivityX);
	state.LeftStickY = toAxis(sticks.leftY, settings.leftSensitivityY);
	state.RightStickX = toAxis(sticks.rightX, settings.rightSensitivityX);
	state.RightStickY = toAxis(sticks.rightY, settings.rightSensitivityY);
}

void
WiiPadCaptureMouse(CMouseControllerState &state)
{
	state.Clear();
	state.x = 0.0f;
	state.y = 0.0f;

	WPADData *data = WPAD_Data(WPAD_CHAN_0);
	if(data == nullptr){
		stopPointerHold();
		return;
	}

	const bool tracked = data->ir.valid != 0;

	if(FrontEndMenuManager.m_bMenuActive){
		// A cursor, absolutely: aiming at an option has to put the cursor on that
		// option.  The frontend clamps and re-reads these itself.  Nothing is held
		// here -- a cursor that carried on drifting after the pointer was lost
		// would walk off the option the player had just settled on.
		if(tracked){
			FrontEndMenuManager.m_nMouseTempPosX = (int32)data->ir.x;
			FrontEndMenuManager.m_nMouseTempPosY = (int32)data->ir.y;
		}

		// The click, and only in here.  In game these same two buttons are already
		// Circle and Cross on the pad state, and CControllerConfigManager::
		// AffectPadFromMouse would bind them a second time to whatever the mouse
		// is configured for -- one press firing two actions.  Read whether or not
		// the pointer is tracked, so a click still lands on wherever the cursor
		// was last left.
		state.LMB = (data->btns_h & WPAD_BUTTON_B) != 0;
		state.RMB = (data->btns_h & WPAD_BUTTON_A) != 0;
		stopPointerHold();
		return;
	}

	if(tracked){
		float rateX, rateY;
		if(irPointerRate(*data, rateX, rateY)){
			s_heldRateX = rateX;
			s_heldRateY = rateY;
			s_heldSeconds = 0.0f;
		}else{
			// Aimed near the middle on purpose, which is a request to STOP rather
			// than a tracking dropout.  Nothing to hold.
			stopPointerHold();
		}
	}else{
		// Aimed past the edge of the sensor's field while still turning.  Keep
		// going the same way until the hold runs out.
		s_heldSeconds += s_pointerDt;
		if(s_heldSeconds >= kPointerHoldSeconds)
			stopPointerHold();
	}

	if(s_heldRateX == 0.0f && s_heldRateY == 0.0f)
		return;

	// Cam.cpp only reaches for the mouse when one of these is non-zero and
	// otherwise falls back to CPad::LookAroundLeftRight, so the dead zone above
	// is also what hands the camera back to the sticks.  The inversion flags are
	// the frontend's, applied the same way the DirectInput path applies them.
	const float deltaX = s_heldRateX*s_pointerDt;
	const float deltaY = s_heldRateY*s_pointerDt;
	state.x = MousePointerStateHelper.bInvertHorizontally ? -deltaX : deltaX;
	state.y = MousePointerStateHelper.bInvertVertically ? -deltaY : deltaY;
}

void
WiiPadUpdateRumble(void)
{
	CPad *pad = CPad::GetPad(0);

	// Spending the duration down is this backend's job, the same way it is the
	// XInput path's in Pad.cpp and glfw's in glfw.cpp.  Without it StartShake
	// only ever raises ShakeDur and the motors would never stop.
	if(pad->ShakeDur < CTimer::GetTimeStepInMilliseconds())
		pad->ShakeDur = 0;
	else
		pad->ShakeDur -= CTimer::GetTimeStepInMilliseconds();
	if(pad->ShakeDur == 0)
		pad->ShakeFreq = 0;

	// Both motors are on/off, with no speed to set, so the frequency the game
	// asked for can only decide whether they run at all.
	const int running = pad->ShakeFreq != 0 ? 1 : 0;
	WPAD_Rumble(WPAD_CHAN_0, running);
	PAD_ControlMotor(PAD_CHAN0, running ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
}
