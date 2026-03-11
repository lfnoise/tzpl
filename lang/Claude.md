
# Statically Typed Real-Time Interpreted Language

I would like to have an interpreter suitable for running within a real time audio thread.

There are a few files in this directory that implement the very beginnings of such a language. Some or all of it may be kept or discarded in this process of finding the best design.

## Requirements:
- It should be implemented in C++.
- It should be very much like Ocaml, in that it is functional, object oriented, and statically typed.
- It should have a real time memory allocator, such as TLSF.
- It should have a real time garbage collector. I have implemented a garbage collector that I have used previously.
- The virtual machine is singly threaded, so no atomic operations or mutexes should be needed or used internally.
- The system memory allocator should never be called.
- No system call that may block should ever be called, except for one time initialization before the virtual machine starts..
- It should be statically typed.
- Static typing infers types from source to sink, not bidirectionally.
- The virtual machine should be event driven. The virtual machine should respond to events and then the stack collapses. 
- There should be no threads or coroutines. An event based model works well for actors, though. The reason I don't want to add coroutines is because it would require atomically scanning the stack at the end of garbage collection, which is not real-time safe.
- Most data are immutable, e.g. local variables, free variables, Arrays, Maps, Tuples, etc. 
- Global variables are mutable.
- Functions may be overloaded on the static types of their arguments.
- The language supports auto-mapping of all functions. Auto-mapping means the following: If a function expects a single item for an argument, but instead receives an array or list, then the function is applied to every element of the array and an array or list is returned. 
- A postfix `@` operator can be used to force auto mapping, do Cartesian mapping, and for constructing data structures. The `@` is placed after the expression it applies to, e.g. `array @ reverse drop(2)`.
- Data items are stored in 64 bit words such as the following union:
```
	union Word {
		i64 i;
		f64 f;
		Symbol* s;
		void* p;
		Obj* o;
	};
```
- Words are untagged because types are statically known.
- The built-in types should include the following:
	* Types stored by value in a 64 bit word:
		- Bool : A boolean. stored as a 64 bit integer.
		- Int : A 64 bit integer.
		- Float : A 64 bit double precision floating point value.
		- Symbol : An interned string. Can be compare for equality by comparing pointer values.
	* Types stored by Obj pointer. 
		- String : A string of utf-8 bytes.
		- Fraction : A rational number with 64 bit numerator and denominator.
		- Float : A 64 bit double precision floating point value.
		- Array[T] : A dynamically sizable homogeneous array of values.
		- List[T] : A singly linked, possibly lazy list of values. The last node of a list may contain a generator object that can generate more values on demand.   
		- Tuple : A fixed length heterogeneous tuple of values.
		- Struct : A collection of named fields, each with a type. Structs can inherit from another struct.
		- Enum : A sum type. One of a collection of named, typed, cases.
		- Ref : A mutable reference to a value. 
		- Function : A callable value with list of argument types and a return type. There are two kinds:
			- Primitive : A built-in function.
			- Lambda : A closure that may capture some values from its lexical environment.
			- Method : A function which dispatches on the type of the receiver argument. Methods are not declared inside of the struct. The receiver argument is not in the argument list. The receiver is referred to as `this` within the method.
- All other types are built from the types listed above. Other built-in types may be added later.

## Virtual Machine
- It could be a stack or register machine.
- This could be a byte code interpreter, or a direct-threaded interpreter where opcode-functions tail-call the next opcode/function via [[clang::musttail]].
- Instructions include:
	- Categories of instructions:
		- unary and binary math operators for all numeric types and arrays, lists and tuples of numeric types.
		- load register or push values onto stack
			- load/push constant value
			- load/push value of stack variable
			- load/push value of lambda free variable
			- load/push value of global variable
			- load/push value of dynamic scope variable
			- load/push a lambda. creates a closure, makes a vector of free variables.
			- load/push a field from a struct or tuple
			- array indexing
		- construct a struct
		- construct an array
		- construct an enum case
		- pattern matching assignment. Use a pattern data structure to deconstruct a value and make assignments.
		- conditional and unconditional branches
		- call a function
		- call a function with implicit auto-mapping
		- call a function with explicit auto-mapping (use of the `@` operator)
		- return from function
		- load a module (with possible alias)
		- load all exports of a module (`load myModule : *`)
		- load names from a module (with possible aliases)
		
## Abstract Syntax Tree
- to be defined...

## Language Grammar
- Language should have syntax as described in the file Tzopilotl_by_Example.html in this directory.

## More about Auto-mapping and the `@` operator.

The following text is taken from the README of the "Tzopilotl" or "sound as pure form" language which I previously wrote. Auto-mapping and the `@` operator should work similarly in this new language. The new language will not be a postfix language like "Tzopilotl". Note that `@` is a postfix operator — it is placed after the expression it applies to (e.g. `array @` not `@ array`), so that no parentheses are needed to chain calls like `array @ reverse drop(2)`.

```
AUTO-MAPPING

    Many built-in operators which take a scalar argument will automatically map
    over a signal or stream passed in that argument position. For example, the
    "to" operator returns a sequence from a starting number to an ending number:
    
        0 4 to  -->  [0 1 2 3 4]
        
    If a list is passed as one of the arguments, then that list is auto-mapped
    and a list of lists is returned.
    
        [0 2] 4 to  -->  [[0 1 2 3 4][2 3 4]]
        
        0 [2 3 4] to  -->  [[0 1 2][0 1 2 3][0 1 2 3 4]]
        
    If lists are passed in for multiple arguments that are subject to
    auto-mapping, then they will be auto-mapped with successive values from each
    of the arguments.
    
        [0 7][2 9] to  -->  [[0 1 2][7 8 9]]
        
    When multiple arguments are auto-mapped, the result will be of the same
    length as the shortest list.
    
        [0 1][5 4 3] to  -->  [[0 1 2 3 4 5][1 2 3 4]]
        
    Auto-mapping may be performed over infinite lists. ord is a function which 
    returns an infinite list of integers starting with 1. 
        ord --> [1 2 3 4 5 ...]
    
        0 ord to  -->  [[0 1][0 1 2][0 1 2 3][0 1 2 3 4][0 1 2 3 4 5]...]

THE "EACH" OPERATOR

    Sometimes an operator needs to be applied at a deeper level than the top
    level. The @ sign, known as the "each" operator, tags the top value on the
    stack so that the next function that consumes it will operate over each of
    its values instead of the list as a whole.
    
    For example say we have the following nested list:
    
        [[1 2 3] [4 5 6]]
        
    If we reverse it we get the outer list reversed:
    
        [[1 2 3] [4 5 6]] reverse  -->  [[4 5 6] [1 2 3]]
        
    What if we want to reverse each of the inner lists? We use the each
    operator:
    
        [[1 2 3] [4 5 6]] @ reverse  -->  [[3 2 1] [6 5 4]]
    
    We can use the each operator to do outer products. Normally math operators
    proceed over lists element-wise like so:
    
        [1 2][10 20] +   -->   [11 22]
    
    If we use the each operator we can apply + to each element of one list and
    the whole other list.
    
        [1 2] @ [10 20] +  -->  [[11 21] [12 22]]
        
        [1 2] [10 20] @ +  -->  [[11 12] [21 22]]
        
    This works because math operators auto-map over lists. 
    Other operators do not auto-map over lists, for example the 2ple operator.
    2ple creates a two item list from the two items on the top of the stack.
    
        [1 2] [10 20] 2ple  -->  [[1 2] [10 20]] 

        [1 2] @ [10 20] 2ple  -->  [[1 [10 20]] [2 [10 20]]]
        
        [1 2] [10 20] @ 2ple  -->  [[[1 2] 10] [[1 2] 20]]
        
        [1 2] @ [10 20] @ 2ple --> [[1 10] [2 20]]
        
    In order to do an outer product we need to use ordered each operators. These
    perform nested loops.
    
        [1 2] @1 [10 20] @2 2ple  -->  [[[1 10] [1 20]] [[2 10] [2 20]]]
        
        [1 2] @2 [10 20] @1 2ple  -->  [[[1 10] [2 10]] [[1 20] [2 20]]]
        
        
    You can do mapping two (or more) levels deep with @@ (or @@@, @@@@, etc) :
    
        [[[1 2 3] [4 5]] [[6 7] [8 9 10]]] @@ reverse  
            -->  [[[3 2 1] [5 4]] [[7 6] [10 9 8]]]
    
    ord @1 ord @2 to   --> an infinite list of infinite lists of finite lists:
        [
            [[1] [1 2] [1 2 3] [1 2 3 4] [1 2 3 4 5] ...] 
            [[2 1] [2] [2 3] [2 3 4] [2 3 4 5] ...] 
            [[3 2 1] [3 2] [3] [3 4] [3 4 5] ...] 
            [[4 3 2 1] [4 3 2] [4 3] [4] [4 5] ...] 
            [[5 4 3 2 1] [5 4 3 2] [5 4 3] [5 4] [5] ...] 
            ...
        ]

    Lists of Forms can be constructed using the each operator.
    
    {:a ord @ :b 0}  -->  [{:a 1 :b 0} {:a 2 :b 0} {:a 3 :b 0} {:a 4 :b 0} ...]
    
    {:a 1 3 to @1  :b 1 4 to @2}  -->          ; outer product
        [
            [{:a 1  :b 1} {:a 1  :b 2} {:a 1  :b 3} {:a 1  :b 4}]
            [{:a 2  :b 1} {:a 2  :b 2} {:a 2  :b 3} {:a 2  :b 4}]
            [{:a 3  :b 1} {:a 3  :b 2} {:a 3  :b 3} {:a 3  :b 4}]
        ]
        
    Lists of lists can be created using the each operator within the list
    constructor syntax:
    
    [[1 2 3] @ 4 5] --> [[1 4 5] [2 4 5] [3 4 5]]
    
    [[1 2 3] @1 [4 5 6] @2] --> 
            [[[1 4] [1 5] [1 6]] [[2 4] [2 5] [2 6]] [[3 4] [3 5] [3 6]]]

```
