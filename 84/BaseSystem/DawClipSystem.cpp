#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace DawWaveformSystemLogic {
    void RebuildWaveform(DawTrack& track, float sampleRate);
}

namespace DawClipSystemLogic {

    namespace {
        int addClipAudio(DawContext& daw, std::vector<float>&& data) {
            daw.clipAudio.emplace_back(std::move(data));
            return static_cast<int>(daw.clipAudio.size()) - 1;
        }

        void sortClipsByStart(std::vector<DawClip>& clips) {
            std::sort(clips.begin(), clips.end(), [](const DawClip& a, const DawClip& b) {
                if (a.startSample == b.startSample) return a.sourceOffset < b.sourceOffset;
                return a.startSample < b.startSample;
            });
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
            for (const auto& clip : track.clips) {
                if (clip.audioId < 0 || clip.audioId >= static_cast<int>(daw.clipAudio.size())) continue;
                const auto& data = daw.clipAudio[clip.audioId];
                uint64_t clipStart = clip.startSample;
                uint64_t clipEnd = clip.startSample + clip.length;
                uint64_t srcOffset = clip.sourceOffset;
                if (clipEnd <= clipStart) continue;
                if (srcOffset >= data.size()) continue;
                uint64_t maxCopy = std::min<uint64_t>(clip.length, data.size() - srcOffset);
                for (uint64_t i = 0; i < maxCopy; ++i) {
                    size_t dst = static_cast<size_t>(clipStart + i);
                    if (dst >= track.audio.size()) break;
                    track.audio[dst] = data[static_cast<size_t>(srcOffset + i)];
                }
            }
            DawWaveformSystemLogic::RebuildWaveform(track, daw.sampleRate);
        }

        void mergePendingRecords(DawContext& daw) {
            for (auto& track : daw.tracks) {
                if (track.pendingRecord.empty()) continue;
                DawClip newClip{};
                newClip.audioId = addClipAudio(daw, std::move(track.pendingRecord));
                newClip.startSample = track.recordStartSample;
                newClip.length = (newClip.audioId >= 0 && newClip.audioId < static_cast<int>(daw.clipAudio.size()))
                    ? static_cast<uint64_t>(daw.clipAudio[newClip.audioId].size())
                    : 0;
                newClip.sourceOffset = 0;
                trimClipsForNewClip(track, newClip);
                if (newClip.length > 0) {
                    track.clips.push_back(newClip);
                    sortClipsByStart(track.clips);
                }
                track.pendingRecord.clear();
                rebuildTrackCacheFromClips(daw, track);
            }
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

    void UpdateDawClips(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
    }
}
