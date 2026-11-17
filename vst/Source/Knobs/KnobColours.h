#pragma once

#include <juce_graphics/juce_graphics.h>

/** Named plastic finishes for the Boss knob library.

    Each palette is a curated set of moulded-plastic colours. Black and white
    are common to both, so a selector usually presents them as a separate
    neutral row above the palette groups.
*/
enum class KnobPalette
{
    modern,
    vintage
};

/** Brushed finishes for the metal cap. */
enum class KnobMetalFinish
{
    aluminium,
    gold,
    black
};

enum class KnobFinish
{
    black,
    white,

    modernRed,
    modernBlue,
    modernGreen,
    modernPurple,
    modernYellow,
    modernOrange,

    vintageOxblood,
    vintageRoyalBlue,
    vintageSageGreen,
    vintageGrey,
    vintageCream
};

inline juce::Colour getKnobFinishColour (KnobFinish finish) noexcept
{
    switch (finish)
    {
        case KnobFinish::black:            return juce::Colour (0xff202020);
        case KnobFinish::white:            return juce::Colour (0xffe9e7e0);

        // Modern: higher chroma, slightly cooler, more saturated.
        case KnobFinish::modernRed:        return juce::Colour (0xffd94a45);
        case KnobFinish::modernBlue:       return juce::Colour (0xff2f7ac8);
        case KnobFinish::modernGreen:      return juce::Colour (0xff33a466);
        case KnobFinish::modernPurple:     return juce::Colour (0xff7e5bcb);
        case KnobFinish::modernYellow:     return juce::Colour (0xffe2b12c);
        case KnobFinish::modernOrange:     return juce::Colour (0xffe57932);

        // Vintage: lower chroma, warmer and dustier.
        case KnobFinish::vintageOxblood:   return juce::Colour (0xff5b262b);
        case KnobFinish::vintageRoyalBlue: return juce::Colour (0xff2c3f73);
        case KnobFinish::vintageSageGreen: return juce::Colour (0xff8b9a7e);
        case KnobFinish::vintageGrey:      return juce::Colour (0xff777a78);
        case KnobFinish::vintageCream:     return juce::Colour (0xffd9c9a5);
    }

    return juce::Colours::black;
}

inline juce::String getKnobFinishName (KnobFinish finish)
{
    switch (finish)
    {
        case KnobFinish::black:            return "Black";
        case KnobFinish::white:            return "White";

        case KnobFinish::modernRed:        return "Modern Red";
        case KnobFinish::modernBlue:       return "Modern Blue";
        case KnobFinish::modernGreen:      return "Modern Green";
        case KnobFinish::modernPurple:     return "Modern Purple";
        case KnobFinish::modernYellow:     return "Modern Yellow";
        case KnobFinish::modernOrange:     return "Modern Orange";

        case KnobFinish::vintageOxblood:   return "Vintage Oxblood";
        case KnobFinish::vintageRoyalBlue: return "Vintage Royal Blue";
        case KnobFinish::vintageSageGreen: return "Vintage Sage Green";
        case KnobFinish::vintageGrey:      return "Vintage Grey";
        case KnobFinish::vintageCream:     return "Vintage Cream";
    }

    return {};
}

inline juce::Array<KnobFinish> getKnobPaletteFinishes (KnobPalette palette)
{
    if (palette == KnobPalette::modern)
        return { KnobFinish::modernRed,
                 KnobFinish::modernBlue,
                 KnobFinish::modernGreen,
                 KnobFinish::modernPurple,
                 KnobFinish::modernYellow,
                 KnobFinish::modernOrange };

    return { KnobFinish::vintageOxblood,
             KnobFinish::vintageRoyalBlue,
             KnobFinish::vintageSageGreen,
             KnobFinish::vintageGrey,
             KnobFinish::vintageCream };
}

inline juce::Array<KnobFinish> getCommonKnobFinishes()
{
    return { KnobFinish::black, KnobFinish::white };
}

inline juce::Colour getKnobMetalColour (KnobMetalFinish finish) noexcept
{
    switch (finish)
    {
        case KnobMetalFinish::aluminium: return juce::Colour (0xffc8c3b7);
        case KnobMetalFinish::gold:      return juce::Colour (0xffd3a64c);
        case KnobMetalFinish::black:     return juce::Colour (0xff383838);
    }

    return juce::Colour (0xffc8c3b7);
}

inline juce::String getKnobMetalName (KnobMetalFinish finish)
{
    switch (finish)
    {
        case KnobMetalFinish::aluminium: return "Brushed Aluminium";
        case KnobMetalFinish::gold:      return "Brushed Gold";
        case KnobMetalFinish::black:     return "Brushed Black";
    }

    return {};
}

inline juce::Array<KnobMetalFinish> getKnobMetalFinishes()
{
    return { KnobMetalFinish::aluminium,
             KnobMetalFinish::gold,
             KnobMetalFinish::black };
}

inline KnobMetalFinish getKnobMetalFinishByName (juce::String name, bool& found) noexcept
{
    name = name.toLowerCase().removeCharacters (" -_");

    if (name == "aluminium" || name == "aluminum" || name == "silver")
    {
        found = true;
        return KnobMetalFinish::aluminium;
    }

    if (name == "gold" || name == "brushedgold")
    {
        found = true;
        return KnobMetalFinish::gold;
    }

    if (name == "black" || name == "blackmetal" || name == "brushedblack")
    {
        found = true;
        return KnobMetalFinish::black;
    }

    found = false;
    return KnobMetalFinish::aluminium;
}

/** Light plastics need a dark dot; dark plastics keep the white dot. */
inline bool isKnobPointerLightColour (juce::Colour plastic) noexcept
{
    const auto white = getKnobFinishColour (KnobFinish::white);
    const auto cream = getKnobFinishColour (KnobFinish::vintageCream);

    return plastic.getARGB() == white.getARGB()
        || plastic.getARGB() == cream.getARGB();
}

inline juce::Colour getKnobIndicatorColour (juce::Colour plastic) noexcept
{
    return isKnobPointerLightColour (plastic)
               ? juce::Colour (0xff1b1b1b)
               : juce::Colour (0xfffff8e8);
}

inline bool isKnobFinishLight (KnobFinish finish) noexcept
{
    return getKnobFinishColour (finish).getPerceivedBrightness() > 0.55f;
}

/** Parses a display name, enum-style key or short colour name. */
inline KnobFinish getKnobFinishByName (juce::String name, bool& found) noexcept
{
    name = name.toLowerCase().removeCharacters (" -_");

    struct Entry { const char* key; KnobFinish finish; };
    static constexpr Entry entries[] = {
        { "black",         KnobFinish::black },
        { "white",         KnobFinish::white },
        { "modernred",     KnobFinish::modernRed },
        { "red",           KnobFinish::modernRed },
        { "modernblue",    KnobFinish::modernBlue },
        { "blue",          KnobFinish::modernBlue },
        { "moderngreen",   KnobFinish::modernGreen },
        { "green",         KnobFinish::modernGreen },
        { "modernpurple",  KnobFinish::modernPurple },
        { "purple",        KnobFinish::modernPurple },
        { "modernyellow",  KnobFinish::modernYellow },
        { "yellow",        KnobFinish::modernYellow },
        { "modernorange",  KnobFinish::modernOrange },
        { "orange",        KnobFinish::modernOrange },
        { "vintageoxblood",KnobFinish::vintageOxblood },
        { "oxblood",       KnobFinish::vintageOxblood },
        { "vintageroyalblue", KnobFinish::vintageRoyalBlue },
        { "royalblue",     KnobFinish::vintageRoyalBlue },
        { "vintagesagegreen", KnobFinish::vintageSageGreen },
        { "vintagesage",   KnobFinish::vintageSageGreen },
        { "sagegreen",     KnobFinish::vintageSageGreen },
        { "sage",          KnobFinish::vintageSageGreen },
        { "vintagegrey",   KnobFinish::vintageGrey },
        { "vintagegray",   KnobFinish::vintageGrey },
        { "grey",          KnobFinish::vintageGrey },
        { "gray",          KnobFinish::vintageGrey },
        { "vintagecream",  KnobFinish::vintageCream },
        { "cream",         KnobFinish::vintageCream }
    };

    for (const auto& entry : entries)
    {
        if (name == entry.key)
        {
            found = true;
            return entry.finish;
        }
    }

    found = false;
    return KnobFinish::black;
}
