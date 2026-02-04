#include "noodlification_state.h"
#include <mata/alphabet.hh>
#include <mata/nfa/algorithms.hh>
#include <optional>
#include <ranges>

// Forgive me
using namespace mata;
using namespace mata::nfa;
using namespace mata::applications::strings;
using namespace mata::applications::strings::seg_nfa;

namespace {
void unify_initial_and_final_states(const std::vector<std::shared_ptr<Nfa>>& nfas, std::unordered_set<std::shared_ptr<Nfa>>& unified_nfas) {
    for (const auto& nfa : nfas) {
        if (!unified_nfas.contains(nfa)) {
            nfa->unify_initial();
            nfa->unify_final();
            unified_nfas.insert(nfa);
        }
    }
}

Nfa concatenate_with(const std::vector<std::shared_ptr<Nfa>>& nfas, const mata::Symbol delimiter) {
    Nfa concatenation{*nfas[0]};
    for (size_t i = 1; i < nfas.size(); ++i) {
        concatenation = mata::nfa::algorithms::concatenate_eps(concatenation, *nfas[i], delimiter, true);
    }
    return concatenation;
}
}

namespace smt::noodler {

    NoodlificationState::NoodlificationState(
        const std::vector<std::shared_ptr<Nfa>>& lhs_automata,
        const std::vector<std::shared_ptr<Nfa>>& rhs_automata, const mata::nfa::ParameterMap& params) {

        //=======================
        // FROM: noodlify_for_equation
        //=======================
        if (lhs_automata.empty() || rhs_automata.empty()) {
            has_more_noodles = false;
            return;
        }

        std::unordered_set<std::shared_ptr<Nfa>> unified_nfas; // Unify each automaton only once.
        unify_initial_and_final_states(lhs_automata, unified_nfas);
        unify_initial_and_final_states(rhs_automata, unified_nfas);

        // Automata representing the left/rigth side concatenated over different epsilon transitions.
        Nfa concatenated_lhs = concatenate_with(lhs_automata, mata::nfa::EPSILON);
        Nfa concatenated_rhs = concatenate_with(rhs_automata, mata::nfa::EPSILON-1);

        auto product_pres_eps_trans{
                intersection(concatenated_lhs, concatenated_rhs, mata::nfa::EPSILON-1).trim() };

        if (product_pres_eps_trans.is_lang_empty()) {
            has_more_noodles = false;
            return;
        }
        if (mata::utils::haskey(params, "reduce")) {
            const std::string& reduce_value = params.at("reduce");
            if (reduce_value == "forward" || reduce_value == "bidirectional") {
                product_pres_eps_trans = reduce(product_pres_eps_trans);
            }
            if (reduce_value == "backward" || reduce_value == "bidirectional") {
                product_pres_eps_trans = revert(product_pres_eps_trans);
                product_pres_eps_trans = reduce(product_pres_eps_trans);
                product_pres_eps_trans = revert(product_pres_eps_trans);
            }
        }

        //=======================
        // FROM: noodlify_mult_eps
        //=======================
        const SegNfa& aut = product_pres_eps_trans;
        const std::set<mata::Symbol>& epsilons{EPSILON, EPSILON-1};

        mata::applications::strings::seg_nfa::Segmentation segmentation{ aut, epsilons };
        segments = { segmentation.get_untrimmed_segments() };

        VisitedEpsilonsCounterMap def_eps_map;
        for(const Symbol& eps : epsilons) {
            def_eps_map[eps] = 0;
        }
        def_eps_vector = process_eps_map(def_eps_map);

        unused_state = aut.num_of_states(); // get some State not used in aut
        segs_one_initial_final(segments, false, unused_state, segments_one_initial_final);

        epsilon_depths_map = { segmentation.get_epsilon_depth_trans_map() };

        for(const State& fn : segments[0].final) {
            SegItem new_item;
            if (std::shared_ptr<Nfa> seg = segments_one_initial_final[{ unused_state, fn }];
                seg->final.size() != 1 || seg->delta.num_of_transitions() > 0) { // L(seg_iter) != {epsilon}
                new_item.noodle.emplace_back(seg, def_eps_vector);
            }
            new_item.seg_id = 0;
            new_item.fin = fn;
            lifo.push_back(new_item);
        }
        /// Number of visited epsilons for each state.
        visited_eps = segmentation.get_visited_eps();
    }
    std::optional<mata::applications::strings::seg_nfa::NoodleWithEpsilonsCounter> NoodlificationState::create_next_noodle() {
        //=======================
        // FROM: noodlify_mult_eps
        //=======================
        if (segments.size() == 1) {
            std::shared_ptr<Nfa> segment = std::make_shared<Nfa>(segments[0]);
            segment->trim();
            if (segment->num_of_states() > 0) {
                return {{ {segment, def_eps_vector} }};
            } else {
                return std::nullopt;
            }
        }

        while(1) {
            if (lifo.empty()) return std::nullopt;

            SegItem item = lifo.back();
            lifo.pop_back();

            if(item.seg_id + 1 == segments.size()) {
                // check if the noodle is already there
                if (!std::ranges::any_of(
                    noodles,
                    [&](const NoodleWithEpsilonsCounter& s) { return s == item.noodle; }
                )) { noodles.push_back(item.noodle); return {item.noodle}; }
                continue;
            }

            for(const Transition& tr : epsilon_depths_map.at(item.seg_id).at(item.fin)) {
                //TODO: is the use of SparseSet here good? It may take a lot of space. Do you need constant test? Otherwise what about StateSet?
                mata::utils::SparseSet<mata::nfa::State> fins = segments[item.seg_id + 1].final; // final states of the segment
                if(item.seg_id + 1 == segments.size() - 1) { // last segment
                    fins = mata::utils::SparseSet<mata::nfa::State>({ unused_state});
                }

                for(const State& fn : fins) {
                    auto seg_iter = segments_one_initial_final.find({ tr.target, fn});
                    if(seg_iter == segments_one_initial_final.end())
                        continue;

                    SegItem new_item = item; // deep copy
                    new_item.seg_id++;
                    // do not include segments with trivial epsilon language
                    if(seg_iter->second->final.size() != 1 || seg_iter->second->delta.num_of_transitions() > 0) { // L(seg_iter) != {epsilon}
                        new_item.noodle.emplace_back(seg_iter->second, process_eps_map(visited_eps[tr.target]));
                    }
                    new_item.fin = fn;
                    lifo.push_back(new_item);
                }
            }
        }
    };
}
