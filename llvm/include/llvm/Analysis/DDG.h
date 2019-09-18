//===- llvm/Analysis/DDG.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Data-Dependence Graph (DDG).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DDG_H
#define LLVM_ANALYSIS_DDG_H

#include "llvm/ADT/DirectedGraph.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DependenceGraphBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Instructions.h"
#include <unordered_map>

namespace llvm {
class DDGNode;
class DDGEdge;
using DDGNodeBase = DGNode<DDGNode, DDGEdge>;
using DDGEdgeBase = DGEdge<DDGNode, DDGEdge>;
using DDGBase = DirectedGraph<DDGNode, DDGEdge>;
class LPMUpdater;

/// Data Dependence Graph Node
/// The graph can represent the following types of nodes:
/// 1. Single instruction node containing just one instruction.
/// 2. Multiple instruction node where two or more instructions from
///    the same basic block are merged into one node.
class DDGNode : public DDGNodeBase {
public:
  using InstructionListType = SmallVectorImpl<Instruction *>;

  enum class NodeKind {
    Unknown,
    SingleInstruction,
    MultiInstruction,
  };

  DDGNode() = delete;
  DDGNode(const NodeKind K) : DDGNodeBase(), Kind(K) {}
  DDGNode(const DDGNode &N) : DDGNodeBase(N), Kind(N.Kind) {}
  DDGNode(DDGNode &&N) : DDGNodeBase(std::move(N)), Kind(N.Kind) {}
  virtual ~DDGNode() = 0;

  DDGNode &operator=(const DDGNode &N) {
    DGNode::operator=(N);
    Kind = N.Kind;
    return *this;
  }

  DDGNode &operator=(DDGNode &&N) {
    DGNode::operator=(std::move(N));
    Kind = N.Kind;
    return *this;
  }

  /// Getter for the kind of this node.
  NodeKind getKind() const { return Kind; }

  /// Collect a list of instructions, in \p IList, for which predicate \p Pred
  /// evaluates to true when iterating over instructions of this node. Return
  /// true if at least one instruction was collected, and false otherwise.
  bool collectInstructions(llvm::function_ref<bool(Instruction *)> const &Pred,
                           InstructionListType &IList) const;

protected:
  /// Setter for the kind of this node.
  void setKind(NodeKind K) { Kind = K; }

private:
  NodeKind Kind;
};

/// Subclass of DDGNode representing single or multi-instruction nodes.
class SimpleDDGNode : public DDGNode {
public:
  SimpleDDGNode() = delete;
  SimpleDDGNode(Instruction &I);
  SimpleDDGNode(const SimpleDDGNode &N);
  SimpleDDGNode(SimpleDDGNode &&N);
  ~SimpleDDGNode();

  SimpleDDGNode &operator=(const SimpleDDGNode &N) {
    DDGNode::operator=(N);
    InstList = N.InstList;
    return *this;
  }

  SimpleDDGNode &operator=(SimpleDDGNode &&N) {
    DDGNode::operator=(std::move(N));
    InstList = std::move(N.InstList);
    return *this;
  }

  /// Get the list of instructions in this node.
  const InstructionListType &getInstructions() const {
    assert(!InstList.empty() && "Instruction List is empty.");
    return InstList;
  }
  InstructionListType &getInstructions() {
    return const_cast<InstructionListType &>(
        static_cast<const SimpleDDGNode *>(this)->getInstructions());
  }

  /// Get the first/last instruction in the node.
  Instruction *getFirstInstruction() const { return getInstructions().front(); }
  Instruction *getLastInstruction() const { return getInstructions().back(); }

  /// Define classof to be able to use isa<>, cast<>, dyn_cast<>, etc.
  static bool classof(const DDGNode *N) {
    return N->getKind() == NodeKind::SingleInstruction ||
           N->getKind() == NodeKind::MultiInstruction;
  }
  static bool classof(const SimpleDDGNode *N) { return true; }

private:
  /// Append the list of instructions in \p Input to this node.
  void appendInstructions(const InstructionListType &Input) {
    setKind((InstList.size() == 0 && Input.size() == 1)
                ? NodeKind::SingleInstruction
                : NodeKind::MultiInstruction);
    InstList.insert(InstList.end(), Input.begin(), Input.end());
  }
  void appendInstructions(const SimpleDDGNode &Input) {
    appendInstructions(Input.getInstructions());
  }

  /// List of instructions associated with a single or multi-instruction node.
  SmallVector<Instruction *, 2> InstList;
};

/// Data Dependency Graph Edge.
/// An edge in the DDG can represent a def-use relationship or
/// a memory dependence based on the result of DependenceAnalysis.
class DDGEdge : public DDGEdgeBase {
public:
  /// The kind of edge in the DDG
  enum class EdgeKind { Unknown, RegisterDefUse, MemoryDependence };

  explicit DDGEdge(DDGNode &N) = delete;
  DDGEdge(DDGNode &N, EdgeKind K) : DDGEdgeBase(N), Kind(K) {}
  DDGEdge(const DDGEdge &E) : DDGEdgeBase(E), Kind(E.getKind()) {}
  DDGEdge(DDGEdge &&E) : DDGEdgeBase(std::move(E)), Kind(E.Kind) {}
  DDGEdge &operator=(const DDGEdge &E) {
    DDGEdgeBase::operator=(E);
    Kind = E.Kind;
    return *this;
  }

  DDGEdge &operator=(DDGEdge &&E) {
    DDGEdgeBase::operator=(std::move(E));
    Kind = E.Kind;
    return *this;
  }

  /// Get the edge kind
  EdgeKind getKind() const { return Kind; };

  /// Return true if this is a def-use edge, and false otherwise.
  bool isDefUse() const { return Kind == EdgeKind::RegisterDefUse; }

  /// Return true if this is a memory dependence edge, and false otherwise.
  bool isMemoryDependence() const { return Kind == EdgeKind::MemoryDependence; }

private:
  EdgeKind Kind;
};

/// Encapsulate some common data and functionality needed for different
/// variations of data dependence graphs.
template <typename NodeType> class DependenceGraphInfo {
public:
  using DependenceList = SmallVector<std::unique_ptr<Dependence>, 1>;

  DependenceGraphInfo() = delete;
  DependenceGraphInfo(const DependenceGraphInfo &G) = delete;
  DependenceGraphInfo(const std::string &N, const DependenceInfo &DepInfo)
      : Name(N), DI(DepInfo) {}
  DependenceGraphInfo(DependenceGraphInfo &&G)
      : Name(std::move(G.Name)), DI(std::move(G.DI)) {}
  virtual ~DependenceGraphInfo() {}

  /// Return the label that is used to name this graph.
  const StringRef getName() const { return Name; }

protected:
  // Name of the graph.
  std::string Name;

  // Store a copy of DependenceInfo in the graph, so that individual memory
  // dependencies don't need to be stored. Instead when the dependence is
  // queried it is recomputed using @DI.
  const DependenceInfo DI;
};

using DDGInfo = DependenceGraphInfo<DDGNode>;

/// Data Dependency Graph
class DataDependenceGraph : public DDGBase, public DDGInfo {
  friend class DDGBuilder;

public:
  using NodeType = DDGNode;
  using EdgeType = DDGEdge;

  DataDependenceGraph() = delete;
  DataDependenceGraph(const DataDependenceGraph &G) = delete;
  DataDependenceGraph(DataDependenceGraph &&G)
      : DDGBase(std::move(G)), DDGInfo(std::move(G)) {}
  DataDependenceGraph(Function &F, DependenceInfo &DI);
  DataDependenceGraph(const Loop &L, DependenceInfo &DI);
  ~DataDependenceGraph();
};

/// Concrete implementation of a pure data dependence graph builder. This class
/// provides custom implementation for the pure-virtual functions used in the
/// generic dependence graph build algorithm.
///
/// For information about time complexity of the build algorithm see the
/// comments near the declaration of AbstractDependenceGraphBuilder.
class DDGBuilder : public AbstractDependenceGraphBuilder<DataDependenceGraph> {
public:
  DDGBuilder(DataDependenceGraph &G, DependenceInfo &D,
             const BasicBlockListType &BBs)
      : AbstractDependenceGraphBuilder(G, D, BBs) {}
  DDGNode &createFineGrainedNode(Instruction &I) final override {
    auto *SN = new SimpleDDGNode(I);
    assert(SN && "Failed to allocate memory for simple DDG node.");
    Graph.addNode(*SN);
    return *SN;
  }
  DDGEdge &createDefUseEdge(DDGNode &Src, DDGNode &Tgt) final override {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::RegisterDefUse);
    assert(E && "Failed to allocate memory for edge");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }
  DDGEdge &createMemoryEdge(DDGNode &Src, DDGNode &Tgt) final override {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::MemoryDependence);
    assert(E && "Failed to allocate memory for edge");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }
};

raw_ostream &operator<<(raw_ostream &OS, const DDGNode &N);
raw_ostream &operator<<(raw_ostream &OS, const DDGNode::NodeKind K);
raw_ostream &operator<<(raw_ostream &OS, const DDGEdge &E);
raw_ostream &operator<<(raw_ostream &OS, const DDGEdge::EdgeKind K);
raw_ostream &operator<<(raw_ostream &OS, const DataDependenceGraph &G);

//===--------------------------------------------------------------------===//
// DDG Analysis Passes
//===--------------------------------------------------------------------===//

/// Analysis pass that builds the DDG for a loop.
class DDGAnalysis : public AnalysisInfoMixin<DDGAnalysis> {
public:
  using Result = std::unique_ptr<DataDependenceGraph>;
  Result run(Loop &L, LoopAnalysisManager &AM, LoopStandardAnalysisResults &AR);

private:
  friend AnalysisInfoMixin<DDGAnalysis>;
  static AnalysisKey Key;
};

/// Textual printer pass for the DDG of a loop.
class DDGAnalysisPrinterPass : public PassInfoMixin<DDGAnalysisPrinterPass> {
public:
  explicit DDGAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

private:
  raw_ostream &OS;
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for the DDG
//===--------------------------------------------------------------------===//

/// non-const versions of the grapth trait specializations for DDG
template <> struct GraphTraits<DDGNode *> {
  using NodeRef = DDGNode *;

  static DDGNode *DDGGetTargetNode(DGEdge<DDGNode, DDGEdge> *P) {
    return &P->getTargetNode();
  }

  // Provide a mapped iterator so that the GraphTrait-based implementations can
  // find the target nodes without having to explicitly go through the edges.
  using ChildIteratorType =
      mapped_iterator<DDGNode::iterator, decltype(&DDGGetTargetNode)>;
  using ChildEdgeIteratorType = DDGNode::iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &DDGGetTargetNode);
  }
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &DDGGetTargetNode);
  }

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->begin();
  }
  static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template <>
struct GraphTraits<DataDependenceGraph *> : public GraphTraits<DDGNode *> {
  using nodes_iterator = DataDependenceGraph::iterator;
  static NodeRef getEntryNode(DataDependenceGraph *DG) { return *DG->begin(); }
  static nodes_iterator nodes_begin(DataDependenceGraph *DG) {
    return DG->begin();
  }
  static nodes_iterator nodes_end(DataDependenceGraph *DG) { return DG->end(); }
};

/// const versions of the grapth trait specializations for DDG
template <> struct GraphTraits<const DDGNode *> {
  using NodeRef = const DDGNode *;

  static const DDGNode *DDGGetTargetNode(const DGEdge<DDGNode, DDGEdge> *P) {
    return &P->getTargetNode();
  }

  // Provide a mapped iterator so that the GraphTrait-based implementations can
  // find the target nodes without having to explicitly go through the edges.
  using ChildIteratorType =
      mapped_iterator<DDGNode::const_iterator, decltype(&DDGGetTargetNode)>;
  using ChildEdgeIteratorType = DDGNode::const_iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &DDGGetTargetNode);
  }
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &DDGGetTargetNode);
  }

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->begin();
  }
  static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template <>
struct GraphTraits<const DataDependenceGraph *>
    : public GraphTraits<const DDGNode *> {
  using nodes_iterator = DataDependenceGraph::const_iterator;
  static NodeRef getEntryNode(const DataDependenceGraph *DG) {
    return *DG->begin();
  }
  static nodes_iterator nodes_begin(const DataDependenceGraph *DG) {
    return DG->begin();
  }
  static nodes_iterator nodes_end(const DataDependenceGraph *DG) {
    return DG->end();
  }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_DDG_H
