#include "control_card_model.h"

namespace fbsampler::ui {

namespace {

bool parseCcId(const std::string& id, int& ccOut)
{
    if (id.size() < 3 || id.compare(0, 2, "cc") != 0)
        return false;
    int value = 0;
    for (std::size_t i = 2; i < id.size(); ++i) {
        const char c = id[i];
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
        if (value > 127)
            return false;
    }
    ccOut = value;
    return true;
}

ControlCardDescriptor generic(ControlCardDescriptor::Kind kind,
                              const char* label)
{
    ControlCardDescriptor d;
    d.kind = kind;
    d.label = label;
    d.accessibleName = label;
    return d;
}

} // namespace

ControlCardModel buildControlCards(const std::vector<ControlMapEntry>& controls,
                                   bool isSoundfont)
{
    ControlCardModel model;
    model.showPresetRow = isSoundfont;

    for (const auto& entry : controls) {
        int cc = -1;
        if (!parseCcId(entry.id, cc))
            continue; // no binding derivable — nothing a knob could drive
        ControlCardDescriptor d;
        d.kind = ControlCardDescriptor::Kind::cc;
        d.ccNumber = cc;
        d.label = entry.displayName.empty() ? entry.id : entry.displayName;
        d.accessibleName =
            entry.accessibleName.empty() ? d.label : entry.accessibleName;
        model.cards.push_back(std::move(d));
    }

    if (model.cards.empty()) {
        // FR12: libraries defining no controls get the generic set.
        model.cards.push_back(
            generic(ControlCardDescriptor::Kind::volume, "Volume"));
        model.cards.push_back(generic(ControlCardDescriptor::Kind::pan, "Pan"));
        model.cards.push_back(
            generic(ControlCardDescriptor::Kind::tuning, "Tuning"));
        model.cards.push_back(
            generic(ControlCardDescriptor::Kind::attack, "Attack"));
        model.cards.push_back(
            generic(ControlCardDescriptor::Kind::release, "Release"));
    }

    return model;
}

} // namespace fbsampler::ui
