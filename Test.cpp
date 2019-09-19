#include "TaskQueue.h"
#include <cstdio>
#include <memory>

using namespace TQ;

struct TArg
{
	int data = 1024;
	//TArg() { printf("Arg constructor \n"); }
	//TArg(const TArg& a) : data(a.data) { printf("Arg copy constructor <--- \n"); }
	//TArg(TArg&& a) : data(a.data) { a.data = 0; printf("Arg move constructor \n"); }
	//TArg& operator=(const TArg& a) { data = a.data; printf("Arg assign operator <--- \n"); return *this; }
	//TArg& operator=(TArg&& a) { data = a.data; a.data = 0; printf("Arg move assign operator \n"); return *this; }
	//~TArg() { printf("Arg destructor \n");
};

using Arg = const TArg&;

int main()
{
	SenderMultiCast<Arg> mc_delegate;
	auto receive = [](Arg val)
	{
		for (int i = 0; i < val.data; i++)
		{
			atan((double)i);
		}
	};
	mc_delegate.Register(std::function(receive), 0);
	mc_delegate.Register(std::function(receive), 1);
	mc_delegate.Register(std::function(receive), 0, EPriority::SkipAfter16Frames);
	using TTQ = TaskQueue<2>;
	auto& tq = TTQ::Get();
	tq.SetBudget(0, TMicrosecond{ 30 });
	tq.SetBudget(1, TMicrosecond{ 30 });
	for (int i = 0; i < 320; i++)
	{
		mc_delegate.Send<TTQ>(TArg{});
		tq.ExecuteTick(TMicrosecond{ 100 });
	}
}