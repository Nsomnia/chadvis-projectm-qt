#pragma once
/*
 * JThread.hpp - portable std::jthread / std::stop_token facade.
 *
 * Provides vc::JThread / vc::StopToken. On toolchains with full C++20
 * thread support these are aliases of the standard types; otherwise a
 * minimal, behavior-compatible implementation backs them with std::thread
 * plus an atomic stop flag (AppleClang's libc++ still ships neither type).
 *
 * Covered API surface (exactly what this project uses):
 *   - Construction with any callable, optionally accepting a stop token as
 *     its first argument (same overload rule as std::jthread).
 *   - request_stop() / join() / joinable().
 *   - RAII: destruction requests stop and joins, matching std::jthread.
 *
 * Not covered (unused here): stop_callback, get_stop_source(), handle(),
 * native_handle(), stop_possible().
 */

#include <atomic>
#include <concepts>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__cpp_lib_jthread)

#include <thread>

namespace vc {

using JThread = std::jthread;
using StopToken = std::stop_token;

} // namespace vc

#else

namespace vc {

class StopToken {
public:
	StopToken() = default;

	[[nodiscard]] bool stop_requested() const noexcept {
		return state_ && state_->load(std::memory_order_relaxed);
	}

private:
	friend class JThread;
	explicit StopToken(std::shared_ptr<std::atomic_bool> state)
			: state_(std::move(state)) {}

	std::shared_ptr<std::atomic_bool> state_;
};

class JThread {
public:
	JThread() = default;

	template <typename F, typename... Args>
		requires std::invocable<std::decay_t<F>, StopToken, std::decay_t<Args>...> ||
		         std::invocable<std::decay_t<F>, std::decay_t<Args>...>
	explicit JThread(F&& f, Args&&... args) {
		using DecayedF = std::decay_t<F>;

		if constexpr (std::invocable<DecayedF, StopToken, std::decay_t<Args>...>) {
			stopFlag_ = std::make_shared<std::atomic_bool>(false);
			StopToken token{stopFlag_};
			thread_ = std::thread(
					[f = DecayedF(std::forward<F>(f)),
			         tup = std::make_tuple(std::forward<Args>(args)...),
			         token = std::move(token)]() mutable {
						std::apply(
								[&f, &token](auto&&... unpacked) {
									std::invoke(std::move(f), token,
									            std::move(unpacked)...);
								},
								std::move(tup));
					});
		} else {
			thread_ = std::thread(DecayedF(std::forward<F>(f)),
			                      std::forward<Args>(args)...);
		}
	}

	~JThread() {
		if (joinable()) {
			request_stop();
			join();
		}
	}

	JThread(const JThread&) = delete;
	JThread& operator=(const JThread&) = delete;

	JThread(JThread&&) noexcept = default;
	// NOTE: like std::jthread, assignment stops and joins any running task
	// first; this project never move-assigns, so keep it simple and correct.
	JThread& operator=(JThread&& other) noexcept {
		if (this != &other) {
			if (joinable()) {
				request_stop();
				join();
			}
			thread_ = std::move(other.thread_);
			stopFlag_ = std::move(other.stopFlag_);
		}
		return *this;
	}

	[[nodiscard]] bool joinable() const noexcept {
		return thread_.joinable();
	}

	void join() {
		if (joinable())
			thread_.join();
	}

	void request_stop() noexcept {
		if (stopFlag_)
			stopFlag_->store(true, std::memory_order_relaxed);
	}

private:
	std::thread thread_;
	std::shared_ptr<std::atomic_bool> stopFlag_;
};

} // namespace vc

#endif // __cpp_lib_jthread
