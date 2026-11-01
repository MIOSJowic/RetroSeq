#pragma once

// Kosmische: patch-filen (.kos). XML med global installning, ackordfoljd, alla spaar,
// kit och lanes, seeds, las, handredigerade monster och kopplingar till RetroSeq-spaar.
// Utgangar och VST3-tillstand ligger i RetroSeqs egen latfil.

#include "KosModel.h"

#include <juce_core/juce_core.h>

namespace kos
{

constexpr int kPatchFileVersion = 1;

std::unique_ptr<juce::XmlElement> patchToXml (const Patch& patch);

// Saknade attribut far forvalda varden; okanda element och attribut ignoreras.
// Returnerar false (och en felbeskrivning) om det inte ar en Kosmische-fil.
bool patchFromXml (const juce::XmlElement& xml, Patch& patch, juce::String& error);

bool savePatchFile (const Patch& patch, const juce::File& file, juce::String& error);
bool loadPatchFile (const juce::File& file, Patch& patch, juce::String& error);

} // namespace kos
