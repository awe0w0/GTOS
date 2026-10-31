# Unit test for libxml_profile.cmake: the shipped parser and the shipped contract resolution, run on synthetic BUILD.gn files. No
# compiler is involved and nothing here needs the target toolchain; the generator run measures the compiler side.
#
#   cmake -P test_profile.cmake -DSCRATCH=<fresh directory> [-DBUILD_GN=<real BUILD.gn>] [-DCALIBRATE=wrong]
#
# The scratch directory must not exist: this test creates a new tree, refuses to reuse one, and never deletes anything it did not
# create in this run. (The r20 version started with an unconditional REMOVE_RECURSE of whatever path it was handed.)
#
# What it covers, and why in this shape:
#   * the parser: a target's `sources` list is what compiles, not a name that appears somewhere in the file. Cases: comments, an
#     `inputs` list, a sibling target, a false conditional branch, a true one, the one-line list form, the missing-target refusal and
#     the unknown-conditional refusal.
#   * the contract: upstream/Chromium declare the feature set, the source list can make a feature impossible, and a missing SDK
#     capability has to be downgraded by name instead of quietly becoming 0. Cases: SAX1 on from SAX2.c, pattern/valid off while
#     their files are still compiled, ICU downgraded explicitly, and three refusals that must not pass silently.
#   * the generated-header check: a valid file passes; a template variable, an empty #if and a truncated file are refused.
#   * the calibration: with -DCALIBRATE=wrong one deliberately wrong expectation is compared, and the run must fail. Without this,
#     a comparison bug like r20's `foreach(pair IN LISTS expected)` on a macro argument would keep every case green.

cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake")

if(NOT DEFINED SCRATCH)
  if(DEFINED ENV{TMPDIR})
    set(SCRATCH "$ENV{TMPDIR}/libxml-profile-test-r21")
  elseif(DEFINED ENV{TEMP})
    set(SCRATCH "$ENV{TEMP}/libxml-profile-test-r21")
  else()
    set(SCRATCH "/tmp/libxml-profile-test-r21")
  endif()
endif()
if(EXISTS "${SCRATCH}")
  message(FATAL_ERROR "refusing to reuse ${SCRATCH}: pass a directory that does not exist yet, this test does not delete old trees")
endif()
file(MAKE_DIRECTORY "${SCRATCH}")

set(failures 0)
set(cases 0)

# A macro, not a function: the counters and the values under test live at the top level, and macros do not open a scope.
macro(fail_case name detail)
  math(EXPR failures "${failures} + 1")
  message(STATUS "FAIL ${name}: ${detail}")
endmacro()

# compare <name> <variable> <expected>
macro(compare name variable want)
  if(NOT "${${variable}}" STREQUAL "${want}")
    fail_case("${name}" "${variable} is '${${variable}}', expected '${want}'")
  endif()
endmacro()

# check_expectations <name> <pair>...   -- each pair is "VARIABLE=value"
# The arguments arrive as one semicolon-separated string per call site, which is exactly the macro caveat CMake documents: a macro
# parameter is a string replacement, so `foreach(IN LISTS expected)` looked up a variable that never existed and the first five cases
# compared nothing. The list is materialised here on purpose.
macro(check_expectations name)
  math(EXPR cases "${cases} + 1")
  set(expected_list "${ARGN}")
  # No `return()` here. In a macro it returns from the enclosing *file*, so the empty case silently ended the whole run: one failure printed,
  # nothing else compared, the final failure check never reached, exit status 0. The body below is in an `else()` so the macro behaves like
  # any other and the run continues to the next case.
  if(expected_list STREQUAL "")
    fail_case("${name}" "the case passed no expectations at all, so it could only ever pass vacuously")
  else()
    set(case_failed 0)
    foreach(pair IN LISTS expected_list)
      string(FIND "${pair}" "=" separator)
      if(separator LESS 0)
        message(STATUS "  ${name}: '${pair}' is not VARIABLE=value")
        set(case_failed 1)
        continue()
      endif()
      string(REPLACE "=" ";" kv "${pair}")
      list(LENGTH kv pair_length)
      if(NOT pair_length EQUAL 2)
        # `VARIABLE=a=b` would otherwise compare a and a, quietly dropping the rest; `=value` would index past the end.
        message(STATUS "  ${name}: '${pair}' splits into ${pair_length} fields, expected exactly VARIABLE=value")
        set(case_failed 1)
        continue()
      endif()
      list(GET kv 0 var)
      list(GET kv 1 want)
      if(var STREQUAL "")
        message(STATUS "  ${name}: '${pair}' has an empty variable name")
        set(case_failed 1)
        continue()
      endif()
      if(NOT "${${var}}" STREQUAL "${want}")
        message(STATUS "  ${name}: ${var} is '${${var}}', expected '${want}'")
        set(case_failed 1)
      endif()
    endforeach()
    if(case_failed)
      math(EXPR failures "${failures} + 1")
      message(STATUS "FAIL ${name}")
    else()
      message(STATUS "PASS ${name}")
    endif()
  endif()
endmacro()

# expect_parse <name> <gn_text> <sources, '+'-separated so a CMake list separator cannot split the expectation>
macro(expect_parse name gn_text want_sources)
  math(EXPR cases "${cases} + 1")
  set(gn_path "${SCRATCH}/${name}.gn")
  file(WRITE "${gn_path}" "${gn_text}")
  libxml_parse_target_sources("${gn_path}" "libxml" parsed_sources parsed_other parsed_skipped)
  set(expected_sources "${want_sources}")
  string(REPLACE "+" ";" expected_sources "${expected_sources}")
  if("${parsed_sources}" STREQUAL "${expected_sources}")
    message(STATUS "PASS ${name}")
  else()
    math(EXPR failures "${failures} + 1")
    message(STATUS "FAIL ${name}: sources are '${parsed_sources}', expected '${expected_sources}'")
  endif()
endmacro()

# expect_fatal <name> <body> <text the failure must name>
# A refusal is only a pass if the subprocess fails *and* says why; the driver is a separate cmake -P run because FATAL_ERROR ends the
# process it happens in.
macro(expect_fatal name body want_text)
  math(EXPR cases "${cases} + 1")
  set(driver "${SCRATCH}/refuse-${name}.cmake")
  file(WRITE "${driver}" "include(\"${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake\")\n${body}\nmessage(STATUS \"NO-REFUSAL\")\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -P "${driver}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  # CMake wraps long messages at the terminal width, so a refusal message can carry a newline in the middle of the phrase being looked for
  # ("declares no\ntarget"). The comparison is on the words, not on where CMake decided to break the line: this collapse made a passing
  # refusal read as a failure until the rerun on 2026-10-31 exposed it.
  string(REGEX REPLACE "[ \t\r\n]+" " " err "${err}")
  set(case_failed 0)
  if(rc EQUAL 0)
    message(STATUS "  ${name}: the call returned success, it had to refuse")
    set(case_failed 1)
  elseif(NOT err MATCHES "${want_text}")
    message(STATUS "  ${name}: refused with '${err}' which does not name '${want_text}'")
    set(case_failed 1)
  endif()
  if(case_failed)
    math(EXPR failures "${failures} + 1")
    message(STATUS "FAIL ${name}")
  else()
    message(STATUS "PASS ${name} (refused: ${want_text})")
  endif()
endmacro()

# ---------------------------------------------------------------- parser cases

set(typical "static_library(\"libxml\") {
  sources = [
    \"src/HTMLparser.c\",
    \"src/HTMLtree.c\",
    \"src/SAX2.c\",
    # \"src/pattern.c\",
    #\"src/c14n.c\",
  ]
}
")
expect_parse(typical "${typical}" "HTMLparser.c+HTMLtree.c+SAX2.c")

# `inputs` is not `sources`: r20's whole-file regex counted this and turned WITH_PATTERN on.
set(with_inputs "static_library(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
  ]
  inputs = [
    \"src/pattern.c\",
    \"src/valid.c\",
  ]
}
")
expect_parse(with_inputs "${with_inputs}" "xmlIO.c")

# A sibling target compiles its own sources; they are not libxml's, even though the file mentions them first.
set(sibling "source_set(\"xml_reader\") {
  sources = [
    \"src/pattern.c\",
  ]
}

static_library(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
  ]
}
")
expect_parse(sibling "${sibling}" "xmlIO.c")

# A per-OS branch is not compiled for GTOS: counted as skipped, never as a source.
set(conditional_false "static_library(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
  ]
  if (is_win) {
    sources += [
      \"src/win32_io.c\",
    ]
  }
}
")
expect_parse(conditional_false "${conditional_false}" "xmlIO.c")

# ... and a branch that is true for GTOS is compiled.
set(conditional_true "static_library(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
  ]
  if (is_gtos) {
    sources += [
      \"src/gtos_io.c\",
    ]
  }
}
")
expect_parse(conditional_true "${conditional_true}" "gtos_io.c+xmlIO.c")

# The one-line list form, and a source that is a file but not a libxml .c.
set(inline_list "static_library(\"libxml\") {
  sources = [ \"src/xmlIO.c\" ]
  sources += [ \"chromium/disabled_libxml_features.cc\" ]
}
")
expect_parse(inline_list "${inline_list}" "xmlIO.c")

# `configs`/`deps` are not source lists either.
set(other_lists "static_library(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
  ]
  configs += [
    \"//build/config/compiler:no_chromium_code\",
  ]
  deps = [
    \"//third_party/zlib\",
  ]
}
")
expect_parse(other_lists "${other_lists}" "xmlIO.c")

# A conditional this configuration cannot decide must refuse rather than guess.
set(unknown_conditional "static_library(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
  ]
  if (use_system_libxml) {
    sources += [
      \"src/extra.c\",
    ]
  }
}
")
set(unknown_path "${SCRATCH}/unknown_conditional.gn")
file(WRITE "${unknown_path}" "${unknown_conditional}")
expect_fatal(unknown_conditional
  "libxml_parse_target_sources(\"${unknown_path}\" \"libxml\" s o k)"
  "use_system_libxml")

# A file without the target must refuse, and name the target it wanted.
set(no_target "source_set(\"libxml_utils\") {
  sources = [
    \"src/xmlIO.c\",
  ]
}
")
set(no_target_path "${SCRATCH}/no_target.gn")
file(WRITE "${no_target_path}" "${no_target}")
expect_fatal(no_target
  "libxml_parse_target_sources(\"${no_target_path}\" \"libxml\" s o k)"
  "declares no target")

# ---------------------------------------------------------------- conditional cases

# libxml_condition_state is three-valued and the parse is explicit: `||` clauses, `&&` terms, leading `!`. These are the exact shapes the
# pinned BUILD.gn uses, including the two the old substring test got wrong.
macro(expect_condition name text want)
  math(EXPR cases "${cases} + 1")
  libxml_condition_state("${text}" state)
  if("${state}" STREQUAL "${want}")
    message(STATUS "PASS ${name} (${text} -> ${state})")
  else()
    fail_case("${name}" "${text} is ${state}, expected ${want}")
  endif()
endmacro()

expect_condition(cond_is_gtos "is_gtos" true)
expect_condition(cond_not_win "!is_win" true)
expect_condition(cond_gtos_or_win "is_gtos || is_win" true)
expect_condition(cond_win_and_gtos "is_win && is_gtos" false)
expect_condition(cond_linux_or_mac "is_linux || is_mac" false)
expect_condition(cond_not_gtos "!is_gtos" false)
expect_condition(cond_double_negation "!!is_win" false)
expect_condition(cond_unknown_atom "is_mystery_platform" unknown)
expect_condition(cond_unknown_in_or "is_gtos || is_mystery_platform" true)
expect_condition(cond_unknown_and "is_win && is_mystery_platform" false)

# ---------------------------------------------------------------- contract cases

# The sources the pinned BUILD.gn really compiles, trimmed to the ones the contract names.
set(real_sources "HTMLparser.c;HTMLtree.c;SAX2.c;pattern.c;threads.c;valid.c;xmlIO.c;xmlreader.c;xmlwriter.c;xpath.c")
set(no_capability "icu=0;threads=0;iconv=0;zlib=0;lzma=0;modules=0")

libxml_resolve_contract("${real_sources}" "${no_capability}" "WITH_ICU;WITH_THREADS")
check_expectations(contract_on
  "WITH_HTML=1;WITH_OUTPUT=1;WITH_PUSH=1;WITH_READER=1;WITH_WRITER=1;WITH_XPATH=1;WITH_SAX1=1"
  "WITH_PATTERN=0;WITH_VALID=0;WITH_C14N=0;WITH_CATALOG=0;WITH_DEBUG=0;WITH_REGEXPS=0;WITH_RELAXNG=0;WITH_SCHEMAS=0"
  "WITH_SCHEMATRON=0;WITH_XINCLUDE=0;WITH_XPTR=0;WITH_ICONV=0;WITH_ZLIB=0;WITH_LZMA=0;WITH_MODULES=0;WITH_ISO8859X=0"
  "WITH_HTTP=0;WITH_WINPATH=0;WITH_ICU=0;WITH_THREADS=0")

# The two "compiled but empty" cases have to say so: that is the r20 mistake this contract exists to prevent.
string(FIND "${WITH_PATTERN_REASON}" "compiled but empty" pattern_reason_at)
if(pattern_reason_at LESS 0)
  fail_case(contract_reason "WITH_PATTERN_REASON does not record that pattern.c compiles to an empty object: '${WITH_PATTERN_REASON}'")
else()
  math(EXPR cases "${cases} + 1")
  message(STATUS "PASS contract_reason (pattern.c and valid.c are recorded as compiled-but-empty)")
endif()

# A capability that is missing without a named downgrade is a refusal, not a silent 0.
expect_fatal(contract_needs_capability
  "libxml_resolve_contract(\"${real_sources}\" \"${no_capability}\" \"\")\nmessage(STATUS \"\")"
  "needs the 'icu'")

# A feature whose implementing source is not compiled cannot be honoured either.
expect_fatal(contract_needs_source
  "libxml_resolve_contract(\"SAX2.c;xmlIO.c;xmlreader.c;xmlwriter.c;xpath.c\" \"${no_capability}\" \"WITH_ICU;WITH_THREADS\")"
  "does not compile src/")

# The same reduction with the two origins: the caller's request and this configuration's own default must not read the same, because the
# r21 configuration used to describe its default ICU/threads reduction as a caller request.
libxml_resolve_contract("${real_sources}" "${no_capability}" "WITH_ICU;WITH_THREADS" "caller")
string(FIND "${WITH_ICU_REASON}" "requested explicitly by the caller" caller_reason_at)
if(caller_reason_at LESS 0 OR NOT "${WITH_ICU_DOWNGRADE_ORIGIN}" STREQUAL "caller")
  fail_case(contract_caller_origin "the caller-named reduction does not say the caller asked: '${WITH_ICU_REASON}' origin='${WITH_ICU_DOWNGRADE_ORIGIN}'")
else()
  math(EXPR cases "${cases} + 1")
  message(STATUS "PASS contract_caller_origin (a caller-named reduction says the caller asked for it)")
endif()

libxml_resolve_contract("${real_sources}" "${no_capability}" "WITH_ICU;WITH_THREADS" "default")
string(FIND "${WITH_ICU_REASON}" "turns the feature off by default" default_reason_at)
string(FIND "${WITH_ICU_REASON}" "requested explicitly" default_caller_words_at)
if(default_reason_at LESS 0 OR NOT default_caller_words_at LESS 0 OR NOT "${WITH_ICU_DOWNGRADE_ORIGIN}" STREQUAL "default")
  fail_case(contract_default_origin
    "the configuration's own reduction is not described as its default, or still claims a caller request: '${WITH_ICU_REASON}' origin='${WITH_ICU_DOWNGRADE_ORIGIN}'")
else()
  math(EXPR cases "${cases} + 1")
  message(STATUS "PASS contract_default_origin (the configuration's own reduction says so and does not claim a caller request)")
endif()

expect_fatal(contract_bad_origin
  "libxml_resolve_contract(\"${real_sources}\" \"${no_capability}\" \"WITH_ICU;WITH_THREADS\" \"because\")"
  "downgrade origin must be")

# A downgrade for a feature that does not exist is a typo, and typos must not look like policy.
expect_fatal(contract_unknown_downgrade
  "libxml_resolve_contract(\"${real_sources}\" \"${no_capability}\" \"WITH_ICU;WITH_THREADS;WITH_NONSENSE\")"
  "not a feature in the")

# ---------------------------------------------------------------- generated-header cases

set(good_header "${SCRATCH}/good.xmlversion.h")
file(WRITE "${good_header}" "#ifndef LIBXML_XMLVERSION_H\n#define LIBXML_XMLVERSION_H\n#define LIBXML_VERSION 21600\n#if 0\n#define LIBXML_HTTP_ENABLED\n#endif\n#define LIBXML_OUTPUT_ENABLED\n")
foreach(filler RANGE 1 12)
  file(APPEND "${good_header}" "/* padding so the size check has something to measure */\n")
endforeach()
file(APPEND "${good_header}" "#endif\n")

macro(expect_generated_ok name)
  math(EXPR cases "${cases} + 1")
  execute_process(COMMAND "${CMAKE_COMMAND}" -P "${SCRATCH}/check-generated.cmake" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 0)
    message(STATUS "PASS ${name}")
  else()
    fail_case("${name}" "the check refused a good header: ${err}")
  endif()
endmacro()

file(WRITE "${SCRATCH}/check-generated.cmake"
  "include(\"${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake\")\nlibxml_check_generated(\"${good_header}\")\n")
expect_generated_ok(generated_ok)

macro(expect_generated_bad name body want_text)
  math(EXPR cases "${cases} + 1")
  set(bad_path "${SCRATCH}/${name}.h")
  file(WRITE "${bad_path}" "${body}")
  set(driver "${SCRATCH}/generated-${name}.cmake")
  file(WRITE "${driver}" "include(\"${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake\")\nlibxml_check_generated(\"${bad_path}\")\nmessage(STATUS \"NO-REFUSAL\")\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -P "${driver}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  # Fold the terminal wrap before matching, for the same reason as expect_fatal: "is only" arrives as "is\nonly" once CMake has wrapped it.
  string(REGEX REPLACE "[ \t\r\n]+" " " err "${err}")
  if(rc EQUAL 0)
    fail_case("${name}" "an invalid header passed")
  elseif(NOT err MATCHES "${want_text}")
    fail_case("${name}" "refused with '${err}' which does not name '${want_text}'")
  else()
    message(STATUS "PASS ${name} (refused: ${want_text})")
  endif()
endmacro()

set(filler_text "")
foreach(filler RANGE 1 12)
  string(APPEND filler_text "/* padding so the size check has something to measure */\n")
endforeach()

expect_generated_bad(generated_leftover "@LEFT@\n${filler_text}" "contains @LEFT@")
expect_generated_bad(generated_empty_if "#if\n#define LIBXML_X@\n${filler_text}" "empty #if|still contains")
expect_generated_bad(generated_tiny "#if 0\n" "is only")

# ---------------------------------------------------------------- the real file, when the caller names it

if(DEFINED BUILD_GN)
  math(EXPR cases "${cases} + 1")
  libxml_parse_target_sources("${BUILD_GN}" "libxml" real_parsed real_other real_skipped)
  list(LENGTH real_parsed real_count)
  if(NOT real_count EQUAL 26)
    fail_case(build_gn_real "the real BUILD.gn parses to ${real_count} compiled .c files, expected 26: ${real_parsed}")
  else()
    message(STATUS "PASS build_gn_real (26 compiled .c files, ${real_skipped} entries skipped as per-OS)")
  endif()
endif()

# ---------------------------------------------------------------- calibration

# Deliberately wrong, and it must fail this run: this is the case that would have caught r20's macro bug, where the expectation list
# was never compared and all thirteen cases passed while measuring nothing.
if(DEFINED CALIBRATE AND CALIBRATE STREQUAL "wrong")
  message(STATUS "calibration: expecting WITH_HTML to be 0 although the contract turns it on")
  check_expectations(calibration_wrong_expectation "WITH_HTML=0")
endif()

# The empty case must fail *and* the run must keep measuring. Before the fix it printed one FAIL and returned out of the file, so everything
# below it — including the final failure check — never ran and cmake exited 0.
if(DEFINED CALIBRATE AND CALIBRATE STREQUAL "empty")
  message(STATUS "calibration: a case with no expectations must fail, and the run must continue to the next case")
  set(calibration_sentinel "continued")
  check_expectations(calibration_empty_case)
  check_expectations(calibration_after_empty "calibration_sentinel=continued")
  check_expectations(calibration_malformed_pair "this is not a pair")
  check_expectations(calibration_three_fields "WITH_HTML=0=extra")
endif()

# ---------------------------------------------------------------- result

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} of ${cases} cases failed")
endif()
message(STATUS "all ${cases} cases passed in ${SCRATCH}")
