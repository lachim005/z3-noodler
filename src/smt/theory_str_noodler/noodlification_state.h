#ifndef _NOODLER_NOODLIFICATION_STATE_H_
#define _NOODLER_NOODLIFICATION_STATE_H_

#include <mata/nfa/types.hh>
#include <memory>
#include <optional>
#include <vector>
#include <deque>
#include <mata/nfa/nfa.hh>
#include <mata/applications/strings.hh>
#include <mata/alphabet.hh>



namespace smt::noodler {
    struct SegItem {
        mata::applications::strings::seg_nfa::NoodleWithEpsilonsCounter noodle{};
        mata::nfa::State fin{};
        size_t seg_id{};
    };
    class NoodlificationState {
    protected:
        std::vector<mata::nfa::Nfa> segments;
        mata::applications::strings::seg_nfa::VisitedEpsilonsCounterVector def_eps_vector;
        mata::applications::strings::seg_nfa::Segmentation::EpsilonDepthTransitionMap epsilon_depths_map;
        std::deque<SegItem> lifo;
        mata::applications::strings::seg_nfa::VisitedEpsMap visited_eps;
        std::map<std::pair<mata::nfa::State, mata::nfa::State>, std::shared_ptr<mata::nfa::Nfa>> segments_one_initial_final;
        mata::nfa::State unused_state;
        bool has_more_noodles = true;
        std::vector<mata::applications::strings::seg_nfa::NoodleWithEpsilonsCounter> noodles;
    public:
        NoodlificationState(
            const std::vector<std::shared_ptr<mata::nfa::Nfa>>& lhs_automata,
            const std::vector<std::shared_ptr<mata::nfa::Nfa>>& rhs_automata, const mata::nfa::ParameterMap& params);
        std::optional<mata::applications::strings::seg_nfa::NoodleWithEpsilonsCounter> create_next_noodle();
    };

}

#endif // !_NOODLER_NOODLIFICATION_STATE_H_
