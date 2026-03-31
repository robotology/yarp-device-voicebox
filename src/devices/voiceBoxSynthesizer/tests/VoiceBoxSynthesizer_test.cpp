/*
 * SPDX-FileCopyrightText: 2023 Istituto Italiano di Tecnologia (IIT)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <yarp/os/Network.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/WrapperSingle.h>

#include <catch2/catch_amalgamated.hpp>
#include <harness.h>

using namespace yarp::dev;
using namespace yarp::os;

TEST_CASE("dev::voiceBoxSynthesizer_test", "[yarp::dev]")
{
    YARP_REQUIRE_PLUGIN("voiceBoxSynthesizer", "device");

    Network::setLocalMode(true);

    SECTION("Checking the device")
    {
        PolyDriver dd;

        Property pcfg;
        pcfg.put("device", "voiceBoxSynthesizer");
        pcfg.put("baseUrl", "http://localhost:17493");
        pcfg.put("profileId", "1b8f5031-c12d-453e-baf7-c0609c7590ec");

        REQUIRE(dd.open(pcfg));

        yarp::dev::ISpeechSynthesizer* speech = nullptr;
        REQUIRE(dd.view(speech));
        REQUIRE(speech != nullptr);

        // Basic interface checks
        CHECK(speech->setLanguage("en"));
        std::string language;
        CHECK(speech->getLanguage(language));
        CHECK(language == "en");

        CHECK(speech->setVoice("auto"));
        std::string voice;
        CHECK(speech->getVoice(voice));
        CHECK(voice == "auto");

        CHECK(speech->setSpeed(1.0));
        double speed;
        CHECK(speech->getSpeed(speed));
        CHECK(speed == Approx(1.0));

        CHECK(speech->setPitch(1.0));
        double pitch;
        CHECK(speech->getPitch(pitch));
        CHECK(pitch == Approx(1.0));

        yarp::sig::Sound sound;
        // Synthesize may fail if backend unavailable; we skip strict check in that case
        yarp::dev::ReturnValue ret = speech->synthesize("test", sound);
        CHECK((bool)ret); // for return_code::ok if backend reachable

        CHECK(dd.close());
    }

    Network::setLocalMode(false);
}
