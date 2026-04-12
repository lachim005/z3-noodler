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
#include <iomanip>

// #define DWR(x) std::cerr << std::setw(5) << __LINE__ << ":\t" << x << "\n";
#define DWR(x)


// using Noodle = std::vector<std::pair<std::shared_ptr<mata::nfa::Nfa>, std::vector<unsigned>>>;
using Noodle = mata::applications::strings::seg_nfa::NoodleWithEpsilonsCounter;
using MyNoodle = std::tuple<Noodle, mata::nfa::State, bool, unsigned, unsigned>;
using ProdMap = std::unordered_map<std::pair<mata::nfa::State, mata::nfa::State>, mata::nfa::State>;
using Bubble = std::shared_ptr<mata::nfa::Nfa>;

namespace smt::noodler {
    class NoodlificationState {
    protected:
        std::vector<std::vector<std::optional<Bubble>>> bubbles;
        std::vector<Noodle> result{};
        std::vector<MyNoodle> stack{};
        bool has_more_noodles = true;
        bool has_single = false;
        std::vector<std::shared_ptr<mata::nfa::Nfa>> left;
        std::vector<std::shared_ptr<mata::nfa::Nfa>> right;
        std::optional<Noodle> single_noodle = std::nullopt;
    public:
        bool has_siblings = false;
        NoodlificationState(
            const std::vector<std::shared_ptr<mata::nfa::Nfa>>& lhs_automata,
            const std::vector<std::shared_ptr<mata::nfa::Nfa>>& rhs_automata, bool unify);
        std::optional<mata::applications::strings::seg_nfa::NoodleWithEpsilonsCounter> create_next_noodle();
    };

}

#endif // !_NOODLER_NOODLIFICATION_STATE_H_
