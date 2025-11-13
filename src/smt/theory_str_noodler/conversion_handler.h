#ifndef _NOODLER_CONVERSION_HANDLER_H_
#define _NOODLER_CONVERSION_HANDLER_H_

#include "solving_state.h"

namespace smt::noodler {

    class ConversionHandler {
        std::vector<TermConversion> conversions;
        unsigned underapprox_length;

        SolvingState solution;
        /// The set of vars s_i, such that there exists "c = to_code(s)" or "s = from_code(c)" in conversions, where s is substituted by s_1 ... s_i ... s_n in the solution.
        std::set<BasicTerm> code_subst_vars;
        /// The set of vars s_i, such that there exists "i = to_int(s)" or "s = from_int(i)" in conversions, where s is substituted by s_1 ... s_i ... s_n in the solution.
        std::set<BasicTerm> int_subst_vars;
        /// The set of vars s_i, such that there exists "r = to_real(s)" or "s = from_real(r)" in conversions, where s is substituted by s_1 ... s_i ... s_n in the solution.
        std::set<BasicTerm> real_subst_vars;

        // Special automata used for computing int and real conversions :
        // only_digits is automaton representing all valid inputs for integers (they contain only digits)
        // - we also keep empty word, because we will use it for substituted vars, and one of them can be empty, while other has only digits (for example s1="45", s2="" but s=s1s2 = "45" is valid)
        const mata::nfa::Nfa only_digits;
        // automaton representing all real numbers (two numbers divided by ".")
        // - we also keep the word ".", because the substituted var could be surrounded by other vars that will represent the whole/decimal part
        const mata::nfa::Nfa real_numbers;
        // The following two automata also depend on the alphabet in solution
        // automaton representing all non-valid inputs for integers (contains some non-digit)
        mata::nfa::Nfa contain_non_digit;
        // automaton representing all non-valid (real/integer) numbers
        mata::nfa::Nfa non_number;

        unsigned max_length_of_word_in_aut(const mata::nfa::Nfa aut, LenNodePrecision& precision) {
            if (aut.is_acyclic()) {
                // there is a finite number of words in aut => the longest possible word is aut.num_of_states()-1
                return aut.num_of_states()-1;
            } else {
                // there is infinite number of such words => we need to underapproximate
                STRACE(str_conversion, tout << "infinite NFA for which we need to do underapproximation:" << std::endl << aut << std::endl;);
                precision = LenNodePrecision::UNDERAPPROX;
                return underapprox_length;
            }
        };

        /**
         * @brief Get the formula for to_code/from_code substituting variables
         * 
         * It basically succinctly encodes `code_version_of(s) = to_code(w_s)` for each s in @p code_subst_vars and w_c \in solution.aut_ass.at(s) while
         * keeping the correspondence between |s| and |w_s|
         */
        LenNode get_formula_for_code_subst_var(const BasicTerm& code_subst_var);

        /**
         * @brief Get the formula encoding that arithmetic variable @p var is any of the numbers encoded by some interval word from @p interval_words
         */
        LenNode encode_interval_words(const BasicTerm& var, const std::vector<IntervalWord>& interval_words);

        /**
         * TODO fix comment to newest version!!!!!!
         * @brief Get the formula for to_int/from_int substituting variables
         * 
         * It basically succinctly encodes `int_version_of(s) = to_int(w_s)` for each s in @p int_subst_vars and w_s \in solution.aut_ass.at(s) while
         * keeping the correspondence between |s|, |w_s|, and code_version_of(s).
         * Note that for w_s = "", we do not put int_version_of(s) = -1 but we instead force that it is NOT -1 (so that get_formula_for_int_conversion
         * can handle this case correctly).
         * 
         * @param int_subst_vars to_int/from_int substituting variables for which we create formulae
         * @param code_subst_vars to_code/from_code substituting variables (needed only if int_subst_vars and code_subst_vars are not disjoint)
         * @param[out] int_subst_vars_to_possible_valid_lengths will map each var from int_subst_vars into a vector of lengths of all possible numbers for var (also 0 if there is empty string)
         * @param underapproximating_length For the case that we need to underapproximate, this variable sets the length up to which we underapproximate
         * @return The formula + precision of the formula (can be precise or underapproximation)
         */
        std::pair<LenNode, LenNodePrecision> get_formula_for_int_real_subst_var(const BasicTerm& int_real_subst_var, std::map<BasicTerm,std::vector<unsigned>>& int_subst_vars_to_possible_valid_lengths, std::map<BasicTerm,std::vector<std::pair<unsigned,unsigned>>>& real_subst_vars_to_possible_valid_lengths_and_dot_positions);

        // TODO!!!!
        LenNode get_formula_for_number_conversion(BasicTerm result, const std::vector<BasicTerm>& subst_vars, std::vector<unsigned> lengths_of_subst_vars, std::optional<std::pair<size_t,unsigned>> dot_position);

        /**
         * @brief Get the formula encoding to_code/from_code conversion
         */
        LenNode get_formula_for_code_conversion(const TermConversion& conv);

        /**
         * TODO fix comment!!!!!
         * @brief Get the formula encoding to_int/from_int conversion
         * 
         * @param int_subst_vars_to_possible_valid_lengths maps each var from int_subst_vars into a vector of lengths of all possible numbers for var (also 0 if there is empty string)
         */
        LenNode get_formula_for_int_real_conversion(const TermConversion& conv, const std::map<BasicTerm,std::vector<unsigned>>& int_subst_vars_to_possible_valid_lengths, const std::map<BasicTerm,std::vector<std::pair<unsigned,unsigned>>>& real_subst_vars_to_possible_valid_lengths_and_dot_positions);

    public:
        ConversionHandler(std::vector<TermConversion> conversions, unsigned underapprox_length) : conversions(conversions), underapprox_length(underapprox_length), only_digits(AutAssignment::digit_automaton_with_epsilon()), real_numbers(AutAssignment::decimal_automaton()) {
        };

        /// Gets a solution for which we want to compute the LIA formula (can be called multiple times to get formula for different solutions)
        void initialize_solution(SolvingState solution);

        /// Gets a LIA formula encoding the conversions based on a given solution from initialize_solution
        std::pair<LenNode, LenNodePrecision> get_formula_encoding_conversions(const std::set<BasicTerm> subst_vars_not_needed_to_be_handled);

        const std::set<BasicTerm>& get_code_subst_vars() const {
            return code_subst_vars;
        }

        const std::set<BasicTerm>& get_int_subst_vars() const {
            return int_subst_vars;
        }

        const std::set<BasicTerm>& get_real_subst_vars() const {
            return real_subst_vars;
        }

        bool is_var_code_subst_var(const BasicTerm& var) const {
            return code_subst_vars.contains(var);
        }

        bool is_var_int_subst_var(const BasicTerm& var) const {
            return int_subst_vars.contains(var);
        }

        bool is_var_real_subst_var(const BasicTerm& var) const {
            return real_subst_vars.contains(var);
        }

        std::set<BasicTerm> get_string_vars_in_conversions() const {
            std::set<BasicTerm> conv_string_vars;
            for (const auto &conv : conversions) {
                conv_string_vars.insert(conv.string_var);
            }
            return conv_string_vars;
        }

        const std::vector<TermConversion> get_conversions() const {
            return conversions;
        }

        /**
         * @brief Adds a conversion @p conversion to the list of conversions handled by this ConversionHandler
         * 
         * WARNING: It is assumed that after initialize_solution is called, this function will never be called
         */
        void add_conversion(TermConversion conversion) {
            conversions.push_back(conversion);
        }

        /// Are there any conversions this ConversionHandler is handling?
        bool are_there_any_conversions() const {
            return !conversions.empty();
        }

        /**
         * Returns the code var version of @p var used to encode to_code/from_code in get_formula_for_conversions
         */
        BasicTerm code_version_of(const BasicTerm& var) {
            return BasicTerm(BasicTermType::Variable, var.get_name() + "!to_code");
        }

        /**
         * Returns the int var version of @p var used to encode to_int/from_int in get_formula_for_conversions
         */
        BasicTerm int_version_of(const BasicTerm& var) {
            return BasicTerm(BasicTermType::Variable, var.get_name() + "!to_int");
        }

        /**
         * Returns the variable for @p var encoding the decimal separator position in @p var
         */
        BasicTerm dot_position_of(const BasicTerm& var) {
            return BasicTerm(BasicTermType::Variable, var.get_name() + "!dot_position");
        }

        /**
         * Returns the variable for @p var encoding the whole part before the decimal separator in @p var
         */
        BasicTerm whole_part_of(const BasicTerm& var) {
            return BasicTerm(BasicTermType::Variable, var.get_name() + "!whole_part");
        }

        /**
         * Returns the variable for @p var encoding the decimal part after the decimal separator in @p var
         */
        BasicTerm decimal_part_of(const BasicTerm& var) {
            return BasicTerm(BasicTermType::Variable, var.get_name() + "!decimal_part");
        }

        std::vector<BasicTerm> get_arith_vars_needed_for_model() {
            std::vector<BasicTerm> needed_vars;
            for (const BasicTerm& code_subst_var : code_subst_vars) {
                needed_vars.push_back(code_version_of(code_subst_var));
            }
            for (const BasicTerm& int_subst_var : int_subst_vars) {
                needed_vars.push_back(int_version_of(int_subst_var));
            }
            for (const BasicTerm& real_subst_var : real_subst_vars) {
                needed_vars.push_back(dot_position_of(real_subst_var));
                needed_vars.push_back(whole_part_of(real_subst_var));
                needed_vars.push_back(decimal_part_of(real_subst_var));
                if (!int_subst_vars.contains(real_subst_var)) { //do not add int_version_of twice
                    needed_vars.push_back(int_version_of(real_subst_var));
                }
            }
            return needed_vars;
        }
    };
};

#endif
