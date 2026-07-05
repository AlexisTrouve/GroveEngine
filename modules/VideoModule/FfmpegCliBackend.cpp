#include "FfmpegCliBackend.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <system_error>

#ifdef _WIN32
  #define GROVE_POPEN  _popen
  #define GROVE_PCLOSE _pclose
#else
  #define GROVE_POPEN  popen
  #define GROVE_PCLOSE pclose
#endif

namespace grove {
namespace video {

namespace {

// Read EXACTLY n bytes from a pipe (which delivers in chunks); returns bytes read (< n only at EOF).
size_t readFull(std::FILE* f, uint8_t* dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        const size_t r = std::fread(dst + got, 1, n - got, f);
        if (r == 0) break;   // EOF / error
        got += r;
    }
    return got;
}

// Run a command and capture its stdout as text (for ffprobe key=value output).
std::string capture(const std::string& cmd) {
    std::string out;
    std::FILE* p = GROVE_POPEN(cmd.c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    GROVE_PCLOSE(p);
    return out;
}

// The value after "key=" up to end-of-line, from ffprobe's default (key=value) output.
std::string kv(const std::string& s, const std::string& key) {
    const std::string tag = key + "=";
    auto pos = s.find(tag);
    if (pos == std::string::npos) return {};
    pos += tag.size();
    const auto end = s.find_first_of("\r\n", pos);
    return s.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

// Derive the ffprobe exe from the ffmpeg exe (sibling tool): replace the last "ffmpeg" with "ffprobe".
std::string deriveFfprobe(const std::string& ffmpeg) {
    const auto pos = ffmpeg.rfind("ffmpeg");
    if (pos == std::string::npos) return "ffprobe";
    std::string s = ffmpeg;
    s.replace(pos, 6, "ffprobe");
    return s;
}

} // namespace

FfmpegCliBackend::FfmpegCliBackend(std::string ffmpegExe)
    : m_ffmpeg(std::move(ffmpegExe)), m_ffprobe(deriveFfprobe(m_ffmpeg)) {}

FfmpegCliBackend::~FfmpegCliBackend() { close(); }

bool FfmpegCliBackend::probe(const std::string& path) {
    // ffprobe emits robust key=value metadata (no fragile stderr banner parsing).
    const std::string cmd = m_ffprobe +
        " -v error -select_streams v:0"
        " -show_entries stream=width,height,r_frame_rate,nb_frames"
        " -show_entries format=duration -of default=noprint_wrappers=1 \"" + path + "\"";
    const std::string out = capture(cmd);
    if (out.empty()) return false;

    m_width  = std::atoi(kv(out, "width").c_str());
    m_height = std::atoi(kv(out, "height").c_str());

    // r_frame_rate is a rational "num/den" (e.g. "30000/1001").
    const std::string rate = kv(out, "r_frame_rate");
    const auto slash = rate.find('/');
    const double num = std::atof(rate.substr(0, slash).c_str());
    const double den = (slash != std::string::npos) ? std::atof(rate.substr(slash + 1).c_str()) : 1.0;
    m_fps = (den > 0.0) ? (num / den) : 0.0;

    m_durationSec = std::atof(kv(out, "duration").c_str());
    const int nbFrames = std::atoi(kv(out, "nb_frames").c_str());   // 0 when the container doesn't store it
    m_frameCount = (nbFrames > 0) ? nbFrames
                 : (m_durationSec > 0.0 && m_fps > 0.0) ? static_cast<int>(std::lround(m_durationSec * m_fps))
                 : -1;

    return m_width > 0 && m_height > 0 && m_fps > 0.0;
}

bool FfmpegCliBackend::open(const std::string& path) {
    close();
    if (!probe(path)) return false;

    // Extract the audio track to a temp OGG — the sound system plays it as the A/V master clock.
    // Optional: a silent clip (no audio stream) simply leaves m_audioPath empty.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::string name = "grovevideo_" + std::to_string(std::hash<std::string>{}(path)) + ".ogg";
        const std::string tmp = (fs::temp_directory_path(ec) / name).string();
        const std::string acmd = m_ffmpeg + " -y -v error -i \"" + path + "\" -vn -c:a libvorbis \"" + tmp + "\"";
        if (std::system(acmd.c_str()) == 0) {
            const auto sz = fs::file_size(tmp, ec);
            if (!ec && sz > 0) m_audioPath = tmp;
            else               fs::remove(tmp, ec);
        }
    }

    // Open the long-running raw-RGBA frame pipe (binary read).
    const std::string vcmd = m_ffmpeg + " -v error -i \"" + path + "\" -f rawvideo -pix_fmt rgba -";
    m_pipe = GROVE_POPEN(vcmd.c_str(), "rb");
    m_currentIndex = -1;
    m_frame.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u, 0);
    return m_pipe != nullptr;
}

const uint8_t* FfmpegCliBackend::frameRGBA(int index) {
    if (!m_pipe || m_width <= 0 || m_height <= 0) return nullptr;
    const size_t frameSize = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u;

    // Forward-only: a request for a past/current frame returns the last-decoded one (no rewind).
    if (index <= m_currentIndex) return m_frame.empty() ? nullptr : m_frame.data();

    // Decode forward, discarding frames the sync skipped (dropped), until we hold `index`.
    while (m_currentIndex < index) {
        if (readFull(m_pipe, m_frame.data(), frameSize) < frameSize) return nullptr;   // EOF / short read
        ++m_currentIndex;
    }
    return m_frame.data();
}

void FfmpegCliBackend::close() {
    if (m_pipe) { GROVE_PCLOSE(m_pipe); m_pipe = nullptr; }
    if (!m_audioPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_audioPath, ec);
        m_audioPath.clear();
    }
    m_currentIndex = -1;
}

} // namespace video
} // namespace grove
