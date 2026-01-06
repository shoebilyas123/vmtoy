# LC-3 Virtual Machine

## 1. Overview
This project is a C implementation of a virtual machine for the LC-3 (Little Computer 3) architecture. It simulates the LC-3 hardware, including memory, registers, and instruction set execution.

## 2. Building the VM
You can build the project using the provided build script or manually with gcc.

### Using build script
```bash
./build.sh
```

### Manual compilation
```bash
gcc index.c -o lc3-vm
```

## 3. Running and Testing the VM
Once built, you can run LC-3 programs (object files) by passing them as arguments to the executable.

Two example applications are provided in the `apps/` directory:
- `2048_vm.obj`: A version of the 2048 game
- `rogue_vm.obj`: A rogue-like game

### To run 2048:
```bash
./lc3-vm apps/2048_vm.obj
```

### To run Rogue:
```bash
./lc3-vm apps/rogue_vm.obj
```
