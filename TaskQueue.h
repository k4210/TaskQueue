#pragma once

#include <vector>
#include <deque>
#include <functional>
#include <array>
#include <chrono>

namespace TQ
{
	enum class ECategory : uint16_t
	{
		Unknown,
		A,
		B,
		C,
		_Count
	};

	constexpr uint16_t CategoryToInt(ECategory c) { return static_cast<uint16_t>(c); }

	enum class EPriority : uint16_t
	{
		CanWait,
		Tick,
		Immediate,
	};

	struct ID
	{
	private:
		uint32_t data = 0;
	public:
		static ID New()
		{
			static uint32_t id = 0;
			id++;
			ID result;
			result.data = id;
			return result;
		}
	};

	struct ReceiverBase
	{
		ECategory category = ECategory::Unknown;
		EPriority priority = EPriority::CanWait;
		ID id;
	};

	template<class... Args> struct Receiver : public ReceiverBase
	{
		std::function<void(Args...)> delegate_func;
	};

	struct Task
	{
		ReceiverBase receiver;
		std::function<void()> delegate_func;
	};

	class TaskQueue
	{
		using TMicrosecond = std::chrono::microseconds;

		static const uint32_t kCategoryNum = CategoryToInt(ECategory::_Count) + 1;

		std::deque<Task> immediate_queue;
		std::deque<Task> can_wait_queue;
		std::deque<Task> tick_queue;
		std::array<TMicrosecond, kCategoryNum> budgets;
		TMicrosecond GetCurrentTime() const;
	public:
		TaskQueue();
		static TaskQueue& get();
		void AddTask(Task&& msg);
		//TODO: Remove Tick task2
		void Execute();
	};

	template<class... Args> struct Sender
	{
	private:
		using TReceiver = Receiver<Args...>;
		std::vector<TReceiver> receivers;

	public:
		 
		ID Register(std::function<void(Args...)>&& func, ECategory category = ECategory::Unknown, EPriority priority = EPriority::CanWait)
		{
			ID id = ID::New();
			TReceiver rec;
			rec.category = category;
			rec.priority = priority;
			rec.id = id;
			rec.delegate_func = std::move(func);
			receivers.emplace_back(std::move(rec));
			return id;
		}

		uint32_t UnRegister(ID receiver_id)
		{
			uint32_t counter = 0;
			for (auto it = receivers.begin(); it != receivers.end(); ) 
			{
				if (receivers[it].receiver_id == receiver_id)
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
				Task task;
				task.receiver = receiver;
				task.delegate_func = std::bind(receiver.delegate_func, args...);
				TaskQueue::get().AddTask(std::move(task));
			}
		}
	};
};