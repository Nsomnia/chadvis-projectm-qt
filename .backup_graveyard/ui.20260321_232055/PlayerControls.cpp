#include "PlayerControls.hpp"
#include "core/Logger.hpp"

#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

namespace vc {

PlayerControls::PlayerControls(QWidget* parent) : QWidget(parent) {
    setupUI();
    applyModernStyling();
}

void PlayerControls::setAudioEngine(AudioEngine* engine) {
    audioEngine_ = engine;

    if (engine) {
        engine->stateChanged.connect([this](PlaybackState s) {
            QMetaObject::invokeMethod(this,
                                      [this, s] { updatePlaybackState(s); });
        });

        engine->positionChanged.connect([this](Duration pos) {
            QMetaObject::invokeMethod(this,
                                      [this, pos] { updatePosition(pos); });
        });

        engine->durationChanged.connect([this](Duration dur) {
            QMetaObject::invokeMethod(this,
                                      [this, dur] { updateDuration(dur); });
        });

        engine->trackChanged.connect([this] {
            QMetaObject::invokeMethod(this, [this] {
                if (audioEngine_) {
                    if (const auto* item =
                                audioEngine_->playlist().currentItem()) {
                        updateTrackInfo(item->metadata);
                    }
                }
            });
        });
    }
}

void PlayerControls::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Track info row
    auto* infoLayout = new QHBoxLayout();

    albumArtLabel_ = new QLabel();
    albumArtLabel_->setFixedSize(64, 64);
    albumArtLabel_->setStyleSheet(
            "background-color: #2d2d2d; border-radius: 4px;");
    albumArtLabel_->setAlignment(Qt::AlignCenter);
    albumArtLabel_->setText("♪");
    infoLayout->addWidget(albumArtLabel_);

    auto* textLayout = new QVBoxLayout();
    titleLabel_ = new QLabel("No track loaded");
    titleLabel_->setObjectName("titleLabel");
    artistLabel_ = new QLabel("");
    artistLabel_->setObjectName("artistLabel");
    textLayout->addWidget(titleLabel_);
    textLayout->addWidget(artistLabel_);
    textLayout->addStretch();
    infoLayout->addLayout(textLayout, 1);

    mainLayout->addLayout(infoLayout);

    // Seek bar row with modern cyan slider
    auto* seekLayout = new QHBoxLayout();

    currentTimeLabel_ = new QLabel("00:00");
    currentTimeLabel_->setFixedWidth(50);
    seekLayout->addWidget(currentTimeLabel_);

    seekSlider_ = new chadvis::CyanSlider();
    seekSlider_->setRange(0, 1000);
    seekSlider_->setValue(0);
    connect(seekSlider_,
            &chadvis::CyanSlider::sliderPressed,
            this,
            &PlayerControls::onSeekSliderPressed);
    connect(seekSlider_,
            &chadvis::CyanSlider::sliderReleased,
            this,
            &PlayerControls::onSeekSliderReleased);
    connect(seekSlider_,
            &chadvis::CyanSlider::sliderMoved,
            this,
            &PlayerControls::onSeekSliderMoved);
    seekLayout->addWidget(seekSlider_, 1);

    totalTimeLabel_ = new QLabel("00:00");
    totalTimeLabel_->setFixedWidth(50);
    totalTimeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    seekLayout->addWidget(totalTimeLabel_);

    mainLayout->addLayout(seekLayout);

    // Controls row with modern glow buttons
    auto* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(6);

    shuffleButton_ = new chadvis::GlowButton("🔀");
    shuffleButton_->setFixedSize(36, 36);
    shuffleButton_->setCheckable(true);
    shuffleButton_->setToolTip("Shuffle");
    connect(shuffleButton_,
            &chadvis::GlowButton::clicked,
            this,
            &PlayerControls::onShuffleClicked);
    controlsLayout->addWidget(shuffleButton_);

    prevButton_ = new chadvis::GlowButton("⏮");
    prevButton_->setFixedSize(36, 36);
    prevButton_->setToolTip("Previous");
    connect(prevButton_,
            &chadvis::GlowButton::clicked,
            this,
            &PlayerControls::previousClicked);
    controlsLayout->addWidget(prevButton_);

    playPauseButton_ = new chadvis::GlowButton("▶");
    playPauseButton_->setObjectName("playButton");
    playPauseButton_->setFixedSize(48, 48);
    playPauseButton_->setCheckable(true);
    playPauseButton_->setToolTip("Play/Pause");
    connect(playPauseButton_,
            &chadvis::GlowButton::clicked,
            this,
            &PlayerControls::onPlayPauseClicked);
    controlsLayout->addWidget(playPauseButton_);

    stopButton_ = new chadvis::GlowButton("⏹");
    stopButton_->setFixedSize(36, 36);
    stopButton_->setToolTip("Stop");
    connect(stopButton_,
            &chadvis::GlowButton::clicked,
            this,
            &PlayerControls::stopClicked);
    controlsLayout->addWidget(stopButton_);

    nextButton_ = new chadvis::GlowButton("⏭");
    nextButton_->setFixedSize(36, 36);
    nextButton_->setToolTip("Next");
    connect(nextButton_,
            &chadvis::GlowButton::clicked,
            this,
            &PlayerControls::nextClicked);
    controlsLayout->addWidget(nextButton_);

    repeatButton_ = new chadvis::GlowButton("🔁");
    repeatButton_->setFixedSize(36, 36);
    repeatButton_->setCheckable(true);
    repeatButton_->setToolTip("Repeat");
    connect(repeatButton_,
            &chadvis::GlowButton::clicked,
            this,
            &PlayerControls::onRepeatClicked);
    controlsLayout->addWidget(repeatButton_);

    controlsLayout->addStretch();

    // Volume with modern glow button and cyan slider
    muteButton_ = new chadvis::GlowButton("🔊");
    muteButton_->setFixedSize(36, 36);
    muteButton_->setCheckable(true);
    muteButton_->setToolTip("Mute");
    connect(muteButton_, &chadvis::GlowButton::clicked, this, [this](bool checked) {
        if (checked) {
            lastVolume_ = volumeSlider_->value() / 100.0f;
            volumeSlider_->setValue(0);
            muteButton_->setText("🔇");
        } else {
            volumeSlider_->setValue(static_cast<int>(lastVolume_ * 100));
            muteButton_->setText("🔊");
        }
    });
    controlsLayout->addWidget(muteButton_);

    volumeSlider_ = new chadvis::CyanSlider();
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(100);
    volumeSlider_->setFixedWidth(100);
    volumeSlider_->setToolTip("Volume");
    connect(volumeSlider_,
            &chadvis::CyanSlider::valueChanged,
            this,
            &PlayerControls::onVolumeSliderChanged);
    controlsLayout->addWidget(volumeSlider_);

    mainLayout->addLayout(controlsLayout);
}

void PlayerControls::applyModernStyling() {
    // Set accent color for sliders
    seekSlider_->setAccentColor(QColor("#00bcd4"));
    volumeSlider_->setAccentColor(QColor("#00bcd4"));
    
    // Configure glow buttons with cyan accent
    auto configureGlowButton = [](chadvis::GlowButton* btn, const QColor& glow) {
        btn->setGlowColor(glow);
        btn->setCornerRadius(8);
        btn->setBackgroundOpacity(0.8);
    };
    
    QColor cyanAccent("#00bcd4");
    QColor greenAccent("#00ff88");
    
    configureGlowButton(shuffleButton_, cyanAccent);
    configureGlowButton(prevButton_, cyanAccent);
    configureGlowButton(playPauseButton_, greenAccent);
    configureGlowButton(stopButton_, cyanAccent);
    configureGlowButton(nextButton_, cyanAccent);
    configureGlowButton(repeatButton_, cyanAccent);
    configureGlowButton(muteButton_, cyanAccent);
}

void PlayerControls::updatePlaybackState(PlaybackState state) {
    currentState_ = state;

    switch (state) {
    case PlaybackState::Playing:
        playPauseButton_->setText("⏸");
        playPauseButton_->setChecked(true);
        break;
    case PlaybackState::Paused:
    case PlaybackState::Stopped:
        playPauseButton_->setText("▶");
        playPauseButton_->setChecked(false);
        break;
    }
}

void PlayerControls::updatePosition(Duration position) {
    currentPosition_ = position;
    currentTimeLabel_->setText(formatTime(position));

    if (!seeking_) {
        updateSeekSlider();
    }
}

void PlayerControls::updateDuration(Duration duration) {
    currentDuration_ = duration;
    totalTimeLabel_->setText(formatTime(duration));
    updateSeekSlider();
}

void PlayerControls::updateTrackInfo(const MediaMetadata& meta) {
    titleLabel_->setText(QString::fromStdString(meta.displayTitle()));
    artistLabel_->setText(QString::fromStdString(meta.displayArtist()));

    if (meta.albumArt) {
        albumArtLabel_->setPixmap(meta.albumArt->scaled(
                64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        albumArtLabel_->setText("♪");
    }
}

void PlayerControls::setControlsEnabled(bool enabled) {
    playPauseButton_->setEnabled(enabled);
    stopButton_->setEnabled(enabled);
    prevButton_->setEnabled(enabled);
    nextButton_->setEnabled(enabled);
    seekSlider_->setEnabled(enabled);
}

void PlayerControls::onPlayPauseClicked() {
    if (currentState_ == PlaybackState::Playing) {
        emit pauseClicked();
    } else {
        emit playClicked();
    }
}

void PlayerControls::onSeekSliderPressed() {
    seeking_ = true;
}

void PlayerControls::onSeekSliderReleased() {
    seeking_ = false;

    if (currentDuration_.count() > 0) {
        f32 ratio = seekSlider_->value() / 1000.0f;
        Duration position(static_cast<i64>(ratio * currentDuration_.count()));
        emit seekRequested(position);
    }
}

void PlayerControls::onSeekSliderMoved(int value) {
    if (currentDuration_.count() > 0) {
        f32 ratio = value / 1000.0f;
        Duration position(static_cast<i64>(ratio * currentDuration_.count()));
        currentTimeLabel_->setText(formatTime(position));
    }
}

void PlayerControls::onVolumeSliderChanged(int value) {
    f32 volume = value / 100.0f;
    emit volumeChanged(volume);

    if (value > 0) {
        muteButton_->setChecked(false);
        muteButton_->setText(value > 50 ? "🔊" : "🔉");
    } else {
        muteButton_->setText("🔇");
    }
}

void PlayerControls::onShuffleClicked() {
    shuffle_ = shuffleButton_->isChecked();
    emit shuffleToggled(shuffle_);
}

void PlayerControls::onRepeatClicked() {
    // Cycle through repeat modes
    switch (repeatMode_) {
    case RepeatMode::Off:
        repeatMode_ = RepeatMode::All;
        repeatButton_->setText("🔁");
        repeatButton_->setChecked(true);
        break;
    case RepeatMode::All:
        repeatMode_ = RepeatMode::One;
        repeatButton_->setText("🔂");
        break;
    case RepeatMode::One:
        repeatMode_ = RepeatMode::Off;
        repeatButton_->setText("🔁");
        repeatButton_->setChecked(false);
        break;
    }
    emit repeatToggled(repeatMode_);
}

void PlayerControls::updateSeekSlider() {
    if (currentDuration_.count() > 0) {
        int value = static_cast<int>(1000.0 * currentPosition_.count() /
                                     currentDuration_.count());
        seekSlider_->setValue(value);
    } else {
        seekSlider_->setValue(0);
    }
}

QString PlayerControls::formatTime(Duration dur) {
    auto totalSecs = dur.count() / 1000;
    auto mins = totalSecs / 60;
    auto secs = totalSecs % 60;
    return QString("%1:%2")
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
}

} // namespace vc
