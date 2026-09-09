if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "INPUT, OUTPUT, and SYMBOL are required")
endif()

file(READ "${INPUT}" binary_hex HEX)
string(LENGTH "${binary_hex}" binary_hex_length)
math(EXPR binary_size "${binary_hex_length} / 2")
math(EXPR binary_remainder "${binary_size} % 4")
if(NOT binary_remainder EQUAL 0)
    message(FATAL_ERROR "SPIR-V byte count must be divisible by four")
endif()
string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," binary_bytes "${binary_hex}")
file(WRITE "${OUTPUT}"
    "#pragma once\n\n"
    "#include <cstddef>\n\n"
    "alignas(4) inline constexpr unsigned char ${SYMBOL}[] = {${binary_bytes}};\n"
    "inline constexpr std::size_t ${SYMBOL}Size = sizeof(${SYMBOL});\n")
