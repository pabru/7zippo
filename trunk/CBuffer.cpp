#include "stdafx.h"
#include <list>
#include <boost/thread.hpp>
#define INITGUID
#include "CBuffer.h"
using namespace std;

// base class for the buffer
class CAbstractBuffer
{
public:
	UInt32 read(void *data, UInt32 size)	{assert(false); return 0;}
	UInt32 write(void* data, UInt32 size)	{assert(false); return 0;}
	UInt32 size()							{assert(false); return 0;}
};

// option implementation 1: a fixed-size circular buffer 
class CCircularBuffer : public CAbstractBuffer
{
public:
	const static size_t _capacity=1024*1024;
	char m_buf[_capacity];
	size_t m_size, m_start;
	
	CCircularBuffer()
	{
		m_start = m_size = 0;
	}

	~CCircularBuffer()
	{
		assert(m_size==0);
	}

	UInt32 size()
	{
		return m_size;
	}

	UInt32 read(void *data, UInt32 size)
	{
		if (size>m_size) size=m_size;
		if (m_start+size > _capacity)
		{
			size_t t = _capacity - m_start;
			memcpy(data, m_buf+m_start, t);
			memcpy((char*)data+t, m_buf, size-t);
			m_start = size-t;
		}
		else 
		{
			memcpy(data, m_buf+m_start, size);
			m_start+=size;
		}
		m_size-=size;
		return size;
	}

	UInt32 write(const void *data, UInt32 size)
	{
		size_t room=_capacity-m_size;
		if (size>room) size=room;
		size_t end = (m_start+m_size) % _capacity;
		if (end+size>_capacity)
		{
			size_t t=_capacity-end;
			memcpy(m_buf+end, data, t);
			memcpy(m_buf, (const char*)data+t, size-t);
			//end=size-t;
		}
		else
		{
			memcpy(m_buf+end, data, size);
			//end+=size;
		}
		m_size+=size;
		return size;
	}
};

struct record
{
	size_t size;
	char* buf;
};
// a dynamic-sized queue buffer
class CQBuffer : public CAbstractBuffer
{
public:
	std::list<record> m_queue;
	size_t m_offset, m_size;

	CQBuffer()
	{
		m_offset = m_size = 0;
	}

	UInt32 size()
	{
		return m_size;
	}

	UInt32 read(void *data, UInt32 size)
	{
		assert(m_queue.front().size > m_offset);
		size_t left = m_queue.front().size - m_offset;
		if (left>size)
		{
			memcpy(data, m_queue.front().buf+m_offset, size);
			m_offset+=size;
			m_size-=size;
			return size;
		}
		else
		{
			memcpy(data, m_queue.front().buf+m_offset, left);
			delete m_queue.front().buf;
			m_queue.pop_front();
			m_offset=0;
			m_size-=left;
			return left;
		}
	}
	UInt32 write(const void* data, UInt32 size)
	{
		record rec;
		rec.size=size;
		rec.buf=new char[size];
		memcpy(rec.buf, data, size);
		m_queue.push_back(rec);
		m_size += size;
		return size;
	}
};

// add synchornization
class CSynchornizedBuffer
{
public:
	typedef CQBuffer CBuffer;
	//typedef CCircularBuffer CBuffer;

	typedef boost::mutex::scoped_lock Lock;
	const static size_t _capacity=1024*1024;
	bool m_reseting, m_closing, m_sealed;
	Int64 m_total_size, m_written_size, m_read_size;
	size_t m_ref_count;
	CBuffer m_buffer;

	CSynchornizedBuffer() : m_barrier(2)
	{
		m_ref_count = 1;
		m_total_size = m_read_size = m_written_size = 0;
		m_reseting = m_closing = false;
		m_sealed = true;
	}

	~CSynchornizedBuffer()
	{
	}

	size_t add_ref()
	{
		Lock lock(m_mutex);
		return ++m_ref_count;
	}

	size_t dec_ref()
	{
		Lock lock(m_mutex);
		return --m_ref_count;
	}

	HRESULT read(void *data, UInt32 size, UInt32 *processedSize)
	{
		Lock lock(m_mutex);
		if (m_read_size==m_total_size)
		{
			*processedSize = 0;
			m_sealed=true;
			return S_OK;
		}
		while (m_buffer.size()==0)
			m_buffer_not_empty.wait(lock);

		try 
		{
			*processedSize = m_buffer.read(data, size);
			m_read_size += *processedSize;
			//printf("CBuffer[%p]: %u bytes read\n", this, *processedSize);
		}
		catch(...)
		{
			assert(false);
		}

		m_buffer_not_full.notify_all();
		lock.unlock();
		//if (m_read_size==m_total_size)
		//	finalize();
		return S_OK;
	}

	HRESULT write(const void *data, UInt32 size, UInt32 *processedSize)
	{
		Lock lock(m_mutex);
		while (m_buffer.size()>_capacity)
			m_buffer_not_full.wait(lock);

		try 
		{
			*processedSize = m_buffer.write(data, size);
			m_written_size += *processedSize;
			//printf("CBuffer[%p]: %u bytes written\n", this, *processedSize);
		}
		catch(...)
		{
			assert(false);
		}

		m_buffer_not_empty.notify_all();
		lock.unlock();
		//if (m_written_size==m_total_size)
		//	finalize();
		return S_OK;
	}

	Int64 total_size()
	{
		return m_total_size;
	}

	void total_size(Int64 size)
	{
		m_total_size = size;
		m_read_size = m_written_size = 0;
	}

	HRESULT flush()
	{
		Lock lock(m_mutex);
		while (!m_sealed)
			m_buffer_not_full.wait(lock);
		m_sealed=false;
		return S_OK;
	}

	void finalize()
	{
		m_barrier.wait();
	}

	void wait_for_read()
	{
		Lock lock(m_mutex);
		size_t old_size=m_buffer.size();
		while (old_size==m_buffer.size())
			m_buffer_not_full.wait(lock);
	}
	void wait_for_write()
	{
		Lock lock(m_mutex);
		size_t old_size=m_buffer.size();
		while (old_size==m_buffer.size())
			m_buffer_not_empty.wait(lock);
	}

	boost::condition_variable m_buffer_not_empty;
	boost::condition_variable m_buffer_not_full;
	boost::barrier m_barrier;
	boost::mutex m_mutex;

};

// interface
class CTwiceBuffer : public IPipeBuffer
{
public:
	CSynchornizedBuffer* m_buffer;
	CTwiceBuffer()
	{
		m_buffer=new CSynchornizedBuffer;
	}
	CTwiceBuffer(CSynchornizedBuffer* buffer)
	{
		m_buffer=buffer;
		m_buffer->add_ref();
	}
	virtual ~CTwiceBuffer()
	{
		//m_buffer->finalize();
		if (m_buffer->dec_ref()==0)
			delete m_buffer;
	}
	virtual void wait_for_read()
	{
		m_buffer->wait_for_read();
	}
	virtual void wait_for_write()
	{
		m_buffer->wait_for_write();
	}
	virtual IPipeBuffer* clone()
	{
		return new CTwiceBuffer(m_buffer);
	}
	STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize)
	{
		HRESULT hr=m_buffer->read(data, size, processedSize);
		//m_buffer->wait_for_write();
		return hr;
	}
	STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize)
	{
		HRESULT hr=m_buffer->write(data, size, processedSize);
		//m_buffer->wait_for_read();
		return hr;
	}
	STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)
	{
		assert(false);
		return S_OK;
	}
	STDMETHOD(GetSize)(UInt64 *size)
	{
		*size = m_buffer->total_size();
		return S_OK;
	}
	STDMETHOD(SetSize)(Int64 newSize)
	{
		m_buffer->total_size(newSize);
		return S_OK;
	}
	STDMETHOD(Flush)()
	{
		return m_buffer->flush();
	}
	virtual void barrier()
	{
		m_buffer->finalize();
	}
};

IPipeBuffer* new_pipe_buffer()
{
	return new CTwiceBuffer;
}
