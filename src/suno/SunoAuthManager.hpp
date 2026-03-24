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
#include <memory>
#include <string>

#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/SunoClient.hpp"
#include "ui/SunoPersistentAuth.hpp"
#include "ui/SystemBrowserAuth.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"

namespace vc::suno {

class SunoAuthManager : public QObject {
    Q_OBJECT

public:
    explicit SunoAuthManager(SunoClient* client, QObject* parent = nullptr);
    ~SunoAuthManager() override;

    void initialize();
    void requestAuthentication();
    void startSystemBrowserAuth();
    
    // Signals
    vc::Signal<std::string> statusMessage;
    vc::Signal<> authenticationRequired;

private:
    SunoClient* client_;
    std::unique_ptr<chadvis::SunoPersistentAuth> persistentAuth_;
    std::unique_ptr<chadvis::SystemBrowserAuth> systemAuth_;
    bool isRefreshingToken_ = false;

    void onPersistentAuthRestored(const chadvis::SunoAuthState& authState);
    void onSystemAuthSuccess(const QString& token);
    void onSystemAuthFailed(const QString& reason);
};

} // namespace vc::suno
