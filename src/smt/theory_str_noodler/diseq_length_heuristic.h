#ifndef _NOODLER_DISEQ_LENGTH_HEURISTIC_H_
#define _NOODLER_DISEQ_LENGTH_HEURISTIC_H_

#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "decision_procedure.h"
#include "formula_preprocess.h"
#include "formula.h"
#include "util.h"

namespace smt::noodler {

    class DiseqLengthModelHandler {
    private:
        AutAssignment aut_ass{};
        SubstitutionMap subst_map{};
        std::map<BasicTerm, zstring> model{};
        bool computed{false};

        /**
         * @brief Pick a word for @p var of the exact length given in @p arith_model using its automaton.
         */
        zstring assign_aut_ass_var(const BasicTerm& var, const std::map<BasicTerm, rational>& arith_model) const {
            const rational& total_length = arith_model.at(var);
            mata::nfa::Nfa sigma_length = this->aut_ass.sigma_automaton_of_length(total_length.get_int32());
            auto maybe_word = mata::nfa::intersection(sigma_length, *this->aut_ass.at(var)).get_word();
            if (!maybe_word.has_value()) {
                util::throw_error("empty NFA during the model generation");
            }
            return aut_ass.get_alphabet().get_string_from_mata_word(*maybe_word);
        }

        /**
         * @brief Expand a substitution-map variable by recursively resolving concats.
         */
        zstring assign_subst_map_var(const BasicTerm& var, const std::map<BasicTerm, rational>& arith_model) {
            const Concat& subst = this->subst_map.at(var);
            zstring res = "";
            for (const BasicTerm& term : subst) {
                zstring val = "";
                if (term.is_literal()) {
                    val = term.get_name();
                } else if (this->subst_map.contains(term)) {
                    val = assign_subst_map_var(term, arith_model);
                } else {
                    val = model.at(term);
                }
                res = res + val;
            }
            model[var] = res;
            return res;
        }

        /**
         * @brief Materialize all substitution-map variables using the arithmetic model.
         */
        void assign_subst_map_vars(const std::map<BasicTerm, rational>& arith_model) {
            for (const auto& [term, subst] : this->subst_map) {
                (void)subst;
                if (!term.is_variable()) {
                    continue;
                }
                assign_subst_map_var(term, arith_model);
            }
        }

        /**
         * @brief Assign every variable in the language assignment to some word of 
         * the required length (according to the arithmetic model).
         */
        void assign_aut_ass_vars(const std::map<BasicTerm, rational>& arith_model) {
            for (const auto& t : this->aut_ass) {
                const BasicTerm& term = t.first;
                model[term] = assign_aut_ass_var(term, arith_model);
            }
        }

    public:
        /**
         * @brief Default construction.
         */
        DiseqLengthModelHandler() = default;
        /**
         * @brief Construct with initial automata and substitution map.
         */
        DiseqLengthModelHandler(AutAssignment aut, SubstitutionMap subst)
            : aut_ass(std::move(aut)), subst_map(std::move(subst)) {}

        /**
         * @brief Refresh stored data and drop cached models.
         */
        void update_data(AutAssignment aut, SubstitutionMap subst) {
            this->aut_ass = std::move(aut);
            this->subst_map = std::move(subst);
            this->model.clear();
            this->computed = false;
        }

        /**
         * @brief Build all models based on the provided arithmetic model.
         */
        void compute_model(const std::map<BasicTerm, rational>& arith_model) {
            this->model.clear();
            assign_aut_ass_vars(arith_model);
            assign_subst_map_vars(arith_model);
            this->computed = true;
        }

        /**
         * @brief Return the cached model for @p var, computing the cache if needed.
         */
        zstring get_model(const BasicTerm& var, const std::map<BasicTerm, rational>& arith_model) {
            if (!this->computed) {
                compute_model(arith_model);
            }
            return model.at(var);
        }
    };

    /**
     * @brief Heuristic decision procedure for instances containing only word disequations
     * (and possibly regex memberships handled elsewhere). It builds a length-only
     * constraint requiring each disequation to have different lengths.
     */
    class DiseqLengthHeuristicProcedure : public AbstractDecisionProcedure {
        Formula diseq_formula;
        std::unordered_set<BasicTerm> length_sensitive_vars;
        AutAssignment aut_ass;
        const theory_str_noodler_params& m_params;
        SubstitutionMap subst_map{};
        DiseqLengthModelHandler model_handler{};

        // the length formula from preprocessing, get_lengths should create conjunct with it
        LenNode preprocessing_len_formula = LenNode(LenFormulaType::TRUE,{});

    public:
        /**
         * Initialize the disequation-length heuristic for formulas containing only disequations.
         *
         * @param diseq_formula Formula containing only disequations
         * @param aut_ass Automaton assignment for variables in the formula
         * @param params Parameters for Noodler string theory
         */
        DiseqLengthHeuristicProcedure(Formula diseq_formula, AutAssignment aut_ass, const theory_str_noodler_params& params)
            : diseq_formula(std::move(diseq_formula)), aut_ass(std::move(aut_ass)), m_params(params) {
            auto vars = this->diseq_formula.get_vars();
            length_sensitive_vars.insert(vars.begin(), vars.end());
            model_handler.update_data(this->aut_ass, this->subst_map);
        }

        /**
         * Do lightweight preprocessing: propagate eps/variables, drop trivial constraints, refresh state, and detect trivial UNSAT.
         *
         * @param opt Preprocess option (ignored)
         * @param len_eq_vars Equivalence classes of variables with equal length (ignored)
         * @return lbool representing whether preprocessing solved the formula
         */
        lbool preprocess(PreprocessType, const BasicTermEqiv&) override {
            FormulaPreprocessor prep_handler(this->diseq_formula, this->aut_ass, this->length_sensitive_vars, m_params, {});

            prep_handler.propagate_eps();
            prep_handler.propagate_variables();
            prep_handler.remove_trivial();

            this->diseq_formula = prep_handler.get_modified_formula();
            this->aut_ass = prep_handler.get_aut_assignment();
            this->length_sensitive_vars = prep_handler.get_len_variables();
            this->preprocessing_len_formula = prep_handler.get_len_formula();
            this->subst_map = prep_handler.get_substitution_map();
            
            if (this->diseq_formula.get_predicates().size() > 0) {
                this->aut_ass.reduce();
            }

            this->model_handler.update_data(this->aut_ass, this->subst_map);

            if (prep_handler.contains_unsat_eqs_or_diseqs()) {
                return l_false;
            }
            if(!this->aut_ass.is_sat()) {
                return l_false;
            }

            return l_undef;
        }

        /**
         * Initialize the computation (no-op, heuristic is single-shot).
         */
        void init_computation() override {}

        /**
         * Compute next solution (returns l_false because the heuristic has no worklist).
         */
        lbool compute_next_solution() override { return l_false; }

        /**
         * Get initial length constraints (only true for this heuristic).
         */
        LenNode get_initial_lengths(bool) override { return LenNode(LenFormulaType::TRUE); }

        /**
         * Get length constraints ensuring each disequation has different lengths.
         *
         * @return Pair of length node and precision (always precise)
         */
        std::pair<LenNode, LenNodePrecision> get_lengths() override {
            std::vector<LenNode> conjuncts;
            conjuncts.emplace_back(this->preprocessing_len_formula);

            for (const auto& t : this->aut_ass) {
                BasicTerm term = t.first;
                conjuncts.emplace_back(LenFormulaType::LEQ, std::vector<LenNode>{LenNode(0), LenNode(term)});
                conjuncts.emplace_back(this->aut_ass.get_lengths(term));
            }
            for (const auto& diseq : diseq_formula.get_predicates()) {
                conjuncts.push_back(diseq.get_formula_eq()); // produces |lhs| != |rhs|
            }
            return {LenNode(LenFormulaType::AND, std::move(conjuncts)), LenNodePrecision::PRECISE};
        }

        /**
         * Get variables whose lengths may influence the model of any string variable.
         *
         * @param str_var String variable for which the model is requested
         * @return Vector of variables affecting length constraints
         */
        std::vector<BasicTerm> get_len_vars_for_model(const BasicTerm& str_var) override {
            (void)str_var;
            return std::vector<BasicTerm>(length_sensitive_vars.begin(), length_sensitive_vars.end());
        }

        /**
         * Get string model for the variable @p var using the arithmetic model.
         *
         * @param var String variable whose model is requested
         * @param arith_model Maps variables to their lengths in the arithmetic model
         * @return Word of the requested length accepted by the automaton, empty string on failure
         */
        zstring get_model(BasicTerm var, const std::map<BasicTerm, rational>& arith_model) override {
            return this->model_handler.get_model(var, arith_model);
        }

        /**
         * @brief Get the length sensitive variables collected during preprocessing
         */
        const std::unordered_set<BasicTerm>& get_init_length_sensitive_vars() const override {
            return length_sensitive_vars;
        }

        /**
         * Check whether the instance is suitable for this heuristic (contains only disequations).
         *
         * @param instance Formula to check
         * @param aut_assignment Automaton assignment of the formula
         * @return True iff the instance contains only disequations after preprocessing
         */
        static bool is_suitable(const Formula& instance, const AutAssignment& aut_assignment) {
            FormulaPreprocessor prep_handler(instance, aut_assignment, {}, theory_str_noodler_params(), {});
            prep_handler.propagate_eps();
            prep_handler.propagate_variables();
            prep_handler.remove_trivial();

            const Formula& pre_f = prep_handler.get_modified_formula();

            if (pre_f.contains_pred_type(PredicateType::Equation)
                || pre_f.contains_pred_type(PredicateType::Transducer)
                || pre_f.contains_pred_type(PredicateType::NotContains)
                || !pre_f.contains_pred_type(PredicateType::Inequation)) {
                return false;
            }

            return true;
        }

    };
}

#endif
