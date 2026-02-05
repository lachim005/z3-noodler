#ifndef _VAR_UNION_FIND_
#define _VAR_UNION_FIND_

#include <functional>
#include <list>
#include <set>
#include <stack>
#include <map>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "params/smt_params.h"
#include "ast/arith_decl_plugin.h"
#include "ast/seq_decl_plugin.h"
#include "params/theory_str_noodler_params.h"
#include "util/scoped_vector.h"
#include "util/union_find.h"
#include "smt/smt_context.h"
#include "ast/rewriter/seq_rewriter.h"
#include "ast/rewriter/th_rewriter.h"
#include "smt/theory_str_noodler/expr_solver.h"

namespace smt::noodler {

    typedef std::vector<std::set<BasicTerm>> BasicTermEqiv;

    static bool ec_are_equal(const BasicTermEqiv& ec, const BasicTerm& t1, const BasicTerm& t2) {
        for(const auto& st: ec) {
            if(st.find(t1) != st.end() && st.find(t2) != st.end()) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Class for union-find like data structure. Allows to handle equivalence classes of 
     * z3 expressions. It is implemented naively so-far.
     * 
     */
    class var_union_find {

        // Map key -> (val -> precise)
        // precise=true means we trust the (key,val) relation without requiring
        // e-graph or LIA-based equivalence confirmation in get_equivalence_bt().
        obj_map<expr, obj_map<expr, bool>> un_find;
        arith_util& m_util_a;
        ast_manager& m;

        // Keep references to keys/values used in this structure so that expr* stored
        // in internal maps stays valid across Z3's GC.
        expr_ref_vector m_pinned;

        std::map<expr*, int> key_to_value; // -1 means key is not a numeral

    public:
        var_union_find(arith_util& m_util_a) : un_find(), m_util_a(m_util_a), m(m_util_a.get_manager()), m_pinned(m) { }

        /**
         * @brief Add new item to the equivalence
         * 
         * @param key Key of the element. Each element @p val has a key (e.g., 
         * length term) that is then used for equivalence class merging.
         * @param val Value (variable) associated with the key.
         * @param precise If true, the (key,val) relation is considered precise
         */
        void add(const expr_ref& key, const expr_ref& val, bool precise = true) {
            m_pinned.push_back(key);
            m_pinned.push_back(val);

            obj_map<expr, bool> found;
            if (this->un_find.find(key, found)) {
                bool already_precise = false;
                if (this->un_find[key].find(val, already_precise)) {
                    this->un_find[key].insert(val, already_precise || precise);
                }
                else {
                    this->un_find[key].insert(val, precise);
                }
            }
            else {
                found.insert(val, precise);
                this->un_find.insert(key, found);
            }

            // we remember the value of key now, because sometimes it happened that the expr* for key stopped being a valid expression in z3
            // and if we did the following in get_equivalence_bt() we would get segfault
            // TODO: this should be probably fixed by using var_union_find with the option to push/pop scope or something like that
            // TODO: this bug might occur even for @p val, either map it now to variable or fix it trough scopes
            rational rat;
            if(this->m_util_a.is_numeral(key, rat)) {
                key_to_value[key] = rat.get_int32();
            } else {
                key_to_value[key] = -1;
            }
        }

        /**
         * @brief Get the equivalence classes
         */
        const obj_map<expr, obj_map<expr, bool>>& get_equivalence() const {
            return this->un_find;
        }

        /**
         * @brief Get the equivalence classes where each z3 term is replaced with the 
         * equivalent BasicTerm object.
         * 
         * @return Equivalence classes consisting of BasicTerms
         */
        BasicTermEqiv get_equivalence_bt(const AutAssignment& aut_ass, smt::context& ctx, seq_util& m_util_s) const {
            std::vector<std::set<BasicTerm>> ret;
            // enodes are sometimes not precise (they do not relate terms to 
            // the same equivalence class even though they are indeed equivalent).
            int_expr_solver lia_solver(ctx.get_manager(), ctx.get_fparams());

            for (const auto& t : this->un_find) {
                int len = key_to_value.at(t.m_key);

                // Internalize key lazily only if we need equivalence checks.
                enode* key_n = nullptr;

                // Basic term in the equivalence class
                std::set<BasicTerm> st;
                for (const auto& s : t.m_value) {
                    expr* var = s.m_key;
                    bool precise = s.m_value;
                    // we consider only variables (not concatenation or other complex terms)
                    // therefore, s = variable
                    if(!is_app(var) || to_app(var)->get_num_args() != 0) {
                        continue;
                    }
                    BasicTerm bvar = util::get_variable_basic_term(var);
                    if (len != -1 && len > 1) {
                        std::set<std::pair<int, int>> aut_constr = mata::applications::strings::get_word_lengths(*aut_ass.at(bvar));
                        if (aut_constr.size() > 1 || !aut_constr.contains({len, 0})) {
                            continue;
                        }
                    }

                    // If the relation is marked precise, we accept it without
                    // e-graph root comparison or LIA fallback.
                    if (precise) {
                        st.insert(bvar);
                        continue;
                    }

                    if (!key_n) {
                        ctx.ensure_internalized(t.m_key);
                        key_n = ctx.find_enode(t.m_key);
                        if (!key_n) {
                            continue;
                        }
                    }

                    // create a term |s|
                    expr_ref len_term(m_util_s.str.mk_length(var), ctx.get_manager());
                    ctx.ensure_internalized(len_term);
                    enode* n = ctx.find_enode(len_term);
                    if (!n) {
                        continue;
                    }

                    // key and s are not in the same equivalence class. We try to resolve using LIA solver.
                    if (key_n->get_root() != n->get_root()) {
                        // Fallback: if key != |s| is UNSAT in the current arithmetic context,
                        // then key and |s| are equivalent even if the e-graph didn't merge them.
                        bool lia_implies_eq = false;
                        if (m_util_a.is_int(t.m_key) && m_util_a.is_int(len_term)) {
                            ast_manager& m = ctx.get_manager();
                            expr_ref neq(m.mk_not(m.mk_eq(t.m_key, len_term)), m);
                            lia_solver.initialize(ctx);
                            lbool res = lia_solver.check_sat(neq);
                            lia_implies_eq = (res == l_false);
                        }
                        if (!lia_implies_eq) {
                            continue;
                        }
                    }
                    st.insert(bvar);
                }
                ret.push_back(st);
            }
            return ret;
        }

    };

}

#endif