#pragma once

#include "dsp/Mapping.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

// Undo and redo for the player's changes. Each change made on the page is one step: a knob turned (from grabbing it to
// letting go), a button, a menu, a preset recalled. Host automation is not recorded (it changes parameters without a
// gesture), and only parameters are: the sounds picked and the beat pattern are not part of it. Everything one click
// sets off counts as a single step, however many parameters it moves. The history lives with the processor, so it
// outlasts closing the window.
class ParamUndo : private juce::AudioProcessorListener, private juce::AsyncUpdater
{
public:
    explicit ParamUndo(juce::AudioProcessor& p, juce::StringArray untracked = {}) : processor(p), skipped(std::move(untracked))
    {
        processor.addListener(this);
    }
    ~ParamUndo() override
    {
        processor.removeListener(this);
        cancelPendingUpdate();
    }

    // Keeps the sound as it is now, before a change the page is about to make without a gesture (a preset recalled).
    void remember()
    {
        if (restoring || grouping)
            return;
        const auto now = values();
        if (undoSteps.empty() || !same(undoSteps.back(), now))
        {
            undoSteps.push_back(now);
            if (undoSteps.size() > kMaxSteps)
                undoSteps.erase(undoSteps.begin());
        }
        redoSteps.clear();
        grouping = true; // until this click (or drag start) has been handled
        triggerAsyncUpdate();
    }

    bool canUndo() const { return firstDifferent(undoSteps) >= 0; }
    bool canRedo() const { return firstDifferent(redoSteps) >= 0; }
    void undo() { step(undoSteps, redoSteps); }
    void redo() { step(redoSteps, undoSteps); }

    // Ends the step now rather than when the message loop comes round (for tests, which have no message loop).
    void endStep() { handleUpdateNowIfNeeded(); }

private:
    static constexpr size_t kMaxSteps = 100;

    std::vector<float> values() const
    {
        std::vector<float> v;
        for (auto* param : processor.getParameters())
            v.push_back(param->getValue());
        return v;
    }

    bool tracked(int index) const
    {
        const auto& params = processor.getParameters();
        if (index < 0 || index >= params.size())
            return false;
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[index]))
            return !skipped.contains(withId->paramID);
        return true;
    }

    bool same(const std::vector<float>& a, const std::vector<float>& b) const
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (tracked((int) i) && !bitvibe::sameFloat(a[i], b[i]))
                return false;
        return true;
    }

    // The newest step that would change what is there now (older ones that match it are left out), or -1.
    int firstDifferent(const std::vector<std::vector<float>>& steps) const
    {
        const auto now = values();
        for (int i = (int) steps.size() - 1; i >= 0; --i)
            if (!same(steps[(size_t) i], now))
                return i;
        return -1;
    }

    void step(std::vector<std::vector<float>>& from, std::vector<std::vector<float>>& to)
    {
        const int index = firstDifferent(from);
        if (index < 0)
            return;
        cancelPendingUpdate();
        grouping = false;
        to.push_back(values());
        const auto target = from[(size_t) index];
        from.resize((size_t) index);
        restoring = true;
        const auto& params = processor.getParameters();
        for (int i = 0; i < params.size() && (size_t) i < target.size(); ++i)
            if (tracked(i) && !bitvibe::sameFloat(params[i]->getValue(), target[(size_t) i]))
            {
                params[i]->beginChangeGesture(); // so the host records it like a change made by hand
                params[i]->setValueNotifyingHost(target[(size_t) i]);
                params[i]->endChangeGesture();
            }
        restoring = false;
    }

    void handleAsyncUpdate() override { grouping = false; }

    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index) override
    {
        if (tracked(index))
            remember();
    }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}

    juce::AudioProcessor& processor;
    juce::StringArray skipped; // parameters left out: the momentary ones
    std::vector<std::vector<float>> undoSteps, redoSteps;
    bool restoring = false, grouping = false;
};
