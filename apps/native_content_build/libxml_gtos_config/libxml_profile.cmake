# libxml2's WITH_* feature profile, derived from the source list //third_party/libxml/BUILD.gn actually compiles.
#
# This lives in its own file so the derivation can be run without a compiler: `cmake -P test_profile.cmake` calls
# libxml_derive_profile() on synthetic BUILD.gn files and checks the answers, including the refusal that fires when a source
# libxml2 cannot be built without stops being listed. The generator itself (CMakeLists.txt) only calls the same function on the
# real file, so the test exercises the code that ships.

function(libxml_derive_profile build_gn_path)
  if(NOT EXISTS "${build_gn_path}")
    message(FATAL_ERROR "no BUILD.gn at ${build_gn_path}")
  endif()
  file(READ "${build_gn_path}" build_gn_text)

  # One entry per feature: which macro xmlversion.h expects, and the .c file whose presence in BUILD.gn justifies turning it on.
  # The mapping is the one the sources themselves use — e.g. pattern.c is what implements the pattern API that WITH_PATTERN gates.
  set(profile_sources
      "WITH_C14N|c14n.c"
      "WITH_CATALOG|catalog.c"
      "WITH_DEBUG|debugXML.c"
      "WITH_HTML|HTMLparser.c"
      "WITH_OUTPUT|xmlIO.c"
      "WITH_PATTERN|pattern.c"
      "WITH_PUSH|SAX2.c"
      "WITH_READER|xmlreader.c"
      "WITH_REGEXPS|xmlregexp.c"
      "WITH_RELAXNG|relaxng.c"
      "WITH_SAX1|SAX.c"
      "WITH_SCHEMAS|xmlschemas.c"
      "WITH_SCHEMATRON|schematron.c"
      "WITH_VALID|valid.c"
      "WITH_WRITER|xmlwriter.c"
      "WITH_XINCLUDE|xinclude.c"
      "WITH_XPATH|xpath.c"
      "WITH_XPTR|xpointer.c")

  foreach(entry IN LISTS profile_sources)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 with_var)
    list(GET parts 1 c_file)
    # A commented-out line does not count: the match has to start the line with optional indentation and a quote.
    string(REGEX MATCH "(^|\n)[ \t]*\"src/${c_file}\"," hit "${build_gn_text}")
    if(hit STREQUAL "")
      set(${with_var} 0)
    else()
      set(${with_var} 1)
    endif()
    set(${with_var} ${${with_var}} PARENT_SCOPE)
  endforeach()

  # These three carry libxml2's parser, I/O and validation core; if the parse above ever stops matching (the sources list moves into
  # a variable, say), every WITH_* would quietly become 0 and the failure would surface far from here. Fail here instead.
  foreach(core_file HTMLparser.c xmlIO.c valid.c)
    set(core_found 0)
    foreach(entry IN LISTS profile_sources)
      string(REPLACE "|" ";" parts "${entry}")
      list(GET parts 1 c_file)
      if(c_file STREQUAL core_file)
        list(GET parts 0 with_var)
        if(${with_var})
          set(core_found 1)
        endif()
      endif()
    endforeach()
    if(NOT core_found)
      message(FATAL_ERROR "BUILD.gn lists no compiled src/${core_file}: libxml2 cannot be built without it, so either the source list "
                          "format changed or the wrong file was passed (${build_gn_path})")
    endif()
  endforeach()

  # WITH_THREADS, WITH_ICONV, WITH_ZLIB, WITH_ICU and WITH_MODULES are not source-list questions: they say whether the target has a
  # thread ABI and whether the libraries are there, so they come from the compiler probes in CMakeLists.txt.
endfunction()

# The other half of "did the templates get an answer": a generated header that kept "@NAME@" or a bare "#if " is not a configuration,
# it is a compile error waiting to happen thousands of lines away. The first run of this project shipped exactly that (WITH_HTTP was
# missing), so the check is part of the derivation instead of being a comment in the generator. Kept here, next to the derivation, so
# test_profile.cmake can run it on files whose defects are known.
function(libxml_check_generated generated_path)
  if(NOT EXISTS "${generated_path}")
    message(FATAL_ERROR "no generated file at ${generated_path}")
  endif()
  file(SIZE "${generated_path}" generated_size)
  if(generated_size LESS 256)
    message(FATAL_ERROR "generated ${generated_path} is only ${generated_size} bytes")
  endif()
  file(READ "${generated_path}" generated_text)
  if(generated_text MATCHES "@[A-Za-z_0-9]+@")
    string(REGEX MATCH "@[A-Za-z_0-9]+@" leftover "${generated_text}")
    message(FATAL_ERROR "${generated_path} still contains ${leftover}: this project left a template variable unanswered")
  endif()
  if(generated_text MATCHES "(^|\n)#[ \t]*if[ \t]*\n")
    message(FATAL_ERROR "${generated_path} contains an empty #if, which is what an unanswered template variable looks like")
  endif()
endfunction()
