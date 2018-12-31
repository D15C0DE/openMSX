#include "catch.hpp"
#include "Interpreter.hh"
#include "TclObject.hh"
#include "view.hh"
#include <cstdint>
#include <cstring>
#include <iterator>

using namespace openmsx;

TEST_CASE("TclObject, constructors")
{
	Interpreter interp;
	SECTION("default") {
		TclObject t;
		CHECK(t.getString() == "");
	}
	SECTION("string_view") {
		TclObject t("foo");
		CHECK(t.getString() == "foo");
	}
	SECTION("int") {
		TclObject t(42);
		CHECK(t.getString() == "42");
	}
	SECTION("double") {
		TclObject t(6.28);
		CHECK(t.getString() == "6.28");
	}
	SECTION("copy") {
		TclObject t1("bar");
		TclObject t2 = t1;
		CHECK(t1.getString() == "bar");
		CHECK(t2.getString() == "bar");
		t1.setInt(123);
		CHECK(t1.getString() == "123");
		CHECK(t2.getString() == "bar");
	}
	SECTION("move") {
		TclObject t1("bar");
		TclObject t2 = std::move(t1);
		CHECK(t2.getString() == "bar");
	}
}

TEST_CASE("TclObject, assignment")
{
	Interpreter interp;
	SECTION("copy") {
		TclObject t1(123);
		TclObject t2(987);
		REQUIRE(t1 != t2);
		t2 = t1;
		CHECK(t1 == t2);
		CHECK(t1.getString() == "123");
		CHECK(t2.getString() == "123");
		t1.setInt(456);
		CHECK(t1 != t2);
		CHECK(t1.getString() == "456");
		CHECK(t2.getString() == "123");
	}
	SECTION("move") {
		TclObject t1(123);
		TclObject t2(987);
		REQUIRE(t1 != t2);
		t2 = std::move(t1);
		CHECK(t2.getString() == "123");
		t1.setInt(456);
		CHECK(t1 != t2);
		CHECK(t1.getString() == "456");
		CHECK(t2.getString() == "123");
	}
}

// skipped getTclObject() / getTclObjectNonConst()

TEST_CASE("TclObject, setXXX")
{
	Interpreter interp;
	TclObject t(123);
	REQUIRE(t.getString() == "123");

	SECTION("string_view") {
		t.setString("foo");
		CHECK(t.getString() == "foo");
	}
	SECTION("int") {
		t.setInt(42);
		CHECK(t.getString() == "42");
	}
	SECTION("bool") {
		t.setBoolean(true);
		CHECK(t.getString() == "1");
		t.setBoolean(false);
		CHECK(t.getString() == "0");
	}
	SECTION("double") {
		t.setDouble(-3.14);
		CHECK(t.getString() == "-3.14");
	}
	SECTION("binary") {
		uint8_t buf[] = {1, 2, 3};
		t.setBinary({buf, sizeof(buf)});
		auto result = t.getBinary();
		CHECK(result.size() == sizeof(buf));
		CHECK(memcmp(buf, result.data(), result.size()) == 0);
		// 'buf' was copied into 't'
		CHECK(result.data() != &buf[0]);
		CHECK(result[0] == 1);
		buf[0] = 99;
		CHECK(result[0] == 1);
	}
}

TEST_CASE("TclObject, addListElement")
{
	Interpreter interp;
	
	SECTION("no error") {
		TclObject t;
		CHECK(t.getListLength(interp) == 0);
		t.addListElement("foo bar");
		CHECK(t.getListLength(interp) == 1);
		t.addListElement(33);
		CHECK(t.getListLength(interp) == 2);
		t.addListElement(9.23);
		CHECK(t.getListLength(interp) == 3);
		TclObject t2("bla");
		t.addListElement(t2);
		CHECK(t.getListLength(interp) == 4);

		TclObject l0 = t.getListIndex(interp, 0);
		TclObject l1 = t.getListIndex(interp, 1);
		TclObject l2 = t.getListIndex(interp, 2);
		TclObject l3 = t.getListIndex(interp, 3);
		CHECK(l0.getString() == "foo bar");
		CHECK(l1.getString() == "33");
		CHECK(l2.getString() == "9.23");
		CHECK(l3.getString() == "bla");

		CHECK(t.getString() == "{foo bar} 33 9.23 bla");
	}
	SECTION("error") {
		TclObject t("{foo"); // invalid list representation
		CHECK_THROWS(t.getListLength(interp));
		CHECK_THROWS(t.addListElement(123));
	}
}

TEST_CASE("TclObject, addListElements")
{
	Interpreter interp;
	int ints[] = {7, 6, 5};
	double doubles[] = {1.2, 5.6};

	SECTION("no error") {
		TclObject t;
		CHECK(t.getListLength(interp) == 0);
		// iterator-pair
		t.addListElements(std::begin(ints), std::end(ints));
		CHECK(t.getListLength(interp) == 3);
		CHECK(t.getListIndex(interp, 1).getString() == "6");
		// range (array)
		t.addListElements(doubles);
		CHECK(t.getListLength(interp) == 5);
		CHECK(t.getListIndex(interp, 3).getString() == "1.2");
		// view::transform
		t.addListElements(view::transform(ints, [](int i) { return 2 * i; }));
		CHECK(t.getListLength(interp) == 8);
		CHECK(t.getListIndex(interp, 7).getString() == "10");
	}
	SECTION("error") {
		TclObject t("{foo"); // invalid list representation
		CHECK_THROWS(t.addListElements(std::begin(doubles), std::end(doubles)));
		CHECK_THROWS(t.addListElements(ints));
	}
}

// there are no setting functions (yet?) for dicts

TEST_CASE("TclObject, getXXX")
{
	Interpreter interp;
	TclObject t0;
	TclObject t1("Off");
	TclObject t2(1);
	TclObject t3(2.71828);

	SECTION("getString") { // never fails
		CHECK(t0.getString() == "");
		CHECK(t1.getString() == "Off");
		CHECK(t2.getString() == "1");
		CHECK(t3.getString() == "2.71828");
	}
	SECTION("getInt") {
		CHECK_THROWS(t0.getInt(interp));
		CHECK_THROWS(t1.getInt(interp));
		CHECK       (t2.getInt(interp) == 1);
		CHECK_THROWS(t3.getInt(interp));
	}
	SECTION("getBoolean") {
		CHECK_THROWS(t0.getBoolean(interp));
		CHECK       (t1.getBoolean(interp) == false);
		CHECK       (t2.getBoolean(interp) == true);
		CHECK       (t3.getBoolean(interp) == true);
	}
	SECTION("getDouble") {
		CHECK_THROWS(t0.getDouble(interp));
		CHECK_THROWS(t1.getDouble(interp));
		CHECK       (t2.getDouble(interp) == 1.0);
		CHECK       (t3.getDouble(interp) == 2.71828);
	}
}

// getBinary() already tested above
// getListLength and getListIndex() already tested above

TEST_CASE("TclObject, getDictValue")
{
	Interpreter interp;

	SECTION("no error") {
		TclObject t("one 1 two 2.0 three drie");
		CHECK(t.getDictValue(interp, TclObject("two"  )).getString() == "2.0");
		CHECK(t.getDictValue(interp, TclObject("one"  )).getString() == "1");
		CHECK(t.getDictValue(interp, TclObject("three")).getString() == "drie");
		// missing key -> empty string .. can be improved when needed
		CHECK(t.getDictValue(interp, TclObject("four" )).getString() == "");
	}
	SECTION("invalid dict") {
		TclObject t("{foo");
		CHECK_THROWS(t.getDictValue(interp, TclObject("foo")));
	}
}

TEST_CASE("TclObject, STL interface on Tcl list")
{
	Interpreter interp;

	SECTION("empty") {
		TclObject t;
		CHECK(t.size() == 0);
		CHECK(t.empty() == true);
		CHECK(t.begin() == t.end());
	}
	SECTION("not empty") {
		TclObject t("1 1 2 3 5 8 13 21 34 55");
		CHECK(t.size() == 10);
		CHECK(t.empty() == false);
		auto b = t.begin();
		auto e = t.end();
		CHECK(std::distance(b, e) == 10);
		CHECK(*b == "1");
		std::advance(b, 5);
		CHECK(*b == "8");
		++b;
		CHECK(*b == "13");
		std::advance(b, 4);
		CHECK(b == e);
	}
	SECTION("invalid list") {
		// acts as if the list is empty .. can be improved when needed
		TclObject t("{foo bar qux");
		CHECK(t.size() == 0);
		CHECK(t.empty() == true);
		CHECK(t.begin() == t.end());
	}
}

TEST_CASE("TclObject, evalBool")
{
	Interpreter interp;
	CHECK(TclObject("23 == (20 + 3)").evalBool(interp) == true);
	CHECK(TclObject("1 >= (6-2)"    ).evalBool(interp) == false);
	CHECK_THROWS(TclObject("bla").evalBool(interp));
}

TEST_CASE("TclObject, executeCommand")
{
	Interpreter interp;
	CHECK(TclObject("return foobar").executeCommand(interp).getString() == "foobar");
	CHECK(TclObject("set n 2").executeCommand(interp).getString() == "2");
	TclObject cmd("string repeat bla $n");
	CHECK(cmd.executeCommand(interp, true).getString() == "blabla");
	CHECK(TclObject("incr n").executeCommand(interp).getString() == "3");
	CHECK(cmd.executeCommand(interp, true).getString() == "blablabla");

	CHECK_THROWS(TclObject("qux").executeCommand(interp));
}

TEST_CASE("TclObject, operator==, operator!=")
{
	Interpreter interp;
	TclObject t0;
	TclObject t1("foo");
	TclObject t2("bar qux");
	TclObject t3("foo");

	CHECK(  t0 == t0 ); CHECK(!(t0 != t0));
	CHECK(!(t0 == t1)); CHECK(  t0 != t1 );
	CHECK(!(t0 == t2)); CHECK(  t0 != t2 );
	CHECK(!(t0 == t3)); CHECK(  t0 != t3 );
	CHECK(  t1 == t1 ); CHECK(!(t1 != t1));
	CHECK(!(t1 == t2)); CHECK(  t1 != t2 );
	CHECK(  t1 == t3 ); CHECK(!(t1 != t3));
	CHECK(  t2 == t2 ); CHECK(!(t2 != t2));
	CHECK(!(t2 == t3)); CHECK(  t2 != t3 );
	CHECK(  t3 == t3 ); CHECK(!(t3 != t3));

	CHECK(t0 == ""   ); CHECK(!(t0 != ""   )); CHECK(""    == t0); CHECK(!(""    != t0));
	CHECK(t0 != "foo"); CHECK(!(t0 == "foo")); CHECK("foo" != t0); CHECK(!("foo" == t0));
	CHECK(t1 != ""   ); CHECK(!(t1 == ""   )); CHECK(""    != t1); CHECK(!(""    == t1));
	CHECK(t1 == "foo"); CHECK(!(t1 != "foo")); CHECK("foo" == t1); CHECK(!("foo" != t1));
	CHECK(t2 != ""   ); CHECK(!(t2 == ""   )); CHECK(""    != t2); CHECK(!(""    == t2));
	CHECK(t2 != "foo"); CHECK(!(t2 == "foo")); CHECK("foo" != t2); CHECK(!("foo" == t2));
}

// skipped XXTclHasher