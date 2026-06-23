#include "koda/kd_global.h"
#include "koda/base/kd_memberaccessor.h"

namespace kd_mem_access_test {
//
class TargetObject {
private:
	std::string str_val = "Secret";
	int int_val1 = 100;
	int int_val2 = 200;
	void hello1(const std::string& s) { printf("hello1: %s\n", s.c_str()); }
	void hello1(int i) { printf("hello1: %d\n", i); }
	void hello2(int i) { printf("hello2: %d\n", i); }
	virtual void vt_hello3(int i) { printf("vt_hello3: %d\n", i); };
};
}; // namespace kd_mem_access_test

// MemberProxy must be in kd namespace on Xcode
namespace kd {
using namespace kd_mem_access_test;
// Accessor defines for member variables
template struct MemberProxy<std::string TargetObject::*, 0, &TargetObject::str_val>;
template struct MemberProxy<int TargetObject::*, 0, &TargetObject::int_val1>;
template struct MemberProxy<int TargetObject::*, 1, &TargetObject::int_val2>;

// Accessor defines for member functions
template struct MemberProxy<void(TargetObject::*)(const std::string& s), 0, &TargetObject::hello1>;
template struct MemberProxy<void(TargetObject::*)(int), 0, &TargetObject::hello1>;
template struct MemberProxy<void(TargetObject::*)(int), 1, &TargetObject::hello2>;
template struct MemberProxy<void(TargetObject::*)(int), 2, &TargetObject::vt_hello3>;
};

namespace kd_mem_access_test {
static void test_main() {
	TargetObject obj;

	// Access and modify private variables
	auto pStr = kd::MemberAccessor<std::string TargetObject::*, 0>::ptr;
	auto pInt1 = kd::MemberAccessor<int TargetObject::*, 0>::ptr;
	auto pInt2 = kd::MemberAccessor<int TargetObject::*, 1>::ptr;

	printf("Before Mod -> str_val: %s, int1: %d, int2: %d\n", (obj.*pStr).c_str(), obj.*pInt1, obj.*pInt2);

	obj.*pStr = "Hacked!";
	obj.*pInt1 = 888;
	obj.*pInt2 = 999;
	printf("After Mod  -> str_val: %s, int1: %d, int2: %d\n", (obj.*pStr).c_str(), obj.*pInt1, obj.*pInt2);

	// Calling private member functions
	auto fn_hello1_s = kd::MemberAccessor<void(TargetObject::*)(const std::string& s), 0>::ptr;
	auto fn_hello1_i = kd::MemberAccessor<void(TargetObject::*)(int), 0>::ptr;
	auto fn_hello2 = kd::MemberAccessor<void(TargetObject::*)(int), 1>::ptr;
	auto fn_vt_hello3 = kd::MemberAccessor<void(TargetObject::*)(int), 2>::ptr;

	(obj.*fn_hello1_s)("abc");
	(obj.*fn_hello1_i)(12);
	(obj.*fn_hello2)(34);
	(obj.*fn_vt_hello3)(56);
}
};
