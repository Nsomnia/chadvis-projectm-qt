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
#include <vector>
#include <string>
#include <memory>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"

namespace vc::suno {

class SunoLibraryManager : public QObject {
    Q_OBJECT

public:
    explicit SunoLibraryManager(SunoClient* client, SunoDatabase& db, QObject* parent = nullptr);
    ~SunoLibraryManager() override;

    void refreshLibrary(int page = 1);
    void syncDatabase(bool forceAuth);
    
    // Accessors
    const std::vector<SunoClip>& accumulatedClips() const { return accumulatedClips_; }
    void clearAccumulatedClips() { accumulatedClips_.clear(); }

    // Signals
    vc::Signal<std::string> statusMessage;
    vc::Signal<std::vector<SunoClip>> libraryUpdated;
    vc::Signal<std::string> clipUpdated;
    vc::Signal<> authenticationRequired;

private:
    SunoClient* client_;
    SunoDatabase& db_;
    
    std::vector<SunoClip> accumulatedClips_;
    bool isSyncing_ = false;
    int currentSyncPage_ = 1;

    void onLibraryFetched(const std::vector<SunoClip>& clips);
};

} // namespace vc::suno
