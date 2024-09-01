/*
 CircularBuffer.h - Circular buffer library for Arduino.
 Copyright (c) 2017 Roberto Lo Giacco.

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

/**
 * @brief Implements a circular buffer that supports LIFO and FIFO operations.
 *
 * @tparam T The type of the data to store in the buffer.
 * @tparam S The maximum number of elements that can be stored in the buffer.
 */
template<typename T, uint32_t S>
class CircularBuffer
{
public:
	/**
	 * @brief The buffer capacity.
	 */
	static constexpr uint32_t kCapacity = S;

	CircularBuffer() = default;

	CircularBuffer(const CircularBuffer&) = delete;
	CircularBuffer(CircularBuffer&&) = delete;
	CircularBuffer& operator=(const CircularBuffer&) = delete;
	CircularBuffer& operator=(CircularBuffer&&) = delete;

	/**
	 * @brief Adds an element to the beginning of buffer.
	 *
	 * @return `false` iff the addition caused overwriting to an existing element.
	 */
	bool unshift(const T& value)
	{
		if (head == buffer) {
			head = buffer + kCapacity;
		}
		*--head = value;
		if (count == kCapacity) {
			if (tail-- == buffer) {
				tail = buffer + kCapacity - 1;
			}
			return false;
		}
		else {
			if (count++ == 0) {
				tail = head;
			}
			return true;
		}
	}

	/**
	 * @brief Adds an element to the end of buffer.
	 *
	 * @return `false` iff the addition caused overwriting to an existing element.
	 */
	bool push(const T& value)
	{
		if (++tail == buffer + kCapacity) {
			tail = buffer;
		}
		*tail = value;
		if (count == kCapacity) {
			if (++head == buffer + kCapacity) {
				head = buffer;
			}
			return false;
		}
		else {
			if (count++ == 0) {
				head = tail;
			}
			return true;
		}
	}

	/**
	 * @brief Removes an element from the beginning of the buffer.
	 */
	T shift()
	{
		assert(!isEmpty());

		T result = *head++;
		if (head >= buffer + kCapacity) {
			head = buffer;
		}
		count--;
		return result;
	}

	/**
	 * @brief Removes an element from the end of the buffer.
	 */
	T pop()
	{
		assert(!isEmpty());

		T result = *tail--;
		if (tail < buffer) {
			tail = buffer + kCapacity - 1;
		}
		count--;
		return result;
	}

	/**
	 * @brief Returns the element at the beginning of the buffer.
	 *
	 * @return The element at the beginning of the buffer.
	 */
	const T& first() const { assert(!isEmpty()); return *head; }

	/**
	 * @brief Returns the element at the end of the buffer.
	 *
	 * @return The element at the end of the buffer.
	 */
	const T& last() const { assert(!isEmpty()); return *tail; }

	/**
	 * @brief Array-like access to buffer.
	 */
	const T& operator [] (uint32_t index) const
	{
		assert(index < count);
		assert(!isEmpty());

		return *(buffer + ((head - buffer + index) % kCapacity));
	}

	/**
	 * @brief Returns how many elements are actually stored in the buffer.
	 *
	 * @return The number of elements stored in the buffer.
	 */
	uint32_t size() const { return count; }

	/**
	 * @brief Returns how many elements can be safely pushed into the buffer.
	 *
	 * @return The number of elements that can be safely pushed into the buffer.
	 */
	uint32_t available() const { return kCapacity - count; }

	/**
	 * @brief Check if the buffer is empty.
	 *
	 * @return `true` iff no elements can be removed from the buffer.
	 */
	bool isEmpty() const { return count == 0; }

	/**
	 * @brief Check if the buffer is full.
	 *
	 * @return `true` if no elements can be added to the buffer without overwriting existing elements.
	 */
	bool isFull() const { return count == kCapacity; }

	/**
	 * @brief Resets the buffer to a clean status, making all buffer positions available.
	 *
	 * @note This does not clean up any dynamically allocated memory stored in the buffer.
	 * Clearing a buffer that points to heap-allocated memory may cause a memory leak, if it's not properly cleaned up.
	 */
	void clear()
	{
		head = tail = buffer;
		count = 0;
	}

private:
	T buffer[kCapacity];
	T* head = buffer;
	T* tail = buffer;
	uint32_t count = 0;
};
