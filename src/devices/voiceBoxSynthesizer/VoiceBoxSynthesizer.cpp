/*
 * SPDX-FileCopyrightText: 2023 Istituto Italiano di Tecnologia (IIT)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include "VoiceBoxSynthesizer.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <yarp/os/LogComponent.h>
#include <yarp/os/LogStream.h>

using namespace yarp::os;
using namespace yarp::dev;
using json = nlohmann::json;

YARP_LOG_COMPONENT(VOICEBOXSYNTHESIZER, "yarp.voiceBoxSynthesizer", yarp::os::Log::TraceType);

namespace {

size_t writeStringCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<const char*>(contents), total);
    return total;
}

size_t writeBytesCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t total = size * nmemb;
    auto* out = static_cast<std::vector<unsigned char>*>(userp);
    const auto* begin = static_cast<const unsigned char*>(contents);
    out->insert(out->end(), begin, begin + total);
    return total;
}

struct HttpResponse
{
    long status = 0;
    std::string body;
};

struct HttpBytesResponse
{
    long status = 0;
    std::vector<unsigned char> body;
};

HttpResponse httpPostJson(const std::string& url, const json& payload)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    HttpResponse resp;
    std::string body = payload.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("HTTP POST failed: ") + curl_easy_strerror(rc));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse httpGetText(const std::string& url)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("HTTP GET failed: ") + curl_easy_strerror(rc));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
    curl_easy_cleanup(curl);
    return resp;
}

HttpBytesResponse httpGetBytes(const std::string& url)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    HttpBytesResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBytesCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("HTTP GET bytes failed: ") + curl_easy_strerror(rc));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
    yCDebug(VOICEBOXSYNTHESIZER) << "HTTP GET bytes completed with status " << resp.status << " and " << resp.body.size() << " bytes received";
    curl_easy_cleanup(curl);
    return resp;
}

struct DecodedAudio
{
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    std::vector<int16_t> interleavedPcm16;
};

DecodedAudio decodeWav16FromMemory(const std::vector<unsigned char>& wavBytes)
{
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 totalFrames = 0;

    int16_t* pcm = drwav_open_memory_and_read_pcm_frames_s16(
        wavBytes.data(),
        wavBytes.size(),
        &channels,
        &sampleRate,
        &totalFrames,
        nullptr);

    if (!pcm) {
        throw std::runtime_error("Unable to decode WAV from memory");
    }

    const size_t totalSamples = static_cast<size_t>(totalFrames) * channels;

    DecodedAudio out;
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.interleavedPcm16.assign(pcm, pcm + totalSamples);

    drwav_free(pcm, nullptr);
    return out;
}

profilesList parseProfiles(const std::string& profilesJson)
{
    profilesList profiles;

    json j = json::parse(profilesJson);

    // Handle both array and object responses
    json profilesArray = j.is_array() ? j : json::array();

    for (const auto& profileJson : profilesArray) {
        std::map<std::string, std::string> profile;

        // Extract required fields, validating that they are not null
        for (auto& [key, value] : profileJson.items()) {
            if (!value.is_null()) {
                profile[key] = value.is_string() ? value.get<std::string>() : value.dump();
            }
        }

        // Only add profile if it has required fields
        if (profile.count("id") && profile.count("name")) {
            profiles.push_back(profile);
        }
    }

    return profiles;
}

}

static yarp::sig::Sound decodedAudioToYarpSound(const DecodedAudio& audio)
{
    if (audio.channels == 0 || audio.sampleRate == 0) {
        throw std::runtime_error("Invalid decoded audio metadata");
    }

    if (audio.interleavedPcm16.size() % audio.channels != 0) {
        throw std::runtime_error("Invalid PCM layout");
    }

    const size_t samplesPerChannel = audio.interleavedPcm16.size() / audio.channels;

    yarp::sig::Sound snd(2); // 16-bit
    snd.setFrequency(static_cast<int>(audio.sampleRate));
    snd.resize(samplesPerChannel, audio.channels);

    for (size_t s = 0; s < samplesPerChannel; ++s) {
        for (size_t ch = 0; ch < audio.channels; ++ch) {
            const int16_t sample = audio.interleavedPcm16[s * audio.channels + ch];
            snd.set(sample, s, ch);
        }
    }

    return snd;
}

static profilesList getProfiles(const std::string& baseUrl)
{
    HttpResponse resp = httpGetText(baseUrl + "/profiles");

    if (resp.status < 200 || resp.status >= 300) {
        throw std::runtime_error("Get profiles failed. HTTP " + std::to_string(resp.status) + ": " + resp.body);
    }

    return parseProfiles(resp.body);
}

static bool findProfileByName(const profilesList& profiles, const std::string& name, profileMap& outProfile)
{
    for (const auto& profile : profiles) {
        if (profile.at("name") == name) {
            outProfile = profile;
            return true;
        }
    }
    return false;
}

static std::string startGeneration(
    const std::string& baseUrl,
    const profileMap& profile,
    const std::string& text,
    const std::string& language,
    double speed)
{
    std::string profileId = profile.at("id");
    yCDebug(VOICEBOXSYNTHESIZER) << "Starting generation with profile ID: " << profileId
                               << " text='" << text << "'"
                               << " language='" << language << "'"
                               << " speed=" << speed;
    std::string engine = profile.at("default_engine");
    yCDebug(VOICEBOXSYNTHESIZER) << "Using engine: " << engine;
    json payload = {
        {"profile_id", profileId},
        {"engine", engine},
        {"text", text},
        {"language", language},
        {"speed", speed},
        {"seed", 0},
        {"max_chunk_chars", 800},
        {"crossfade_ms", 50},
        {"normalize", true}
    };

    HttpResponse resp = httpPostJson(baseUrl + "/generate", payload);

    yCInfo(VOICEBOXSYNTHESIZER) << "Generation request sent. HTTP " << resp.status << ": " << resp.body;

    if (resp.status < 200 || resp.status >= 300) {
        throw std::runtime_error("Generate failed. HTTP " + std::to_string(resp.status) + ": " + resp.body);
    }

    json j = json::parse(resp.body);

    if (!j.contains("id") || j.at("id").is_null()) {
        throw std::runtime_error("Generate response has no valid id: " + resp.body);
    }

    std::string id = j.at("id").get<std::string>();
    if (id.empty()) {
        throw std::runtime_error("Generate response has empty id: " + resp.body);
    }

    yCInfo(VOICEBOXSYNTHESIZER) << "Generation started with ID: " << id;

    return id;
}

// static void waitUntilFinished(
//     const std::string& baseUrl,
//     const std::string& generationId,
//     std::chrono::milliseconds pollInterval = std::chrono::milliseconds(1000),
//     std::chrono::seconds timeout = std::chrono::seconds(120))
// {
//     const auto deadline = std::chrono::steady_clock::now() + timeout;

//     while (std::chrono::steady_clock::now() < deadline) {
//         HttpResponse resp = httpGetText(baseUrl + "/history/" + generationId);

//         if (resp.status < 200 || resp.status >= 300) {
//             throw std::runtime_error("History failed. HTTP " + std::to_string(resp.status) + ": " + resp.body);
//         }

//         json j = json::parse(resp.body);

//         // Safely extract status and error, handling null values
//         std::string status;
//         if (j.contains("status") && !j.at("status").is_null()) {
//             status = j.at("status").is_string() ? j.at("status").get<std::string>() : j.at("status").dump();
//         }

//         std::string error;
//         if (j.contains("error") && !j.at("error").is_null()) {
//             error = j.at("error").is_string() ? j.at("error").get<std::string>() : j.at("error").dump();
//         }

//         if (status == "completed" || status == "done" || status == "finished") {
//             yCDebug(VOICEBOXSYNTHESIZER) << "Generation completed with status: " << status;
//             return;
//         }

//         if (status == "error" || status == "failed" || !error.empty()) {
//             throw std::runtime_error("Generation failed. Status=" + status + " error=" + error);
//         }

//         std::this_thread::sleep_for(pollInterval);
//     }

//     throw std::runtime_error("Timeout waiting for Voicebox generation");
// }
static void waitUntilFinished(
    const std::string& baseUrl,
    const std::string& generationId,
    std::chrono::milliseconds pollInterval = std::chrono::milliseconds(1000),
    std::chrono::seconds timeout = std::chrono::seconds(120))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        HttpResponse resp = httpGetText(baseUrl + "/history/" + generationId);

        if (resp.status < 200 || resp.status >= 300) {
            throw std::runtime_error("History failed. HTTP " + std::to_string(resp.status) + ": " + resp.body);
        }

        json j = json::parse(resp.body);

        std::string status;
        if (j.contains("status") && !j.at("status").is_null()) {
            status = j.at("status").is_string() ? j.at("status").get<std::string>() : j.at("status").dump();
        }

        std::string error;
        if (j.contains("error") && !j.at("error").is_null()) {
            error = j.at("error").is_string() ? j.at("error").get<std::string>() : j.at("error").dump();
        }

        std::string audioPath;
        if (j.contains("audio_path") && !j.at("audio_path").is_null()) {
            audioPath = j.at("audio_path").is_string() ? j.at("audio_path").get<std::string>() : j.at("audio_path").dump();
        }

        double duration = 0.0;
        if (j.contains("duration") && !j.at("duration").is_null()) {
            duration = j.at("duration").get<double>();
        }

        yCDebug(VOICEBOXSYNTHESIZER)
            << "Generation status=" << status
            << " audio_path='" << audioPath
            << "' duration=" << duration
            << " error='" << error << "'";

        if (status == "error" || status == "failed" || !error.empty()) {
            throw std::runtime_error("Generation failed. Status=" + status + " error=" + error);
        }

        // Considera pronto solo se c'è davvero un file audio associato
        if ((status == "completed" || status == "done" || status == "finished") &&
            !audioPath.empty() &&
            audioPath != ".") {
            return;
        }

        std::this_thread::sleep_for(pollInterval);
    }

    throw std::runtime_error("Timeout waiting for Voicebox generation");
}

// static yarp::sig::Sound downloadAudioAsYarpSound(
//     const std::string& baseUrl,
//     const std::string& generationId)
// {
//     yCInfo(VOICEBOXSYNTHESIZER) << "Downloading audio for generation ID: " << generationId;

//     HttpBytesResponse resp = httpGetBytes(baseUrl + "/audio/" + generationId);

//     if (resp.status < 200 || resp.status >= 300) {
//         std::string errorBody;
//         if (!resp.body.empty()) {
//             errorBody = std::string(resp.body.begin(), resp.body.end());
//         }
//         yCError(VOICEBOXSYNTHESIZER) << "Audio download failed. HTTP " << resp.status << " received " << resp.body.size() << " bytes. Response: " << errorBody;
//         throw std::runtime_error("Audio download failed. HTTP " + std::to_string(resp.status) + ": " + errorBody);
//     }

//     if (resp.body.empty()) {
//         throw std::runtime_error("Audio download returned empty response");
//     }

//     yCInfo(VOICEBOXSYNTHESIZER) << "Downloaded " << resp.body.size() << " bytes";

//     DecodedAudio audio = decodeWav16FromMemory(resp.body);
//     return decodedAudioToYarpSound(audio);
// }
static yarp::sig::Sound downloadAudioAsYarpSound(
    const std::string& baseUrl,
    const std::string& generationId)
{
    HttpResponse hist = httpGetText(baseUrl + "/history/" + generationId);
    yCInfo(VOICEBOXSYNTHESIZER) << "History before audio download: " << hist.body;

    yCInfo(VOICEBOXSYNTHESIZER) << "Downloading audio for generation ID: " << generationId;
    HttpBytesResponse resp = httpGetBytes(baseUrl + "/audio/" + generationId);

    if (resp.status < 200 || resp.status >= 300) {
        std::string errorBody;
        if (!resp.body.empty()) {
            errorBody = std::string(resp.body.begin(), resp.body.end());
        }
        yCError(VOICEBOXSYNTHESIZER) << "Audio download failed. HTTP " << resp.status
                                     << " received " << resp.body.size()
                                     << " bytes. Response: " << errorBody;
        throw std::runtime_error("Audio download failed. HTTP " + std::to_string(resp.status) + ": " + errorBody);
    }

    if (resp.body.empty()) {
        throw std::runtime_error("Audio download returned empty response");
    }

    DecodedAudio audio = decodeWav16FromMemory(resp.body);
    return decodedAudioToYarpSound(audio);
}

static yarp::sig::Sound synthesizeVoiceboxToYarpSound(
    const std::string& baseUrl,
    const profileMap& profile,
    const std::string& text,
    const std::string& language,
    double speed)
{
    const std::string id = startGeneration(baseUrl, profile, text, language, speed);
    yCInfo(VOICEBOXSYNTHESIZER) << "Waiting for generation to complete...";
    waitUntilFinished(baseUrl, id);
    yCInfo(VOICEBOXSYNTHESIZER) << "Generation completed. Downloading audio...";
    return downloadAudioAsYarpSound(baseUrl, id);
}

VoiceBoxSynthesizer::VoiceBoxSynthesizer()
{
}

bool VoiceBoxSynthesizer::open(yarp::os::Searchable& config)
{
    if (!parseParams(config)) {
        yCError(VOICEBOXSYNTHESIZER) << "Failed to parse parameters";
        return false;
    }

    m_baseUrl = "http://" + m_base_ip + ":" + m_base_port;

    if(!findProfileByName(getProfiles(m_baseUrl), m_voice, m_profile)) {
        yCError(VOICEBOXSYNTHESIZER) << "Voice profile not found: " << m_voice;
        return false;
    }


    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        yCError(VOICEBOXSYNTHESIZER) << "curl_global_init failed";
        return false;
    }

    yCInfo(VOICEBOXSYNTHESIZER) << "Open";
    return true;
}

bool VoiceBoxSynthesizer::close()
{
    curl_global_cleanup();
    yCInfo(VOICEBOXSYNTHESIZER) << "Close";
    return true;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::setLanguage(const std::string& language)
{
    if (language.empty()) {
        return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
    }
    m_language = language;
    return ReturnValue_ok;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::getLanguage(std::string& language)
{
    language = m_language;
    return ReturnValue_ok;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::setVoice(const std::string& voice_name)
{
    if (voice_name.empty()) {
        return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
    }

    try {
        profileMap profile;
        if(!findProfileByName(getProfiles(m_baseUrl), voice_name, profile)) {
            yCError(VOICEBOXSYNTHESIZER) << "Voice profile not found: " << voice_name;
            return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
        }

        // Verify profile has required fields
        if (profile.find("id") == profile.end() || profile["id"].empty()) {
            yCError(VOICEBOXSYNTHESIZER) << "Profile missing required 'id' field: " << voice_name;
            return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
        }

        m_voice = voice_name;
        m_profile = profile;
        return ReturnValue_ok;
    } catch (const std::exception& ex) {
        yCError(VOICEBOXSYNTHESIZER) << "setVoice failed: " << ex.what();
        return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
    }
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::getVoice(std::string& voice_name)
{
    voice_name = m_voice;
    return ReturnValue_ok;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::setSpeed(const double speed)
{
    if (speed <= 0.0) {
        return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
    }
    m_speed = speed;
    return ReturnValue_ok;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::getSpeed(double& speed)
{
    speed = m_speed;
    return ReturnValue_ok;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::setPitch(const double pitch)
{
    if (pitch <= 0.0) {
        return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
    }
    m_pitch = pitch;
    return ReturnValue_ok;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::getPitch(double& pitch)
{
    pitch = m_pitch;
    return ReturnValue_ok;
}

yarp::dev::ReturnValue VoiceBoxSynthesizer::synthesize(const std::string& text, yarp::sig::Sound& sound)
{
    if (text.empty()) {
        return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
    }

    try {
        sound = synthesizeVoiceboxToYarpSound(m_baseUrl, m_profile, text, m_language, m_speed);
        return ReturnValue_ok;
    } catch (const std::exception& ex) {
        yCError(VOICEBOXSYNTHESIZER) << "synthesize failed: " << ex.what();
        return yarp::dev::ReturnValue(yarp::dev::ReturnValue::return_code::return_value_error_method_failed);
    }
}

