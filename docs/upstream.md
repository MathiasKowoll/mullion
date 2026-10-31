# Upstream

This fork (github.com/MathiasKowoll/mullion) follows two projects: DXMT, which Mullion is built on, and Mullion itself
(github.com/dttdrv/mullion). This file lists what has been taken from each, what was left and why, and how far each
was reviewed, so the next review starts where the last one stopped.

Mullion imported DXMT as one commit, with no history in common, so DXMT's changes cannot be merged: each is ported by
hand, and its commit message names the DXMT commit it ports. Mullion's own commits are cherry-picked.

## DXMT

- Base: [`7c8dee1`](https://github.com/3Shain/dxmt/commit/7c8dee1c2d73415301ceb7d1fa810861cef4cd67) (16 September
  2026), as Mullion's README says.
- Listed up to: [`3b25259`](https://github.com/3Shain/dxmt/commit/3b252590cb0290e36fca6f088b5255b871d2d95a)
  (27 October 2026), 121 commits after the base. None is ported yet.

DXMT now writes a Direct3D 12 and a DXIL front end of its own, beside Mullion's, which were written independently.
Those commits are a reference for what Mullion lacks (DXMT's tessellation pipeline, for one), not patches. Each other
commit is reviewed: ported, already done in Mullion, or not applicable, with the reason.

States: *ported as `<commit>`*, *already in Mullion (`<commit>`)*, *reference*, *not applicable: <why>*, *in progress*,
*to review*.

| DXMT commit | Date | Subject | State |
|---|---|---|---|
| [`c5f854c`](https://github.com/3Shain/dxmt/commit/c5f854c) | 2026-09-28 | ci: add `_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23` macro to llvm build | not applicable: DXMT's CI |
| [`e86484e`](https://github.com/3Shain/dxmt/commit/e86484e) | 2026-09-28 | ci: disable benchmark in llvm build | not applicable: DXMT's CI |
| [`fb45156`](https://github.com/3Shain/dxmt/commit/fb45156) | 2026-09-30 | fix(dxmt): initialize `device_` before the command helpers that use it | to review |
| [`830eb41`](https://github.com/3Shain/dxmt/commit/830eb41) | 2026-10-02 | fix(d3d11): raise vertex buffer binding count to `D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT` | to review |
| [`f9beaea`](https://github.com/3Shain/dxmt/commit/f9beaea) | 2026-10-02 | fix(dxmt): pass mipped width & height to clear encoder | to review |
| [`acda4f9`](https://github.com/3Shain/dxmt/commit/acda4f9) | 2026-10-02 | fix(d3d11): remove `d3d11_device_mutex::try_lock()` | to review |
| [`a22700c`](https://github.com/3Shain/dxmt/commit/a22700c) | 2026-10-02 | fix: fix a couple of copy-paste bugs | to review |
| [`59eb4c1`](https://github.com/3Shain/dxmt/commit/59eb4c1) | 2026-10-02 | fix(nvngx): use correct nvngx error value (0xbad00000) | in progress on branch dlss-d3d12 |
| [`542ca17`](https://github.com/3Shain/dxmt/commit/542ca17) | 2026-10-02 | fix(nvngx): use 1.0 as default pre-exposure value | in progress on branch dlss-d3d12 |
| [`e4f868d`](https://github.com/3Shain/dxmt/commit/e4f868d) | 2026-10-02 | fix(d3d10): fix a wrong pointer assignment | to review |
| [`0fff7ff`](https://github.com/3Shain/dxmt/commit/0fff7ff) | 2026-10-02 | fix(nvapi): copy build branch string with null terminator | to review |
| [`ce79ad0`](https://github.com/3Shain/dxmt/commit/ce79ad0) | 2026-10-05 | fix(d3d11): fix some logic flaws in dilated motion vector handling | to review |
| [`0b63990`](https://github.com/3Shain/dxmt/commit/0b63990) | 2026-10-05 | fix(d3d11): lock device in `Signal()` and `Wait()` | to review |
| [`ae91269`](https://github.com/3Shain/dxmt/commit/ae91269) | 2026-10-05 | fix(airconv): fix some logic flaws | to review |
| [`a3d9005`](https://github.com/3Shain/dxmt/commit/a3d9005) | 2026-10-05 | fix(d3d12): bail out failed `ExtractMTLInputLayoutElements()` before using unintialized `num_elements` | reference: DXMT's own Direct3D 12 |
| [`68af85e`](https://github.com/3Shain/dxmt/commit/68af85e) | 2026-10-05 | fix(d3d11): handle legitimate null parameter in `Create{Blend\|Rasterizer}State()` | to review |
| [`e40c9d7`](https://github.com/3Shain/dxmt/commit/e40c9d7) | 2026-10-05 | feat(d3d11): allow copying between different texture types in `CopySubresourceRegion()` | to review |
| [`e94c312`](https://github.com/3Shain/dxmt/commit/e94c312) | 2026-10-06 | fix(d3d11): avoid deadlock on reentrant `ResizeTarget()` | to review |
| [`5beb3b2`](https://github.com/3Shain/dxmt/commit/5beb3b2) | 2026-10-06 | chore(d3d11): remove redundant code for calculate number of mips | to review |
| [`baf79e0`](https://github.com/3Shain/dxmt/commit/baf79e0) | 2026-10-07 | fix(d3d12): fixed handling case for unknown format in `CheckFeatureSupport` | reference: DXMT's own Direct3D 12 |
| [`0013428`](https://github.com/3Shain/dxmt/commit/0013428) | 2026-10-07 | fix(d3d11, winemetal): fixed compilation when `DXMT_NO_PRIVATE_API` is used | to review |
| [`186a232`](https://github.com/3Shain/dxmt/commit/186a232) | 2026-10-07 | fix(d3d11): `pMappedResource` argument in `ID3D11DeviceContext::Map` can be NULL | to review |
| [`50102f9`](https://github.com/3Shain/dxmt/commit/50102f9) | 2026-10-16 | fix(nvngx): allow resources stored as `void *` to be read as D3D resources | in progress on branch dlss-d3d12 |
| [`5c80192`](https://github.com/3Shain/dxmt/commit/5c80192) | 2026-10-22 | refactor(airconv): introduce `VertexPuller` | to review |
| [`4b255b7`](https://github.com/3Shain/dxmt/commit/4b255b7) | 2026-10-22 | refactor(airconv): introduce `IOHelper` | to review |
| [`39625fa`](https://github.com/3Shain/dxmt/commit/39625fa) | 2026-10-22 | fix(airconv): fix potential leaks of `sm50_*` objects | to review |
| [`35bfdb1`](https://github.com/3Shain/dxmt/commit/35bfdb1) | 2026-10-22 | feat(airconv): add kernel id intrinsics | to review |
| [`f2fd9e9`](https://github.com/3Shain/dxmt/commit/f2fd9e9) | 2026-10-22 | feat(airconv): recognize dxil blob | to review |
| [`4219e15`](https://github.com/3Shain/dxmt/commit/4219e15) | 2026-10-22 | feat(d3d12): pass dxil shader to airconv | reference: DXMT's own Direct3D 12 |
| [`47476d2`](https://github.com/3Shain/dxmt/commit/47476d2) | 2026-10-22 | chore(airconv): replace `handleAllErrors()` with `toString()` | to review |
| [`7921d6b`](https://github.com/3Shain/dxmt/commit/7921d6b) | 2026-10-22 | feat(airconv): introduce `MetadataReader` | to review |
| [`f108810`](https://github.com/3Shain/dxmt/commit/f108810) | 2026-10-22 | feat(airconv): parse dxil compute shader | to review |
| [`8196329`](https://github.com/3Shain/dxmt/commit/8196329) | 2026-10-22 | feat(airconv): introduce dxil `Converter` | to review |
| [`6871952`](https://github.com/3Shain/dxmt/commit/6871952) | 2026-10-22 | feat(airconv): implement dxil `createHandle` | to review |
| [`c9126a7`](https://github.com/3Shain/dxmt/commit/c9126a7) | 2026-10-22 | feat(airconv): initialize dxil shader resource map | to review |
| [`9166462`](https://github.com/3Shain/dxmt/commit/9166462) | 2026-10-22 | feat(airconv): implement dxil `cbufferLoadLegacy` | to review |
| [`7336b95`](https://github.com/3Shain/dxmt/commit/7336b95) | 2026-10-22 | feat(airconv): implement dxil thread/group id intrinsics | to review |
| [`33ea6ff`](https://github.com/3Shain/dxmt/commit/33ea6ff) | 2026-10-22 | feat(airconv): implement dxil `bufferStore` | to review |
| [`20e291b`](https://github.com/3Shain/dxmt/commit/20e291b) | 2026-10-22 | feat(airconv): implement dxil `bufferLoad` | to review |
| [`6836a75`](https://github.com/3Shain/dxmt/commit/6836a75) | 2026-10-22 | feat(airconv): implement dxil `textureStore` | to review |
| [`8be0d72`](https://github.com/3Shain/dxmt/commit/8be0d72) | 2026-10-22 | feat(airconv): implement dxil `textureLoad` | to review |
| [`a32ddb3`](https://github.com/3Shain/dxmt/commit/a32ddb3) | 2026-10-22 | feat(airconv): implement dxil `bufferUpdateCounter` | to review |
| [`4f1f313`](https://github.com/3Shain/dxmt/commit/4f1f313) | 2026-10-22 | feat(airconv): handle 16-bit resource descriptors | to review |
| [`01e8ab3`](https://github.com/3Shain/dxmt/commit/01e8ab3) | 2026-10-22 | feat(d3d12): report `Native16BitShaderOpsSupported`=`TRUE` | reference: DXMT's own Direct3D 12 |
| [`130aefd`](https://github.com/3Shain/dxmt/commit/130aefd) | 2026-10-22 | feat(airconv): implement dxil `getDimensions` | to review |
| [`e473584`](https://github.com/3Shain/dxmt/commit/e473584) | 2026-10-22 | feat(airconv): implement dxil `rawBufferStore` and `rawBufferLoad` | to review |
| [`0a02fff`](https://github.com/3Shain/dxmt/commit/0a02fff) | 2026-10-22 | feat(airconv): implement dxil `legacyF16ToF32` and `legacyF32ToF16` | to review |
| [`7061e6e`](https://github.com/3Shain/dxmt/commit/7061e6e) | 2026-10-22 | feat(airconv): implement dxil `unaryBits` | to review |
| [`2249dab`](https://github.com/3Shain/dxmt/commit/2249dab) | 2026-10-22 | feat(airconv): support group shared memory and indexable constants | to review |
| [`73573ab`](https://github.com/3Shain/dxmt/commit/73573ab) | 2026-10-22 | fix(d3d12): fail unsupported shader model query | reference: DXMT's own Direct3D 12 |
| [`23f2247`](https://github.com/3Shain/dxmt/commit/23f2247) | 2026-10-22 | feat(airconv): implement dxil `barrier` | to review |
| [`cec11cb`](https://github.com/3Shain/dxmt/commit/cec11cb) | 2026-10-22 | refactor(airconv): store a fat `dxil::EntryPoint` object instead of a raw `llvm::Function` pointer | to review |
| [`b8ab0d3`](https://github.com/3Shain/dxmt/commit/b8ab0d3) | 2026-10-22 | feat(airconv): parse dxil vertex & pixel shader | to review |
| [`2bc394b`](https://github.com/3Shain/dxmt/commit/2bc394b) | 2026-10-22 | feat(airconv): implement dxil `loadInput` and `storeOutput` | to review |
| [`297d279`](https://github.com/3Shain/dxmt/commit/297d279) | 2026-10-22 | feat(airconv): implement dxil `sample` | to review |
| [`1f96b5e`](https://github.com/3Shain/dxmt/commit/1f96b5e) | 2026-10-22 | feat(airconv): handle 16-bit input and output | to review |
| [`6eefe27`](https://github.com/3Shain/dxmt/commit/6eefe27) | 2026-10-22 | feat(airconv): handle 16-bit `SV_Position` | to review |
| [`c7ff6c1`](https://github.com/3Shain/dxmt/commit/c7ff6c1) | 2026-10-22 | refactor(airconv): introduce `to_air_sample_type()` | to review |
| [`d72b482`](https://github.com/3Shain/dxmt/commit/d72b482) | 2026-10-22 | feat(airconv): record texture gather usage | to review |
| [`a04f6af`](https://github.com/3Shain/dxmt/commit/a04f6af) | 2026-10-22 | chore(d3d12): advertise `Int64ShaderOps` | reference: DXMT's own Direct3D 12 |
| [`cfc1647`](https://github.com/3Shain/dxmt/commit/cfc1647) | 2026-10-22 | feat(airconv): implement dxil `sample` variants | to review |
| [`2baa9b4`](https://github.com/3Shain/dxmt/commit/2baa9b4) | 2026-10-22 | feat(airconv): implement dxil `textureGather` and `textureGatherCmp` | to review |
| [`bf67ddc`](https://github.com/3Shain/dxmt/commit/bf67ddc) | 2026-10-22 | fix(d3d12): add atomic usage to eligible texture | reference: DXMT's own Direct3D 12 |
| [`17ac060`](https://github.com/3Shain/dxmt/commit/17ac060) | 2026-10-22 | feat(airconv): add more AIR trigonometric intrinsics | to review |
| [`0268975`](https://github.com/3Shain/dxmt/commit/0268975) | 2026-10-22 | chore(airconv): define known dxil op constants | to review |
| [`9f38e54`](https://github.com/3Shain/dxmt/commit/9f38e54) | 2026-10-22 | feat(airconv): implement dxil `unary` | to review |
| [`e2ca44b`](https://github.com/3Shain/dxmt/commit/e2ca44b) | 2026-10-22 | feat(airconv): implement dxil `binary` | to review |
| [`fe79b95`](https://github.com/3Shain/dxmt/commit/fe79b95) | 2026-10-22 | feat(airconv): implement dxil `tertiary` | to review |
| [`e9cc1d0`](https://github.com/3Shain/dxmt/commit/e9cc1d0) | 2026-10-22 | feat(airconv): implement dxil dot products | to review |
| [`9ec6613`](https://github.com/3Shain/dxmt/commit/9ec6613) | 2026-10-22 | feat(airconv): implement dxil `isSpecialFloat` | to review |
| [`8f4dad4`](https://github.com/3Shain/dxmt/commit/8f4dad4) | 2026-10-22 | feat(airconv): handle llvm `atomicrmw` | to review |
| [`f0e8a21`](https://github.com/3Shain/dxmt/commit/f0e8a21) | 2026-10-22 | feat(airconv): implement dxil `atomicBinOp` | to review |
| [`1197857`](https://github.com/3Shain/dxmt/commit/1197857) | 2026-10-22 | feat(airconv): implement dxil `atomicCompareExchange` | to review |
| [`7774bba`](https://github.com/3Shain/dxmt/commit/7774bba) | 2026-10-22 | feat(airconv): implement dxil `sampleGrad` | to review |
| [`69233ed`](https://github.com/3Shain/dxmt/commit/69233ed) | 2026-10-22 | feat(airconv): implement dxil `discard` | to review |
| [`a8e5b87`](https://github.com/3Shain/dxmt/commit/a8e5b87) | 2026-10-22 | feat(airconv): implement dxil `[earlydepthstencil]` | to review |
| [`aa76581`](https://github.com/3Shain/dxmt/commit/aa76581) | 2026-10-22 | feat(airconv): implement dxil `SV_Depth` | to review |
| [`5aee35a`](https://github.com/3Shain/dxmt/commit/5aee35a) | 2026-10-22 | feat(airconv): implement dxil `SV_IsFrontFace` | to review |
| [`918df56`](https://github.com/3Shain/dxmt/commit/918df56) | 2026-10-22 | feat(airconv): parse dxil semantic indices | to review |
| [`b856c46`](https://github.com/3Shain/dxmt/commit/b856c46) | 2026-10-22 | feat(airconv): support multi-row signature element | to review |
| [`30d81b2`](https://github.com/3Shain/dxmt/commit/30d81b2) | 2026-10-22 | feat(airconv): implement dxil `calculateLOD` | to review |
| [`aa11f0b`](https://github.com/3Shain/dxmt/commit/aa11f0b) | 2026-10-22 | feat(airconv): implement dxil `texture2DMSGetSamplePosition` | to review |
| [`f2df8cf`](https://github.com/3Shain/dxmt/commit/f2df8cf) | 2026-10-22 | feat(airconv): implement dxil `SV_RenderTargetArrayIndex` and `SV_ViewportArrayIndex` | to review |
| [`5979bc5`](https://github.com/3Shain/dxmt/commit/5979bc5) | 2026-10-22 | feat(airconv): implement dxil `SV_ClipDistance` and `SV_CullDistance` | to review |
| [`f94c7cf`](https://github.com/3Shain/dxmt/commit/f94c7cf) | 2026-10-22 | feat(airconv): implement dxil `waveGetLaneIndex` and `waveGetLaneCount` | to review |
| [`72ee0cd`](https://github.com/3Shain/dxmt/commit/72ee0cd) | 2026-10-22 | feat(airconv): implement dxil `quadReadLaneAt` and `waveReadLaneAt` | to review |
| [`4b25328`](https://github.com/3Shain/dxmt/commit/4b25328) | 2026-10-22 | feat(airconv): implement dxil `quadOp` | to review |
| [`12dcfee`](https://github.com/3Shain/dxmt/commit/12dcfee) | 2026-10-22 | feat(airconv): implement dxil `sampleIndex` | to review |
| [`c6dd845`](https://github.com/3Shain/dxmt/commit/c6dd845) | 2026-10-22 | feat(airconv): implement dxil `coverage` | to review |
| [`9ea4ec9`](https://github.com/3Shain/dxmt/commit/9ea4ec9) | 2026-10-22 | feat(airconv): implement dxil `SV_Coverage` output | to review |
| [`9092131`](https://github.com/3Shain/dxmt/commit/9092131) | 2026-10-22 | feat(airconv): implement dxil `renderTargetGetSampleCount` | to review |
| [`ce001df`](https://github.com/3Shain/dxmt/commit/ce001df) | 2026-10-22 | fix(airconv): handle OUTPUT_CONTROL_POINT_ID relative indexing | to review |
| [`a71b60e`](https://github.com/3Shain/dxmt/commit/a71b60e) | 2026-10-20 | fix(d3d11): take the device lock on the unlocked context entries | to review |
| [`e186434`](https://github.com/3Shain/dxmt/commit/e186434) | 2026-10-05 | feat(d3d11): implement `SwapDeviceContextState()` | to review |
| [`a148782`](https://github.com/3Shain/dxmt/commit/a148782) | 2026-10-06 | feat(d3d11, dxgi): implement dxgi surface | to review |
| [`5209b37`](https://github.com/3Shain/dxmt/commit/5209b37) | 2026-10-26 | feat(airconv): parse dxil geometry shader | to review |
| [`cae78f4`](https://github.com/3Shain/dxmt/commit/cae78f4) | 2026-10-26 | feat(airconv): implement dxil `emitStream` and `cutStream` | to review |
| [`7a14a9a`](https://github.com/3Shain/dxmt/commit/7a14a9a) | 2026-10-26 | feat(airconv): implement dxil `loadInput` with vertex index in geometry shader | to review |
| [`f13dfc7`](https://github.com/3Shain/dxmt/commit/f13dfc7) | 2026-10-26 | feat(airconv): implement dxil `SV_RenderTargetArrayIndex` and `SV_ViewportArrayIndex` in geometry shader | to review |
| [`6d1bb98`](https://github.com/3Shain/dxmt/commit/6d1bb98) | 2026-10-26 | feat(airconv): implement dxil `SV_ClipDistance` and `SV_CullDistance` in geometry shader | to review |
| [`e2ec8a9`](https://github.com/3Shain/dxmt/commit/e2ec8a9) | 2026-10-26 | fix(airconv): emit `SV_ClipDistance` and `SV_CullDistance` as vertex outputs in dxbc geometry shader | to review |
| [`94d12d3`](https://github.com/3Shain/dxmt/commit/94d12d3) | 2026-10-26 | feat(airconv): implement dxil `gsInstanceID` | to review |
| [`914ae9a`](https://github.com/3Shain/dxmt/commit/914ae9a) | 2026-10-26 | feat(airconv): implement gs with dynamic index buffer view | to review |
| [`39a4722`](https://github.com/3Shain/dxmt/commit/39a4722) | 2026-10-26 | feat(d3d12): introduce `GetRenderPipelineVariant()` on render pipeline state object | reference: DXMT's own Direct3D 12 |
| [`9432f58`](https://github.com/3Shain/dxmt/commit/9432f58) | 2026-10-26 | feat(d3d12): implement render pipeline with geometry shader | reference: DXMT's own Direct3D 12 |
| [`7854fdf`](https://github.com/3Shain/dxmt/commit/7854fdf) | 2026-10-26 | fix(airconv): do handle `SV_GSInstanceID` in dxbc geometry shader | to review |
| [`ff5ea68`](https://github.com/3Shain/dxmt/commit/ff5ea68) | 2026-10-26 | feat(airconv): implement dxil  & dxbc `SV_PrimitiveID` in geometry & pixel shader | to review |
| [`21734bc`](https://github.com/3Shain/dxmt/commit/21734bc) | 2026-10-26 | chore(airconv): remove unused compare-exchange temp register | to review |
| [`12107ac`](https://github.com/3Shain/dxmt/commit/12107ac) | 2026-10-26 | fix(DXBCParser): remove assertion to allow dxbc container to not have `RTS0` chunk | to review |
| [`82246f7`](https://github.com/3Shain/dxmt/commit/82246f7) | 2026-10-26 | feat(d3d12): report minimum feature level passed at device creation | reference: DXMT's own Direct3D 12 |
| [`1ee4988`](https://github.com/3Shain/dxmt/commit/1ee4988) | 2026-10-26 | feat(d3d12): implement `WriteBufferImmediate()` | reference: DXMT's own Direct3D 12 |
| [`1b9104a`](https://github.com/3Shain/dxmt/commit/1b9104a) | 2026-10-23 | fix(d3d12): handle heap allocation failure in `MTLD3D12HeapImpl::Initialize` | reference: DXMT's own Direct3D 12 |
| [`5cafc43`](https://github.com/3Shain/dxmt/commit/5cafc43) | 2026-10-26 | fix(winemetal): retain returned error | to review |
| [`310d5d0`](https://github.com/3Shain/dxmt/commit/310d5d0) | 2026-10-26 | fix(winemetal): create a `MTLCompileOptions` for `newLibraryWithSource` | to review |
| [`a741a3f`](https://github.com/3Shain/dxmt/commit/a741a3f) | 2026-10-26 | fix(winemetal): fix memory leaks in `WMTGetDisplayDescription()` | to review |
| [`d815f7f`](https://github.com/3Shain/dxmt/commit/d815f7f) | 2026-10-26 | fix(winemetal): fix memory leaks in `WMTQueryDisplaySettingForLayer()` | to review |
| [`9839e55`](https://github.com/3Shain/dxmt/commit/9839e55) | 2026-10-26 | fix(airconv): store call return point before redirecting to branch | to review |
| [`77be3b9`](https://github.com/3Shain/dxmt/commit/77be3b9) | 2026-10-26 | feat(airconv): implement ts with dynamic index buffer view | to review |
| [`a159fb3`](https://github.com/3Shain/dxmt/commit/a159fb3) | 2026-10-26 | feat(d3d12): implement tessellation render pipeline | reference: DXMT's own Direct3D 12 |
| [`d162ea4`](https://github.com/3Shain/dxmt/commit/d162ea4) | 2026-10-27 | fix(airconv): set point size and primitive count for gs pointlist output | to review |
| [`3b25259`](https://github.com/3Shain/dxmt/commit/3b25259) | 2026-10-27 | fix(d3d12): remove double-free of `sm50_err` | reference: DXMT's own Direct3D 12 |

## Mullion (dttdrv/mullion)

- Base: [`695dbe2`](https://github.com/dttdrv/mullion/commit/695dbe2a6dfaf22ea74143e3e49ab39441f2c443) (30 October
  2026), upstream's main.
- Reviewed up to: `695dbe2` on main; 43 pull requests were open on 31 October 2026, all unmerged.

| Upstream | Taken as | State |
|---|---|---|
| PR [#101](https://github.com/dttdrv/mullion/pull/101) (`970d431`): airconv moves loads of device and constant memory ahead of their uses | `339b061` | cherry-picked |

## This fork's own commits

Commits on main that neither upstream has, oldest first.

| Commit | Subject |
|---|---|
| `224dfe9` | winemetal: keep each pipeline's GPU code in an archive of its own |
| `5c2d455` | winemetal: compile a missed pipeline once, and cap the pipeline cache |
| `b439baa` | winemetal: keep only the GPU's slice of each pipeline archive |
| `3b8beb5` | winemetal: remove the gpuarchiver folders the pipeline cache leaves |
| `1dea20e` | winemetal: drop the shader cache tables of other converter versions |
| `8a02f3f` | winemetal: raise the pipeline cache's default cap to 10 GB |
