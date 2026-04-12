#include "noodlification_state.h"
#include <mata/alphabet.hh>
#include <mata/nfa/algorithms.hh>
#include <optional>
#include <ranges>


namespace smt::noodler {
    using Nfa = mata::nfa::Nfa;

    unsigned int_map(const Nfa &r, unsigned il, unsigned ir) { return il * r.num_of_states() + ir; }

    Nfa full_intersection(const Nfa &l, const Nfa &r) {
        mata::nfa::Nfa i(l.num_of_states() * r.num_of_states());

        for (unsigned il = 0; il < l.num_of_states(); il++) {
            for (unsigned ir = 0; ir < r.num_of_states(); ir++) {
                auto new_state = il*r.num_of_states() + ir;
                for (auto letter : l.delta[il]) {
                    for (auto dl : l.post(il, letter.symbol)) {
                        for (auto dr : r.post(ir, letter.symbol)) {
                            auto dst = dl * r.num_of_states() + dr;
                            i.delta.add(new_state, letter.symbol, dst);
                        }
                    }
                }
            }
        }

        return i;
    }

    NoodlificationState::NoodlificationState(
        const std::vector<std::shared_ptr<Nfa>>& left,
        const std::vector<std::shared_ptr<Nfa>>& right, bool unify) {

        this->left = left;
        this->right = right;

        if (unify) {
            for (auto nfa : this->left) {
                nfa->unify_initial();
                nfa->unify_final();
            }
            for (auto nfa : this->right) {
                nfa->unify_initial();
                nfa->unify_final();
            }
        }

        this->bubbles = std::vector(left.size(), std::vector<std::optional<Bubble>>(right.size(), std::nullopt));
        DWR("ctor")
        DWR(left.size() << ":" << right.size())

        if (left.size() == 0 || right.size() == 0) {
            has_more_noodles = false;
            return;
        }
        if (left.size() == 1 && right.size() == 1) {
            auto aut = mata::nfa::intersection(*left[0], *right[0]).trim();
            if (aut.num_of_states() == 0) {
                has_more_noodles = false;
                return;
            }
            DWR("setting single noodle")
            has_single = true;
            single_noodle = {{std::make_shared<mata::nfa::Nfa>(aut), {0, 0}}};
            return;
        }

        this->has_siblings = true;
        // Create bubble
        auto bubble = std::make_shared<mata::nfa::Nfa>(full_intersection(*left[0], *right[0]));
        for (auto l : left[0]->initial) {
            for (auto r : right[0]->initial) {
                bubble->initial.insert(int_map(*right[0], l, r));
            }
        }
        bubbles[0][0] = { bubble };
        if (right.size() > 1) {
            for (unsigned i = 0; i < left[0]->num_of_states(); i++) {
                bubble->final.clear();
                for (auto f : right[0]->final) {
                        bubble->final.insert(int_map(*right[0], i, f));
                }
                auto aut = std::make_shared<mata::nfa::Nfa>(mata::nfa::Nfa(*bubble).trim());
                if (aut->num_of_states() == 0) {
                    continue;
                }
                if (aut->num_of_states() != 1 || aut->delta.num_of_transitions() > 0)
                    stack.push_back({{{aut, {0, 0}}}, i, true, 0, 0});
                else
                    stack.push_back({{}, i, true, 0, 0});
            }
        }
        if (left.size() > 1) {
            for (unsigned i = 0; i < right[0]->num_of_states(); i++) {
                bubble->final.clear();
                for (auto f : left[0]->final) {
                    bubble->final.insert(int_map(*right[0], f, i));
                }
                auto aut = std::make_shared<mata::nfa::Nfa>(mata::nfa::Nfa(*bubble).trim());
                if (aut->num_of_states() == 0) {
                    continue;
                }
                if (aut->num_of_states() != 1 || aut->delta.num_of_transitions() > 0)
                    stack.push_back({{ {aut, {0, 0}} }, i, false, 0, 0});
                else
                    stack.push_back({{}, i, false, 0, 0});
            }
        }

    }

    std::optional<mata::applications::strings::seg_nfa::NoodleWithEpsilonsCounter> NoodlificationState::create_next_noodle() {
        DWR("create_next_noodle");
        if (!has_more_noodles) {
            DWR("No more baby");
            return std::nullopt;
        }
        if (has_single) {
            DWR("Single");
            has_more_noodles = false;
            return single_noodle;
        }
        DWR("entering loop");

        while (!stack.empty()) {
            DWR("Stack size: " << stack.size())
            auto [noodle, level, crossed_right, pos_l, pos_r] = std::move(stack.back());
            stack.pop_back();

            if (crossed_right) pos_r++; else pos_l++;

            if (!bubbles[pos_l][pos_r].has_value()) {
                // Create bubble
                ProdMap map;
                auto aut = std::make_shared<mata::nfa::Nfa>(full_intersection(*left[pos_l], *right[pos_r]));
                bubbles[pos_l][pos_r] = aut;
            }
            auto& bubble = bubbles[pos_l][pos_r].value();
            bubble->initial.clear();
            if (crossed_right) {
                for (auto f : right[pos_r]->initial) {
                    bubble->initial.insert(int_map(*right[pos_r], level, f));
                }
            } else {
                for (auto f : left[pos_l]->initial) {
                    bubble->initial.insert(int_map(*right[pos_r], f, level));
                }
            }
            if (pos_l + 1 == left.size() && pos_r + 1 == right.size()) {
                bubble->final.clear();
                for (auto l : left[pos_l]->final) {
                    for (auto r : right[pos_r]->final) {
                        bubble->final.insert(int_map(*right[pos_r], l, r));
                    }
                }
                auto aut = std::make_shared<mata::nfa::Nfa>(mata::nfa::Nfa(*bubble).trim());
                if (aut->num_of_states() == 0) {
                    continue;
                }
                Noodle new_noodle{noodle};
                if (aut->num_of_states() != 1 || aut->delta.num_of_transitions() > 0)
                    new_noodle.push_back({aut, {pos_l, pos_r}});

                bool contains = false;
                for (auto s : result) {
                    bool match = true;
                    if (new_noodle.size() != s.size()) continue;
                    for (unsigned i = 0; i < new_noodle.size(); i++) {
                        if (new_noodle[i].second[0] != s[i].second[0] || new_noodle[i].second[1] != s[i].second[1]) {
                            match = false;
                            break;
                        }
                        if (!new_noodle[i].first->is_identical(*s[i].first)) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        contains =true;
                        break;
                    }
                }
                if (!contains)
                    result.push_back(new_noodle);
                // if (!set_first) {
                //     first = std::chrono::system_clock::now();
                //     set_first = true;
                // }
                DWR("Produced " << result.size() << ". noodle")
                return new_noodle;
            }
            if (pos_r + 1 < right.size()) {
                for (unsigned i = 0; i < left[pos_l]->num_of_states(); i++) {
                    bubble->final.clear();
                    for (auto f : right[pos_r]->final) {
                        bubble->final.insert(int_map(*right[pos_r], i, f));
                    }
                    auto aut = std::make_shared<mata::nfa::Nfa>(mata::nfa::Nfa(*bubble).trim());
                    if (aut->num_of_states() == 0) {
                        continue;
                    }
                    Noodle n{noodle};
                    if (aut->num_of_states() != 1 || aut->delta.num_of_transitions() > 0)
                        n.push_back({aut, {pos_l, pos_r}});
                    stack.push_back({n, i, true, pos_l, pos_r});
                }
            }
            if (pos_l + 1 < left.size()) {
                for (unsigned i = 0; i < right[pos_r]->num_of_states(); i++) {
                    bubble->final.clear();
                    for (auto f : left[pos_l]->final) {
                        bubble->final.insert(int_map(*right[pos_r], f, i));
                    }
                    auto aut = std::make_shared<mata::nfa::Nfa>(mata::nfa::Nfa(*bubble).trim());
                    if (aut->num_of_states() == 0) {
                        continue;
                    }
                    Noodle n{noodle};
                    if (aut->num_of_states() != 1 || aut->delta.num_of_transitions() > 0)
                        n.push_back({aut, {pos_l, pos_r}});
                    stack.push_back({n, i, false, pos_l, pos_r});
                }
            }
        }
        // auto end = std::chrono::system_clock::now();
        // if (!set_first) {
        //     first = end;
        // }
        // auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        // auto fdur = std::chrono::duration_cast<std::chrono::microseconds>(first - start);
        // static unsigned total = 0;
        // static unsigned total_first = 0;
        // total += dur.count();
        // total_first += fdur.count();
        // std::cerr << "total:\t" << total << "\tfirst:\t" << total_first << "\t(" << unsigned(((double)total_first / total)*100) << "%)\n";
        DWR("No more noodles")
        has_more_noodles = false;
        return std::nullopt;
    };
}
