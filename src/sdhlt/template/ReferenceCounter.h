// Copyright (C) 2000  Sean Cavanaugh
// This file is licensed under the terms of the Lesser GNU Public License
// (see LPGL.txt, or http://www.gnu.org/copyleft/lesser.txt)

#ifndef ReferenceCounter_H__
#define ReferenceCounter_H__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef HAVE_SYS_ATOMIC_H
#include <sys/atomic.h>
#endif

#ifdef HAVE_ASM_ATOMIC_H
#include <asm/atomic.h>
#endif

/*!
  \author  Sean Cavanaugh
  \email   sean@dimensionalrift.com
  \cvsauthor $Author: sean $
  \date    $Date: 2000/09/11 20:28:24 $
  \version $Revision: 1.1 $
  \brief   ReferenceCounter abstracts a reference counted integer with proper thread safe access
		   The interface is not platform specific in any way, except the protected data
		   is platform specific, as well as the implementation details
*/

class ReferenceCounter
{
	// Construction
public:
	ReferenceCounter();
	ReferenceCounter(int InitialValue);
	virtual ~ReferenceCounter() = default; // Should optimize to nothing except in the derived-class case
	ReferenceCounter(const ReferenceCounter &other) { copy(other); }
	auto operator=(const ReferenceCounter &other) -> ReferenceCounter &
	{
		copy(other);
		return *this;
	}

public:
	// User functions
	inline auto add(int amt) -> int;		 // increment the value by amt, returns the ORIGINAL value
	inline auto sub(int amt) -> int;		 // increment the value by amt, returns the ORIGINAL value
	inline auto inc() -> int;				 // increment the value, returns the NEW value
	inline auto dec() -> int;				 // decrement the value, returns the NEW value
	inline auto read() const -> int;		 // read the current value
	inline void write(int newvalue); // change the counter to a new value blindly
	inline auto swap(int newvalue) -> int;	 // change the counter to a new value, and return the ORIGINAL value

	// Convenient Operators
	auto operator++() -> int { return inc(); }
	auto operator--() -> int { return dec(); }
	auto operator++(int) -> int { return inc() - 1; }
	auto operator--(int) -> int { return dec() + 1; }
	auto operator+=(int amt) -> int { return add(amt) + amt; }
	auto operator-=(int amt) -> int { return sub(amt) - amt; }
	auto operator=(int value) -> int
	{
		write(value);
		return value;
	}
	operator int() const { return read(); }

	// Internal Methods
protected:
	inline void copy(const ReferenceCounter &other);

	// Data
protected:
#ifdef SINGLE_THREADED
	int m_atom;
#else // SINGLE_THREADED

#ifdef _WIN32
	long m_atom;
#endif
#ifdef HAVE_ATOMIC
	atomic_t m_atom;
#endif

#endif // SINGLE_THREADED
};

#ifdef SINGLE_THREADED
inline ReferenceCounter::ReferenceCounter()
{
	m_atom = 0;
}
inline ReferenceCounter::ReferenceCounter(int InitialValue)
{
	m_atom = InitialValue;
}
inline int ReferenceCounter::add(int amt)
{
	m_atom += amt;
	return m_atom;
}
inline int ReferenceCounter::sub(int amt)
{
	m_atom -= amt;
	return m_atom;
}
inline int ReferenceCounter::inc()
{
	m_atom++;
	return m_atom;
}
inline int ReferenceCounter::dec()
{
	m_atom--;
	return m_atom;
}
inline int ReferenceCounter::swap(int newvalue)
{
	int rval = m_atom;
	m_atom = newvalue;
	return rval;
}
inline void ReferenceCounter::write(int newvalue)
{
	m_atom = newvalue;
}
inline int ReferenceCounter::read() const
{
	return m_atom;
}
inline void ReferenceCounter::copy(const ReferenceCounter &other)
{
	m_atom = other.m_atom;
}
#else // SINGLE_THREADED

#ifdef _WIN32
inline ReferenceCounter::ReferenceCounter()
{
	m_atom = 0;
}
inline ReferenceCounter::ReferenceCounter(int InitialValue)
{
	m_atom = InitialValue;
}
inline int ReferenceCounter::add(int amt)
{
	return InterlockedExchangeAdd(&m_atom, amt);
}
inline int ReferenceCounter::sub(int amt)
{
	return InterlockedExchangeAdd(&m_atom, -amt);
}
inline int ReferenceCounter::inc()
{
	return InterlockedIncrement(&m_atom);
}
inline int ReferenceCounter::dec()
{
	return InterlockedDecrement(&m_atom);
}
inline int ReferenceCounter::swap(int newvalue)
{
	return InterlockedExchange(&m_atom, newvalue);
}
inline void ReferenceCounter::write(int newvalue)
{
	InterlockedExchange(&m_atom, newvalue);
}
inline int ReferenceCounter::read() const
{
	return m_atom;
}
inline void ReferenceCounter::copy(const ReferenceCounter &other)
{
	m_atom = other.m_atom;
}
#endif //_WIN32

#ifdef HAVE_ATOMIC
inline ReferenceCounter::ReferenceCounter()
{
	m_atom.counter = 0;
}
inline ReferenceCounter::ReferenceCounter(int InitialValue)
{
	m_atom.counter = InitialValue;
}
inline int ReferenceCounter::add(int amt)
{
	int rval = atomic_read(&m_atom);
	atomic_add(amt, &m_atom);
	return rval;
}
inline int ReferenceCounter::sub(int amt)
{
	int rval = atomic_read(&m_atom);
	atomic_sub(amt, &m_atom);
	return rval;
}
inline int ReferenceCounter::inc()
{
	int rval = atomic_read(&m_atom);
	atomic_inc(&m_atom);
	return rval + 1;
}
inline int ReferenceCounter::dec()
{
	int rval = atomic_read(&m_atom);
	atomic_dec(&m_atom);
	return rval - 1;
}
inline int ReferenceCounter::swap(int newvalue)
{
	int rval = atomic_read(&m_atom);
	atomic_set(&m_atom, newvalue);
	return rval;
}
inline void ReferenceCounter::write(int newvalue)
{
	atomic_set(&m_atom, newvalue);
}
inline int ReferenceCounter::read() const
{
	return atomic_read(&m_atom);
}
inline void ReferenceCounter::copy(const ReferenceCounter &other)
{
	m_atom.counter = other.read();
}
#endif // HAVE_ATOMIC
#endif // SINGLE_THREADED

#endif // ReferenceCounter_H__
