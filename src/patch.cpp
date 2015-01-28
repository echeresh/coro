#include <iostream>
#include <cassert>
#include <vector>
#include <set>
#include <stack>
#include <cassert>

#include "PatchCommon.h"
#include "PatchCFG.h"
#include "PatchMgr.h"
#include "Snippet.h"
#include "Visitor.h"
#include "Expression.h"
#include "Immediate.h"
#include "Dereference.h"
#include "BinaryFunction.h"
#include "Variable.h"
#include "Function.h"

#include "BPatch.h"
#include "BPatch_addressSpace.h"
#include "BPatch_process.h"
#include "BPatch_binaryEdit.h"
#include "BPatch_function.h"
#include "BPatch_point.h"
#include "BPatch_flowGraph.h"
#include "BPatch_object.h"
#include "BPatch_addressSpace.h"
#include "BPatch_edge.h"
#include "BPatch_edge.h"

#include "jitasm.h"

using namespace std;
using namespace Dyninst;
using namespace Dyninst::PatchAPI;
using namespace Dyninst::SymtabAPI;
using namespace Dyninst::InstructionAPI;

typedef unsigned char byte;

const char* OMP_FUNC_NAME = "main._omp_fn.0";
const char* EXE_PATH = "test/test.out";
const char* EXE_PATH_PATCHED = "test/test_patched.out";

void replaceGOMP_paralell(BPatch_binaryEdit* appBin)
{
    BPatch_addressSpace* app = appBin;
    BPatch_image* appImage = app->getImage();    
    
    std::vector<BPatch_function*> funcs;
    appImage->findFunction("main", funcs);
    std::vector<BPatch_point*>* mainPoints = funcs[0]->findPoint(BPatch_subroutine);
    
    std::vector<BPatch_function*> funcs1;
    appImage->findFunction("GOMP_parallel_start_patched", funcs1);
    BPatch_function* GOMP_parallel_start_patched = funcs1[0];
    
    std::vector<BPatch_function*> funcs2;
    appImage->findFunction("GOMP_parallel_end_patched", funcs2);
    BPatch_function* GOMP_parallel_end_patched = funcs2[0];
    
    std::vector<BPatch_function*> funcs3;
    appImage->findFunction("omp_get_thread_num_patched", funcs3);
    BPatch_function* omp_get_thread_num_patched = funcs3[0];
    
    for (auto p : *mainPoints)
    {
        if (auto* f = p->getCalledFunction())
        {
            auto name = f->getName();
            if (name == "GOMP_parallel_start")
            {
                app->replaceFunctionCall(*p, *GOMP_parallel_start_patched);
            }
            else if (name == "GOMP_parallel_end")
            {
                app->replaceFunctionCall(*p, *GOMP_parallel_end_patched);
            }
            else if(name == OMP_FUNC_NAME)
            {
                app->removeFunctionCall(*p);
            }
        }
    }

    std::vector<BPatch_function*> funcs4;
    appImage->findFunction(OMP_FUNC_NAME, funcs4);
    std::vector<BPatch_point*>* ompPoints = funcs4[0]->findPoint(BPatch_subroutine);

    for (auto p : *ompPoints)
    {
        if (auto* f = p->getCalledFunction())
        {
            auto name = f->getName();
            if(name == "omp_get_thread_num")
            {
                app->replaceFunctionCall(*p, *omp_get_thread_num_patched);
            }
        }
    }
}

enum class NodeType
{
    BinaryFunction,
    Immediate,
    Dereference,
    RegisterAST
};

class FrontendRaw : public jitasm::Frontend
{
    boost::shared_ptr<jitasm::Mem8> src;

    void* coroYieldAddress;

    jitasm::RegID toRegID(const std::string& regName)
    {
        Reg64 reg;
        if (regName == "")
            return jitasm::RegID::Invalid();

        if (regName == "RSP")
            reg = rsp;
        else if (regName == "RBP")
            reg = rbp;
        else if (regName == "RAX")
            reg = rax;
        else if (regName == "RCX")
            reg = rcx;
        else if (regName == "RDX")
            reg = rdx;
        else
        {
            cout << "unknown register name: " << regName << endl;
            exit(1);
        }
        return reg.GetReg();
    }

public:
    FrontendRaw() = default;

    FrontendRaw(const std::string& base, const std::string& index, int64_t scale, int64_t offset,
                void* coroYieldAddress) :
        coroYieldAddress(coroYieldAddress)
    {
        auto baseRegId = toRegID(base);
        auto indexRegId = toRegID(index);
        src = boost::shared_ptr<jitasm::Mem8>(new jitasm::Mem8(jitasm::O_SIZE_64, baseRegId, indexRegId, scale, offset));
        cout << "prefetch for addr: (" << base << ", " << index << ", " << scale << ", " << offset << ")" << endl;
    }

    void InternalMain()
    {
        prefetcht0(*src);
        push(rax);
        push(rcx);
        push(rdx);
        push(rsi);
        push(rdi);
        mov(rax, jitasm::Imm64((uint64_t)coroYieldAddress));
        call(rax);
        pop(rdi);
        pop(rsi);
        pop(rdx);
        pop(rcx);
        pop(rax);
    }
};

class PrefetchSnippet : public Snippet
{
    FrontendRaw f;

public:
    PrefetchSnippet(const std::string& base, const std::string& index, int64_t scale, int64_t offset,
                    void* coroYieldAddress) :
        f(base, index, scale, offset, coroYieldAddress)
    {
    }

    virtual bool generate(Point* pt, Buffer& buf)
    {
        byte* code = (byte*)f.GetCode();
        int codeSize = f.GetCodeSize();
        for (int i = 0; i < codeSize; i++)
            buf.push_back(code[i]);
        return true;
    }
};

struct Node
{
    Expression* e;
    NodeType nodeType;

    vector<Node> children;

    Node(Expression* e, NodeType nodeType) :
        e(e), nodeType(nodeType)
    {
    }

    string str()
    {
        ostringstream oss;
        switch (nodeType)
        {
        case NodeType::BinaryFunction:
        {
            auto* bf = (BinaryFunction*)e;
            if (bf->isAdd())
            {
                oss << "add(" << children[0].str() << ", "
                    << children[1].str() << ")";
            }
            else if (bf->isMultiply())
            {
                oss << "mul(" << children[0].str() << ", "
                    << children[1].str() << ")";
            }
            else
            {
                assert(false);
            }
            break;
        }
        case NodeType::Immediate:
            oss << e->format();
            break;
        case NodeType::Dereference:
            oss << "*(" << children.front().str() << ")";
            break;
        case NodeType::RegisterAST:
            oss << e->format();
            break;
        }
        return oss.str();
    }

    boost::shared_ptr<Snippet> prefetchSnippet(void* coroYieldAddress)
    {
        if (nodeType == NodeType::Dereference)
        {
            string base;
            string index;
            int64_t scale = 0;
            int64_t offset = 0;
            auto child = children.front();
            assert(child.nodeType == NodeType::BinaryFunction);
            assert(((BinaryFunction*)child.e)->isAdd());
            
            auto child0 = child.children[0];
            auto child1 = child.children[1];
            if (child0.nodeType == NodeType::BinaryFunction)
            {
                assert(((BinaryFunction*)child0.e)->isMultiply());
                auto child00 = child0.children[0];
                auto child01 = child0.children[1];
                scale = stoull(child00.str());
                base = child1.str();
                index = child01.str();
                auto* ps = new PrefetchSnippet(base, index, scale, offset, coroYieldAddress);
                return Snippet::Ptr(ps);
            }
        }
        return Snippet::Ptr(nullptr);
    }
};

class TreeBuilderVisitor : public Visitor
{
    stack<Node> q;

public:
    virtual void visit(BinaryFunction* b)
    {
        auto op0 = q.top();
        q.pop();
        auto op1 = q.top();
        q.pop();

        Node node(b, NodeType::BinaryFunction);
        node.children.push_back(op0);
        node.children.push_back(op1);
        q.push(node);
    }

    virtual void visit(Immediate* i)
    {
        q.push(Node(i, NodeType::Immediate));
    }

    virtual void visit(RegisterAST* r)
    {
        q.push(Node(r, NodeType::RegisterAST));
    }

    virtual void visit(Dereference* d)
    {
        auto op = q.top();
        q.pop();
        Node node(d, NodeType::Dereference);
        node.children.push_back(op);
        q.push(node);
    }

    Node getRoot() const
    {
        assert(q.size() == 1);
        return q.top();
    }
};

void insertYields(BPatch_binaryEdit* appBin)
{
    BPatch_addressSpace* app = appBin;
    BPatch_image* appImage = app->getImage();

    BPatch_Vector<BPatch_function *> found_funcs;
    appImage->findFunction(OMP_FUNC_NAME, found_funcs);
    auto* func = Dyninst::PatchAPI::convert(found_funcs[0]);

    PatchMgrPtr mgr = Dyninst::PatchAPI::convert(app);
    vector<Point*> func_points;
    mgr->findPoints(Scope(func), Point::PreInsn, back_inserter(func_points));
    cout << "find points: " << func_points.size() << endl;

    std::vector<BPatch_snippet*> yieldArgs;    
    void* coroYieldAddress;

    std::vector<Variable*> ret;
    Symtab* obj;
    Symtab::openFile(obj, EXE_PATH);

    std::vector<SymtabAPI::Function*> ff;
    obj->findFunctionsByName(ff, "coroYield");
    assert(!ff.empty());
    coroYieldAddress = (void*)ff[0]->getOffset();

    for (auto* p : func_points)
    {
        auto i = p->insn();
        cout << i->format() << endl;
        auto op = i->getOperation();

        auto id = op.getID();

        //hack to identify desired instruction
        if (i->format() == string("movsxd RDX, [RCX + RDX * 4]"))
        {   
            TreeBuilderVisitor tbv;
            std::vector<InstructionAPI::Operand> operands;
            i->getOperands(operands);
            auto eptr = operands[1].getValue();
            std::vector<InstructionAPI::Expression::Ptr> children;
            eptr->getChildren(children);
            eptr->apply(&tbv);
            auto root = tbv.getRoot();
            cout << "root: " << root.str() << endl;
            auto prefetchSnippet = root.prefetchSnippet(coroYieldAddress);
            if (prefetchSnippet)
                p->pushBack(prefetchSnippet);
        }
    }
}

int main()
{
    BPatch bpatch;

    //bpatch.setTrampRecursive(true);
    //bpatch.setMergeTramp(true);

    BPatch_binaryEdit* appBin = bpatch.openBinary(EXE_PATH);

    replaceGOMP_paralell(appBin);
    insertYields(appBin);

    appBin->writeFile(EXE_PATH_PATCHED);
    return 0;
}
