set(source_files
  model/galois-field.cc
  model/network-coding-packet.cc
  model/network-coding-encoder.cc
  model/network-coding-decoder.cc
  model/network-coding-udp-application.cc
  helper/network-coding-helper.cc
)

set(header_files
  model/galois-field.h
  model/network-coding-packet.h
  model/network-coding-encoder.h
  model/network-coding-decoder.h
  model/network-coding-udp-application.h
  helper/network-coding-helper.h
)

build_lib(
  LIBNAME network-coding
  SOURCE_FILES ${source_files}
  HEADER_FILES ${header_files}
  LIBRARIES_TO_LINK 
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libapplications}
    ${libpoint-to-point}
  TEST_SOURCES
    test/network-coding-test-suite.cc
)