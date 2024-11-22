.. title:: clang-tidy - bugprone-unsequenced-global-accesses

bugprone-unsequenced-global-accesses
====================================

Finds unsequenced actions (i.e. unsequenced write and read/write)
on global variables nested in functions in the same translation unit.

Modifying twice or reading and modifying a memory location without a
defined sequence of the operations is either undefined behavior or has
unspecified order. This check is similar to the `-Wunsequenced` Clang warning,
however it only looks at global variables and therefore can find unsequenced
actions recursively inside function calls as well.

For example code-block:: c++

    int a = 0;
    int b = (a++) - a; // This is flagged by -Wunsequenced.

However global variables allow for more complex scenarios that
`-Wunsequenced` doesn't detect. E.g. code-block:: c++

    int globalVar = 0;

    int incFun() {
      globalVar++;
      return globalVar;
    }

    int main() {
      return globalVar + incFun(); // This is not detected by -Wunsequenced.
    }

This check attempts to detect such cases. It recurses into functions that are
inside the same translation unit. The flagged cases can overlap with
`-Wunsequenced`. Global unions and structs are also handled.
For example code-block:: c++
    
    typedef struct {
        int A;
        float B;
    } IntAndFloat;

    IntAndFloat GlobalIF;

    int globalIFGetSum() {
        int sum = GlobalIF.A + (int)GlobalIF.B;
        GlobalIF = (IntAndFloat){};
        return sum;
    }

    int main() {
        // The following printf could give different results on different
        // compilers.
        printf("sum: %i, int: %i", globalIFGetSum(), GlobalIF.A);
    }

Options
~~~~~~~

.. option:: HandleMutableFunctionParametersAsWrites

When `true`, treat function calls with mutable reference or pointer parameters
as writes to the parameter.

The default value is `false`.

For example, the following code block will get flagged if
`HandleMutableFunctionParametersAsWrites` is `true` code-block:: c++

    void func(int& a);
    int globalVar;

    int main() {
        int a = globalVar + func(globalVar);
        // func could write to globalVar here
    }

When `HandleMutableFunctionParametersAsWrites` is set to `true`, the
``func(globalVar)`` call is treated as a write to `globalVar`. Because no
sequencing is defined for the `+` operator, a write to `globalVar`
inside `func`, would be undefined behavior.

When `HandleMutableFunctionParametersAsWrites` is set to `false` the expression
does not get flagged as the call expression is only treated as a read from
`globalVar`.
