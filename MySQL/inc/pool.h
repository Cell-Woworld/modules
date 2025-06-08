#pragma once
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

template <typename T>
struct DefaultDeleter {
	DefaultDeleter() {};
	void operator()(T* p) const {
		delete p;
	}
};


/// \brief A blocking object pool containing objects of type T.
/// Objects burrowed from the pool are automatically returned to the pool when they
/// are out of scope.
/// \n
/// When the pool is empty calls to borrow objects blocks until a resource is returned to the pool.
template <typename T, typename Deleter = DefaultDeleter<T>>
class ObjectPool {
public:

	/// \brief Construct an object pool of size n.
	///
	/// \param n The size of the pool.
	/// \param factory A factory method that returns a pointer to a resource.
	ObjectPool(size_t n, std::function<T * ()> factory) {
		for (size_t i = 0; i < n; ++i) {
			resources.emplace_back(factory());
		}
	}

	/// \brief destruct the object pool and delete all resources using the provided deleter.
	~ObjectPool() {
		Deleter d;
		for (auto it = resources.begin(); it != resources.end(); ++it) {
			d(*it);
		}
	};

	/// \typedef Alias for a function which returns a resource to the pool.
	using ObjectReturner = std::function<void(T*)>;

	/// \brief burrow an object from the pool.
	/// When the borrowed pointer is out of scope the resource is returned to the pool.
	std::unique_ptr<T, ObjectReturner> borrow() {
		std::unique_lock<std::mutex> lock(mu);
		if (resources.empty()) {
			this->condition.wait(lock, [this] {return !this->resources.empty(); });
		}
		auto resource = std::unique_ptr<T, ObjectReturner>(this->resources.front(), [this](T* t) {
			std::unique_lock<std::mutex> lock(mu);
			resources.emplace_back(t);
			condition.notify_one();
			});
		this->resources.pop_front();
		return std::move(resource);
	}


private:
	std::deque<T*> resources;

	// synchronization
	std::mutex mu;
	std::condition_variable condition;
};