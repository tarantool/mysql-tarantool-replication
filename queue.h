#ifndef REPLICATOR_QUEUE_H
#define REPLICATOR_QUEUE_H

#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

template<typename T> class Queue
{
	public:
		Queue(const unsigned limit_) : limit(limit_) {}

		inline T pop()
		{
			std::unique_lock<std::mutex> lock(mutex);

			if (queue.empty()) {
				cv1.wait(lock, [this] { return !queue.empty(); });
			}

			T item = std::move(queue.front());
			queue.pop_front();

			lock.unlock();
			cv2.notify_all();

			return item;
		}

		inline void push(T&& item)
		{
			std::unique_lock<std::mutex> lock(mutex);

			if (queue.size() >= limit) {
				cv2.wait(lock, [this] { return queue.size() < limit; });
			}

			queue.push_back(std::forward<T>(item));

			lock.unlock();
			cv1.notify_one();
		}

		inline unsigned wait(const std::chrono::milliseconds timeout) const
		{
			std::unique_lock<std::mutex> lock(mutex);

			if (!queue.empty() || cv1.wait_for(lock, timeout, [this] { return !queue.empty(); }))
				return queue.size();

			return 0;
		}

		inline unsigned size() const {
			// std::lock_guard<std::mutex> lock(mutex);
			return queue.size();
		}

	private:
		std::deque<T> queue;
		mutable std::mutex mutex;
		mutable std::condition_variable cv1;
		mutable std::condition_variable cv2;
		const unsigned limit;
};

#endif // REPLICATOR_QUEUE_H
