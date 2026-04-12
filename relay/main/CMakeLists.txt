idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_app_format
)

# THERMA_RELAY_ROLE is a cache var set at build time:
#   idf.py -C relay -B relay/build_heater -DTHERMA_RELAY_ROLE=HEATER build
#   idf.py -C relay -B relay/build_cooler -DTHERMA_RELAY_ROLE=COOLER build
#
# The role flag only affects logging and the version banner — core
# logic is byte-for-byte identical in both artifacts.
if(DEFINED THERMA_RELAY_ROLE)
    string(TOUPPER "${THERMA_RELAY_ROLE}" THERMA_RELAY_ROLE_UPPER)
    if(THERMA_RELAY_ROLE_UPPER STREQUAL "HEATER")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE THERMA_RELAY_ROLE_HEATER=1)
    elseif(THERMA_RELAY_ROLE_UPPER STREQUAL "COOLER")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE THERMA_RELAY_ROLE_COOLER=1)
    else()
        message(FATAL_ERROR "THERMA_RELAY_ROLE must be HEATER or COOLER")
    endif()
else()
    message(WARNING "THERMA_RELAY_ROLE not set — building unconfigured relay firmware")
endif()
