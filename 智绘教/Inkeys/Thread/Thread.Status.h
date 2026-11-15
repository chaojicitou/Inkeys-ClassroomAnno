#pragma once

#include "../../IdtMain.h"

#undef max
#undef min
#include "libcuckoo/cuckoohash_map.hh"

namespace Inkeys::Thread
{
	//线程状态管理
	inline libcuckoo::cuckoohash_map<string, bool> threadStatus;

	// 设置线程状态
	inline void SetStatus(const string& key, bool down)
	{
		threadStatus.insert_or_assign(key, down);
	}
	// 获取线程状态
	inline bool GetStatus(const string& key)
	{
		bool down = false;
		threadStatus.find(key, down);
		return down;
	}

	// 线程状态守卫类，构造时设置状态为 true，析构时设置状态为 false
	class StatusGuard
	{
	public:
		StatusGuard(const string& keyInput)
		{
			key = keyInput;
			SetStatus(key, true);
		}
		~StatusGuard()
		{
			SetStatus(key, false);
		}

		StatusGuard(const StatusGuard&) = delete;
		StatusGuard& operator=(const StatusGuard&) = delete;

	private:
		string key;
	};
}
