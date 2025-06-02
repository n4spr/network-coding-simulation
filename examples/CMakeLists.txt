build_lib_example(
  NAME network-coding-example
  SOURCE_FILES network-coding-example.cc
  LIBRARIES_TO_LINK
    ${libnetwork-coding}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
    ${libflow-monitor}
)

build_lib_example(
  NAME network-coding-comparison-example
  SOURCE_FILES network-coding-comparison-example.cc
  LIBRARIES_TO_LINK
    ${libnetwork-coding}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
    ${libflow-monitor}
)

build_lib_example(
  NAME network-coding-against-tcp
  SOURCE_FILES network-coding-against-tcp.cc
  LIBRARIES_TO_LINK
    ${libnetwork-coding}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
    ${libflow-monitor}
)

build_lib_example(
  NAME network-encoding-verification
  SOURCE_FILES network-encoding-verification.cc
  LIBRARIES_TO_LINK
    ${libnetwork-coding}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
    ${libflow-monitor}
)

build_lib_example(
  NAME network-encoding-minimal
  SOURCE_FILES network-encoding-minimal.cc
  LIBRARIES_TO_LINK
    ${libnetwork-coding}
    ${libcore}
)

build_lib_example(
  NAME network-coding-butterfly
  SOURCE_FILES network-coding-butterfly.cc
  LIBRARIES_TO_LINK
    ${libnetwork-coding}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
    ${libflow-monitor}
    ${libcore}
    ${libnetwork}
)