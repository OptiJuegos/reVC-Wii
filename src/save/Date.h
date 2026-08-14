#ifndef __GTA_DATE_H__
#define __GTA_DATE_H__

class CDate
{
public:
	int m_nSecond;
	int m_nMinute;
	int m_nHour;
	int m_nDay;
	int m_nMonth;
	int m_nYear;
	
	CDate();
	bool operator>(const CDate &right);
	bool operator<(const CDate &right);
	bool operator==(const CDate &right);
	void PopulateDateFields(int8 &second, int8 &minute, int8 &hour, int8 &day, int8 &month, int16 year);
};

#endif // __GTA_DATE_H__
