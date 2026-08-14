#ifndef __GTA_DIRECTORY_H__
#define __GTA_DIRECTORY_H__

class CDirectory
{
public:
	struct DirectoryInfo {
		uint32 offset;
		uint32 size;
		char name[24];

		void ConvertFromLittleEndian();
	};
	DirectoryInfo *entries;
	int32 maxEntries;
	int32 numEntries;

	CDirectory(int32 maxEntries);
	~CDirectory(void);

	void ReadDirFile(const char *filename);
	bool WriteDirFile(const char *filename);
	void AddItem(const DirectoryInfo &dirinfo);
	void AddItem(const DirectoryInfo &dirinfo, int32 imgId);
	bool FindItem(const char *name, uint32 &offset, uint32 &size);
};

#endif // __GTA_DIRECTORY_H__
