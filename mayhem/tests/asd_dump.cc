// Known-answer tool for mayhem/test.sh: parse an Ableton .asd file with
// AbletonClipInfo::readWarpFile() and print every parsed field, plus the
// beat->seconds/sample mapping of a few beats. test.sh diffs this output
// against committed golden files.
#include <cstdio>
#include <stdexcept>

#include "AbletonClipInfo.h"

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s file.asd\n", argv[0]);
        return 2;
    }
    AbletonClipInfo info;
    try
    {
        info.readWarpFile(argv[1]);
    }
    catch (const std::runtime_error& e)
    {
        printf("error: %s\n", e.what());
        return 1;
    }
    printf("loop_start=%.17g\nloop_end=%.17g\nstart_marker=%.17g\n", info.loop_start,
           info.loop_end, info.start_marker);
    printf("hidden_loop_start=%.17g\nhidden_loop_end=%.17g\nend_marker=%.17g\n",
           info.hidden_loop_start, info.hidden_loop_end, info.end_marker);
    printf("loop_on=%d\nwarp_on=%d\nwarp_markers=%zu\n", info.loop_on ? 1 : 0,
           info.warp_on ? 1 : 0, info.warp_markers.size());
    for (size_t i = 0; i < info.warp_markers.size() && i < 8; i++)
        printf("marker[%zu]=%.17g,%.17g\n", i, info.warp_markers[i].first,
               info.warp_markers[i].second);
    for (double beat : {0.0, 1.0, 4.0, 16.0})
    {
        double s = 0, bpm = 0;
        info.beat_to_seconds(beat, s, bpm);
        printf("beat %.1f -> %.9f s @ %.6f bpm, sample %d\n", beat, s, bpm,
               info.beat_to_sample(beat, 44100.0));
    }
    return 0;
}
