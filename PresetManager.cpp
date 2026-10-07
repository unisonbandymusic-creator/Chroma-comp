#include "PresetManager.h"

namespace
{
const char* const kIds[] = { "threshold", "ratio", "knee", "attack", "release", "makeup", "mix", "lookahead", "feel", "intensity", "transients",
                             "hold", "range", "lookaheadTime", "autoRelease", "adapt", "kneeHardness", "adaptiveOn" };
constexpr int kNumIds = (int) (sizeof (kIds) / sizeof (kIds[0]));
const char* const kExt = ".ccpreset";

struct Factory { const char* name; float v[kNumIds]; };
//                                     thr    ratio knee  atk   rel   makeup mix  look feel int trans | hold range lkT  auto adapt kneeH adaptOn
const Factory kFactory[] = {
    // Init: does no compression at all (threshold 0 dB, ratio 1:1 -> zero gain reduction, no makeup, mix 100 %),
    // lookahead off and the adaptive engine switched off.
    { "Init",                        {   0.f, 1.0f,  0.f, 10.f, 120.f, 0.f,  100.f, 0.f, 1.f, 1.f, 0.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 0.f } },
    { "Gentle Master Bus",           { -14.f, 1.6f, 12.f, 30.f, 200.f, 1.f,  100.f, 0.f, 3.f, 0.f, 0.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 1.f } },
    { "Mix Glue",                    { -16.f, 2.0f, 10.f, 20.f, 150.f, 1.5f, 100.f, 0.f, 3.f, 1.f, 0.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 1.f } },
    { "Vocal Control",               { -22.f, 3.5f,  6.f,  6.f,  90.f, 3.f,  100.f, 0.f, 2.f, 1.f, 0.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 1.f } },
    { "Drum Punch",                  { -18.f, 4.0f,  4.f, 25.f,  80.f, 2.f,  100.f, 0.f, 0.f, 2.f, 0.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 1.f } },
    { "Parallel Drum Crush",         { -28.f, 6.0f,  6.f,  3.f, 100.f, 6.f,   45.f, 1.f, 0.f, 2.f, 1.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 1.f } },
    { "Bass Control",                { -20.f, 4.0f,  8.f, 15.f, 150.f, 2.f,  100.f, 0.f, 2.f, 1.f, 0.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 1.f } },
    { "Peak Tamer (Lookahead)",      { -10.f, 10.f,  2.f, 0.5f,  60.f, 0.f,  100.f, 1.f, 0.f, 2.f, 1.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f, 1.f } },
};
const Factory* findFactory (const juce::String& n)
{
    for (auto& f : kFactory) if (n == f.name) return &f;
    return nullptr;
}
} // namespace

juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray a;
    for (auto& f : kFactory) a.add (f.name);
    return a;
}

juce::File PresetManager::getUserDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    dir = dir.getChildFile ("HimzDSP").getChildFile ("Chroma Comp").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray a;
    auto files = getUserDirectory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExt);
    files.sort();
    for (auto& f : files) a.add (f.getFileNameWithoutExtension());
    return a;
}

juce::StringArray PresetManager::getAllNames() const
{
    auto a = getFactoryNames();
    a.addArray (getUserNames());
    return a;
}

bool PresetManager::isFactory (const juce::String& n) const { return findFactory (n) != nullptr; }

juce::File PresetManager::fileFor (const juce::String& name) const
{
    return getUserDirectory().getChildFile (juce::File::createLegalFileName (name.trim()) + kExt);
}

juce::String PresetManager::getCurrentName() const
{
    const auto n = state.state.getProperty ("preset", "Init").toString();
    return n == "Init - Transparent" ? juce::String ("Init") : n;   // sessions saved under the old preset name
}

void PresetManager::applyValues (const juce::String& name, const std::map<juce::String, float>& values)
{
    for (auto& kv : values)
        if (auto* p = state.getParameter (kv.first))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));
            p->endChangeGesture();
        }
    state.state.setProperty ("preset", name, nullptr);
}

bool PresetManager::load (const juce::String& name)
{
    std::map<juce::String, float> values;

    if (auto* f = findFactory (name))
    {
        for (int i = 0; i < kNumIds; ++i) values[kIds[i]] = f->v[i];
    }
    else
    {
        auto xml = juce::parseXML (fileFor (name));
        if (xml == nullptr || ! xml->hasTagName ("ChromaPreset")) return false;

        std::map<juce::String, float> fromFile;
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
            for (auto* id : kIds)
                if (e->getStringAttribute ("id") == id)
                    fromFile[id] = (float) e->getDoubleAttribute ("v");
        if (fromFile.empty()) return false;

        // Presets saved before hold / range / lookahead time / auto release / adapt / knee hardness / adaptive
        // engine existed do not contain those ids: start from the Init values so they load deterministically.
        // The adaptive engine was always running before it had a switch, so an old preset turns it on.
        for (int i = 0; i < kNumIds; ++i) values[kIds[i]] = kFactory[0].v[i];
        values["adaptiveOn"] = 1.0f;
        for (auto& kv : fromFile) values[kv.first] = kv.second;
    }
    applyValues (name, values);
    return true;
}

bool PresetManager::save (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty() || isFactory (name)) return false;

    juce::XmlElement root ("ChromaPreset");
    root.setAttribute ("name", name);
    for (auto* id : kIds)
        if (auto* v = state.getRawParameterValue (id))
        {
            auto* e = root.createNewChildElement ("P");
            e->setAttribute ("id", id);
            e->setAttribute ("v", (double) v->load());
        }
    if (! root.writeTo (fileFor (name))) return false;
    state.state.setProperty ("preset", name, nullptr);
    return true;
}

bool PresetManager::remove (const juce::String& name)
{
    if (isFactory (name)) return false;
    return fileFor (name).deleteFile();
}

bool PresetManager::step (int delta)
{
    const auto names = getAllNames();
    if (names.isEmpty()) return false;
    int idx = names.indexOf (getCurrentName());
    if (idx < 0) idx = delta > 0 ? -1 : 0;
    const int n = names.size();
    return load (names[((idx + delta) % n + n) % n]);
}
