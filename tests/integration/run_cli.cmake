# End-to-end test of the real fxval-price executable, run by CTest as a CMake script:
#   cmake -DPRICE=<exe> -DCONFIGS=<dir> -DWORK=<scratch dir> -P run_cli.cmake
# It checks exit codes, standard output and error, files written, and that the config hash in the
# report equals CMake's own SHA-256 of the same file (an independent implementation).

if(NOT PRICE OR NOT CONFIGS OR NOT WORK)
  message(FATAL_ERROR "usage: -DPRICE=... -DCONFIGS=... -DWORK=... -P run_cli.cmake")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{SOURCE_DATE_EPOCH} "1700000000")

function(run_price expected_code)
  execute_process(
    COMMAND "${PRICE}" ${ARGN}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    INPUT_FILE "${WORK}/stdin.txt")
  if(NOT code EQUAL expected_code)
    message(FATAL_ERROR "fxval-price ${ARGN}: expected exit code ${expected_code}, got ${code}\n"
                        "stdout: ${out}\nstderr: ${err}")
  endif()
  set(LAST_OUT "${out}" PARENT_SCOPE)
  set(LAST_ERR "${err}" PARENT_SCOPE)
endfunction()

function(expect_contains text needle what)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${what}: expected to find '${needle}' in:\n${text}")
  endif()
endfunction()

file(WRITE "${WORK}/stdin.txt" "")

# 1. help and version
run_price(0 --help)
expect_contains("${LAST_OUT}" "usage: fxval-price" "help")
run_price(0 --version)
expect_contains("${LAST_OUT}" "fxval-price " "version")

# 2. every shipped example prices, in both formats, with metadata
foreach(example gk_vanillas heston_semi_analytic_smile heston_mc_qe)
  set(config "${CONFIGS}/${example}.json")
  run_price(0 --config "${config}" --output "${WORK}/${example}.json" --quiet)
  run_price(0 --config "${config}" --output "${WORK}/${example}.csv" --format csv --quiet)
  file(READ "${WORK}/${example}.json" report)
  file(READ "${WORK}/${example}.csv" csv)

  file(SHA256 "${config}" expected_hash)
  string(JSON reported_hash GET "${report}" metadata config_sha256)
  if(NOT reported_hash STREQUAL expected_hash)
    message(FATAL_ERROR "${example}: config_sha256 ${reported_hash} != CMake's ${expected_hash}")
  endif()
  expect_contains("${csv}" "# config_sha256: ${expected_hash}" "${example} csv")
  string(JSON stamp GET "${report}" metadata timestamp_utc)
  if(NOT stamp STREQUAL "2023-11-14T22:13:20Z")
    message(FATAL_ERROR "${example}: SOURCE_DATE_EPOCH not honoured (${stamp})")
  endif()
  string(JSON failed GET "${report}" summary failed)
  if(NOT failed EQUAL 0)
    message(FATAL_ERROR "${example}: ${failed} option(s) failed")
  endif()
  string(JSON n_options GET "${report}" summary options)
  string(JSON n_results LENGTH "${report}" results)
  if(NOT n_options EQUAL n_results)
    message(FATAL_ERROR "${example}: summary says ${n_options}, results has ${n_results}")
  endif()
  string(JSON git GET "${report}" metadata git_commit)
  if(git STREQUAL "")
    message(FATAL_ERROR "${example}: empty git_commit")
  endif()
endforeach()

# 3. the same input gives a byte-identical report
run_price(0 --config "${CONFIGS}/heston_mc_qe.json" --output "${WORK}/again.json" --quiet)
file(READ "${WORK}/heston_mc_qe.json" first)
file(READ "${WORK}/again.json" second)
if(NOT first STREQUAL second)
  message(FATAL_ERROR "two runs of the same configuration differ")
endif()

# 4. stdout carries only the report; the log goes to stderr
run_price(0 --config "${CONFIGS}/gk_vanillas.json")
string(JSON format_version GET "${LAST_OUT}" format_version)
expect_contains("${LAST_ERR}" "configuration ok" "log on stderr")

# 5. standard input
file(READ "${CONFIGS}/gk_vanillas.json" gk_text)
file(WRITE "${WORK}/stdin.txt" "${gk_text}")
run_price(0 --config - --quiet)
file(WRITE "${WORK}/stdin.txt" "")

# 6. exit codes for bad input
run_price(1)
expect_contains("${LAST_ERR}" "missing required option --config" "no arguments")
run_price(1 --config "${CONFIGS}/gk_vanillas.json" --format xml)
run_price(4 --config "${WORK}/does_not_exist.json")
file(WRITE "${WORK}/bad.json" "{\"format_version\": 1, \"market\": {\"spot\": -1}}")
run_price(2 --config "${WORK}/bad.json")
expect_contains("${LAST_ERR}" "market.spot" "field named in the message")
if(NOT "${LAST_OUT}" STREQUAL "")
  message(FATAL_ERROR "an invalid configuration must produce no report, got: ${LAST_OUT}")
endif()
file(WRITE "${WORK}/notjson.json" "this is not json")
run_price(2 --config "${WORK}/notjson.json")
run_price(4 --config "${CONFIGS}/gk_vanillas.json" --output "${WORK}/no_such_dir/out.json")

# 7. SOURCE_DATE_EPOCH must be whole seconds
set(ENV{SOURCE_DATE_EPOCH} "yesterday")
run_price(1 --config "${CONFIGS}/gk_vanillas.json")
expect_contains("${LAST_ERR}" "SOURCE_DATE_EPOCH" "bad epoch")

message(STATUS "fxval-price integration checks passed")
