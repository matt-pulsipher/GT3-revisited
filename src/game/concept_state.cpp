#include "game/concept_state.h"

#include <algorithm>

namespace gt3r::game {

int ConceptState::medal(int course) const {
    const auto it = licenses.find(course);
    return it == licenses.end() ? kConceptNoMedal : it->second.medal;
}

int ConceptState::place(int level, int course) const {
    const auto it = places.find({level, course});
    return it == places.end() ? kConceptNoPlace : it->second;
}

bool ConceptState::won_any(int course) const {
    for (int l = 0; l < kConceptLevels; ++l)
        if (won(l, course)) return true;
    return false;
}

bool ConceptState::won_pro(int course) const {
    for (int l = 1; l < kConceptLevels; ++l)
        if (won(l, course)) return true;
    return false;
}

void ConceptState::record_license(int course, int medal, double seconds) {
    if (course < 0 || medal < 0 || medal > 2) return;
    License& l = licenses[course];
    if (l.medal == kConceptNoMedal || medal < l.medal) l.medal = medal;
    if (seconds > 0 && (l.best_time < 0 || seconds < l.best_time)) l.best_time = seconds;
}

void ConceptState::record_place(int level, int course, int place) {
    if (level < 0 || level >= kConceptLevels || course < 0 || place < 0) return;
    auto [it, fresh] = places.try_emplace({level, course}, place);
    if (!fresh) it->second = std::min(it->second, place);
}

int ConceptRules::course_index(std::string_view course) const {
    for (size_t i = 0; i < courses.size(); ++i)
        if (courses[i] == course) return int(i);
    return -1;
}

const ConceptRules::Key* ConceptRules::key(std::string_view name) const {
    for (const Key& k : keys)
        if (k.name == name) return &k;
    return nullptr;
}

std::optional<ConceptRules::Key> ConceptRules::parse_key(std::string_view name) {
    auto digit = [&](size_t i) { return i < name.size() && name[i] >= '0' && name[i] <= '9'; };
    if (name.size() < 5 || name[0] != 'C' || !digit(1) || !digit(2) || !digit(4)) return std::nullopt;
    const char kind = name[3];
    if (kind != 'L' && kind != 'N' && kind != 'H') return std::nullopt;
    Key k;
    k.name = std::string(name);
    k.course = (name[1] - '0') * 10 + (name[2] - '0');
    k.kind = kind;
    k.variant = name[4] - '0';
    if (name.size() == 5) return k;
    if (name.substr(5) != "_S") return std::nullopt;
    k.twin = true;
    return k;
}

ConceptRun concept_run(const ConceptRules& rules, std::string_view race_code, std::string_view course) {
    ConceptRun r;
    auto digit = [&](size_t from_end) {
        if (race_code.size() < from_end) return -1;
        const char c = race_code[race_code.size() - from_end];
        return c >= '0' && c <= '9' ? c - '0' : -1;
    };
    if (race_code.substr(0, 2) == "LC") {
        if (digit(1) < 0 || digit(2) < 0) return r;
        r.mode = 0;
        r.course = digit(2) * 10 + digit(1);
        return r;
    }
    const int level = digit(1);
    if (level < 1 || level > kConceptLevels) return r;
    r.course = rules.course_index(course);
    if (r.course >= 0) r.mode = level;
    return r;
}

std::string concept_race_code(std::string_view entry_code, int level) {
    std::string code(entry_code);
    if (!code.empty() && level > 0) code.back() = char(code.back() + level);
    return code;
}

bool concept_all_won(const ConceptRules& rules, const ConceptState& state) {
    for (int c = 0; c < rules.course_count(); ++c)
        if (!state.won_any(c)) return false;
    return rules.course_count() > 0;
}

bool concept_all_won_pro(const ConceptRules& rules, const ConceptState& state) {
    for (int c = 0; c < rules.course_count(); ++c)
        if (!state.won_pro(c)) return false;
    return rules.course_count() > 0;
}

bool concept_all_licenses(const ConceptRules& rules, const ConceptState& state) {
    for (int c = 0; c < rules.course_count(); ++c)
        if (!state.license_passed(c)) return false;
    return rules.course_count() > 0;
}

std::vector<ConceptGrant> concept_prize_pass(const ConceptRules& rules, const ConceptState& state, const ConceptRun& run) {
    std::vector<ConceptGrant> out;
    if (run.mode < 0 || run.course < 0) return out;
    auto add = [&](const std::string& name, int kind) {
        if (!state.present(name) && out.size() < kConceptMaxGrants) out.push_back({name, kind});
    };
    const bool race = run.mode >= 1;
    const bool win = race && state.won(run.level(), run.course);
    for (const ConceptRules::Key& k : rules.keys) {
        if (k.course != run.course) continue;
        bool given = false;
        switch (k.kind) {
        case 'L': given = run.license() && (k.variant == 0 ? state.license_passed(run.course) : state.license_gold(run.course)); break;
        case 'N': given = win; break;
        case 'H': given = win && run.mode >= 2; break;
        default: break;
        }
        if (given) add(k.name, 0);
    }
    // The four specials, each once.
    if (race && concept_all_won(rules, state)) add(kConceptMovie0, 3), add(kConceptTransfer, 2);
    if (race && concept_all_won_pro(rules, state)) add(kConceptMovie1, 4);
    if (run.license() && concept_all_licenses(rules, state)) add(kConceptPodRace, 1);
    std::stable_sort(out.begin(), out.end(), [](const ConceptGrant& a, const ConceptGrant& b) { return a.kind < b.kind; });
    return out;
}

void concept_set_presents(ConceptState& state, const std::vector<ConceptGrant>& grants) {
    for (const ConceptGrant& g : grants) state.presents.insert(g.name);
}

bool concept_slot_locked(const ConceptRules& rules, const ConceptState& state, std::string_view car_class, int index) {
    for (const ConceptRules::Key& k : rules.keys)
        if (k.index == index && k.car_class == car_class && !state.present(k.name)) return true;
    return false;
}

bool concept_course_open(const ConceptState& state, int course) { return state.license_passed(course); }

float concept_completion(const ConceptRules& rules, const ConceptState& state) {
    int points = 0;
    for (int c = 0; c < rules.course_count(); ++c)
        points += int(state.won_any(c)) + int(state.won_pro(c)) + int(state.license_passed(c)) + int(state.license_gold(c));
    return std::min(1.0f, float(points) / float(kConceptCompletionPoints));
}

bool concept_status_reached(const ConceptRules& rules, const ConceptState& state, std::string_view item) {
    if (item == kConceptMovie0 || item == kConceptTransfer) return concept_all_won(rules, state);
    if (item == kConceptMovie1) return concept_all_won_pro(rules, state);
    if (item == kConceptPodRace) return concept_all_licenses(rules, state);
    const std::optional<ConceptRules::Key> k = ConceptRules::parse_key(item);
    if (!k) return false;
    switch (k->kind) {
    case 'L': return k->variant == 0 ? state.license_passed(k->course) : state.license_gold(k->course);
    case 'N': return state.won(0, k->course);
    case 'H': return state.won(1, k->course);
    default: return false;
    }
}

} // namespace gt3r::game
