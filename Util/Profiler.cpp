#include <Util/Profiler.hpp>

#include <sstream>

using namespace std;

ProfilerSample  Profiler::mFrames[PROFILER_FRAME_COUNT];
ProfilerSample* Profiler::mCurrentSample = nullptr;
uint64_t Profiler::mCurrentFrame = 0;
const std::chrono::high_resolution_clock Profiler::mTimer;

void Profiler::BeginSample(const string& label, bool resume = false) {
	if (resume) {
		for (auto& c : mCurrentSample->mChildren)
			if (c.mLabel == label) {
				mCurrentSample = &c;
				c.mStartTime = mTimer.now();
				return;
			}
	}
	mCurrentSample->mChildren.push_back({ label, mCurrentSample, {} });
	mCurrentSample = &mCurrentSample->mChildren.back();
	mCurrentSample->mStartTime = mTimer.now();
}
void Profiler::EndSample() {
	if (!mCurrentSample->mParent) {
		fprintf_color(Red, stderr, "Error: Attempt to end nonexistant Profiler sample!");
		throw;
	}
	mCurrentSample->mTime += mTimer.now() - mCurrentSample->mStartTime;
	mCurrentSample = mCurrentSample->mParent;
}

void Profiler::FrameStart() {
	int i = mCurrentFrame % PROFILER_FRAME_COUNT;
	mFrames[i].mLabel = "Frame " + to_string(mCurrentFrame);
	mFrames[i].mParent = nullptr;
	mFrames[i].mStartTime = mTimer.now();
	mFrames[i].mTime = chrono::nanoseconds::zero();
	mFrames[i].mChildren.clear();
	mCurrentSample = &mFrames[i];
}
void Profiler::FrameEnd() {
	mCurrentFrame++;
}

void PrintSample(char* data, size_t& size, ProfilerSample* s, uint32_t tabLevel) {
	for (uint32_t i = 0; i < tabLevel; i++)
		size -= sprintf_s(data, size, "  ");

	size -= sprintf_s(data, size, "%s: %.2fms\n", s->mLabel.c_str(), (s->mTime.count() * 1e-6));
	for (auto& pc : s->mChildren)
		PrintSample(data, size, &pc, tabLevel + 1);
}
void Profiler::PrintLastFrame(char* buffer, size_t size) {
	PrintSample(buffer, size, &mFrames[(mCurrentFrame + PROFILER_FRAME_COUNT - 1) % PROFILER_FRAME_COUNT], 1);
}