#define SOL_CHECK_ARGUMENTS 1

#include <sol.hpp>
#include <catch.hpp>

#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <memory>

TEST_CASE("gc/destructors", "test if destructors are fired properly through gc of unbound usertypes") {
	struct test;
	static std::vector<test*> tests_destroyed;
	struct test {
		int v = 10;
		~test() {
			tests_destroyed.push_back(this);
		}
	};
	test t;
	test* pt = nullptr;
	{
		sol::state lua;

		lua["t"] = test{};
		pt = lua["t"];
	}

	REQUIRE(tests_destroyed.size() == 2);
	REQUIRE(tests_destroyed.back() == pt);

	{
		sol::state lua;

		lua["t"] = &t;
		pt = lua["t"];
	}

	REQUIRE(tests_destroyed.size() == 2);
	REQUIRE(&t == pt);

	{
		sol::state lua;

		lua["t"] = std::ref(t);
		pt = lua["t"];
	}

	REQUIRE(tests_destroyed.size() == 2);
	REQUIRE(&t == pt);

	{
		sol::state lua;

		lua["t"] = t;
		pt = lua["t"];
	}

	REQUIRE(tests_destroyed.size() == 3);
	REQUIRE(&t != pt);
	REQUIRE(nullptr != pt);
}

TEST_CASE("gc/virtual destructors", "ensure types with virtual destructions behave just fine") {
	class B;
	class A;
	static std::vector<B*> bs;
	static std::vector<A*> as;

	class A {
	public:
		virtual ~A() { as.push_back(this); std::cout << "~A" << std::endl; }
	};

	class B : public A {
	public:
		virtual ~B() { bs.push_back(this); std::cout << "~B" << std::endl; }
	};

	{
		sol::state lua;
		lua.open_libraries(sol::lib::base);

		lua.new_usertype<A>("A");
		lua.new_usertype<B>("B", sol::base_classes, sol::bases<A>());

		B b1;
		lua["b1"] = b1; // breaks here
	}

	REQUIRE(as.size() == 2);
	REQUIRE(bs.size() == 2);
}

TEST_CASE("gc/function argument storage", "ensure functions take references on their types, not ownership, when specified") {
	class gc_entity;
	static std::vector<gc_entity*> entities;

	class gc_entity {
	public:
		~gc_entity() { entities.push_back(this); }
	};
	SECTION("plain") {
		entities.clear();

		sol::state lua;
		lua.open_libraries();
		sol::function f = lua.safe_script(R"(
return function(e)
end
)");
		gc_entity* target = nullptr;
		{
			gc_entity e;
			target = &e;
			{
				f(e); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}
			{
				f(&e); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}
			{
				f(std::ref(e)); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}

		}
		REQUIRE(entities.size() == 1);
		REQUIRE(entities.back() == target);
	}
	SECTION("regular") {
		entities.clear();

		sol::state lua;
		lua.open_libraries();
		lua.new_usertype<gc_entity>("entity");
		sol::function f = lua.safe_script(R"(
return function(e)
end
)");
		gc_entity* target = nullptr;
		{
			gc_entity e;
			target = &e;
			{
				f(e); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}
			{
				f(&e); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}
			{
				f(std::ref(e)); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}

		}
		REQUIRE(entities.size() == 1);
		REQUIRE(entities.back() == target);
	}
	SECTION("simple") {
		entities.clear();

		sol::state lua;
		lua.open_libraries();
		lua.new_simple_usertype<gc_entity>("entity");
		sol::function f = lua.safe_script(R"(
return function(e)
end
)");
		gc_entity* target = nullptr;
		{
			gc_entity e;
			target = &e;
			{
				f(e); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}
			{
				f(&e); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}
			{
				f(std::ref(e)); // same with std::ref(e)!
				lua.collect_garbage(); // destroys e for some reason
			}

		}
		REQUIRE(entities.size() == 1);
		REQUIRE(entities.back() == target);
	}
}


TEST_CASE("gc/function storage", "show that proper copies / destruction happens for function storage (or not)") {
	static int created = 0;
	static int destroyed = 0;
	static void* last_call = nullptr;
	static void* static_call = reinterpret_cast<void*>(0x01);
	typedef void(*fptr)();
	struct x {
		x() { ++created; }
		x(const x&) { ++created; }
		x(x&&) { ++created; }
		x& operator=(const x&) { return *this; }
		x& operator=(x&&) { return *this; }
		void func() { last_call = static_cast<void*>(this); };
		~x() { ++destroyed; }
	};
	struct y {
		y() { ++created; }
		y(const x&) { ++created; }
		y(x&&) { ++created; }
		y& operator=(const x&) { return *this; }
		y& operator=(x&&) { return *this; }
		static void func() { last_call = static_call; };
		void operator()() { func(); }
		operator fptr () { return func; }
		~y() { ++destroyed; }
	};

	// stateful functors/member functions should always copy unless specified
	{
		created = 0;
		destroyed = 0;
		last_call = nullptr;
		{
			sol::state lua;
			x x1;
			lua.set_function("x1copy", &x::func, x1);
			lua.safe_script("x1copy()");
			REQUIRE(created == 2);
			REQUIRE(destroyed == 0);
			REQUIRE_FALSE(last_call == &x1);

			lua.set_function("x1ref", &x::func, std::ref(x1));
			lua.safe_script("x1ref()");
			REQUIRE(created == 2);
			REQUIRE(destroyed == 0);
			REQUIRE(last_call == &x1);
		}
		REQUIRE(created == 2);
		REQUIRE(destroyed == 2);
	}

	// things convertible to a static function should _never_ be forced to make copies
	// therefore, pass through untouched
	{
		created = 0;
		destroyed = 0;
		last_call = nullptr;
		{
			sol::state lua;
			y y1;
			lua.set_function("y1copy", y1);
			lua.safe_script("y1copy()");
			REQUIRE(created == 1);
			REQUIRE(destroyed == 0);
			REQUIRE(last_call == static_call);

			last_call = nullptr;
			lua.set_function("y1ref", std::ref(y1));
			lua.safe_script("y1ref()");
			REQUIRE(created == 1);
			REQUIRE(destroyed == 0);
			REQUIRE(last_call == static_call);
		}
		REQUIRE(created == 1);
		REQUIRE(destroyed == 1);
	}
}


TEST_CASE("gc/same type closures", "make sure destructions are per-object, not per-type, by destroying one type multiple times") {
	static std::set<void*> last_my_closures;
	static bool checking_closures = false;
	static bool check_failed = false;

	struct my_closure {
		int& n;

		my_closure(int& n) : n(n) {}
		~my_closure() noexcept(false) {
			if (!checking_closures)
				return;
			void* addr = static_cast<void*>(this);
			auto f = last_my_closures.find(addr);
			if (f != last_my_closures.cend()) {
				check_failed = true;
			}
			last_my_closures.insert(f, addr);
		}

		int operator() () {
			++n; return n;
		}
	};

	int n = 250;
	my_closure a(n);
	my_closure b(n);
	{
		sol::state lua;

		lua.set_function("f", a);
		lua.set_function("g", b);
		checking_closures = true;
	}
	REQUIRE_FALSE(check_failed);
	REQUIRE(last_my_closures.size() == 2);
}

TEST_CASE("gc/usertypes", "show that proper copies / destruction happens for usertypes") {
	static int created = 0;
	static int destroyed = 0;
	struct x {
		x() { ++created; }
		x(const x&) { ++created; }
		x(x&&) { ++created; }
		x& operator=(const x&) { return *this; }
		x& operator=(x&&) { return *this; }
		~x() { ++destroyed; }
	};
	SECTION("plain") {
		created = 0;
		destroyed = 0;
		{
			sol::state lua;
			x x1;
			x x2;
			lua.set("x1copy", x1, "x2copy", x2, "x1ref", std::ref(x1));
			x& x1copyref = lua["x1copy"];
			x& x2copyref = lua["x2copy"];
			x& x1ref = lua["x1ref"];
			REQUIRE(created == 4);
			REQUIRE(destroyed == 0);
			REQUIRE(std::addressof(x1) == std::addressof(x1ref));
			REQUIRE(std::addressof(x1copyref) != std::addressof(x1));
			REQUIRE(std::addressof(x2copyref) != std::addressof(x2));
		}
		REQUIRE(created == 4);
		REQUIRE(destroyed == 4);
	}
	SECTION("regular") {
		created = 0;
		destroyed = 0;
		{
			sol::state lua;
			lua.new_usertype<x>("x");
			x x1;
			x x2;
			lua.set("x1copy", x1, "x2copy", x2, "x1ref", std::ref(x1));
			x& x1copyref = lua["x1copy"];
			x& x2copyref = lua["x2copy"];
			x& x1ref = lua["x1ref"];
			REQUIRE(created == 4);
			REQUIRE(destroyed == 0);
			REQUIRE(std::addressof(x1) == std::addressof(x1ref));
			REQUIRE(std::addressof(x1copyref) != std::addressof(x1));
			REQUIRE(std::addressof(x2copyref) != std::addressof(x2));
		}
		REQUIRE(created == 4);
		REQUIRE(destroyed == 4);
	}
	SECTION("simple") {
		created = 0;
		destroyed = 0;
		{
			sol::state lua;
			lua.new_simple_usertype<x>("x");
			x x1;
			x x2;
			lua.set("x1copy", x1, "x2copy", x2, "x1ref", std::ref(x1));
			x& x1copyref = lua["x1copy"];
			x& x2copyref = lua["x2copy"];
			x& x1ref = lua["x1ref"];
			REQUIRE(created == 4);
			REQUIRE(destroyed == 0);
			REQUIRE(std::addressof(x1) == std::addressof(x1ref));
			REQUIRE(std::addressof(x1copyref) != std::addressof(x1));
			REQUIRE(std::addressof(x2copyref) != std::addressof(x2));
		}
		REQUIRE(created == 4);
		REQUIRE(destroyed == 4);
	}
}

TEST_CASE("gc/double-deletion tests", "make sure usertypes are properly destructed and don't double-delete memory or segfault") {
	class crash_class {
	public:
		crash_class() {}
		~crash_class() { a = 10; }
	private:
		int a;
	};

	sol::state lua;

	SECTION("regular") {
		lua.new_usertype<crash_class>("CrashClass",
			sol::call_constructor, sol::constructors<sol::types<>>()
			);

		lua.safe_script(R"(
		function testCrash()
			local x = CrashClass()
		end
		)");

		for (int i = 0; i < 1000; ++i) {
			lua["testCrash"]();
		}
	}
	SECTION("simple") {
		lua.new_simple_usertype<crash_class>("CrashClass",
			sol::call_constructor, sol::constructors<sol::types<>>()
			);

		lua.safe_script(R"(
		function testCrash()
			local x = CrashClass()
			end
		)");

		for (int i = 0; i < 1000; ++i) {
			lua["testCrash"]();
		}
	}
}

TEST_CASE("gc/shared_ptr regression", "metatables should not screw over unique usertype metatables") {
	static int created = 0;
	static int destroyed = 0;
	struct test {
		test() {
			++created;
		}

		~test() {
			++destroyed;
		}
	};

	SECTION("regular") {
		created = 0;
		destroyed = 0;
		{
			std::list<std::shared_ptr<test>> tests;
			sol::state lua;
			lua.open_libraries();

			lua.new_usertype<test>("test",
				"create", [&]() -> std::shared_ptr<test> {
				tests.push_back(std::make_shared<test>());
				return tests.back();
			}
			);
			REQUIRE(created == 0);
			REQUIRE(destroyed == 0);
			lua.safe_script("x = test.create()");
			REQUIRE(created == 1);
			REQUIRE(destroyed == 0);
			REQUIRE_FALSE(tests.empty());
			std::shared_ptr<test>& x = lua["x"];
			std::size_t xuse = x.use_count();
			std::size_t tuse = tests.back().use_count();
			REQUIRE(xuse == tuse);
		}
		REQUIRE(created == 1);
		REQUIRE(destroyed == 1);
	}
	SECTION("simple") {
		created = 0;
		destroyed = 0;
		{
			std::list<std::shared_ptr<test>> tests;
			sol::state lua;
			lua.open_libraries();

			lua.new_simple_usertype<test>("test",
				"create", [&]() -> std::shared_ptr<test> {
				tests.push_back(std::make_shared<test>());
				return tests.back();
			}
			);
			REQUIRE(created == 0);
			REQUIRE(destroyed == 0);
			lua.safe_script("x = test.create()");
			REQUIRE(created == 1);
			REQUIRE(destroyed == 0);
			REQUIRE_FALSE(tests.empty());
			std::shared_ptr<test>& x = lua["x"];
			std::size_t xuse = x.use_count();
			std::size_t tuse = tests.back().use_count();
			REQUIRE(xuse == tuse);
		}
		REQUIRE(created == 1);
		REQUIRE(destroyed == 1);
	}
}

TEST_CASE("gc/double deleter guards", "usertype metatables internally must not rely on C++ state") {
	SECTION("regular") {
		struct c_a { int xv; };
		struct c_b { int yv; };
		auto routine = []() {
			sol::state lua;
			lua.new_usertype<c_a>("c_a", "x", &c_a::xv);
			lua.new_usertype<c_b>("c_b", "y", &c_b::yv);
			lua = sol::state();
			lua.new_usertype<c_a>("c_a", "x", &c_a::xv);
			lua.new_usertype<c_b>("c_b", "y", &c_b::yv);
			lua = sol::state();
		};
		REQUIRE_NOTHROW(routine());
	}
	SECTION("simple") {
		struct sc_a { int xv; };
		struct sc_b { int yv; };
		auto routine = []() {
			sol::state lua;
			lua.new_simple_usertype<sc_a>("c_a", "x", &sc_a::xv);
			lua.new_simple_usertype<sc_b>("c_b", "y", &sc_b::yv);
			lua = sol::state();
			lua.new_simple_usertype<sc_a>("c_a", "x", &sc_a::xv);
			lua.new_simple_usertype<sc_b>("c_b", "y", &sc_b::yv);
			lua = sol::state();
		};
		REQUIRE_NOTHROW(routine());
	}
}
