# libxml2's GTOS profile, derived without a compiler: which sources the real GN target compiles, and which WITH_* features the
# configuration may then promise.
#
# Two things changed after the r20 review, and both are the reason this file looks the way it does:
#
#   1. The source set used to come from a regular expression over the whole BUILD.gn. That regex could not tell a target's `sources`
#      list from its `inputs`, from a sibling target, or from a branch behind `if (is_win)` — and the r20 derivation turned features
#      on because a name appeared anywhere in the file. libxml_parse_target_sources() below reads one named target, tracks braces,
#      list names and conditionals, and refuses to guess about a condition it cannot decide.
#
#   2. "The file is compiled" was treated as "the feature is on". In the pinned tree that is false in both directions: pattern.c and
#      valid.c are compiled but Chromium declares pattern/valid off (their bodies sit behind LIBXML_*_ENABLED, so those objects are
#      effectively empty), and SAX.c is *not* compiled while Chromium declares sax1 on (the SAX1 API lives in SAX2.c). The feature
#      answers therefore come from a contract table that records what upstream and Chromium declare, what the build actually
#      compiles, and which target capability is required — and the resolution fails loudly instead of guessing.
#
# What this file does not decide: whether a symbol links, and whether the result runs. Both are measured by callers that link and
# execute, and by nothing here.

# ---------------------------------------------------------------- GN: one target's compiled sources

# Decide a GN conditional for the GTOS configuration.
#
# This used to be a substring test (`MATCHES "is_win|is_mac|..."`), which answered `!is_win` false — the text contains `is_win` — and
# `is_gtos || is_win` false as well, so sources that the pinned file does compile were dropped from the compiled set without a word. The
# condition is parsed here instead: `||` between clauses, `&&` between terms, a leading `!` per term, and a closed atom set. Atoms outside
# that set make the whole condition `unknown`, which the caller refuses, so an unrecognised expression can never be answered by accident.
function(libxml_condition_atom atom out_var)
  if(atom STREQUAL "is_gtos" OR atom STREQUAL "true")
    set(${out_var} true PARENT_SCOPE)
  elseif(atom STREQUAL "false")
    set(${out_var} false PARENT_SCOPE)
  elseif(atom MATCHES "^(is_win|is_mac|is_ios|is_apple|is_android|is_chromeos|is_fuchsia|is_linux|is_nacl|is_posix|is_bsd|is_openbsd)$")
    set(${out_var} false PARENT_SCOPE)
  else()
    set(${out_var} unknown PARENT_SCOPE)
  endif()
endfunction()

function(libxml_condition_state condition_text out_var)
  string(REPLACE "\t" " " compact "${condition_text}")
  string(REGEX REPLACE "[ ]+" "" compact "${compact}")
  if(compact STREQUAL "")
    set(${out_var} unknown PARENT_SCOPE)
    return()
  endif()
  string(REPLACE "||" ";" clauses "${compact}")
  set(any_clause_true 0)
  set(any_unknown 0)
  foreach(clause IN LISTS clauses)
    string(REPLACE "&&" ";" terms "${clause}")
    set(term_false 0)
    set(term_unknown 0)
    foreach(raw_term IN LISTS terms)
      set(term "${raw_term}")
      set(negations 0)
      while(term MATCHES "^!")
        string(SUBSTRING "${term}" 1 -1 term)
        math(EXPR negations "${negations} + 1")
      endwhile()
      if(term STREQUAL "")
        set(term_unknown 1)
        continue()
      endif()
      libxml_condition_atom("${term}" atom_state)
      if(atom_state STREQUAL "unknown")
        set(term_unknown 1)
      else()
        if(negations EQUAL 1)
          if(atom_state STREQUAL "true")
            set(atom_state "false")
          else()
            set(atom_state "true")
          endif()
        elseif(negations GREATER 1)
          # `!!x` is x, `!!!x` is !x: only the parity matters, and the parser above already collapsed nothing, so it is decided here.
          math(EXPR odd "${negations} % 2")
          if(odd EQUAL 1)
            if(atom_state STREQUAL "true")
              set(atom_state "false")
            else()
              set(atom_state "true")
            endif()
          endif()
        endif()
        if(atom_state STREQUAL "false")
          set(term_false 1)
        endif()
      endif()
    endforeach()
    if(term_false)
      # a && false is false whatever the rest of the clause says
    elseif(term_unknown)
      set(any_unknown 1)
    else()
      set(any_clause_true 1)
    endif()
  endforeach()
  if(any_clause_true)
    set(${out_var} true PARENT_SCOPE)
  elseif(any_unknown)
    set(${out_var} unknown PARENT_SCOPE)
  else()
    set(${out_var} false PARENT_SCOPE)
  endif()
endfunction()

# Sort one quoted entry of a `sources` list into the buckets the caller reports separately: libxml .c files, other files the target
# compiles (config.h, *.cc), and entries that are compiled but disabled by the feature contract (handled by the caller).
function(libxml_classify_entry entry out_sources out_other)
  set(source_list "")
  set(other_list "")
  if(entry MATCHES "^src/([A-Za-z0-9_]+\\.c)$")
    set(source_list "${CMAKE_MATCH_1}")
  else()
    set(other_list "${entry}")
  endif()
  set(${out_sources} "${source_list}" PARENT_SCOPE)
  set(${out_other} "${other_list}" PARENT_SCOPE)
endfunction()

# Read a text file as a list of lines, one element per line, without file(STRINGS).
#
# file(STRINGS) is deliberately not used here: on CMake 3.16.3 a line that opens a bracket — "sources = [" is exactly that shape — merges
# with every following line up to the closing "]", so a BUILD.gn came back as three elements instead of one per line and the r21 parser
# matched a whole list as if it were a single line. Splitting the text here is a few lines and has no such surprise.
function(libxml_brace_block text open_at out_block out_close_at)
  string(LENGTH "${text}" text_length)
  set(depth 0)
  set(position ${open_at})
  set(block_start -1)
  while(position LESS text_length)
    string(SUBSTRING "${text}" ${position} 1 character)
    if(character STREQUAL "{")
      math(EXPR depth "${depth} + 1")
      if(depth EQUAL 1)
        math(EXPR block_start "${position} + 1")
      endif()
    elseif(character STREQUAL "}")
      math(EXPR depth "${depth} - 1")
      if(depth EQUAL 0)
        math(EXPR block_length "${position} - ${block_start}")
        string(SUBSTRING "${text}" ${block_start} ${block_length} block)
        set(${out_block} "${block}" PARENT_SCOPE)
        set(${out_close_at} ${position} PARENT_SCOPE)
        return()
      endif()
    endif()
    math(EXPR position "${position} + 1")
  endwhile()
  message(FATAL_ERROR "unbalanced braces: the block opened at ${open_at} is never closed")
endfunction()

function(libxml_parse_target_sources build_gn_path target_name out_sources out_other out_skipped)
  if(NOT EXISTS "${build_gn_path}")
    message(FATAL_ERROR "no BUILD.gn at ${build_gn_path}")
  endif()
  file(READ "${build_gn_path}" text)
  string(REPLACE "\r\n" "\n" text "${text}")
  string(REPLACE "\r" "\n" text "${text}")
  # Comments first, so a commented-out source cannot be mistaken for one that compiles (the r20 regex needed the same rule; this time it is
  # applied to the text the parser actually walks). No GN path in this file contains '#', and a '#' inside a string is not something the
  # pinned BUILD.gn does.
  string(REGEX REPLACE "#[^\n]*" "" text "${text}")

  # The target block, located by its declaration and matched by braces. A sibling target is simply never entered, which is the difference
  # between reading a target and reading a file.
  string(REGEX MATCH "(static_library|source_set|component|shared_library|group)[ ]*\\(\"${target_name}\"\\)[ ]*\\{" declaration "${text}")
  if(declaration STREQUAL "")
    message(FATAL_ERROR "${build_gn_path} declares no target \"${target_name}\": either the file changed shape or the wrong target name was "
                        "passed (the parser reads one target's sources, not every quoted path in the file)")
  endif()
  string(LENGTH "${declaration}" declaration_length)
  string(FIND "${text}" "${declaration}" declaration_at)
  math(EXPR brace_at "${declaration_at} + ${declaration_length} - 1")
  libxml_brace_block("${text}" ${brace_at} body body_close_at)

  # Conditionals inside the target. The source list has per-OS branches, and a branch that cannot be true for GTOS must not contribute
  # sources. Each branch is removed (false) or replaced by its own body (true); a condition this build cannot decide is refused, because
  # guessing is how a Windows-only file ends up in a GTOS source list.
  set(guard 0)
  while(1)
    math(EXPR guard "${guard} + 1")
    if(guard GREATER 200)
      message(FATAL_ERROR "conditional rewriting did not converge in ${build_gn_path}; this parser handles plain if-blocks, not a maze")
    endif()
    string(REGEX MATCH "if[ ]*\\(([^)]*)\\)[ ]*\\{" if_open "${body}")
    if(if_open STREQUAL "")
      break()
    endif()
    set(condition_text "${CMAKE_MATCH_1}")
    libxml_condition_state("${condition_text}" condition_state)
    if(condition_state STREQUAL "unknown")
      message(FATAL_ERROR "${build_gn_path}: cannot decide 'if (${condition_text})' for the GTOS configuration; refusing to guess whether "
                          "the sources under it compile (teach libxml_condition_state() about this variable)")
    endif()
    string(FIND "${body}" "${if_open}" if_at)
    string(LENGTH "${if_open}" if_length)
    math(EXPR if_brace "${if_at} + ${if_length} - 1")
    libxml_brace_block("${body}" ${if_brace} if_body if_close_at)
    string(SUBSTRING "${body}" 0 ${if_at} before)
    math(EXPR after_at "${if_close_at} + 1")
    string(SUBSTRING "${body}" ${after_at} -1 after)
    if(condition_state STREQUAL "true")
      set(body "${before}${if_body}${after}")
    else()
      set(body "${before}${after}")
    endif()
  endwhile()
  if(body MATCHES "else")
    message(FATAL_ERROR "${build_gn_path}: 'else' inside target \"${target_name}\" is not handled by this parser; refusing rather than "
                        "dropping a branch")
  endif()

  # Only `sources`/`sources +=` lists are compile lists. `inputs`, `deps`, `configs`, `visibility` are not, and the r20 whole-file regex
  # could not tell them apart.
  set(sources "")
  set(other "")
  set(position 0)
  while(1)
    string(SUBSTRING "${body}" ${position} -1 window)
    string(REGEX MATCH "sources[ ]*\\+?=[ ]*\\[([^]]*)\\]" sources_list "${window}")
    if(sources_list STREQUAL "")
      break()
    endif()
    string(FIND "${body}" "${sources_list}" list_at)
    set(entries "${CMAKE_MATCH_1}")
    string(REGEX MATCHALL "\"[^\"]+\"" quoted "${entries}")
    foreach(entry_text IN LISTS quoted)
      string(REGEX REPLACE "^\"(.*)\"$" "\\1" entry "${entry_text}")
      set(entry_sources "")
      set(entry_other "")
      libxml_classify_entry("${entry}" entry_sources entry_other)
      if(NOT entry_sources STREQUAL "")
        list(APPEND sources "${entry_sources}")
      elseif(NOT entry_other STREQUAL "")
        list(APPEND other "${entry_other}")
      endif()
    endforeach()
    string(LENGTH "${sources_list}" list_length)
    math(EXPR position "${list_at} + ${list_length}")
  endwhile()

  if(sources STREQUAL "")
    message(FATAL_ERROR "${build_gn_path} declares no compiled src/*.c for target \"${target_name}\": either the file changed shape or the "
                        "wrong target name was passed")
  endif()
  list(REMOVE_DUPLICATES sources)
  list(SORT sources)
  list(REMOVE_DUPLICATES other)
  list(SORT other)
  set(${out_sources} "${sources}" PARENT_SCOPE)
  set(${out_other} "${other}" PARENT_SCOPE)
  # Per-OS branches are gone by now; the caller reports them by not seeing them. The variable is kept so callers do not have to change
  # shape, and it is empty on purpose: a branch that was dropped is not "skipped evidence", it simply does not compile here.
  set(${out_skipped} "" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------- the feature contract

# One line per template variable: <feature>|<chromium says>|<sources that implement it>|<capability it needs>|<why>
# `chromium says` is read from //third_party/libxml/chromium/roll.py's configure options for the pinned version, not from taste: the
# ON list there is html, icu, output, push, reader, sax1, threads, tree, writer, xpath, and everything else is explicitly disabled.
# `sources` names the files whose code provides the feature (several files where several do), `-` when the feature is not about a
# particular file but about a library or target capability.
function(libxml_contract_entries out_var)
  set(entries
      "WITH_HTML|on|HTMLparser.c,HTMLtree.c|-|tree-to-HTML serializer, Chromium enables html and compiles both files"
      "WITH_OUTPUT|on|xmlIO.c|-|the I/O layer, Chromium enables output and compiles it"
      "WITH_PUSH|on|SAX2.c|-|push parser, Chromium enables push"
      "WITH_READER|on|xmlreader.c|-|reader API, Chromium enables reader"
      "WITH_WRITER|on|xmlwriter.c|-|writer API, Chromium enables writer"
      "WITH_XPATH|on|xpath.c|-|XPath, Chromium enables xpath"
      "WITH_SAX1|on|SAX2.c|-|the SAX1 compatibility API is implemented in SAX2.c (grep LIBXML_SAX1_ENABLED), SAX.c is deleted by roll.py, so its absence is not evidence that SAX1 is off"
      "WITH_ICU|on|-|icu|Chromium configures --with-icu, needs an ICU library the GTOS SDK does not have"
      "WITH_THREADS|on|threads.c|threads|Chromium configures --with-threads, threads.c needs pthread.h, which the SDK does not expose"
      "WITH_C14N|off|-|-|Chromium passes --without-c14n and roll.py deletes c14n.c"
      "WITH_CATALOG|off|-|-|Chromium passes --without-catalog and deletes catalog.c"
      "WITH_DEBUG|off|-|-|Chromium passes --without-debug"
      "WITH_PATTERN|off|pattern.c|-|Chromium passes --without-pattern, the file is still compiled but its body is behind LIBXML_PATTERN_ENABLED, so the object is effectively empty"
      "WITH_REGEXPS|off|-|-|Chromium passes --without-regexps"
      "WITH_RELAXNG|off|-|-|Chromium passes --without-relaxng"
      "WITH_SCHEMAS|off|-|-|Chromium passes --without-schemas"
      "WITH_SCHEMATRON|off|-|-|Chromium passes --without-schematron"
      "WITH_VALID|off|valid.c|-|Chromium passes --without-valid, the file is still compiled but its body is behind LIBXML_VALID_ENABLED"
      "WITH_XINCLUDE|off|-|-|Chromium passes --without-xinclude"
      "WITH_XPTR|off|-|-|Chromium passes --without-xptr"
      "WITH_ICONV|off|-|iconv|Chromium passes --without-iconv"
      "WITH_ZLIB|off|-|zlib|Chromium passes --without-zlib"
      "WITH_LZMA|off|-|lzma|Chromium passes --without-lzma"
      "WITH_MODULES|off|-|modules|Chromium passes --without-modules"
      "WITH_ISO8859X|off|-|-|Chromium passes --without-iso8859x"
      "WITH_HTTP|off|-|-|libxml2 2.16 removed HTTP and kept stubs, Chromium passes --without-http"
      "WITH_WINPATH|off|-|-|Windows path handling, meaningless off Windows"
      )
  set(${out_var} "${entries}" PARENT_SCOPE)
endfunction()

# Resolve the contract for a measured build:
#   * a feature Chromium enables needs every implementing source compiled, or the contract is impossible here (hard error);
#   * it also needs its capability (ICU library, pthread API, ...); if that is missing the caller must downgrade it explicitly by
#     naming it in LIBXML_DOWNGRADES, so a missing capability can never silently become "off";
#   * a feature Chromium disables is off, and the reason says so.
# The result is the *honoured* feature set: what xmlversion.h is allowed to declare. It is not a statement that anything links.
#
# The optional fourth argument is where the names in `downgrades` came from: "caller" (the default, the historical meaning) or "default",
# meaning this configuration turned the feature off on its own because building it would only work after the SDK gained the capability. The
# derived reason and the `<FEATURE>_DOWNGRADE_ORIGIN` variable both say which, because "requested explicitly" must not describe a decision the
# caller never made.
function(libxml_resolve_contract compiled_sources capabilities downgrades)
  if(ARGC GREATER 3)
    set(downgrade_origin "${ARGV3}")
  else()
    set(downgrade_origin "caller")
  endif()
  if(NOT downgrade_origin STREQUAL "caller" AND NOT downgrade_origin STREQUAL "default")
    message(FATAL_ERROR "downgrade origin must be 'caller' or 'default', not '${downgrade_origin}'")
  endif()
  list(LENGTH compiled_sources compiled_count)
  if(compiled_count EQUAL 0)
    message(FATAL_ERROR "no compiled sources were passed to libxml_resolve_contract()")
  endif()
  libxml_contract_entries(entries)
  foreach(downgrade IN LISTS downgrades)
    set(known 0)
    foreach(line IN LISTS entries)
      string(REPLACE "|" ";" fields "${line}")
      list(GET fields 0 feature_name)
      if(feature_name STREQUAL downgrade)
        set(known 1)
      endif()
    endforeach()
    if(NOT known)
      message(FATAL_ERROR "LIBXML_DOWNGRADES names ${downgrade}, which is not a feature in the contract table")
    endif()
  endforeach()

  foreach(line IN LISTS entries)
    string(REPLACE "|" ";" fields "${line}")
    list(GET fields 0 feature_name)
    list(GET fields 1 chromium_says)
    list(GET fields 2 needed_sources)
    string(REPLACE "," ";" needed_sources "${needed_sources}")
    list(GET fields 3 capability)
    list(GET fields 4 rationale)
    set(reason "${rationale}")
    if(chromium_says STREQUAL "off")
      set(value 0)
      # A source that is compiled anyway is a compile-time stub, and saying so here keeps the next reader from counting it twice.
      if(NOT needed_sources STREQUAL "-")
        foreach(source IN LISTS needed_sources)
          list(FIND compiled_sources "${source}" source_at)
          if(NOT source_at LESS 0)
            string(APPEND reason "; src/${source} is compiled but empty without this macro")
          endif()
        endforeach()
      endif()
    else()
      set(value 1)
      if(NOT needed_sources STREQUAL "-")
        foreach(source IN LISTS needed_sources)
          list(FIND compiled_sources "${source}" source_at)
          if(source_at LESS 0)
            message(FATAL_ERROR "the contract enables ${feature_name} but ${needed_sources} must implement it and BUILD.gn does not "
                                "compile src/${source}: the source list and the contract disagree")
          endif()
        endforeach()
      endif()
      if(NOT capability STREQUAL "-")
        set(capability_ok 0)
        foreach(pair IN LISTS capabilities)
          string(REPLACE "=" ";" kv "${pair}")
          list(GET kv 0 cap_name)
          list(GET kv 1 cap_value)
          if(cap_name STREQUAL capability AND cap_value)
            set(capability_ok 1)
          endif()
        endforeach()
        if(NOT capability_ok)
          list(FIND downgrades "${feature_name}" downgrade_at)
          if(NOT downgrade_at LESS 0)
            set(value 0)
            if(downgrade_origin STREQUAL "caller")
              string(APPEND reason "; capability '${capability}' is absent and this downgrade was requested explicitly by the caller")
            else()
              string(APPEND reason "; capability '${capability}' is absent and this configuration turns the feature off by default")
            endif()
            set(${feature_name}_DOWNGRADE_ORIGIN "${downgrade_origin}" PARENT_SCOPE)
          else()
            message(FATAL_ERROR "${feature_name} needs the '${capability}' capability, which this SDK does not provide. Either provide it "
                                "or name ${feature_name} in -DLIBXML_DOWNGRADES=... so the reduction is explicit and reported")
          endif()
        endif()
      endif()
    endif()
    set(${feature_name} ${value} PARENT_SCOPE)
    set(${feature_name}_REASON "${reason}" PARENT_SCOPE)
  endforeach()
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
