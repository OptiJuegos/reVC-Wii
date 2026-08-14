#ifndef __GTA_RPANISOT_H__
#define __GTA_RPANISOT_H__

RwInt8      RpAnisotGetMaxSupportedMaxAnisotropy(void);
RwTexture    *RpAnisotTextureSetMaxAnisotropy(RwTexture *tex, RwInt8 val);
RwInt8       RpAnisotTextureGetMaxAnisotropy(RwTexture *tex);
RwBool       RpAnisotPluginAttach(void);

#endif // __GTA_RPANISOT_H__
