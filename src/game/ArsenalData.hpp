#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace restoration {

enum class EquipmentCategory { Main, Sub, Special };
enum class TuningType { Number, Boolean, String, Array, Object, Null };

struct TuningField {
    std::string_view path;
    TuningType type;
    double value;
    std::string_view text;
};

// Source paths use dots for nested objects and array indices (for example "stats.range").
struct EquipmentData {
    std::string_view id, name, kind, sub, special;
    const TuningField* fields;
    std::size_t fieldCount;

    const TuningField* find(std::string_view path) const {
        for(std::size_t i=0;i<fieldCount;++i) if(fields[i].path==path) return &fields[i];
        return nullptr;
    }
    const TuningField& require(std::string_view path,TuningType type) const {
        const auto* field=find(path);
        if(!field || field->type!=type) throw std::invalid_argument("Invalid tuning field: "+std::string(id)+"."+std::string(path));
        return *field;
    }
    double number(std::string_view path) const { return require(path,TuningType::Number).value; }
    double number(std::string_view path,double fallback) const { return find(path)?number(path):fallback; }
    bool flag(std::string_view path) const { return require(path,TuningType::Boolean).value!=0; }
    bool flag(std::string_view path,bool fallback) const { return find(path)?flag(path):fallback; }
    std::string_view text(std::string_view path) const { return require(path,TuningType::String).text; }
    std::string_view text(std::string_view path,std::string_view fallback) const { return find(path)?text(path):fallback; }
    std::size_t size(std::string_view path) const {
        const auto* field=find(path);
        if(!field || (field->type!=TuningType::Array && field->type!=TuningType::Object))
            throw std::invalid_argument("Invalid tuning container: "+std::string(id)+"."+std::string(path));
        return static_cast<std::size_t>(field->value);
    }
    std::vector<double> array(std::string_view path) const {
        require(path,TuningType::Array);
        std::vector<double> values;
        for(std::size_t i=0;i<size(path);++i) values.push_back(number(std::string(path)+"."+std::to_string(i)));
        return values;
    }
};

const std::vector<EquipmentData>& MainEquipment();
const std::vector<EquipmentData>& SubEquipment();
const std::vector<EquipmentData>& SpecialEquipment();
const EquipmentData& GetEquipment(EquipmentCategory category,std::string_view id);
const EquipmentData& PlayerTuning();
const EquipmentData& DifficultyTuning(std::string_view id);
const EquipmentData& MatchTuning();
const std::vector<std::string_view>& BotNames();

} // namespace restoration
