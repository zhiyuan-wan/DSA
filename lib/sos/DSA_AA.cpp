#define DEBUG_TYPE "dsa-aa"
#include "sos/DSA_AA.h"

#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
using namespace llvm;

namespace {
    STATISTIC(DSAAA_TOTAL_ANSWER,       "Total # of alias queries");
    STATISTIC(DSAAA_INCOMPLETE_SAME_NODE,    "# of queries with the DSNode that is incomplete (pointed to by two variables) in DSA");
    STATISTIC(DSAAA_INCOMPLETE_NODE,    "# of queries involving DSNode that is incomplete in DSA");
    STATISTIC(DSAAA_CANNOT_ANSWER,      "# DSA-AA consults chained AA");
    STATISTIC(DSAAA_INCOMPLETE_NODE_COUNT, "# of incomplete DSNodes in the queries");
    STATISTIC(DSAAA_TOTAL_QUERY_FUNCTIONS, "# of queried funtions *********");
}

AliasAnalysis::AliasResult DSAAA::alias(const AliasAnalysis::Location& l1, const AliasAnalysis::Location& l2)
{
    DSAAA_TOTAL_ANSWER++;
	if (l1.Size == 0 || l2.Size == 0)
		return NoAlias;
    /// ? Zhiyuan: weired, l1 & l2's locations are both instructions, in my opinion, they should be operands
	const Value* v1 = (l1.Ptr)->stripPointerCasts();
	const Value* v2 = (l2.Ptr)->stripPointerCasts();


	if (!v1->getType()->isPointerTy() || !v2->getType()->isPointerTy())
		return NoAlias;

	if (v1 == v2) return MustAlias;

    DSGraph *G1 = getGraphForValue(v1);
    DSGraph *G2 = getGraphForValue(v2);
    assert((!G1 || !G2 || G1 == G2) && "Alias query for 2 different functions?");

    const Function *func = nullptr;
    /// Zhiyuan: Debug
    if (const Instruction *I = dyn_cast<Instruction>(v1)) {
        func = I->getParent()->getParent();
    }  else if (const Argument *A = dyn_cast<Argument>(v1)) {
        func = A->getParent();
    }  else if (const BasicBlock *BB = dyn_cast<BasicBlock>(v1)) {
        func = BB->getParent();
    }
    if (func != nullptr) {
        //errs() << "[DSAAA Debug] We are in function [" << func->getName() << "].\n";
        DEBUG_QueryFunctionSet.insert(func->getName().str());
    }
    DSAAA_TOTAL_QUERY_FUNCTIONS = DEBUG_QueryFunctionSet.size();
    // Get the graph to use...
    DSGraph* G = G1 ? G1 : (G2 ? G2 : TD->getGlobalsGraph());

    const DSGraph::ScalarMapTy &GSM = G->getScalarMap();
    DSGraph::ScalarMapTy::const_iterator I = GSM.find((Value*)v1);
    if (I == GSM.end()) return NoAlias;
    DSGraph::ScalarMapTy::const_iterator J = GSM.find((Value*)v2);
    if (J == GSM.end()) return NoAlias;

    DSNode  *N1 = I->second.getNode(),  *N2 = J->second.getNode();
    unsigned O1 = I->second.getOffset(), O2 = J->second.getOffset();
    if (N1 == nullptr || N2 == nullptr) {
        // Can't tell whether anything aliases null.
        errs() << "[DSAAA DEBUG] nullptr for this value. \n";
        return AliasAnalysis::alias(l1, l2);
    }

    if (!N1->isCompleteNode() && !N2->isCompleteNode()) {
//        if (llvm::DebugFlag) {
//            errs() << "We calculate MayAlias here.\n";
//            errs() << "v1 = " << *(l1.Ptr) << "; v2 = " << *(l2.Ptr) << "\n";
//            errs() << "N1 = " << N1 << "; N2 = " << N2 << "\n";
//            errs() << "N1 complete? " << N1->isCompleteNode() << "; N2 complete? " << N2->isCompleteNode() << "\n";
//        }
        if (N1 == N2) {
            DSAAA_INCOMPLETE_SAME_NODE++;
        }
        DSAAA_INCOMPLETE_NODE++;
        DEBUG_IncompleteNodeSet.insert(N1);
        DSAAA_INCOMPLETE_NODE_COUNT = DEBUG_IncompleteNodeSet.size();
        if ( llvm::DebugFlag && func != nullptr && func->getName().str() == "BZ2_decompress") {
            errs() << "[DSAAA DEBUG] # of referrers: " << N1->getNumReferrers() << "\n";
//            errs() << "[DSAAA DEBUG] # of links: " << N1->getLinkCount() << "\n";
            N1->print(errs(), G);
            const DSScalarMap &SM = G->getScalarMap();
            int refCount = 1;
            for (DSScalarMap::const_iterator i = SM.begin(); i != SM.end(); i++) {
                if (i->second.getNode() == N1 && refCount < 240) {
                    errs() << refCount++ <<": " << *(i->first) << "\n";
                }
            }
            //exit(0);
        }
        return AliasAnalysis::alias(l1, l2);
    }

    // We can only make a judgment if one of the nodes is complete.
    if (N1->isCompleteNode() || N2->isCompleteNode()) {
        if (N1 != N2) return NoAlias;   // Completely different nodes.

        // See if they point to different offsets...  if so, we may be able to
        // determine that they do not alias...
        if (O1 != O2) {
            uint64_t V1Size = l1.Size;
            uint64_t V2Size = l2.Size;
            if (O2 < O1) {    // Ensure that O1 <= O2
                std::swap(v1, v2);
                std::swap(O1, O2);
                std::swap(V1Size, V2Size);
            }

            if (O1+V1Size <= O2) return NoAlias;
        }
    }

  /**
   * Below added by Zhiyuan
   */
//    if (N1 == N2 && N1->isCompleteNode() && N2->isCompleteNode()) return MustAlias;

//    if (llvm::DebugFlag) {
//        errs() << "We need to consult other alias analysis for better results.\n";
//        errs() << "v1 = " << *(l1.Ptr) << "; v2 = " << *(l2.Ptr) << "\n";
//        errs() << "N1 = " << N1 << "; N2 = " << N2 << "\n";
//        errs() << "N1 complete? " << N1->isCompleteNode() << "; N2 complete? " << N2->isCompleteNode() << "\n";
//    }
  /**
   * Above added by Zhiyuan
   */
   DSAAA_CANNOT_ANSWER++;

  // FIXME: we could improve on this by checking the globals graph for aliased
  // global queries...
    return AliasAnalysis::alias(l1, l2);
}

//AliasAnalysis::AliasResult DSAAA::alias(const Value *V1, unsigned V1Size,
//                                       const Value *V2, unsigned V2Size) {
//  errs() << "@@@@@@ I AM HERE calling DSAAA::alias. \n";
//  if (V1 == V2) return MustAlias;
//
//  DSGraph *G1 = getGraphForValue(V1);
//  DSGraph *G2 = getGraphForValue(V2);
//  assert((!G1 || !G2 || G1 == G2) && "Alias query for 2 different functions?");
//
//  // Get the graph to use...
//  DSGraph* G = G1 ? G1 : (G2 ? G2 : TD->getGlobalsGraph());
//
//  const DSGraph::ScalarMapTy &GSM = G->getScalarMap();
//  DSGraph::ScalarMapTy::const_iterator I = GSM.find((Value*)V1);
//  if (I == GSM.end()) return NoAlias;
//
//  DSGraph::ScalarMapTy::const_iterator J = GSM.find((Value*)V2);
//  if (J == GSM.end()) return NoAlias;
//
//  DSNode  *N1 = I->second.getNode(),  *N2 = J->second.getNode();
//  unsigned O1 = I->second.getOffset(), O2 = J->second.getOffset();
//  if (N1 == 0 || N2 == 0)
//    // Can't tell whether anything aliases null.
//    return AliasAnalysis::alias(V1, V1Size, V2, V2Size);
//
//  // We can only make a judgment if one of the nodes is complete.
//  if (N1->isCompleteNode() || N2->isCompleteNode()) {
//    if (N1 != N2)
//      return NoAlias;   // Completely different nodes.
//
//    // See if they point to different offsets...  if so, we may be able to
//    // determine that they do not alias...
//    if (O1 != O2) {
//      if (O2 < O1) {    // Ensure that O1 <= O2
//        std::swap(V1, V2);
//        std::swap(O1, O2);
//        std::swap(V1Size, V2Size);
//      }
//
//      if (O1+V1Size <= O2)
//        return NoAlias;
//    }
//  }
//
//  // FIXME: we could improve on this by checking the globals graph for aliased
//  // global queries...
//  return AliasAnalysis::alias(V1, V1Size, V2, V2Size);
//}

void DSAAA::deleteValue(llvm::Value* v) {
    InvalidateCache();
    BU->deleteValue(v);
    TD->deleteValue(v);
}

void DSAAA::copyValue(llvm::Value* from, llvm::Value* to)
{
    if (from == to) return;
    InvalidateCache();
    BU->copyValue(from, to);
    TD->copyValue(from, to);
}

// getGraphForValue - Return the DSGraph to use for queries about the specified
// value...
//
DSGraph *DSAAA::getGraphForValue(const Value *V) {
  if (const Instruction *I = dyn_cast<Instruction>(V))
    return TD->getDSGraph(*(I->getParent()->getParent()));
  else if (const Argument *A = dyn_cast<Argument>(V))
    return TD->getDSGraph(*(A->getParent()));
  else if (const BasicBlock *BB = dyn_cast<BasicBlock>(V))
    return TD->getDSGraph(*(BB->getParent()));
  return 0;
}

bool DSAAA::pointsToConstantMemory(const Location& loc, bool orLocal)
{
//	NodeIndex node = (anders->nodeFactory).getValueNodeFor(loc.Ptr);
//	if (node == AndersNodeFactory::InvalidIndex)
//		return AliasAnalysis::pointsToConstantMemory(loc, orLocal);
//
//	auto itr = (anders->ptsGraph).find(node);
//	if (itr == (anders->ptsGraph).end())
//		// Not a pointer?
//		return AliasAnalysis::pointsToConstantMemory(loc, orLocal);
//
//	const AndersPtsSet& ptsSet = itr->second;
//	for (auto const& idx: ptsSet)
//	{
//		if (const Value* val = (anders->nodeFactory).getValueForNode(idx))
//		{
//			if (!isa<GlobalValue>(val) || (isa<GlobalVariable>(val) && !cast<GlobalVariable>(val)->isConstant()))
//        		return AliasAnalysis::pointsToConstantMemory(loc, orLocal);
//		}
//		else
//		{
//			if (idx != (anders->nodeFactory).getNullObjectNode())
//				return AliasAnalysis::pointsToConstantMemory(loc, orLocal);
//		}
//	}

	return true;
}

void DSAAA::getAnalysisUsage(AnalysisUsage &AU) const
{
	AliasAnalysis::getAnalysisUsage(AU);
    AU.addRequired<TDDataStructures>(); // Uses TD Datastructures
    AU.addRequired<BUDataStructures>(); // Uses BU Datastructures
	AU.addRequired<DataLayoutPass>();
	AU.setPreservesAll();
}

void* DSAAA::getAdjustedAnalysisPointer(AnalysisID PI)
{
	if (PI == &AliasAnalysis::ID)
		return (AliasAnalysis *)this;
	return this;
}

bool DSAAA::runOnModule(Module &M)
{
	InitializeAliasAnalysis(this);

//	anders = &getAnalysis<Andersen>();
	dataLayout = &(getAnalysis<DataLayoutPass>().getDataLayout());
    TD = &getAnalysis<TDDataStructures>();
    BU = &getAnalysis<BUDataStructures>();
	return false;
}

char DSAAA::ID = 0;
static RegisterPass<DSAAA> X("dsa-aa", "Data Structure Analysis Alias Analysis", true, true);
static RegisterAnalysisGroup<AliasAnalysis> Y(X);
