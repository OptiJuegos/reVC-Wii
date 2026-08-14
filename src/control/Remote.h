#ifndef __GTA_REMOTE_H__
#define __GTA_REMOTE_H__

class CRemote
{
public:
	static void GivePlayerRemoteControlledCar(float, float, float, float, uint16);
	static void TakeRemoteControlledCarFromPlayer(bool blowUp = true);
};

#endif // __GTA_REMOTE_H__
