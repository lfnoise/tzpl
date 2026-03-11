// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  tzpl_atomic_fifo.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_atomic_fifo_h
#define tzpl_atomic_fifo_h

#include <vector>
#include <atomic>

namespace engine {

//=============================================================================================
#pragma mark ATOMIC FIFO

template <class T>
class AtomicFifo
{
	// Correct and Efficient Bounded FIFO Queues, Le, Guatto, Cohen, Pop.
	// https://hal.inria.fr/hal-00911893/file/sbac13.pdf
	// SBAC-PAD 2013: International Symposium on Computer Architecture and High Performance Computing,
	// Oct 2013, Porto de Galinhas, Brazil. hal-00911893
	std::vector<T> ring_;
	std::atomic_int front_ = 0;
	std::atomic_int back_ = 0;
	int size_;
	int pfront = 0;
	int cback = 0	;

public:
	AtomicFifo(int size)
		: ring_(size), size_(size)
	{}

	bool push(T t) {
		int b = back_.load(std::memory_order_relaxed);
		if (pfront + size_ - b < 1) {
			pfront = front_.load(std::memory_order_acquire);
			if (pfront + size_ - b < 1) return false;
		}
		ring_[b % size_] = t;
		back_.store(b+1, std::memory_order_release);
		return true;
	}
	
	bool pop(T& t) {
		int f = front_.load(std::memory_order_relaxed);
		if (cback - f < 1) {
			cback = back_.load(std::memory_order_acquire);
			if (cback - f < 1) return false;
		}
		t = ring_[f % size_];
		front_.store(f+1, std::memory_order_release);
		return true;
	}
//	int numPushed() const { return back_.load(); }
//	int numPopped() const { return front_.load(); }
};


}


#endif /* tzpl_atomic_fifo_h */
