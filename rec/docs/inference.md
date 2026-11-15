# Inference

[Documentation index](README.md)

CPU/OpenCL inference and the dynamically loaded ONNX Runtime DirectML C ABI. Internal runtime types are defined only in Inference.cpp.

## `car::Runtime::Runtime`

```cpp
Runtime()
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Initialize the process-wide ONNX Runtime C API and environment.

**Inputs:** No arguments; expects `onnxruntime.dll` beside the executable.

**Returns / output:** An internal runtime with DLL handle, API table, and environment.

**Behavior, ownership, and failure:** Builds an executable-relative DLL path, loads it, resolves `OrtGetApiBase`, obtains the compiled API version, and creates an environment with error-level logging. Throws for missing DLL/export/API or environment failure. Created lazily through `runtime`, not for CPU-only inference.

## `car::Runtime::check`

```cpp
void Runtime::check(OrtStatus* status)
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Translate ONNX Runtime status pointers into C++ exceptions.

**Inputs:** Null for success or an owned nonnull status returned by an ORT API.

**Returns / output:** Returns normally on success; otherwise throws `std::runtime_error`.

**Behavior, ownership, and failure:** Copies the error message, releases the status, then throws. A nonnull status is consumed and must not be used or freed again.

## `car::Runtime::~Runtime`

```cpp
~Runtime()
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Release the shared environment and loaded inference DLL.

**Inputs:** No arguments.

**Returns / output:** No return value.

**Behavior, ownership, and failure:** On normal singleton teardown, releases an existing environment before freeing the DLL. Sessions must already be destroyed. Its existence does not imply every partially failed constructor is cleaned up by this destructor.

## `car::runtime`

```cpp
static Runtime& runtime()
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Provide the lazily initialized shared runtime.

**Inputs:** No arguments.

**Returns / output:** Reference to a function-local static `Runtime`.

**Behavior, ownership, and failure:** Internal helper; initializes on first use, can propagate constructor failure, and retains the environment for process lifetime. It does not create or share model sessions.

## `car::gpuIndex`

```cpp
static int gpuIndex(int requested)
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Select the DirectML DXGI adapter.

**Inputs:** An explicit nonnegative adapter index or negative value for automatic selection.

**Returns / output:** The requested index, or the nonsoftware adapter index with the most dedicated video memory.

**Behavior, ownership, and failure:** Returns zero if factory creation fails or no adapter improves the initial choice. Enumerates/release DXGI COM objects. Does not validate an explicit index, benchmark devices, or prove DirectML support; session creation handles failures.

## `car::Network::Impl::Impl`

```cpp
Network::Impl::Impl()
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Create internal GPU-session bookkeeping.

**Inputs:** No explicit arguments.

**Returns / output:** An implementation holding a shared runtime reference, initially null session/memory pointers, and empty I/O names.

**Behavior, ownership, and failure:** Acquiring the runtime may load the DLL or throw. This constructor alone does not load an ONNX model.

## `car::Network::Impl::~Impl`

```cpp
Network::Impl::~Impl()
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Release resources owned by one GPU network.

**Inputs:** No arguments.

**Returns / output:** No return value.

**Behavior, ownership, and failure:** Releases an existing ORT session and CPU-memory descriptor through the shared API. Does not free the process-wide environment.

## `car::Network::Network`

```cpp
Network(const std::string& path, bool opencl=false, bool directml=false, int device=-1)
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Load one ONNX model using the selected backend.

**Inputs:** UTF-8 model path; OpenCL/DirectML selectors; adapter index.

**Returns / output:** An initialized network ready for `forward`.

**Behavior, ownership, and failure:** CPU/OpenCL mode uses OpenCV DNN; OpenCL must be available. DirectML takes precedence when requested, resolves the provider append function dynamically, disables memory patterns, selects sequential execution, enables graph optimization, and sets two intra-op threads. Loads a session using a UTF-16 path; records input/output name zero and creates a CPU memory descriptor. Prints adapter/model information. No silent DirectML-to-CPU retry. Throws on loading/provider/session errors. Session options and normal owned session resources are released appropriately.

## `car::Network::~Network`

```cpp
~Network()
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Destroy one inference wrapper.

**Inputs:** No arguments.

**Returns / output:** No return value.

**Behavior, ownership, and failure:** The explicit body is empty; automatic member destruction releases the OpenCV network or `unique_ptr<Impl>` and its GPU resources. The wrapper must not be destroyed during `forward`.

## `car::Network::forward`

```cpp
cv::Mat Network::forward(const cv::Mat& blob)
```

**Source:** [src/Inference.cpp](../src/Inference.cpp).

Run the model and return its first output tensor.

**Inputs:** A contiguous `CV_32F` input blob with dimensions matching the selected ONNX model; callers use `blobFromImage`.

**Returns / output:** A `cv::Mat` containing output zero. GPU output is cloned into owned CPU memory before ORT buffers are released.

**Behavior, ownership, and failure:** CPU/OpenCL uses `setInput`/`forward`. DirectML wraps caller memory as an input tensor, runs one named input/output, obtains output dimensions, rejects nonpositive or oversized dimensions, and copies the result. Assumes float output; it does not implement a generic arbitrary-type/multi-input/multi-output executor. Frees temporary ORT values/type information on normal completion and exceptions. Not safe to call concurrently on the same instance.

## `car::Network::Network(copy)`

```cpp
Network(const Network&) = delete
```

**Source:** [include/Inference.hpp](../include/Inference.hpp).

Prevent copying an owner of inference session resources.

**Inputs:** A source network; operation is unavailable.

**Returns / output:** No executable operation: attempting to copy is a compile-time error.

**Behavior, ownership, and failure:** Declared but intentionally not implemented. Construct a separate network if a second session is needed.

## `car::Network::operator=`

```cpp
Network& operator=(const Network&) = delete
```

**Source:** [include/Inference.hpp](../include/Inference.hpp).

Prevent copy-assignment of inference session ownership.

**Inputs:** A source network; operation is unavailable.

**Returns / output:** No executable operation: copy-assignment is a compile-time error.

**Behavior, ownership, and failure:** Declared but intentionally not implemented. Ownership remains with the existing wrapper.
