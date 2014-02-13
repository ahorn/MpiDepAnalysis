# MPI Dependency Analysis

Static analysis to speed up runtime verification of Message Passing Interface code

The Message Passing Interface (MPI) is a portable and standard way of writing highly
distributed systems. But runtime verification of MPI code is difficult due to the
nondeterminism between senders and receivers. Similar difficulties arise in other
programming paradigms with say channels (e.g. Go language).

At Oxford, we're interesting in using SAT or SMT solvers to verify distributed systems.
But the size of the generated (propositional or first-order logic) formulas can get
prohibitively large and so various techniques are needed to shrink the problem size.
Static analysis is one such technique, particularly control dependency analysis.

To illustrate this, we've created a research prototype that finds memory allocations
that directly or indirectly influence the execution of MPI send and/or receive calls.
More accurately, the analysis finds (transitive) control dependencies on MPI calls.
This is useful to prune execution paths. While the analysis for this is nothing fancy,
its application in the context of MPI runtime verification is new.

The analysis happens to be implemented in LLVM; we are not LLVM experts so any bug
reports and/or suggestions on how to improve the analysis are warmly invited. Note
that the implementation currently does not perform any alias analysis. For this,
Owen Anderson's MemoryDependencyAnalysis pass is likely a good starting point.

## Install

For convenience, here is a possible way to build LLVM and clang with
MpiDepAnalysis enabled:

    $ svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm
    $ cd llvm/tools
    $ svn co http://llvm.org/svn/llvm-project/cfe/trunk clang
    $ cd ../..
    $ cd llvm/projects
    $ svn co http://llvm.org/svn/llvm-project/compiler-rt/trunk compiler-rt
    $ cd ../..
    $ mkdir -p build/target
    $ cd llvm/lib/Analysis/
    $ git clone https://github.com/ahorn/MpiDepAnalysis.git
    $ echo "DIRS += MpiDepAnalysis" >> Makefile
    $ echo "add_subdirectory(MpiDepAnalysis)" >> CMakeLists.txt
    $ cd ../../../build
    $ CC=clang CXX=clang++ ../llvm/configure --prefix=`pwd`/target
    $ make
    $ make check
    $ make install 

You'll also need to install the MPI headers (`mpi.h`). The MPI library
itself, however, is not required for the analysis.

## Getting started

Set your `PATH` environment variable so that you can compile an MPI program
written in C.

Example:

    $ clang -emit-llvm -I</your/include/path/to/mpi> example.c -c -o example.bc

More detailed examples on how to compile code with clang and dissemble
LLVM bitcode (should the need arise) can be found [here][clang].

Now suppose we've got an LLVM bitcode file called `example.bc` of an MPI
program. Then, the analysis can be run with the following command:

    $ opt -load </your/library/path/to/LLVMMpiAnalysis> -mpidep -debug < example.bc

The variables that determine whether an MPI receive/send can execute are
reported by lines starting with `Alloca dep`. For runtime verification
purposes, these variables may contribute to things such as MPI deadlocks and
should be focused on.

[clang]: http://llvm.org/docs/GettingStarted.html#example-with-clang
