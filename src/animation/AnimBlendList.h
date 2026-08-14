#ifndef __GTA_ANIMBLENDLIST_H__
#define __GTA_ANIMBLENDLIST_H__

// name made up
class CAnimBlendLink
{
public:
	CAnimBlendLink *next;
	CAnimBlendLink *prev;

	void Init(void){
		next = nil;
		prev = nil;
	}
	void Prepend(CAnimBlendLink *link){
		if(next)
		        next->prev = link;
		link->next = next;
		link->prev = this;
		next = link;
	}
	void Remove(void){
		if(prev)
			prev->next = next;
		if(next)
			next->prev = prev;
		Init();
	}
};

#endif // __GTA_ANIMBLENDLIST_H__
