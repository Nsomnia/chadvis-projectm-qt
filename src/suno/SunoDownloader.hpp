/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <string>
#include <memory>
#include <filesystem>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "audio/AudioEngine.hpp"
#include "util/Result.hpp"

namespace fs = std::filesystem;

namespace vc::suno {

class SunoDownloader : public QObject {
    Q_OBJECT

public:
    explicit SunoDownloader(SunoClient* client, 
                           SunoDatabase& db, 
                           AudioEngine* audioEngine,
                           QNetworkAccessManager* networkManager,
                           QObject* parent = nullptr);
    ~SunoDownloader() override;

    void downloadAndPlay(const SunoClip& clip);
    void saveLyricsSidecar(const std::string& clipId, 
                          const std::string& json,
                          const QJsonDocument& doc,
                          const std::vector<SunoClip>& clips);
    void saveMetadataSidecar(const SunoClip& clip);

private:
    SunoClient* client_;
    SunoDatabase& db_;
    AudioEngine* audioEngine_;
    QNetworkAccessManager* networkManager_;

    void downloadAudio(const SunoClip& clip);
    void downloadAudioFromUrl(const std::string& clipId,
                             const std::string& url,
                             const std::string& extension);
    void onWavConversionReady(const std::string& clipId, const std::string& wavUrl);
    void processDownloadedFile(const SunoClip& clip, const fs::path& path);
};

} // namespace vc::suno
