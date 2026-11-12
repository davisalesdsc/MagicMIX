#include "SpecJson.h"

#include "../Texto.h"

namespace mm::json
{
namespace
{
juce::DynamicObject::Ptr object() { return new juce::DynamicObject(); }

juce::var strings (const std::vector<std::string>& items)
{
    juce::Array<juce::var> out;
    for (auto& s : items)
        out.add (tx (s));
    return out;
}

std::vector<std::string> stringsFrom (const juce::var& v)
{
    std::vector<std::string> out;
    if (auto* array = v.getArray())
        for (auto& item : *array)
            out.push_back (item.toString().toStdString());
    return out;
}

std::string text (const juce::var& data, const char* key) { return data.getProperty (key, {}).toString().toStdString(); }

juce::var macrosVar (const ChainSpec& spec)
{
    juce::Array<juce::var> macros;
    for (auto& m : spec.macros)
    {
        auto macro = object();
        macro->setProperty ("name", tx (m.name));
        macro->setProperty ("description", tx (m.description));
        macro->setProperty ("value", m.value);
        juce::Array<juce::var> targets;
        for (auto& t : m.targets)
        {
            auto target = object();
            target->setProperty ("module_id", tx (t.moduleId));
            target->setProperty ("param", tx (t.param));
            target->setProperty ("value_at_0", t.valueAt0);
            target->setProperty ("value_at_1", t.valueAt1);
            targets.add (target.get());
        }
        macro->setProperty ("targets", targets);
        macros.add (macro.get());
    }
    return macros;
}

juce::var modulesVar (const ChainSpec& spec, bool paramsAsList)
{
    juce::Array<juce::var> modules;
    for (auto& m : spec.modules)
    {
        auto module = object();
        module->setProperty ("id", tx (m.id));
        module->setProperty ("type", tx (m.type));
        if (! m.enabled)
            module->setProperty ("enabled", false);
        if (paramsAsList)
        {
            juce::Array<juce::var> params;
            for (auto& [name, value] : m.params)
            {
                auto param = object();
                param->setProperty ("name", tx (name));
                param->setProperty ("value", value);
                params.add (param.get());
            }
            module->setProperty ("params", params);
        }
        else
        {
            auto params = object();
            for (auto& [name, value] : m.params)
                params->setProperty (juce::Identifier (tx (name)), value);
            module->setProperty ("params", params.get());
        }
        modules.add (module.get());
    }
    return modules;
}
}

juce::var toVar (const ChainSpec& spec)
{
    auto root = object();
    root->setProperty ("version", spec.version);
    root->setProperty ("title", tx (spec.title));
    root->setProperty ("prompts", strings (spec.prompts));
    root->setProperty ("summary", tx (spec.summary));
    root->setProperty ("details", tx (spec.details));
    root->setProperty ("suggestions", strings (spec.suggestions));
    root->setProperty ("modules", modulesVar (spec, false));
    root->setProperty ("macros", macrosVar (spec));
    root->setProperty ("mix", spec.mix);
    root->setProperty ("output_gain_db", spec.outputGainDb);
    return root.get();
}

juce::String toString (const ChainSpec& spec, bool singleLine)
{
    return juce::JSON::toString (toVar (spec), singleLine);
}

juce::var toLlmVar (const ChainSpec& spec)
{
    auto root = object();
    root->setProperty ("title", tx (spec.title));
    root->setProperty ("summary", tx (spec.summary));
    root->setProperty ("details", tx (spec.details));
    root->setProperty ("suggestions", strings (spec.suggestions));
    root->setProperty ("modules", modulesVar (spec, true));
    root->setProperty ("macros", macrosVar (spec));
    return root.get();
}

bool fromVar (const juce::var& data, ChainSpec& out, juce::String& error)
{
    if (! data.isObject())
    {
        error = tx ("o JSON não é um objeto");
        return false;
    }

    ChainSpec spec;
    spec.title = text (data, "title");
    spec.summary = text (data, "summary");
    spec.details = text (data, "details");
    spec.prompts = stringsFrom (data.getProperty ("prompts", {}));
    if (spec.prompts.empty() && data.hasProperty ("prompt") && data["prompt"].toString().isNotEmpty())
        spec.prompts.push_back (text (data, "prompt"));  // versão 1 guardava um único pedido
    spec.suggestions = stringsFrom (data.getProperty ("suggestions", {}));
    spec.mix = (float) (double) data.getProperty ("mix", 1.0);
    spec.outputGainDb = (float) (double) data.getProperty ("output_gain_db", 0.0);

    if (auto* modules = data.getProperty ("modules", {}).getArray())
        for (auto& m : *modules)
        {
            ModuleSpec module { text (m, "id"), text (m, "type"), {}, (bool) m.getProperty ("enabled", true) };
            const auto params = m.getProperty ("params", {});
            if (auto* list = params.getArray())  // resposta crua do LLM
            {
                for (auto& p : *list)
                    module.params[text (p, "name")] = (float) (double) p.getProperty ("value", 0.0);
            }
            else if (auto* dictionary = params.getDynamicObject())
            {
                for (auto& property : dictionary->getProperties())
                    module.params[property.name.toString().toStdString()] = (float) (double) property.value;
            }
            spec.modules.push_back (std::move (module));
        }

    if (auto* macros = data.getProperty ("macros", {}).getArray())
        for (auto& m : *macros)
        {
            MacroSpec macro;
            macro.name = text (m, "name");
            macro.description = text (m, "description");
            macro.value = (float) (double) m.getProperty ("value", 0.5);
            if (auto* targets = m.getProperty ("targets", {}).getArray())
                for (auto& t : *targets)
                    macro.targets.push_back ({ text (t, "module_id"), text (t, "param"),
                                               (float) (double) t.getProperty ("value_at_0", 0.0),
                                               (float) (double) t.getProperty ("value_at_1", 0.0) });
            spec.macros.push_back (std::move (macro));
        }

    out = std::move (spec);
    return true;
}

bool fromString (const juce::String& jsonText, ChainSpec& out, juce::String& error)
{
    juce::var data;
    const auto result = juce::JSON::parse (jsonText, data);
    if (result.failed())
    {
        error = tx ("JSON inválido: ") + result.getErrorMessage();
        return false;
    }
    return fromVar (data, out, error);
}

juce::var featuresToVar (const Features& f, double bpm, const juce::String& trackName)
{
    auto root = object();
    if (f.valid)
    {
        root->setProperty ("duration_s", f.durationS);
        root->setProperty ("sample_rate", f.sampleRate);
        root->setProperty ("channels", f.channels);
        root->setProperty ("peak_dbfs", f.peakDbfs);
        root->setProperty ("rms_dbfs", f.rmsDbfs);
        root->setProperty ("crest_factor_db", f.crestFactorDb);
        root->setProperty ("spectral_centroid_hz", f.spectralCentroidHz);

        static const char* bandNames[6] = { "sub", "low", "low_mid", "mid", "presence", "air" };
        auto bands = object();
        for (int i = 0; i < 6; ++i)
            bands->setProperty (bandNames[i], f.bandEnergyDb[i]);
        root->setProperty ("band_energy_db", bands.get());

        if (! f.resonances.empty())
        {
            juce::Array<juce::var> resonances;
            for (auto& r : f.resonances)
            {
                auto item = object();
                item->setProperty ("freq_hz", r.freqHz);
                item->setProperty ("prominence_db", r.prominenceDb);
                resonances.add (item.get());
            }
            root->setProperty ("resonances", resonances);
        }
        if (f.hasCorrelation)
            root->setProperty ("stereo_correlation", f.stereoCorrelation);
    }
    if (bpm > 0.0)
        root->setProperty ("bpm", bpm);
    if (trackName.isNotEmpty())
        root->setProperty ("track_name", trackName);
    return root.get();
}
}
