#include "config.h"
#ifdef GTA_PS2
#include <stdio.h>
#include <stdlib.h>
#include <sif.h>
#include <sifrpc.h>

#include "CdStream.h"

unsigned int gRpcOutArgBuffer[16] __attribute__ ((aligned(64)));
unsigned int gRpcInArgBuffer[16] __attribute__ ((aligned(64)));
sceSifClientData gCdStreamClientData;

void
CdStreamInit(int n)
{
	int i;

	sceSifInitRpc(0);
	while(1){
		if(sceSifBindRpc(&gCdStreamClientData, 0x65686577, 0) < 0)
			printf("sceSifBindRpc to Cdvd Stream module failed");
		if(gCdStreamClientData.serve != 0)
			break;
		i = 10000;
		while(i--);
	}
	gRpcInArgBuffer[0] = n;
	sceSifCallRpc(&gCdStreamClientData, 0, 0, gRpcInArgBuffer, 16, NULL, 0, NULL, NULL);
}

int
CdStreamRead(int chan, void *buf, unsigned int off, unsigned int size)
{
	gRpcInArgBuffer[0] = chan;
	gRpcInArgBuffer[1] = (int)buf;
	gRpcInArgBuffer[2] = off;
	gRpcInArgBuffer[3] = size;
	sceSifCallRpc(&gCdStreamClientData, 1, 0, gRpcInArgBuffer, 16, gRpcOutArgBuffer, 16, NULL, NULL);
	return gRpcOutArgBuffer[0];
}

int
CdStreamGetStatus(int chan)
{
	gRpcInArgBuffer[0] = chan;
	sceSifCallRpc(&gCdStreamClientData, 2, 0, gRpcInArgBuffer, 16, gRpcOutArgBuffer, 16, NULL, NULL);
	return gRpcOutArgBuffer[0];
}

int
CdStreamGetLastPosn(void)
{
	sceSifCallRpc(&gCdStreamClientData, 4, 0, gRpcInArgBuffer, 0, gRpcOutArgBuffer, 16, NULL, NULL);
	return gRpcOutArgBuffer[0];
}

int
CdStreamSync(int chan)
{
	gRpcInArgBuffer[0] = chan;
	sceSifCallRpc(&gCdStreamClientData, 3, 0, gRpcInArgBuffer, 16, gRpcOutArgBuffer, 16, NULL, NULL);
	return gRpcOutArgBuffer[0];
}
#endif
