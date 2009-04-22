#pragma once
#include <list>
#include "../../IStream.h"
#include "../../../Common/MyCom.h"

class IPipeBuffer : 
	public IInStream,
	public IOutStream,
	public IOutStreamFlush,
	public IStreamGetSize,
	public CMyUnknownImp
{
public:
	MY_UNKNOWN_IMP3(IInStream, 
		IOutStream, IStreamGetSize);

	virtual ~IPipeBuffer(){}

	virtual void wait_for_read()=0;
	virtual void wait_for_write()=0;
	virtual void barrier()=0;
	virtual IPipeBuffer* clone()=0;
};

IPipeBuffer* new_pipe_buffer();




template<class T>
class CObjectPool
{
protected:
	typedef std::list<T*> List;
	typedef typename List::iterator itertor;
	List m_list;
	int m_reserve;

	int check_free()
	{
		int free=0;
		while (m_list.size() > m_reserve)
		{
			free++;
			delete m_list.front();
			m_list.pop_front();
		}
		return free;
	}

public:
	CObjectPool(int max_reservation=100)
	{
		m_reserve = max_reservation;
	}

	~CObjectPool()
	{
		for (iterator it=m_list.begin(); 
			it!=m_list.end(); ++it)
			delete *it;
	}
	
	T* get()
	{
		T* o;
		if (!m_list.empty())
		{
			=m_list.front();
			m_list.pop_front();
		}
		else o=new T;
		return o;
	}

	void recycle(T* o)
	{
		if (check_free()>0) delete o;
		else m_list.push_back(o);
	}
};

