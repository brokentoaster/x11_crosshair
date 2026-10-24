// test_logic.cpp
// Self-contained tests for the X-free logic: option parsing and the
// idle/fade state machine.
#include "../options.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL " << __LINE__ << ": " #cond << "\n";                  \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static int parse(std::vector<const char *> args, Options &opts) {
  args.insert(args.begin(), "x11_crosshair");
  std::vector<char *> argv;
  for (auto *a : args)
    argv.push_back(const_cast<char *>(a));
  return parseArgs(static_cast<int>(argv.size()), argv.data(), opts);
}

static void testDefaults() {
  Options o;
  CHECK(parse({}, o) == 0);
  CHECK(o.pollIntervalMs == 0);
  CHECK(o.showSelection == true);
  CHECK(o.thickness == 3);
  CHECK(o.fadeMs == 0);
  CHECK(o.fadeDurationMs == 0);
}

static void testValidValues() {
  Options o;
  CHECK(parse({"--poll", "50"}, o) == 0);
  CHECK(o.pollIntervalMs == 50);
  CHECK(parse({"--no-selection"}, o) == 0);
  CHECK(o.showSelection == false);
  CHECK(parse({"--thickness", "7"}, o) == 0);
  CHECK(o.thickness == 7);
  CHECK(parse({"--fade-ms", "1000", "--fade-duration", "400"}, o) == 0);
  CHECK(o.fadeMs == 1000);
  CHECK(o.fadeDurationMs == 400);
  CHECK(parse({"--poll"}, o) == 0);
  CHECK(o.pollIntervalMs == 10);
}

static void testInvalidValues() {
  Options o;
  CHECK(parse({"--fade-ms", "12abc"}, o) == 1);
  CHECK(parse({"--thickness", ""}, o) == 1);
  CHECK(parse({"--thickness"}, o) == 1);
  CHECK(parse({"--thickness", "0"}, o) == 1);
  CHECK(parse({"--fade-duration", "-5"}, o) == 1);
  CHECK(parse({"--poll", "0"}, o) == 1);
  CHECK(parse({"--bogus"}, o) == 1);
}

static void testHelp() {
  Options o;
  CHECK(parse({"--help"}, o) == 2);
}

static void testEvaluateIdleDisabled() {
  CHECK(evaluateIdle(0, 0, 100000).hideNow == false);
  CHECK(evaluateIdle(0, 0, 100000).fading == false);
}

static void testEvaluateIdleTimeout() {
  IdleState s = evaluateIdle(1000, 0, 999);
  CHECK(!s.hideNow && !s.fading);
  s = evaluateIdle(1000, 0, 1000);
  CHECK(s.hideNow && !s.fading);
  s = evaluateIdle(1000, 0, 5000);
  CHECK(s.hideNow);
}

static void testEvaluateIdleFade() {
  IdleState s = evaluateIdle(1000, 400, 999);
  CHECK(!s.hideNow && !s.fading);
  s = evaluateIdle(1000, 400, 1000);
  CHECK(!s.hideNow && s.fading && s.alpha == 255);
  s = evaluateIdle(1000, 400, 1200);
  CHECK(!s.hideNow && s.fading && s.alpha == 128);
  s = evaluateIdle(1000, 400, 1399);
  CHECK(!s.hideNow && s.fading && s.alpha > 0);
  s = evaluateIdle(1000, 400, 1400);
  CHECK(s.hideNow && !s.fading);
}

int main() {
  testDefaults();
  testValidValues();
  testInvalidValues();
  testHelp();
  testEvaluateIdleDisabled();
  testEvaluateIdleTimeout();
  testEvaluateIdleFade();
  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}