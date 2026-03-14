#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace DawWaveformSystemLogic {
    void RebuildWaveform(DawTrack& track, float sampleRate);
}

namespace DawClipSystemLogic {

    namespace {
        int addClipAudio(DawContext& daw, DawClipAudio&& data) {
            daw.clipAudio.emplace_back(std::move(data));
            return static_cast<int>(daw.clipAudio.size()) - 1;
        }

        void sortClipsByStart(std::vector<DawClip>& clips) {
            std::sort(clips.begin(), clips.end(), [](const DawClip& a, const DawClip& b) {
                if (a.startSample == b.startSample) return a.sourceOffset < b.sourceOffset;
                return a.startSample < b.startSample;
            });
        }

        int issueTakeId(DawTrack& track) {
            if (track.nextTakeId <= 0) track.nextTakeId = 1;
            int id = track.nextTakeId;
            if (track.nextTakeId < std::numeric_limits<int>::max()) {
                track.nextTakeId += 1;
            }
            return id;
        }

        void pushTakeClip(DawTrack& track, DawClip clip) {
            if (clip.length == 0) return;
            if (clip.takeId < 0) {
                clip.takeId = issueTakeId(track);
            }
            track.loopTakeClips.push_back(clip);
            track.activeLoopTakeIndex = static_cast<int>(track.loopTakeClips.size()) - 1;
        }

        void trimClipsForNewClip(DawTrack& track, const DawClip& clip) {
            if (clip.length == 0) return;
            uint64_t newStart = clip.startSample;
            uint64_t newEnd = clip.startSample + clip.length;
            std::vector<DawClip> updated;
            updated.reserve(track.clips.size() + 1);
            for (const auto& existing : track.clips) {
                if (existing.length == 0) continue;
                uint64_t exStart = existing.startSample;
                uint64_t exEnd = existing.startSample + existing.length;
                if (exEnd <= newStart || exStart >= newEnd) {
                    updated.push_back(existing);
                    continue;
                }
                if (newStart <= exStart && newEnd >= exEnd) {
                    continue;
                }
                if (newStart > exStart && newEnd < exEnd) {
                    DawClip left = existing;
                    left.length = newStart - exStart;
                    DawClip right = existing;
                    right.startSample = newEnd;
                    right.length = exEnd - newEnd;
                    right.sourceOffset = existing.sourceOffset + (newEnd - exStart);
                    if (left.length > 0) updated.push_back(left);
                    if (right.length > 0) updated.push_back(right);
                } else if (newStart <= exStart) {
                    DawClip right = existing;
                    right.startSample = newEnd;
                    right.length = exEnd - newEnd;
                    right.sourceOffset = existing.sourceOffset + (newEnd - exStart);
                    if (right.length > 0) updated.push_back(right);
                } else {
                    DawClip left = existing;
                    left.length = newStart - exStart;
                    if (left.length > 0) updated.push_back(left);
                }
            }
            track.clips = std::move(updated);
        }

        void rebuildTrackCacheFromClips(DawContext& daw, DawTrack& track) {
            uint64_t maxEnd = 0;
            for (const auto& clip : track.clips) {
                uint64_t end = clip.startSample + clip.length;
                if (end > maxEnd) maxEnd = end;
            }
            track.audio.clear();
            track.audio.resize(static_cast<size_t>(maxEnd), 0.0f);
            track.audioRight.clear();
            track.audioRight.resize(static_cast<size_t>(maxEnd), 0.0f);
            for (const auto& clip : track.clips) {
                if (clip.audioId < 0 || clip.audioId >= static_cast<int>(daw.clipAudio.size())) continue;
                const DawClipAudio& data = daw.clipAudio[clip.audioId];
                uint64_t clipStart = clip.startSample;
                uint64_t clipEnd = clip.startSample + clip.length;
                uint64_t srcOffset = clip.sourceOffset;
                if (clipEnd <= clipStart) continue;
                if (srcOffset >= data.left.size()) continue;
                uint64_t maxCopy = std::min<uint64_t>(clip.length, data.left.size() - srcOffset);
                for (uint64_t i = 0; i < maxCopy; ++i) {
                    size_t dst = static_cast<size_t>(clipStart + i);
                    if (dst >= track.audio.size()) break;
                    const size_t src = static_cast<size_t>(srcOffset + i);
                    float left = data.left[src];
                    float right = (data.channels > 1 && src < data.right.size()) ? data.right[src] : left;
                    track.audio[dst] = left;
                    track.audioRight[dst] = right;
                }
            }
            DawWaveformSystemLogic::RebuildWaveform(track, daw.sampleRate);
        }

        struct PendingRecordSegment {
            uint64_t sourceOffset = 0;
            uint64_t length = 0;
            uint64_t logicalStart = 0;
            bool loopTake = false;
        };

        void buildLoopRecordSegments(uint64_t rawStartSample,
                                     uint64_t pendingLength,
                                     uint64_t loopStartSample,
                                     uint64_t loopLength,
                                     std::vector<PendingRecordSegment>& outSegments) {
            outSegments.clear();
            if (pendingLength == 0 || loopLength == 0) return;
            uint64_t consumed = 0;
            outSegments.reserve(static_cast<size_t>(pendingLength / std::max<uint64_t>(1, loopLength)) + 2);
            while (consumed < pendingLength) {
                uint64_t rawSample = rawStartSample + consumed;
                uint64_t remaining = pendingLength - consumed;
                PendingRecordSegment seg{};
                seg.sourceOffset = consumed;
                if (rawSample < loopStartSample) {
                    uint64_t toLoopStart = loopStartSample - rawSample;
                    seg.length = std::min<uint64_t>(remaining, toLoopStart);
                    seg.logicalStart = rawSample;
                    seg.loopTake = false;
                } else {
                    uint64_t loopPos = (rawSample - loopStartSample) % loopLength;
                    uint64_t toLoopBoundary = loopLength - loopPos;
                    seg.length = std::min<uint64_t>(remaining, toLoopBoundary);
                    seg.logicalStart = loopStartSample + loopPos;
                    seg.loopTake = true;
                }
                if (seg.length == 0) break;
                outSegments.push_back(seg);
                consumed += seg.length;
            }
        }

        DawClip makeClipFromPendingRange(DawContext& daw,
                                         const std::vector<float>& pendingLeft,
                                         const std::vector<float>& pendingRight,
                                         uint64_t sourceOffset,
                                         uint64_t length,
                                         uint64_t startSample) {
            DawClip clip{};
            if (length == 0 || sourceOffset >= pendingLeft.size()) return clip;
            uint64_t safeLength = std::min<uint64_t>(length, pendingLeft.size() - sourceOffset);
            bool stereo = !pendingRight.empty();
            if (stereo && sourceOffset < pendingRight.size()) {
                safeLength = std::min<uint64_t>(safeLength, pendingRight.size() - sourceOffset);
            }
            if (safeLength == 0) return clip;
            DawClipAudio segment;
            segment.channels = stereo ? 2 : 1;
            segment.left.assign(pendingLeft.begin() + static_cast<size_t>(sourceOffset),
                                pendingLeft.begin() + static_cast<size_t>(sourceOffset + safeLength));
            if (stereo) {
                segment.right.assign(pendingRight.begin() + static_cast<size_t>(sourceOffset),
                                     pendingRight.begin() + static_cast<size_t>(sourceOffset + safeLength));
            }
            clip.audioId = addClipAudio(daw, std::move(segment));
            clip.startSample = startSample;
            clip.length = safeLength;
            clip.sourceOffset = 0;
            clip.takeId = -1;
            return clip;
        }

        void captureOverwrittenAsTakes(DawTrack& track, const DawClip& incoming) {
            if (incoming.length == 0) return;
            const uint64_t incomingStart = incoming.startSample;
            const uint64_t incomingEnd = incoming.startSample + incoming.length;
            for (const auto& existing : track.clips) {
                if (existing.length == 0) continue;
                const uint64_t exStart = existing.startSample;
                const uint64_t exEnd = existing.startSample + existing.length;
                if (exEnd <= incomingStart || exStart >= incomingEnd) continue;
                const uint64_t overlapStart = std::max(exStart, incomingStart);
                const uint64_t overlapEnd = std::min(exEnd, incomingEnd);
                if (overlapEnd <= overlapStart) continue;
                DawClip take = existing;
                take.startSample = overlapStart;
                take.length = overlapEnd - overlapStart;
                take.sourceOffset = existing.sourceOffset + (overlapStart - exStart);
                pushTakeClip(track, take);
            }
        }

        void insertActiveClip(DawTrack& track, const DawClip& clip, bool captureOverwritten) {
            if (clip.length == 0) return;
            if (captureOverwritten) {
                captureOverwrittenAsTakes(track, clip);
            }
            trimClipsForNewClip(track, clip);
            track.clips.push_back(clip);
            sortClipsByStart(track.clips);
        }

        void mergePendingRecords(DawContext& daw) {
            for (auto& track : daw.tracks) {
                if (track.pendingRecord.empty() && track.pendingRecordRight.empty()) continue;
                std::vector<float> pendingLeft = std::move(track.pendingRecord);
                std::vector<float> pendingRight = std::move(track.pendingRecordRight);
                track.pendingRecord.clear();
                track.pendingRecordRight.clear();
                uint64_t pendingFrames = static_cast<uint64_t>(pendingLeft.size());

                const bool loopCapture = track.recordLoopCapture
                    && track.recordLoopEndSample > track.recordLoopStartSample;
                std::vector<DawClip> regularClips;
                std::vector<DawClip> loopTakeClips;

                if (loopCapture) {
                    const uint64_t loopStart = track.recordLoopStartSample;
                    const uint64_t loopLength = track.recordLoopEndSample - track.recordLoopStartSample;
                    std::vector<PendingRecordSegment> segments;
                    buildLoopRecordSegments(track.recordStartSample,
                                            pendingFrames,
                                            loopStart,
                                            loopLength,
                                            segments);
                    for (const auto& seg : segments) {
                        DawClip clip = makeClipFromPendingRange(daw,
                                                                pendingLeft,
                                                                pendingRight,
                                                                seg.sourceOffset,
                                                                seg.length,
                                                                seg.logicalStart);
                        if (clip.length == 0) continue;
                        if (seg.loopTake) {
                            loopTakeClips.push_back(clip);
                        } else {
                            regularClips.push_back(clip);
                        }
                    }
                } else {
                    DawClip clip = makeClipFromPendingRange(daw,
                                                            pendingLeft,
                                                            pendingRight,
                                                            0,
                                                            pendingFrames,
                                                            track.recordStartSample);
                    if (clip.length > 0) {
                        regularClips.push_back(clip);
                    }
                }

                for (const auto& clip : regularClips) {
                    DawClip take = clip;
                    if (take.takeId < 0) take.takeId = issueTakeId(track);
                    pushTakeClip(track, take);
                    insertActiveClip(track, take, true);
                }

                if (!loopTakeClips.empty()) {
                    const uint64_t loopRangeStart = track.recordLoopStartSample;
                    const uint64_t loopRangeLength = track.recordLoopEndSample - track.recordLoopStartSample;
                    track.loopTakeRangeStartSample = loopRangeStart;
                    track.loopTakeRangeLength = loopRangeLength;
                    for (auto take : loopTakeClips) {
                        if (take.takeId < 0) take.takeId = issueTakeId(track);
                        pushTakeClip(track, take);
                    }
                    if (track.activeLoopTakeIndex >= 0
                        && track.activeLoopTakeIndex < static_cast<int>(track.loopTakeClips.size())) {
                        const DawClip& activeTake = track.loopTakeClips[static_cast<size_t>(track.activeLoopTakeIndex)];
                        insertActiveClip(track, activeTake, true);
                    }
                }

                track.recordLoopCapture = false;
                rebuildTrackCacheFromClips(daw, track);
            }
        }

        bool cycleTrackLoopTake(DawContext& daw, int trackIndex, int direction) {
            if (direction == 0) return false;
            if (trackIndex < 0 || trackIndex >= static_cast<int>(daw.tracks.size())) return false;
            DawTrack& track = daw.tracks[static_cast<size_t>(trackIndex)];
            const int takeCount = static_cast<int>(track.loopTakeClips.size());
            if (takeCount < 2) return false;
            int active = track.activeLoopTakeIndex;
            if (active < 0 || active >= takeCount) active = takeCount - 1;
            active += (direction > 0) ? 1 : -1;
            if (active < 0) active = takeCount - 1;
            if (active >= takeCount) active = 0;
            track.activeLoopTakeIndex = active;
            const DawClip& nextTake = track.loopTakeClips[static_cast<size_t>(active)];
            insertActiveClip(track, nextTake, false);
            rebuildTrackCacheFromClips(daw, track);
            return true;
        }
    }

    void TrimClipsForNewClip(DawTrack& track, const DawClip& clip) {
        trimClipsForNewClip(track, clip);
    }

    void RebuildTrackCacheFromClips(DawContext& daw, DawTrack& track) {
        rebuildTrackCacheFromClips(daw, track);
    }

    void MergePendingRecords(DawContext& daw) {
        mergePendingRecords(daw);
    }

    bool CycleTrackLoopTake(DawContext& daw, int trackIndex, int direction) {
        return cycleTrackLoopTake(daw, trackIndex, direction);
    }

    void UpdateDawClips(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
    }
}
