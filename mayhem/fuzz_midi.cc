// Fuzz DawDreamer's Standard MIDI File loading: the body of SamplerProcessor::loadMidi()
// (Source/SamplerProcessor.h; FaustProcessor::loadMidi and PluginProcessor::loadMidi are the
// same code) on top of JUCE's juce::MidiFile parser, built from the pinned JUCE submodule
// commit with the fuzz flags. The processor class itself cannot be instantiated without the
// whole Python extension (nanobind ndarrays, the JUCE plugin host), so the method body is
// reproduced verbatim below. mayhem/build.sh fails the build if it drifts from upstream
// (it compares the two loadMidi bodies with comments and whitespace stripped).
//
// Input layout: byte 0 picks the load_midi(beats=, all_events=) flags, the rest is the .mid
// file, written to a fresh mkstemp() file under $TMPDIR (loadMidi takes a path).
// After loading, the buffers are walked the way processBlock() does and re-serialised the way
// save_midi() does.
#include <JuceHeader.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>

using namespace juce;

namespace
{
struct MidiLoader
{
    const static std::uint32_t PPQN = 3840; // ProcessorBase::PPQN
    double mySampleRate = 44100.0;
    MidiBuffer myMidiBufferSec;
    MidiBuffer myMidiBufferQN;

    // == SamplerProcessor::loadMidi (Source/SamplerProcessor.h) ==
    bool loadMidi(const std::string& path, bool clearPrevious, bool isBeats, bool allEvents)
    {
        if (!std::filesystem::exists(path.c_str()))
        {
            throw std::runtime_error("File not found: " + path);
        }

        File file = File(path);
        FileInputStream fileStream(file);
        MidiFile midiFile;
        midiFile.readFrom(fileStream);

        if (clearPrevious)
        {
            myMidiBufferSec.clear();
            myMidiBufferQN.clear();
        }

        if (!isBeats)
        {
            midiFile.convertTimestampTicksToSeconds();

            for (int t = 0; t < midiFile.getNumTracks(); t++)
            {
                const MidiMessageSequence* track = midiFile.getTrack(t);
                for (int i = 0; i < track->getNumEvents(); i++)
                {
                    MidiMessage& m = track->getEventPointer(i)->message;
                    int sampleOffset = (int)(mySampleRate * m.getTimeStamp());
                    if (allEvents || m.isNoteOff() || m.isNoteOn())
                    {
                        myMidiBufferSec.addEvent(m, sampleOffset);
                    }
                }
            }
        }
        else
        {
            auto timeFormat = midiFile.getTimeFormat();
            for (int t = 0; t < midiFile.getNumTracks(); t++)
            {
                const MidiMessageSequence* track = midiFile.getTrack(t);
                for (int i = 0; i < track->getNumEvents(); i++)
                {
                    MidiMessage& m = track->getEventPointer(i)->message;

                    if (allEvents || m.isNoteOff() || m.isNoteOn())
                    {
                        auto timeStamp = m.getTimeStamp() * PPQN / timeFormat;
                        myMidiBufferQN.addEvent(m, timeStamp);
                    }
                }
            }
        }

        return true;
    }
};

void walkAndSave(MidiLoader& l)
{
    // processBlock(): iterate the scheduled events; save_midi(): write them back out.
    MidiMessageSequence seq;
    for (auto* buf : {&l.myMidiBufferSec, &l.myMidiBufferQN})
    {
        MidiBuffer::Iterator it(*buf);
        MidiMessage msg;
        int pos = 0;
        while (it.getNextEvent(msg, pos))
        {
            msg.setTimeStamp(pos);
            seq.addEvent(msg);
        }
    }
    MidiFile out;
    // save_midi() uses setSmpteTimeFormat(30, 80), whose (-30) << 8 is a constant UBSan
    // shift-base report on every call regardless of input; a PPQN format keeps writeTo() reachable.
    out.setTicksPerQuarterNote(960);
    out.addTrack(seq);
    MemoryOutputStream mos;
    out.writeTo(mos);
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;
    const bool isBeats = data[0] & 1;
    const bool allEvents = data[0] & 2;
    data++;
    size--;

    const char* tmpdir = getenv("TMPDIR");
    std::string tmpl = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/fuzz_midi_XXXXXX";
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

    MidiLoader loader;
    try
    {
        if (loader.loadMidi(tmpl, true, isBeats, allEvents))
            walkAndSave(loader);
    }
    catch (const std::runtime_error&)
    {
    }
    unlink(tmpl.c_str());
    return 0;
}
