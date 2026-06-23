/****************************************************************************
Created by: Arlin (arlins.dps@gmail.com).

This file is wrapper for accessing private class members
without modifying the class definition.

The principle is to use the compiler template instantiation mechanism,
which does not check member permissions, to obtain a pointer to the member,
and then use the member pointer to access the member.

Example:
	class TargetObject {
	private:
		int value = 42;
		void hello(int i) { printf("Hello: %d\n", i); }
	};

	// 1. Define MemberProxy
	// Tag 0 is used to distinguish same types
	template struct kd::MemberProxy<int TargetObject::*, 0, &TargetObject::value>;
	template struct kd::MemberProxy<void(TargetObject::*)(int), 0, &TargetObject::hello>;

	// 2. Access
	TargetObject obj;

	// Access Variable
	auto pVal = kd::MemberAccessor<int TargetObject::*, 0>::ptr;
	obj.*pVal = 100;

	// Call Function
	auto pFn = kd::MemberAccessor<void(TargetObject::*)(int), 0>::ptr;
	(obj.*pFn)(123);
****************************************************************************/

#pragma once
#include "koda/kd_global.h"

__NAMESPACE_KD_BEGIN

// Tag is used to distinguish same types
template<typename Type, int tag>
struct MemberAccessor {
	static Type ptr;
	static int save(Type _ptr) {
		ptr = _ptr;
		return 0;
	}
};
template<typename Type, int tag>
Type MemberAccessor<Type, tag>::ptr = nullptr;

template<typename Type, int tag, Type member>
struct MemberProxy {
	static int value;
};
template<typename Type, int tag, Type member>
int MemberProxy<Type, tag, member>::value = MemberAccessor<Type, tag>::save(member);

__NAMESPACE_KD_END
