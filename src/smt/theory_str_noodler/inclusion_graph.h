
#ifndef Z3_INCLUSION_GRAPH_H
#define Z3_INCLUSION_GRAPH_H

#include <optional>
#include <deque>
#include <algorithm>

#include "formula.h"

namespace smt::noodler {

    //----------------------------------------------------------------------------------------------------------------------------------

    class FormulaGraphNode {
    public:
        FormulaGraphNode() = delete;
        explicit FormulaGraphNode(Predicate predicate, bool is_reversed = false) : node_predicate(predicate), reversed(is_reversed) {
            SASSERT(predicate.is_transducer() || predicate.is_equation());
        }

        /**
         * @brief Get the predicate of this node (without reversing)
         */
        [[nodiscard]] const Predicate& get_predicate() const { return node_predicate; }

        /**
         * @brief Get the "real" predicate represented by this node, taking into account whether it is reversed
         */
        [[nodiscard]] Predicate get_real_predicate() const {
            if (!is_reversed()) {
                return node_predicate;
            } else {
                return node_predicate.get_switched_sides_predicate();
            }
        }
        [[nodiscard]] bool is_reversed() const { return reversed; }

        /**
         * @brief Get the "real" left side of this node, taking into account whether it is reversed
         */
        [[nodiscard]] const std::vector<BasicTerm>& get_real_left_side() const {
            if (node_predicate.is_transducer()) {
                // for trasnducer, left side is output
                if (!is_reversed()) {
                    return node_predicate.get_output();
                } else {
                    return node_predicate.get_input();
                }
            } else {
                // for inclusions, left side is left side
                if (!is_reversed()) {
                    return node_predicate.get_left_side();
                } else {
                    return node_predicate.get_right_side();
                }
            }
        }

        /**
         * @brief Get the "real" right side of this node, taking into account whether it is reversed
         */
        [[nodiscard]] const std::vector<BasicTerm>& get_real_right_side() const {
            if (node_predicate.is_transducer()) {
                // for predicate, right side is input
                if (!is_reversed()) {
                    return node_predicate.get_input();
                } else {
                    return node_predicate.get_output();
                }
            } else  {
                // for inclusions, right side is right side
                if (!is_reversed()) {
                    return node_predicate.get_right_side();
                } else {
                    return node_predicate.get_left_side();
                }
            }
        }

        /**
         * @brief Checks if this node is reversion of @p other node
         */
        [[nodiscard]] bool is_reverse_of(const FormulaGraphNode& other) const {
            return (node_predicate == other.node_predicate && reversed != other.reversed);
        }

        /**
         * @brief Get the reversed node of this node
         */
        [[nodiscard]] FormulaGraphNode get_reversed() const {
            return FormulaGraphNode(get_predicate(), !reversed);
        }

        [[nodiscard]] std::string print() const {
            std::ostringstream output;
            output << get_real_left_side();
            if (node_predicate.is_equation()) {
                // inclusion
                output << " ⊆ " << get_real_right_side();
            } else {
                //transducer, we name them based on the raw pointer
                output << " = T" << node_predicate.get_transducer().get();
                if (is_reversed()) {
                    output << "^-1";
                }
                output << "(" << get_real_right_side() << ")";
            }
            return output.str();
        }

        auto operator<=>(const FormulaGraphNode& other) const = default;

    private:
        Predicate node_predicate;
        bool reversed = false;
    }; // Class FormulaGraphNode.

    class FormulaGraph {
    public:
        using Nodes = std::vector<FormulaGraphNode>;
        using NodeSet = std::set<FormulaGraphNode>;
        using NodeIdx = unsigned;
        using NodeIdxSet = std::set<NodeIdx>;
        using Edges = std::vector<NodeIdxSet>;
    private:
        Nodes nodes;
        Edges edges;
        Edges inverse_edges;
        NodeIdx counter = 0;
        static const NodeSet empty_nodes;
        static const NodeIdxSet empty_indices;
        // set of nodes that are NOT on some cycle
        // it is guaranteed to be correct ONLY after creating inclusion graph???
        NodeSet nodes_not_on_cycle;

    public:

        const Nodes& get_nodes() const { return nodes; }

        NodeIdx get_index_of(const FormulaGraphNode& node) const {
            for (NodeIdx i = 0; i < nodes.size(); i++) {
                if (nodes[i] == node) return i;
            }
            return nodes.size();
        }

        bool contains_node(const FormulaGraphNode& node) const {
            for (const FormulaGraphNode& node_of_graph : nodes) {
                if (node == node_of_graph) {
                    return true;
                }
            }
            return false;
        }

        void add_edge(const FormulaGraphNode& source, const FormulaGraphNode& target) {
            auto src_idx = get_index_of(source);
            auto target_idx = get_index_of(target);
            edges.at(src_idx).insert(target_idx);
            inverse_edges.at(target_idx).insert(src_idx);
        }

        void remove_edge(const FormulaGraphNode& source, const FormulaGraphNode& target) {
            auto src_idx = get_index_of(source);
            auto target_idx = get_index_of(target);
            edges.at(src_idx).erase(target_idx);
            inverse_edges.at(target_idx).erase(src_idx);
        }

        void remove_edges_from(const FormulaGraphNode& source) {
            auto src_idx = get_index_of(source);
            for (NodeIdx target : get_edge_indices_from(source)) {
                inverse_edges.at(target).erase(src_idx);
            }
            edges.at(src_idx).clear();
        }

        void remove_edges_to(const FormulaGraphNode& target) {
            auto target_idx = get_index_of(target);
            for (NodeIdx source : get_edge_indices_to(target)) {
                edges.at(source).erase(target_idx);
            }
            inverse_edges.at(target_idx).clear();
        }

        void remove_edges_with(const FormulaGraphNode& node) {
            remove_edges_from(node);
            remove_edges_to(node);
        }

        void remove_all_edges() {
            edges.clear();
            inverse_edges.clear();
        }

        size_t get_num_of_edges() const {
            size_t num_of_edges{ 0 };
            for (const auto& edge_set: edges) {
                num_of_edges += edge_set.size();
            }
            return num_of_edges;
        }

        const Edges& get_edges() const { return edges; }

        void substitute_edges(Nodes new_nodes) {
            nodes = new_nodes;
        }

        const NodeSet get_edges_from(const FormulaGraphNode& source) const {
            SASSERT(contains_node(source));
            auto src_idx = get_index_of(source);
            if (src_idx != nodes.size()) {
                NodeIdxSet edge_indices = edges.at(src_idx);
                NodeSet res;
                for (NodeIdx i : edge_indices) {
                    res.insert(nodes[i]);
                }
                return res;
            } else {
                return empty_nodes;
            }
        }

        const NodeIdxSet& get_edge_indices_from(const FormulaGraphNode& source) const {
            SASSERT(contains_node(source));
            auto src_idx = get_index_of(source);
            if (src_idx != nodes.size()) {
                return edges.at(src_idx);
            } else {
                return empty_indices;
            }
        }

        const NodeSet get_edges_to(const FormulaGraphNode& target) const {
            SASSERT(contains_node(target));
            auto target_idx = get_index_of(target);
            if (target_idx != nodes.size()) {
                NodeIdxSet edge_indices = inverse_edges.at(target_idx);
                NodeSet res;
                for (NodeIdx i : edge_indices) {
                    res.insert(nodes[i]);
                }
                return res;
            } else {
                return empty_nodes;
            }
        }

        const NodeIdxSet& get_edge_indices_to(const FormulaGraphNode& target) const {
            SASSERT(contains_node(target));
            auto target_idx = get_index_of(target);
            if (target_idx != nodes.size()) {
                return inverse_edges.at(target_idx);
            } else {
                return empty_indices;
            }
        }

        /**
         * @brief Adds node with @p predicate to graph (even if a node with such predicate exists in graph).
         * 
         * @return The node that was added 
         */
        const FormulaGraphNode& add_node(const Predicate& predicate, bool is_reversed = false) {
            nodes.push_back(FormulaGraphNode(predicate, is_reversed));
            edges.push_back({});
            inverse_edges.push_back({});
            return nodes.back();
        }

        /**
         * @brief Removes node from the graph
         * 
         * Invalidates iterator in nodes.
         */
        void remove_node(const FormulaGraphNode& node) {
            remove_node_at(get_index_of(node));
        }
        void remove_node_at(NodeIdx idx) {
            nodes.erase(nodes.begin() + idx);
            edges.erase(edges.begin() + idx);
            inverse_edges.erase(inverse_edges.begin() + idx);
            // Update indices
            for (unsigned i = 0; i < edges.size(); i++) {
                NodeIdxSet set = edges[i];
                NodeIdxSet new_edges{};
                for (auto i : set) {
                    if (i == idx) {
                        continue;
                    }
                    if (i > idx) {
                        new_edges.insert(i - 1);
                        continue;
                    }
                    new_edges.insert(i);
                }
                edges[i] = new_edges;
            }
            for (unsigned i = 0; i < inverse_edges.size(); i++) {
                NodeIdxSet set = inverse_edges[i];
                NodeIdxSet new_edges{};
                for (auto i : set) {
                    if (i == idx) {
                        continue;
                    }
                    if (i > idx) {
                        new_edges.insert(i - 1);
                        continue;
                    }
                    new_edges.insert(i);
                }
                inverse_edges[i] = new_edges;
            }
        }

        bool is_on_cycle(const FormulaGraphNode& node) const {
            SASSERT(contains_node(node));
            return (nodes_not_on_cycle.count(node) == 0);
        }

        /**
         * @brief Check if the inclusion graph is cyclic.
         * 
         * @return true <-> the inclusion graph contains a cycle.
         */
        bool is_cyclic() const {
            return this->nodes.size() != this->nodes_not_on_cycle.size();
        }

        void set_is_on_cycle(const FormulaGraphNode& node, bool is_on_cycle) {
            SASSERT(contains_node(node));
            if (!is_on_cycle) {
                nodes_not_on_cycle.insert(node);
            }
        }

        // adds edges so that inclusions x and y where left side of x shares variable with right side of y have edge from x to y
        void add_inclusion_graph_edges();

        /**
         * @brief Create a simplified splitting graph for @p formula
         * 
         * The set of nodes will have the same order as the predicates in the formula
         * where reversed node will always follow the normal one.
         * 
         * Assumes that formula does not contain same equalities (with swapped sides).
         * 
          * @param formula must contain only equations and transducers
          * @return splitting graph
          */
        static FormulaGraph create_simplified_splitting_graph(const Formula& formula);

        /**
         * @brief Create an inclusion graph from splitting graph
         */
        static FormulaGraph create_inclusion_graph(FormulaGraph& simplified_splitting_graph);


        /**
         * @brief Create an inclusion graph for @p formula
         * 
         * The set of nodes will be ordered compatible with the topological
         * order of the strongly connected components.
         * 
         * Assumes that formula does not contain same equalities (with swapped sides).
         * 
         * @param formula must contain only equations and transducers
         * @return the inclusion graph 
         */
        static FormulaGraph create_inclusion_graph(const Formula& formula);

        /**
         * Print the inclusion graph in a DOT format.
         * @param output_stream Stream to print the graph to.
         */
        void print_to_dot(std::ostream &output_stream) const;
    }; // Class FormulaGraph.
}


#endif //Z3_INCLUSION_GRAPH_H
