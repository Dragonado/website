# Codex critique notes: Metal API Remoter blog

This is a working checklist of review comments that remain unresolved in `docs/blog/posts/2026-08-02-metal-api-remoting.md`. It is not intended to dictate the blog's voice. Delete each item when the corresponding text is fixed or deliberately accepted.

## Remaining blog plan and length cap

The published draft is already approximately an 11-minute read. Cap the finished post at approximately 20 minutes by adding no more than roughly 1,500–1,800 words. The remaining narrative should contain only these sections:

1. **Turning Metal objects into remote handles**
   - Explain why pointers cannot cross machines.
   - Show how client proxy objects carry IDs that identify real server-side Metal objects.
   - Briefly cover the `Device` → queue/buffer/library → function/pipeline object graph.

2. **Which calls cross the network?**
   - Creation and server-state queries use synchronous RPCs.
   - Encoder recording stays local.
   - `commit()` sends the recorded job.
   - `waitUntilCompleted()` waits and receives results.
   - `release()` releases the remote handle.
   - Show only an abbreviated protobuf request; do not add a protobuf wire-format tutorial.

3. **`commit()` and `waitUntilCompleted()`**
   - Connect the shadow-buffer solution to its implementation.
   - At commit, send command metadata, bindings, and input bytes.
   - At wait, receive output bytes and copy them into the client shadow buffers.

4. **gRPC concurrency and the asynchronous scheduler**
   - Explain that gRPC runs handlers concurrently but does not make shared service state thread-safe.
   - Include the experiment where 3 of 100 adders failed without locking and all succeeded with locking.
   - Explain that `commit()` enqueues a job and a scheduler thread submits jobs to Metal.
   - Preserve submission order for command buffers from the same queue.
   - State that MAR controls admission order, not GPU preemption.
   - Do not turn this into a general C++ threading tutorial.

5. **Working demo**
   - Show that the same vector-add source can compile against native `metal-cpp` or MAR.
   - Show remote execution, copied-back results, verification, and concurrent clients.
   - Claim correctness and concurrent remote submission only.
   - Do not claim an utilization improvement or performance win that has not been measured.
   - Near the end, list remote object and queued-resource lifetime behavior as an outstanding caveat. Do not interrupt the remote-handle explanation with a full lifetime section.

The intended remaining flow is:

```text
remote object handles
    -> RPC versus local recording
    -> commit/wait data movement
    -> concurrent handlers and asynchronous scheduling
    -> working demo and honest claims
```

To stay within the length cap, omit dedicated sections about `NS::String`, protobuf wire encoding, every individual RPC, reference-count memory ordering, general multithreading concepts, rendering/audio architecture, adversarial security design, and an exhaustive TODO list. Do not add a separate MAR-versus-Thunder comparison section.

## Thunder Compute and oversubscription

- **"For most ML workloads, the GPU is idle a lot of the time" is too broad.** Sustained training and some inference services can keep a GPU busy. A safer claim is that many long-lived GPU allocations contain idle periods or are underutilized across time.
- **The PCIe-versus-TCP comparison is stated as universal.** A local GPU path normally has much lower latency and avoids network serialization and protocol overhead, but bandwidth depends on the PCIe generation, lane count, NIC, and network. TCP itself does not define a fixed throughput.
- **"TNR makes a profit" is not established by the public material.** Thunder says its design improves utilization and capacity. That supports a claim about better fleet economics, not a claim that the company is profitable or that a particular workload is unprofitable.
- **The localhost demonstration does not pass data between two devices.** It creates a network boundary between two processes on the same Mac. A later deployment could place those processes on separate machines.

## Software setup and scope

- **`metal-cpp` is called a framework.** Apple describes it as a low-overhead C++ interface to Metal, distributed as headers. Calling it a C++ interface or header-only library is more technically precise than calling it an Apple framework.
- **The shim does not hijack every Metal call.** It uses compile-time substitution for the supported `metal-cpp` surface. Calls outside the implemented shim are not transparently remoted.
- **The pasted `metal_hijack.h` snippet has the wrong namespace.** The blog currently shows `#define MTL MTLShim`, but the real project contains `#define MTL MetalShim`.
- **"Much much harder to solve than TNR" is too broad.** Metal introduces two specific complications for this prototype, but Thunder's production CUDA virtualization system also handles compatibility, networking, isolation, lifecycle management, scheduling, and failures. Compare the particular interception and memory-coherence problems instead of the overall difficulty of both systems.

## CUDA and Metal interception

- **The dynamic-link-layer claim is now supported both publicly and empirically.** Thunder's current job posting says it uses a userspace shim loaded through `LD_PRELOAD` to intercept CUDA calls and send them over gRPC. Inspection of a live TNR instance confirmed that `/etc/ld.so.preload` contains `/etc/thunder/libthunder.so`, causing the dynamic loader to inject Thunder's shim system-wide.
- **The blog currently says Thunder swaps the symbol "at link time."** That is inaccurate. The application is linked with an unresolved dynamic CUDA symbol and a PLT entry; the Linux dynamic loader chooses Thunder's preloaded definition when the process starts. Say "at process load time" or "during dynamic symbol resolution."
- **The PTX ABI link does not support the host-side interception explanation.** NVIDIA's PTX Writer's Guide documents the ABI for generated GPU device code. It is not a table of CUDA host-library symbols. The CUDA Driver API documentation is a better source for C entry points such as `cuMemAlloc` and `cuLaunchKernel`.
- **CUDA symbols are not "generated after compilation."** Compilation emits references/imports; the linker and dynamic loader resolve those references to functions in a shared library. `LD_PRELOAD` changes which implementation is found first.
- **"Apple don't have documentation for this" is inaccurate.** Apple documents Metal, `metal-cpp`, and the Objective-C runtime. The obstacle is that most Metal object methods are dispatched as Objective-C messages rather than exposed as one dynamic-library symbol per method.
- **Not literally every Metal operation is an Objective-C message send.** Object methods generally are, while entry points such as `MTLCreateSystemDefaultDevice()` are ordinary C functions.
- **Runtime interception is difficult, not categorically unavailable.** Objective-C forwarding, proxies, and method swizzling exist. They are substantially less convenient and more fragile for this goal than interposing a table of C functions, so the blog should describe the missing easy per-method linker boundary rather than imply that interception is impossible.

Useful primary sources:

- [Thunder Compute C++ Systems role](https://www.thundercompute.com/careers/role?id=2efae53b-817c-43e7-9da3-72694813f608)
- [NVIDIA CUDA Driver API guide](https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/driver-api.html)
- [NVIDIA PTX Writer's Guide](https://docs.nvidia.com/cuda/ptx-writers-guide-to-interoperability/index.html)
- [Apple metal-cpp](https://developer.apple.com/metal/cpp/)

### Empirical binary verification (August 2026)

The CUDA and Metal interception comparison was verified against actual compiled binaries rather than inferred only from documentation. Do not include the temporary instance address, credentials, SSH-key paths, or API tokens in the blog.

#### Native metal-cpp binary

The native `adder` was built with:

```bash
bazel build //src:adder --//src:shim=false
```

Its undefined imports include:

```text
_objc_msgSend
_sel_registerName
```

There is no `MTL::Buffer::contents()` or `_MTLBuffer_contents` callable symbol in the final binary. The inline metal-cpp wrapper has disappeared into the caller.

The literal selector name is stored in `__TEXT,__cstring`:

```text
contents
```

metal-cpp registers that string through `_sel_registerName` and stores the resulting selector at:

```text
MTL::Private::Selector::s_kcontents
```

The disassembly inside `Adder::populate_random_float` then:

1. loads `s_kcontents`;
2. puts the `MTL::Buffer*` receiver in `x0`;
3. puts the `contents` selector in `x1`; and
4. calls the generic `_objc_msgSend` stub.

This verifies the blog's central Metal claim: there is no per-method dynamic function symbol comparable to a CUDA entry point. It does **not** prove runtime interception is impossible; it proves that interposing one clean symbol per Metal method is unavailable.

The blog was corrected to use `__TEXT,__cstring`, not `__objc_methname`, because that is what the actual metal-cpp binary emits.

#### Live TNR CUDA binary

A minimal CUDA Driver API probe was compiled on a live TNR RTX A6000 instance. It initialized CUDA, created a context, allocated and freed 4 KiB, destroyed the context, and exited successfully with status `0`.

The source-level calls compiled into these undefined ELF symbols:

```text
U cuCtxCreate_v4
U cuCtxDestroy_v2
U cuDeviceGet
U cuInit
U cuMemAlloc_v2
U cuMemFree_v2
```

This confirms that modern CUDA headers can map a source call such as `cuMemAlloc(...)` to an ABI-versioned symbol such as `cuMemAlloc_v2`.

The actual allocation call used the Procedure Linkage Table:

```asm
call  cuMemAlloc_v2@plt
```

The instance's system preload file contained:

```text
/etc/thunder/libthunder.so
```

`libthunder.so` exported CUDA entry points under its `LIBTHUNDER` symbol version, including multiple ABI generations such as:

```text
cuCtxCreate
cuCtxCreate_v2
cuCtxCreate_v3
cuCtxCreate_v4
cuMemAlloc
cuMemAlloc_v2
```

`LD_DEBUG=bindings` showed every probe call resolving to `/etc/thunder/libthunder.so`, including `cuInit`, `cuDeviceGet`, `cuCtxCreate_v4`, `cuMemAlloc_v2`, `cuMemFree_v2`, and `cuCtxDestroy_v2`. Although `libcuda.so.1` remained a declared dependency, Thunder's preloaded definitions won dynamic symbol resolution.

The experimentally verified chain is therefore:

```text
source call: cuMemAlloc(...)
    -> undefined ELF symbol: cuMemAlloc_v2
    -> PLT entry: cuMemAlloc_v2@plt
    -> Linux dynamic loader
    -> /etc/ld.so.preload
    -> /etc/thunder/libthunder.so::cuMemAlloc_v2
    -> Thunder's remote GPU implementation
```

This is excellent primary evidence for the blog. It is stronger and more durable to describe the commands and observed output than to rely only on a screenshot of Thunder's job posting.

## Unified memory and data movement

- **The NVIDIA description should be scoped to a traditional discrete GPU.** NVIDIA also supports integrated and coherent-memory systems, and CUDA offers managed/unified-memory abstractions. Separate host memory and device VRAM is the conventional discrete-GPU case, not a universal NVIDIA property.
- **`cudaMemcpy` does not "invoke the PCI Express bus."** It requests a transfer through the CUDA runtime or driver. The actual transfer may use DMA over PCIe, NVLink, or another supported interconnect.
- **Thunder does more than replace PCIe with TCP.** Remote execution also requires allocations, object or handle identity, contexts, streams, launches, synchronization, error propagation, library state, and lifetimes. Replacing a local data path with a network path is a useful high-level analogy, not the literal only difference.
- **"There is no possibility of incorrectness" is false.** Explicit copies create convenient observation and synchronization points, but a remoter can still break ordering, asynchronous behavior, pointer semantics, errors, retries, version compatibility, and unsupported memory modes.
- **Unified memory does not mean that there is "nothing to transfer."** Shared Metal resources allow the CPU and GPU to access the same system-memory allocation without an explicit host-to-VRAM copy. Cache visibility, synchronization, bandwidth, and private GPU resources still matter.
- **The programmer cannot treat the CPU and GPU as the same processor.** They remain separate processors that execute asynchronously. Metal applications must obey resource-synchronization rules even when both processors can access the same allocation.
- **"Correctness cannot be resolved" is too absolute.** Dirty-page tracking, page protection, explicit coherence protocols, or restricted access rules could support more behavior. The precise limitation is that the current copy-at-commit/copy-at-wait snapshot model cannot reproduce every legal Metal access pattern.

Useful primary source:

- [Apple shared storage mode](https://developer.apple.com/documentation/metal/mtlstoragemode/shared)

## Publishing and presentation

- **A draft marker is visible in the published page.** `TODO: Rest of blog.` currently renders. The two `[CHAT: ...]` callouts have been replaced with concrete examples.
- **The article ends abruptly while the remaining outline is commented out.** Because the post is published, readers encounter the TODO and then a short limitations section rather than a deliberate stopping point.
- **The rendering limitation mixes mandatory and conditional problems.** A remote renderer must handle client-owned drawables, presentation timing, and frame transport. Image compression is a design choice, and audio synchronization matters only when the remoted application also has audio.
- **The student-credit price conversion is time-sensitive.** The "$20 equals about nine H100 hours" example should either state the observed date/rate or be rechecked before final publication.

## Already addressed since the earlier review

- Removed the unsupported implication that Thunder must be VC-subsidized or losing money.
- Changed Thunder's transport wording from the public internet to a network.
- Corrected the CPU-to-CPU typo to CPU-to-GPU.
- Deliberately accepted the informal "Exciting to be the first" wording. It follows the author's qualifier that they could not find prior work, and the author accepts that the novelty claim is not independently verifiable.
- Reworded the gRPC bullet so it no longer claims that the project has transport security merely because it uses gRPC.
- Confirmed from Thunder's own current job posting that its CUDA interception uses a userspace `LD_PRELOAD` shim.
- Confirmed on a live TNR instance that `/etc/ld.so.preload` injects `libthunder.so`, which wins runtime binding for ABI-versioned CUDA symbols such as `cuMemAlloc_v2`.
- Archived the Thunder C++ Systems job-posting screenshot in `docs/assets` with a dated caption and retained the live source link, removing the blog's dependency on ImgBB.
- Replaced both visible `[CHAT: ...]` requests with CUDA/Metal interception and memory-transfer examples.
