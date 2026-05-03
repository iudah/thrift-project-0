# Zot - Memory Management Interface

Zot is a lightweight dummy interface to the memory allocator library (memalloc). It is designed to simplify memory management within the ML stack and serve as an accessible entry point for integrating the underlying allocator.

**Highlights:**
- Works in tandem with memalloc.
- A utility component within a six-module ML stack.
- Built using CMake.
- Demonstrates basic memory management integration.

## ML Stack Modules
1. **memalloc:** Memory allocator/manager.
2. **zot:** Dummy memory management interface (this project).
3. **zobject:** Object-oriented programming emulator.
4. **zor:** Tensor library.
5. **zode:** Autodiff library.
6. **zone:** Neural network library.

## Getting Started

Clone and build with CMake:
```bash
git clone https://github.com/iudah/zot.git
cd zot
cmake -B ./build -S .
cmake --build ./build
```

## Feedback

For issues or suggestions, please use the issue tab.
