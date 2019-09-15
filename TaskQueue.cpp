#include "TaskQueue.h"

using namespace TQ;

static TaskQueue __instance = TaskQueue{};

TaskQueue& TaskQueue::get()
{
	return __instance;
}

TaskQueue::TaskQueue()
{
	budgets[CategoryToInt(ECategory::Unknown)]	= TMicrosecond{ 1000 };
	budgets[CategoryToInt(ECategory::A)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::B)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::C)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::_Count)]	= TMicrosecond{ 16000 };
}

void TaskQueue::AddTask(Task&& task)
{
	switch (task.receiver.priority)
	{
		case EPriority::CanWait:	can_wait_queue.emplace_back(std::move(task));	break;
		case EPriority::Tick:		tick_queue.emplace_back(std::move(task));		break;
		case EPriority::Immediate:	immediate_queue.emplace_back(std::move(task));	break;
	}
}

TaskQueue::TMicrosecond TaskQueue::GetCurrentTime() const
{
	return std::chrono::duration_cast<TMicrosecond>(std::chrono::system_clock::now().time_since_epoch());
}

void TaskQueue::Execute()
{
	std::array<TMicrosecond, kCategoryNum> local_budgets = budgets;

	TMicrosecond current_time = GetCurrentTime();
	auto update_time = [&](TMicrosecond& budget)
	{
		auto old_time = current_time;
		current_time = GetCurrentTime();
		budget -= current_time - old_time;
	};

	while (immediate_queue.size())
	{
		Task& task = immediate_queue.front();
		const ECategory cat = task.receiver.category;
		task.delegate_func();
		immediate_queue.pop_front();
		update_time(local_budgets[CategoryToInt(cat)]);
	}

	for (auto& task : tick_queue)
	{
		auto& local_budget = local_budgets[CategoryToInt(task.receiver.category)];
		if (local_budget <= TMicrosecond::zero())
			continue;

		task.delegate_func();
		update_time(local_budget);
	}

	while (can_wait_queue.size())
	{
		Task& task = can_wait_queue.front();
		auto& local_budget = local_budgets[CategoryToInt(task.receiver.category)];
		if (local_budget <= TMicrosecond::zero())
			continue;

		task.delegate_func();
		can_wait_queue.pop_front();
		update_time(local_budget);
	}
}