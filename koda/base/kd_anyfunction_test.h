#pragma once
#include <stdio.h>
#include <iostream>
#include <string>
#include "kd_anyfunction.h"

namespace kd_anyfun_test {
struct ClassBase {
	virtual ~ClassBase() {};
};

struct ClassA : public ClassBase {
	int m_val;
	ClassA(int val) : m_val(val)
	{};

	void func1() {
		printf("ClassA::func1\n");
	};

	void func2(int a) {
		printf("ClassA::func2, a = %d\n", a);
	};

	void func3(int a, float b) {
		printf("ClassA::func3, a = %d, b = %f\n", a, b);
	};

	void func4(int a, const std::string& s) const {
		printf("ClassA::func4, a = %d, s = %s\n", a, s.c_str());
	};

	int func5(int a, const std::string& s) {
		printf("ClassA::func5, a = %d, s = %s\n", a, s.c_str());
		return a + 1;
	}

	virtual std::string func6(int a, const std::string& s) {
		printf("ClassA::func6, a = %d, s = %s\n", a, s.c_str());
		return "amf_" + s;
	};
};

//
void g_func1() {
	printf("g_func1\n");
};

void g_func2(int a) {
	printf("g_func2, a = %d\n", a);
};

void g_func3(int a, float b) {
	printf("g_func3, a = %d, b = %f\n", a, b);
};

void g_func4(int a, const std::string& s) {
	printf("g_func4, a = %d, s = %s\n", a, s.c_str());
};

int g_func5(int a, const std::string& s) {
	printf("g_func5, a = %d, s = %s\n", a, s.c_str());
	return a + 1;
}

std::string g_func6(int a, const std::string& s) {
	printf("g_func6, a = %d, s = %s\n", a, s.c_str());
	return "af_" + s;
};

//
static void anyfun_test() {
	printf("\n\n");

	using FT1 = decltype(&ClassA::func1);
	using FT2 = decltype(&ClassA::func2);
	using FT3 = decltype(&ClassA::func3);
	using FT4 = decltype(&ClassA::func4);
	printf("FT1 size = %d\n", sizeof(FT1));
	printf("FT2 size = %d\n", sizeof(FT2));
	printf("FT3 size = %d\n", sizeof(FT3));
	printf("FT4 size = %d\n", sizeof(FT4));

	printf("g_func4 = %p\n", g_func4);
	printf("-g_func4 = %p\n", &g_func4);
	printf("ClassA::func4 = %p\n", &ClassA::func4);

	{ //
		printf("\n\n============ 1 =========== \n");
		kd::any_function af1(g_func1);
		kd::any_function af2(g_func2);
		kd::any_function af3(g_func3);
		kd::any_function af4(&g_func4);
		kd::any_function af5(g_func5);
		kd::any_function af6(g_func6);

		std::string as = "as";
		std::string bs = "bs";
		af1.invoke_fn<void()>();
		af2.invoke_fn<void(int)>(20);
		af3.invoke_fn<void(int, float)>(30, 1.0f);
		af4.invoke_fn<void(int, const std::string&)>(40, as);
		af4.invoke_fn<void(int, const std::string&)>(41, std::string("411"));
		af4.invoke_fn<void(int, const std::string&)>(42, "421");
		af4.invoke_fn<void(int, const std::string&)>(43, as + "431");

		auto r5 = af5.invoke_fn<int(int, const std::string&)>(50, as + "_func5");
		auto ar61 = af6.invoke_fn<std::string(int, const std::string&)>(60, as + "_func66");
		auto ar62 = af6.invoke_as(g_func6, 61, as + "_func67");
	}

	{ //
		printf("\n\n============ 2 =========== \n");
		kd::any_function af1(g_func1);
		kd::any_function af2(g_func2);
		kd::any_function af3(g_func3);
		kd::any_function af4(&g_func4);
		kd::any_function af5(g_func5);
		kd::any_function af6(g_func6);

		std::string as = "as";
		std::string bs = "bs";
		af1.invoke<void()>();
		af2.invoke<void(int)>(20);
		af3.invoke<void(int, float)>(30, 1.0f);
		af4.invoke<void(int, const std::string&)>(40, as);
		af4.invoke<void(int, const std::string&)>(41, std::string("411"));
		af4.invoke<void(int, const std::string&)>(42, "421");
		af4.invoke<void(int, const std::string&)>(43, as + "431");

		auto r5 = af5.invoke<int(int, const std::string&)>(50, as + "_func5");
		auto ar61 = af6.invoke<std::string(int, const std::string&)>(60, as + "_func66");
		auto ar62 = af6.invoke_as(g_func6, 61, as + "_func67");
	}

	{ //
		printf("\n\n============ 3 =========== \n");
		ClassA a(1);
		kd::any_function amf1(&ClassA::func1);
		kd::any_function amf2(&ClassA::func2);
		kd::any_function amf3(&ClassA::func3);
		kd::any_function amf4(&ClassA::func4);
		kd::any_function amf5(&ClassA::func5);
		kd::any_function amf6(&ClassA::func6);

		std::string as = "as";
		std::string bs = "bs";
		amf1.invoke_mfn<void()>(&a);
		amf2.invoke_mfn<void(int)>(&a, 20);
		amf3.invoke_mfn<void(int, float)>(&a, 30, 1.0f);
		amf4.invoke_mfn<void(int, const std::string&)>(&a, 40, as);
		amf4.invoke_mfn<void(int, const std::string&)>(&a, 41, std::string("_411"));
		amf4.invoke_mfn<void(int, const std::string&)>(&a, 42, "_421");
		amf4.invoke_mfn<void(int, const std::string&)>(&a, 43, as + "_431");
		amf4.invoke_as(&ClassA::func4, &a, 44, as + "_441");

		auto r5 = amf5.invoke_mfn<int(int, const std::string&)>(&a, 50, as + "_func5");
		auto r61 = amf6.invoke_mfn<std::string(int, const std::string&)>(&a, 60, as + "_func66");
		auto r62 = amf6.invoke_as(&ClassA::func6, &a, 61, as + "_func67");
	}

	{ //
		printf("\n\n============ 4 =========== \n");
		ClassA a(1);
		kd::any_function amf1(&ClassA::func1);
		kd::any_function amf2(&ClassA::func2);
		kd::any_function amf3(&ClassA::func3);
		kd::any_function amf4(&ClassA::func4);
		kd::any_function amf5(&ClassA::func5);
		kd::any_function amf6(&ClassA::func6);

		std::string as = "as";
		std::string bs = "bs";
		amf1.invoke<void()>(&a);
		amf2.invoke<void(int)>(&a, 20);
		amf3.invoke<void(int, float)>(&a, 30, 1.0f);
		amf4.invoke<void(int, const std::string&)>(&a, 40, as);
		amf4.invoke<void(int, const std::string&)>(&a, 41, std::string("_411"));
		amf4.invoke<void(int, const std::string&)>(&a, 42, "_421");
		amf4.invoke<void(int, const std::string&)>(&a, 43, as + "_431");
		amf4.invoke_as(&ClassA::func4, &a, 44, as + "_441");

		auto r5 = amf5.invoke<int(int, const std::string&)>(&a, 50, as + "_func5");
		auto r61 = amf6.invoke<std::string(int, const std::string&)>(&a, 60, as + "_func66");
		auto r62 = amf6.invoke_as(&ClassA::func6, &a, 61, as + "_func67");
	}
}
}
