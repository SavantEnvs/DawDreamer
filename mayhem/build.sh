#!/usr/bin/env bash
# mayhem/build.sh — build DawDreamer's fuzz harness(es) and the known-answer test tool.
#
# The fuzzed code is the Ableton .asd warp-file parser (Source/AbletonClipInfo.h), which is
# header-only and needs only the in-tree thirdparty/portable_endian headers, so the harness TU
# compiles the project code itself with $SANITIZER_FLAGS $DEBUG_FLAGS. No network access.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:?STANDALONE_FUZZ_MAIN must be set (provided by the base image)}"

cd "$SRC"
INC=(-I"$SRC/Source" -I"$SRC/thirdparty")
OBJ="$(mktemp -d)"
trap 'rm -rf "$OBJ"' EXIT

# LeakSanitizer off at build time (docs/rules/c.md); ASan + UBSan stay fully active.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

# UBSan's `bool` check is relaxed for fuzz_asd ONLY (ASan and every other UBSan check stay halting):
# AbletonClipInfo::read_bool() (Source/AbletonClipInfo.h:250-255) freads a raw file byte straight into
# a `bool`, so ANY byte other than 0/1 at the loop_on/warp_on offsets leaves an invalid bool behind.
# Reproducing frame without this flag: "fuzz_asd.cc:25:14: runtime error: load of value 2, which is
# not a valid value for type 'bool'" in exercise() (the `info.warp_on` read that mirrors
# PlaybackWarpProcessor::getWarpOn()). It fires on nearly every mutated input and would stop the
# fuzzer from reaching anything past it. fuzz_midi and the JUCE objects do NOT get this flag.
RELAX="-fno-sanitize=bool"
for h in fuzz_asd; do
  $CXX -std=c++17 $SANITIZER_FLAGS $RELAX $DEBUG_FLAGS $LIB_FUZZING_ENGINE "${INC[@]}" \
      "$SRC/mayhem/$h.cc" "$OBJ/lsan_off.o" -o "$SRC/$h"
  $CXX -std=c++17 $SANITIZER_FLAGS $RELAX $DEBUG_FLAGS "${INC[@]}" \
      "$SRC/mayhem/$h.cc" "$OBJ/standalone_main.o" "$OBJ/lsan_off.o" -o "$SRC/$h-standalone"
done

# Known-answer test tool, NORMAL flags (no sanitizers): mayhem/test.sh runs it.
$CXX -std=c++17 -O2 $COVERAGE_FLAGS "${INC[@]}" "$SRC/mayhem/tests/asd_dump.cc" -o "$SRC/asd_dump"

# fuzz_midi.cc carries a copy of SamplerProcessor::loadMidi (the class needs the Python extension).
# Fail the build if the copy drifts from upstream: compare the two method bodies with // comments and
# whitespace stripped.
loadmidi_body() {
  awk '/bool loadMidi\(const std::string& path/{f=1} f{print} f&&/^        return true;/{getline; print; exit}' "$1" \
    | sed 's#//.*##' | tr -d ' \t\n'
}
up="$(loadmidi_body "$SRC/Source/SamplerProcessor.h")"; ours="$(loadmidi_body "$SRC/mayhem/fuzz_midi.cc")"
if [ -z "$up" ] || [ "$up" != "$ours" ]; then
  echo "build.sh: mayhem/fuzz_midi.cc's loadMidi copy differs from Source/SamplerProcessor.h — re-sync it" >&2
  exit 1
fi

# fuzz_midi: SamplerProcessor::loadMidi on JUCE's MidiFile parser. JUCE's juce_core + juce_audio_basics
# (pinned submodule commit, fetched into /opt/toolchains/deps by the Dockerfile) are compiled with the
# fuzz flags since the MIDI parser is the fuzzed surface.
: "${JUCE_DIR:=/opt/toolchains/deps/juce-cf4f12a452611e46d29e4549ce69adac345706b7/src}"
JINC=(-I"$SRC/mayhem/juce_min" -I"$JUCE_DIR/modules")
JFLAGS="-std=c++17 -O1 -DNDEBUG=1 $SANITIZER_FLAGS $DEBUG_FLAGS"
$CXX $JFLAGS -fsanitize=fuzzer-no-link -w "${JINC[@]}" -c "$SRC/mayhem/juce_min/juce_core.cpp" -o "$OBJ/juce_core.o" & p1=$!
$CXX $JFLAGS -fsanitize=fuzzer-no-link -w "${JINC[@]}" -c "$SRC/mayhem/juce_min/juce_audio_basics.cpp" -o "$OBJ/juce_audio_basics.o" & p2=$!
wait "$p1"; wait "$p2"
JLIBS=(-lpthread -ldl -lrt)
$CXX $JFLAGS $LIB_FUZZING_ENGINE "${JINC[@]}" "$SRC/mayhem/fuzz_midi.cc" "$OBJ/juce_core.o" \
    "$OBJ/juce_audio_basics.o" "$OBJ/lsan_off.o" "${JLIBS[@]}" -o "$SRC/fuzz_midi"
$CXX $JFLAGS "${JINC[@]}" "$SRC/mayhem/fuzz_midi.cc" "$OBJ/juce_core.o" "$OBJ/juce_audio_basics.o" \
    "$OBJ/standalone_main.o" "$OBJ/lsan_off.o" "${JLIBS[@]}" -o "$SRC/fuzz_midi-standalone"

echo "build.sh: built fuzz_asd, fuzz_midi (+ -standalone), asd_dump"
