#pragma once
#include "move.h"

template <typename T>
class Singleton
{
public:
    template<typename... Args>
	inline static T* GetInstance(Args&&... args)
	{
		static auto instance = T(std::forward<Args>(args)...);
		return &instance;
	}
};