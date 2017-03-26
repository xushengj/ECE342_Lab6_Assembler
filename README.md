# ECE342_Lab6_Assembler
Assembler for 2017 Winter Session ECE342 Lab6 (@ University of Toronto)

This is a small assembler for ECE342 Lab6 processor. It reads plain text assembly code from stdin and write [.mif file](http://quartushelp.altera.com/15.0/mergedProjects/reference/glossary/def_mif.htm) to stdout. Use redirection to read/write to file. Compile with at least C++11.

The processor is 16 bits, and the instructions are 9 bits wide. By default the instruction is in the upper 9 bits of 16bit word. The width of mif file is hardcoded as 16 bits.

The processor supports following instructions:

| Mnemonic, Argument1, Argument2 | Effect |
| :--- | :--- |
| mv   Rx, Ry	 | Rx=Ry |
| mvi  Rx, Imm16 | Rx=Imm16 |
| add  Rx, Ry    | Rx+=Ry |
| sub  Rx, Ry    | Rx-=Ry |
| ld   Rx, Ry    | Rx=mem[Ry] |
| st   Rx, Ry    | mem[Ry]=Rx	**Notice the write direction |
| mvnz Rx, Ry    | Rx=Ry if last add/sub gives non-zero result |

Instructions will be in the form of IIIXXXYYY0000000, where:

| Field | Description |
| :--- | :--- |
| III | the opcode for instruction |
| XXX | the operand Rx |
| YYY | the operand Ry. Zero if the second operand is Imm16 |

opcode for each instruction:

| Mnemonic | Opcode |
| :--- | :--- |
| mv | 000 |
| mvi | 001 |
| add | 010 |
| sub | 011 |
| ld | 100 |
| st | 101 |
| mvnz | 110 |

This assembler supports labels (mark the address of next instruction / data) and constants (evaluated in the first pass).
- To define a label: use "`<labelname>:`". Label name should not appear on the same line after a valid instruction.
- To define a constant: use "`#define <Name> <ConstantExpression>`".
- To hardcode a data: use "`#data <ImmediateExpression>`"

All evaluation of expressions support addition and subtraction (e.g. "mvi R0,ADDRESS+1" where ADDRESS is a constant or label), but a label cannot be subtracted. ImmediateExpression can have at most one dependency on label (e.g. you cannot have "mvi R0, ADDRESS_END-ADDRESS_BEGIN"), and ConstantExpression cannot have dependency on label (they should have determinable value when program see it).

** Avoid spaces inside a single expression (e.g. Do not write "ADDRESS + 1"; write it as "ADDRESS+1") **

Feel free to modify the program to fit your case. Thank you!

