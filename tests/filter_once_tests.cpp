#include "../src/writer/physical_write_filter_once.h"
#include <cstdlib>
#include <iostream>
using namespace djmeta_foobar;
static void check(bool okay) { if(!okay)std::exit(1); }
int main() {
 FilterOnce written;
 check(written.result()==FilterResult::Pending);
 check(written.enter());
 written.wrote();
 check(written.result()==FilterResult::Wrote);
 check(!written.enter());
 check(written.result()==FilterResult::Duplicate);
 FilterOnce noop;
 check(noop.enter());
 noop.noop();
 check(noop.result()==FilterResult::Noop);
 check(!noop.enter());
 check(noop.result()==FilterResult::Duplicate);
 FilterOnce incomplete;
 check(incomplete.enter());
 check(incomplete.result()==FilterResult::Pending);
 check(!incomplete.enter());
 incomplete.wrote();
 check(incomplete.result()==FilterResult::Duplicate);
 std::cout<<"PASS: native physical SDK filter callback single-invocation monitor\n";
}
