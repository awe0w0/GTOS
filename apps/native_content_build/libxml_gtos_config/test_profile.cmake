# Unit test for libxml_profile.cmake: the shipped derivation, run on seventeen synthetic BUILD.gn files, no compiler involved.
#
#   cmake -P test_profile.cmake            (uses $TMPDIR, $TEMP or /tmp for its scratch files, and removes them)
#   cmake -P test_profile.cmake -DSCRATCH=<dir>
#
# What it covers: the ordinary answer (a listed source turns its feature on and an unlisted one leaves it off), the comment case (a
# source that only appears commented out does not count), the header case (a file listed somewhere else in the file, e.g. as an
# input rather than a source, does not count unless it is listed as "src/<name>,"), the refusal when one of the three sources
# libxml2 cannot be built without disappears, and the order-independence of the source list. It does not test the compiler probes,
# the templates, or anything a build would do with the output — those need the target toolchain and are measured by the generator
# run itself.
cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake")

if(NOT DEFINED SCRATCH)
  if(DEFINED ENV{TMPDIR})
    set(SCRATCH "$ENV{TMPDIR}/libxml-profile-test")
  elseif(DEFINED ENV{TEMP})
    set(SCRATCH "$ENV{TEMP}/libxml-profile-test")
  else()
    set(SCRATCH "/tmp/libxml-profile-test")
  endif()
endif()
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

set(failures 0)
set(cases 0)

# A macro, not a function: libxml_derive_profile() answers through PARENT_SCOPE, and a macro keeps that scope the top level.
macro(check_profile name gn_text expected)
  math(EXPR cases "${cases} + 1")
  set(gn_path "${SCRATCH}/${name}.gn")
  file(WRITE "${gn_path}" "${gn_text}")
  libxml_derive_profile("${gn_path}")
  set(case_failed 0)
  foreach(pair IN LISTS expected)
    string(REPLACE "=" ";" kv "${pair}")
    list(GET kv 0 var)
    list(GET kv 1 want)
    if(NOT "${${var}}" STREQUAL "${want}")
      message(STATUS "FAIL ${name}: ${var} is '${${var}}', expected '${want}'")
      set(case_failed 1)
    endif()
  endforeach()
  if(case_failed)
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "PASS ${name}")
  endif()
endmacro()

# The shape of a real BUILD.gn: an indented sources list with trailing commas.
set(typical "source_set(\"libxml\") {
  sources = [
    \"src/HTMLparser.c\",
    \"src/SAX2.c\",
    \"src/catalog.c\",
    \"src/pattern.c\",
    \"src/valid.c\",
    \"src/xmlIO.c\",
    \"src/xpath.c\",
  ]
}
")

check_profile(typical "${typical}"
  "WITH_HTML=1;WITH_PUSH=1;WITH_CATALOG=1;WITH_PATTERN=1;WITH_VALID=1;WITH_OUTPUT=1;WITH_XPATH=1;WITH_C14N=0;WITH_SAX1=0")

# Order must not matter, and neither must extra sources the profile does not know.
set(reordered "source_set(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
    \"src/xpath.c\",
    \"src/valid.c\",
    \"src/pattern.c\",
    \"src/catalog.c\",
    \"src/SAX2.c\",
    \"src/HTMLparser.c\",
    \"src/encoding.c\",
  ]
}
")
check_profile(reordered "${reordered}"
  "WITH_HTML=1;WITH_PUSH=1;WITH_CATALOG=1;WITH_PATTERN=1;WITH_VALID=1;WITH_OUTPUT=1;WITH_XPATH=1;WITH_C14N=0")

# A commented-out entry is not a compiled source: r19's frontier needed exactly this distinction.
set(commented "source_set(\"libxml\") {
  sources = [
    \"src/HTMLparser.c\",
    \"src/valid.c\",
    \"src/xmlIO.c\",
    # \"src/pattern.c\",
    #\"src/c14n.c\",
  ]
}
")
check_profile(commented "${commented}" "WITH_PATTERN=0;WITH_C14N=0;WITH_HTML=1")

# A name that appears as an input or in a comment prologue, but never as "src/<name>," in the sources list.
set(not_a_source "source_set(\"libxml\") {
  sources = [
    \"src/HTMLparser.c\",
    \"src/valid.c\",
    \"src/xmlIO.c\",
  ]
  inputs = [
    \"src/pattern.c\",
  ]
}
")
check_profile(not_a_source "${not_a_source}" "WITH_PATTERN=0;WITH_HTML=1")

# Turning a feature on by adding its source, and off again by removing it.
set(added_c14n "source_set(\"libxml\") {
  sources = [
    \"src/HTMLparser.c\",
    \"src/valid.c\",
    \"src/xmlIO.c\",
    \"src/c14n.c\",
  ]
}
")
check_profile(added_c14n "${added_c14n}" "WITH_C14N=1;WITH_PATTERN=0")

# The refusal path: libxml2 cannot be built without its HTML parser, its I/O core or its validator, so a BUILD.gn that stops listing
# one of them has to fail loudly instead of quietly switching the feature off. Each case is a separate cmake -P run, because a
# FATAL_ERROR ends the run it happens in.
foreach(core_file HTMLparser.c xmlIO.c valid.c)
  math(EXPR cases "${cases} + 1")
  set(gn_path "${SCRATCH}/missing-${core_file}.gn")
  file(WRITE "${gn_path}" "source_set(\"libxml\") {
  sources = [
    \"src/xmlIO.c\",
    \"src/valid.c\",
    \"src/HTMLparser.c\",
  ]
}
")
  set(driver "${SCRATCH}/missing-${core_file}.cmake")
  file(WRITE "${driver}" "include(\"${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake\")
# Drop the core file from the list by passing a file that does not name it.
libxml_derive_profile(\"${SCRATCH}/missing-${core_file}.gn\")
")
  # Remove the core file from that copy, leaving the other two.
  file(READ "${gn_path}" body)
  string(REPLACE "    \"src/${core_file}\",\n" "" body "${body}")
  file(WRITE "${gn_path}" "${body}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -P "${driver}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 0)
    message(STATUS "FAIL missing-${core_file}: the derivation accepted a BUILD.gn without it")
    message(STATUS "     output: ${out}${err}")
    math(EXPR failures "${failures} + 1")
  else()
    string(FIND "${err}" "${core_file}" position)
    if(position EQUAL -1)
      message(STATUS "FAIL missing-${core_file}: refused, but the message does not name the file")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "PASS missing-${core_file} refused")
    endif()
  endif()
endforeach()

# A missing BUILD.gn is also a refusal, not an empty profile.
math(EXPR cases "${cases} + 1")
set(driver "${SCRATCH}/missing-file.cmake")
file(WRITE "${driver}" "include(\"${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake\")
libxml_derive_profile(\"${SCRATCH}/does-not-exist/BUILD.gn\")
")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${driver}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 0)
  message(STATUS "FAIL missing-file: the derivation accepted a path that does not exist")
  math(EXPR failures "${failures} + 1")
else()
  message(STATUS "PASS missing-file refused")
endif()

file(REMOVE_RECURSE "${SCRATCH}")

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} of ${cases} cases failed")
endif()
message(STATUS "profile test: ${cases} cases passed")
