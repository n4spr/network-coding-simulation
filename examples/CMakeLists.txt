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

build_lib_example(
  NAME network-coding-butterfly-oslr
  SOURCE_FILES network-coding-butterfly-oslr.cpp
  LIBRARIES_TO_LINK
    ${libnetwork-coding}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
    ${libflow-monitor}
    ${libolsr}
)
