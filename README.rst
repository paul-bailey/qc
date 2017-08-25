
==
qc
==

--------------------------------------------------------------------------
A C interpreter derived from Herbert Schildt's "C: The Complete Reference"
--------------------------------------------------------------------------

This was originally a branch that was derived from an example in the book
"C: The Complete Reference" by Herbert Schildt.  It was maintained for a
while in a private repository until made public here.

This software is **AS-IS**.

QC Limitations
--------------

* QC streamlines the precedence of its binary operations. If you are
  unsure, use parentheses.
  * ``&``, ``^``, and ``|`` all have the same precedence, left to right.
  * ``&&`` and ``||`` have the same precedence, left to right.
* The ternary operator (an expression of the form 'a?b:c') is
  unsupported.
* The post- and pre- incrementers and decrementers are unsupported.
* Type casting is unsupported. Implicit casting occurs when assigning
  a value of one type to a value of another type.
* The ``sizeof`` operator is unsupported.
* The C interpreter does not check for valid pointers; this means that
  *an invalid pointer in the interpreted code can do the same kind of
  damage as an invalid pointer in the compiled code.*
* Implicit casting does not occur when passing arguments into a function
  call.
* Function pointers are not supported.
* The internal functions cannot make callbacks to user-defined functions.
* Structs are not supported.
* Array names without braces are not treated like pointers, like in
  regular C.

  .. code-block:: c

     int *a, b[]; a = b;</p>

  is invalid in QC. Instead use

  .. code-block:: c

     int *a, b[]; a= &b[0];

* Arrays are not packed to the size of the variable type they are
  declared for. Instead, they are packed to the size of a core type used
  by QC. Pointer math *always* adds or subtracts by multiples of this
  size type. If you declare an array, this should not be a problem, but
  it matters when you try to declare a pointer to a constant string.
  (TODO: This should not be.)
* No optimizations are made for temporary variables. The only variables
  that QC treats as temporary are its own, used for evaluating
  expression.

.. todo::

   The ultimate goal is to have a file-scope namespace, which gets
   loaded. So far this is not fully implemented.

The QC interpreter is not interactive. Everything must be in a function,
and functions may be called from the ???? interface. If this is
cumbersome, write a wrapper function and call that, or write a script
using the ???? interface. The latter is recommended due to the limited
size of the user-function stack.
