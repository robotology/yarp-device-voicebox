# SPDX-FileCopyrightText: 2023 Istituto Italiano di Tecnologia (IIT)
# SPDX-License-Identifier: BSD-3-Clause

yarp_prepare_plugin(voiceBoxSynthesizer
  CATEGORY device
  TYPE VoiceBoxSynthesizer
  INCLUDE VoiceBoxSynthesizer.h
  INTERNAL ON
)

if(NOT SKIP_voiceBoxSynthesizer)
  find_package(CURL REQUIRED)
  yarp_add_plugin(yarp_voiceBoxSynthesizer)
  generateDeviceParamsParser(VoiceBoxSynthesizer voiceBoxSynthesizer)

  target_sources(yarp_voiceBoxSynthesizer
    PRIVATE
      VoiceBoxSynthesizer.cpp
      VoiceBoxSynthesizer.h
      VoiceBoxSynthesizer_ParamsParser.cpp
      VoiceBoxSynthesizer_ParamsParser.h
  )

  target_link_libraries(yarp_voiceBoxSynthesizer
    PRIVATE
      YARP::YARP_os
      YARP::YARP_sig
      YARP::YARP_dev
      CURL::libcurl
  )

  yarp_install(
    TARGETS yarp_voiceBoxSynthesizer
    EXPORT yarp-device-voiceBoxSynthesizer
    COMPONENT yarp-device-voiceBoxSynthesizer
    LIBRARY DESTINATION ${YARP_DYNAMIC_PLUGINS_INSTALL_DIR}
    ARCHIVE DESTINATION ${YARP_STATIC_PLUGINS_INSTALL_DIR}
    YARP_INI DESTINATION ${YARP_PLUGIN_MANIFESTS_INSTALL_DIR}
  )

  if(YARP_COMPILE_TESTS)
    add_subdirectory(tests)
  endif()

  set_property(TARGET yarp_voiceBoxSynthesizer PROPERTY FOLDER "Plugins/Device")
endif()
