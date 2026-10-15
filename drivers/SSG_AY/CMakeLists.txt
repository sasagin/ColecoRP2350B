add_library(aySoundSoft INTERFACE)
target_link_libraries(aySoundSoft INTERFACE hardware_gpio)
target_sources(aySoundSoft INTERFACE ${CMAKE_CURRENT_LIST_DIR}/aySoundSoft.c)
target_include_directories(aySoundSoft INTERFACE ${CMAKE_CURRENT_LIST_DIR})
