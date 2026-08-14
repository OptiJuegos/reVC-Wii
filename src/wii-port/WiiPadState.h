#pragma once

#ifdef WII_PLATFORM

#include <cstdint>

// Controller actions reserved while the common on-screen keyboard owns input.
// The snapshot is assembled from GameCube, Wiimote and Classic Controller, so
// the GUI does not need to care which controller is connected.
enum WiiTextKey : std::uint32_t
{
	WII_TEXT_LEFT   = 1u << 0,
	WII_TEXT_RIGHT  = 1u << 1,
	WII_TEXT_UP     = 1u << 2,
	WII_TEXT_DOWN   = 1u << 3,
	WII_TEXT_TYPE   = 1u << 4,
	WII_TEXT_BACK   = 1u << 5,
	WII_TEXT_SPACE  = 1u << 6,
	WII_TEXT_SHIFT  = 1u << 7,
	WII_TEXT_ENTER  = 1u << 8,
	WII_TEXT_CLOSE  = 1u << 9,
};

struct WiiTextInputSnapshot
{
	std::uint32_t held = 0;
	std::uint32_t pressed = 0;
	bool irValid = false;
	int irX = 0;
	int irY = 0;
	int irWidth = 0;
	int irHeight = 0;
};

WiiTextInputSnapshot wiiTextInputSnapshot();

#endif
