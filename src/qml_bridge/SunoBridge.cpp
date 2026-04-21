#include "SunoBridge.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoClient.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::suno::SunoController* SunoBridge::s_controller = nullptr;
vc::suno::SunoClient* SunoBridge::s_client = nullptr;
SunoBridge* SunoBridge::s_instance = nullptr;

SunoBridge::SunoBridge(QObject* parent) : QObject(parent) {
    s_instance = this;
}

QObject* SunoBridge::create(QQmlEngine*, QJSEngine*) {
    return new SunoBridge();
}

void SunoBridge::setSunoController(vc::suno::SunoController* controller) {
    s_controller = controller;
    if (s_controller) s_client = s_controller->client();
}

bool SunoBridge::loading() const { return false; }
QVariantList SunoBridge::clips() const { return QVariantList(); }

void SunoBridge::generate(const QString&, const QString&, bool, const QString&) {
    if (s_client) emit generationStarted();
}

void SunoBridge::fetchLibrary() {}

} // namespace qml_bridge
