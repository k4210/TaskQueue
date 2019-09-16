#include "TaskQueue.h"

using namespace TQ;

TaskQueue& TaskQueue::Get()
{
	static TaskQueue sInstance;
	return sInstance;
}

TaskQueue::TaskQueue()
{
	budgets[CategoryToInt(ECategory::Unknown)]	= TMicrosecond{ 1000 };
	budgets[CategoryToInt(ECategory::A)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::B)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::C)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::_Count)]	= TMicrosecond{ 16000 };
}

void TaskQueue::AddTask(TaskInfo info, std::function<void()>&& delegate_func)
{
	Task task;
	task.delegate_func = std::move(delegate_func);
	task.info = info;
	task.source_frame = frame;
	switch (task.info.priority)
	{
		case EPriority::CanWait:	can_wait_queue	.emplace_back(std::move(task));	break;
		case EPriority::Tick:		tick_queue		.emplace_back(std::move(task));	break;
		case EPriority::Immediate:	immediate_queue	.emplace_back(std::move(task));	break;
	}
}

uint32_t TaskQueue::Remove(TaskInfo Info)
{
	//TODO:
	return 0;
}

TMicrosecond TaskQueue::GetCurrentTime() const
{
	return std::chrono::duration_cast<TMicrosecond>(
		std::chrono::system_clock::now().time_since_epoch());
}

//TODO: Stats with ifdef
void TaskQueue::ExecuteTick(TMicrosecond whole_tick_time)
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
		const ECategory cat = task.info.category;
		task.delegate_func();
		immediate_queue.pop_front();
		update_time(local_budgets[CategoryToInt(cat)]);
	}

	for (auto& task : tick_queue)
	{
		//TODO: remember last executed tick, start execution from next

		auto& local_budget = local_budgets[CategoryToInt(task.info.category)];
		if (local_budget <= TMicrosecond::zero())
			continue;

		task.delegate_func();
		update_time(local_budget);
	}

	while (can_wait_queue.size())
	{
		Task& task = can_wait_queue.front();
		auto& local_budget = local_budgets[CategoryToInt(task.info.category)];
		if (local_budget <= TMicrosecond::zero())
			continue;

		task.delegate_func();
		can_wait_queue.pop_front();
		update_time(local_budget);
	}

	//TODO: SkipAfter16Frames

	//TODO: Whole tick time -> if some time is left, then do tasks from exceeded budgets

	frame++;
}