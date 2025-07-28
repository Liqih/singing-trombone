#pragma once

#include <JuceHeader.h>

using namespace juce;

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();

    static CustomLookAndFeel& getInstance();
};
