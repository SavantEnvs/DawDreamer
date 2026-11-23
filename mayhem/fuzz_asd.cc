// Fuzz DawDreamer's Ableton .asd warp-file parser through the same entry point
// PlaybackWarpProcessor::setClipFile() uses: AbletonClipInfo::readWarpFile(path),
// followed by the beat->sample mapping processBlock() performs with the parsed
// loop/start/end markers and warp markers (the area of upstream fix 53f853c).
//
// readWarpFile() takes a path (it is fopen-based), so each input is written to a
// fresh mkstemp() file under $TMPDIR. No state survives between inputs.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "AbletonClipInfo.h"

static void exercise(AbletonClipInfo& info)
{
    const double sr = 44100.0;
    // Each result is stored on its own (no harness-side arithmetic on the returned
    // ints), so any overflow reported here is inside beat_to_sample itself.
    volatile int sink = 0;
    // PlaybackWarpProcessor::processBlock with warp_on maps the clip's start
    // marker (plus a per-clip offset) and the loop bounds through the warp markers.
    if (info.warp_on)
    {
        sink = info.beat_to_sample(info.start_marker, sr);
        sink = info.beat_to_sample(info.loop_start, sr);
        sink = info.beat_to_sample(info.loop_end, sr);
        sink = info.beat_to_sample(info.end_marker, sr);
    }
    double seconds = 0, bpm = 0;
    info.beat_to_seconds(info.hidden_loop_end, seconds, bpm);
    bool loop_on = info.loop_on;
    sink = loop_on ? 1 : 0;
    (void)sink;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const char* tmpdir = getenv("TMPDIR");
    std::string tmpl = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/fuzz_asd_XXXXXX";
    int fd = mkstemp(&tmpl[0]);
    if (fd < 0)
        return 0;
    size_t off = 0;
    while (off < size)
    {
        ssize_t n = write(fd, data + off, size - off);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    close(fd);

    AbletonClipInfo info;
    try
    {
        if (info.readWarpFile(tmpl.c_str()))
            exercise(info);
    }
    catch (const std::runtime_error&)
    {
    }
    unlink(tmpl.c_str());
    return 0;
}
