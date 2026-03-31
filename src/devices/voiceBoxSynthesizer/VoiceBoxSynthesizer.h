/*
 * SPDX-FileCopyrightText: 2023 Istituto Italiano di Tecnologia (IIT)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef YARP_VOICEBOXSYNTHESIZER_H
#define YARP_VOICEBOXSYNTHESIZER_H

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <map>

#include <yarp/dev/DeviceDriver.h>
#include <yarp/dev/ISpeechSynthesizer.h>
#include "VoiceBoxSynthesizer_ParamsParser.h"


using profilesList = std::vector<std::map<std::string, std::string>>;
using profileMap = std::map<std::string, std::string>;


class VoiceBoxSynthesizer :
        public yarp::dev::DeviceDriver,
        public yarp::dev::ISpeechSynthesizer,
        public VoiceBoxSynthesizer_ParamsParser
{
public:
    VoiceBoxSynthesizer();
    VoiceBoxSynthesizer(const VoiceBoxSynthesizer&) = delete;
    VoiceBoxSynthesizer(VoiceBoxSynthesizer&&) noexcept = delete;
    VoiceBoxSynthesizer& operator=(const VoiceBoxSynthesizer&) = delete;
    VoiceBoxSynthesizer& operator=(VoiceBoxSynthesizer&&) noexcept = delete;
    ~VoiceBoxSynthesizer() override = default;

    // DeviceDriver
    bool open(yarp::os::Searchable& config) override;
    bool close() override;

    // ISpeechSynthesizer
    yarp::dev::ReturnValue setLanguage(const std::string& language) override;
    yarp::dev::ReturnValue getLanguage(std::string& language) override;
    yarp::dev::ReturnValue setVoice(const std::string& voice_name) override;
    yarp::dev::ReturnValue getVoice(std::string& voice_name) override;
    yarp::dev::ReturnValue setSpeed(const double speed) override;
    yarp::dev::ReturnValue getSpeed(double& speed) override;
    yarp::dev::ReturnValue setPitch(const double pitch) override;
    yarp::dev::ReturnValue getPitch(double& pitch) override;
    yarp::dev::ReturnValue synthesize(const std::string& text, yarp::sig::Sound& sound) override;

private:
    std::string m_baseUrl = "http://localhost:17493";
    std::string m_language = "en";
    profileMap m_profile;
    double m_speed = 1.0;
    double m_pitch = 1.0;
};

#endif // YARP_VOICEBOXSYNTHESIZER_H
