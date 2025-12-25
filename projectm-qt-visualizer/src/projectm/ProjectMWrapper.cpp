/**
 * @file ProjectMWrapper.cpp
 * @brief Implementation of projectM v4 C API wrapper.
 */
#include "ProjectMWrapper.hpp"
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
ProjectMWrapper::ProjectMWrapper()
: m_silenceBuffer(2048, 0.0f) // Pre-allocate silence buffer
{
}
ProjectMWrapper::~ProjectMWrapper()
{
destroy();
}
bool ProjectMWrapper::initialize()
{
if (m_handle) {
qWarning() << "ProjectMWrapper::initialize() called on already initialized instance";
return true;
}
// Create projectM instance
m_handle = projectm_create();
if (!m_handle) {
qCritical() << "Failed to create projectM instance!";
return false;
}
qDebug() << "projectM instance created successfully";
// Configure settings
projectm_set_window_size(m_handle, m_width, m_height);
// Try to find preset path
// Common locations on Arch Linux:
// /usr/share/projectM/presets
// ~/.projectM/presets
QString presetPath = "/usr/share/projectM/presets";
if (QDir(presetPath).exists()) {
qDebug() << "Using preset path:" << presetPath;
projectm_set_preset_path(m_handle, presetPath.toUtf8().constData());
} else {
qWarning() << "Default preset path not found, visualization may be limited";
}
qDebug() << "projectM initialized - Neck-beard visualization ready!";
return true;
}
void ProjectMWrapper::destroy()
{
if (m_handle) {
projectm_destroy(m_handle);
m_handle = nullptr;
qDebug() << "projectM destroyed";
}
}
void ProjectMWrapper::resize(int width, int height)
{
m_width = width;
m_height = height;
if (m_handle) {
projectm_set_window_size(m_handle, width, height);
qDebug() << "projectM resized to" << width << "x" << height;
}
}
void ProjectMWrapper::renderFrame()
{
if (!m_handle) {
return;
}
// projectM handles glClear internally, but we should ensure proper state
projectm_opengl_render_frame(m_handle);
}
void ProjectMWrapper::addPCMData(const float* data, unsigned int samples)
{
if (!m_handle || !data || samples == 0) {
return;
}
projectm_pcm_add_float(m_handle, data, samples, PROJECTM_STEREO);
}
void ProjectMWrapper::feedSilence()
{
if (!m_handle) {
return;
}
// Feed pre-allocated silence buffer
// This keeps the visualization alive in "idle" mode
projectm_pcm_add_float(m_handle, m_silenceBuffer.data(),
m_silenceBuffer.size() / 2, PROJECTM_STEREO);
}
bool ProjectMWrapper::loadPreset(const std::string& path)
{
if (!m_handle) {
return false;
}
projectm_load_preset_file(m_handle, path.c_str(), true);
qDebug() << "Loaded preset:" << QString::fromStdString(path);
return true;
}
void ProjectMWrapper::nextPreset()
{
if (m_handle) {
projectm_playlist_play_next(m_handle, true);
}
}
void ProjectMWrapper::previousPreset()
{
if (m_handle) {
projectm_playlist_play_previous(m_handle, true);
}
}
void ProjectMWrapper::randomPreset()
{
if (m_handle) {
// Note: May need playlist configured first
projectm_playlist_set_shuffle(m_handle, true);
projectm_playlist_play_next(m_handle, true);
}
}
