#pragma once

#include <vector>
#include <deque>
#include <functional>
#include <array>
#include <optional>
#include <chrono>

namespace TQ
{
	enum class ECategory : uint8_t
	{
		Unknown,
		A,
		B,
		C,
		_Count
	};

	constexpr uint8_t CategoryToInt(ECategory c) { return static_cast<uint8_t>(c); }

	enum class EPriority : uint8_t
	{
		SkipAfter16Frames,
		CanWait,
		Tick,
		Immediate,
	};

	struct ID
	{
	private:
		uint16_t data = 0;
	public:
		static ID New()
		{
			static uint16_t id = 0;
			id++;
			ID result;
			result.data = id;
			return result;
		}
	};

	struct TaskInfo
	{
		ID id;
		ECategory category = ECategory::Unknown;
		EPriority priority = EPriority::CanWait;
	};

	template<class... Args> struct Receiver 
	{
		TaskInfo info;
		std::function<void(Args...)> delegate_func;
	};

	using TMicrosecond = std::chrono::microseconds;

	class TaskQueue
	{
		static const uint32_t kCategoryNum = CategoryToInt(ECategory::_Count) + 1;
		struct Task
		{
			TaskInfo info;
			uint32_t source_frame;
			std::function<void()> delegate_func;
		};

		std::deque<Task> immediate_queue;
		std::deque<Task> can_wait_queue;
		std::deque<Task> tick_queue;
		std::array<TMicrosecond, kCategoryNum> budgets;
		uint32_t frame = 0;

		TMicrosecond GetCurrentTime() const;
		TaskQueue();
		TaskQueue(const TaskQueue&) = delete;
		TaskQueue(TaskQueue&&) = delete;
	public:
		static TaskQueue& Get();
		void AddTask(TaskInfo info, std::function<void()>&& delegate_func);
		uint32_t Remove(TaskInfo Info);
		void ExecuteTick(TMicrosecond whole_tick_time);
	};

	template<class... Args> class Sender
	{
		using TReceiver = Receiver<Args...>;
		std::optional<TReceiver> receiver;
	public:
		bool IsSet() const { receiver.has_value(); }
		void Reset() { receiver.reset(); }

		Sender() {}
		Sender(std::function<void(Args...)>&& func
			, ECategory category = ECategory::Unknown
			, EPriority priority = EPriority::CanWait)
		{
			TReceiver rec;
			rec.info.category = category;
			rec.info.priority = priority;
			rec.info.id = ID::New();
			rec.delegate_func = std::move(func);
			receiver.emplace(std::move(rec))
		}

		void Send(Args... args) const
		{
			if(receiver.has_value())
			{
				TaskQueue::Get().AddTask(receiver->info
					, std::bind(receiver->delegate_func, args...));
			}
		}

		//TODO:
	};

	template<class... Args> class SenderMultiCast
	{
		using TReceiver = Receiver<Args...>;
		std::vector<TReceiver> receivers;

	public:
		 
		ID Register(std::function<void(Args...)>&& func
			, ECategory category = ECategory::Unknown
			, EPriority priority = EPriority::CanWait)
		{
			ID id = ID::New();
			TReceiver rec;
			rec.info.category = category;
			rec.info.priority = priority;
			rec.info.id = id;
			rec.delegate_func = std::move(func);
			receivers.emplace_back(std::move(rec));
			return id;
		}

		uint32_t UnRegister(ID receiver_id)
		{
			uint32_t counter = 0;
			for (auto it = receivers.begin(); it != receivers.end(); ) 
			{
				if (receivers[it].info.receiver_id == receiver_id)
				{
					counter++;
					it = receivers.erase(it);
				}
				else
				{
					it++;
				}
			}
			return counter;
		}

		void Send(Args... args) const
		{
			for (const auto& receiver : receivers)
			{
				TaskQueue::Get().AddTask(receiver.info
					, std::bind(receiver.delegate_func, args...));
			}
		}
	};
};